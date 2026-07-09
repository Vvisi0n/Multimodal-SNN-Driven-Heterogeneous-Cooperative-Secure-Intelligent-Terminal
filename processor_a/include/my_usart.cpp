#include "my_usart.h"
#include "my_motor.h"
#include "MPU.h"
#include "INA.h"
#include "MIC.h"
#include "DHT11.h"
#include "BUZZER.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <HardwareSerial.h>

static HardwareSerial MCU1_Serial(1);
static HardwareSerial MCU3_Serial(2);

uint8_t MCU1_RxBuffer[MCU1_BUF_SIZE];
uint16_t MCU1_RxLen = 0;
uint8_t MCU1_NewDataFlag = 0;

uint8_t MCU3_RxBuffer[MCU1_BUF_SIZE];
uint16_t MCU3_RxLen = 0;
uint8_t MCU3_NewDataFlag = 0;

void MCU1_UART_Init(void) {
    MCU1_Serial.begin(MCU1_UART_BAUD, SERIAL_8N1, MCU1_RX, MCU1_TX);
    MCU1_RxLen = 0;
    MCU1_NewDataFlag = 0;
}

void MCU1_UART_Send(uint8_t *data, uint16_t len) {
    MCU1_Serial.write(data, len);
}

void MCU1_UART_Print(const char *str) {
    MCU1_Serial.print(str);
}

void MCU3_UART_Init(void) {
    MCU3_Serial.begin(MCU3_UART_BAUD, SERIAL_8N1, MCU3_RX, MCU3_TX);
    MCU3_RxLen = 0;
    MCU3_NewDataFlag = 0;
}

void MCU3_UART_Send(uint8_t *data, uint16_t len) {
    MCU3_Serial.write(data, len);
}

void MCU3_UART_Print(const char *str) {
    MCU3_Serial.print(str);
}

static void uart_process_cmd(const char *cmd, HardwareSerial &respSerial) {
    if (strcmp(cmd, "wf stop") == 0) {
        motor_wf_stop();
        respSerial.print("OK:wf_stop\r\n");
    }
    else if (strcmp(cmd, "[buzzer]beep_loop") == 0) {
        BUZZER_BeepLoop();
        respSerial.print("OK:buzzer_beep_loop\r\n");
    }
    else if (strcmp(cmd, "[buzzer]beep_once") == 0) {
        BUZZER_BeepOnce();
        respSerial.print("OK:buzzer_beep_once\r\n");
    }
    else if (strcmp(cmd, "[buzzer]stop") == 0) {
        BUZZER_Stop();
        respSerial.print("OK:buzzer_stop\r\n");
    }
    else if (strcmp(cmd, "QzStop") == 0) {
        motor_qz_stop();
        respSerial.print("OK:QzStop\r\n");
    }
    else if (strcmp(cmd, "QzUnstop") == 0) {
        motor_qz_unstop();
        respSerial.print("OK:QzUnstop\r\n");
    }
}

static void uart_read_line(HardwareSerial &ser, const char *tag,
                           uint8_t *rxBuf, uint16_t *rxLen, uint8_t *newFlag,
                           uint8_t *tempBuf, uint16_t *tempLen, unsigned long *lastCharTime)
{
    while (ser.available() > 0) {
        char c = ser.read();
        *lastCharTime = millis();
        if (c == '\n' || c == '\r') {
            if (*tempLen > 0) {
                tempBuf[*tempLen] = '\0';
                memcpy(rxBuf, tempBuf, *tempLen + 1);
                *rxLen = *tempLen;
                *newFlag = 1;
                Serial.printf("[%s] '%s' (len=%d)\r\n", tag, tempBuf, *tempLen);
                ser.write(tempBuf, *tempLen);
                uart_process_cmd((const char*)tempBuf, ser);
                *tempLen = 0;
            }
            continue;
        }
        if (*tempLen < MCU1_BUF_SIZE - 1) {
            tempBuf[(*tempLen)++] = c;
        }
    }
}

static void uart_flush_timeout(HardwareSerial &ser, const char *tag,
                               uint8_t *rxBuf, uint16_t *rxLen, uint8_t *newFlag,
                               uint8_t *tempBuf, uint16_t *tempLen, unsigned long *lastCharTime)
{
    if (*tempLen > 0 && (millis() - *lastCharTime > 5)) {
        tempBuf[*tempLen] = '\0';
        memcpy(rxBuf, tempBuf, *tempLen + 1);
        *rxLen = *tempLen;
        *newFlag = 1;
        Serial.printf("[%s] '%s' (len=%d)\r\n", tag, tempBuf, *tempLen);
        ser.write(tempBuf, *tempLen);
        uart_process_cmd((const char*)tempBuf, ser);
        *tempLen = 0;
    }
}

static void uart_send_sensor_data(HardwareSerial &ser) {
    char fmtBuf[32];

    snprintf(fmtBuf, sizeof(fmtBuf), "[I]%.2f\n", inaData.current_mA);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[V]%.2f\n", inaData.loadVoltage_V);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[MPU_ax]%.1f\n", mpuData.accelX);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[MPU_ay]%.1f\n", mpuData.accelY);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[MPU_az]%.1f\n", mpuData.accelZ);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[MIC_amp]%.1f\n", (float)MIC_Amp);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[MIC_adc]%.1f\n", (float)MIC_ADC);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[Temp]%.1f\n", DHT11_Temp);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[Humi]%.1f\n", DHT11_Humi);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[phase_current]%d\n", (int)motorParams.phase_current);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[rotate_speed]%d\n", (int)motorParams.rotate_speed);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[mt_position]%.2f\n", (double)motorParams.position / 51200.0);
    ser.print(fmtBuf);
    snprintf(fmtBuf, sizeof(fmtBuf), "[mt_online]%d\n", motorParams.mt_online);
    ser.print(fmtBuf);
}

static void MCU1_UART_Task(void *parameter) {
    uint8_t tempBuf1[MCU1_BUF_SIZE];
    uint16_t tempLen1 = 0;
    unsigned long lastCharTime1 = 0;

    uint8_t tempBuf3[MCU1_BUF_SIZE];
    uint16_t tempLen3 = 0;
    unsigned long lastCharTime3 = 0;

    unsigned long lastSendTime = 0;
    
    while (1) {
        uart_read_line(MCU1_Serial, "MCU1_RX",
                       MCU1_RxBuffer, &MCU1_RxLen, &MCU1_NewDataFlag,
                       tempBuf1, &tempLen1, &lastCharTime1);

        uart_read_line(MCU3_Serial, "MCU3_RX",
                       MCU3_RxBuffer, &MCU3_RxLen, &MCU3_NewDataFlag,
                       tempBuf3, &tempLen3, &lastCharTime3);

        uart_flush_timeout(MCU1_Serial, "MCU1_RX",
                           MCU1_RxBuffer, &MCU1_RxLen, &MCU1_NewDataFlag,
                           tempBuf1, &tempLen1, &lastCharTime1);

        uart_flush_timeout(MCU3_Serial, "MCU3_RX",
                           MCU3_RxBuffer, &MCU3_RxLen, &MCU3_NewDataFlag,
                           tempBuf3, &tempLen3, &lastCharTime3);
        
        if (millis() - lastSendTime >= 500) {
            lastSendTime = millis();
            uart_send_sensor_data(MCU1_Serial);
            uart_send_sensor_data(MCU3_Serial);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void MCU1_UART_CreateTask(void) {
    xTaskCreatePinnedToCore(
        MCU1_UART_Task,
        "MCU1_UART_Task",
        4096,
        NULL,
        2,
        NULL,
        0
    );
}