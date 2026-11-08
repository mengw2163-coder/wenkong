# wenkong — STM32F103 Keil MDK 工程

## 项目概述

- 目标芯片：STM32F103C8T6（Cortex-M3，72MHz，64KB Flash / 20KB RAM）
- 开发环境：Keil MDK 5（`D:\Keil_v5`），ARMCC v5.06，器件包 Keil.STM32F1xx_DFP 2.4.1
- 固件库：STM32 标准外设库 V3.5.0（宏 `USE_STDPERIPH_DRIVER`）
- 工程文件：`Project.uvprojx`，单一 Target（Target 1）

## 目录结构

| 目录 | 内容 |
| --- | --- |
| `Start/` | 启动文件 `startup_stm32f10x_md.s`、`core_cm3`、`system_stm32f10x`、`stm32f10x.h` |
| `Library/` | STM32F10x 标准外设库 V3.5.0 |
| `System/` | `Delay.c/h` — 基于 SysTick 的 us/ms/s 延时 |
| `Hardware/` | `LED.c/h`（PA1、PA2）、`Key.c/h`（PB1、PB11）、`OLED.c/h` + `OLED_Font.h`（PB8/PB9 软件 I2C） |
| `User/` | `main.c`、`stm32f10x_conf.h`、`stm32f10x_it.c/h` |
| `Objects/`、`Listings/`、`DebugConfig/` | 构建产物，不纳入版本管理 |

## 构建与调试

- GUI：Keil 打开 `Project.uvprojx` → Rebuild → Download（Flash 驱动 `STM32F10x_128`，SWD）
- 命令行构建：

  ```powershell
  & "D:\Keil_v5\UV4\UV4.exe" -b Project.uvprojx -o build.log
  ```

  退出码 0 表示成功，日志写入 `build.log`。
- 构建产物：`Objects/Project.axf`

## 代码约定

- 每个外设驱动独立成对文件放在 `Hardware/`，命名 `Xxx.c` / `Xxx.h`。
- 驱动接口风格：`Xxx_Init()`、`Xxx_ON()` / `Xxx_OFF()` / `Xxx_Turn()`（江协科技教程风格）。
- 注释使用中文，函数头采用 Doxygen 风格块注释。
- 新增外设驱动后需在 `Project.uvprojx` 的 `Hardware` 组中登记文件。

## 已知问题

- **工程当前无法编译通过**：`Project.uvprojx` 中有一个额外的 `library` 组，引用 `E:\STM32Project\2-1STM32工程\library\` 下的同名标准外设库，与本地 `Library/` 组重复编译，链接时报 467 个 `L6200E: Symbol ... multiply defined` 错误。
  - 修复：从工程中移除该 `library` 组（保留本地 `Library/` 组），或在 Keil 中删除该组。修改前先备份 `Project.uvprojx`。
- `Objects/` 中残留 `ds18b20.o` 等历史产物，说明该工程早期版本曾包含 DS18B20 驱动，现已不在工程文件中。

## 注意事项

- 修改 `Project.uvprojx` 前先备份，保持 XML 结构完整（该文件由 Keil 管理）。
- 不要同时保留两份标准外设库。
