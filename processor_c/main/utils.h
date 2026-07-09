#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 跨核共享数据互斥锁（Core 0 ↔ Core 1）
extern xSemaphoreHandle g_data_mutex;

extern volatile uint8_t is_wifi;
extern uint8_t Open_Wifi;
extern uint8_t Open_mqtt;
extern uint8_t Open_mqtt_upload;
extern uint8_t Need_login;
extern char login_username[32];
extern char login_password[32];

// 解析命令字符串
uint8_t Command_Analysis(char *cmd_str);

// 全局字符串缓冲区（所有转换函数共用这个）
extern char g_value_str[64];

// 转换 uint8_t  → 字符串
char* uint8_to_str(uint8_t value);

// 转换 int      → 字符串
char* int_to_str(int value);

// 转换 float    → 字符串（保留1位小数）
char* float_to_str(float value);

// 转换 float    → 自定义小数位数
char* float_to_str_precise(float value, uint8_t decimals);

// float 转字符串 + 自定义后缀（如 "k"、"℃"、"%"）
char* float_to_str_suffix(float value, uint8_t decimals, const char* suffix);

// float → 字符串（安全版：纯整数运算，不依赖 %f）
char* safe_ftoa_1d(float val);
char* safe_ftoa_1d_suffix(float val, const char* suffix);
char* safe_utoa(uint8_t val);

// 向图表曲线1注入数据（lv_coord_t 支持负数，适用于 -50~50 的加速度数据）
uint8_t Add_data2Chart_1(lv_coord_t value);
// 向图表曲线2注入数据
uint8_t Add_data2Chart_2(lv_coord_t value);
// 向图表曲线3注入数据
uint8_t Add_data2Chart_3(lv_coord_t value);
// 向图表曲线4注入数据（声源大小，副Y轴 700~900）
uint8_t Add_data2Chart_4(lv_coord_t value);
// 向图表2曲线1注入数据（I-U-RMP图表第一列）
uint8_t Add_data2Chart2_1(uint16_t value);
// 向图表2曲线2注入数据（I-U-RMP图表第二列）
uint8_t Add_data2Chart2_2(uint16_t value);

// 图表数据数组（全局管理）
#define CHART2_DATA_POINTS 10
extern lv_coord_t ui_Chart1_series_1_array[];
extern lv_coord_t ui_Chart1_series_2_array[];
extern lv_coord_t ui_Chart1_series_3_array[];
extern lv_coord_t ui_Chart1_series_4_array[];
extern lv_coord_t ui_Chart2_series_1_array[CHART2_DATA_POINTS];
extern lv_coord_t ui_Chart2_series_2_array[CHART2_DATA_POINTS];

// Screen4 标签文本缓冲区
extern char g_label_device_online[32];
extern char g_label_speed[32];
extern char g_label_position[32];
extern char g_label_phase_current[32];

// Screen4 MCU1 电机实时数据（Core 0 串口写入，Core 1 LVGL定时器读取）
extern volatile int      g_motor_phase_current;   // 相电流 (mA)
extern volatile int      g_motor_rotate_speed;    // 转速 (RPM)
extern volatile float    g_motor_position;        // 位置 (R)
extern volatile int      g_motor_online;          // 设备在线状态: 1=Online, 0=Offline

// 麦克风传感器数据（全局变量）
extern float MIC_Amp;
extern float MIC_ADC;
extern float Temperature;
extern float Humidity;
extern uint16_t sound_level;
extern uint8_t jitter_level;
// 原始传感器数据（未缩放，供 MQTT JSON 上报使用）
extern float g_sensor_current;
extern float g_sensor_voltage;
extern float g_sensor_accel_x;
extern float g_sensor_accel_y;
extern float g_sensor_accel_z;

// 滑动窗口历史记录（最近7个值，字符串格式）
#define HISTORY_WINDOW 7
extern char sound_level_history[HISTORY_WINDOW][16];
extern char jitter_level_history[HISTORY_WINDOW][16];

// 图表待推送数据（Core 0 写，Core 1 读，lv_chart_set_next_value 在 Core 1 定时器中安全调用）
extern volatile lv_coord_t g_chart1_val1;
extern volatile lv_coord_t g_chart1_val2;
extern volatile lv_coord_t g_chart1_val3;
extern volatile lv_coord_t g_chart1_val4;
extern volatile lv_coord_t g_chart2_val1;
extern volatile lv_coord_t g_chart2_val2;

// 刷新所有Screen2标签（由LVGL定时器调用，必须在LVGL线程中执行）
void ui_refresh_screen2_labels(void);

// 刷新所有Screen4标签（由LVGL定时器调用，必须在LVGL线程中执行）
void ui_refresh_screen4_labels(void);

// ====== AI Chat Message System (全局链表，页面跳转不丢失) ======

typedef struct ai_msg_node {
    char *question;               // 堆分配，NULL表示分配失败
    char *answer;                 // 堆分配，NULL表示分配失败
    struct ai_msg_node *next;
} ai_msg_node_t;

#define AI_MSG_MAX_COUNT    12    // 最多12条问答，超出FIFO移出旧数据
#define AI_MSG_FALLBACK_LEN 64    // malloc失败时的静态后备缓冲区长度

void  ai_msg_init(void);
bool  ai_msg_add(const char *question, const char *answer);
void  ai_msg_clear(void);
ai_msg_node_t *ai_msg_get_head(void);
uint8_t ai_msg_count(void);


#ifdef __cplusplus
}
#endif

#endif // __UTILS_H__