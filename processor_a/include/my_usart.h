#ifndef MY_USART_H
#define MY_USART_H

#include <Arduino.h>

#define MCU1_TX 16
#define MCU1_RX 15

#define MCU3_TX 13
#define MCU3_RX 14

#define MCU1_UART_BAUD 115200
#define MCU3_UART_BAUD 115200
#define MCU1_BUF_SIZE 256

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t MCU1_RxBuffer[MCU1_BUF_SIZE];
extern uint16_t MCU1_RxLen;
extern uint8_t MCU1_NewDataFlag;

extern uint8_t MCU3_RxBuffer[MCU1_BUF_SIZE];
extern uint16_t MCU3_RxLen;
extern uint8_t MCU3_NewDataFlag;

void MCU1_UART_Init(void);
void MCU1_UART_Send(uint8_t *data, uint16_t len);
void MCU1_UART_Print(const char *str);

void MCU3_UART_Init(void);
void MCU3_UART_Send(uint8_t *data, uint16_t len);
void MCU3_UART_Print(const char *str);

void MCU1_UART_CreateTask(void);

#ifdef __cplusplus
}
#endif

#endif