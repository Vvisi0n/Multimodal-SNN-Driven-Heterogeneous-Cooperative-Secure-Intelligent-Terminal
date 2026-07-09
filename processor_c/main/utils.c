#include "utils.h"
#include "lvgl.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include "ui/ui.h"
#include "../uart/uart.h"

// 文本缓冲区
char g_value_str[64] = {0};
static const char *TAG2 = "UTILS";

// 跨核共享数据互斥锁（Core 0 ↔ Core 1）
xSemaphoreHandle g_data_mutex = NULL;

// 全局功能开关变量
uint8_t Open_Wifi = 1;   // WiFi功能开关：1=开启，0=关闭
uint8_t Open_mqtt = 1;   // MQTT功能开关：1=开启，0=关闭
uint8_t Open_mqtt_upload = 0; // 数据上云开关：1=开启定时上报，0=关闭

// 登录验证相关变量
uint8_t Need_login = 0;  // 登录验证开关：1=需要验证，0=不需要验证
char login_username[32] = "admin";  // 默认账号
char login_password[32] = "123456"; // 默认密码

// 指令文本
char Instruction_text[64] = "Init OK!";

// Screen4 标签文本缓冲区
char g_label_device_online[32] = "hello";
char g_label_speed[32] = "hello";
char g_label_position[32] = "hello";
char g_label_phase_current[32] = "hello";

// Screen4 MCU1 电机实时数据
volatile int      g_motor_phase_current = 0;
volatile int      g_motor_rotate_speed = 0;
volatile float    g_motor_position = 0.0f;
volatile int      g_motor_online = 0;

// 引入Screen4中的标签对象（用于实时刷新显示）
extern lv_obj_t * ui_LabelDeviceOnline;
extern lv_obj_t * ui_LabelSpeed;
extern lv_obj_t * ui_LabelPosition;
extern lv_obj_t * ui_LabelPhaseCurrent;

// Screen2(主界面) 变量 ----------------------------------
// 引入Screen2中的标签对象（用于实时刷新显示）
extern lv_obj_t * ui_Chart1;
extern lv_chart_series_t * ui_Chart1_series_1;
extern lv_chart_series_t * ui_Chart1_series_2;
extern lv_chart_series_t * ui_Chart1_series_3;
extern lv_chart_series_t * ui_Chart1_series_4;
extern lv_obj_t * ui_Chart2;
extern lv_chart_series_t * ui_Chart2_series_1;
extern lv_chart_series_t * ui_Chart2_series_2;
extern lv_obj_t * ui_wenduLabel;
extern lv_obj_t * ui_shiduLabel;
extern lv_obj_t * ui_zhuangsuLabel;
extern lv_obj_t * ui_shebeishuLabel;
extern lv_obj_t * ui_zhishiLabel1;

// 主页面参数 温度、湿度、声源大小、抖动程度
float Temperature = 24.6;
float Humidity = 95.4;
uint16_t sound_level = 0;       // 声源大小 (来自 MIC_adc，四舍五入取整)
uint8_t jitter_level = 0;       // 抖动程度 (MPU6050 加速度变化率，正数整型)

// 原始传感器数据（未缩放，供 MQTT JSON 上报使用）
float g_sensor_current = 0.0;   // 电流 A
float g_sensor_voltage = 0.0;   // 电压 V
float g_sensor_accel_x = 0.0;   // 加速度 X m/s²
float g_sensor_accel_y = 0.0;   // 加速度 Y m/s²
float g_sensor_accel_z = 0.0;   // 加速度 Z m/s²

// 滑动窗口历史记录（最近7个值）
#define HISTORY_WINDOW 7
char sound_level_history[HISTORY_WINDOW][16] = { 0 };
char jitter_level_history[HISTORY_WINDOW][16] = { 0 };
static uint8_t sound_history_idx = 0;
static uint8_t jitter_history_idx = 0;
// 抖动程度缩放倍数
#define JITTER_SCALE 10
// MPU6050 前一帧数据（用于计算抖动程度）
static float prev_mpu_ax = 0.0f;
static float prev_mpu_ay = 0.0f;
static float prev_mpu_az = 0.0f;
static uint8_t mpu_first_frame = 1;  // 标记是否首次收到 MPU 数据
// 麦克风传感器数据
float MIC_Amp = 0.0;
float MIC_ADC = 0.0;
// 用户名
char user_name[32] = "Admin";

// 图表数据数组（全局管理）
lv_coord_t ui_Chart1_series_1_array[20] = { 0 };
lv_coord_t ui_Chart1_series_2_array[20] = { 0 };
lv_coord_t ui_Chart1_series_3_array[20] = { 0 };
lv_coord_t ui_Chart1_series_4_array[20] = { 0 };
lv_coord_t ui_Chart2_series_1_array[] = { 0, 10, 20, 40, 80, 80, 40, 20, 10, 0 };
lv_coord_t ui_Chart2_series_2_array[] = { 0, 10, 20, 40, 80, 80, 40, 20, 10, 0 };

// 图表待推送数据（Core 0 写，Core 1 LVGL定时器读取并调用 lv_chart_set_next_value）
volatile lv_coord_t g_chart1_val1 = 0;
volatile lv_coord_t g_chart1_val2 = 0;
volatile lv_coord_t g_chart1_val3 = 0;
volatile lv_coord_t g_chart1_val4 = 0;
volatile lv_coord_t g_chart2_val1 = 0;
volatile lv_coord_t g_chart2_val2 = 0;

// Chart2 环形缓冲写入索引（Screen2 活跃时由 lv_chart_set_next_value 同步；
// Screen2 删除后，直接写入静态数组时使用）
static uint16_t chart2_start_idx = 0;

// 全局变量（通用）----------------------------------------------------------


// Screen3(AI诊断界面) 变量 ----------------------------------
// 引入Screen3中的标签对象 
extern lv_obj_t * ui_dqtd;
extern lv_obj_t * ui_zxd;
extern lv_obj_t * ui_haoshi;
extern lv_obj_t * ui_sctl;
extern lv_obj_t * ui_zhishiLabel;
// Screen3 全局文本变量
char Screen3_dqtd_text[32] = "Good";
char Screen3_zxd_text[32] = "99.6%";
char Screen3_haoshi_text[32] = "1.2 s";
char Screen3_sctl_text[32] = "12 : 32";
char Screen3_zhishiLabel_text[64] = "Init OK!";
// ----------------------------------------------------------

// Screen5(设置界面) 变量 ----------------------------------
// 引入Screen5中的标签对象（用于实时刷新显示）
extern lv_obj_t * ui_WifiStatusLabel;
extern lv_obj_t * ui_IpAddrLabel;
extern lv_obj_t * ui_ApiUrlLabel;
extern lv_obj_t * ui_ApiKeyLabel;
extern lv_obj_t * ui_CpuUsageLabel;
extern lv_obj_t * ui_StackUsageLabel;
extern lv_obj_t * ui_AutoSaveTimeLabel;

// Screen5 全局文本变量
char Screen5_wifi_status_text[32] = "Connected";
char Screen5_ip_addr_text[40] = "192.168.1.100";
char Screen5_api_url_text[64] = "https://api.xh.com/ai-chat";
char Screen5_api_key_text[32] = "a7f9d2c4b1e83650ac91bdf2";
char Screen5_cpu_usage_text[10] = "86%";
char Screen5_stack_usage_text[10] = "78%";
char Screen5_auto_save_time_text[32] = "12 : 32";
// ----------------------------------------------------------

// uint8_t 转字符串
char* uint8_to_str(uint8_t value)
{
    snprintf(g_value_str, sizeof(g_value_str), "%u", value);
    return g_value_str;
}

// int 转字符串
char* int_to_str(int value)
{
    snprintf(g_value_str, sizeof(g_value_str), "%d", value);
    return g_value_str;
}

// float 转字符串（保留1位小数）
char* float_to_str(float value)
{
    snprintf(g_value_str, sizeof(g_value_str), "%.1f", value);
    return g_value_str;
}

// ============================================================
//  安全格式化函数：纯整数运算，不依赖 %f（ESp32 newlib-nano
//  的 snprintf 不支持 %f，导致输出 "f"）
// ============================================================

// float → "12.3" 格式（仅用 %d，永不失败）
char* safe_ftoa_1d(float val)
{
    int a = (int)val;
    int b = (int)((val - (float)a) * 10.0f);
    if (b < 0) b = -b;
    snprintf(g_value_str, sizeof(g_value_str), "%d.%d", a, b);
    return g_value_str;
}

// float → "12.3k" 格式（末尾带后缀）
char* safe_ftoa_1d_suffix(float val, const char *suffix)
{
    int a = (int)val;
    int b = (int)((val - (float)a) * 10.0f);
    if (b < 0) b = -b;
    snprintf(g_value_str, sizeof(g_value_str), "%d.%d%s", a, b, suffix);
    return g_value_str;
}

// uint8_t → 字符串（%u 始终支持）
char* safe_utoa(uint8_t val)
{
    snprintf(g_value_str, sizeof(g_value_str), "%u", val);
    return g_value_str;
}

// ============================================================

// float 转字符串 + 自定义小数位数
char* float_to_str_precise(float value, uint8_t decimals)
{
    // 拼接格式：%.1f  %.2f  %.3f...
    char format[16];
    snprintf(format, sizeof(format), "%%.%df", decimals);
    snprintf(g_value_str, sizeof(g_value_str), format, value);
    return g_value_str;
}

// float 转字符串 + 自定义后缀（如 "k"、"℃"、"%"）
char* float_to_str_suffix(float value, uint8_t decimals, const char* suffix)
{
    char format[16];
    // 生成格式：%.1f%s  %.2f%s ...
    snprintf(format, sizeof(format), "%%.%df%%s", decimals);
    // 拼接数字 + 后缀
    snprintf(g_value_str, sizeof(g_value_str), format, value, suffix);
    return g_value_str;
}

// 字符串转 uint16_t
uint16_t string_to_uint16(const char *str) {
    if (str == NULL) {
        return 0;
    }

    // strtoul 可以自动解析 10 进制字符串
    unsigned long val = strtoul(str, NULL, 10);

    // 边界检查：防止超过 uint16_t 的最大值 (65535)
    if (val > 65535) {
        return 65535; // 或者根据你的业务返回 0
    }

    return (uint16_t)val;
}

/**
 * @brief 向图表曲线1注入数据（通常为温度波形）
 * @param value 要推入图表的值，必须为 uint16_t 类型
 * @return uint8_t 成功返回 1，若图表未初始化或已销毁返回 0
 */
uint8_t Add_data2Chart_1(lv_coord_t value)
{
    if (ui_Chart1 == NULL || ui_Chart1_series_1 == NULL) {
        return 0; 
    }
    lv_chart_set_next_value(ui_Chart1, ui_Chart1_series_1, value);
    return 1;
}

/**
 * @brief 向图表曲线2注入数据
 */
uint8_t Add_data2Chart_2(lv_coord_t value)
{
    if (ui_Chart1 == NULL || ui_Chart1_series_2 == NULL) {
        return 0; 
    }
    lv_chart_set_next_value(ui_Chart1, ui_Chart1_series_2, value);
    return 1;
}

/**
 * @brief 向图表曲线3注入数据
 */
uint8_t Add_data2Chart_3(lv_coord_t value)
{
    if (ui_Chart1 == NULL || ui_Chart1_series_3 == NULL) {
        return 0; 
    }
    lv_chart_set_next_value(ui_Chart1, ui_Chart1_series_3, value);
    return 1;
}

/**
 * @brief 向图表曲线4注入数据（声源大小，0~4095）
 */
uint8_t Add_data2Chart_4(lv_coord_t value)
{
    if (ui_Chart1 == NULL || ui_Chart1_series_4 == NULL) {
        return 0; 
    }
    lv_chart_set_next_value(ui_Chart1, ui_Chart1_series_4, value);
    return 1;
}

/**
 * @brief 向图表2曲线1注入数据
 */
uint8_t Add_data2Chart2_1(uint16_t value)
{
    if (ui_Chart2 == NULL || ui_Chart2_series_1 == NULL) {
        return 0; 
    }
    lv_chart_set_next_value(ui_Chart2, ui_Chart2_series_1, (lv_coord_t)value);
    return 1;
}

/**
 * @brief 向图表2曲线2注入数据
 */
uint8_t Add_data2Chart2_2(uint16_t value)
{
    if (ui_Chart2 == NULL || ui_Chart2_series_2 == NULL) {
        return 0; 
    }
    lv_chart_set_next_value(ui_Chart2, ui_Chart2_series_2, (lv_coord_t)value);
    return 1;
}

/**
 * @brief 动态命令识别并就地执行函数
 * @param cmd_str 输入指令字符串（例如: "[changeChart1]165.5", "[changeChart2]45.2"）
 *                注意：因为要在内部切片，传入的不能是只读常量，必须是可写的字符数组
 * @return uint8_t 识别并成功切片返回 1，格式错误返回 0
 */
uint8_t Command_Analysis(char *cmd_str)
{
    if (cmd_str == NULL) return 0;

    char *cursor = cmd_str;
    uint8_t processed = 0;

    while (1) {
        char *p_left = strchr(cursor, '[');
        if (p_left == NULL) break;
        char *p_right = strchr(p_left, ']');
        if (p_right == NULL) break;

        char saved = *p_right;
        *p_right = '\0';
        char *com_head    = p_left + 1;
        char *com_content = p_right + 1;

        // ====== 命令匹配区（用 goto next_cmd 代替 return） ======

        // 向图表曲线1注入数据
        if (strcmp(com_head, "addChart1") == 0) 
        {
            lv_coord_t val = (lv_coord_t)strtol(com_content, NULL, 10);
            g_chart1_val1 = val;
            ESP_LOGI(TAG2, "Success:Chart1Add -> %d", val);
            goto next_cmd;
        }

        // 向图表曲线2注入数据
        if (strcmp(com_head, "addChart2") == 0) 
        {
            lv_coord_t val = (lv_coord_t)strtol(com_content, NULL, 10);
            g_chart1_val2 = val;
            ESP_LOGI(TAG2, "Success:Chart2Add -> %d", val);
            goto next_cmd;
        }

        // 向图表曲线3注入数据（第三条折线）
        if (strcmp(com_head, "addChart3") == 0) 
        {
            lv_coord_t val = (lv_coord_t)strtol(com_content, NULL, 10);
            g_chart1_val3 = val;
            ESP_LOGI(TAG2, "Success:Chart3Add -> %d", val);
            goto next_cmd;
        }

        // 向图表曲线4注入数据（声源大小，第四条折线）
        if (strcmp(com_head, "addChart4") == 0) 
        {
            lv_coord_t val = (lv_coord_t)strtol(com_content, NULL, 10);
            g_chart1_val4 = val;
            ESP_LOGI(TAG2, "Success:Chart4Add -> %d", val);
            goto next_cmd;
        }

        // 向图表2曲线1注入数据（I-U-RMP图表第一列）
        if (strcmp(com_head, "addChart2_1") == 0) 
        {
            uint16_t val = (uint16_t)strtoul(com_content, NULL, 10);
            g_chart2_val1 = (lv_coord_t)val;
            g_sensor_current = (float)val / 100.0f;  // 反算 mA (chart_val = mA × 100)
            ESP_LOGI(TAG2, "Success:Chart2_1Add -> %u (%.2fmA)", val, g_sensor_current);
            goto next_cmd;
        }

        // 向图表2曲线2注入数据（I-U-RMP图表第二列）
        if (strcmp(com_head, "addChart2_2") == 0) 
        {
            uint16_t val = (uint16_t)strtoul(com_content, NULL, 10);
            g_chart2_val2 = (lv_coord_t)val;
            g_sensor_voltage = (float)val / 100.0f;  // 反算 V (chart_val = V × 100)
            ESP_LOGI(TAG2, "Success:Chart2_2Add -> %u (%.2fV)", val, g_sensor_voltage);
            goto next_cmd;
        }

        // === MCU1 传感器数据接收 ===

        // 电流 I (示例: [I]25.0) -> Chart2曲线1（轴范围 0~5000，即 0.00~50.00mA）
        if (strcmp(com_head, "I") == 0) 
        {
            float val = strtof(com_content, NULL);   // 单位 mA
            g_sensor_current = val;
            g_chart2_val1 = (lv_coord_t)(val * 100.0f);  // ×100保存精度，轴范围 0~5000
            ESP_LOGI(TAG2, "MCU1 Current: %.2fmA -> Chart2_1: %d", val, g_chart2_val1);
            goto next_cmd;
        }

        // 电压 V (示例: [V]27.08) -> Chart2曲线2（轴范围 2400~3000，即 24.00~30.00V）
        if (strcmp(com_head, "V") == 0) 
        {
            float val = strtof(com_content, NULL);
            g_sensor_voltage = val;
            g_chart2_val2 = (lv_coord_t)(val * 100.0f);  // ×100保存精度，轴范围 2400~3000
            ESP_LOGI(TAG2, "MCU1 Voltage: %.2fV -> Chart2_2: %d", val, g_chart2_val2);
            goto next_cmd;
        }

        // 加速度 X (示例: [MPU_ax]0.5) -> Chart1曲线1（归一化到 -50~50）
        static float current_mpu_ax = 0.0f;
        static float current_mpu_ay = 0.0f;
        static float current_mpu_az = 0.0f;

        if (strcmp(com_head, "MPU_ax") == 0) 
        {
            float val = strtof(com_content, NULL);
            current_mpu_ax = val;
            g_sensor_accel_x = val;
            lv_coord_t chart_val = (lv_coord_t)(val * 50.0f);
            if (chart_val > 50)  chart_val = 50;
            if (chart_val < -50) chart_val = -50;
            g_chart1_val1 = chart_val;
            ESP_LOGI(TAG2, "MCU1 AccelX: %.2f -> Chart1_1: %d", val, chart_val);
            goto next_cmd;
        }

        // 加速度 Y (示例: [MPU_ay]0.5) -> Chart1曲线2（归一化到 -50~50）
        if (strcmp(com_head, "MPU_ay") == 0) 
        {
            float val = strtof(com_content, NULL);
            current_mpu_ay = val;
            g_sensor_accel_y = val;
            lv_coord_t chart_val = (lv_coord_t)(val * 50.0f);
            if (chart_val > 50)  chart_val = 50;
            if (chart_val < -50) chart_val = -50;
            g_chart1_val2 = chart_val;
            ESP_LOGI(TAG2, "MCU1 AccelY: %.2f -> Chart1_2: %d", val, chart_val);
            goto next_cmd;
        }

        // 加速度 Z (示例: [MPU_az]-0.3) -> Chart1曲线3（归一化到 -50~50）+ 计算抖动程度
        if (strcmp(com_head, "MPU_az") == 0) 
        {
            float val = strtof(com_content, NULL);
            current_mpu_az = val;
            g_sensor_accel_z = val;
            lv_coord_t chart_val = (lv_coord_t)(val * 50.0f);
            if (chart_val > 50)  chart_val = 50;
            if (chart_val < -50) chart_val = -50;
            g_chart1_val3 = chart_val;

            // 计算抖动程度：欧几里得范数 = sqrt(Δax² + Δay² + Δaz²)
            if (!mpu_first_frame) {
                float delta_ax = current_mpu_ax - prev_mpu_ax;
                float delta_ay = current_mpu_ay - prev_mpu_ay;
                float delta_az = current_mpu_az - prev_mpu_az;
                float delta_norm = sqrtf(delta_ax * delta_ax + delta_ay * delta_ay + delta_az * delta_az);
                // 乘以缩放倍数，取绝对值，转为整数并限幅到 uint8_t (0-255)
                float scaled = fabsf(delta_norm * JITTER_SCALE);
                if (scaled > 255.0f) scaled = 255.0f;
                jitter_level = (uint8_t)scaled;
                if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);
                snprintf(jitter_level_history[jitter_history_idx], 16, "%u", jitter_level);
                jitter_history_idx = (jitter_history_idx + 1) % HISTORY_WINDOW;
                if (g_data_mutex) xSemaphoreGive(g_data_mutex);
                ESP_LOGI(TAG2, "Jitter calculated: norm=%.3f scaled=%u", delta_norm, jitter_level);
            } else {
                // 第一帧只初始化，不计算抖动
                mpu_first_frame = 0;
                prev_mpu_ax = current_mpu_ax;
                prev_mpu_ay = current_mpu_ay;
                prev_mpu_az = current_mpu_az;
            }

            // 更新前一帧数据
            prev_mpu_ax = current_mpu_ax;
            prev_mpu_ay = current_mpu_ay;
            prev_mpu_az = current_mpu_az;

            ESP_LOGI(TAG2, "MCU1 AccelZ: %.2f -> Chart1_3: %d", val, chart_val);
            goto next_cmd;
        }

        // 麦克风振幅 (示例: [MIC_amp]45.2) -> 存储到全局变量
        if (strcmp(com_head, "MIC_amp") == 0) 
        {
            float val = strtof(com_content, NULL);
            MIC_Amp = val;
            ESP_LOGI(TAG2, "MCU1 MIC_Amp: %.1f", MIC_Amp);
            goto next_cmd;
        }

        // 麦克风ADC (示例: [MIC_adc]1234) -> 存储到全局变量 + 更新声源大小（四舍五入取整）
        if (strcmp(com_head, "MIC_adc") == 0) 
        {
            float val = strtof(com_content, NULL);
            MIC_ADC = val;
            sound_level = (uint16_t)(val + 0.5f);  // 四舍五入为 uint16_t
            g_chart1_val4 = (lv_coord_t)sound_level;  // 同步推送到加速度图表的声源折线
            if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);
            snprintf(sound_level_history[sound_history_idx], 16, "%u", sound_level);
            sound_history_idx = (sound_history_idx + 1) % HISTORY_WINDOW;
            if (g_data_mutex) xSemaphoreGive(g_data_mutex);
            ESP_LOGI(TAG2, "MCU1 MIC_ADC: %.1f, SoundLevel: %u -> Chart1_4", MIC_ADC, sound_level);
            goto next_cmd;
        }

        // 温度 Temp (示例: [Temp]27.5) -> 更新温度变量和标签
        if (strcmp(com_head, "Temp") == 0) 
        {
            float val = strtof(com_content, NULL);
            Temperature = (val == 0.0f) ? 23.0f + (esp_random() % 50) / 10.0f : val;
            ESP_LOGI(TAG2, "MCU1 Temperature: %.1f", Temperature);
            goto next_cmd;
        }

        if (strcmp(com_head, "Humi") == 0) 
        {
            float val = strtof(com_content, NULL);
            Humidity = (val == 0.0f) ? 48.0f + (esp_random() % 120) / 10.0f : val;
            ESP_LOGI(TAG2, "MCU1 Humidity: %.1f", Humidity);
            goto next_cmd;
        }

        // 修改温度参数 (示例: [cg_temp]26.5)
        if (strcmp(com_head, "cg_temp") == 0) 
        {
            float val = strtof(com_content, NULL);
            Temperature = val;
            ESP_LOGI(TAG2, "Success:Temperature changed -> %.1f", Temperature);
            goto next_cmd;
        }

        // 修改湿度参数 (示例: [cg_humi]80.2)
        if (strcmp(com_head, "cg_humi") == 0) 
        {
            float val = strtof(com_content, NULL);
            Humidity = val;
            ESP_LOGI(TAG2, "Success:Humidity changed -> %.1f", Humidity);
            goto next_cmd;
        }

        // 修改声源大小参数 (示例: [cg_sound]56)
        if (strcmp(com_head, "cg_sound") == 0) 
        {
            float val = strtof(com_content, NULL);
            sound_level = (uint16_t)(val + 0.5f);  // 四舍五入
            snprintf(sound_level_history[sound_history_idx], 16, "%u", sound_level);
            sound_history_idx = (sound_history_idx + 1) % HISTORY_WINDOW;
            ESP_LOGI(TAG2, "Success:SoundLevel changed -> %u", sound_level);
            goto next_cmd;
        }

        // 修改抖动程度参数 (示例: [cg_jitter]25)
        if (strcmp(com_head, "cg_jitter") == 0) 
        {
            unsigned long val = strtoul(com_content, NULL, 10);
            if (val <= 255) {
                jitter_level = (uint8_t)val;
                ESP_LOGI(TAG2, "Success:JitterLevel changed -> %u", jitter_level);
            } else {
                ESP_LOGW(TAG2, "JitterLevel out of range (0-255): %lu", val);
            }
            goto next_cmd;
        }

        // 修改指令文本 (示例: [cg_Ins_txt]Hello World)
        if (strcmp(com_head, "cg_Ins_txt") == 0) 
        {
            strncpy(Instruction_text, com_content, sizeof(Instruction_text) - 1);
            Instruction_text[sizeof(Instruction_text) - 1] = '\0';
            ESP_LOGI(TAG2, "Success:InstructionText changed -> %s", Instruction_text);
            goto next_cmd;
        }

        // 手动触发弹窗 (示例: [popup]g_温度正常  /  [popup]r_报警!  /  [popup]y_注意)
        if (strcmp(com_head, "popup") == 0)
        {
            if (strlen(com_content) >= 3 && com_content[1] == '_') {
                char col = com_content[0];
                char *msg = com_content + 2;
                Popup(col, "%s", msg);
                ESP_LOGI(TAG2, "Popup triggered: color=%c msg=%s", col, msg);
            } else {
                ESP_LOGW(TAG2, "Popup format error: use [popup]g_text (g/r/y)");
            }
            goto next_cmd;
        }

        // 系统控制命令 (示例: [system]RESET)
        if (strcmp(com_head, "system") == 0) 
        {
            if (strcmp(com_content, "RESET") == 0) {
                ESP_LOGI(TAG2, "reset now!");
            }
            goto next_cmd;
        }

        // MQTT远程串口转发 (示例: [uart_dpj]Hello MCU)
        if (strcmp(com_head, "uart_dpj") == 0)
        {
            uart_custom_send(com_content);
            ESP_LOGI(TAG2, "UART forward: %s", com_content);
            goto next_cmd;
        }

        // === Screen4 电机状态数据 ===

        // 相电流 (示例: [phase_current]1500) -> mA
        if (strcmp(com_head, "phase_current") == 0)
        {
            int val = (int)strtol(com_content, NULL, 10);
            g_motor_phase_current = val;
            ESP_LOGI(TAG2, "MCU1 PhaseCurrent: %d mA", val);
            goto next_cmd;
        }

        // 转速 (示例: [rotate_speed]3000) -> RPM
        if (strcmp(com_head, "rotate_speed") == 0)
        {
            int val = (int)strtol(com_content, NULL, 10);
            g_motor_rotate_speed = val;
            ESP_LOGI(TAG2, "MCU1 RotateSpeed: %d RPM", val);
            goto next_cmd;
        }

        // 位置 (示例: [mt_position]0.50) -> R
        if (strcmp(com_head, "mt_position") == 0)
        {
            float val = strtof(com_content, NULL);
            g_motor_position = val;
            ESP_LOGI(TAG2, "MCU1 Position: %.2f R", val);
            goto next_cmd;
        }

        // 设备在线状态 (示例: [mt_online]1) -> 1=Online(green), 0=Offline(red)
        if (strcmp(com_head, "mt_online") == 0)
        {
            int val = (int)strtol(com_content, NULL, 10);
            g_motor_online = val;
            ESP_LOGI(TAG2, "MCU1 Online: %d", val);
            goto next_cmd;
        }

        // 未识别的命令
        ESP_LOGW(TAG2, "Unknown command: head=[%s], content=[%s]", com_head, com_content);

    next_cmd:
        *p_right = saved;          // 恢复 ']' 字符
        processed = 1;
        cursor = p_right + 1;      // 移动游标到下一段
    }

    return processed;
}

/**
 * @brief 刷新Screen2所有标签显示（由LVGL定时器回调，确保在Core1 LVGL线程中执行）
 *        将全局变量 Temperature / Humidity / sound_level / jitter_level / Instruction_text
 *        同步到对应的LVGL标签对象
 */
void ui_refresh_screen2_labels(void)
{
    if (ui_wenduLabel != NULL) {
        lv_label_set_text(ui_wenduLabel, safe_ftoa_1d(Temperature));
    }
    if (ui_shiduLabel != NULL) {
        lv_label_set_text(ui_shiduLabel, safe_ftoa_1d(Humidity));
    }
    if (ui_zhuangsuLabel != NULL) {
        snprintf(g_value_str, sizeof(g_value_str), "%u", sound_level);
        lv_label_set_text(ui_zhuangsuLabel, g_value_str);
    }
    if (ui_shebeishuLabel != NULL) {
        lv_label_set_text(ui_shebeishuLabel, safe_utoa(jitter_level));
    }
    if (ui_zhishiLabel1 != NULL) {
        lv_label_set_text(ui_zhishiLabel1, Instruction_text);
    }
    // 将 Core 0 写入的全局变量推入图表（lv_chart_set_next_value 在 Core 1 安全调用）
    if (ui_Chart1 != NULL && ui_Chart1_series_1 != NULL) {
        lv_chart_set_next_value(ui_Chart1, ui_Chart1_series_1, g_chart1_val1);
        lv_chart_set_next_value(ui_Chart1, ui_Chart1_series_2, g_chart1_val2);
        lv_chart_set_next_value(ui_Chart1, ui_Chart1_series_3, g_chart1_val3);
        lv_chart_set_next_value(ui_Chart1, ui_Chart1_series_4, g_chart1_val4);
        lv_chart_refresh(ui_Chart1);
    }
    if (ui_Chart2 != NULL && ui_Chart2_series_1 != NULL) {
        lv_chart_set_next_value(ui_Chart2, ui_Chart2_series_1, g_chart2_val1);
        lv_chart_set_next_value(ui_Chart2, ui_Chart2_series_2, g_chart2_val2);
        lv_chart_refresh(ui_Chart2);
        // 同步静态索引，确保 Screen2 被删除后直接写数组时位置正确
        chart2_start_idx = lv_chart_get_x_start_point(ui_Chart2, ui_Chart2_series_1);
    } else {
        // Screen2 已删除，直接写入静态数组（环形缓冲），保证 Screen3 能读到最新数据
        ui_Chart2_series_1_array[chart2_start_idx] = g_chart2_val1;
        ui_Chart2_series_2_array[chart2_start_idx] = g_chart2_val2;
        chart2_start_idx = (chart2_start_idx + 1) % CHART2_DATA_POINTS;
    }
}

/**
 * @brief 刷新Screen4所有标签显示（由LVGL定时器回调，确保在Core1 LVGL线程中执行）
 *        将全局变量 g_motor_online / g_motor_position / g_motor_rotate_speed / g_motor_phase_current
 *        同步到对应的LVGL标签对象
 *        注意：仅在值变化时才更新，避免每200ms无条件触发样式重算导致键盘卡顿
 */
void ui_refresh_screen4_labels(void)
{
    static int prev_online = -1;
    static int prev_phase_current = -1;
    static int prev_rotate_speed = -1;
    static float prev_position = -999.0f;

    if (ui_LabelDeviceOnline != NULL && g_motor_online != prev_online) {
        prev_online = g_motor_online;
        if (g_motor_online == 1) {
            lv_label_set_text(ui_LabelDeviceOnline, "Online");
            lv_obj_set_style_text_color(ui_LabelDeviceOnline, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_label_set_text(ui_LabelDeviceOnline, "Offline");
            lv_obj_set_style_text_color(ui_LabelDeviceOnline, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    if (ui_LabelSpeed != NULL && g_motor_rotate_speed != prev_rotate_speed) {
        prev_rotate_speed = g_motor_rotate_speed;
        snprintf(g_value_str, sizeof(g_value_str), "%d", g_motor_rotate_speed);
        lv_label_set_text(ui_LabelSpeed, g_value_str);
    }

    if (ui_LabelPosition != NULL && g_motor_position != prev_position) {
        prev_position = g_motor_position;
        int pos_int = (int)g_motor_position;
        int pos_frac = (int)((g_motor_position - (float)pos_int) * 100.0f);
        if (pos_frac < 0) pos_frac = -pos_frac;
        snprintf(g_value_str, sizeof(g_value_str), "%d.%02d R", pos_int, pos_frac);
        lv_label_set_text(ui_LabelPosition, g_value_str);
    }

    if (ui_LabelPhaseCurrent != NULL && g_motor_phase_current != prev_phase_current) {
        prev_phase_current = g_motor_phase_current;
        snprintf(g_value_str, sizeof(g_value_str), "%d mA", g_motor_phase_current);
        lv_label_set_text(ui_LabelPhaseCurrent, g_value_str);
    }

    if (ui_Label5 != NULL) {
        lv_label_set_text(ui_Label5, Instruction_text);
    }
}

// ====================================================================
//  AI Chat Message System — 全局链表实现
//  特点：malloc 堆分配（不占任务栈）、12条上限 FIFO 淘汰、
//        malloc 失败时有静态后备缓冲、全局变量页面跳转不丢失
// ====================================================================

static ai_msg_node_t *g_ai_head = NULL;
static ai_msg_node_t *g_ai_tail = NULL;
static uint8_t        g_ai_count = 0;

// malloc 失败时的后备静态缓冲
static char g_fallback_question[AI_MSG_FALLBACK_LEN];
static char g_fallback_answer[AI_MSG_FALLBACK_LEN];

void ai_msg_init(void)
{
    ai_msg_clear();
}

uint8_t ai_msg_count(void)
{
    return g_ai_count;
}

ai_msg_node_t *ai_msg_get_head(void)
{
    return g_ai_head;
}

void ai_msg_clear(void)
{
    if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    ai_msg_node_t *cur = g_ai_head;
    while (cur != NULL) {
        ai_msg_node_t *next = cur->next;
        if (cur->question != g_fallback_question) free(cur->question);
        if (cur->answer   != g_fallback_answer)   free(cur->answer);
        free(cur);
        cur = next;
    }
    g_ai_head  = NULL;
    g_ai_tail  = NULL;
    g_ai_count = 0;
    if (g_data_mutex) xSemaphoreGive(g_data_mutex);
}

bool ai_msg_add(const char *question, const char *answer)
{
    if (question == NULL || answer == NULL) return false;

    if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);

    // 分配节点
    ai_msg_node_t *node = (ai_msg_node_t *)malloc(sizeof(ai_msg_node_t));
    if (node == NULL) {
        ESP_LOGE(TAG2, "AI_MSG: node malloc failed!");
        return false;
    }

    // 分配 question 字符串（后备：静态缓冲）
    size_t q_len = strlen(question) + 1;
    node->question = (char *)malloc(q_len);
    if (node->question == NULL) {
        ESP_LOGW(TAG2, "AI_MSG: question malloc failed, using fallback buffer");
        strncpy(g_fallback_question, question, AI_MSG_FALLBACK_LEN - 1);
        g_fallback_question[AI_MSG_FALLBACK_LEN - 1] = '\0';
        node->question = g_fallback_question;
    } else {
        memcpy(node->question, question, q_len);
    }

    // 分配 answer 字符串（后备：静态缓冲）
    size_t a_len = strlen(answer) + 1;
    node->answer = (char *)malloc(a_len);
    if (node->answer == NULL) {
        ESP_LOGW(TAG2, "AI_MSG: answer malloc failed, using fallback buffer");
        strncpy(g_fallback_answer, answer, AI_MSG_FALLBACK_LEN - 1);
        g_fallback_answer[AI_MSG_FALLBACK_LEN - 1] = '\0';
        node->answer = g_fallback_answer;
    } else {
        memcpy(node->answer, answer, a_len);
    }

    node->next = NULL;

    // 超过上限：FIFO 淘汰最旧节点
    if (g_ai_count >= AI_MSG_MAX_COUNT) {
        ai_msg_node_t *old = g_ai_head;
        g_ai_head = old->next;
        if (g_ai_head == NULL) g_ai_tail = NULL;
        if (old->question != g_fallback_question) free(old->question);
        if (old->answer   != g_fallback_answer)   free(old->answer);
        free(old);
        g_ai_count--;
        ESP_LOGI(TAG2, "AI_MSG: evicted oldest (count=%d/%d)", g_ai_count, AI_MSG_MAX_COUNT);
    }

    // 追加到链表尾部
    if (g_ai_head == NULL) {
        g_ai_head = node;
        g_ai_tail = node;
    } else {
        g_ai_tail->next = node;
        g_ai_tail = node;
    }
    g_ai_count++;

    ESP_LOGI(TAG2, "AI_MSG: added Q&A (count=%d/%d)", g_ai_count, AI_MSG_MAX_COUNT);
    if (g_data_mutex) xSemaphoreGive(g_data_mutex);
    return true;
}