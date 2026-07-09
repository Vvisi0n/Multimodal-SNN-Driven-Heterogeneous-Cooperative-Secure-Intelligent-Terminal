#include <Arduino.h>
const int buzzerPin = 21;

void setup() {
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, HIGH);
}

void loop() {
  digitalWrite(buzzerPin, LOW);
  delay(300);
  digitalWrite(buzzerPin, HIGH);
  delay(700);
}