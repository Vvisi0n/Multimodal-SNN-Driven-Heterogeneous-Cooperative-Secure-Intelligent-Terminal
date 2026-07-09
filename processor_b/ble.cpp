#include <Arduino.h>
#include "ble.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

#define PROFILE_APP_ID  0

uint8_t  BLE_RxBuffer[BLE_BUF_SIZE];
uint16_t BLE_RxLen = 0;
uint8_t  BLE_NewDataFlag = 0;

static bool g_connected = false;
static bool g_ffe1_found = false;
static bool g_ffe2_found = false;
static uint16_t g_conn_id = 0;
static uint16_t g_ffe1_handle = 0;
static uint16_t g_ffe2_handle = 0;
static uint16_t g_svc_start = 0;
static uint16_t g_svc_end = 0;
static esp_gatt_if_t g_gattc_if = 0;
static esp_bd_addr_t g_target_bda;
static bool g_target_found = false;

static SemaphoreHandle_t g_scan_sem = NULL;
static SemaphoreHandle_t g_srvc_sem = NULL;

static const uint8_t SERVICE_UUID[16] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00
};

static const uint8_t CHAR_FFE1_UUID[16] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xE1, 0xFF, 0x00, 0x00
};

static const uint8_t CHAR_FFE2_UUID[16] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xE2, 0xFF, 0x00, 0x00
};

static bool uuid128_eq(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 16) == 0;
}

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        uint8_t adv_name_len = 0;
        uint8_t *adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                 ESP_BLE_AD_TYPE_NAME_CMPL,
                                 &adv_name_len);
        if (adv_name == NULL) {
            adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                                     ESP_BLE_AD_TYPE_NAME_SHORT,
                                     &adv_name_len);
        }

        char name[32] = {0};
        if (adv_name && adv_name_len > 0) {
            int len = adv_name_len < 31 ? adv_name_len : 31;
            memcpy(name, adv_name, len);
        }

        if (adv_name) {
            Serial.printf("[BLE] [%02X:%02X:%02X:%02X:%02X:%02X] name='%s' RSSI=%d\n",
                         param->scan_rst.bda[0], param->scan_rst.bda[1],
                         param->scan_rst.bda[2], param->scan_rst.bda[3],
                         param->scan_rst.bda[4], param->scan_rst.bda[5],
                         name, param->scan_rst.rssi);
        }

        if (strcmp(name, HOST_BLE_NAME) == 0) {
            Serial.printf("[BLE] >>> Target found! <<<\n");
            memcpy(g_target_bda, param->scan_rst.bda, ESP_BD_ADDR_LEN);
            g_target_found = true;
            esp_ble_gap_stop_scanning();
        }
        break;
    }
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (g_scan_sem) {
            xSemaphoreGive(g_scan_sem);
        }
        break;
    default:
        break;
    }
}

static void gattc_event_handler(esp_gattc_cb_event_t event,
                                 esp_gatt_if_t gattc_if,
                                 esp_ble_gattc_cb_param_t *param) {
    switch (event) {
    case ESP_GATTC_REG_EVT:
        g_gattc_if = gattc_if;
        Serial.printf("[BLE] GATTC registered\n");
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK) {
            g_conn_id = param->open.conn_id;
            Serial.printf("[BLE] Connected, discovering services...\n");
            esp_ble_gattc_search_service(gattc_if, g_conn_id, NULL);
        } else {
            Serial.printf("[BLE] Connection failed, status=%d\n", param->open.status);
        }
        break;

    case ESP_GATTC_SEARCH_RES_EVT: {
        if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128 &&
            uuid128_eq(param->search_res.srvc_id.uuid.uuid.uuid128, SERVICE_UUID)) {
            g_svc_start = param->search_res.start_handle;
            g_svc_end   = param->search_res.end_handle;
            Serial.printf("[BLE] Found target service (handle: %04x-%04x)\n",
                         g_svc_start, g_svc_end);
        }
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT:
        Serial.printf("[BLE] Service discovery complete\n");
        if (g_svc_start > 0 && g_svc_end > 0) {
            esp_bt_uuid_t uuid;
            uint16_t count = 1;
            esp_gattc_char_elem_t result;

            uuid.len = ESP_UUID_LEN_128;
            memcpy(uuid.uuid.uuid128, CHAR_FFE1_UUID, 16);
            if (esp_ble_gattc_get_char_by_uuid(gattc_if, g_conn_id,
                                               g_svc_start, g_svc_end,
                                               uuid, &result, &count) == ESP_OK && count > 0) {
                g_ffe1_handle = result.char_handle;
                g_ffe1_found = true;
                Serial.printf("[BLE] Found FFE1 (handle: %04x)\n", g_ffe1_handle);
            }

            count = 1;
            memcpy(uuid.uuid.uuid128, CHAR_FFE2_UUID, 16);
            if (esp_ble_gattc_get_char_by_uuid(gattc_if, g_conn_id,
                                               g_svc_start, g_svc_end,
                                               uuid, &result, &count) == ESP_OK && count > 0) {
                g_ffe2_handle = result.char_handle;
                g_ffe2_found = true;
                Serial.printf("[BLE] Found FFE2 (handle: %04x)\n", g_ffe2_handle);
            }
        }
        if (g_srvc_sem) {
            xSemaphoreGive(g_srvc_sem);
        }
        break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        if (param->reg_for_notify.status == ESP_GATT_OK) {
            Serial.printf("[BLE] Registered for notifications, writing CCCD...\n");
            uint8_t cccd_val[2] = {0x01, 0x00};
            esp_ble_gattc_write_char_descr(gattc_if, g_conn_id,
                                           param->reg_for_notify.handle,
                                           2, cccd_val,
                                           ESP_GATT_WRITE_TYPE_NO_RSP,
                                           ESP_GATT_AUTH_REQ_NONE);
        }
        break;

    case ESP_GATTC_NOTIFY_EVT:
        if (param->notify.value_len <= BLE_BUF_SIZE) {
            BLE_RxLen = param->notify.value_len;
            memset(BLE_RxBuffer, 0, BLE_BUF_SIZE);
            memcpy(BLE_RxBuffer, param->notify.value, param->notify.value_len);
            BLE_NewDataFlag = 1;
        }
        break;

    case ESP_GATTC_WRITE_DESCR_EVT:
        if (param->write.status == ESP_GATT_OK) {
            g_connected = true;
            Serial.printf("[BLE] Central ready!\n");
        }
        break;

    case ESP_GATTC_WRITE_CHAR_EVT:
        if (param->write.status == ESP_GATT_OK) {
            Serial.printf("[BLE] Sent to FFE1 OK\n");
        }
        break;

    case ESP_GATTC_CLOSE_EVT:
        Serial.printf("[BLE] Connection closed\n");
        g_connected = false;
        g_conn_id = 0;
        g_ffe1_handle = 0;
        g_ffe2_handle = 0;
        g_ffe1_found = false;
        g_ffe2_found = false;
        break;

    default:
        break;
    }
}

static void scan_task(void* pvParameters) {
    (void)pvParameters;

    while (1) {
        if (g_connected) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        g_target_found = false;
        memset(g_target_bda, 0, ESP_BD_ADDR_LEN);

        Serial.printf("[BLE] Scanning for '%s'...\n", HOST_BLE_NAME);

        esp_ble_scan_params_t scan_params = {
            .scan_type          = BLE_SCAN_TYPE_PASSIVE,
            .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
            .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
            .scan_interval      = 0x50,
            .scan_window        = 0x30,
            .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE
        };

        esp_ble_gap_set_scan_params(&scan_params);
        esp_ble_gap_start_scanning(5);

        xSemaphoreTake(g_scan_sem, pdMS_TO_TICKS(6000));

        if (g_target_found && g_gattc_if != 0) {
            Serial.printf("[BLE] Connecting to " \
                         "%02X:%02X:%02X:%02X:%02X:%02X...\n",
                         g_target_bda[0], g_target_bda[1],
                         g_target_bda[2], g_target_bda[3],
                         g_target_bda[4], g_target_bda[5]);

            esp_ble_gattc_open(g_gattc_if, g_target_bda,
                               BLE_ADDR_TYPE_PUBLIC, true);

            xSemaphoreTake(g_srvc_sem, pdMS_TO_TICKS(10000));

            if (g_ffe2_found && g_ffe2_handle > 0) {
                esp_ble_gattc_register_for_notify(g_gattc_if,
                                                  g_target_bda,
                                                  g_ffe2_handle);
            } else {
                Serial.printf("[BLE] Warning: FFE2 not found\n");
            }
        } else {
            Serial.printf("[BLE] Target not found, retrying in 1s...\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    vTaskDelete(NULL);
}

void BLE_Init(void) {
    Serial.printf("[BLE] Initializing BLE Central...\n");

    g_scan_sem = xSemaphoreCreateBinary();
    g_srvc_sem = xSemaphoreCreateBinary();

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gattc_register_callback(gattc_event_handler);
    esp_ble_gattc_app_register(PROFILE_APP_ID);

    esp_ble_gatt_set_local_mtu(200);

    xTaskCreate(scan_task, "ble_scan", 4096, NULL, 1, NULL);
    Serial.printf("[BLE] BLE Central initialized\n");
}

void BLE_Send(uint8_t *data, uint16_t len) {
    if (!g_connected || len == 0 || data == NULL) {
        return;
    }

    if (!g_ffe1_found || g_ffe1_handle == 0) {
        return;
    }

    if (len > BLE_BUF_SIZE) {
        len = BLE_BUF_SIZE;
    }

    esp_ble_gattc_write_char(g_gattc_if, g_conn_id, g_ffe1_handle,
                             len, data,
                             ESP_GATT_WRITE_TYPE_NO_RSP,
                             ESP_GATT_AUTH_REQ_NONE);
}

uint8_t BLE_IsConnected(void) {
    return g_connected ? 1 : 0;
}