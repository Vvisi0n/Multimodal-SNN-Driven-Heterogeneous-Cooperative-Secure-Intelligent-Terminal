#ifndef _MQTT_PROJ_H_
#define _MQTT_PROJ_H_

#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// MQTT 命令队列元素类型
#define MQTT_CMD_QUEUE_LEN 10
typedef struct {
    char cmd[128];
} mqtt_cmd_item_t;

/**
 * @brief MQTT 命令队列句柄（MQTT 任务推入，LVGL 定时器取出处理）
 */
extern QueueHandle_t mqtt_cmd_queue;

/**
 * @brief 初始化 WiFi 并连接 MQTT 服务器
 * @note  该函数内部包含 while() 循环，会进入阻塞状态，
 *        直到 WiFi 成功连接并获取到 IP 地址后才会继续向下执行，
 *        随后全自动初始化 MQTT 客户端、订阅预设主题并拉起异步发送任务。
 */
void app_mqtt_and_wifi_init(void *arg);

/**
 * @brief 全局默认 MQTT 主题（从云端配置获取，失败时为 "dpj_mqtt"）
 * @note  其他模块可直接使用此变量作为默认发布主题
 */
extern char g_mqtt_default_topic[64];

/**
 * @brief 向指定的 MQTT 主题异步发送消息
 * @param msg   要发送的字符串消息内容
 * @param topic 目标 MQTT 主题 (Topic)
 * @note  该接口为非阻塞设计，数据会安全地推入 FreeRTOS 队列中
 *        由后台专有任务处理发送。如果 MQTT 处于离线状态，数据包将被拦截丢弃。
 */
void mqtt_send_message(const char *msg, const char *topic);

/**
 * @brief 将当前传感器数据格式化为 JSON 并发送到默认主题
 * @note  由 LVGL 定时器每 1 秒调用一次
 */
void mqtt_send_sensor_json(void);

#ifdef __cplusplus
}
#endif

#endif /* _MQTT_PROJ_H_ */