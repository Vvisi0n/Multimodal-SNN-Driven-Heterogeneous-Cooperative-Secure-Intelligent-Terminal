#include <Arduino.h>
#include "TriLED.h"

#define LED_PIN 48
Adafruit_NeoPixel strip(1, LED_PIN, NEO_GRB + NEO_KHZ800);

void TLED_Init(void) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    strip.begin();
    strip.show();
    strip.setBrightness(50);
}

void TLED_ON(int color) {
    switch(color) {
        case TLED_COLOR_RED:
            strip.setPixelColor(0, strip.Color(255, 0, 0));
            break;
        case TLED_COLOR_GREEN:
            strip.setPixelColor(0, strip.Color(0, 255, 0));
            break;
        case TLED_COLOR_BLUE:
            strip.setPixelColor(0, strip.Color(0, 0, 255));
            break;
    }
    strip.show();
}

void TLED_OFF(void) {
    strip.setPixelColor(0, strip.Color(0, 0, 0));
    strip.show();
}

void TLED_SetBrightness(uint8_t brightness) {
    strip.setBrightness(brightness);
}