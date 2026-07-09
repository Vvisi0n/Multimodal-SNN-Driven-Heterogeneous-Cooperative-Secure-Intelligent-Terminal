#ifndef MIC_H
#define MIC_H

#include <Arduino.h>

#define MIC_PIN 7

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t MIC_Value;
extern uint32_t MIC_Amp;
extern uint32_t MIC_ADC;

void MIC_Init(void);
void MIC_CreateTask(void);

#ifdef __cplusplus
}
#endif

#endif