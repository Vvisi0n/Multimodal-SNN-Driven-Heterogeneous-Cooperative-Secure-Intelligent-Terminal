#include "INA.h"
#include <Wire.h>
#include <INA219_WE.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static INA219_WE ina219(&Wire1, INA219_I2C_ADDRESS);
static bool ina_ready = false;

INA219_Data inaData = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false};

bool INA_Init(void) {
    Serial.println("INA: Wire1 begin...");
    Wire1.begin(I2C_SDA, I2C_SCL);
    Serial.println("INA: Wire1 begin OK");

    bool deviceFound = false;
    for (int retry = 0; retry < 2; retry++) {
        Wire1.beginTransmission(INA219_I2C_ADDRESS);
        if (Wire1.endTransmission() == 0) {
            deviceFound = true;
            break;
        }
        if (retry < 1) {
            delay(50);
        }
    }

    if (!deviceFound) {
        Serial.println("INA: Device not found!");
        return false;
    }
    Serial.println("INA: Device found, init library...");

    if (!ina219.init()) {
        Serial.println("INA: Library init failed!");
        return false;
    }

    Serial.println("INA: Init OK");
    ina_ready = true;
    return true;
}

void INA_UpdateData(void) {
    if (!ina_ready) return;

    inaData.shuntVoltage_mV = ina219.getShuntVoltage_mV();
    inaData.busVoltage_V    = ina219.getBusVoltage_V();
    inaData.current_mA      = ina219.getCurrent_mA();
    inaData.power_mW        = ina219.getBusPower();
    inaData.loadVoltage_V   = inaData.busVoltage_V + (inaData.shuntVoltage_mV / 1000.0f);
    inaData.overflow        = ina219.getOverflow();
}

static void INA_Task(void *parameter) {
    (void)parameter;
    while (1) {
        INA_UpdateData();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void INA_CreateTask(void) {
    xTaskCreatePinnedToCore(
        INA_Task,
        "INA_Task",
        2048,
        NULL,
        1,
        NULL,
        0
    );
}