#include "ble.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>


/* ================================================================
 *  连接状态
 * ================================================================ */
typedef enum {
    ST_IDLE = 0,
    ST_READY
} ble_state_t;

typedef struct {
    esp_bd_addr_t addr;
    uint16_t      conn_id;
    uint16_t      rx_handle;
    ble_state_t   state;
    esp_gatt_if_t gatt_if;
} ble_conn_t;

static ble_conn_t s_conns[MAX_BLE_CONNECTIONS];

/* ================================================================
 *  GATTS 句柄
 * ================================================================ */
static uint16_t s_svc_handle     = 0;
static uint16_t s_rx_char_handle = 0;  /* notify: 我们发 -> 对方收 */
static uint16_t s_tx_char_handle = 0;  /* write:  对方发 -> 我们收 */
static esp_gatt_if_t s_gatts_if  = ESP_GATT_IF_NONE;
static bool s_adv_active = false;

/* ================================================================
 *  接收缓冲区
 * ================================================================ */
uint8_t  BLE_RxBuffer[BLE_BUF_SIZE];
uint16_t BLE_RxLen       = 0;
uint8_t  BLE_NewDataFlag = 0;
uint8_t  BLE_ConnIndex   = 0;
uint8_t  BLE_DebugEnable = 0;

/* ================================================================
 *  工具函数
 * ================================================================ */
static int find_by_conn_id(uint16_t cid) {
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (s_conns[i].state != ST_IDLE && s_conns[i].conn_id == cid)
            return i;
    }
    return -1;
}
static int find_free(void) {
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (s_conns[i].state == ST_IDLE) return i;
    }
    return -1;
}
static void rx_to_buf(int idx, const uint8_t *data, uint16_t len) {
    size_t n = (len < BLE_BUF_SIZE - 1) ? len : (BLE_BUF_SIZE - 1);
    memcpy(BLE_RxBuffer, data, n);
    BLE_RxBuffer[n] = '\0';
    BLE_RxLen       = n;
    BLE_NewDataFlag = 1;
    BLE_ConnIndex   = (uint8_t)idx;
    Serial.printf("[BLE_RX:%d|%02x:%02x:%02x:%02x:%02x:%02x] '%s' (len=%d)\r\n",
                  idx,
                  s_conns[idx].addr[0], s_conns[idx].addr[1],
                  s_conns[idx].addr[2], s_conns[idx].addr[3],
                  s_conns[idx].addr[4], s_conns[idx].addr[5],
                  BLE_RxBuffer, n);
    /* 回声：调试模式下把收到的数据原样发回去 */
    if (BLE_DebugEnable) {
        BLE_Send(idx, (uint8_t *)BLE_RxBuffer, n);
    }
}

/* ================================================================
 *  GAP 回调（处理安全请求）
 * ================================================================ */
static void gap_cb(esp_gap_ble_cb_event_t evt, esp_ble_gap_cb_param_t *p) {
    switch (evt) {
    case ESP_GAP_BLE_SEC_REQ_EVT:
        Serial.println("[BLE] Security request received, accepting (Just Works)");
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (p->ble_security.auth_cmpl.success) {
            Serial.println("[BLE] Pairing OK");
        } else {
            Serial.printf("[BLE] Pairing failed, reason=0x%x\r\n",
                          p->ble_security.auth_cmpl.fail_reason);
        }
        break;
    default:
        break;
    }
}

/* ================================================================
 *  GATTS 回调（Peripheral：别人连我们）
 * ================================================================ */
static void gatts_cb(esp_gatts_cb_event_t evt, esp_gatt_if_t gatts_if,
                     esp_ble_gatts_cb_param_t *p) {
    switch (evt) {
    case ESP_GATTS_REG_EVT: {
        s_gatts_if = gatts_if;
        esp_bt_uuid_t svc_uuid = { .len = ESP_UUID_LEN_16 };
        svc_uuid.uuid.uuid16 = BLE_SERVICE_UUID16;
        esp_gatt_srvc_id_t srvc_id = {
            .id = { .uuid = svc_uuid, .inst_id = 0 },
            .is_primary = true
        };
        esp_ble_gatts_create_service(gatts_if, &srvc_id, 8);
        break;
    }
    case ESP_GATTS_CREATE_EVT: {
        s_svc_handle = p->create.service_handle;

        /* 手机写通道 (FFE1): 手机发 -> ESP32 收 */
        esp_bt_uuid_t u = { .len = ESP_UUID_LEN_16 };
        u.uuid.uuid16 = BLE_CHAR_RX_UUID16;
        esp_ble_gatts_add_char(s_svc_handle, &u,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE |
                               ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                               NULL, NULL);

        /* ESP32 通知通道 (FFE2): ESP32 发 -> 手机收 */
        u.uuid.uuid16 = BLE_CHAR_TX_UUID16;
        esp_ble_gatts_add_char(s_svc_handle, &u,
                               ESP_GATT_PERM_READ,
                               ESP_GATT_CHAR_PROP_BIT_READ |
                               ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                               NULL, NULL);
        break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
        if (p->add_char.char_uuid.uuid.uuid16 == BLE_CHAR_TX_UUID16) {
            s_rx_char_handle = p->add_char.attr_handle;
            esp_bt_uuid_t desc_uuid = { .len = ESP_UUID_LEN_16 };
            desc_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            esp_ble_gatts_add_char_descr(
                s_svc_handle, &desc_uuid,
                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                NULL, NULL);
        }
        if (p->add_char.char_uuid.uuid.uuid16 == BLE_CHAR_RX_UUID16) {
            s_tx_char_handle = p->add_char.attr_handle;
        }
        break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        esp_ble_gatts_start_service(s_svc_handle);
        break;

    case ESP_GATTS_START_EVT: {
        /* 广播数据：Flags + Service UUID + 设备名称 */
        uint8_t adv_raw[32];
        int pos = 0;

        adv_raw[pos++] = 2;
        adv_raw[pos++] = 0x01;
        adv_raw[pos++] = 0x06;

        adv_raw[pos++] = 3;
        adv_raw[pos++] = 0x03;
        adv_raw[pos++] = (uint8_t)(BLE_SERVICE_UUID16 & 0xFF);
        adv_raw[pos++] = (uint8_t)((BLE_SERVICE_UUID16 >> 8) & 0xFF);

        int name_len = strlen(BLE_DEVICE_NAME);
        adv_raw[pos++] = name_len + 1;
        adv_raw[pos++] = 0x09;
        memcpy(&adv_raw[pos], BLE_DEVICE_NAME, name_len);
        pos += name_len;

        esp_ble_gap_config_adv_data_raw(adv_raw, pos);

        esp_ble_adv_params_t adv = {
            .adv_int_min        = 0x20,
            .adv_int_max        = 0x40,
            .adv_type           = ADV_TYPE_IND,
            .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
            .channel_map        = ADV_CHNL_ALL,
            .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        esp_ble_gap_start_advertising(&adv);
        s_adv_active = true;
        Serial.println("[BLE] Advertising started");
        break;
    }

    case ESP_GATTS_CONNECT_EVT: {
        int slot = find_free();
        if (slot < 0) {
            Serial.println("[BLE] Rejected: all slots full");
            break;
        }
        memcpy(s_conns[slot].addr, p->connect.remote_bda, ESP_BD_ADDR_LEN);
        s_conns[slot].conn_id   = p->connect.conn_id;
        s_conns[slot].rx_handle = s_rx_char_handle;
        s_conns[slot].state     = ST_READY;
        s_conns[slot].gatt_if   = s_gatts_if;
        Serial.printf("[BLE] Connected -> slot %d (%02x:%02x:%02x:%02x:%02x:%02x)\r\n",
                      slot, p->connect.remote_bda[0], p->connect.remote_bda[1],
                      p->connect.remote_bda[2], p->connect.remote_bda[3],
                      p->connect.remote_bda[4], p->connect.remote_bda[5]);
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT: {
        int i = find_by_conn_id(p->disconnect.conn_id);
        if (i < 0) break;
        Serial.printf("[BLE] Disconnected -> slot %d\r\n", i);
        s_conns[i].state     = ST_IDLE;
        s_conns[i].rx_handle = 0;
        /* 重新广播 */
        esp_ble_adv_params_t adv = {
            .adv_int_min        = 0x20,
            .adv_int_max        = 0x40,
            .adv_type           = ADV_TYPE_IND,
            .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
            .channel_map        = ADV_CHNL_ALL,
            .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        esp_ble_gap_start_advertising(&adv);
        s_adv_active = true;
        break;
    }

    case ESP_GATTS_WRITE_EVT: {
        int i = find_by_conn_id(p->write.conn_id);
        if (i < 0) break;

        if (p->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, p->write.conn_id,
                                        p->write.trans_id, ESP_GATT_OK, NULL);
        }

        if (p->write.handle == s_tx_char_handle) {
            rx_to_buf(i, p->write.value, p->write.len);
        } else {
            Serial.printf("[BLE] Write to handle 0x%04x (not TX char, ignored)\r\n",
                          p->write.handle);
        }
        break;
    }

    case ESP_GATTS_READ_EVT: {
        Serial.printf("[BLE] Read request on handle 0x%04x\r\n", p->read.handle);
        break;
    }

    case ESP_GATTS_MTU_EVT:
        Serial.printf("[BLE] MTU exchanged: %d\r\n", p->mtu.mtu);
        break;

    default: break;
    }
}

/* ================================================================
 *  BLE_Init
 * ================================================================ */
void BLE_Init(void) {
    memset(s_conns, 0, sizeof(s_conns));
    memset(BLE_RxBuffer, 0, BLE_BUF_SIZE);
    BLE_RxLen       = 0;
    BLE_NewDataFlag = 0;
    BLE_ConnIndex   = 0;

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gap_register_callback(gap_cb);
    esp_ble_gatts_register_callback(gatts_cb);
    esp_ble_gatts_app_register(1);

    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));

    uint8_t auth_req = ESP_LE_AUTH_NO_BOND;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));

    uint8_t key_size = 16;
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));

    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,  &rsp_key,  sizeof(uint8_t));

    esp_bt_dev_set_device_name(BLE_DEVICE_NAME);

    Serial.println("[BLE] Init OK (Peripheral)");
}

/* ================================================================
 *  BLE_Send
 * ================================================================ */
void BLE_Send(uint8_t idx, uint8_t *data, uint16_t len) {
    if (idx >= MAX_BLE_CONNECTIONS) return;
    if (s_conns[idx].state != ST_READY) return;
    if (s_conns[idx].rx_handle == 0) return;

    esp_ble_gatts_send_indicate(s_conns[idx].gatt_if, s_conns[idx].conn_id,
                                s_conns[idx].rx_handle, len, data, false);
}

void BLE_Broadcast(uint8_t *data, uint16_t len) {
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++)
        BLE_Send(i, data, len);
}

/* ================================================================
 *  BLE_Task（空闲，仅保活） 
 * ================================================================ */
static void BLE_Task(void *parameter) {
    (void)parameter;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void BLE_CreateTask(void) {
    xTaskCreatePinnedToCore(BLE_Task, "BLE_Task", 4096, NULL, 1, NULL, 0);
}