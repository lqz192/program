# STM32F103C8T6 双气压传感器代码编写指南

适用于工作区的 LQFP48 **STM32F103C8T6** 工程。I²C1 接 LPS22HB，I²C2 接 MS5837，USART1 单独输出两组读数，USART2 作为节点 3 响应 F767。两个传感器均输出绝对气压，统一用整数 **Pa** 和 **0.01 °C**，这样 CMake 工程可直接用整数格式化。

## 1. 现状、复用位置与修改顺序

- C8T6 已有 HAL 初始化和 CMake 工程。现有 I²C 时钟是 100 kHz，两串口都是 115200 bit/s，USART2 尚未生成 IRQ。如果直接复制旧节点代码，接收回调无法工作。先按第 3 节打开 USART2 中断。
- C8T6 已有 `User/App`、`User/Drivers`、`User/Bsp` 和 `User/Common`。当前 `User/App/app_lps22hb.c/.h` 放的是驱动代码，且地址变量 `adress/address` 拼写不一致。先按第 4 节把驱动放回 Drivers，再用第 6 节的内容替换应用文件，避免重复定义和编译失败。
- 两路应用沿用 `STM32F103ZET61/User/App/app_imu.c` 的初始化、按周期采样、失败后清缓存和定时重试流程。I²C 地址沿用 `STM32F103ZET61/User/Drivers/mpu6050.c` 的传参方式，由应用给出 7 位地址，驱动调用 HAL 时左移。
- 复用 `STM32F103ZET62/User/Bsp/debug_uart.c/.h` 的 `send_line()`、`User/App/app_node_link.c/.h` 的缓存及 UART 回调，以及 `User/Comm/link_uart.c/.h` 的协议代码。C8T6 的通信文件位于已有的 `User/Common`。两路应用都直接调用这些接口。
- F767 当前只有节点 1 和 2，第三块板即使接线正确也不会被轮询。先单独验证 C8T6，再按第 8 节扩展 F767。
- 按第 2、3 节核对硬件并生成代码，第 4、5 节填写传感器驱动，第 6 节复用通信和调试模块并填写应用，第 7 节接入入口和编译，最后按第 8、9 节联调。

## 2. 按实物丝印接线

| 模块 | 实物标记 | 连接 |
| --- | --- | --- |
| 红色 LPS22HB | 主排针依次为 GND、SCL、SDA、VCC | GND → GND，SCL → PB6，SDA → PB7，VCC → 3.3 V |
| 红色 LPS22HB | 另一排 SDO、CS、INT（照片上近似 NT） | CS 保持高电平以选 I²C。SDO/SA0 为低时地址 0x5C，为高时 0x5D。INT 悬空 |
| 黑色 MS5837 | 沿排针从 VIN 一端起为 VIN、GND、SCL、SDA | GND → GND，SCL → PB10，SDA → PB11。核对模块资料后用 VIN 接 3.3 V 起步，7 位地址 0x76 |
| USB–TTL | RXD、GND | RXD ← C8T6 PA9，GND 共地。只显示时 PA10 留空 |
| F767 新链路 | USART3 TX/RX 和 GND | PD8 TX → C8T6 PA3 RX，PD9 RX ← C8T6 PA2 TX，GND 共地 |

I²C 两路各自需要 SCL/SDA 上拉。短线 100 kHz 可在**缺少板载上拉**时每线加一只约 4.7 kΩ 到 3.3 V。先查板背或原理图，确认模块的上拉电源、CS 和 SDO 的板载绑定位，再决定是否加电阻或跳线。避免和硬绑电平相冲。黑板的 VIN 丝印仅能确认供电入口，实物的稳压和电平转换仍待核对。上电前确认两路 I²C 信号属于 3.3 V 逻辑。USB–TTL 和板间线使用 3.3 V TTL，板间各自供电。 [LPS22HB 数据手册](https://www.st.com/resource/en/datasheet/lps22hb.pdf)

**MS5837 实物已确认为 02BA**。驱动直接使用 02BA 补偿。

## 3. 在 CubeMX 中配置 C8T6

使用 **STM32CubeMX 6.18.1、STM32Cube FW_F1 V1.8.7**。通过 `Help → Manage embedded software packages` 确认已经安装。 [CubeMX 6.18.1 手册](https://dev.st.com/stm32cube-docs/stm32cubemx/6.18.1/en/global_toc.html)

1. `File → Open Project` 打开 `E:/desk/program/STM32F103C8T6/STM32F103C8T6.ioc`。核对器件 STM32F103C8T6、LQFP48。若从空工程重建，走 `File → New Project → MCU/MPU Selector`，按完整型号搜索。
2. `Pinout & Configuration → System Core → SYS` 选 `Debug = Serial Wire`、`Timebase Source = SysTick`，确认 PA13/SWDIO、PA14/SWCLK。`System Core → RCC` 保持 HSE、LSE 禁用。`Clock Configuration` 选 HSI 8 MHz 作为 SYSCLK，AHB、APB1、APB2 分频均为 /1，HCLK、PCLK1、PCLK2 均为 8 MHz，PLL 关闭。
3. `Connectivity → I2C1/I2C2` 均选 `I2C` 模式。`Parameter Settings` 各设 `Standard Mode`、`100000 Hz`、`7-bit` 寻址，时钟延展允许，双地址和通用呼叫禁用。保持 I²C1 默认映射 PB6/SCL、PB7/SDA，I²C2 为 PB10/SCL、PB11/SDA。`GPIO Settings` 核对复用开漏，使用第 2 节的外部上拉。两路 I²C 的 DMA、事件中断和错误中断均不启用。
4. `Connectivity → USART1/USART2` 均选 `Asynchronous`。`Parameter Settings` 设 `115200 bit/s、8 Bits、None、1 Stop Bit、Receive and Transmit、无硬件流控`。核对 USART1 PA9/TX、PA10/RX，USART2 PA2/TX、PA3/RX。两路 DMA 均留空，USART1 不开中断。
5. `USART2 → NVIC Settings` 勾 `USART2 global interrupt`，抢占优先级 5，子优先级 0。`System Core → NVIC` 核对 USART2 IRQ 已启用。Middleware 保持空。
6. `Project Manager → Project` 核对项目名 STM32F103C8T6，工具链 `CMake`、编译器 `GCC`、固件包版本 V1.8.7。`Project Manager → Code Generator` 勾 `Generate peripheral initialization as a pair of .c/.h files per peripheral` 和 `Keep User Code when re-generating`。保留生成的 0x200 heap、0x400 stack，使用现有链接脚本。保存 .ioc 后点 `GENERATE CODE`。
7. 检查 `Core/Src/i2c.c` 两个 `Init.ClockSpeed` 都为 100000，`Core/Src/usart.c` 两个 `Init.BaudRate` 都为 115200 且 USART2 MSP 启用 NVIC。检查 `Core/Src/stm32f1xx_it.c` 的 `USART2_IRQHandler()` 调用 `HAL_UART_IRQHandler(&huart2)`。`Core/Src/main.c` 应先调用 GPIO、I²C1、I²C2、USART1、USART2 初始化，再进入 `USER CODE BEGIN 2`。

以下第 4–7 节的路径均相对 `E:/desk/program/STM32F103C8T6`。新建用户文件和修改顶层 CMake 后，重生成一次并复查 `USER CODE` 与 CMake 条目。

## 4. 将 LPS22HB 驱动归位

先把 `User/Drivers/lsp22hb.c/.h` 的文件名改为 `lps22hb.c/.h`，用下面的完整内容替换这两个驱动文件。第 6 节再替换当前误放驱动的 `User/App/app_lps22hb.c/.h`。接线时核对 SDO，按实际绑位设置应用的 `device_addr`。驱动用 WHO_AM_I = 0xB1 确认器件，25 Hz 连续采样，启用 BDU。按手册建议，在不使用 FIFO 时逐字节读五个输出寄存器。采集函数无数据就绪时返回 false，应用在下个采样周期再调用。HAL I²C 地址参数传入左移一位后的地址。 [ST LPS22HB 数据手册](https://www.st.com/resource/en/datasheet/lps22hb.pdf)

`User/Drivers/lps22hb.h`

```c
#ifndef LPS22HB_H  // 防止重复包含。
#define LPS22HB_H  // 声明头文件保护。
#include "main.h"  // 引入 HAL I²C 句柄。
#include <stdbool.h>  // 提供布尔返回值。
#include <stdint.h>  // 提供固定宽度整数。
bool lps22hb_init(I2C_HandleTypeDef *bus, uint8_t device_addr);  // 检查器件并启动连续采样。
bool lps22hb_read(I2C_HandleTypeDef *bus, uint8_t device_addr, int32_t *pressure_pa, int32_t *temperature_centi);  // 返回一对完整读数。
#endif  // 结束头文件保护。
```

`User/Drivers/lps22hb.c`

```c
#include "lps22hb.h"  // 引入本驱动接口。
static const uint32_t timeout_ms = 5U;  // HAL 单次寄存器事务的超时参数。
bool lps22hb_init(I2C_HandleTypeDef *bus, uint8_t device_addr)  // 核对器件并启动连续采样。
{  // device_addr 由应用按 SDO 绑位提供。
    uint16_t hal_addr = (uint16_t)(device_addr << 1U);  // 转为 STM32 HAL 地址。
    uint8_t id;  // 接收 WHO_AM_I。
    if (HAL_I2C_Mem_Read(bus, hal_addr, 0x0FU, I2C_MEMADD_SIZE_8BIT, &id, 1U, timeout_ms) != HAL_OK || id != 0xB1U)  // 确认器件响应及型号。
    {  // 地址、接线或器件型号有问题。
        return false;  // 应用稍后重试初始化。
    }  // 器件已确认。
    uint8_t ctrl2 = 0U;  // 关闭 FIFO 和地址自增，保留 I²C。
    uint8_t ctrl1 = 0x32U;  // 25 Hz 连续转换，启用 BDU。
    return HAL_I2C_Mem_Write(bus, hal_addr, 0x11U, I2C_MEMADD_SIZE_8BIT, &ctrl2, 1U, timeout_ms) == HAL_OK &&  // 先写 CTRL_REG2。
           HAL_I2C_Mem_Write(bus, hal_addr, 0x10U, I2C_MEMADD_SIZE_8BIT, &ctrl1, 1U, timeout_ms) == HAL_OK;  // 再启动连续采样。
}  // 初始化完成。
bool lps22hb_read(I2C_HandleTypeDef *bus, uint8_t device_addr, int32_t *pressure_pa, int32_t *temperature_centi)  // 输出单位为 Pa 和 0.01 °C。
{  // 先检查压力与温度的数据就绪位。
    uint16_t hal_addr = (uint16_t)(device_addr << 1U);  // 转为 STM32 HAL 地址。
    uint8_t status;  // 存放 STATUS 寄存器。
    if (HAL_I2C_Mem_Read(bus, hal_addr, 0x27U, I2C_MEMADD_SIZE_8BIT, &status, 1U, timeout_ms) != HAL_OK || (status & 3U) != 3U)  // 两组值都应已更新。
    {  // I²C 出错或输出尚未就绪。
        return false;  // 保留输出参数。
    }  // 开始读取五个输出字节。
    uint8_t data[5];  // 前三字节压力，后两字节温度。
    for (uint16_t index = 0U; index < sizeof(data); ++index)  // 按寄存器地址顺序读。
    {  // 此次事务只读取一个字节。
        if (HAL_I2C_Mem_Read(bus, hal_addr, (uint16_t)(0x28U + index), I2C_MEMADD_SIZE_8BIT, &data[index], 1U, timeout_ms) != HAL_OK)  // 读取当前字节。
        {  // 任意字节失败就放弃整对读数。
            return false;  // 交由应用更新无效标记。
        }  // 当前字节有效。
    }  // 全部字节就绪。
    int32_t p_raw = data[0] | ((int32_t)data[1] << 8U) | ((int32_t)data[2] << 16U);  // 拼接小端 24 位压力码。
    int32_t t_raw = data[3] | ((int32_t)data[4] << 8U);  // 拼接小端 16 位温度码。
    if ((p_raw & 0x800000) != 0)  // 判断压力符号位。
    {  // 将 24 位二补码扩展为有符号整数。
        p_raw -= 0x1000000;  // 减去 2^24 得到原始负值。
    }  // 压力符号转换完成。
    if ((t_raw & 0x8000) != 0)  // 判断温度符号位。
    {  // 将 16 位二补码扩展为有符号整数。
        t_raw -= 0x10000;  // 减去 2^16 得到原始负温。
    }  // 温度符号转换完成。
    *pressure_pa = (p_raw * 100) / 4096;  // 4096 LSB/hPa，1 hPa = 100 Pa。
    *temperature_centi = t_raw;  // 100 LSB/°C，直接输出百分之一摄氏度。
    return true;  // 只有完整读数才报告成功。
}  // 结束读取。
```

## 5. 填写 MS5837 驱动

本工程使用 MS5837-02BA。驱动先读取并校验 PROM，再按 02BA 公式补偿压力和温度。应用通过 I²C2 调用驱动。

### 5.1 替换 `User/Drivers/ms5837.h`：完整内容

```c
#ifndef MS5837_H /* 防止重复包含本头文件。 */
#define MS5837_H /* 定义头文件包含标志。 */

#include "main.h" /* 使用当前芯片工程提供的 HAL 类型和 I2C 接口。 */
#include <stdbool.h> /* 提供 bool、true 和 false。 */
#include <stdint.h> /* 提供固定宽度整数类型。 */

/* MS5837-02BA 驱动。 */
bool ms5837_init(I2C_HandleTypeDef *bus); /* 复位并读取和校验 PROM；成功返回 true。 */
bool ms5837_read(I2C_HandleTypeDef *bus, int32_t *pressure_pa, int32_t *temperature_centi); /* 成功输出绝对压力 Pa 和百分之一摄氏度。 */

#endif /* 结束头文件保护。 */
```

### 5.2 替换 `User/Drivers/ms5837.c`：完整内容

OSR 固定为 1024，D1/D2 指令分别为 `0x44/0x54`，每次转换等待 4 ms，覆盖 2.17 ms 最大转换时间并留出 HAL tick 余量。每笔 HAL 传输超时 10 ms；一次正常读取包含两次转换、六笔传输，约 10 ms，故障时 HAL 的 BUSY 等待可能额外延长耗时。HAL tick 必须正常运行，否则 `HAL_Delay()` 和 HAL 超时机制不具备这一时间界限。

```c
#include "ms5837.h" /* 引入驱动接口及工程 HAL 定义。 */

#define MS5837_ADDR (0x76U << 1) /* STM32 HAL 使用左移一位后的 7 位 I2C 地址。 */
#define MS5837_TIMEOUT_MS 10U /* 限制单笔 I2C 事务等待时间，单位 ms。 */
static uint16_t calibration[8]; /* 保存 PROM 的 0～6 字及 CRC 计算所需的第 7 字。 */

static bool send_command(I2C_HandleTypeDef *bus, uint8_t command) /* 发送一个器件命令。 */
{ /* 命令发送函数开始。 */
    return HAL_I2C_Master_Transmit(bus, MS5837_ADDR, &command, 1U, MS5837_TIMEOUT_MS) == HAL_OK; /* 将 HAL 状态转换为成功标志。 */
} /* 命令发送函数结束。 */

static uint8_t prom_crc4(const uint16_t *prom) /* 计算 MS5837 PROM 的 CRC4，不修改原始 PROM。 */
{ /* CRC 计算函数开始。 */
    uint16_t remainder = 0U; /* 清空 CRC 余数。 */
    for (uint8_t index = 0U; index < 16U; ++index) /* 按高字节在前处理 8 个 16 位字。 */
    { /* 字节循环开始。 */
        uint16_t word = prom[index / 2U]; /* 取得当前字节所属的 PROM 字。 */
        if (index < 2U) { word &= 0x0FFFU; } /* 清除第 0 字中存储的 CRC 高四位。 */
        if (index >= 14U) { word = 0U; } /* 按厂商算法将第 7 字视为零。 */
        remainder ^= (index & 1U) ? (word & 0x00FFU) : (word >> 8); /* 将当前字节混入余数。 */
        for (uint8_t bit = 0U; bit < 8U; ++bit) /* 每个输入字节执行八次多项式运算。 */
        { /* 位循环开始。 */
            remainder = (uint16_t)((remainder & 0x8000U) ? ((remainder << 1) ^ 0x3000U) : (remainder << 1)); /* 用 0x3000 多项式更新 16 位余数。 */
        } /* 位循环结束。 */
    } /* 字节循环结束。 */
    return (uint8_t)((remainder >> 12) & 0x0FU); /* 返回高四位 CRC。 */
} /* CRC 计算函数结束。 */

bool ms5837_init(I2C_HandleTypeDef *bus) /* 初始化一个 MS5837。 */
{ /* 初始化函数开始。 */
    if (!send_command(bus, 0x1EU)) { return false; } /* 发送复位命令并处理通信失败。 */
    HAL_Delay(10U); /* 等待复位及 PROM 装载完成。 */
    for (uint8_t index = 0U; index < 7U; ++index) /* 读取 PROM 第 0～6 字。 */
    { /* PROM 读取循环开始。 */
        uint8_t data[2]; /* 接收当前校准字的高低字节。 */
        if (!send_command(bus, (uint8_t)(0xA0U + index * 2U))) { return false; } /* 指定 PROM 字地址。 */
        if (HAL_I2C_Master_Receive(bus, MS5837_ADDR, data, 2U, MS5837_TIMEOUT_MS) != HAL_OK) { return false; } /* 有界接收校准字。 */
        calibration[index] = (uint16_t)(((uint16_t)data[0] << 8) | data[1]); /* 按大端字节序组装校准值。 */
    } /* PROM 读取循环结束。 */
    calibration[7] = 0U; /* 初始化 CRC 算法的补零字。 */
    uint16_t coefficient_or = 0U; /* 检测全零校准区，避免把全零误认为有效 CRC。 */
    for (uint8_t index = 1U; index <= 6U; ++index) /* 仅检查六个实际校准系数。 */
    { /* 校准区检查开始。 */
        coefficient_or |= calibration[index]; /* 汇总出现的有效位。 */
    } /* 校准区检查结束。 */
    return coefficient_or != 0U && prom_crc4(calibration) == (uint8_t)(calibration[0] >> 12); /* 拒绝全零校准系数并核对 CRC。 */
} /* 初始化函数结束。 */

static bool read_adc(I2C_HandleTypeDef *bus, uint8_t command, uint32_t *value) /* 启动一次 OSR1024 转换并取回 ADC。 */
{ /* ADC 读取函数开始。 */
    uint8_t data[3]; /* 保存三字节 ADC 结果。 */
    if (!send_command(bus, command)) { return false; } /* 启动压力或温度转换。 */
    HAL_Delay(4U); /* 等待转换完成，包含时钟粒度余量。 */
    if (!send_command(bus, 0x00U)) { return false; } /* 发出 ADC 读取命令。 */
    if (HAL_I2C_Master_Receive(bus, MS5837_ADDR, data, 3U, MS5837_TIMEOUT_MS) != HAL_OK) { return false; } /* 有界接收 ADC 字节。 */
    *value = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2]; /* 组装无符号 24 位 ADC 值。 */
    return *value != 0U; /* 转换未完成或 ADC 无结果会返回零。 */
} /* ADC 读取函数结束。 */

bool ms5837_read(I2C_HandleTypeDef *bus, int32_t *pressure_pa, int32_t *temperature_centi) /* 读取并补偿压力与温度。 */
{ /* 传感器读取函数开始。 */
    uint32_t d1; /* 保存压力 ADC 结果。 */
    uint32_t d2; /* 保存温度 ADC 结果。 */
    if (!read_adc(bus, 0x44U, &d1) || !read_adc(bus, 0x54U, &d2)) { return false; } /* 顺序完成压力及温度转换。 */
    int64_t delta_t = (int64_t)d2 - (int64_t)calibration[5] * 256LL; /* 用有符号 64 位计算温度差，防止无符号下溢。 */
    int64_t temperature = 2000LL + delta_t * calibration[6] / 8388608LL; /* 计算一阶温度，单位 0.01℃。 */
    int64_t sensitivity = (int64_t)calibration[1] * 65536LL + (int64_t)calibration[3] * delta_t / 128LL; /* 02BA 一阶灵敏度。 */
    int64_t offset = (int64_t)calibration[2] * 131072LL + (int64_t)calibration[4] * delta_t / 64LL; /* 02BA 一阶偏移。 */

    if (temperature < 2000LL) /* 低于 20 °C 时应用二阶补偿。 */
    {  // 进入低温补偿分支。
        int64_t temperature_delta = temperature - 2000LL;  // 计算与 20 °C 的温差。
        int64_t temperature_square = temperature_delta * temperature_delta;  // 计算温差平方，供偏移和灵敏度补偿共用。
        temperature -= 11LL * delta_t * delta_t / 34359738368LL;  // 应用温度补偿。
        offset -= 31LL * temperature_square / 8LL;  // 应用偏移补偿。
        sensitivity -= 63LL * temperature_square / 32LL;  // 应用灵敏度补偿。
    }  // 完成低温补偿。

    *pressure_pa = (int32_t)(((int64_t)d1 * sensitivity / 2097152LL - offset) / 32768LL);  // 输出绝对气压，单位 Pa。
    *temperature_centi = (int32_t)temperature;  // 输出温度，单位 0.01 °C。
    return true; /* 本次采样成功，输出值有效。 */
} /* 传感器读取函数结束。 */
```

### 5.3 验证与使用约束

先初始化 I2C 和 HAL tick，再调用 `ms5837_init(&hi2c2)`，对应本项目的 I²C2。初始化失败不得进入正常采样；读取失败时两个输出保持旧值，调用方必须根据返回值标记本帧无效，不能把旧值当新数据。MS5837 输出绝对压力，水深需由应用层使用当时空气压力基准和实际液体密度另行换算。

上板先在空气中观察压力与现场气压是否相符、温度是否合理；拔掉传感器应有限时返回失败，重新连接后需重新初始化。用逻辑分析仪核对 7 位地址 `0x76`、D1 `0x44`、D2 `0x54`、ADC `0x00` 以及转换间隔。用 TE 02BA 手册示例校准系数与 ADC 原始值验证时，应得到 110,002 Pa 和 20.00 °C。 [TE 02BA 数据手册](https://www.amsys-sensor.com/downloads/data/MS5837-02BA01-AMSYS-datasheet.pdf#page=7)

算法核对来源：[Blue Robotics 官方 MS5837 驱动源码](https://github.com/bluerobotics/BlueRobotics_MS5837_Library/blob/master/src/MS5837.cpp)。本实现采用独立 C/HAL 结构，核对了 PROM CRC4、02BA 的缩放及二阶公式；温度分支直接比较百分之一摄氏度，所有平方项先提升为 64 位。

## 6. 复用通信模块，分别新建两套采集应用

用下表的 ZET62 文件内容替换 C8T6 的对应占位文件。通信文件保留 C8T6 现有的 `Common` 目录。三个模块的公开接口和实现直接沿用，`app_node_link.c` 负责 C8T6 的 USART2 HAL 回调。

| ZET62 来源 | C8T6 目标 | 复用内容 |
| --- | --- | --- |
| `User/Comm/link_uart.c/.h` | `User/Common/link_uart.c/.h` | 收发、行缓冲、帧头解析 |
| `User/App/app_node_link.c/.h` | `User/App/app_node_link.c/.h` | USART2 中断接收及两槽缓存 |
| `User/Bsp/debug_uart.c/.h` | `User/Bsp/debug_uart.c/.h` | `send_line()` 输出到 USART1 |

正常采样时，USART1 每路约 200 ms 输出一行。USART2 收到 `PING,3,17` 时回复 `PONG,3,17`，收到 `GET,3,18` 时回复 `DATA,3,18,LPS22HB p_pa=101325 t_centi=2350|MS5837 p_pa=101290 t_centi=2410`。帧以 LF 结束，接收端也接受 CRLF。USART1 各行以 CRLF 结束。缓存槽 1 固定为 LPS22HB，槽 2 固定为 MS5837。某槽初始化失败、读失败或缓存超过 500 ms 时，协议相应位置为 `NA`。F767 的 `TIMEOUT` 则表示请求没有收到有效响应。

两套应用各自保留一个就绪状态和一个时间戳。沿用 MPU6050 的节拍写法，正常时每 200 ms 采样，失败时每 1000 ms 重试。先调用各自的初始化入口，再反复调用各自的运行入口。某一路发生故障时，只清除该路缓存并重新初始化该传感器。采样在主循环执行，USART2 中断可以继续接收，主循环在两次采样之间处理收到的请求。

### LPS22HB 应用

替换 `User/App/app_lps22hb.h`。公开主循环入口。

```c
#ifndef APP_LPS22HB_H  // 防止重复包含。
#define APP_LPS22HB_H  // 定义头文件保护。
void app_lps22hb_init(void);  // 在 HAL 外设初始化后启动本传感器。
void app_lps22hb_run(void);  // 由主循环调用，内部控制采样节拍。
#endif  // 结束头文件保护。
```

替换 `User/App/app_lps22hb.c`。管理 LPS22HB 的初始化、采样、USART1 显示及第一槽缓存。

```c
#include "app_lps22hb.h"  // 引入本应用接口。
#include "app_node_link.h"  // 更新 USART2 的采样缓存。
#include "debug_uart.h"  // 通过 USART1 显示本地读数。
#include "i2c.h"  // 引入 hi2c1 句柄。
#include "lps22hb.h"  // 引入 LPS22HB 驱动。
#include <stdio.h>  // 使用 snprintf 格式化读数。
static const uint8_t device_addr = 0x5CU;  // SDO 接低用 0x5C，接高改为 0x5D。
static bool ready;  // 记录本传感器的初始化状态。
static uint32_t last_action_ms;  // 上次初始化结束或开始读数的时刻。
void app_lps22hb_init(void)  // 开机和故障重试共用此入口。
{  // 配置本应用对应的传感器。
    ready = lps22hb_init(&hi2c1, device_addr);  // 调用该传感器驱动。
    if (!ready)  // 初始化失败时报告具体通道。
    {  // USART1 显示初始化错误。
        send_line("LPS22HB INIT_ERR\r\n");  // 留下本次重试的结果。
    }  // 初始化结果已处理。
    last_action_ms = HAL_GetTick();  // 从初始化结束起计算下一个周期。
}  // 初始化完成。
void app_lps22hb_run(void)  // 在主循环执行到期的采样或重试。
{  // 复用 MPU6050 应用的计时方式。
    uint32_t now_ms = HAL_GetTick();  // 取当前毫秒数。
    if (now_ms - last_action_ms < (ready ? 200U : 1000U))  // 正常采样与故障重试使用各自的间隔。
    {  // 时间未到。
        return;  // 让主循环继续处理其他任务。
    }  // 本轮操作到期。
    if (!ready)  // 开机初始化失败或上轮读数失败。
    {  // 沿用同一初始化入口。
        app_lps22hb_init();  // 重新配置本传感器。
        return;  // 下个周期再读取。
    }  // 本传感器已就绪。
    last_action_ms = now_ms;  // 记录本轮读数开始时间。
    int32_t pressure_pa;  // 存放本轮绝对气压，单位 Pa。
    int32_t temperature_centi;  // 存放本轮温度，单位 0.01 °C。
    if (!lps22hb_read(&hi2c1, device_addr, &pressure_pa, &temperature_centi))  // 读完整的气压和温度。
    {  // 本轮读数失败。
        ready = false;  // 下个周期重新初始化本传感器。
        app_node_sample_update(node_first, NULL);  // 立即撤销本槽的有效值。
        send_line("LPS22HB READ_ERR\r\n");  // 在 USART1 标记本轮读数失败。
        return;  // 返回主循环。
    }  // 本轮两项读数均有效。
    char line[80];  // 保存记录文本及换行。
    int length = snprintf(line, sizeof(line), "LPS22HB p_pa=%ld t_centi=%ld\r\n", (long)pressure_pa, (long)temperature_centi);  // 格式化整数读数。
    if (length > 0 && (size_t)length < sizeof(line))  // 沿用旧采集应用的完整行检查。
    {  // 两个输出口使用同一条文本。
        app_node_sample_update(node_first, line);  // 发布本传感器最新数据。
        send_line(line);  // 通过 USART1 输出。
    }  // 完成本地显示。
}  // 本传感器采样任务结束。
```

### MS5837 应用

替换 `User/App/app_ms5837.h`。公开主循环入口。

```c
#ifndef APP_MS5837_H  // 防止重复包含。
#define APP_MS5837_H  // 定义头文件保护。
void app_ms5837_init(void);  // 在 HAL 外设初始化后启动本传感器。
void app_ms5837_run(void);  // 由主循环调用，内部控制采样节拍。
#endif  // 结束头文件保护。
```

替换 `User/App/app_ms5837.c`。管理 MS5837 的初始化、采样、USART1 显示及第二槽缓存。

```c
#include "app_ms5837.h"  // 引入本应用接口。
#include "app_node_link.h"  // 更新 USART2 的采样缓存。
#include "debug_uart.h"  // 通过 USART1 显示本地读数。
#include "i2c.h"  // 引入 hi2c2 句柄。
#include "ms5837.h"  // 引入 MS5837 驱动。
#include <stdio.h>  // 使用 snprintf 格式化读数。
static bool ready;  // 记录本传感器的初始化状态。
static uint32_t last_action_ms;  // 上次初始化结束或开始读数的时刻。
void app_ms5837_init(void)  // 开机和故障重试共用此入口。
{  // 配置本应用对应的传感器。
    ready = ms5837_init(&hi2c2);  // 调用该传感器驱动。
    if (!ready)  // 初始化失败时报告具体通道。
    {  // USART1 显示初始化错误。
        send_line("MS5837 INIT_ERR\r\n");  // 留下本次重试的结果。
    }  // 初始化结果已处理。
    last_action_ms = HAL_GetTick();  // 从初始化结束起计算下一个周期。
}  // 初始化完成。
void app_ms5837_run(void)  // 在主循环执行到期的采样或重试。
{  // 复用 MPU6050 应用的计时方式。
    uint32_t now_ms = HAL_GetTick();  // 取当前毫秒数。
    if (now_ms - last_action_ms < (ready ? 200U : 1000U))  // 正常采样与故障重试使用各自的间隔。
    {  // 时间未到。
        return;  // 让主循环继续处理其他任务。
    }  // 本轮操作到期。
    if (!ready)  // 开机初始化失败或上轮读数失败。
    {  // 沿用同一初始化入口。
        app_ms5837_init();  // 重新配置本传感器。
        return;  // 下个周期再读取。
    }  // 本传感器已就绪。
    last_action_ms = now_ms;  // 记录本轮读数开始时间。
    int32_t pressure_pa;  // 存放本轮绝对气压，单位 Pa。
    int32_t temperature_centi;  // 存放本轮温度，单位 0.01 °C。
    if (!ms5837_read(&hi2c2, &pressure_pa, &temperature_centi))  // 读完整的气压和温度。
    {  // 本轮读数失败。
        ready = false;  // 下个周期重新初始化本传感器。
        app_node_sample_update(node_second, NULL);  // 立即撤销本槽的有效值。
        send_line("MS5837 READ_ERR\r\n");  // 在 USART1 标记本轮读数失败。
        return;  // 返回主循环。
    }  // 本轮两项读数均有效。
    char line[80];  // 保存记录文本及换行。
    int length = snprintf(line, sizeof(line), "MS5837 p_pa=%ld t_centi=%ld\r\n", (long)pressure_pa, (long)temperature_centi);  // 格式化整数读数。
    if (length > 0 && (size_t)length < sizeof(line))  // 沿用旧采集应用的完整行检查。
    {  // 两个输出口使用同一条文本。
        app_node_sample_update(node_second, line);  // 发布本传感器最新数据。
        send_line(line);  // 通过 USART1 输出。
    }  // 完成本地显示。
}  // 本传感器采样任务结束。
```

## 7. 接入 C8T6 的入口和构建

修改 `Core/Src/main.c`。在各 `USER CODE` 区内新增以下代码。

`USER CODE BEGIN Includes`

```c
#include "app_lps22hb.h"  // 接入 I²C1 的 LPS22HB 应用。
#include "app_ms5837.h"  // 接入 I²C2 的 MS5837 应用。
#include "app_node_link.h"  // 接入板间串口节点。
```

`USER CODE BEGIN 2`

```c
app_lps22hb_init();  // 初始化 I²C1 的 LPS22HB。
app_ms5837_init();  // 初始化 I²C2 的 MS5837。
app_node_link_init(3U);  // 两路初始化结束后，以节点 3 开始接收。
```

`USER CODE BEGIN 3` 内（即 `while (1)` 的循环体内）。

```c
app_node_link_run();  // 响应缓存读取请求。
app_lps22hb_run();  // 到期时采集 LPS22HB，更新第一槽。
app_node_link_run();  // 处理 LPS22HB 事务期间收到的请求。
app_ms5837_run();  // 到期时采集 MS5837，更新第二槽。
```

修改顶层 `CMakeLists.txt`，分别在现有 `target_sources` 与 `target_include_directories` 块内添加条目。保留原有 `stm32cubemx` 链接。

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE  # 把用户代码加入 C8T6 固件。
    User/Drivers/lps22hb.c  # I²C1 气压计驱动。
    User/Drivers/ms5837.c  # I²C2 气压计驱动。
    User/App/app_lps22hb.c  # I²C1 采样、USART1 显示与第一槽缓存。
    User/App/app_ms5837.c  # I²C2 采样、USART1 显示与第二槽缓存。
    User/App/app_node_link.c  # 节点 3 协议处理。
    User/Comm/link_uart.c  # 串口收发与解析。
    User/Bsp/debug_uart.c  # USART1 文本输出。
)  # 结束用户源文件列表。
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE  # 添加用户头文件路径。
    User/Drivers  # 传感器接口。
    User/App  # 采样和节点接口。
    User/Comm  # 行协议接口。
    User/Bsp  # 调试串口接口。
)  # 结束头文件路径列表。
```

在项目目录执行已有 CMake 预设。CLion 中也可以重新加载 CMake 并构建 `Debug` 目标。

```powershell
cmake --preset Debug # 使用工程预设、Ninja 与 ARM GCC 配置。
cmake --build --preset Debug # 编译链接，输出 build/Debug/STM32F103C8T6.elf。
```

用 ST-LINK 接 SWDIO、SWCLK、GND 和目标参考电压。用 STM32CubeProgrammer 的 SWD 连接，打开该 ELF，下载并运行。先只接 USB–TTL 验收两路采样，再接 F767。

## 8. 在原 F767 工程上增加节点 3

本文选 F767 的 **USART3（PD8/PD9）** 作为第三条独立链路。根据手上挑战者 V1/V2 的原理图确认两脚在排针可用、与板载外设无冲突。PD8/PD9 还可作 FMC_D13/D14，如果底板有并联 SRAM 或已接扩展硬件，要先核对布线及片选状态。V2 还应核对 USART3 与 RS-232 相关跳帽，用 MCU 一侧的 TTL 引脚，隔离板载电平转换器。若实物需要换引脚，改用原理图确认的空闲 USART3 复用脚，以下 `huart3` 软件配置仍适用。 [挑战者 V1/V2 底板说明](https://doc.embedfire.com/stm32_products/must_read/zh/latest/doc/introduction_of_stm32/STM32/ebf_stm32f767_tiaozhanzhe_v1_v2/stm32f767_tiaozhanzhe_v1_v2.html) · [STM32F767IG 复用表](https://www.st.com/resource/en/datasheet/stm32f767ig.pdf)

1. CubeMX 6.18.1 中通过 `File → Open Project` 打开 `E:/desk/program/STM32F767_Hub/STM32F767_Hub.ioc`，核对 STM32F767IGT6/LQFP176 和 FW_F7 V1.17.4。原时钟用 HSI 16 MHz，AHB/APB1/APB2 分频 /1，项目使用 SWD 和 SysTick。
2. `Connectivity → USART3` 选 `Asynchronous`，引脚图手动确认 PD8/TX、PD9/RX，两脚复用 AF7。`Parameter Settings` 设 115200 bit/s、8 位、无校验、1 停止位、收发模式、无流控、16 倍采样。若时钟树显示 USART3 时钟复用，选 PCLK1。
3. `USART3 → NVIC Settings` 勾选 USART3 global interrupt，抢占优先级 5、子优先级 0。DMA 留空，无需中间件。核对 USART1 PA9/PA10、USART2 PD5/PD6 和 USART6 PC6/PC7 的原设置。`Project Manager` 选择原 CMake/GCC、分外设生成并保留用户代码，保存 .ioc 后生成。
4. 核对 `Core/Inc/usart.h` 声明 `huart3`，`Core/Src/usart.c` 有 USART3 初始化和 IRQ 使能，`Core/Src/stm32f7xx_it.c` 的 `USART3_IRQHandler()` 调用 `HAL_UART_IRQHandler(&huart3)`。`Core/Src/main.c` 中 `MX_USART3_UART_Init()` 必须先于原有的 `app_hub_init()`。

仅修改 `STM32F767_Hub/User/App/app_hub.c` 的以下五处。复用现有协议解析、轮询周期 100 ms 和 75 ms 应答超时。原构建目标已编译 `app_hub.c`，无需增加源文件。

1. 把文件顶部的 `nodes[2]` 声明替换为下方声明。

```c
static hub_node nodes[3];  // 分别保存节点 1、2、3 的 UART 和调度状态。
```

2. 在 `app_hub_init()` 里为 `nodes[1]` 启动接收后新增前三行，并用第四行替换紧随其后的循环头。复用原循环体。

```c
nodes[2].node = 3U;  // 目标是 C8T6 压力节点。
nodes[2].last_request_ms = now_ms - poll_period_ms / 4U;  // 第三路错开到约 75 ms 后启动。
nodes[2].enabled = link_uart_start(&nodes[2].link, &huart3);  // 在 USART3 启动字节接收。
for (unsigned index = 0U; index < sizeof(nodes) / sizeof(nodes[0]); ++index)  // 核对三路接收启动结果。
```

3. 在 `app_hub_run()` 的 `service_node(&nodes[1], HAL_GetTick());` 后新增一行。

```c
service_node(&nodes[2], HAL_GetTick());  // 处理 USART3 上的压力节点。
```

4. 在 `HAL_UART_RxCpltCallback()` 内的 `huart6` 分支后追加。

```c
else if (uart == &huart3)  // 第三路接收了一个字节。
{  // 交给该串口独立的接收缓存。
    link_uart_on_rx(&nodes[2].link);  // 记录字节并重新打开接收。
}  // 节点 3 接收分支结束。
```

5. 在 `HAL_UART_ErrorCallback()` 内的 `huart6` 分支后追加。

```c
else if (uart == &huart3)  // 第三路遇到 UART 错误。
{  // 只恢复 USART3 对应链路。
    link_uart_on_error(&nodes[2].link);  // 丢弃损坏的行并重启监听。
}  // 节点 3 错误分支结束。
```

保存后在 F767 工程目录用 `cmake --preset Debug`、`cmake --build --preset Debug` 构建，下载 `STM32F767_Hub.elf`。连接第 2 节三根板间线并打开 F767 的 USART1 对应串口，配置 115200 / 8N1，核对节点 1、2、3 均有回应。

## 9. 验收与排查

| 顺序 | 操作 | 预期与排查位置 |
| --- | --- | --- |
| 1 | 先接电源、地和 USB–TTL，打开 C8T6 USART1，115200 / 8N1 | 每约 200 ms 打印两行。初始化或读数失败会输出 INIT_ERR 或 READ_ERR。乱码先核对 TX→RX、共地、电平和时钟 |
| 2 | 在同一处测室内静态气压 | 两路绝对压应接近当地实测站压，海平面附近常在约 100000 Pa；海拔影响读数。t_centi=2350 表示 23.50 °C。芯片温度可能和室温有差异 |
| 3 | LPS22HB 一直 NA | 核对 PB6/PB7、上拉、CS/SDO 绑位，断点看 `lps22hb_init()` 返回与 WHO_AM_I。探测 7 位地址 0x5C/0x5D；逻辑分析仪可能用 8 位地址显示 |
| 4 | MS5837 一直 NA 或压力量级异常 | 核对 PB10/PB11 和 0x76，检查复位、PROM 读序列和 CRC。CRC 过而气压量级错误时，重点核对版本、补偿系数和 Pa 单位 |
| 5 | F767 接入前，用另一只 3.3 V USB–TTL 连 C8T6 USART2，发送 PING 与 GET，附加 LF | 应回正确节点号和序号。测试时移除 F767 TX，避免两个 TX 驱动 PA3。无响应查 NVIC、IRQ 和 HAL 回调 |
| 6 | 接 F767，观察三个节点，再断电后拔掉一个气压模块并重新上电 | 节点 3 按序号返回两槽，其中断开的槽为 NA，另一槽应继续更新；节点 1、2 仍按原链路上报 |
| 7 | 检查故障时的板间延迟 | 在 F767 TX 和 C8T6 TX 用逻辑分析仪测从请求末字节到应答末字节的时间，应小于 hub 的 75 ms。若传感器总线被持续拉低，查 HAL 阻塞点和实际用时，修正接线或恢复总线后再测 |
| 8 | 断电检查后重连模块，重新上电 | 检查初始化能够重试，两槽重新出现实际读数。记录最先失败的阶段和测得的时序，便于判断是供电、总线还是选型问题 |

