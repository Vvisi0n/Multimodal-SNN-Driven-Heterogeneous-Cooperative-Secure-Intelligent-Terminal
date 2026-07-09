#include <Arduino.h>
#include "MIC.h"
#include "MPU.h"
#include "my_usart.h"
#include "my_motor.h"
#include "BUZZER.h"
#include "ble.h"

/* ---------- 功能开关： 1 启用，0 禁用 ---------- */
#define __Key__      0
#define __MIC__      1
#define __MPU__      1
#define __EdgeInf__  0
#define __INA__      1
#define __DHT11__    1
#define __Motor__    1
#define __BLE__      1

/* ---------- 模块调试打印开关 ---------- */
#define __KeyDebug__      0
#define __MICDebug__      0
#define __MPUDebug__      0
#define __EdgeInfDebug__  0
#define __INADebug__      0
#define __DHT11Debug__    0
#define __BLEDebug__      1   /* 1=开启BLE回声调试 */

#if __Key__
  #include "KEY.h"
#endif

#if __EdgeInf__
  #include "Edge_Inference.h"
#endif

#if __INA__
  #include "INA.h"
#endif

#if __DHT11__
  #include "DHT11.h"
#endif

/*==================================================================
 *  setup()
 *==================================================================*/
void setup() {
    Serial.begin(115200);
    // 等待串口连接，最多 3 秒
    unsigned long start_ms = millis();
    while (!Serial && (millis() - start_ms < 3000)) {
        delay(10);
    }
    delay(100);

    #if __EdgeInf__
        EdgeInference_Init();
        Serial.println("EdgeInference Task Created on Core 1");
    #endif
    
    #if __Key__
        KEY_Init();
        KEY_CreateTask();
        Serial.println("KEY Task Created on Core 0");
    #endif
    
    #if __MIC__
        MIC_Init();
        MIC_CreateTask();
        Serial.println("MIC Task Created on Core 0");
    #endif
  
    #if __MPU__
        if (MPU_Init()) {
          MPU_CreateTask();
          Serial.println("MPU Task Created on Core 0");
        } else {
          Serial.println("MPU Init Failed!");
        }
    #endif

    MCU1_UART_Init();
    MCU1_UART_CreateTask();
    Serial.println("MCU1_UART Task Created on Core 0");

    BUZZER_Init();
    Serial.println("BUZZER Init Done");

    #if __INA__
        if (INA_Init()) {
          INA_CreateTask();
          Serial.println("INA Task Created on Core 0");
        } else {
          Serial.println("INA Init Failed!");
        }
    #endif

    #if __DHT11__
        DHT11_Init();
        DHT11_CreateTask();
        Serial.println("DHT11 Task Created on Core 0");
    #endif

    #if __BLE__
        BLE_Init();
        BLE_CreateTask();
        #if __BLEDebug__
            BLE_DebugEnable = 1;
        #endif
        Serial.println("BLE Task Created on Core 0");
    #endif

    #if __Motor__
        MCU2_UART_Init();
        MCU2_UART_CreateTask();
        Serial.println("MCU2 Motor Task Created on Core 0");
        delay(1500);

        motor_serial_task_create();
        Serial.println("Motor Serial Task Created on Core 0");

        delay(500);
        motor_init();
        Serial.println("[Motor] Commands: f=+0.1  b=-0.1  s=start  p=stop  z=zero");
        Serial.println("[Motor] wf speed acc revs = reciprocating  wf stop = stop");
    #endif
  
}

/*==================================================================
 *  loop()
 *==================================================================*/
void loop() {
    static unsigned long lastPrintTime = 0;

    #if __Motor__
    {
        /* 往复模式：优先级最高，阻塞执行直到被 wf stop 打断 */
        if (motor_is_recip_mode()) {
            motor_wf_loop();
            /* 往复结束后继续正常循环 */
        }

        motor_cmd_t cmd = motor_cmd;
        if (cmd != MOTOR_CMD_NONE) {
            motor_cmd = MOTOR_CMD_NONE;

            switch (cmd) {
                case MOTOR_CMD_START:       motor_start();       break;
                case MOTOR_CMD_STOP:        motor_stop();        break;
                case MOTOR_CMD_RETURN_ZERO: motor_return_zero(); break;
                default: break;
            }
        }

        if (MCU1_NewDataFlag) {
            MCU1_NewDataFlag = 0;
            const char *rcmd = (const char *)MCU1_RxBuffer;

            /* wf stop: 任意时刻都可停止往复运动 */
            if (strcmp(rcmd, "wf stop") == 0) {
                Serial.println("[RC] wf stop");
                motor_wf_stop();
                MCU1_UART_Print("OK:wf_stop\r\n");
            }
            /* buzzer 命令：不依赖电机状态 */
            else if (strcmp(rcmd, "[buzzer]beep_loop") == 0) {
                Serial.println("[RC] buzzer beep_loop");
                BUZZER_BeepLoop();
                MCU1_UART_Print("OK:buzzer_beep_loop\r\n");
            }
            else if (strcmp(rcmd, "[buzzer]beep_once") == 0) {
                Serial.println("[RC] buzzer beep_once");
                BUZZER_BeepOnce();
                MCU1_UART_Print("OK:buzzer_beep_once\r\n");
            }
            else if (strcmp(rcmd, "[buzzer]stop") == 0) {
                Serial.println("[RC] buzzer stop");
                BUZZER_Stop();
                MCU1_UART_Print("OK:buzzer_stop\r\n");
            }
            /* QzStop / QzUnstop: 急停制动，无视电机状态 */
            else if (strcmp(rcmd, "QzStop") == 0) {
                Serial.println("[RC] QzStop");
                motor_qz_stop();
                MCU1_UART_Print("OK:QzStop\r\n");
            }
            else if (strcmp(rcmd, "QzUnstop") == 0) {
                Serial.println("[RC] QzUnstop");
                motor_qz_unstop();
                MCU1_UART_Print("OK:QzUnstop\r\n");
            }
            /* wf 速度 加速度 圈数: 启动往复运动（已在往复中则拒绝） */
            else if (strncmp(rcmd, "wf ", 3) == 0) {
                if (motor_is_recip_mode()) {
                    Serial.printf("[RC] WF already running, use 'wf stop' first\r\n");
                    MCU1_UART_Print("ERR:WF_Busy\r\n");
                } else {
                    uint16_t speed = 100;
                    int      acc_i = 50;
                    float    revs  = 6.0;
                    sscanf(rcmd + 3, "%hu %d %f", &speed, &acc_i, &revs);
                    uint8_t acc = (uint8_t)acc_i;
                    Serial.printf("[RC] WF start: speed=%d, acc=%d, revs=%.1f\n", speed, acc, (double)revs);
                    motor_wf_start(speed, acc, revs);
                    MCU1_UART_Print("OK:wf_start\r\n");
                }
            }
            /* 往复模式中，其他命令一律拒绝（复用守卫函数） */
            else if (!motor_cmd_guard()) {
                MCU1_UART_Print("ERR:Busy\r\n");
            }
            else if (rcmd[0] == 'f' || rcmd[0] == 'F') {
                if (rcmd[1] == ' ') {
                    float revs = atof(rcmd + 2);
                    Serial.printf("[RC] GOTO %.2f revs\n", revs);
                    motor_goto_rev(revs);
                    MCU1_UART_Print("OK:f\r\n");
                } else {
                    motor_pulse_go();
                    MCU1_UART_Print("OK:Go+\r\n");
                }
            } else if (strcmp(rcmd, "GoStop") == 0) {
                Serial.println("[RC] GoStop");
                motor_stop();
                MCU1_UART_Print("OK:GoStop\r\n");
            } else if (!motor_is_busy()) {
                if (strcmp(rcmd, "Go+") == 0) {
                    Serial.println("[RC] Go+");
                    motor_pulse_go();
                    motor_start();
                    MCU1_UART_Print("OK:Go+\r\n");
                } else if (strcmp(rcmd, "Go-") == 0) {
                    Serial.println("[RC] Go-");
                    motor_pulse_back();
                    motor_start();
                    MCU1_UART_Print("OK:Go-\r\n");
                } else if (strcmp(rcmd, "GoOn") == 0) {
                    Serial.println("[RC] GoOn");
                    motor_start();
                    MCU1_UART_Print("OK:GoOn\r\n");
                } else if (strcmp(rcmd, "Back0") == 0) {
                    Serial.println("[RC] Back0");
                    motor_return_zero();
                    MCU1_UART_Print("OK:Back0\r\n");
                } else if (rcmd[0] == 'b' || rcmd[0] == 'B') {
                    if (rcmd[1] == ' ') {
                        float revs = atof(rcmd + 2);
                        Serial.printf("[RC] GOTO %.2f revs\n", revs);
                        motor_goto_rev(revs);
                        MCU1_UART_Print("OK:b\r\n");
                    } else {
                        motor_pulse_back();
                        MCU1_UART_Print("OK:Go-\r\n");
                    }
                } else {
                    Serial.printf("[RC] Unknown: '%s'\r\n", rcmd);
                    MCU1_UART_Print("ERR:Unknown\r\n");
                }
            } else {
                Serial.printf("[RC] Busy, discard: '%s'\r\n", rcmd);
                MCU1_UART_Print("ERR:Busy\r\n");
            }
        }
    }
    #endif

    #if __EdgeInf__
        EdgeInference_Process();
    #endif

    if (millis() - lastPrintTime > 100) {
        lastPrintTime = millis();
        
        #if __KeyDebug__
            Serial.printf("KEY: K1=%d K2=%d K3=%d\n",
                         KEY1_isPressed, KEY2_isPressed, KEY3_isPressed);
        #endif

        #if __MICDebug__
            Serial.printf("MIC: ADC=%u Amp=%u Value=%u\n",
                         MIC_ADC, MIC_Amp, MIC_Value);
        #endif

        #if __MPUDebug__
            Serial.printf("MPU: Yaw=%.2f Pitch=%.2f Roll=%.2f\n",
                         mpuData.yaw, mpuData.pitch, mpuData.roll);
            Serial.printf("     Accel: X=%.2f Y=%.2f Z=%.2f g\n",
                         mpuData.accelX, mpuData.accelY, mpuData.accelZ);
            Serial.printf("     Gyro:  X=%.2f Y=%.2f Z=%.2f\n",
                         mpuData.gyroX, mpuData.gyroY, mpuData.gyroZ);
        #endif

        #if __INADebug__
            Serial.printf("INA: Bus=%.2fV Load=%.2fV Shunt=%.2fmV Curr=%.2fmA Power=%.2fmW Overflow=%d\n",
                         inaData.busVoltage_V,
                         inaData.loadVoltage_V,
                         inaData.shuntVoltage_mV,
                         inaData.current_mA,
                         inaData.power_mW,
                         inaData.overflow);
        #endif

        #if __DHT11Debug__
            Serial.printf("DHT11: Temp=%.1fC Humi=%.1f%%\n",
                         DHT11_Temp, DHT11_Humi);
        #endif

        #if __EdgeInfDebug__
        {
            EdgeInference_Result result;
            memset(&result, 0, sizeof(result));
            EdgeInference_ConsumeResult(&result);
            if (result.ok) {
                int pred  = result.predicted_label;
                int truth = result.expected_label;
                const char* match = (pred == truth) ? "O" : "X";

                if (pred >= 0 && pred < EDGE_NUM_CLASSES &&
                    truth >= 0 && truth < EDGE_NUM_CLASSES) {
                    Serial.printf("[EdgeInf] Pred=%d(%s)  True=%d(%s)  %s  | %lums\n",
                                pred, g_edge_class_names[pred],
                                truth, g_edge_class_names[truth],
                                match,
                                result.inference_time_ms);
                }
            }
        }
        #endif
    }
    delay(10);
}