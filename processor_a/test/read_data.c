#include <Arduino.h>

#define TX_PIN 42
#define RX_PIN 41
#define SLAVE_ADDR 1
#define BAUD_RATE 115200

// 严格遵守正点原子手册第 6 条要求：两条完整指令之间应保持至少 2ms 的时间间隔
const unsigned long HARD_DELAY = 5; 

struct MotorData {
  float bus_voltage;       // Byte1~4: 电机总线电压 (V)
  int16_t phase_current;   // Byte5~6: 读取相电流 (mA)
  float flux;              // Byte7~10: 电机磁链参数 (mWb)
  float resistance;        // Byte11~14: 电机相电阻参数 (Ω)
  float inductance;        // Byte15~18: 电机相电感参数 (mH)
  int16_t speed;           // Byte19~20: 实时转速 (RPM)
  int32_t target_position; // Byte21~24: 目标位置
  int64_t position;        // Byte25~28: 纯软件维护的相对物理位置（有符号 64 位）
  int64_t position_offset; // 上电一瞬间捕获到的绝对物理位置基准（有符号 64 位）
  float pos_error;         // Byte29~32: 位置误差
  uint32_t pulse_count;    // Byte33~36: 读取脉冲数
  uint16_t enable_status;  // Byte37: 电机使能状态 (0:使能, 1:失能)
  uint16_t target_reached; // Byte38: 电机到位标志 (0:未到位, 1:到位)
  uint16_t stall_flag;     // Byte39: 电机堵转标志 (0:未堵转, 1:堵转)
};
MotorData myMotor;

// 标准 Modbus RTU CRC16 计算码
uint16_t calculateCRC(uint8_t *buf, int len) {
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

// 【洗白网 1】：严格依据正点原子 STM32 小端联合体转 Modbus 协议格式提取浮点数 (CDAB 字节序)
float getFloatFromBuf(uint8_t *buf, int offset) {
  float val;
  uint8_t b0 = buf[offset];     
  uint8_t b1 = buf[offset + 1]; 
  uint8_t b2 = buf[offset + 2]; 
  uint8_t b3 = buf[offset + 3]; 

  uint8_t float_bytes[4] = { b3, b2, b1, b0 }; 
  memcpy(&val, float_bytes, 4);

  if (isnan(val) || isinf(val)) {
    return 0.00f;
  }
  return val;
}

// 【洗白网 2】：安全解析大端序有符号 Int16 类型的电机相电流、速度值
int16_t getInt16FromBuf(uint8_t *buf, int offset) {
  uint16_t uVal = ((uint16_t)buf[offset] << 8) | (uint16_t)buf[offset + 1];
  int16_t sVal;
  memcpy(&sVal, &uVal, 2);
  return sVal;
}

// 【洗白网 3】：安全解析大端序有符号 Int32 类型的物理位置、脉冲、误差
int32_t getInt32FromBuf(uint8_t *buf, int offset) {
  uint32_t uVal = ((uint32_t)buf[offset]     << 24) |
                  ((uint32_t)buf[offset + 1] << 16) |
                  ((uint32_t)buf[offset + 2] << 8)  |
                  ((uint32_t)buf[offset + 3]);
  int32_t sVal;
  memcpy(&sVal, &uVal, 4);
  return sVal;
}

// =================== 【上电专用：阻塞式高精准捕获原始物理位置】 ===================
int32_t captureRawPositionBlocking() {
  // 下行帧：01 04 00 2A 00 02 50 03 (独立读取实时物理绝对位置，用于校准零点)
  uint8_t cmd[8] = { SLAVE_ADDR, 0x04, 0x00, 0x2A, 0x00, 0x02, 0x50, 0x03 };

  while(Serial2.available()) Serial2.read(); 
  Serial2.write(cmd, 8); 
  Serial2.flush();

  int expectedLen = 9; 
  unsigned long startT = millis();
  while (Serial2.available() < expectedLen) {
    if (millis() - startT > 50) return 0; 
  }

  uint8_t rxBuf[16];
  for (int i = 0; i < expectedLen; i++) rxBuf[i] = Serial2.read();

  uint16_t rxCrc = calculateCRC(rxBuf, expectedLen - 2);
  uint16_t msgCrc = (rxBuf[expectedLen - 1] << 8) | rxBuf[expectedLen - 2];

  if (rxBuf[0] == SLAVE_ADDR && rxBuf[1] == 0x04 && rxBuf[2] == 0x04 && rxCrc == msgCrc) {
    return getInt32FromBuf(rxBuf, 3); 
  }
  return 0;
}

// =================== 【上电校准：纯软件虚拟零点初始化】 ===================
void initSoftwareZeroBaseline() {
  Serial.print("[CONFIG] Initializing software virtual zero point... ");
  int32_t last_raw = 0;
  
  for(int i = 0; i < 5; i++) {
    last_raw = captureRawPositionBlocking();
    delay(40);
  }
  
  myMotor.position_offset = (int64_t)last_raw;
  myMotor.position = 0; 
  
  Serial.print("SUCCESS! Baseline offset locked: ");
  Serial.printf("%lld\n", (long long)myMotor.position_offset);
}

// =================== 【核心优化：单次读取并精准映射全系统参数】 ===================
bool pollAllSystemParametersBulk() {
  // 严格根据 5.3.18 下行帧数据配置：01 04 00 31 00 14 A1 CA
  uint8_t cmd[8] = { SLAVE_ADDR, 0x04, 0x00, 0x31, 0x00, 0x14, 0xA1, 0xCA };

  // 1. 清空接收残留干扰
  while(Serial2.available()) Serial2.read(); 

  // 2. 发送批量读取指令
  Serial2.write(cmd, 8); 
  Serial2.flush();

  // 3. 上行帧预期返回长度计算：从机地址(1B) + 功能码(1B) + 字节数(1B,为28H即40字节) + 数据段(40B) + CRC(2B) = 45 字节
  int expectedLen = 45; 
  unsigned long startT = millis();
  bool timeout = false;
  
  while (Serial2.available() < expectedLen) {
    if (millis() - startT > 60) { // 稍微放宽批量大包的接收等待超时
      timeout = true;
      break;
    }
  }

  if (timeout) return false;

  // 4. 全量读取回包
  uint8_t rxBuf[50];
  for (int i = 0; i < expectedLen; i++) {
    rxBuf[i] = Serial2.read();
  }

  // 5. 校验安全网 (地址=01H, 功能码=04H, 返回字节数=40)
  uint16_t rxCrc = calculateCRC(rxBuf, expectedLen - 2);
  uint16_t msgCrc = (rxBuf[expectedLen - 1] << 8) | rxBuf[expectedLen - 2];

  if (rxBuf[0] == SLAVE_ADDR && rxBuf[1] == 0x04 && rxBuf[2] == 0x28 && rxCrc == msgCrc) {
    
    // 【严格遵循正点原子 5.3.18 映射关系进行指针偏移解析】
    // 基础数据起始索引为 rxBuf[3]，即为手册中的 Byte1
    
    myMotor.bus_voltage    = getFloatFromBuf(rxBuf, 3);   // Byte1~Byte4
    myMotor.phase_current  = getInt16FromBuf(rxBuf, 7);   // Byte5~Byte6
    myMotor.flux           = getFloatFromBuf(rxBuf, 9);   // Byte7~Byte10
    myMotor.resistance     = getFloatFromBuf(rxBuf, 13);  // Byte11~Byte14
    myMotor.inductance     = getFloatFromBuf(rxBuf, 17);  // Byte15~Byte18
    myMotor.speed          = getInt16FromBuf(rxBuf, 21);  // Byte19~Byte20
    myMotor.target_position= getInt32FromBuf(rxBuf, 23);  // Byte21~Byte24
    
    // 实时物理位置 Byte25~Byte28 深度融入软件 64 位零点换算网
    int32_t raw_position   = getInt32FromBuf(rxBuf, 27);  // Byte25~Byte28
    myMotor.position       = (int64_t)raw_position - myMotor.position_offset;
    
    myMotor.pos_error      = getFloatFromBuf(rxBuf, 31);  // Byte29~Byte32
    myMotor.pulse_count    = (uint32_t)getInt32FromBuf(rxBuf, 35); // Byte33~Byte36
    
    // 单字节状态标志位的提取 (Byte37, 38, 39)
    myMotor.enable_status  = rxBuf[39]; // Byte37
    myMotor.target_reached = rxBuf[40]; // Byte38
    myMotor.stall_flag     = rxBuf[41]; // Byte39

    // 每次成功通信后强挂延时，保证总线硬件级物理消磁
    delay(HARD_DELAY); 
    return true; 
  }
  return false; 
}

void setup() {
  // 初始化驱动器通讯串口 (Serial2)
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  // 初始化上位机 PC 调试打印串口 (Serial)
  Serial.begin(BAUD_RATE);
  delay(1200);
  Serial.println("\n=== 正点原子 PD42S1 高性能单条指令批量监控系统已就绪 ===");

  // 捕获上电物理机械位置，建立软件零点
  initSoftwareZeroBaseline();

  Serial.println("[SYSTEM] Bulk configuration complete. Ultra-high speed loop running...");
}

void loop() {
  if (pollAllSystemParametersBulk()) {
    // 串口全量高速打印输出
    Serial.printf("V: %.1fV | Spd: %d RPM | Pos: %lld | Flux: %.2fmWb | R: %.2fR | L: %.3fmH | I: %d mA | Err: %.1f | Pls: %u | En: %d | Reach: %d | Stall: %d\n",
                  myMotor.bus_voltage, myMotor.speed, (long long)myMotor.position, myMotor.flux,
                  myMotor.resistance, myMotor.inductance, myMotor.phase_current, myMotor.pos_error,
                  myMotor.pulse_count, myMotor.enable_status, myMotor.target_reached, myMotor.stall_flag);
  } else {
    Serial.println("[ERROR] 总线读取超时或大包数据 CRC 校验失败...");
    delay(10); // 出错时短暂挂起保护总线
  }
}