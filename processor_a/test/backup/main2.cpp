#include <Arduino.h>
#include "../include/uart.h"
#include "../include/smd.h"
#include "../include/process_frame.h"

uint8_t g_rx_cmd[128];         /* 接收到的指令缓冲区 */

const long TARGET_POS = 60000;  /* 目标位置 */

enum MotionState {
    ZERO_POSITION,
    MOVE_FORWARD,
    MOVE_BACKWARD,
    WAIT_ARRIVAL
};

MotionState motionState = ZERO_POSITION;
MotionState lastMoveState = ZERO_POSITION;
unsigned long stateTimer = 0;
bool firstCycle = true;

uint8_t handle_ack(uint32_t over_time)
{
    uint16_t rec_ct = 0;
    SERIAL_FRAME g_serial_frame;
    unsigned long last_time = millis();

    while(1)
    {
        if(Serial1.available() > 0)
        {
            g_rx_cmd[rec_ct++] = Serial1.read();
            last_time = millis();
        }
        else
        {
            if(rec_ct > 128)
            {
                Serial.printf("接收缓冲区溢出！\n");
                return 1;
            }

            if((millis() - last_time) > over_time)
            {
                if (rec_ct == 0)
                {
                    Serial.printf("接收超时，无数据！！！\n\n");
                    return 2;
                }

                for(uint8_t i = 0; i < rec_ct; i++)
                {
                    Serial.printf("%x ", g_rx_cmd[i]);
                }
                Serial.printf("\n");
                
                serial_frame_process((uint8_t *)g_rx_cmd, rec_ct, &g_serial_frame);

                break;
            }
        }
    }

    return 0;
}

void setup() 
{
    uart_init(0, 115200);
    uart_init(1, 115200);

    Serial.println("\n========================================");
    Serial.println("步进电机自动往返运动系统");
    Serial.println("使用绝对位置模式");
    Serial.println("========================================\n");
    
    Serial.println("系统初始化完成，开始往返运动...");
    delay(1000);
}

void loop()
{
    switch(motionState)
    {
        case ZERO_POSITION:
            Serial.println("【步骤1】将当前位置清零（设为0点）");
            smd_angle_to_zero(1);
            handle_ack(50);
            delay(100);
            
            Serial.println("位置清零完成，准备正向移动...");
            motionState = MOVE_FORWARD;
            break;
            
        case MOVE_FORWARD:
            Serial.printf("【步骤2】正向移动到位置: %ld\n", TARGET_POS);
            /* 绝对位置模式：地址=1, 方向=0(顺时针/正向), 加速度=200, 速度=500RPM, 目标位置=60000 */
            smd_pos_mode(1, 0, 200, 50, TARGET_POS);
            handle_ack(50);
            
            lastMoveState = MOVE_FORWARD;
            stateTimer = millis();
            motionState = WAIT_ARRIVAL;
            break;
            
        case WAIT_ARRIVAL:
            if((millis() - stateTimer) > 100)
            {
                smd_read_arrived_sta(1);
                uint8_t result = handle_ack(50);
                
                if(result == 0)
                {
                    SERIAL_FRAME frame;
                    serial_frame_process(g_rx_cmd, 128, &frame);
                    
                    if(frame.function_code == FCT_READ_ARRIVED_STA && frame.data_len > 0)
                    {
                        if(frame.data[0] == 1)
                        {
                            Serial.println("电机已到位！");
                            smd_read_pos(1);
                            handle_ack(50);
                            
                            if(lastMoveState == MOVE_FORWARD)
                            {
                                Serial.println("到达目标位置，准备反向移动...");
                                delay(100);
                                motionState = MOVE_BACKWARD;
                            }
                            else if(lastMoveState == MOVE_BACKWARD)
                            {
                                Serial.println("返回零点完成，准备再次正向移动...");
                                delay(100);
                                motionState = MOVE_FORWARD;
                            }
                        }
                    }
                }
            }
            break;
            
        case MOVE_BACKWARD:
            Serial.println("【步骤3】反向移动到位置: 0");
            /* 绝对位置模式：地址=1, 方向=1(逆时针/反向), 加速度=200, 速度=500RPM, 目标位置=0 */
            smd_pos_mode(1, 1, 200, 50, 0);
            handle_ack(50);
            
            lastMoveState = MOVE_BACKWARD;
            stateTimer = millis();
            motionState = WAIT_ARRIVAL;
            break;
    }
    
    delay(50);
}