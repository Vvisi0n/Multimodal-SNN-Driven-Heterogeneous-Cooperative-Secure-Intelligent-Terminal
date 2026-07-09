#include "MPU.h"
#include <Wire.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static MPU6050 mpu(0x68);
static bool dmpReady = false;
static uint16_t packetSize; 
static uint8_t fifoBuffer[64];

MPU_Data mpuData = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

static volatile bool mpuInterrupt = false;
static void IRAM_ATTR dmpDataReady() {
    mpuInterrupt = true;
}

bool MPU_Init(void) {
    Serial.println("MPU: Initializing I2C...");
    Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
    Wire.setClock(100000);
    delay(200);

    // 快速检测设备是否存在（最多尝试 2 次，避免长时间阻塞）
    Serial.println("MPU: Probing device...");
    bool deviceFound = false;
    for (int retry = 0; retry < 2; retry++) {
        Wire.beginTransmission(0x68);
        if (Wire.endTransmission() == 0) {
            deviceFound = true;
            break;
        }
        if (retry < 1) {
            delay(50);
        }
    }

    if (!deviceFound) {
        Serial.println("MPU: Device NOT found! (2 probes failed)");
        return false;
    }
    Serial.println("MPU: Device found!");

    Serial.println("MPU: Initializing device...");
    mpu.initialize();
    delay(200);
    
    Serial.println("MPU: Testing connection...");
    if (!mpu.testConnection()) {
        Serial.println("MPU: Connection FAILED!");
        return false;
    }
    Serial.println("MPU: Connection OK");

    Serial.println("MPU: Waking up device...");
    mpu.setSleepEnabled(false);
    delay(100);

    Serial.println("MPU: Initializing DMP...");
    uint8_t devStatus = mpu.dmpInitialize();
    Serial.printf("MPU: DMP Status: %d\n", devStatus);

    if (devStatus == 0) {
        Serial.println("MPU: Setting offsets...");
        mpu.setXAccelOffset(0);
        mpu.setYAccelOffset(0);
        mpu.setZAccelOffset(1688);
        mpu.setXGyroOffset(0);
        mpu.setYGyroOffset(0);
        mpu.setZGyroOffset(0);
        delay(100);

        Serial.println("MPU: Enabling DMP...");
        mpu.setDMPEnabled(true);
        delay(100);
        
        packetSize = mpu.dmpGetFIFOPacketSize();
        dmpReady = true;
        
        Serial.printf("MPU: DMP Ready! Packet Size: %d\n", packetSize);
        Serial.println("MPU: Increasing I2C speed to 400kHz...");
        Wire.setClock(400000);
        return true;
    } else {
        Serial.printf("MPU: DMP Init FAILED with error: %d\n", devStatus);
        Serial.println("MPU: Trying without DMP...");
        return false;
    }
}

bool MPU_UpdateData(void) {
    if (!dmpReady) return false;

    uint16_t fifoCount = mpu.getFIFOCount();
    if (fifoCount >= packetSize) {
        if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
            Quaternion q;
            VectorFloat gravity;
            float ypr[3];
            VectorInt16 aa;
            VectorInt16 gy;

            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetGravity(&gravity, &q);
            mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

            mpuData.yaw   = ypr[0] * 180.0 / M_PI;
            mpuData.pitch = ypr[1] * 180.0 / M_PI;
            mpuData.roll  = ypr[2] * 180.0 / M_PI;

            mpu.dmpGetAccel(&aa, fifoBuffer);
            mpu.dmpGetGyro(&gy, fifoBuffer);

            mpuData.accelX = (float)aa.x / 16384.0;
            mpuData.accelY = (float)aa.y / 16384.0;
            mpuData.accelZ = (float)aa.z / 16384.0;

            mpuData.gyroX  = (float)gy.x / 16.4;
            mpuData.gyroY  = (float)gy.y / 16.4;
            mpuData.gyroZ  = (float)gy.z / 16.4;

            return true;
        }
    }
    return false;
}

static void MPU_Task(void *parameter) {
    while (1) {
        MPU_UpdateData();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void MPU_CreateTask(void) {
    xTaskCreatePinnedToCore(
        MPU_Task,
        "MPU_Task",
        4096,
        NULL,
        1,
        NULL,
        0
    );
}