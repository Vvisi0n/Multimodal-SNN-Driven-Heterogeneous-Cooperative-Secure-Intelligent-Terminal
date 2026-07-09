按键控制逻辑：
#if __Key__
static int lastKey1State = 0;
static int lastKey2State = 0;
static int lastKey3State = 0;

static void printKeyStatus() {
  if (KEY1_isPressed != lastKey1State) {
    lastKey1State = KEY1_isPressed;
    Serial.printf("KEY1 %s\n", KEY1_isPressed ? "Pressed" : "Released");
  }
  
  if (KEY2_isPressed != lastKey2State) {
    lastKey2State = KEY2_isPressed;
    Serial.printf("KEY2 %s\n", KEY2_isPressed ? "Pressed" : "Released");
  }
  
  if (KEY3_isPressed != lastKey3State) {
    lastKey3State = KEY3_isPressed;
    Serial.printf("KEY3 %s\n", KEY3_isPressed ? "Pressed" : "Released");
  }
}
#endif