#include "KEY.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

int KEY1_isPressed = 0;
int KEY2_isPressed = 0;
int KEY3_isPressed = 0;

typedef struct {
  int pin;
  int lastState;
  int currentState;
  unsigned long lastDebounceTime;
} Button;

static Button buttons[3];

void KEY_Init(void) {
  buttons[0].pin = KEY1_PIN;
  buttons[0].lastState = HIGH;
  buttons[0].currentState = HIGH;
  buttons[0].lastDebounceTime = 0;
  
  buttons[1].pin = KEY2_PIN;
  buttons[1].lastState = HIGH;
  buttons[1].currentState = HIGH;
  buttons[1].lastDebounceTime = 0;
  
  buttons[2].pin = KEY3_PIN;
  buttons[2].lastState = HIGH;
  buttons[2].currentState = HIGH;
  buttons[2].lastDebounceTime = 0;
  
  for (int i = 0; i < 3; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].lastState = digitalRead(buttons[i].pin);
    buttons[i].currentState = buttons[i].lastState;
  }
}

static void KEY_CheckButton(Button *btn, int *isPressed) {
  int reading = digitalRead(btn->pin);
  
  if (reading != btn->lastState) {
    btn->lastDebounceTime = millis();
  }
  
  if ((millis() - btn->lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != btn->currentState) {
      btn->currentState = reading;
      
      if (btn->currentState == LOW) {
        *isPressed = 1;
      } else {
        *isPressed = 0;
      }
    }
  }
  
  btn->lastState = reading;
}

void KEY_Check(void) {
  KEY_CheckButton(&buttons[0], &KEY1_isPressed);
  KEY_CheckButton(&buttons[1], &KEY2_isPressed);
  KEY_CheckButton(&buttons[2], &KEY3_isPressed);
}

static void KEY_Task(void *parameter) {
  while (1) {
    KEY_Check();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void KEY_CreateTask(void) {
  xTaskCreatePinnedToCore(
    KEY_Task,
    "KEY_Task",
    1024,
    NULL,
    1,
    NULL,
    0
  );
}