#ifndef BLE_H
#define BLE_H

#include <Arduino.h>

#define BLE_BUF_SIZE          512
#define MAX_BLE_CONNECTIONS   4

#define BLE_DEVICE_NAME       "ESP32S3_Host_QS"

#define BLE_SERVICE_UUID16    0xFFE0
#define BLE_CHAR_TX_UUID16    0xFFE2
#define BLE_CHAR_RX_UUID16    0xFFE1

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t  BLE_RxBuffer[BLE_BUF_SIZE];
extern uint16_t BLE_RxLen;
extern uint8_t  BLE_NewDataFlag;
extern uint8_t  BLE_ConnIndex;
extern uint8_t  BLE_DebugEnable;   /* 1=开启回声调试 */

void BLE_Init(void);
void BLE_Send(uint8_t conn_idx, uint8_t *data, uint16_t len);
void BLE_Broadcast(uint8_t *data, uint16_t len);
void BLE_CreateTask(void);

#ifdef __cplusplus
}
#endif

#endif