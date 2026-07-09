#include "INA.h"

void setup() {
  Serial.begin(115200);
  
  // 初始化传感器
  Serial.println("正在初始化 INA219...");
  if (!INA_Init()) {
    Serial.println("INA219 初始化失败，请检查引脚连接！");
    while (1); // 挂起
  }
  Serial.println("INA219 初始化成功！");
}

void loop() {
  // 1. 刷新数据
  INA_UpdateData();

  // 2. 直接从全局结构体变量 `inaData` 中读取数据
  Serial.print("分流电压 [mV]: "); Serial.println(inaData.shuntVoltage_mV);
  Serial.print("总线电压 [V]:  "); Serial.println(inaData.busVoltage_V);
  Serial.print("负载电压 [V]:  "); Serial.println(inaData.loadVoltage_V);
  Serial.print("当前电流 [mA]: "); Serial.println(inaData.current_mA);
  Serial.print("当前功率 [mW]: "); Serial.println(inaData.power_mW);

  if (inaData.overflow) {
    Serial.println("警告: 数据溢出！请调整量程或更换分流电阻。");
  } else {
    Serial.println("状态: 数据正常");
  }
  
  Serial.println("------------------------------------");
  delay(3000); // 每3秒刷新一次
}