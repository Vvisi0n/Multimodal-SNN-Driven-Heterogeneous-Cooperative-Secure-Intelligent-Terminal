## 📋 项目文件概述

本项目是一个基于 **ESP32-S3** 的工业智能终端系统，集成了边缘计算、云端AI诊断、多模态传感器数据采集和图形用户界面。系统采用双核异构架构，实现高效的多任务并行处理，适用于工业设备监控、智能诊断和人机交互等场景。

### 系统架构图

```
┌─────────────────────────────────────────────────────────────┐
│                     ESP32-S3 双核系统                       │
├─────────────────────────┬───────────────────────────────────┤
│    Core 0 (通信核)      │      Core 1 (UI核)              │
│  ┌──────────────────┐   │    ┌────────────────────────┐   │
│  │ WiFi/UDP/MQTT    │   │    │    LVGL GUI渲染         │   │
│  │ LLM云请求        │   │    │    5个交互屏幕          │   │
│  │ UART传感器通信   │   │    │    实时图表更新         │   │
│  └──────────────────┘   │    └────────────────────────┘   │
└──────────┬──────────────┴──────────────┬──────────────────┘
           │                               │
           ▼                               ▼
    ┌─────────────┐                ┌─────────────┐
    │  HiveMQ云   │                │  LCD触摸屏  │
    │  SiliconFlow│                │  800×480    │
    │  LLM API    │                └─────────────┘
    └─────────────┘
           │
           ▼
    ┌─────────────┐
    │  PC上位机   │
    │  UDP控制台  │
    │  视频接收   │
    └─────────────┘
```

---

## 🚀 核心特性

### 1. 双核并行架构
- **Core 0 (通信核)**：WiFi网络管理、MQTT云通信、UART传感器数据采集、LLM API请求
- **Core 1 (UI核)**：LVGL图形界面渲染、触摸事件处理、实时数据显示
- **跨核数据安全**：通过 `g_data_mutex` 互斥锁实现线程安全数据共享

### 2. 丰富的交互界面 (LVGL)
| 屏幕 | 功能 | 核心组件 |
|------|------|----------|
| **登录界面** | 账号密码验证 | 文本输入框、验证按钮 |
| **主仪表盘** | 四维传感器数据 + 实时曲线 | 温度/湿度/声源/振动图表 |
| **AI诊断** | 设备状态诊断 + LLM对话 | 置信度显示、聊天窗口 |
| **设备控制** | 电机参数调节 | 速度/位置/电流控制、启停按钮 |
| **系统设置** | 网络配置、系统监控 | WiFi状态、IP、CPU/栈使用率 |

### 3. 云端集成
- **MQTT云连接**：通过TLS连接HiveMQ Cloud，支持配置热更新（JSON over HTTPS）
- **LLM AI诊断**：集成SiliconFlow API（Qwen2.5-7B），SSE流式响应，实时推送到UI
- **故障容错**：云端配置失败时自动fallback到本地默认值

### 4. 可靠通信
- **UDP广播发现**：端口8888，支持多设备自动发现与双向通信
- **UDP FEC前向纠错**：1:4异或冗余编码，视频流抗丢包能力
- **UART串口通信**：与MCU1通信（UART1, TX:17/RX:18），栈守卫动态检测

### 5. 工具链支持
- **PC端UDP管理工具**：Tkinter GUI，自动发现设备，支持广播/私发
- **视频流处理**：FEC解码 + JPEG帧重组 + OpenCV显示
- **图片压缩工具**：RLE按行压缩，支持LVGL原始图片格式转换

---

## 📁 项目结构

```
ESP32-S3-Industrial-Terminal/
├── main/                          # ESP32主程序
│   ├── ui/                        # LVGL UI界面
│   │   ├── screen1_login.c        # 登录界面
│   │   ├── screen2_dashboard.c    # 主仪表盘
│   │   ├── screen3_ai_diagnosis.c # AI诊断
│   │   ├── screen4_device_control.c # 设备控制
│   │   └── screen5_settings.c     # 系统设置
│   ├── wifi_udp/                  # 网络通信
│   │   ├── wifi_udp.c            # WiFi连接 + UDP通信
│   │   ├── mqtt.c                # MQTT云连接
│   │   └── llm_cloud.c           # LLM API调用（SSE流式）
│   ├── uart/                      # 串口通信
│   │   └── uart.c                # UART传感器数据接收
│   ├── utils.c                    # 工具函数与全局变量
│   └── main.c                     # 主入口（双核任务初始化）
├── components/                    # 组件库
│   ├── lvgl/                     # LVGL图形库（v8.3.11）
│   │   └── porting/              # 显示/触摸/文件系统移植
│   └── lvgl_esp32_drivers/       # LCD驱动（I2C总线）
├── Python/                        # PC端工具集
│   ├── udp_server.py             # UDP多设备管理GUI
│   ├── mqtt_test.py              # MQTT测试终端
│   ├── AI_chat.py                # AI对话客户端
│   ├── udp_fec_receiver.py       # FEC视频流接收库
│   ├── video_processor.py        # 视频流显示
│   ├── a.py                      # 视频流+拍照功能
│   ├── compress_rle.py           # RLE图片压缩工具
│   └── dataset/                  # 训练数据集
│       ├── Crop_top.zip
│       ├── dataset.zip
│       └── wire.zip
├── partitions.csv                 # 分区表
├── sdkconfig                      # ESP-IDF配置
└── README.md                      # 本文档
```

---

## 🔧 硬件配置

### 核心硬件
| 组件 | 型号/规格 | 说明 |
|------|-----------|------|
| **MCU** | ESP32-S3 (双核 Xtensa LX7) | 主频240MHz，512KB SRAM |
| **显示屏** | LCD 800×480 | RGB接口，支持触摸 |
| **触摸控制器** | GT911 / CST3240 | I2C接口 |
| **外部存储** | SPIFFS (4MB) | 存储UI资源、配置文件 |
| **Flash** | 16MB | 固件存储 |

### 外设接口
- **UART1**: TX:GPIO17, RX:GPIO18 (与MCU1通信)
- **I2C**: SDA/SCL (触摸控制器)
- **WiFi**: 2.4GHz 802.11 b/g/n

---

## 📐 分区表

| 分区名称 | 类型 | 子类型 | 偏移地址 | 大小 |
|----------|------|--------|----------|------|
| nvs | 数据 | nvs | 0x9000 | 24KB (0x6000) |
| phy_init | 数据 | phy | 0xf000 | 4KB (0x1000) |
| factory | 应用 | factory | 0x10000 | 8MB (0x800000) |
| storage | 数据 | spiffs | - | 4MB (0x400000) |

---

## 💻 软件架构

### 1. 双核任务分配

```c
// Core 0: 通信任务
void core0_task(void *pvParameters) {
    wifi_init();           // WiFi连接
    mqtt_init();           // MQTT云连接
    udp_server_start();    // UDP服务器
    uart_init();           // UART传感器采集
    llm_request_handler(); // LLM云请求
}

// Core 1: UI任务  
void core1_task(void *pvParameters) {
    lvgl_init();           // LVGL初始化
    lcd_driver_init();     // 显示驱动
    touch_driver_init();   // 触摸驱动
    ui_create_screens();   // 创建5个屏幕
    lv_task_handler_loop(); // 定时刷新（5ms）
}
```

### 2. 数据流

```
传感器(MCU1) → UART → Core0解析 → 互斥锁 → Core1显示
     ↓
 UDP广播 → PC上位机 (监控/调试)
     ↓
 MQTT → HiveMQ Cloud (远程监控)
     ↓
 LLM API → SiliconFlow (AI诊断)
```

### 3. 安全机制
- **栈守卫**：UART任务通过 `__asm__` 获取栈指针，动态检测栈深度，超阈值时延迟处理
- **堆分配管理**：LLM响应使用堆分配队列，避免栈溢出和内存泄漏
- **看门狗**：任务超时自动重启

---

## 🎨 UI界面展示

### Screen 1 - 登录界面
- 账号/密码输入框
- 登录验证（本地/云端）
- 记住密码选项

### Screen 2 - 主仪表盘
- **传感器四维数据**：温度(℃)、湿度(%)、声源(dB)、振动(mm/s)
- **实时曲线图**：双图表显示历史趋势（可切换）
- **状态指示灯**：设备运行状态

### Screen 3 - AI诊断
- **当前状态**：正常/警告/故障
- **置信度**：诊断结果可信度百分比
- **时间戳**：最新诊断时间
- **LLM对话窗口**：自然语言交互，流式显示AI回复

### Screen 4 - 设备控制
- **电机参数**：速度(rpm)、位置(°)、相电流(A)
- **控制按钮**：方向切换、启动/停止
- **键盘输入**：数字参数设置

### Screen 5 - 系统设置
- **WiFi状态**：SSID/IP地址/RSSI
- **API配置**：MQTT Broker、LLM Key
- **系统监控**：CPU使用率、栈使用率
- **自动保存**：配置持久化到SPIFFS

---

## 🛠 PC端工具集

### 1. UDP服务器 (udp_server.py)
```
功能：
- 自动发现ESP32设备（广播）
- 设备编号管理
- 广播/私发消息
- Tkinter图形界面
```

### 2. 视频流接收 (video_processor.py)
```
流程：
UDP接收 → FEC异或解码 → JPEG帧重组 → OpenCV显示
丢包恢复：1:4冗余编码，可恢复50%丢包率
```

### 3. 图片压缩工具 (compress_rle.py)
```
支持格式：
- TRUE_COLOR (RGB888)
- TRUE_COLOR_ALPHA (RGBA8888)
压缩率：平均60-80%
输出：C数组（可直接嵌入固件）
```

---

## 🔌 通信协议

### UDP协议（端口8888）
```json
{
  "type": "sensor_data",
  "device_id": "ESP32_001",
  "timestamp": 1234567890,
  "data": {
    "temperature": 25.6,
    "humidity": 65.2,
    "sound": 45.3,
    "vibration": 1.2
  }
}
```

### MQTT主题
```
发布：
- /esp32/{id}/sensor    # 传感器数据
- /esp32/{id}/status    # 设备状态
- /esp32/{id}/event     # 事件通知

订阅：
- /esp32/{id}/config    # 配置更新
- /esp32/{id}/command   # 远程控制
```

### LLM API (SiliconFlow)
```
接口：https://api.siliconflow.cn/v1/chat/completions
模型：Qwen2.5-7B-Instruct
格式：SSE (Server-Sent Events)
```

---

## 📊 性能指标

| 指标 | 数值 | 说明 |
|------|------|------|
| **UI刷新率** | 60 FPS | LVGL双缓冲 |
| **UDP吞吐量** | 2 Mbps | 视频流传输 |
| **MQTT延迟** | < 100ms | 云端通信 |
| **LLM响应** | 流式1-2s首字 | SiliconFlow API |
| **栈使用** | < 60% | 栈守卫监测 |
| **内存占用** | ~200KB | 堆/栈/全局 |

---

## 🚀 快速开始

### 1. 环境准备
```bash
# 安装ESP-IDF v5.3.1
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.3.1
./install.sh
. ./export.sh
```

### 2. 配置项目
```bash
# 设置目标芯片
idf.py set-target esp32s3

# 配置WiFi/API密钥
idf.py menuconfig
# 进入: Component config → WiFi Configuration
# 设置: SSID, Password, MQTT Broker, LLM API Key
```

### 3. 编译与烧录
```bash
# 编译
idf.py build

# 烧录（自动擦除分区表）
idf.py -p /dev/ttyUSB0 flash

# 监控日志
idf.py -p /dev/ttyUSB0 monitor
```

### 4. PC端启动
```bash
# 安装依赖
pip install opencv-python pillow paho-mqtt requests

# 启动UDP服务器
python Python/udp_server.py

# 启动视频接收（需ESP32发送视频流）
python Python/video_processor.py

```

---

## 📝 开发注意事项

1. **内存管理**
   - 堆分配使用 `heap_caps_malloc()` 指定PSRAM（如可用）
   - LVGL分配器使用 `lv_mem_alloc_custom()` 优化碎片

2. **双核同步**
   - 使用 `g_data_mutex` 保护所有共享数据
   - 避免在ISR中调用LVGL API

3. **WiFi重连**
   - 实现自动重连机制（指数退避）
   - 断网时缓存MQTT消息

4. **LLM流式处理**
   - 解析SSE的 `data:` 字段
   - 使用环形缓冲区避免阻塞

5. **OTA升级**
   - 预留 `factory` 和 `ota_0/ota_1` 分区
   - HTTPS证书校验

---

## 📚 依赖库

| 库名 | 版本 | 用途 |
|------|------|------|
| **ESP-IDF** | v5.3.1 | 框架 |
| **LVGL** | v8.3.11 | 图形界面 |
| **lwIP** | 2.1.3 | TCP/IP栈 |
| **mbedTLS** | 3.1.0 | TLS加密 |
| **OpenCV** | 4.5+ | 视频处理 (PC) |
| **Paho-MQTT** | 1.6.1 | MQTT客户端 (PC) |

---
