#ifndef MPU_H
#define MPU_H

#include <Arduino.h>

#define MPU_SDA_PIN  4
#define MPU_SCL_PIN  5
#define MPU_INT_PIN  6   

// 升级版的姿态与传感器数据结构体
typedef struct {
    // 角度值 (单位: 度 °)
    float yaw;      
    float pitch;    
    float roll;     

    // 加速度值 (单位: g, 1g 约等于 9.8 m/s²)
    float accelX;
    float accelY;
    float accelZ;

    // 角速度值 (单位: °/s, 每秒转动的角度)
    float gyroX;
    float gyroY;
    float gyroZ;
} MPU_Data;

extern MPU_Data mpuData;

bool MPU_Init(void);
bool MPU_UpdateData(void);
void MPU_CreateTask(void);

#endif // MPU_H