#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "cjson.h"
#include "mqtt.h"
#include "utils.h"
#include "wifi.h"

// ── 云端 MQTT 配置地址 ──
#define MQTT_CONFIG_URL "https://cdn.codenews.cc/blog/9db8af8e5566f464c361e3365bc08431.json"

// ── 默认参数（云端获取失败时的 fallback） ──
#define DEFAULT_MQTT_SERVER_URI  "mqtts://4d291f35b96f49e0aaa91e929f888eb5.s1.eu.hivemq.cloud:8883"
#define DEFAULT_MQTT_USERNAME    "WGT_QIANSAI"
#define DEFAULT_MQTT_PASSWORD    "Qq112211!"
#define DEFAULT_MQTT_TOPIC       "dpj_mqtt"

// ── 运行时 MQTT 配置（优先从云端获取，失败则保持默认值） ──
static char g_mqtt_server_uri[128] = DEFAULT_MQTT_SERVER_URI;
static char g_mqtt_username[64]    = DEFAULT_MQTT_USERNAME;
static char g_mqtt_password[64]    = DEFAULT_MQTT_PASSWORD;
char g_mqtt_default_topic[64]      = DEFAULT_MQTT_TOPIC;

// ── HTTP 响应缓冲区（JSON 配置很小，512 字节足够） ──
#define HTTP_RX_BUF_SIZE 1024
static char g_http_rx_buf[HTTP_RX_BUF_SIZE];
static int  g_http_rx_len = 0;

// 订阅主题
static const char* subscribe_topics[] = {
    "cp_mqtt",
};
#define TOPICS_NUM (sizeof(subscribe_topics) / sizeof(subscribe_topics[0]))

// ==================== 全局变量 ====================
static const char *TAG = "MQTT_PROJ";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

// FreeRTOS 队列传输结构体
typedef struct {
    char topic[64];
    char data[256];
} mqtt_tx_msg_t;

static QueueHandle_t mqtt_tx_queue = NULL;

// MQTT 命令队列（MQTT 任务 → LVGL 定时器，避免在 MQTT 任务中调用 LVGL 函数）
QueueHandle_t mqtt_cmd_queue = NULL;

// ==================== HTTP 事件回调（用于拉取云端配置） ====================
static esp_err_t mqtt_config_http_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (g_http_rx_len + evt->data_len < HTTP_RX_BUF_SIZE - 1) {
                memcpy(g_http_rx_buf + g_http_rx_len, evt->data, evt->data_len);
                g_http_rx_len += evt->data_len;
                g_http_rx_buf[g_http_rx_len] = '\0';
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            break;
        case HTTP_EVENT_DISCONNECTED:
            break;
        case HTTP_EVENT_ERROR:
            break;
        default:
            break;
    }
    return ESP_OK;
}

// ==================== 从云端拉取 MQTT 配置 ====================
static void fetch_mqtt_config_from_cloud(void)
{
    ESP_LOGI(TAG, "Fetching MQTT config from cloud...");
    g_http_rx_len = 0;
    memset(g_http_rx_buf, 0, HTTP_RX_BUF_SIZE);

    esp_http_client_config_t http_cfg = {
        .url = MQTT_CONFIG_URL,
        .method = HTTP_METHOD_GET,
        .event_handler = mqtt_config_http_handler,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        ESP_LOGW(TAG, "HTTP client init failed, using default MQTT config");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status_code != 200) {
        ESP_LOGW(TAG, "Cloud config fetch failed (err=%d, http=%d), using defaults",
                 err, status_code);
        return;
    }

    ESP_LOGI(TAG, "Cloud config received: %s", g_http_rx_buf);

    cJSON *root = cJSON_Parse(g_http_rx_buf);
    if (root == NULL) {
        ESP_LOGW(TAG, "JSON parse failed, using defaults");
        return;
    }

    cJSON *item;

    item = cJSON_GetObjectItem(root, "mqtt_server_uri");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(g_mqtt_server_uri, item->valuestring, sizeof(g_mqtt_server_uri) - 1);
        ESP_LOGI(TAG, "  -> server_uri: %s", g_mqtt_server_uri);
    }

    item = cJSON_GetObjectItem(root, "mqtt_user");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(g_mqtt_username, item->valuestring, sizeof(g_mqtt_username) - 1);
        ESP_LOGI(TAG, "  -> username: %s", g_mqtt_username);
    }

    item = cJSON_GetObjectItem(root, "mqtt_pass");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(g_mqtt_password, item->valuestring, sizeof(g_mqtt_password) - 1);
        ESP_LOGI(TAG, "  -> password: %s", g_mqtt_password);
    }

    item = cJSON_GetObjectItem(root, "default_topic");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(g_mqtt_default_topic, item->valuestring, sizeof(g_mqtt_default_topic) - 1);
        ESP_LOGI(TAG, "  -> default_topic: %s", g_mqtt_default_topic);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Cloud config applied successfully.");
}

// ==================== MQTT 事件处理 ====================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT broker connected!");
            mqtt_connected = true;

            for (int i = 0; i < TOPICS_NUM; i++) {
                esp_mqtt_client_subscribe(event->client, subscribe_topics[i], 0);
                ESP_LOGI(TAG, "Subscribed to: %s", subscribe_topics[i]);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT broker disconnected!");
            mqtt_connected = false;
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "--------- [MQTT RECEIVED] ---------");
            ESP_LOGI(TAG, "TOPIC: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA : %.*s", event->data_len, event->data);

            if (event->data_len > 0 && mqtt_cmd_queue != NULL) {
                mqtt_cmd_item_t cmd_item;
                int copy_len = (event->data_len < (int)(sizeof(cmd_item.cmd) - 1))
                               ? event->data_len : (int)(sizeof(cmd_item.cmd) - 1);
                memcpy(cmd_item.cmd, event->data, copy_len);
                cmd_item.cmd[copy_len] = '\0';

                if (xQueueSend(mqtt_cmd_queue, &cmd_item, 0) != pdPASS) {
                    ESP_LOGW(TAG, "MQTT cmd queue full, dropping: %s", cmd_item.cmd);
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT event error");
            break;
        default:
            break;
    }
}

// ==================== 发送任务 ====================
static void mqtt_tx_task(void *arg)
{
    mqtt_tx_msg_t tx_msg;
    while (1) {
        if (xQueueReceive(mqtt_tx_queue, &tx_msg, portMAX_DELAY) == pdPASS) {
            if (mqtt_connected && mqtt_client != NULL) {
                int msg_id = esp_mqtt_client_publish(mqtt_client, tx_msg.topic, tx_msg.data, 0, 0, 0);
                if (msg_id != -1) {
                    ESP_LOGI(TAG, "Publish OK -> [Topic: %s] [Data: %s]", tx_msg.topic, tx_msg.data);
                } else {
                    ESP_LOGE(TAG, "Publish failed");
                }
            } else {
                ESP_LOGW(TAG, "MQTT offline, packet dropped.");
            }
        }
    }
}

// ==================== 对外发送接口 ====================
void mqtt_send_message(const char *msg, const char *topic)
{
    if (mqtt_tx_queue == NULL || msg == NULL || topic == NULL) return;

    mqtt_tx_msg_t tx_data;
    strncpy(tx_data.topic, topic, sizeof(tx_data.topic) - 1);
    tx_data.topic[sizeof(tx_data.topic) - 1] = '\0';
    strncpy(tx_data.data, msg, sizeof(tx_data.data) - 1);
    tx_data.data[sizeof(tx_data.data) - 1] = '\0';

    xQueueSend(mqtt_tx_queue, &tx_data, 0);
}

// ==================== 定时上报传感器 JSON ====================
void mqtt_send_sensor_json(void)
{
    if (!mqtt_connected || mqtt_client == NULL) return;

    char json[512];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"current\":%.2f,"
        "\"voltage\":%.2f,"
        "\"temperature\":%.1f,"
        "\"humidity\":%.1f,"
        "\"accelX\":%.2f,"
        "\"accelY\":%.2f,"
        "\"accelZ\":%.2f,"
        "\"gyroX\":%.2f,"
        "\"gyroY\":%.2f,"
        "\"gyroZ\":%.2f,"
        "\"micRaw\":%.1f,"
        "\"snr\":%.1f,"
        "\"position\":%.2f,"
        "\"rpm\":%d"
        "}",
        (double)g_sensor_current,              // current: mA
        (double)g_sensor_voltage,              // voltage: V
        (double)Temperature,
        (double)Humidity,
        (double)g_sensor_accel_x,
        (double)g_sensor_accel_y,
        (double)g_sensor_accel_z,
        0.0,   // gyroX: 暂无数据，预设
        0.0,   // gyroY: 暂无数据，预设
        0.0,   // gyroZ: 暂无数据，预设
        (double)MIC_ADC,
        0.0,   // snr: 暂无数据，预设
        (double)g_motor_position,
        g_motor_rotate_speed
    );

    if (len > 0 && len < (int)sizeof(json)) {
        mqtt_send_message(json, g_mqtt_default_topic);
    }
}

// ==================== 初始化与强制等待 ====================
// 注意：此函数作为 FreeRTOS 任务运行，绝对不能 return！
// 必须无限循环或调用 vTaskDelete(NULL) 退出
void app_mqtt_and_wifi_init(void *arg)
{
    // 等待WiFi连接
    while (is_wifi == 0) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "WiFi connected successfully.");

    // 尝试从云端拉取最新 MQTT 配置（失败则使用默认值）
    fetch_mqtt_config_from_cloud();

    // 打印完整 MQTT 配置，方便排查乱码/截断问题
    ESP_LOGI(TAG, "=== MQTT Config ===");
    ESP_LOGI(TAG, "  URI      : %s", g_mqtt_server_uri);
    ESP_LOGI(TAG, "  Username : %s", g_mqtt_username);
    ESP_LOGI(TAG, "  Password : %s", g_mqtt_password);
    ESP_LOGI(TAG, "  Topic    : %s", g_mqtt_default_topic);
    ESP_LOGI(TAG, "===================");

    // 创建发送队列和命令队列
    mqtt_tx_queue = xQueueCreate(10, sizeof(mqtt_tx_msg_t));
    mqtt_cmd_queue = xQueueCreate(MQTT_CMD_QUEUE_LEN, sizeof(mqtt_cmd_item_t));

    // 【关键修复】mqtt_cfg 必须用 static，不能放在栈上！
    // 因为此函数是 FreeRTOS 任务，即使 ESP-IDF 会深拷贝配置，
    // 但 crt_bundle_attach 等函数指针若拷贝异常，TLS 握手时会跳转到非法地址
    static esp_mqtt_client_config_t mqtt_cfg;
    memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
    mqtt_cfg.broker.address.uri = g_mqtt_server_uri;
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    mqtt_cfg.credentials.username = g_mqtt_username;
    mqtt_cfg.credentials.authentication.password = g_mqtt_password;
    mqtt_cfg.network.disable_auto_reconnect = false;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to init MQTT client! Task will retry...");
        // 清理队列后重试
        if (mqtt_tx_queue) { vQueueDelete(mqtt_tx_queue); mqtt_tx_queue = NULL; }
        if (mqtt_cmd_queue) { vQueueDelete(mqtt_cmd_queue); mqtt_cmd_queue = NULL; }
        vTaskDelay(pdMS_TO_TICKS(5000));
        // 不 return，而是让外层循环处理
        // 这里简化：删除任务，由 main 重新创建
        vTaskDelete(NULL);
        return;
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    // 创建发送任务
    xTaskCreate(mqtt_tx_task, "mqtt_tx_task", 4096, NULL, 5, NULL);

    // 等待 MQTT 握手（15 秒超时，海外 TLS 服务器需要更长时间）
    ESP_LOGI(TAG, "Waiting for MQTT handshake (timeout: 15s)...");
    int timeout_ms = 15000;
    int delay_ms = 100;

    while (!mqtt_connected && timeout_ms > 0) {
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
        timeout_ms -= delay_ms;
    }

    if (mqtt_connected) {
        ESP_LOGI(TAG, "MQTT handshake success! Ready to communicate.");
    } else {
        ESP_LOGE(TAG, "MQTT handshake timeout (15s expired)! Check network/credentials.");
        ESP_LOGE(TAG, "MQTT client will auto-reconnect in background (disable_auto_reconnect=false).");
    }

    // 【关键修复】任务函数绝对不能 return！
    // 让任务持续运行，监控 MQTT 连接状态，必要时可在此做重连逻辑
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // 定期打印连接状态（调试用）
        if (!mqtt_connected) {
            ESP_LOGW(TAG, "MQTT still disconnected, auto-reconnect is active...");
        }
    }
}