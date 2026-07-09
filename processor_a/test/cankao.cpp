/**
 ******************************************************************************
 * @file     04_uart.ino
 * @author   正点原子团队(正点原子)
 * @version  V1.0
 * @date     2025-06-25
 * @brief    绝对位置模式控制 实验
 * @license  Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ******************************************************************************
 * 
 * 实验目的：学习和测试步进电机驱动器的指令
 *
 * 硬件资源及引脚分配： 
 * 1, UART0 --> ESP32S3 IO（用于下载程序和串口打印数据）
 *     TXD0 --> IO43
 *     RXD0 --> IO44
 * 2, UART1 --> ESP32S3 IO（用于与步进电机驱动器数据交互）
 *     TXD1 --> IO19
 *     RXD1 --> IO20
 * 3, LED --> ESP32S3 IO
 *    LED --> IO1
 * 4, XL9555 --> ESP32S3 IO
 *       SCL --> IO42
 *       SDA --> IO41
 *       INT --> IO0(记得用跳线帽连接IIC_INT和IO0，否则按键不起作用！！！) 
 *
 * 实验现象：
 * 1, 根据不同功能函数控制步进电机驱动器完成不用动作
 * 
 * 注意事项：
 * 1，注意Arduino ESP32S3 串口打印数据的格式是：UTF-8，如果使用XCOM（点击左下角齿轮设置）等串口调试助手时，要设置好编码方式，否则可能会乱码！！！
 * 2，IO0也是BOOT引脚，开发板重新上电时先拔出跳线帽，正常启动后再用跳线帽连接IIC_INT和IO0，否则可能系统可能无法启动！！！
 * 3，下载代码和打印串口数据都是使用串口1，注意切换连接。
 * 
 ******************************************************************************
 * 
 * 实验平台:正点原子 ESP32S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com/forum.php
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ******************************************************************************
 */

#include "led.h"
#include "xl9555.h"
#include "uart.h"
#include "smd.h"
#include "process_frame.h"


uint8_t t = 0;                 /* 时间记录 */
uint8_t g_rx_cmd[128];         /* 接收到的指令缓冲区 */

/**
 * @brief    接收和处理应答
 * @param    over_time : 超时时间（单位ms）
 * @retval   0：处理成功，1：缓冲区溢出，2：等待应答超时
 */
uint8_t handle_ack(uint32_t over_time)
{
    uint16_t rec_ct = 0;                              /* 累计接收到的数据长度 */
    SERIAL_FRAME g_serial_frame;
    unsigned long last_time = millis();               /* 记录上一时刻的时间 */

    while(1)
    {
        if(Serial1.available() > 0)                   /* 串口有数据进来 */
        {
            g_rx_cmd[rec_ct++] = Serial1.read();      /* 接收数据 */
            last_time = millis();                     /* 更新上一时刻的时间 */
        }
        else                                          /* 串口有没有数据 */
        {
            if(rec_ct > 128)                          /* 缓冲区溢出 */
            {
                Serial.printf("接收缓冲区溢出！\n");
                return 1;         /* 返回缓冲区溢出 */
            }

            if((millis() - last_time) > over_time)    /* 过了over_time毫秒都没有接收到数据，一帧数据结束 */
            {
                if (rec_ct == 0)  /* 没有接收到数据 */
                {
                    Serial.printf("接收超时，无数据！！！\n\n");
                    return 2;     /* 返回超时 */
                }

                for(uint8_t i = 0; i < rec_ct; i++)   /* 打印接收到的数据 */
                {
                    Serial.printf("%x ", g_rx_cmd[i]);
                }
                Serial.printf("\n");
                
                serial_frame_process((uint8_t *)g_rx_cmd, rec_ct, &g_serial_frame);

                break;           /* 退出while(1)循环 */
            }
        }
    }

    return 0;
}



/**
 * @brief    当程序开始执行时，将调用setup()函数，通常用来初始化变量、函数等(只执行一次)
 * @param    无
 * @retval   无
 */
void setup() 
{
    uart_init(0, 115200);   /* 串口0初始化 */
    uart_init(1, 115200);   /* 串口1初始化 */

    led_init();             /* LED初始化 */
    xl9555_init();          /* IO扩展芯片初始化 */

    xl9555_io_config(KEY0 | KEY1 | KEY2 | KEY3, IO_SET_INPUT);  /* 初始化IO扩展芯片用作按键的引脚为输入状态 */
    xl9555_io_config(BEEP, IO_SET_OUTPUT);                      /* 初始化IO扩展芯片用作蜂鸣器控制的引脚为输出状态 */
}

/**
 * @brief    循环函数，通常放程序的主体或者需要不断刷新的语句（循环执行）
 * @param    无
 * @retval   无
 */
void loop()
{
    if (!IIC_INT)   /* 端口有发生变化变为低电平 */
    {
        if (xl9555_get_pin(KEY0) == 0)
        {
            /***********************************************************
            * 绝对位置模式控制
            * 电机地址          ：1
            * 方向              ：0，顺时针
            * 加速度            ：200，加速度
            * 速度              ：6000 RPM，实际速度跟负载大小有关
            * 脉冲数            ：10240000，即200圈
            **********************************************************/
            smd_pos_mode(1, 0, 200, 6000, 10240000);
            handle_ack(50);             /* 处理应答 */
        }

        if (xl9555_get_pin(KEY1) == 0)
        {
            smd_read_motor_sta(1);      /* 读取电机运行状态 */
            handle_ack(50);             /* 处理应答 */
            
            smd_read_rotate_speed(1);   /* 读取电机实时转速 */
            handle_ack(50);             /* 处理应答 */
        }

        if (xl9555_get_pin(KEY2) == 0)  /* 请在电机运动完成后，等1秒左右到位，再按 */
        {
            smd_read_pos(1);            /* 读取电机实时位置 */
            handle_ack(50);             /* 处理应答 */
            
            smd_read_pos_error(1);      /* 读取电机位置误差 */
            handle_ack(50);             /* 处理应答 */
        }

        if (xl9555_get_pin(KEY3) == 0)  /* 请在电机运动完成后，再按 */
        {
            /* 如果没有将当前位置角度清零，在绝对位置模式控制下，当前位置角度值到设定的值后，按KEY0重复设置，电机将不会继续运动 */
            smd_angle_to_zero(1);       /* 将当前位置角度清零（请在电机运动完成后，再清零） */
            handle_ack(50);             /* 处理应答 */
        }
    }

    t++;
    if((t % 50) == 0)
    {
        t = 0;
        LED_TOGGLE();  /* LED翻转 */
    }

    delay(10);
}
