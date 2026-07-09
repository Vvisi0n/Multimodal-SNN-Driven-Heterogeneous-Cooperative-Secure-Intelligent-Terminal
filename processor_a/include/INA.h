#ifndef INA_H
#define INA_H

#include <Arduino.h>

// ESP32-S3 引脚定义
#define I2C_SDA 8
#define I2C_SCL 9
#define INA219_I2C_ADDRESS 0x40

// INA219 数据结构体定义
typedef struct {
    float shuntVoltage_mV;  // 分流电阻电压 (mV)
    float busVoltage_V;     // 总线电压 (V)
    float loadVoltage_V;    // 负载电压 (V)
    float current_mA;       // 电流 (mA)
    float power_mW;         // 功率 (mW)
    bool overflow;          // 是否溢出 (true = 溢出)
} INA219_Data;

// 全局结构体变量声明
extern INA219_Data inaData;

// 函数声明 (C 语言风格)
bool INA_Init(void);
void INA_UpdateData(void);
void INA_CreateTask(void);

#endif // INA_H