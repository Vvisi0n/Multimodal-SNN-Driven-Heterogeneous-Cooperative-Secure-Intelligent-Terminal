/**
 * @file    mqtt.h
 * @brief   MQTT 客户端：从云端拉取配置，连接 HiveMQ Cloud Broker
 *
 * 支持多订阅主题（最多 5 个），默认订阅 cp_mqtt
 * 首次连接前从 MQTT_CONFIG_URL 拉取动态配置，失败则使用默认值
 */

#ifndef MQTT_H
#define MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// 云端 MQTT 配置地址
#define MQTT_CONFIG_URL "https://cdn.codenews.cc/blog/9db8af8e5566f464c361e3365bc08431.json"

// 默认参数（云端获取失败时的 fallback）
#define DEFAULT_MQTT_SERVER_URI  "mqtts://4d291f35b96f49e0aaa91e929f888eb5.s1.eu.hivemq.cloud:8883"
#define DEFAULT_MQTT_USERNAME    "WGT_QIANSAI"
#define DEFAULT_MQTT_PASSWORD    "Qq112211!"
#define DEFAULT_MQTT_TOPIC       "dpj_vis_mqtt"

#define MQTT_MAX_TOPIC_LEN          64
#define MQTT_MAX_SUBSCRIBE_TOPICS   5

// 配置结构体
typedef struct {
    char mqtt_host[128];
    int  mqtt_port;
    char mqtt_user[64];
    char mqtt_pass[64];
    char default_topic[MQTT_MAX_TOPIC_LEN];
    bool tls;
} mqtt_config_t;

// 消息回调原型
typedef void (*mqtt_callback_t)(const char* topic, const char* payload, int len);

// 公开接口
bool mqtt_init(void);
bool mqtt_is_connected(void);
int  mqtt_publish(const char* topic, const char* payload);
void mqtt_set_callback(mqtt_callback_t cb);
void mqtt_poll(void);

#ifdef __cplusplus
}
#endif

#endif