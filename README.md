# 智能温度监测报警器

基于 **STM32F103C8T6** 的温度监测报警装置：实时采集环境温度并显示，支持按键设置报警阈值，超限本地声光报警，通过 **NRF24L01** 把温度和报警无线传送到电脑接收端，并把温度线性转换为 **0~3.3 V** 电压输出。

> 对应电子设计协会入会测试题《智能温度监测报警器》。设计与实施计划见 [planning.md](./planning.md)。

---

## 0. 重要警告：ESP8266 / WiFi 是错误道路

本项目**没有 ESP8266 模块**，STM32F103C8 也**没有 WiFi 能力**。

- ❌ 不要用 ESP8266 做热点、直连 WiFi、开网页服务器。
- ❌ 不要因为「电脑有 WiFi 网卡」就设想让单片机连 WiFi —— 单片机没有 WiFi 模块。
- ✅ 远程通信只用 **NRF24L01**：监测端发送 → 接收端（电脑 + NRF24L01 接收器）。

---

## 1. 功能

- **温度采集与显示**：DS18B20 采集环境温度，OLED 实时显示（单位 ℃），随温度变化更新
- **阈值设置与本地报警**：按键设置温度上、下限；实测温度越限时自动触发声光报警（蜂鸣器 + LED）
- **远程监控**：监测端经 NRF24L01 上报温度与报警；电脑接收端（Python 上位机）实时显示，报警时变红并提示
- **温度-电压转换**：温度线性转换为 0~3.3 V 电压输出（PWM + RC 滤波）

---

## 2. 硬件连接

### 2.1 系统架构与数据流

```mermaid
flowchart LR
    subgraph MONITOR[监测端]
        MCU[STM32F103C8T6]
        SENSOR[DS18B20<br/>PB10]
        OLED[OLED<br/>PB8/PB9]
        KEY[按键<br/>PB1/PB11]
        ALARM[蜂鸣器 PB0<br/>LED PA1/PA2]
        DAC[PWM 输出 PA6]
        TX[NRF24L01 发送<br/>SPI2]
        SENSOR --> MCU
        OLED --> MCU
        KEY --> MCU
        ALARM --> MCU
        DAC --> MCU
        MCU -->|SPI| TX
    end

    TX ==>|2.4G 无线| RX[NRF24L01 接收]

    subgraph PC[接收端（电脑）]
        direction TB
        RXMCU[接收端主控<br/>另一块开发板 或 无线串口模块]
        APP[Python 上位机<br/>temp_monitor.py]
        RX -->|SPI| RXMCU
        RXMCU -->|USB 串口| APP
    end
```

> ⚠️ **NRF24L01 没有 WiFi，也不是串口设备。** 它是 2.4G 射频收发芯片，模块用 **SPI** 与主控相连；电脑那头的接收器负责把无线数据转到 **USB 串口**，电脑通过串口读数据。

### 2.2 监测端接线（STM32）

| 外设 | 引脚 | 说明 |
| --- | --- | --- |
| DS18B20 DQ | PB10 | 1-Wire，需 4.7 kΩ 上拉电阻到 VCC |
| OLED SCL / SDA | PB8 / PB9 | 软件 I²C |
| 按键 Key1 / Key2 | PB1 / PB11 | 上拉输入，低电平有效 |
| 蜂鸣器 | PB0 | 有源蜂鸣器；低电平触发改 `BUZZER_ON_LEVEL` |
| LED1 / LED2 | PA1 / PA2 | 低电平点亮 |
| PWM 输出 | PA6 | → R10k → C100n → GND，输出 0~3.3 V |
| USART1 TX / RX | PA9 / PA10 | USB-TTL，本地设阈值/调试（可选） |

### 2.3 NRF24L01 接线（重点）

NRF24L01 模块（8 脚）与 STM32 的连线：

| NRF24L01 引脚 | STM32 引脚 | 说明 |
| --- | --- | --- |
| VCC | **3.3 V** | ⚠️ 必须 3.3 V，接 5 V 会烧模块 |
| GND | GND | 共地 |
| CE | PB6 | 收发使能（GPIO） |
| CSN | PB12 | SPI2 片选（GPIO） |
| SCK | PB13 | SPI2 时钟 |
| MOSI | PB15 | SPI2 主出从入 |
| MISO | PB14 | SPI2 主入从出 |
| IRQ | 不接 | 本驱动用状态寄存器轮询 |

> 模块必须用 **SPI** 接 STM32（PB12~PB15 + PB6）。如果你手上是以太网口（TX/RX）的「无线串口模块」，那是另一种硬件，接线和本工程代码不同，需要告诉我。

### 2.4 接收端（电脑）

接收端必须是「NRF24L01 + 能把数据转到串口的设备」，二选一：

| 方案 | 组成 | 说明 |
| --- | --- | --- |
| A. 另一块开发板 | 第二块 STM32/单片机 + NRF24L01，跑接收端固件，通过 USB 串口连电脑 | 需要接收端固件（可复用本工程，改为接收角色） |
| B. 成品无线串口模块 | 一对「NRF24L01 无线串口模块」（UART 接口），直接插电脑 USB 串口 | 需用配套工具把频道/地址配成与监测端一致 |

无论哪种方案，都要与监测端匹配：**频道 76（2476 MHz）、1 Mbps、地址 `0x12 34 56 78 9A`、负载 16 字节**。

电脑端运行 `PC_Software/temp_monitor.py` 读串口显示。

---

## 3. 目录结构

```
wenkong/
├── User/
│   ├── main.c              主循环 + 状态机 + 参数管理
│   ├── stm32f10x_it.c      中断服务（TIM2、USART1）
│   └── stm32f10x_conf.h    外设头文件配置
├── System/
│   ├── Delay.c/.h          SysTick 延时
│   └── Tick.c/.h           1 ms 定时基准（millis）
├── Hardware/
│   ├── LED.c/.h            报警灯 / 状态灯
│   ├── Key.c/.h            按键（含非阻塞扫描）
│   ├── OLED.c/.h           显示屏
│   ├── DS18B20.c/.h        温度采集
│   ├── Buzzer.c/.h         蜂鸣器
│   ├── USART.c/.h          串口收发（本地设置/调试）
│   ├── NRF24L01.c/.h       无线收发（SPI2）
│   └── DAC_Output.c/.h     PWM 电压输出
├── PC_Software/
│   └── temp_monitor.py     电脑接收端上位机（Python + tkinter）
├── planning.md             设计与实施计划
└── README.md
```

---

## 4. 构建与烧录

- **IDE**：Keil MDK 5（`E:\Keil_v5`，完整工具链在 E 盘），器件包 Keil.STM32F1xx_DFP 2.4.1
- **GUI**：打开 `Project.uvprojx` → Rebuild → Download
- **命令行**：

  ```powershell
  & "E:\Keil_v5\UV4\UV4.exe" -b Project.uvprojx -o build.log
  ```

  退出码 0 表示成功，日志写入 `build.log`（当前已验证：0 错误 0 警告）。

---

## 5. 使用说明

### 5.1 按键操作

| 按键 | 功能 |
| --- | --- |
| Key1（PB1） | 短按：切换设置项（正常 → 下限 → 上限 → 正常） |
| Key2（PB11） | 短按：当前设置项 +1；长按（≥0.8 s）：−1 |

### 5.2 本地串口（可选）

USB-TTL 接 PA9/PA10（115200），发送 `TL=20` 设下限、`TH=60` 设上限。

### 5.3 电脑接收端（NRF24L01 → 串口）

1. NRF24L01 接收器通过 USB 转串口接入电脑（记住 COM 口）
2. 安装依赖：`pip install pyserial`
3. 运行上位机：`python PC_Software/temp_monitor.py`
4. 选择对应串口与波特率 → 连接
5. 页面实时显示温度、阈值、输出电压；报警时变红并发出提示音

---

## 6. 验收演示

| 项目 | 演示方法 |
| --- | --- |
| B1 温度采集显示 | OLED 显示实时温度（℃），变化更新 |
| B2 阈值设置与本地报警 | 按键设阈值，加热/手捏 DS18B20 使温度越限 → 蜂鸣器 + LED；说明触发条件（T>上限 或 T<下限）与解除条件（回到回差区间） |
| B3 远程报警与传输 | NRF24L01 接收器接电脑，上位机显示温度；报警时界面变红并提示 |
| B4 程序与答辩 | 分层模块 + 状态机 + 注释 |
| E1 固定量程 | 0 ℃ → 0 V，60 ℃ → 3.3 V，30 ℃ → 1.65 V |

---

## 7. 备注

- **E2 扩展**：当前硬件输出 0~3.3 V；如需 0~10 V，在 PA6 与输出端之间加同相放大电路（增益≈3.03，12 V 供电），软件已预留可配置映射接口 `DAC_Output_SetMap()`
- **NRF24L01 参数**：频道 76（2476 MHz），1 Mbps，0 dBm，地址 `{0x12,0x34,0x56,0x78,0x9A}`，负载 16 字节；接收端模块需与之匹配并把负载原样转发到串口
- **DS18B20 时序**：1-Wire 对时序敏感，关键时序使用 `Delay_us`
