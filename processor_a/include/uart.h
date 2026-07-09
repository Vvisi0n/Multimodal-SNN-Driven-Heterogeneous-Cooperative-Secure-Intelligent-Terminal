/**
 ****************************************************************************************************
 * @file        uart.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-12-01
 * @brief       UART 驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 */

#ifndef __UART_H
#define __UART_H

#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 引脚定义 */
/* 串口0默认已经使用了固定IO(GPIO43为U0TXD,GPIO44为U0RXD)  
 * 以下两个宏为串口1或串口2使用到的IO口
 */
#define TXD_PIN      42
#define RXD_PIN      41

/* 函数声明 */
void uart_init(uint8_t uartx, uint32_t baud);   /* uart初始化函数 */

#ifdef __cplusplus
}
#endif

#endif