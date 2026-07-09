#include "my_motor.h"
#include "smd.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <HardwareSerial.h>

static HardwareSerial MCU2_Serial(2);
static TaskHandle_t MCU2_TaskHandle = NULL;
static volatile bool motor_cmd_busy = false;

MotorParams motorParams;

void MCU2_UART_Init(void) {
    MCU2_Serial.begin(MCU2_UART_BAUD, SERIAL_8N1, MCU2_RX, MCU2_TX);
    memset(&motorParams, 0, sizeof(motorParams));
}

void MCU2_UART_Send(uint8_t *data, uint16_t len) {
    MCU2_Serial.write(data, len);
}

static int motor_wait_response(uint8_t *buf, uint16_t buf_size,
                                uint32_t timeout_ms, uint8_t expected_func) {
    uint16_t idx = 0;
    uint32_t start = millis();
    uint8_t state = 0;

    while (millis() - start < timeout_ms) {
        while (MCU2_Serial.available() > 0 && idx < buf_size) {
            uint8_t b = MCU2_Serial.read();
            buf[idx++] = b;

            if (state == 0 && b == SMD_FRAME_HEAD) {
                state = 1;
                idx = 1;
                buf[0] = b;
            } else if (state == 1 && b == SMD_FRAME_TAIL) {
                if (idx >= 6 && buf[2] == expected_func) {
                    return idx;
                }
                state = 0;
                idx = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return -1;
}

static void MCU2_UART_Task(void *parameter) {
    uint8_t rxBuf[MCU2_BUF_SIZE];
    int cmdIndex = 0;
    unsigned long lastPollTime = 0;

    static const uint8_t readCmds[] = {
        FCT_READ_PHASE_MA,       // 0x23 相电流
        FCT_READ_ROTATE_SPEED,   // 0x29 转速 RPM
        FCT_READ_POS,            // 0x2A 实时位置
        FCT_READ_ENABLE_STA,     // 0x2F 使能状态
        FCT_READ_ARRIVED_STA,    // 0x30 到位状态
    };
    static const int cmdCount = sizeof(readCmds) / sizeof(readCmds[0]);

    while (1) {
        if (millis() - lastPollTime >= 200) {
            lastPollTime = millis();

            uint8_t func = readCmds[cmdIndex];

            switch (func) {
                case FCT_READ_PHASE_MA:     smd_read_phase_ma(MOTOR_ADDR);     break;
                case FCT_READ_ROTATE_SPEED: smd_read_rotate_speed(MOTOR_ADDR); break;
                case FCT_READ_POS:          smd_read_pos(MOTOR_ADDR);          break;
                case FCT_READ_ENABLE_STA:   smd_read_enable_sta(MOTOR_ADDR);   break;
                case FCT_READ_ARRIVED_STA:  smd_read_arrived_sta(MOTOR_ADDR);  break;
            }

            int len = motor_wait_response(rxBuf, MCU2_BUF_SIZE, 100, func);
            cmdIndex = (cmdIndex + 1) % cmdCount;

            if (len < 6) {
                motorParams.mt_online = 0;
            } else {
                motorParams.mt_online = 1;
                motorParams.data_updated = 1;

                switch (func) {
                    case FCT_READ_PHASE_MA: {
                        int16_t raw = (int16_t)((rxBuf[4] << 8) | rxBuf[5]);
                        motorParams.phase_current = (float)raw;
                        Serial.printf("[Motor] Phase Current: %.0f mA\n", motorParams.phase_current);
                        break;
                    }
                    case FCT_READ_ROTATE_SPEED: {
                        int16_t raw = (int16_t)((rxBuf[4] << 8) | rxBuf[5]);
                        motorParams.rotate_speed = (float)raw;
                        Serial.printf("[Motor] Rotate Speed: %d RPM\n", raw);
                        break;
                    }
                    case FCT_READ_POS: {
                        motorParams.position = (int32_t)((rxBuf[4] << 24) | (rxBuf[5] << 16) |
                                                         (rxBuf[6] << 8)  |  rxBuf[7]);
                        Serial.printf("[Motor] Position: %ld (%.1f revs)\n",
                                      (long)motorParams.position,
                                      (double)motorParams.position / 51200.0);
                        break;
                    }
                    case FCT_READ_ENABLE_STA: {
                        motorParams.enable_status = rxBuf[4];
                        Serial.printf("[Motor] Enable Status: %s\n",
                                      motorParams.enable_status == 0 ? "Enabled" : "Disabled");
                        break;
                    }
                    case FCT_READ_ARRIVED_STA: {
                        motorParams.arrived_status = rxBuf[4];
                        Serial.printf("[Motor] Arrived Status: %s\n",
                                      motorParams.arrived_status ? "Arrived" : "Moving");
                        break;
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void MCU2_UART_CreateTask(void) {
    xTaskCreatePinnedToCore(
        MCU2_UART_Task,
        "MCU2_Motor_Task",
        4096,
        NULL,
        2,
        &MCU2_TaskHandle,
        0
    );
}

/* ================================================================
 * 往复运动功能：将当前位置设为0点 → 正向慢速前进N圈 → 回归0点
 * ================================================================
 * 参数:
 *   dir         - 方向: 0=正转(CW), 1=反转(CCW)
 *   acc         - 加速度: 0~200 (RPM/SS), 0=直接启动
 *   speed       - 最大速度: 0~3000 RPM
 *   revolutions - 前进圈数
 *
 * 注意:
 *   - 1圈 = 51200 脉冲 (细分设置决定)
 *   - 此函数是阻塞式的，会等待每次运动到位后才继续
 *   - 每步都有超时保护，防止死等
 *   - 执行期间会暂停后台轮询任务，避免串口冲突
 * ================================================================ */

#define PULSES_PER_REV      51200U
#define PULSES_01_REV       5120U
#define ARRIVE_TIMEOUT_MS   30000U

static uint8_t  motor_default_dir = 0;
static uint8_t  motor_default_acc = 50;
static uint16_t motor_default_speed = 60;
static int32_t  motor_target_pos = 0;
static bool     motor_moving = false;
static volatile bool motor_ready = false;       /* 初始化完成标志，跨核访问 */

static float    motor_soft_limit_low  = -0.1f;
static float    motor_soft_limit_high = 10.0f;
static float    motor_extreme_limit   = 20.0f;

static int motor_poll_arrived(uint8_t addr, uint32_t timeout_ms) {
    uint8_t rxBuf[32];
    uint32_t t0 = millis();
    uint8_t last_result = 0;
    uint32_t last_print = 0;

    while (MCU2_Serial.available() > 0) {
        MCU2_Serial.read();
    }

    while (millis() - t0 < timeout_ms) {
        smd_read_arrived_sta(addr);

        int len = motor_wait_response(rxBuf, sizeof(rxBuf), 200, FCT_READ_ARRIVED_STA);
        if (len >= 6) {
            last_result = rxBuf[4];
            if (last_result == 1) {
                return 1;
            }
        }

        if (millis() - last_print > 2000) {
            last_print = millis();
            Serial.printf("[Motor] Waiting... arrived=%d, elapsed=%lu ms\n",
                          last_result, (unsigned long)(millis() - t0));
        }

        vTaskDelay(pdMS_TO_TICKS(300));
    }

    Serial.printf("[Motor] Arrive timeout! Last status: %d\n", last_result);
    return 0;
}

static void motor_goto_target(bool wait_arrived) {
    if (MCU2_TaskHandle != NULL && motor_cmd_busy == false) {
        motor_cmd_busy = true;
        vTaskSuspend(MCU2_TaskHandle);
    }
    delay(50);
    while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }

    int32_t target = motor_target_pos;
    if (target < 0) target = 0;
    int32_t current = motorParams.position;

    if (target == current) {
        Serial.printf("[Motor] GOTO: already at target %ld, skip\n", (long)target);
        motor_moving = false;
        if (MCU2_TaskHandle != NULL && motor_cmd_busy) {
            vTaskResume(MCU2_TaskHandle);
            motor_cmd_busy = false;
        }
        return;
    }

    smd_stop_now(MOTOR_ADDR);
    delay(50);
    smd_clear_sta(MOTOR_ADDR);
    delay(50);
    smd_motor_enable(MOTOR_ADDR, 0);
    delay(100);

    if (target > current) {
        Serial.printf("[Motor] GOTO: cur=%ld -> tgt=%ld, dir=CW, acc=%d, speed=%d RPM\n",
                      (long)current, (long)target,
                      motor_default_acc, motor_default_speed);
        smd_pos_mode(MOTOR_ADDR, 0, motor_default_acc, motor_default_speed, (uint32_t)target);
    } else {
        uint32_t delta = (uint32_t)(current - target);
        Serial.printf("[Motor] GOTO: cur=%ld -> tgt=%ld, dir=CCW, acc=%d, speed=%d RPM, delta=%lu\n",
                      (long)current, (long)target,
                      motor_default_acc, motor_default_speed, (unsigned long)delta);
        smd_pos_rel_mode(MOTOR_ADDR, 1, motor_default_acc, motor_default_speed, delta);
    }
    motor_moving = true;

    if (wait_arrived) {
        delay(100);
        if (motor_poll_arrived(MOTOR_ADDR, ARRIVE_TIMEOUT_MS)) {
            motor_moving = false;
            motorParams.position = target;
            Serial.printf("[Motor] Reached target: %ld pulses (%.1f revs)\n",
                          (long)target, (double)(target) / PULSES_PER_REV);
        } else {
            motor_moving = false;
            smd_stop_now(MOTOR_ADDR);
            delay(50);
            smd_clear_sta(MOTOR_ADDR);
            delay(50);
            Serial.printf("[Motor] WARNING: Target %ld timed out\n", (long)target);
        }
        while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }
    }

    if (MCU2_TaskHandle != NULL && motor_cmd_busy) {
        vTaskResume(MCU2_TaskHandle);
        motor_cmd_busy = false;
    }
}

// 往返运动
void motor_reciprocating_motion(uint8_t dir, uint8_t acc,
                                 uint16_t speed, float revolutions) {
    uint32_t target_pulses = (uint32_t)(revolutions * PULSES_PER_REV);

    Serial.println("\n========== [Motor] Reciprocating Motion Start ==========");
    Serial.printf("  Dir: %s,  Acc: %d,  Speed: %d RPM,  Forward: %.1f revs (%lu pulses)\n",
                  dir ? "CCW" : "CW", acc, speed,
                  (double)revolutions, (unsigned long)target_pulses);

    if (MCU2_TaskHandle != NULL) {
        vTaskSuspend(MCU2_TaskHandle);
        Serial.println("[Motor] Background task suspended");
    }
    delay(50);
    while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }

    Serial.println("[Motor] Step1: Set current position as zero...");
    smd_stop_now(MOTOR_ADDR);
    delay(50);
    smd_clear_sta(MOTOR_ADDR);
    delay(50);
    smd_motor_enable(MOTOR_ADDR, 0);
    delay(100);
    smd_angle_to_zero(MOTOR_ADDR);
    delay(500);
    motor_target_pos = 0;
    motorParams.position = 0;
    Serial.println("[Motor] Current position set to zero (no physical move)");

    Serial.printf("[Motor] Step2: Move forward %.1f revs...\n", (double)revolutions);
    motor_target_pos = target_pulses;
    smd_pos_rel_mode(MOTOR_ADDR, dir, acc, speed, target_pulses);
    motor_moving = true;
    delay(100);

    if (motor_poll_arrived(MOTOR_ADDR, ARRIVE_TIMEOUT_MS)) {
        motor_moving = false;
        motorParams.position = target_pulses;
        Serial.println("[Motor] Target position reached!");
    } else {
        motor_moving = false;
        Serial.println("[Motor] WARNING: Forward arrive timeout, continue returning to zero...");
    }

    Serial.println("[Motor] Step3: Return to zero...");
    delay(300);
    motor_target_pos = 0;
    smd_pos_rel_mode(MOTOR_ADDR, dir ^ 1, acc, speed, target_pulses);
    motor_moving = true;
    delay(100);

    if (motor_poll_arrived(MOTOR_ADDR, ARRIVE_TIMEOUT_MS)) {
        motor_moving = false;
        motorParams.position = 0;
        Serial.println("[Motor] Returned to zero!");
    } else {
        motor_moving = false;
        Serial.println("[Motor] WARNING: Return to zero timeout!");
    }

    if (MCU2_TaskHandle != NULL) {
        vTaskResume(MCU2_TaskHandle);
        Serial.println("[Motor] Background task resumed");
    }

    Serial.println("========== [Motor] Reciprocating Motion End ==========\n");
}

volatile bool motor_recip_mode = false;
volatile bool wf_stop_pending  = false;   /* true=等回零后再停 */
volatile bool QzStop_flag      = false;   /* true=急停制动，所有运动禁止 */
static uint16_t wf_speed = 100;
static uint8_t  wf_acc   = 50;
static float    wf_revs  = 6.0;

bool motor_is_recip_mode(void) {
    return motor_recip_mode;
}

void motor_wf_start(uint16_t speed, uint8_t acc, float revs) {
    wf_speed = speed;
    wf_acc   = acc;
    wf_revs  = revs;
    motor_recip_mode = true;
    wf_stop_pending  = false;
    Serial.printf("[WF] Start: speed=%d, acc=%d, revs=%.1f\n", speed, acc, (double)revs);
}

void motor_wf_stop(void) {
    wf_stop_pending = true;
    Serial.println("[WF] Will stop after returning to zero");
}

bool motor_cmd_guard(void) {
    if (QzStop_flag) {
        Serial.println("[CMD] Blocked: QzStop active, use 'QzUnstop' first");
        return false;
    }
    if (motor_recip_mode) {
        Serial.println("[CMD] Busy: reciprocating, use 'wf stop' first");
        return false;
    }
    return true;
}

bool motor_is_qz_stopped(void) {
    return QzStop_flag;
}

static int motor_wf_poll_arrived(uint8_t addr, uint32_t timeout_ms) {
    uint8_t rxBuf[32];
    uint32_t t0 = millis();
    uint32_t last_print = 0;

    while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }

    while (millis() - t0 < timeout_ms) {
        smd_read_arrived_sta(addr);
        int len = motor_wait_response(rxBuf, sizeof(rxBuf), 200, FCT_READ_ARRIVED_STA);
        if (len >= 6 && rxBuf[4] == 1) {
            return 1;
        }

        if (millis() - last_print > 2000) {
            last_print = millis();
            Serial.printf("[WF] Waiting... elapsed=%lu ms\n", (unsigned long)(millis() - t0));
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    Serial.printf("[WF] Arrive timeout! elapsed=%lu ms\n", (unsigned long)(millis() - t0));
    return 0;
}

void motor_wf_loop(void) {
    uint32_t max_pulses = (uint32_t)(wf_revs * PULSES_PER_REV);

    if (MCU2_TaskHandle != NULL) {
        vTaskSuspend(MCU2_TaskHandle);
        Serial.println("[WF] Background task suspended");
    }
    delay(50);
    while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }

    /* Step 1: 如果当前位置不在 0，先回到 0 */
    if (motorParams.position != 0) {
        Serial.printf("[WF] Current pos=%ld, returning to zero first...\n", (long)motorParams.position);
        smd_stop_now(MOTOR_ADDR); delay(50);
        smd_clear_sta(MOTOR_ADDR); delay(50);
        smd_motor_enable(MOTOR_ADDR, 0); delay(100);

        uint32_t delta = (uint32_t)motorParams.position;
        motor_target_pos = 0;
        smd_pos_rel_mode(MOTOR_ADDR, 1, wf_acc, wf_speed, delta);
        if (motor_wf_poll_arrived(MOTOR_ADDR, ARRIVE_TIMEOUT_MS)) {
            motorParams.position = 0;
            Serial.println("[WF] Returned to zero");
        } else {
            Serial.println("[WF] WARNING: Return to zero timeout");
        }
    }

    /* Step 2: 往复循环，直到 wf_stop_pending */
    Serial.println("[WF] Reciprocating loop started (0 -> target -> 0)");
    while (!wf_stop_pending) {
        /* 去目标位置 */
        Serial.printf("[WF] -> %.1f revs (%lu pulses)\n", (double)wf_revs, (unsigned long)max_pulses);
        motor_target_pos = max_pulses;
        smd_pos_mode(MOTOR_ADDR, 0, wf_acc, wf_speed, max_pulses);
        delay(100);
        if (!motor_wf_poll_arrived(MOTOR_ADDR, ARRIVE_TIMEOUT_MS)) break;
        motorParams.position = max_pulses;
        Serial.printf("[WF] Arrived at %.1f revs\n", (double)wf_revs);

        if (wf_stop_pending) break;  /* 到了目标但需回零，跳出由 Step 3 处理 */

        /* 回到零 */
        Serial.println("[WF] -> 0");
        motor_target_pos = 0;
        smd_pos_rel_mode(MOTOR_ADDR, 1, wf_acc, wf_speed, max_pulses);
        delay(100);
        if (!motor_wf_poll_arrived(MOTOR_ADDR, ARRIVE_TIMEOUT_MS)) break;
        motorParams.position = 0;
        Serial.println("[WF] Arrived at 0");
    }

    /* Step 3: 如果标记了待停止且不在零位，回零再停 */
    if (wf_stop_pending && motorParams.position != 0) {
        Serial.printf("[WF] Stop pending, returning to zero from %ld...\n", (long)motorParams.position);
        uint32_t delta = (uint32_t)motorParams.position;
        motor_target_pos = 0;
        smd_pos_rel_mode(MOTOR_ADDR, 1, wf_acc, wf_speed, delta);
        delay(100);
        if (motor_wf_poll_arrived(MOTOR_ADDR, ARRIVE_TIMEOUT_MS)) {
            motorParams.position = 0;
            Serial.println("[WF] Returned to zero, now stopping");
        }
    }

wf_cleanup:
    smd_stop_now(MOTOR_ADDR); delay(50);
    smd_clear_sta(MOTOR_ADDR); delay(50);
    motor_moving = false;

    if (MCU2_TaskHandle != NULL) {
        vTaskResume(MCU2_TaskHandle);
        Serial.println("[WF] Background task resumed");
    }
    motor_recip_mode = false;
    wf_stop_pending  = false;
    Serial.println("[WF] Reciprocating motion ended");
}

void motor_pulse_go(void) {
    motor_target_pos += PULSES_01_REV;
    Serial.printf("[Motor] Pulse GO   -> Target: %ld (%.1f revs)\n",
                  (long)motor_target_pos, (double)motor_target_pos / PULSES_PER_REV);
}

void motor_pulse_back(void) {
    motor_target_pos -= PULSES_01_REV;
    Serial.printf("[Motor] Pulse BACK -> Target: %ld (%.1f revs)\n",
                  (long)motor_target_pos, (double)motor_target_pos / PULSES_PER_REV);
}

void motor_return_zero(void) {
    motor_target_pos = 0;
    Serial.println("[Motor] Return to zero requested");
    motor_goto_target(true);
}

void motor_start(void) {
    Serial.printf("[Motor] Start -> Go to target: %ld (%.1f revs), current pos: %ld\n",
                  (long)motor_target_pos, (double)motor_target_pos / PULSES_PER_REV,
                  (long)motorParams.position);
    motor_goto_target(true);
}

void motor_stop(void) {
    Serial.println("[Motor] Stop requested");
    if (MCU2_TaskHandle != NULL && motor_cmd_busy == false) {
        vTaskSuspend(MCU2_TaskHandle);
        motor_cmd_busy = true;
    }
    while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }

    smd_stop_now(MOTOR_ADDR);
    motor_moving = false;
    delay(50);
    smd_clear_sta(MOTOR_ADDR);
    delay(100);

    if (MCU2_TaskHandle != NULL && motor_cmd_busy) {
        vTaskResume(MCU2_TaskHandle);
        motor_cmd_busy = false;
    }

    Serial.println("[Motor] Stopped, holding position");
}

void motor_qz_stop(void) {
    Serial.println("[QzStop] Emergency stop + motor disable");

    motor_recip_mode = false;
    wf_stop_pending  = false;

    if (MCU2_TaskHandle != NULL && motor_cmd_busy == false) {
        vTaskSuspend(MCU2_TaskHandle);
        motor_cmd_busy = true;
    }
    while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }

    smd_stop_now(MOTOR_ADDR);
    motor_moving = false;
    delay(50);
    smd_clear_sta(MOTOR_ADDR);
    delay(50);
    smd_motor_enable(MOTOR_ADDR, 1);
    delay(50);

    QzStop_flag = true;

    if (MCU2_TaskHandle != NULL && motor_cmd_busy) {
        vTaskResume(MCU2_TaskHandle);
        motor_cmd_busy = false;
    }

    Serial.println("[QzStop] Motor disabled, QzStop_flag = 1");
}

void motor_qz_unstop(void) {
    Serial.println("[QzUnstop] Re-enable motor + return to zero");

    if (MCU2_TaskHandle != NULL && motor_cmd_busy == false) {
        vTaskSuspend(MCU2_TaskHandle);
        motor_cmd_busy = true;
    }
    while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }

    smd_motor_enable(MOTOR_ADDR, 0);
    delay(100);
    smd_angle_to_zero(MOTOR_ADDR);
    delay(500);
    motor_target_pos = 0;
    motorParams.position = 0;

    QzStop_flag = false;
    motor_moving = false;

    if (MCU2_TaskHandle != NULL && motor_cmd_busy) {
        vTaskResume(MCU2_TaskHandle);
        motor_cmd_busy = false;
    }

    Serial.println("[QzUnstop] Motor enabled, zero set, QzStop_flag = 0");
}

int32_t motor_get_target_pos(void) {
    return motor_target_pos;
}

bool motor_is_busy(void) {
    return motor_moving || motor_cmd_busy;
}

/* 走到指定圈数位置，带软限位和极端值保护 */
void motor_goto_rev(float revs) {
    /* 第一步：极端值保护，20圈以上直接拒绝 */
    if (fabsf(revs) >= motor_extreme_limit) {
        Serial.printf("[Motor] GOTO Rev: EXTREME value %.2f, |revs| >= %.1f, REJECTED\n",
                      revs, motor_extreme_limit);
        return;
    }

    /* 第二步：软限位钳位 */
    if (revs < motor_soft_limit_low) {
        Serial.printf("[Motor] GOTO Rev: %.2f below soft limit %.2f, clamped to %.2f\n",
                      revs, motor_soft_limit_low, motor_soft_limit_low);
        revs = motor_soft_limit_low;
    }
    if (revs > motor_soft_limit_high) {
        Serial.printf("[Motor] GOTO Rev: %.2f above soft limit %.2f, clamped to %.2f\n",
                      revs, motor_soft_limit_high, motor_soft_limit_high);
        revs = motor_soft_limit_high;
    }

    motor_target_pos = (int32_t)(revs * PULSES_PER_REV);
    Serial.printf("[Motor] GOTO Rev: target=%.2f revs -> %ld pulses\n",
                  revs, (long)motor_target_pos);
    motor_goto_target(true);
}

void motor_set_soft_limit_low(float revs) {
    motor_soft_limit_low = revs;
    Serial.printf("[Motor] Soft limit low  set to: %.2f revs\n", revs);
}

void motor_set_soft_limit_high(float revs) {
    motor_soft_limit_high = revs;
    Serial.printf("[Motor] Soft limit high set to: %.2f revs\n", revs);
}

float motor_get_soft_limit_low(void) {
    return motor_soft_limit_low;
}

float motor_get_soft_limit_high(void) {
    return motor_soft_limit_high;
}

void motor_init(void) {
    Serial.println("[Motor] Init: stop + enable + set zero...");
    if (MCU2_TaskHandle != NULL) {
        vTaskSuspend(MCU2_TaskHandle);
    }
    delay(50);
    while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }

    smd_stop_now(MOTOR_ADDR);
    delay(100);
    smd_motor_enable(MOTOR_ADDR, 0);
    delay(100);
    smd_angle_to_zero(MOTOR_ADDR);
    delay(500);
    motor_target_pos = 0;

    while (MCU2_Serial.available() > 0) { MCU2_Serial.read(); }

    if (MCU2_TaskHandle != NULL) {
        vTaskResume(MCU2_TaskHandle);
    }
    motor_ready = true;
    Serial.println("[Motor] Init done. Zero point set. Ready.");
}

volatile motor_cmd_t motor_cmd = MOTOR_CMD_NONE;

static void motor_serial_task(void *parameter) {
    char buf[32];
    uint8_t idx = 0;

    while (1) {
        while (Serial.available() > 0 && idx < sizeof(buf) - 1) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (idx > 0) {
                    buf[idx] = '\0';
                    Serial.printf("[CMD] Got: '%s'\n", buf);

                    if (strncmp(buf, "set_low ", 8) == 0) {
                        motor_set_soft_limit_low(atof(buf + 8));
                    }
                    else if (strncmp(buf, "set_high ", 9) == 0) {
                        motor_set_soft_limit_high(atof(buf + 9));
                    }
                    else if (!motor_ready) {
                        Serial.println("[CMD] Motor not ready yet, wait for init...");
                    }
                    else if (strcmp(buf, "wf stop") == 0) {
                        Serial.println("[CMD] wf stop");
                        motor_wf_stop();
                    }
                    else if (strcmp(buf, "QzStop") == 0) {
                        Serial.println("[CMD] QzStop");
                        motor_qz_stop();
                    }
                    else if (strcmp(buf, "QzUnstop") == 0) {
                        Serial.println("[CMD] QzUnstop");
                        motor_qz_unstop();
                    }
                    else if (strncmp(buf, "wf ", 3) == 0) {
                        if (motor_is_recip_mode()) {
                            Serial.println("[CMD] WF already running, use 'wf stop' first");
                        } else {
                            uint16_t speed = 100;
                            int      acc_i = 50;
                            float    revs  = 6.0;
                            sscanf(buf + 3, "%hu %d %f", &speed, &acc_i, &revs);
                            uint8_t acc = (uint8_t)acc_i;
                            Serial.printf("[CMD] WF start: speed=%d, acc=%d, revs=%.1f\n", speed, acc, (double)revs);
                            motor_wf_start(speed, acc, revs);
                        }
                    }
                    else if (!motor_cmd_guard()) {
                        /* 往复模式中，拒绝 */
                    }
                    else if (buf[0] == 'f' || buf[0] == 'F') {
                        if (buf[1] == ' ') {
                            motor_goto_rev(atof(buf + 2));
                        } else {
                            motor_pulse_go();
                        }
                    }
                    else if (buf[0] == 'b' || buf[0] == 'B') {
                        if (buf[1] == ' ') {
                            motor_goto_rev(atof(buf + 2));
                        } else {
                            motor_pulse_back();
                        }
                    }
                    else if (idx == 1) {
                        switch (buf[0]) {
                            case 's': case 'S':
                                motor_cmd = MOTOR_CMD_START;
                                Serial.println("[CMD] START");
                                break;
                            case 'p': case 'P':
                                motor_cmd = MOTOR_CMD_STOP;
                                Serial.println("[CMD] STOP");
                                break;
                            case 'z': case 'Z':
                                motor_cmd = MOTOR_CMD_RETURN_ZERO;
                                Serial.println("[CMD] RETURN ZERO");
                                break;
                            default:
                                Serial.printf("[CMD] Unknown: '%s'\n", buf);
                                Serial.println("  f 0.3 = goto 0.3rev  f = +0.1rev  b = -0.1rev");
                                Serial.println("  s=start  p=stop  z=zero");
                                Serial.println("  set_low X  set_high X");
                                Serial.println("  wf speed acc revs = reciprocating  wf stop = stop");
                                break;
                        }
                    }
                    else {
                        Serial.printf("[CMD] Unknown: '%s'\n", buf);
                        Serial.println("  f 0.3 = goto 0.3rev  f = +0.1rev  b = -0.1rev");
                        Serial.println("  s=start  p=stop  z=zero");
                        Serial.println("  set_low X  set_high X");
                        Serial.println("  wf speed acc revs = reciprocating  wf stop = stop");
                    }
                    idx = 0;
                }
            } else if (c >= ' ' && c <= '~') {
                buf[idx++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void motor_serial_task_create(void) {
    xTaskCreatePinnedToCore(
        motor_serial_task,
        "Motor_Serial_Task",
        8192,   /* 栈加大到 8KB，因为 goto_rev -> goto_target -> poll_arrived 调用链很深 */
        NULL,
        1,
        NULL,
        0
    );
}