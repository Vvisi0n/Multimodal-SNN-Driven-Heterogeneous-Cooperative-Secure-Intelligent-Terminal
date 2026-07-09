#include "wifi_udp.h"
#include "wifi.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_netif.h"
#include "utils.h"

#define UDP_PORT         8888

static const char *TAG = "UDP";
static int udp_socket = -1;
static struct sockaddr_in dest_addr;

// 自动保存电脑IP（网关 = 你的电脑）
static uint32_t auto_pc_ip = 0;

// IP事件
static void ip_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip = (ip_event_got_ip_t*)data;
        auto_pc_ip = ip->ip_info.gw.addr;
        ESP_LOGI(TAG, "MCU Local IP: %s", inet_ntoa(ip->ip_info.ip));
        ESP_LOGI(TAG, "Gateway(PC) IP: %s", inet_ntoa(ip->ip_info.gw));
    }
}

// UDP接收任务
static void udp_recv_task(void *arg)
{
    char buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);

    while (1) {
        int len = recvfrom(udp_socket, buf, sizeof(buf)-1, MSG_DONTWAIT,
                           (struct sockaddr*)&from, &from_len);
        if (len > 0) {
            buf[len] = 0;
            ESP_LOGI(TAG, "PC: %s", buf);
            Command_Analysis(buf);
            // 发送确认包
            udp_send2com("Rev Success");
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

// 初始化函数
void wifi_udp_init(void)
{
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL, NULL);

    // 等待WiFi连接
    while (is_wifi == 0) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelay(300 / portTICK_PERIOD_MS);

    // Create UDP socket
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket! errno: %d", errno);
        return;
    }

    int broadcast = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in local_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(UDP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(udp_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind port %d! errno: %d", UDP_PORT, errno);
        close(udp_socket);
        return;
    }

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_PORT);
    dest_addr.sin_addr.s_addr = auto_pc_ip;

    ESP_LOGI(TAG, "Port bind success, creating RX task...");

    BaseType_t err = xTaskCreate(udp_recv_task, "udp_recv_task", 8192, NULL, 4, NULL);
    if (err != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task! error: %d", err);
    } else {
        ESP_LOGI(TAG, "UDP receive task started successfully!");
    }
}

// 发送函数
void udp_send2com(const char *fmt, ...)
{
    if (!is_wifi || udp_socket < 0) return;

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    sendto(udp_socket, buf, strlen(buf), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
}