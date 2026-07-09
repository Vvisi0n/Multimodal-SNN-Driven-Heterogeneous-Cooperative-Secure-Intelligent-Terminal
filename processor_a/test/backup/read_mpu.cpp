#include "MPU.h"

void setup() {
  Serial.begin(115200);
  if (!MPU_Init()) {
    while (1);
  }
}

void loop() {
  if (MPU_UpdateData()) {
    // 打印角度
    Serial.print("角度 -> Y: "); Serial.print(mpuData.yaw, 1);
    Serial.print(" P: "); Serial.print(mpuData.pitch, 1);
    Serial.print(" R: "); Serial.print(mpuData.roll, 1);

    // 打印加速度 (当模块平放静止时，X和Y应该接近 0，Z轴因为有地心引力，应该接近 1.0g)
    Serial.print(" | 加速度(g) -> X: "); Serial.print(mpuData.accelX, 2);
    Serial.print(" Y: "); Serial.print(mpuData.accelY, 2);
    Serial.print(" Z: "); Serial.print(mpuData.accelZ, 2);

    // 打印角速度 (静止时三个轴都应该接近 0)
    Serial.print(" | 角速度(°/s) -> X: "); Serial.print(mpuData.gyroX, 1);
    Serial.print(" Y: "); Serial.print(mpuData.gyroY, 1);
    Serial.print(" Z: "); Serial.println(mpuData.gyroZ, 1);
  }
  delay(10); 
}