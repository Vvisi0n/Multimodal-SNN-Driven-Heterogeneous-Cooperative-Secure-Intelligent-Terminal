/**
 * @file    mqtt.cpp
 * @brief   MQTT 客户端实现：云端配置拉取 + Broker 连接 + 话题收发
 *
 * 架构：WiFiClientSecure（TLS） + PubSubClient（MQTT 协议）
 * 线程安全：通过 FreeRTOS 互斥锁保护，Core0 收 / Core1 发
 * 连接流程：NTP 同步 → 拉取云端配置 → 解析 URI → 连接 Broker → 订阅话题
 */

#include "mqtt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_http_client.h"
#include "esp_tls.h"

/* --------------------------------------------------------------------------
   订阅主题列表（最多 5 个，NULL 结束）
   -------------------------------------------------------------------------- */
static const char* g_subscribe_topics[MQTT_MAX_SUBSCRIBE_TOPICS] = {
    "cp_mqtt",
    NULL, NULL, NULL, NULL
};

/* --------------------------------------------------------------------------
   默认配置（云端拉取失败时的兜底参数）
   -------------------------------------------------------------------------- */
static char g_broker_uri[128] = DEFAULT_MQTT_SERVER_URI;
static char g_username[64]   = DEFAULT_MQTT_USERNAME;
static char g_password[64]   = DEFAULT_MQTT_PASSWORD;
static char g_pub_topic[64]  = DEFAULT_MQTT_TOPIC;

/* --------------------------------------------------------------------------
   MQTT 客户端全局状态
   -------------------------------------------------------------------------- */
static WiFiClientSecure  g_wifi_secure;
static PubSubClient      g_pubsub(g_wifi_secure);
static SemaphoreHandle_t g_mqtt_mutex = NULL;    // 线程安全锁
static mqtt_callback_t   g_callback = NULL;       // 消息回调
static bool              g_connected = false;     // 连接状态
static char              g_mqtt_host[128] = {0};  // 解析后的 Broker 地址
static uint16_t          g_mqtt_port = 8883;      // 解析后的 Broker 端口
static char              g_client_id[32] = {0};   // 客户端 ID

/* --------------------------------------------------------------------------
   简易 JSON 解析：从 JSON 字符串中提取 key 对应的字符串值
   仅支持 {"key": "value"} 格式，不处理嵌套和数组
   -------------------------------------------------------------------------- */
static bool json_get_string(const char* json, const char* key, char* out, size_t out_len) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return false;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;

    if (*pos != '"') return false;
    pos++;

    size_t i = 0;
    while (*pos && *pos != '"' && i < out_len - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return i > 0;
}

/* --------------------------------------------------------------------------
   HTTP 事件回调：接收云端配置 JSON 到缓冲区
   -------------------------------------------------------------------------- */
static char g_json_buf[1024];
static int  g_json_len = 0;

static esp_err_t http_event_cb(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (g_json_len + evt->data_len < (int)sizeof(g_json_buf) - 1) {
            memcpy(g_json_buf + g_json_len, evt->data, evt->data_len);
            g_json_len += evt->data_len;
            g_json_buf[g_json_len] = '\0';
        }
    }
    return ESP_OK;
}

/* --------------------------------------------------------------------------
   从云端拉取 MQTT 配置（覆盖默认值）
   返回：true=成功覆盖，false=使用默认值
   -------------------------------------------------------------------------- */
static bool fetch_cloud_config(void) {
    g_json_len = 0;
    memset(g_json_buf, 0, sizeof(g_json_buf));

    Serial.printf("[MQTT] 正在拉取云端配置：%s\n", MQTT_CONFIG_URL);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = MQTT_CONFIG_URL;
    http_cfg.event_handler = http_event_cb;
    http_cfg.timeout_ms = 10000;
    http_cfg.skip_cert_common_name_check = true;
    http_cfg.use_global_ca_store = true;

    esp_http_client_handle_t http = esp_http_client_init(&http_cfg);
    if (!http) {
        Serial.printf("[MQTT] HTTP 初始化失败，使用默认配置\n");
        return false;
    }

    esp_err_t err = esp_http_client_perform(http);
    int status = esp_http_client_get_status_code(http);
    int content_len = esp_http_client_get_content_length(http);
    esp_http_client_cleanup(http);

    Serial.printf("[MQTT] HTTP 结果：err=%d  status=%d  content_length=%d\n",
                  err, status, content_len);
    Serial.printf("[MQTT] 原始响应体 (%d 字节)：%s\n", g_json_len,
                  g_json_len > 0 ? g_json_buf : "(空)");

    if (err != ESP_OK || status != 200) {
        Serial.printf("[MQTT] 云端配置拉取失败，使用默认配置\n");
        return false;
    }

    // 解析 JSON 字段，存在则覆盖默认值
    char buf[128];
    if (json_get_string(g_json_buf, "mqtt_server_uri", buf, sizeof(buf))) {
        Serial.printf("[MQTT] 解析到 mqtt_server_uri = %s\n", buf);
        strncpy(g_broker_uri, buf, sizeof(g_broker_uri) - 1);
    }
    if (json_get_string(g_json_buf, "mqtt_user", buf, sizeof(buf))) {
        Serial.printf("[MQTT] 解析到 mqtt_user = %s\n", buf);
        strncpy(g_username, buf, sizeof(g_username) - 1);
    }
    if (json_get_string(g_json_buf, "mqtt_pass", buf, sizeof(buf))) {
        Serial.printf("[MQTT] 解析到 mqtt_pass = %s\n", buf);
        strncpy(g_password, buf, sizeof(g_password) - 1);
    }
    if (json_get_string(g_json_buf, "default_topic", buf, sizeof(buf))) {
        Serial.printf("[MQTT] 解析到 default_topic = %s\n", buf);
        strncpy(g_pub_topic, buf, sizeof(g_pub_topic) - 1);
    }

    return true;
}

/* --------------------------------------------------------------------------
   URI 解析：从 mqtts://host:port 格式中提取 host 和 port
   -------------------------------------------------------------------------- */
static void parse_mqtt_uri(const char* uri, char* host, size_t host_len, uint16_t* port) {
    const char* p = strstr(uri, "://");
    if (p) p += 3; else p = uri;

    const char* colon = strrchr(p, ':');
    if (colon) {
        *port = (uint16_t)atoi(colon + 1);
        size_t len = colon - p;
        if (len >= host_len) len = host_len - 1;
        memcpy(host, p, len);
        host[len] = '\0';
    } else {
        *port = 8883;
        strncpy(host, p, host_len - 1);
        host[host_len - 1] = '\0';
    }
}

/* --------------------------------------------------------------------------
   PubSubClient 回调适配：将收到的消息转发给上层注册的回调
   -------------------------------------------------------------------------- */
static void pubsub_callback(char* topic, byte* payload, unsigned int length) {
    Serial.printf("[MQTT] 接收：topic=%s payload=%.*s\n", topic, length, payload);

    if (g_callback) {
        char payload_str[512];
        int copy_len = length < (int)sizeof(payload_str) - 1
                     ? (int)length : (int)sizeof(payload_str) - 1;
        memcpy(payload_str, payload, copy_len);
        payload_str[copy_len] = '\0';
        g_callback(topic, payload_str, (int)length);
    }
}

/* --------------------------------------------------------------------------
   公共接口
   -------------------------------------------------------------------------- */

/*
 * 初始化 MQTT 客户端
 * 流程：NTP 时间同步 → 全局 CA 证书存储 → 拉取云端配置
 *        → 解析 Broker URI → 连接 → 遍历订阅所有话题
 */
bool mqtt_init(void) {
    Serial.printf("[MQTT] 可用堆内存：%u 字节\n", ESP.getFreeHeap());

    // 创建互斥锁（Core0 接收 / Core1 发送，保证线程安全）
    if (!g_mqtt_mutex) {
        g_mqtt_mutex = xSemaphoreCreateMutex();
    }

    // NTP 时间同步（TLS 证书验证依赖正确时间）
    static bool time_synced = false;
    if (!time_synced) {
        configTime(8 * 3600, 0, "ntp.aliyun.com", "time.google.com", "pool.ntp.org");
        Serial.printf("[MQTT] 正在同步 NTP 时间...\n");
        int retry = 0;
        while (time(nullptr) < 1000000000 && retry < 30) {
            delay(500);
            retry++;
        }
        if (time(nullptr) >= 1000000000) {
            time_synced = true;
            Serial.printf("[MQTT] 时间同步完成\n");
        } else {
            Serial.printf("[MQTT] 时间同步超时\n");
        }
    }

    // 初始化全局 CA 证书存储（HTTP 拉取配置用）
    static bool ca_store_inited = false;
    if (!ca_store_inited) {
        esp_err_t ca_err = esp_tls_init_global_ca_store();
        if (ca_err == ESP_OK) {
            ca_store_inited = true;
            Serial.printf("[MQTT] 全局 CA 证书存储初始化完成\n");
        } else {
            Serial.printf("[MQTT] 全局 CA 证书存储初始化失败：%d\n", ca_err);
        }
    }

    // 拉取云端配置（失败则使用默认值）
    fetch_cloud_config();

    Serial.printf("[MQTT] HTTP 请求后可用堆内存：%u 字节\n", ESP.getFreeHeap());

    // 构建客户端 ID
    if (g_client_id[0] == '\0') {
        snprintf(g_client_id, sizeof(g_client_id), "ESP32S3_%s",
                 WiFi.macAddress().c_str());
    }

    // 解析 URI → host + port
    parse_mqtt_uri(g_broker_uri, g_mqtt_host, sizeof(g_mqtt_host), &g_mqtt_port);

    Serial.printf("[MQTT] 主机地址：%s\n", g_mqtt_host);
    Serial.printf("[MQTT] 端口号  ：%u\n", g_mqtt_port);
    Serial.printf("[MQTT] 客户端ID：%s\n", g_client_id);
    Serial.printf("[MQTT] 用户名  ：%s\n", g_username);
    Serial.printf("[MQTT] 发布话题：%s\n", g_pub_topic);

    // 配置 TLS（跳过证书验证，适配 HiveMQ Cloud）
    g_wifi_secure.setInsecure();

    // 配置 PubSubClient
    g_pubsub.setServer(g_mqtt_host, g_mqtt_port);
    g_pubsub.setCallback(pubsub_callback);
    g_pubsub.setKeepAlive(60);

    // 连接 Broker 并订阅所有话题
    Serial.printf("[MQTT] 正在连接 Broker...\n");
    if (g_pubsub.connect(g_client_id, g_username, g_password)) {
        g_connected = true;
        // 遍历订阅所有非 NULL 话题
        for (int i = 0; i < MQTT_MAX_SUBSCRIBE_TOPICS; i++) {
            if (g_subscribe_topics[i]) {
                g_pubsub.subscribe(g_subscribe_topics[i]);
                Serial.printf("[MQTT] 已订阅：%s\n", g_subscribe_topics[i]);
            }
        }
        Serial.printf("[MQTT] 连接成功\n");
        return true;
    } else {
        g_connected = false;
        Serial.printf("[MQTT] 连接失败，状态码=%d\n", g_pubsub.state());
        return false;
    }
}

/*
 * 查询连接状态（线程安全）
 */
bool mqtt_is_connected(void) {
    if (!g_mqtt_mutex) return false;
    xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
    bool ret = g_connected;
    xSemaphoreGive(g_mqtt_mutex);
    return ret;
}

/*
 * 发布消息到指定话题（线程安全）
 * 返回：1=成功，-1=失败
 */
int mqtt_publish(const char* topic, const char* payload) {
    if (!g_mqtt_mutex) return -1;
    xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
    if (!g_connected) {
        xSemaphoreGive(g_mqtt_mutex);
        return -1;
    }
    bool ok = g_pubsub.publish(topic, payload);
    xSemaphoreGive(g_mqtt_mutex);
    return ok ? 1 : -1;
}

/*
 * 注册消息回调函数
 */
void mqtt_set_callback(mqtt_callback_t cb) {
    g_callback = cb;
}

/*
 * MQTT 主循环（需在 loop() 中高频调用）
 * 负责：保持连接保活、断线重连、接收消息
 */
void mqtt_poll(void) {
    if (!g_mqtt_mutex) return;
    xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);

    g_pubsub.loop();

    if (!g_pubsub.connected()) {
        if (g_connected) {
            g_connected = false;
            xSemaphoreGive(g_mqtt_mutex);
            Serial.printf("[MQTT] 连接断开\n");
            xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
        }

        // 每隔 5 秒尝试重连一次
        static unsigned long last_reconnect = 0;
        unsigned long now = millis();
        if (now - last_reconnect > 5000) {
            last_reconnect = now;
            xSemaphoreGive(g_mqtt_mutex);
            Serial.printf("[MQTT] 正在重连...\n");
            xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
            if (g_pubsub.connect(g_client_id, g_username, g_password)) {
                g_connected = true;
                // 重连后重新订阅所有话题
                for (int i = 0; i < MQTT_MAX_SUBSCRIBE_TOPICS; i++) {
                    if (g_subscribe_topics[i]) {
                        g_pubsub.subscribe(g_subscribe_topics[i]);
                    }
                }
                xSemaphoreGive(g_mqtt_mutex);
                Serial.printf("[MQTT] 重连成功\n");
                xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
            } else {
                xSemaphoreGive(g_mqtt_mutex);
                Serial.printf("[MQTT] 重连失败，状态码=%d\n", g_pubsub.state());
                xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
            }
        }
    }

    xSemaphoreGive(g_mqtt_mutex);
}