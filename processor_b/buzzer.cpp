#include "buzzer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static TaskHandle_t buzzer_task_handle = NULL;
static bool buzzer_loop_active = false;

static void buzzer_task(void *pvParameters) {
    uint16_t duration_ms = (uint16_t)(uint32_t)pvParameters;

    gpio_set_level((gpio_num_t)BUZZER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level((gpio_num_t)BUZZER_PIN, 0);

    buzzer_task_handle = NULL;
    vTaskDelete(NULL);
}

static void buzzer_loop_task(void *pvParameters) {
    (void)pvParameters;

    while (buzzer_loop_active) {
        gpio_set_level((gpio_num_t)BUZZER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    buzzer_task_handle = NULL;
    vTaskDelete(NULL);
}

void BUZZER_Init(void) {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << BUZZER_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
}

void BUZZER_On(uint16_t buzzer_ms) {
    BUZZER_Stop();

    if (buzzer_ms == 0) {
        return;
    }

    xTaskCreate(buzzer_task, "buzzer_on", 1024,
                (void *)(uint32_t)buzzer_ms, 1, &buzzer_task_handle);
}

void BUZZER_BeepOnce(void) {
    BUZZER_On(750);
}

void BUZZER_BeepLoop(void) {
    BUZZER_Stop();

    buzzer_loop_active = true;
    xTaskCreate(buzzer_loop_task, "buzzer_loop", 1024,
                NULL, 1, &buzzer_task_handle);
}

void BUZZER_Stop(void) {
    buzzer_loop_active = false;

    if (buzzer_task_handle != NULL) {
        vTaskDelete(buzzer_task_handle);
        buzzer_task_handle = NULL;
    }

    gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
}