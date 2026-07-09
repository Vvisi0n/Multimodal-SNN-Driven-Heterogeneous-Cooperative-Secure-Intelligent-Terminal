#ifndef DHT11_H
#define DHT11_H

#include <Arduino.h>

#define DHT11_PIN 19

#ifdef __cplusplus
extern "C" {
#endif

extern float DHT11_Temp;
extern float DHT11_Humi;

void DHT11_Init(void);
void DHT11_CreateTask(void);

#ifdef __cplusplus
}
#endif

#endif