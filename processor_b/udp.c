#include "udp.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "tcpip_adapter.h"
#include "img_converters.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "udp_cam";

#define UDP_PORT           5555
#define CHUNK_DATA_SIZE    1024
#define DATA_PACKS_PER_GROUP  4    // 每组数据包数
#define REDUN_PACKS_PER_GROUP 1    // 每组冗余包数

// --------------- 新包头（8字节） ---------------
#pragma pack(push, 1)
typedef struct {
    uint16_t frame_id;       // 帧序号
    uint16_t chunk_index;    // 数据包: 0..total_chunks-1; 冗余包: 0xFFFF
    uint16_t total_chunks;   // 该帧数据包总数（不含冗余）
    uint16_t jpeg_len;       // JPEG 原始数据长度（用于截断）
} udp_chunk_header_t;
#pragma pack(pop)

static int udp_socket = -1;
static struct sockaddr_in target_addr;
static uint16_t frame_counter = 0;
static SemaphoreHandle_t camera_mutex = NULL;

#define SHARED_JPEG_MAX 65536
static uint8_t  shared_jpeg_buf[SHARED_JPEG_MAX];
static size_t   shared_jpeg_len = 0;
static SemaphoreHandle_t shared_jpeg_mutex = NULL;

void set_camera_mutex(SemaphoreHandle_t mutex) {
    camera_mutex = mutex;
}

bool get_shared_jpeg(uint8_t **out_buf, size_t *out_len) {
    if (!shared_jpeg_mutex) return false;
    if (xSemaphoreTake(shared_jpeg_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
    if (shared_jpeg_len == 0) {
        xSemaphoreGive(shared_jpeg_mutex);
        return false;
    }
    *out_buf = shared_jpeg_buf;
    *out_len = shared_jpeg_len;
    return true;
}

void release_shared_jpeg(void) {
    if (shared_jpeg_mutex) xSemaphoreGive(shared_jpeg_mutex);
}

// ---------- 获取网关 IP ----------
static esp_err_t get_gateway_addr(struct sockaddr_in *addr_out) {
    tcpip_adapter_ip_info_t ip_info;
    esp_err_t err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi IP info");
        return err;
    }
    uint32_t gw = ip_info.gw.addr;
    if (gw == 0) {
        ESP_LOGE(TAG, "Gateway address is 0.0.0.0, check WiFi connection");
        return ESP_FAIL;
    }
    memset(addr_out, 0, sizeof(*addr_out));
    addr_out->sin_family = AF_INET;
    addr_out->sin_port = htons(UDP_PORT);
    addr_out->sin_addr.s_addr = gw;
    ESP_LOGI(TAG, "Target (gateway): %s:%d", inet_ntoa(addr_out->sin_addr), UDP_PORT);
    return ESP_OK;
}

// ---------- 发送一个UDP包（底层） ----------
static void udp_send_packet(const uint8_t *packet, size_t len) {
    sendto(udp_socket, packet, len, 0,
           (struct sockaddr *)&target_addr, sizeof(target_addr));
}

// ---------- 发送一个数据块（填充到 CHUNK_DATA_SIZE） ----------
static void udp_send_data_chunk(uint16_t frame_id, uint16_t chunk_index,
                                uint16_t total_chunks, uint16_t jpeg_len,
                                const uint8_t *data) {
    uint8_t packet[sizeof(udp_chunk_header_t) + CHUNK_DATA_SIZE];
    udp_chunk_header_t *header = (udp_chunk_header_t *)packet;

    header->frame_id     = htons(frame_id);
    header->chunk_index  = htons(chunk_index);
    header->total_chunks = htons(total_chunks);
    header->jpeg_len     = htons(jpeg_len);

    memcpy(packet + sizeof(udp_chunk_header_t), data, CHUNK_DATA_SIZE);
    udp_send_packet(packet, sizeof(udp_chunk_header_t) + CHUNK_DATA_SIZE);
}

// ---------- 发送一个冗余包（数据区：2字节组号 + 冗余数据） ----------
static void udp_send_redundant_chunk(uint16_t frame_id,
                                     uint16_t total_chunks,
                                     uint16_t jpeg_len,
                                     uint16_t group_id,
                                     const uint8_t *redundant_data) {
    uint8_t packet[sizeof(udp_chunk_header_t) + 2 + CHUNK_DATA_SIZE];
    udp_chunk_header_t *header = (udp_chunk_header_t *)packet;

    header->frame_id     = htons(frame_id);
    header->chunk_index  = htons(0xFFFF);          // 冗余包特殊标记
    header->total_chunks = htons(total_chunks);
    header->jpeg_len     = htons(jpeg_len);

    // 数据区前两个字节写入组号 (大端)
    uint8_t *payload = packet + sizeof(udp_chunk_header_t);
    uint16_t net_group_id = htons(group_id);
    memcpy(payload, &net_group_id, 2);
    memcpy(payload + 2, redundant_data, CHUNK_DATA_SIZE);

    udp_send_packet(packet, sizeof(udp_chunk_header_t) + 2 + CHUNK_DATA_SIZE);
}

// ---------- 发送带 FEC 的整帧 ----------
static void udp_send_frame_fec(const uint8_t *jpeg_buf, size_t jpeg_len) {
    if (jpeg_len == 0) return;

    // 计算填充后的总块数（每块 1024 字节，不足则末尾填0）
    uint16_t total_chunks = (jpeg_len + CHUNK_DATA_SIZE - 1) / CHUNK_DATA_SIZE;
    uint16_t frame_id = frame_counter++;

    // 把 JPEG 数据拷贝并填充到 CHUNK_DATA_SIZE 对齐的缓冲区
    uint8_t *aligned_buf = (uint8_t *)calloc(total_chunks, CHUNK_DATA_SIZE);
    if (!aligned_buf) {
        ESP_LOGE(TAG, "Failed to allocate aligned buffer");
        return;
    }
    memcpy(aligned_buf, jpeg_buf, jpeg_len);   // 后面自动填充0

    // 分组并发送
    uint16_t groups = (total_chunks + DATA_PACKS_PER_GROUP - 1) / DATA_PACKS_PER_GROUP;
    for (uint16_t g = 0; g < groups; g++) {
        uint8_t group_data[DATA_PACKS_PER_GROUP][CHUNK_DATA_SIZE];
        uint8_t redundant[CHUNK_DATA_SIZE];
        memset(redundant, 0, CHUNK_DATA_SIZE);

        int chunks_in_group = 0;
        for (int i = 0; i < DATA_PACKS_PER_GROUP; i++) {
            uint16_t chunk_idx = g * DATA_PACKS_PER_GROUP + i;
            if (chunk_idx >= total_chunks) break;

            // 从对齐缓冲区里取这个块（已经是 1024 字节）
            memcpy(group_data[i], aligned_buf + chunk_idx * CHUNK_DATA_SIZE, CHUNK_DATA_SIZE);

            // 累加 XOR 到冗余块
            for (int b = 0; b < CHUNK_DATA_SIZE; b++) {
                redundant[b] ^= group_data[i][b];
            }
            chunks_in_group++;
        }

        // 发送组内的数据包
        for (int i = 0; i < chunks_in_group; i++) {
            uint16_t chunk_idx = g * DATA_PACKS_PER_GROUP + i;
            udp_send_data_chunk(frame_id, chunk_idx, total_chunks, (uint16_t)jpeg_len, group_data[i]);
        }

        // 发送冗余包（即使组内不足4个块，仍发送冗余，通过填充的0保证XOR正确）
        udp_send_redundant_chunk(frame_id, total_chunks, (uint16_t)jpeg_len, g, redundant);
    }

    free(aligned_buf);
}

// ---------- 主任务 ----------
static void udp_stream_task(void *pvParameters) {
    udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket < 0) {
        ESP_LOGE(TAG, "Socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    int sendbuf = 64 * 1024;
    setsockopt(udp_socket, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf));

    if (get_gateway_addr(&target_addr) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot get gateway, falling back to broadcast 192.168.116.255");
        int broadcastEnable = 1;
        setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(UDP_PORT);
        target_addr.sin_addr.s_addr = inet_addr("192.168.116.255");
    }

    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;

    shared_jpeg_mutex = xSemaphoreCreateMutex();

    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        if (fb->format == PIXFORMAT_JPEG) {
            udp_send_frame_fec(fb->buf, fb->len);

            if (shared_jpeg_mutex && fb->len <= SHARED_JPEG_MAX) {
                if (xSemaphoreTake(shared_jpeg_mutex, 0) == pdTRUE) {
                    memcpy(shared_jpeg_buf, fb->buf, fb->len);
                    shared_jpeg_len = fb->len;
                    xSemaphoreGive(shared_jpeg_mutex);
                }
            }

            esp_camera_fb_return(fb);
        } else {
            bool ok = frame2jpg(fb, 15, &jpg_buf, &jpg_len);
            esp_camera_fb_return(fb);
            if (ok) {
                udp_send_frame_fec(jpg_buf, jpg_len);

                if (shared_jpeg_mutex && jpg_len <= SHARED_JPEG_MAX) {
                    if (xSemaphoreTake(shared_jpeg_mutex, 0) == pdTRUE) {
                        memcpy(shared_jpeg_buf, jpg_buf, jpg_len);
                        shared_jpeg_len = jpg_len;
                        xSemaphoreGive(shared_jpeg_mutex);
                    }
                }

                free(jpg_buf);
                jpg_buf = NULL;
            } else {
                ESP_LOGE(TAG, "JPEG conversion failed");
            }
        }

        vTaskDelay(40 / portTICK_PERIOD_MS);
    }

    close(udp_socket);
    vTaskDelete(NULL);
}

void startUdpStream(void) {
    xTaskCreate(udp_stream_task, "udp_stream", 8192, NULL, 5, NULL);
}