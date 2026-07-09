#include <Arduino.h>

#define TX_PIN 42
#define RX_PIN 41
#define SLAVE_ADDR 1
#define BAUD_RATE 115200

const unsigned long HARD_DELAY = 10; // 保障 2ms~10ms 帧间隔安全时序

struct MotorData {
  float flux; float resistance; float inductance;        
  int16_t phase_current; float bus_voltage; int16_t speed;           
  float position; float pos_error;         
  uint16_t run_status; uint16_t enable_status; uint16_t target_reached; 
};
MotorData myMotor;

struct ModbusReadTask {
  uint16_t startAddr; uint16_t regQty; int expectedLen;
};

#define TOTAL_READ_TASKS 10
ModbusReadTask readTasks[TOTAL_READ_TASKS] = {
  {0x0021, 2, 9}, {0x0022, 4, 13}, {0x0023, 1, 7}, {0x0024, 2, 9},
  {0x0029, 1, 7}, {0x002A, 2, 9},  {0x002B, 2, 9}, {0x002C, 1, 7},
  {0x002F, 1, 7}, {0x0030, 1, 7}
};

// =================== 【⚙️ 速度运动控制参数】 ===================
const float TARGET_SPEED_FLOAT = 50.0f; // 温和的目标转速 300.0 RPM 
bool isMotorConfigured = false; 

// 严格遵循正点原子的标准 CRC16 校验
uint16_t calculateCRC(const uint8_t *buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) { crc >>= 1; crc ^= 0xA001; }
      else { crc >>= 1; }
    }
  }
  return crc;
}

// 解析 04H 读回来的大端浮点数
float getFloatFromBuf(uint8_t *buf, int offset) {
  float val;
  uint8_t float_bytes[4] = { buf[offset + 3], buf[offset + 2], buf[offset + 1], buf[offset + 0] }; 
  memcpy(&val, float_bytes, 4);
  return (isnan(val) || isinf(val)) ? 0.00f : val;
}

// =================== 【Modbus 0x06 功能码 单寄存器写入】 ===================
bool sendModbusWrite06(uint16_t regAddr, uint16_t regValue) {
  uint8_t cmd[8] = {
    SLAVE_ADDR, 0x06,
    (uint8_t)(regAddr >> 8), (uint8_t)(regAddr & 0xFF),
    (uint8_t)(regValue >> 8), (uint8_t)(regValue & 0xFF), 0x00, 0x00
  };
  uint16_t crc = calculateCRC(cmd, 6);
  cmd[6] = crc & 0xFF; cmd[7] = (crc >> 8) & 0xFF;

  while(Serial2.available()) Serial2.read();
  Serial2.write(cmd, 8);
  Serial2.flush();

  unsigned long startT = millis();
  while (Serial2.available() < 8) { if (millis() - startT > 40) return false; }
  for (int i = 0; i < 8; i++) Serial2.read(); 
  delay(HARD_DELAY); // 满足两条完整指令之间的间隔时序
  return true;
}

// =================== 【Modbus 10H 功能码：多寄存器精准写入速度】 ===================
// 手册第 93 页：起始 0x00F1，连续写 3 个寄存器 (共 6 字节数据)
bool sendModbusSpeed10H(uint8_t dir, uint8_t accel, float speedRPM) {
  uint8_t cmd[15] = {
    SLAVE_ADDR, 0x10,           // 10H 功能码
    0x00, 0xF1,                 // 起始寄存器地址：0x00F1
    0x00, 0x03,                 // 寄存器数量：3 个
    0x06,                       // 数据长度：6 字节
    
    // --- 寄存器 1 (0x00F1) ---
    dir,                        // Byte1: 方向 (0：正转；1：反转)
    accel,                      // Byte2: 加减速度 (取值 100，数值越大响应越快)
    
    // --- 寄存器 2 & 3 (0x00F2 & 0x00F3) ---
    0x00, 0x00, 0x00, 0x00,     // Byte3~6: 浮点速度预留 (IEEE-754 标准格式)
    0x00, 0x00                  // CRC 校验占位
  };

  // 精准转换 float 到大端 Modbus 字节流
  uint8_t temp[4];
  memcpy(temp, &speedRPM, 4);
  cmd[9]  = temp[3];            // 速度高字节
  cmd[10] = temp[2];
  cmd[11] = temp[1];
  cmd[12] = temp[0];            // 速度低字节

  uint16_t crc = calculateCRC(cmd, 13);
  cmd[13] = crc & 0xFF; cmd[14] = (crc >> 8) & 0xFF;

  while(Serial2.available()) Serial2.read();
  Serial2.write(cmd, 15);
  Serial2.flush();

  unsigned long startT = millis();
  while (Serial2.available() < 8) { if (millis() - startT > 50) return false; }
  for (int i = 0; i < 8; i++) Serial2.read(); 
  
  delay(HARD_DELAY);
  return true;
}

// =================== 【Modbus 0x04 功能码 轮询刷新看板】 ===================
bool sendModbusRead(ModbusReadTask task) {
  uint8_t cmd[8] = {
    SLAVE_ADDR, 0x04, 
    (uint8_t)(task.startAddr >> 8), (uint8_t)(task.startAddr & 0xFF),
    (uint8_t)(task.regQty >> 8), (uint8_t)(task.regQty & 0xFF), 0x00, 0x00
  };
  uint16_t crc = calculateCRC(cmd, 6);
  cmd[6] = crc & 0xFF; cmd[7] = (crc >> 8) & 0xFF;

  while(Serial2.available()) Serial2.read(); 
  Serial2.write(cmd, 8); 
  Serial2.flush();

  unsigned long startT = millis();
  while (Serial2.available() < task.expectedLen) { if (millis() - startT > 30) return false; }

  uint8_t rxBuf[16];
  for (int i = 0; i < task.expectedLen; i++) rxBuf[i] = Serial2.read();

  if (rxBuf[0] == SLAVE_ADDR && rxBuf[1] == 0x04) {
    switch (task.startAddr) {
      case 0x0021: myMotor.flux = getFloatFromBuf(rxBuf, 3); break;
      case 0x0022: myMotor.resistance = getFloatFromBuf(rxBuf, 3); myMotor.inductance = getFloatFromBuf(rxBuf, 7); break;
      case 0x0023: myMotor.phase_current = (int16_t)((rxBuf[3] << 8) | rxBuf[4]); break;
      case 0x0024: myMotor.bus_voltage = getFloatFromBuf(rxBuf, 3); break;
      case 0x0029: myMotor.speed = (int16_t)((rxBuf[3] << 8) | rxBuf[4]); break;
      case 0x002A: myMotor.position = getFloatFromBuf(rxBuf, 3); break;
      case 0x002B: myMotor.pos_error = getFloatFromBuf(rxBuf, 3); break;
      case 0x002C: myMotor.run_status = (uint16_t)((rxBuf[3] << 8) | rxBuf[4]); break;
      case 0x002F: myMotor.enable_status = (uint16_t)((rxBuf[3] << 8) | rxBuf[4]); break;
      case 0x0030: myMotor.target_reached = (uint16_t)((rxBuf[3] << 8) | rxBuf[4]); break; 
    }
    delay(HARD_DELAY); 
    return true; 
  }
  return false; 
}

void setup() {
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.begin(BAUD_RATE);
  delay(1500);
  Serial.println("\n=== [SYSTEM] 开始执行手册级硬件初始化... ===");

  int retry = 0;
  while (!isMotorConfigured && retry < 10) {
    retry++;
    Serial.printf("[SYSTEM] [第%d次尝试] 正在切换工作模式为通信速度模式 (0x0062 -> 1)...\n", retry);
    
    // 步骤一：写 0x0062 寄存器，切换成 0x01（通信速度模式）
    if (sendModbusWrite06(0x0062, 1)) {
      Serial.println("[SYSTEM] ✅ 成功切入通信速度模式！");
      delay(50);
      
      // 步骤二：写 0x00FA 寄存器，按照手册第 99 页规定，写入 0 才是真正使能（锁合电机）
      Serial.println("[SYSTEM] 正在下达电机正向使能锁死命令 (0x00FA -> 0)...");
      if (sendModbusWrite06(0x00FA, 0)) {
        Serial.println("[SYSTEM] ✅ 电机成功锁定使能！此时电机应当已经带电抱死（用手拧不动）。");
        delay(50);
        
        // 步骤三：写 0x00F1 复合寄存器组，连续注入速度数据
        Serial.println("[SYSTEM] 🚀 正在精准注入 Modbus 10H 速度核心控制帧...");
        if (sendModbusSpeed10H(0, 100, TARGET_SPEED_FLOAT)) { // 方向=0正转，加速度=100，速度=300RPM
          Serial.println("[SYSTEM] 🎉 发送成功！指令已锁入，电机进入闭环运行模式。");
          isMotorConfigured = true;
          break;
        }
      }
    }
    delay(200); 
  }
}

void loop() {
  static int readIdx = 0;

  // 高频无缝死循环刷新
  sendModbusRead(readTasks[readIdx]);
  readIdx++;
  if (readIdx >= TOTAL_READ_TASKS) {
    readIdx = 0; 
    Serial.printf("V: %.2fV | Spd: %d | Pos: %.2f | I: %d mA\n",
                  myMotor.bus_voltage, myMotor.speed, myMotor.position, myMotor.phase_current);
  }
  delay(5); 
}