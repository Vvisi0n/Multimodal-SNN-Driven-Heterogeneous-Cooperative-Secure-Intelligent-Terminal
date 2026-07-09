#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

#define BUZZER_PIN 21

#ifdef __cplusplus
extern "C" {
#endif

void BUZZER_Init(void);
void BUZZER_On(uint16_t buzzer_ms);
void BUZZER_BeepLoop(void);
void BUZZER_BeepOnce(void);
void BUZZER_Stop(void);

#ifdef __cplusplus
}
#endif

#endif