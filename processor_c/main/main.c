#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_port_fs.h"
#include "lv_demos.h"
#include <inttypes.h>
#include <stdlib.h>

#include "utils.h"
#include "ui/ui.h"
#include "wifi_udp/wifi.h"
#include "wifi_udp/wifi_udp.h"
#include "wifi_udp/mqtt.h"
#include "wifi_udp/llm_cloud.h"
#include "uart/uart.h"

static const char *TAG = "e^jt ";

void lvgl_hardWare_init() // 外围硬件初始化
{
    ESP_ERROR_CHECK(bsp_i2c_init(I2C_NUM_0, 400000));
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    lv_port_tick_init();
}

void lv_tick_task(void *arg)// LVGL 时钟任务
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(20));
        lv_task_handler();
    }
}


void Touch_IO_RST(void)
{

#if(GT911==1)
	//touch reset pin  低电平复位
	gpio_reset_pin(39);
	gpio_reset_pin(40);
	gpio_pullup_en(40);
	gpio_pullup_en(39);

	gpio_set_direction(39, GPIO_MODE_OUTPUT);
	gpio_set_direction(40, GPIO_MODE_OUTPUT);
	gpio_set_level(40, 1);
	gpio_set_level(39, 1);
	ESP_LOGI(TAG, "io39 set_high");
	vTaskDelay(pdMS_TO_TICKS(50));
	gpio_pulldown_en(39);
	gpio_pulldown_en(40);
	gpio_set_level(39, 0);
	gpio_set_level(40, 0);
	ESP_LOGI(TAG, "io39 set_low");

	vTaskDelay(pdMS_TO_TICKS(20));
	gpio_set_level(40, 1);
	vTaskDelay(pdMS_TO_TICKS(20));
	gpio_pulldown_en(39);
	gpio_set_level(39, 0);
	//gpio_reset_pin(39);
	//gpio_set_direction(39, GPIO_MODE_INPUT);//中断脚没有用上，这只是用来配置地址
	
#elif(CST3240==1)

    gpio_reset_pin(39);
	gpio_reset_pin(40);
	
	gpio_set_direction( GPIO_NUM_40, GPIO_MODE_OUTPUT);//RST SET PORT OUTPUT
	gpio_set_level(40, 0);        //RST RESET IO
	vTaskDelay(pdMS_TO_TICKS(50));//DELAY 50ms 
	gpio_set_level(40, 1);        //SET RESET IO
	vTaskDelay(pdMS_TO_TICKS(10));//DELAY 10ms 	
#endif

}

void app_main(void)
{
    Touch_IO_RST();
    lvgl_hardWare_init();       // 外围硬件初始化
    
    // WiFi管理初始化（根据开关变量控制）
    if (Open_Wifi) {
        wifi_manager_init();
        ESP_LOGI(TAG, "WiFi function enabled");
    } else {
        ESP_LOGI(TAG, "WiFi function disabled");
    }
    
    // MQTT初始化（根据开关变量控制，异步执行避免阻塞UI）
    if (Open_mqtt) {
        xTaskCreatePinnedToCore(
            app_mqtt_and_wifi_init,
            "mqtt_init_task",
            1024 * 8,
            NULL,
            4,
            NULL,
            0
        );
        ESP_LOGI(TAG, "MQTT init task created on Core 0");
    } else {
        ESP_LOGI(TAG, "MQTT function disabled");
    }

    // 初始化LLM云服务
    llm_cloud_init();
    ESP_LOGI(TAG, "LLM Cloud function enabled");

    // 创建跨核互斥锁（必须在任务创建之前）
    g_data_mutex = xSemaphoreCreateMutex();
    
    // 串口初始化
    uart_custom_init();
	// 测试发送
    uart_custom_send("UART Inited!\n");

    ESP_LOGI(TAG, "init ok");
	// LVGL初始化
    ui_init();
    
	// 分配给Core0处理串口接收任务（始终运行，所有页面都可接收数据）
	xTaskCreatePinnedToCore(
        uart_rx_task,
        "uart_rx_task",
        1024 * 4,
        NULL,
        4,
        NULL,
        0
    );

    // 分配给Core1处理LVGL时钟任务
    xTaskCreatePinnedToCore(
        lv_tick_task,      		// 任务函数
        "lv_tick_task",    	// 任务名称
        1024 * 12,            // 栈大小（大屏UI给12K很安全）
        NULL,                 // 参数
        5,                    // 优先级提高到 5
        NULL,                 // 任务句柄
        1                     // 核心 ID 填 1，把刷屏工作彻底扔给空闲的 CPU Core 1
    );

    // 让 app_main 所在的 Core 0 纯粹去处理 Wi-Fi/MQTT 后台，互不干扰
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(100)); // 调大这里的延时，让主循环不空转占用 CPU0
    }
}