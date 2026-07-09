#ifndef _UART_H_
#define _UART_H_

#include "driver/uart.h"

// 定义串口参数
#define UART_NUM_1          UART_NUM_1
#define UART_TX_PIN         17
#define UART_RX_PIN         18
#define UART_BAUD_RATE      115200
#define UART_BUF_SIZE       1024

// 初始化函数
void uart_custom_init(void);

// 发送数据函数
void uart_custom_send(const char *data);

// 接收任务函数
void uart_rx_task(void *pv);

// Screen2 可见时启用接收，离开时禁用（避免卡死界面切换）
void uart_rx_enable(void);
void uart_rx_disable(void);

#endif