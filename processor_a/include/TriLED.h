#ifndef TRILED_H
#define TRILED_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define TLED_COLOR_RED 0
#define TLED_COLOR_GREEN 1
#define TLED_COLOR_BLUE 2

void TLED_Init(void);
void TLED_ON(int color);
void TLED_OFF(void);
void TLED_SetBrightness(uint8_t brightness);

#endif