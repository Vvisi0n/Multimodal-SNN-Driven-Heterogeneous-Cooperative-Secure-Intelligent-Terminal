# 基于多模态脉冲神经网络的异构协同安全智控终端
# Multimodal SNN-Driven Heterogeneous Cooperative Secure Intelligent Terminal

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32--S3-green.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino%20%7C%20ESP--IDF-yellow.svg)]()

---

## 📖 项目简介 | Project Introduction

**中文：** 本项目实现了一款基于**多模态脉冲神经网络（SNN）** 的异构协同安全智控边缘AI终端，面向工业物联网场景，采用三片 **ESP32-S3** 处理器异构级联架构，将数据采集、边缘推理、视觉安全与人机交互集成于一体，突破传统工控系统算力不足、实时性差、能耗偏高的局限。终端整机功耗 **< 5W**，时序故障诊断准确率 **96.5%~98.2%**，危险中断响应时间 **< 100ms**，为工业物联网低功耗、高安全、智能化运维提供可靠技术方案。

**English:** This project implements an edge AI terminal based on **multi-modal Spiking Neural Network (SNN)** for heterogeneous collaborative safety-intelligent control. Targeting industrial IoT scenarios, it adopts a heterogeneous cascaded architecture of three **ESP32-S3** processors, integrating data acquisition, edge inference, vision-based safety, and human-machine interaction into a unified system, breaking through the limitations of traditional industrial control systems in computing power, real-time performance, and energy consumption. The terminal achieves **< 5W** power consumption, **96.5%~98.2%** temporal fault diagnosis accuracy, and **< 100ms** safety interrupt response time, providing a reliable technical solution for low-power, high-safety, and intelligent IIoT operations.
<div align="center"><img src="https://cdn.codenews.cc/blog/66e224dd650b705684c11436585d84dd.jpg" style="height:500px;width:auto;object-fit:contain;"></div>

---

## 🎯 核心特性 | Core Features

### 🤖 边缘智能闭环控制 | Edge-Intelligent Closed-Loop Control
**中文：** 构建“采集-计算-控制”一体化边缘智能体，将多模态SNN推理下沉至终端，打通数据流与控制流闭环，实现微秒级本地决策。

**English:** Building an integrated "acquisition-computation-control" edge intelligence agent that offloads multi-modal SNN inference to the terminal, establishing a closed loop between data flow and control flow, enabling microsecond-level local decision-making.

---

### 👁️ 视觉安全主动防护 | Vision-Based Active Safety Protection
**中文：** 集成OV5640图像采集，本地部署轻量化视觉SNN模型，实时检测手部及上肢区域。一旦突入虚拟安全红线，立即触发硬件中断锁止设备，人员撤离后自动恢复。

**English:** Integrated OV5640 image acquisition with locally deployed lightweight vision SNN model for real-time detection of hands and upper limbs. Once the virtual safety redline is breached, hardware interrupt is immediately triggered to lock the equipment, with automatic recovery after personnel evacuation.
<div align="center"><img src="https://cdn.codenews.cc/blog/68024976cc735d0989a82d049256dcdc.png" style="height:500px;width:auto;object-fit:contain;"></div>

---

### 📊 设备状态精准诊断 | Precision Equipment Condition Diagnosis
**中文：** 采集温湿度、三相电、振动、定子磁链等多维信号，部署时序SNN模型，实现微秒级隐性故障诊断与精准调控，大幅降低误判漏判。

**English:** Acquiring multi-dimensional signals including temperature/humidity, three-phase power, vibration, and stator flux linkage, with deployed temporal SNN models for microsecond-level latent fault diagnosis and precision control, significantly reducing false positives and missed detections.

---

### 🖥️ 人机交互与数据追溯 | HMI & Data Traceability
**中文：** 基于LVGL搭建工业级触控看板，支持现场操控与数据本地持久化。通过MQTT上传云端完成SQL归档与Web可视化，内嵌千问大模型接口提供智能运维问答。

**English:** Industrial-grade LVGL-based touch dashboard supporting on-site control and local data persistence. MQTT-based cloud upload for SQL archiving and Web visualization, with embedded Qwen LLM interface for intelligent O&M Q&A.
<div align="center"><img src="https://cdn.codenews.cc/blog/832c65956cfb63a26555929825a0db1f.png" style="height:500px;width:auto;object-fit:contain;"></div>

---

### 🔧 异构分布式硬件架构 | Heterogeneous Distributed Hardware Architecture
**中文：** 三片ESP32-S3串口级联，将视觉SNN、时序SNN与控制任务解耦至独立核心，保障并行推理高吞吐，整机功耗<5W，适配严苛工业场景。

**English:** Three ESP32-S3 processors cascaded via UART, decoupling vision SNN, temporal SNN, and control tasks onto independent cores, ensuring high-throughput parallel inference with <5W total power consumption, suitable for harsh industrial environments.

---

## 🏗️ 系统架构 | System Architecture

<div align="center"><img src="https://cdn.codenews.cc/blog/7be0428c5bc0fe20ea7cb9e2fc7bfd61.png" style="height:700px;width:auto;object-fit:contain;"></div>


---

## 📁 目录结构 | Directory Structure

```
.
├── firmware/                     # 固件源码 | Firmware Source
│   ├── processor_a/              # 处理器A：中央调度 (Arduino)
│   │   ├── src/
│   │   │   ├── sensors/          # 传感器驱动 | Sensor Drivers
│   │   │   ├── snn/              # 时序SNN推理 | Temporal SNN
│   │   │   ├── router/           # 消息路由 | Message Router
│   │   │   └── main.cpp
│   │   └── platformio.ini
│   │
│   ├── processor_b/              # 处理器B：视觉感知 (Arduino)
│   │   ├── src/
│   │   │   ├── camera/           # OV5640驱动 | OV5640 Driver
│   │   │   ├── snn/              # 视觉SNN推理 | Vision SNN
│   │   │   ├── udp/              # UDP视频推流 | UDP Streaming
│   │   │   └── main.cpp
│   │   └── platformio.ini
│   │
│   └── processor_c/              # 处理器C：人机交互 (ESP-IDF)
│       ├── main/
│       │   ├── lvgl/             # LVGL触控看板 | LVGL Dashboard
│       │   ├── mqtt/             # MQTT通信 | MQTT Communication
│       │   ├── llm/              # 大模型接口 | LLM Interface
│       │   ├── motor/            # 电机控制 | Motor Control
│       │   └── main.c
│       └── CMakeLists.txt
│
├── models/                       # 模型训练 | Model Training
│   ├── training/                 # SNN训练脚本 (Python)
│   │   ├── temporal_snn/         # 时序SNN | Temporal SNN
│   │   ├── vision_snn/           # 视觉SNN | Vision SNN
│   │   └── quantization/         # INT8量化 | INT8 Quantization
│   └── weights/                  # 预训练权重 | Pretrained Weights
│
├── cloud/                        # 云端平台 | Cloud Platform
│   ├── mqtt_broker/              # EMQX配置 | EMQX Config
│   ├── database/                 # MySQL表结构 | MySQL Schema
│   └── web_dashboard/            # Web可视化 | Web Dashboard
│       ├── index.html
│       ├── app.js
│       └── style.css
│
├── hardware/                     # 硬件设计 | Hardware Design
│   ├── pcb/                      # PCB设计 (立创EDA)
│   │   ├── main_board/           # 核心控制板 | Main Board
│   │   ├── vision_board/         # 视觉模块 | Vision Module
│   │   └── display_board/        # 显示屏模块 | Display Module
│   ├── cad/                      # 3D模型 (STL)
│   │   └── enclosure/            # 机壳模型 | Enclosure
│   └── schematics/               # 电路原理图 | Schematics
│
├── docs/                         # 文档 | Documentation
│   ├── api/                      # API文档 | API Docs
│   ├── protocol/                 # 通信协议 | Protocol
│   └── deployment/               # 部署指南 | Deployment Guide
│
├── tests/                        # 测试 | Tests
│   ├── unit/                     # 单元测试 | Unit Tests
│   └── integration/              # 集成测试 | Integration Tests
│
├── LICENSE
└── README.md
```

---

## 🚀 快速开始 | Quick Start

### 硬件准备 | Hardware Requirements

| 组件 Component | 型号 Model | 数量 Qty |
|----------------|------------|----------|
| 主控芯片 Main MCU | ESP32-S3-WROOM | 3 |
| 摄像头 Camera | OV5640 | 1 |
| 显示屏 Display | 4.3" RGB Touch (480×272) | 1 |
| 温湿度传感器 Temp/Humidity | DHT11 | 1 |
| 六轴传感器 6-Axis IMU | MPU6050 | 1 |
| 麦克风 Microphone | ECM | 1 |
| 电机驱动 Motor Driver | L298N | 1 |

### 环境搭建 | Environment Setup

```bash
# 1. 安装 PlatformIO (处理器A和B | For Processor A & B)
# 访问 Visit: https://platformio.org/

# 2. 安装 ESP-IDF (处理器C | For Processor C)
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
. ./export.sh

# 3. 安装 Python 依赖 (模型训练 | For Model Training)
pip install tensorflow spikingjelly numpy pandas
```

### 固件烧录 | Firmware Flashing

```bash
# 烧录处理器A (中央调度 | Central Scheduler)
cd firmware/processor_a
pio run -t upload

# 烧录处理器B (视觉感知 | Vision Processor)
cd ../processor_b
pio run -t upload

# 烧录处理器C (人机交互 | HMI Processor)
cd ../processor_c
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB2 flash
```

### 云端部署 | Cloud Deployment

```bash
# 1. 启动 EMQX Broker | Start EMQX Broker
docker run -d --name emqx -p 1883:1883 -p 8083:8083 emqx/emqx:latest

# 2. 初始化数据库 | Initialize Database
mysql -u root -p < cloud/database/schema.sql

# 3. 启动 Web 看板 | Start Web Dashboard
cd cloud/web_dashboard
python -m http.server 8080
```

---

## 🔬 性能指标 | Performance Metrics

<div align="center">

| 指标项 Metric | 本终端 Our Terminal | 常规工控设备 Conventional IPC |
|---------------|---------------------|-------------------------------|
| 整机功耗 Power Consumption | 3.8W ~ 4.3W | 15.2W ~ 47.2W |
| 时序SNN诊断准确率 Temporal SNN Accuracy | 96.5% ~ 98.2% | — |
| 视觉SNN检测准确率 Vision SNN Accuracy | 93.7% ~ 95.5% | — |
| 故障检测响应时间 Fault Detection Latency | 54ms ~ 102ms | 231ms ~ 486ms |
| 危险中断响应时间 Safety Interrupt Latency | 51ms ~ 89ms | 205ms ~ 365ms |
| 数据上传丢包率 Packet Loss Rate | 0.1% ~ 0.3% | 0.5% ~ 2% |
| 堆区内存占用 Heap Usage | 198KB ~ 236KB | 512KB ~ 2MB |
| 工作温度范围 Operating Temperature | -25℃ ~ 70℃ | 0℃ ~ 50℃ |

</div>

---

## 🧠 SNN模型说明 | SNN Model Specifications

### 时序SNN（故障诊断）| Temporal SNN (Fault Diagnosis)
- **输入层 Input:** 32维传感器特征 (32-dim sensor features: temp×2, 3-phase×3, vibration×6, flux×3, audio×18)
- **编码 Encoding:** 时间编码 Time-to-First-Spike
- **网络结构 Architecture:** 全连接三层 3-Layer FC (32→64→8)
- **神经元模型 Neuron:** LIF (Leaky Integrate-and-Fire)
- **学习规则 Learning:** STDP (Spike-Timing-Dependent Plasticity)
- **输出分类 Outputs:** 正常运行Normal、皮带松弛Belt Slack、系统过载Overload、重量突增Weight Surge、振动超标Excess Vibration

### 视觉SNN（安全检测）| Vision SNN (Safety Detection)
- **输入分辨率 Resolution:** 320×240
- **帧率 Frame Rate:** 15fps
- **网络结构 Architecture:** 三层卷积脉冲网络 3-Layer Conv-SNN (Conv2D→Pool→Conv2D→Pool→FC)
- **输出 Output:** 手部/上肢边界框 + 置信度 Bounding Box + Confidence

### 模型量化 | Model Quantization
- **量化方式 Method:** INT8量化 INT8 Quantization
- **模型大小 Size:** ≈103KB
- **精度保持 Accuracy Retention:** >92%

---

## 📷 实物展示 | Physical Showcase

| 终端整体 Device Overall | 触控看板 LVGL Dashboard |
|-------------------------|-------------------------|
| <div align="center"><img src="https://cdn.codenews.cc/blog/942508a060dd3c97bad6a9527b1d8cf1.jpg" style="height:500px;width:auto;object-fit:contain;"></div> | <div align="center"><img src="https://cdn.codenews.cc/blog/4947eefecae75746721fd41c222080d2.jpg" style="height:500px;width:auto;object-fit:contain;"></div> |

| Web大屏 Web Dashboard | 视觉检测 Vision Detection |
|-----------------------|---------------------------|
| <div align="center"><img src="https://cdn.codenews.cc/blog/351bec71663fb6feac30c98e55891b7d.png" style="height:500px;width:auto;object-fit:contain;"></div> | <div align="center"><img src="https://cdn.codenews.cc/blog/a11115dc318f5cf1ee9b81a1c05a515d.png" style="height:500px;width:auto;object-fit:contain;"></div> |

---

## 🤝 贡献指南 | Contributing

**中文：** 欢迎提交 Issue 和 Pull Request。提交代码前请确保：
1. 代码符合项目编码规范
2. 已通过所有单元测试
3. 新功能有对应的文档说明

**English:** Issues and Pull Requests are welcome. Please ensure before submitting:
1. Code follows project coding standards
2. All unit tests pass
3. New features have corresponding documentation

---

## 📄 开源协议 | License

本项目采用 MIT 协议，详见 [LICENSE](LICENSE) 文件。
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 📚 参考资料 | References

- [ESP32-S3 技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_cn.pdf)
- [LVGL 图形库文档](https://docs.lvgl.io/)
- [SpikingJelly SNN 框架](https://github.com/fangwei123456/spikingjelly)
- [SpikeFSL: Spike-Driven Few-Shot Learning for Cross-Domain Hyperspectral Image Classification](https://ieeexplore.ieee.org/document/11558490) 
- [基于SNN类脑芯片的神经网络模型部署](https://www.bilibili.com/video/BV1wh411D7Qi/) 
- [基于ESP-IDF的ESP32-S3蓝牙透传实现](https://blog.csdn.net/Volvo_2/article/details/158364658)

---


**⭐ 如果这个项目对您有帮助，请给一个 Star 支持我们！**
**⭐ If this project helps you, please give us a Star!**
