#include "BUZZER.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/gpio.h"

static TaskHandle_t buzzerTaskHandle = NULL;
static TaskHandle_t buzzerLoopTaskHandle = NULL;

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

static void BUZZER_Task(void *parameter) {
  uint16_t duration = (uint16_t)(uintptr_t)parameter;
  
  gpio_set_level((gpio_num_t)BUZZER_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(duration));
  gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
  
  buzzerTaskHandle = NULL;
  vTaskDelete(NULL);
}

static void BUZZER_LoopTask(void *parameter) {
  (void)parameter;
  while (1) {
    gpio_set_level((gpio_num_t)BUZZER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

void BUZZER_On(uint16_t buzzer_ms) {
  if (buzzerTaskHandle != NULL) {
    vTaskDelete(buzzerTaskHandle);
    buzzerTaskHandle = NULL;
    gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
  }
  
  xTaskCreatePinnedToCore(
    BUZZER_Task,
    "BUZZER_Task",
    512,
    (void *)(uintptr_t)buzzer_ms,
    1,
    &buzzerTaskHandle,
    0
  );
}

void BUZZER_BeepLoop(void) {
  if (buzzerTaskHandle != NULL) {
    vTaskDelete(buzzerTaskHandle);
    buzzerTaskHandle = NULL;
    gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
  }
  if (buzzerLoopTaskHandle != NULL) {
    vTaskDelete(buzzerLoopTaskHandle);
    buzzerLoopTaskHandle = NULL;
  }

  xTaskCreatePinnedToCore(
    BUZZER_LoopTask,
    "BUZZER_Loop",
    1024,
    NULL,
    1,
    &buzzerLoopTaskHandle,
    0
  );
}

void BUZZER_BeepOnce(void) {
  BUZZER_On(750);
}

void BUZZER_Stop(void) {
  if (buzzerTaskHandle != NULL) {
    vTaskDelete(buzzerTaskHandle);
    buzzerTaskHandle = NULL;
  }
  if (buzzerLoopTaskHandle != NULL) {
    vTaskDelete(buzzerLoopTaskHandle);
    buzzerLoopTaskHandle = NULL;
  }
  gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
}