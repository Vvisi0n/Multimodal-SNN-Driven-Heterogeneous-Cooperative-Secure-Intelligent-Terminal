/**
 * @file    main.cpp
 * @brief   ESP32-S3 双核四分类检测 + MQTT/UDP 推流
 *
 * 硬件：ESP32-S3 N16R8 + OV2640
 * Core 0：UDP JPEG 推流
 * Core 1：TFLite-Micro 推理（CopperSpacer / background / hand / wire）
 * 无线：WiFi + MQTT（PubSubClient）数据上报
 */

#define CAMERA_MODEL_ESP32S3_EYE

/* --------------------------------------------------------------------------
   功能开关（1=开，0=关）
   -------------------------------------------------------------------------- */
#define ENABLE_UDP_STREAM       1   // UDP 推流
#define ENABLE_AI_INFERENCE     1   // TFLite 手势检测（Core 1）
#define ENABLE_MQTT             1   // MQTT 客户端（收发，不自动发送）
#define ENABLE_AI_DATA_SHARE    0   // 推理结果上推 MQTT

/* --------------------------------------------------------------------------
   头文件引用
   -------------------------------------------------------------------------- */
#include "esp_camera.h"
#include <WiFi.h>
#include "udp.h"
#include "camera_pins.h"
#include "buzzer.h"

#if ENABLE_AI_INFERENCE
#include "infer.h"
#endif

#if ENABLE_MQTT
#include "mqtt.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* --------------------------------------------------------------------------
   常量定义
   -------------------------------------------------------------------------- */
const char* ssid     = "JIGE666";
const char* password = "1234567890";

static constexpr int kCameraWidth  = 640;
static constexpr int kCameraHeight = 480;

static bool is_initialised = false;

static SemaphoreHandle_t camera_mutex = nullptr;

/* --------------------------------------------------------------------------
   MQTT 回调：串口回声打印接收到的消息
   -------------------------------------------------------------------------- */
#if ENABLE_MQTT
static void mqtt_msg_cb(const char* topic, const char* payload, int len) {
    Serial.printf("[MQTT] topic=%s  payload=%s\n", topic, payload);
}
#endif

/* --------------------------------------------------------------------------
   推理任务（Core 1）
   -------------------------------------------------------------------------- */
#if ENABLE_AI_INFERENCE
static void inference_task(void* pvParameters) {
    Serial.println("[Core 1] 推理任务启动");

    // 在 PSRAM 分配 RGB 转换缓冲区
    uint8_t* rgb_buf = (uint8_t*)heap_caps_malloc(
        kCameraWidth * kCameraHeight * 3,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb_buf) {
        Serial.println("[Core 1] 致命错误：RGB 缓冲区分配失败");
        vTaskDelete(NULL);
        return;
    }

    unsigned long last_report = 0;
    int frame_count = 0;

    while (1) {
        unsigned long t_start = millis();

        // 从 Core 0 获取共享 JPEG 帧
        uint8_t* jpeg_buf = nullptr;
        size_t jpeg_len = 0;
        if (!get_shared_jpeg(&jpeg_buf, &jpeg_len)) {
            static int fail_count = 0;
            fail_count++;
            if (fail_count <= 5 || fail_count % 50 == 0) {
                Serial.printf("[Core 1] 无可用帧 #%d\n", fail_count);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // JPEG → RGB888
        unsigned long t1 = millis();
        bool converted = fmt2rgb888(jpeg_buf, jpeg_len, PIXFORMAT_JPEG, rgb_buf);
        release_shared_jpeg();

        if (!converted) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 执行 TFLite 推理
        unsigned long t2 = millis();
        float prob = 0.0f;
        int  predicted = 0;
        unsigned long infer_ms = 0;

        bool ok = infer_run(rgb_buf, kCameraWidth, kCameraHeight,
                            &prob, &predicted, &infer_ms);

        if (!ok) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        unsigned long t_end = millis();
        frame_count++;

        // 每 500ms 输出一次遥测摘要
        if (millis() - last_report > 500) {
            UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
            Serial.printf("[Core 1] #%d | 总耗时=%lums | 解码=%lu 推理=%lu | %s (%.1f%%) | 栈剩余=%u\n",
                          frame_count, t_end - t_start,
                          t2 - t1, infer_ms,
                          infer_class_name(predicted), prob * 100.0f, stack);

#if ENABLE_MQTT
            if (mqtt_is_connected()) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "[esp_vis]{ \"total_t\": %lu, \"infer_t\": %lu, \"infer_res\": \"%s\", \"Confidence\": \"%.1f%%\", \"stack\": %u }",
                         t_end - t_start,
                         infer_ms,
                         infer_class_name(predicted),
                         prob * 100.0f,
                         stack);

#if ENABLE_AI_DATA_SHARE
                mqtt_publish("dpj_vis_mqtt", buf);
                mqtt_publish("cp_mqtt",  buf);
#endif
            }
#endif

            last_report = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(rgb_buf);
    vTaskDelete(NULL);
}
#endif

/* --------------------------------------------------------------------------
   初始化
   -------------------------------------------------------------------------- */
void setup() {
    /* ----- 串口 ----- */
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    unsigned long serial_timeout = millis() + 3000;
    while (!Serial && millis() < serial_timeout) {
        delay(10);
    }

    Serial.println("\n========================================");
    Serial.println(" ESP32-S3 双核四分类检测");
    Serial.println("========================================\n");

    BUZZER_Init();

    /* ----- 摄像头 ----- */
    Serial.println("[Setup] 初始化摄像头...");
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0  = Y2_GPIO_NUM;
    config.pin_d1  = Y3_GPIO_NUM;
    config.pin_d2  = Y4_GPIO_NUM;
    config.pin_d3  = Y5_GPIO_NUM;
    config.pin_d4  = Y6_GPIO_NUM;
    config.pin_d5  = Y7_GPIO_NUM;
    config.pin_d6  = Y8_GPIO_NUM;
    config.pin_d7  = Y9_GPIO_NUM;
    config.pin_xclk   = XCLK_GPIO_NUM;
    config.pin_pclk   = PCLK_GPIO_NUM;
    config.pin_vsync  = VSYNC_GPIO_NUM;
    config.pin_href   = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn   = PWDN_GPIO_NUM;
    config.pin_reset  = RESET_GPIO_NUM;
    config.xclk_freq_hz  = 20000000;
    config.frame_size    = FRAMESIZE_VGA;
    config.pixel_format  = PIXFORMAT_JPEG;
    config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location   = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality  = 8;
    config.fb_count      = 1;

    if (psramFound()) {
        Serial.println("[Setup] 检测到 PSRAM，启用高分辨率模式");
        config.jpeg_quality = 8;
        config.fb_count     = 2;
        config.grab_mode    = CAMERA_GRAB_LATEST;
    } else {
        Serial.println("[Setup] 未检测到 PSRAM，降级到 DRAM");
        config.frame_size   = FRAMESIZE_SVGA;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[Setup] 摄像头初始化失败：0x%x\n", err);
        return;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        Serial.printf("[Setup] 传感器：PID=0x%02X VER=0x%02X MIDL=0x%02X MIDH=0x%02X\n",
                      s->id.PID, s->id.VER, s->id.MIDL, s->id.MIDH);
    }
    s->set_vflip(s, 1);          // 垂直翻转
    s->set_brightness(s, 1);     // 亮度 +1
    s->set_saturation(s, 0);     // 饱和度默认
    Serial.println("[Setup] 摄像头初始化完成");

    /* ----- WiFi ----- */
    Serial.printf("[Setup] 正在连接 WiFi：%s\n", ssid);
    WiFi.begin(ssid, password);
    WiFi.setSleep(true);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[Setup] WiFi 已连接");
    Serial.printf("[Setup] IP：%s  网关：%s\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str());

    /* ----- MQTT ----- */
    Serial.println("[Setup] 初始化 MQTT...");
#if ENABLE_MQTT
    mqtt_set_callback(mqtt_msg_cb);
    if (!mqtt_init()) {
        Serial.println("[Setup] MQTT 初始化失败（非致命）");
    }
#else
    Serial.println("[Setup] MQTT 已禁用（ENABLE_MQTT=0）");
#endif

    /* ----- TFLite 模型 ----- */
#if ENABLE_AI_INFERENCE
    if (!infer_init()) {
        Serial.println("[Setup] TFLite 初始化失败！系统停止...");
        while (1) { delay(1000); }
    }
#endif

    is_initialised = true;

    /* ----- 启动任务 ----- */
#if ENABLE_AI_INFERENCE
    Serial.println("[Setup] 在 Core 1 上创建推理任务...");
    xTaskCreatePinnedToCore(
        inference_task,
        "inference_task",
        32768,
        NULL,
        2,
        NULL,
        1
    );
#endif

#if ENABLE_UDP_STREAM
    Serial.println("[Setup] 在 Core 0 上启动 UDP 推流...");
    startUdpStream();
#else
    Serial.println("[Setup] UDP 推流已禁用（ENABLE_UDP_STREAM=0）");
#endif

    Serial.println("[Setup] 系统就绪！\n");
}

/* --------------------------------------------------------------------------
   主循环：保持 MQTT 保活，不自动发布
   -------------------------------------------------------------------------- */
void loop() {
#if ENABLE_MQTT
    mqtt_poll();
#endif
    vTaskDelay(pdMS_TO_TICKS(1000));
}