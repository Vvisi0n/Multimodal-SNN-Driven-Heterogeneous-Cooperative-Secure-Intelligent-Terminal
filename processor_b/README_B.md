# ESP32-S3 N16R8 四分类 AI 视觉检测系统

基于 **ESP32-S3 N16R8** 与 **OV5640** 摄像头构建的嵌入式边缘 AI 视觉检测系统。系统通过双核并行架构实现实时视频推流与 TFLite-Micro 四分类推理的并发执行，推理结果通过 MQTT over TLS 上报至云端。

## 硬件平台

| 组件 | 规格 |
|------|------|
| 主控 | ESP32-S3 N16R8（双核 Xtensa LX7, 240MHz） |
| 存储 | 16MB Flash（QIO/OPI） + 8MB PSRAM（Octal SPI） |
| 摄像头 | OV5640（200万像素，支持 JPEG 输出） |
| 外设 | 蜂鸣器（GPIO 19） |

## 系统架构

```
┌─────────────────────────────────────────────────┐
│                   ESP32-S3                      │
│                                                  │
│  Core 0 (Protocol Core)     Core 1 (App Core)   │
│  ┌───────────────────┐     ┌──────────────────┐ │
│  │  UDP JPEG 推流     │     │  TFLite-Micro    │ │
│  │  (实时视频流)       │     │  四分类推理       │ │
│  │                   │     │                  │ │
│  │  MQTT 保活/收发    │     │  JPEG→RGB 解码   │ │
│  │  (PubSubClient)   │     │  预处理 + 推理    │ │
│  │                   │     │  结果统计上报     │ │
│  │  WiFi 协议栈      │     │                  │ │
│  └───────────────────┘     └──────────────────┘ │
│           ↕ 共享 JPEG 帧缓冲区 (信号量保护)        │
└─────────────────────────────────────────────────┘
```

- **Core 0**：UDP 视频推流 + WiFi 协议栈 + MQTT 客户端
- **Core 1**：TFLite-Micro 推理流水线（JPEG→RGB→预处理→推理→后处理）
- **通信**：双核通过共享 JPEG 帧缓冲区 + FreeRTOS 信号量实现线程安全的数据交换

## 核心功能模块

### AI 推理引擎 (`infer.cpp`)
- **模型**：四分类 TFLite 模型（INT8 对称量化），约 120KB
- **输入**：96×96×3 RGB
- **分类**：`CopperSpacer` / `background` / `hand` / `wire`
- **预处理**：双线性插值缩放 → [-1,1] 归一化 → INT8 量化
- **后处理**：反量化 → Softmax → Argmax
- **内存**：600KB 张量 Arena 位于 PSRAM

### MQTT 客户端 (`mqtt.cpp`)
- **协议**：MQTT over TLS（WiFiClientSecure）
- **Broker**：HiveMQ Cloud（端口 8883）
- **配置**：支持从云端 CDN 动态拉取 JSON 配置，失败时回退硬编码默认值
- **订阅**：最多 5 个主题，默认 `cp_mqtt`
- **线程安全**：FreeRTOS 互斥锁保护跨核并发访问

### 其他模块
| 模块 | 功能 |
|------|------|
| `udp.h` | 实时 JPEG 视频流 UDP 推流 |
| `ble.cpp` | BLE GATT 客户端（自定义 128-bit UUID） |
| `buzzer.cpp` | GPIO 19 蜂鸣器控制（单次/循环/定时） |
| `app_httpd.cpp` | HTTP 服务器（人脸检测/识别） |

## 功能开关

在 `main.cpp` 中通过宏定义控制编译行为：

| 宏 | 默认 | 说明 |
|----|------|------|
| `ENABLE_UDP_STREAM` | 1 | 启用 UDP 视频推流 |
| `ENABLE_AI_INFERENCE` | 1 | 启用 TFLite 推理 |
| `ENABLE_MQTT` | 1 | 启用 MQTT 客户端 |
| `ENABLE_AI_DATA_SHARE` | 0 | 推理结果自动上报 MQTT |

## 依赖库

- **olikraus/U8g2** — OLED 显示屏驱动
- **knolleary/PubSubClient** — MQTT 协议客户端
- **Edge Impulse SDK** — TFLite-Micro 推理框架（内嵌）
- **ArduinoJson** — JSON 解析（自动依赖）

## 工作流程

1. **启动** → 串口初始化 → 蜂鸣器自检
2. **摄像头** → 检测 PSRAM → 配置 OV5640（VGA/JPEG, 20MHz XCLK）
3. **WiFi** → 连接 AP → 获取 IP 地址
4. **MQTT** → 从云端 CDN 拉取配置 → TLS 连接 HiveMQ → 订阅 `cp_mqtt`
5. **TFLite** → 加载四分类模型 → PSRAM 分配张量 Arena → 校验维度
6. **Core 1** → 启动推理任务：获取共享 JPEG → 解码→预处理→推理（每 500ms 输出遥测）
7. **Core 0** → 启动 UDP 推流任务
8. **主循环** → MQTT 保活轮询（每秒一次）

## 技术亮点

- **双核异构并行**：充分利用 ESP32-S3 双核，视频推流与 AI 推理互不干扰
- **PSRAM 优化**：张量 Arena 和 RGB 缓冲区均分配在 8MB PSRAM 中
- **INT8 量化推理**：模型仅 120KB，推理内存约 600KB，适合边缘设备
- **云端动态配置**：MQTT 配置可远程更新，无需重新烧录固件
- **TLS 安全通信**：MQTT 通过 TLS 加密连接云端 Broker
- **模块化设计**：功能开关 + 独立模块，易于裁剪和扩展
- **线程安全**：FreeRTOS 互斥锁 + 信号量保护跨核共享资源

## 项目结构

```
ESP32S3_N16R8_CAM1/
├── platformio.ini              # PlatformIO 配置
├── src/
│   ├── main.cpp                # 主入口：初始化、双核任务调度
│   ├── infer.cpp / infer.h     # TFLite-Micro 推理引擎
│   ├── mqtt.cpp / mqtt.h       # MQTT 客户端（TLS + 云端配置）
│   ├── udp.h                   # UDP 推流接口
│   ├── ble.cpp                 # BLE GATT 客户端
│   ├── buzzer.cpp / buzzer.h   # 蜂鸣器控制
│   ├── app_httpd.cpp           # HTTP 服务器（人脸检测）
│   ├── camera_pins.h           # 摄像头引脚定义
│   ├── hand_det.h              # 二分类手势模型数据 (120KB)
│   ├── quad_det.h              # 四分类检测模型数据 (120KB)
│   └── edge-impulse-sdk/       # Edge Impulse TFLite-Micro SDK
├── lib/
│   ├── Hand_detec_inferencing/ # Edge Impulse 推理库
│   └── JY901/                  # JY901 传感器库
└── test/                       # 历史版本备份
    ├── backup_http_v1/
    └── backup_udp_v1/
```

## 快速开始

1. **配置 WiFi**：在 `main.cpp` 中设置 `WIFI_SSID` 和 `WIFI_PASSWORD`
2. **配置 MQTT**：默认连接 HiveMQ Cloud，可通过 CDN JSON 动态配置
3. **编译烧录**：
   ```bash
   pio run -t upload -e esp32-s3
   ```
4. **查看输出**：打开串口监视器（115200 baud）查看启动日志和推理结果
