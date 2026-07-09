#ifndef BLE_H
#define BLE_H

#include <stdint.h>

#define BLE_BUF_SIZE             256

#define BLE_DEVICE_NAME          "ESP32S3_VIS_QS"

#define HOST_BLE_NAME            "ESP32S3_Host_QS"
#define HOST_SERVICE_UUID        "0000FFE0-0000-1000-8000-00805F9B34FB"
#define HOST_CHAR_FFE1_UUID      "0000FFE1-0000-1000-8000-00805F9B34FB"
#define HOST_CHAR_FFE2_UUID      "0000FFE2-0000-1000-8000-00805F9B34FB"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t  BLE_RxBuffer[BLE_BUF_SIZE];
extern uint16_t BLE_RxLen;
extern uint8_t  BLE_NewDataFlag;

void BLE_Init(void);
void BLE_Send(uint8_t *data, uint16_t len);
uint8_t BLE_IsConnected(void);

#ifdef __cplusplus
}
#endif

#endif