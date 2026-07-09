#ifndef MY_MOTOR_H
#define MY_MOTOR_H

#include <Arduino.h>

#define MCU2_TX         41
#define MCU2_RX         42
#define MCU2_UART_BAUD  115200
#define MCU2_BUF_SIZE   256
#define MOTOR_ADDR      0x01

/* SMD 协议帧定义 */
#define SMD_FRAME_HEAD  0xC5
#define SMD_FRAME_TAIL  0x5C

/* 电机参数结构体（闭环控制只需这些关键参数） */
typedef struct {
    float    phase_current;     /* 相电流 (mA) */
    float    rotate_speed;      /* 实时转速 (RPM) */
    int32_t  position;          /* 实时位置 (51200 = 1圈) */
    uint8_t  enable_status;     /* 使能状态: 0=使能, 1=失能 */
    uint8_t  arrived_status;    /* 到位状态: 0=未到位, 1=到位 */
    uint8_t  data_updated;      /* 数据更新标志 */
    uint8_t  mt_online;         /* 电机在线: 0=离线, 1=在线 */
} MotorParams;

extern MotorParams motorParams;

typedef enum {
    MOTOR_CMD_NONE = 0,
    MOTOR_CMD_START,
    MOTOR_CMD_STOP,
    MOTOR_CMD_RETURN_ZERO,
} motor_cmd_t;

extern volatile motor_cmd_t motor_cmd;

#ifdef __cplusplus
extern "C" {
#endif

void MCU2_UART_Init(void);
void MCU2_UART_Send(uint8_t *data, uint16_t len);
void MCU2_UART_CreateTask(void);
void motor_serial_task_create(void);
void motor_init(void);

void motor_reciprocating_motion(uint8_t dir, uint8_t acc,
                                uint16_t speed, float revolutions);

void  motor_pulse_go(void);
void  motor_pulse_back(void);
void  motor_return_zero(void);
void  motor_start(void);
void  motor_stop(void);
int32_t motor_get_target_pos(void);
bool  motor_is_busy(void);

void  motor_goto_rev(float revs);
void  motor_set_soft_limit_low(float revs);
void  motor_set_soft_limit_high(float revs);
float motor_get_soft_limit_low(void);
float motor_get_soft_limit_high(void);

void  motor_wf_start(uint16_t speed, uint8_t acc, float revs);
void  motor_wf_stop(void);
bool  motor_is_recip_mode(void);
void  motor_wf_loop(void);
bool  motor_cmd_guard(void);

extern volatile bool QzStop_flag;
void  motor_qz_stop(void);
void  motor_qz_unstop(void);
bool  motor_is_qz_stopped(void);

#ifdef __cplusplus
}
#endif

#endif