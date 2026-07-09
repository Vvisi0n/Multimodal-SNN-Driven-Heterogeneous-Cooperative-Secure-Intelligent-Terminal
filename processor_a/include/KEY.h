#ifndef KEY_H
#define KEY_H

#include <Arduino.h>

#define KEY1_PIN 10
#define KEY2_PIN 11
#define KEY3_PIN 12

#define DEBOUNCE_DELAY 50

#ifdef __cplusplus
extern "C" {
#endif

extern int KEY1_isPressed;
extern int KEY2_isPressed;
extern int KEY3_isPressed;

void KEY_Init(void);
void KEY_Check(void);
void KEY_CreateTask(void);

#ifdef __cplusplus
}
#endif

#endif