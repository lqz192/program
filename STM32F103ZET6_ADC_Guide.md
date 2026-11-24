# STM32F103ZET6 在 MPU6050 工程中新增片内 ADC 采集

适用于 `E:\desk\program\STM32F103ZET61`。在已有 MPU6050 采集基础上，增加 **PA1／ADC1_IN1** 模拟输入。两路共用 **USART1**，分别输出 `IMU_RAW` 和 `ADC raw`，各自约每 100 ms 输出一行。

当前工程已启用 PA1 的 ADC 功能，并生成了 ADC1 初始化代码。接线前确认开发板有引出 **PA1**，并确认板上没有与这次测量冲突的电路。PA1 支持 ADC 通道 1。[STM32F103ZE 数据手册，表 5](https://www.st.com/resource/en/datasheet/stm32f103ze.pdf)

## 文件分层

| 层级 | 文件 | 职责 |
|---|---|---|
| CubeMX 生成层 | `Core/Src/adc.c`、`Core/Inc/adc.h` | 初始化 PA1、ADC1 时钟和转换通道，提供 `hadc1`。 |
| ADC 驱动层 | `User/Drivers/adc_input.c`、`adc_input.h` | 校准 ADC1，执行一次转换，返回原始值和 HAL 状态。 |
| 应用层 | `User/App/app_adc.c`、`app_adc.h` | 将原始值换算为毫伏，使用现有 USART1 输出数据，按错误类型打印提示。 |
| 程序入口 | `Core/Src/main.c` | 完成外设初始化，启动 MPU6050 和 ADC 应用，并在主循环中依次调用两者。 |

ADC 的硬件句柄 `hadc1` 由驱动层使用。应用通过 `adc_input.h` 调用驱动，驱动通过返回值把故障交给应用处理。UART 日志集中在应用层的 `send_line()` 中。

## 1. 核对当前工程的配置

以下配置已经存在，可以从第 2 节继续。对应文件是 `STM32F103ZET61.ioc`、`Core/Src/adc.c` 和 `Core/Src/main.c`。

| 项目 | 当前配置 |
|---|---|
| ADC 输入 | `ADC1_IN1`，引脚 `PA1`，模拟模式 |
| 单次规则转换 | `Rank 1 → Channel 1`，转换数为 1 |
| 数据和触发 | 右对齐，`ADC_SOFTWARE_START`，扫描、连续、间断模式关闭 |
| 采样时间 | `ADC_SAMPLETIME_55CYCLES_5` |
| ADC 时钟 | PCLK2 为 8 MHz，经 `RCC_ADCPCLK2_DIV2` 分频后为 4 MHz |
| MPU6050 | 由现有 I2C1 和 `app_imu` 采集 |
| 串口 | `USART1`，PA9/TX、PA10/RX，115200、8N1 |

ADC 转换时钟由 CubeMX 配置，应用采集周期由 `app_adc_run()` 管理。4 MHz 在器件规定的 ADC 时钟范围内。[STM32F103ZE 数据手册，表 59](https://www.st.com/resource/en/datasheet/stm32f103ze.pdf)

## 2. ADC 驱动层

ADC 驱动使用 CubeMX 生成的 `hadc1`。应用传入有效的输出指针。驱动按 **启动 → 等待完成 → 读取 → 停止** 执行，等待超时后也调用停止函数，清理本轮转换。转换完成时写入原始值，应用在驱动最终返回 `HAL_OK` 时使用本次结果。[STM32F1 HAL ADC 驱动](https://github.com/STMicroelectronics/stm32f1xx-hal-driver/blob/master/Src/stm32f1xx_hal_adc.c)

### 2.1 核对 `User/Drivers/adc_input.h`

```c
#ifndef ADC_INPUT_H  // 第一次包含时导出驱动接口。
#define ADC_INPUT_H  // 防止重复包含。

#include <stdint.h>  // 提供原始值类型 uint32_t。
#include "stm32f1xx_hal.h"  // 提供 HAL_StatusTypeDef。

HAL_StatusTypeDef adc_input_calibrate(void);      // 上电后校准 ADC1，返回校准状态。
HAL_StatusTypeDef adc_input_read(uint32_t *raw);  // 调用者提供有效指针，返回本轮的采集状态。

#endif  // 结束驱动头文件保护区。
```

### 2.2 补全 `User/Drivers/adc_input.c`

当前第 10 行将 `adc_input_read` 写成了 `adc_input_read(void)`，应与头文件一致，接收 `uint32_t *raw`。现有实现写到了启动 ADC，下面给出包含等待、读数和停止的完整源码。

```c
#include "adc_input.h"  // 使实现与驱动接口声明一致。
#include "adc.h"        // 使用 CubeMX 生成的 ADC1 句柄 hadc1。

HAL_StatusTypeDef adc_input_calibrate(void) {  // 在外设初始化完成后执行一次校准。
    return HAL_ADCEx_Calibration_Start(&hadc1);  // 由调用方处理校准结果。
}  // 返回 ADC1 校准状态。

HAL_StatusTypeDef adc_input_read(uint32_t *raw) {  // 读取一次 PA1 的 12 位原始值。
    HAL_StatusTypeDef status = HAL_ADC_Start(&hadc1);  // 软件触发一次规则组转换。
    if (status != HAL_OK) {  // 启动失败时，本轮无法取得有效结果。
        return status;  // 把错误交给调用者。
    }  // 启动成功后进入轮询等待。

    status = HAL_ADC_PollForConversion(&hadc1, 10U);  // 等待转换完成，超时参数为 10 ms。
    if (status == HAL_OK) {  // 转换完成后才读取数据寄存器。
        *raw = HAL_ADC_GetValue(&hadc1);  // 保存本轮的原始值。
    }  // 等待失败时不使用原始值。
    HAL_StatusTypeDef stop_status = HAL_ADC_Stop(&hadc1);  // 已启动的转换无论成功或超时都执行停止。
    return status == HAL_OK ? stop_status : status;  // 保留轮询错误，轮询成功时检查停止结果。
}  // 返回本轮采集状态。
```

## 3. ADC 采集应用层

应用通过 `HAL_GetTick()` 独立安排约 100 ms 的 ADC 采集周期。ADC 校准失败时打印 `ADC CAL_ERR` 并停用 ADC 采集，原有 MPU6050 应用仍在主循环运行。单次 ADC 读失败时打印 `ADC READ_ERR`，下个周期继续尝试。两路应用依次发送各自的一行 USART1 日志。

### 3.1 补全 `User/App/app_adc.h`

```c
#ifndef APP_ADC_H  // 第一次包含时处理下面的声明。
#define APP_ADC_H  // 防止重复声明。

void app_adc_init(void);    // 外设初始化完成后，校准 ADC1 一次。
void app_adc_run(void);     // 到达采集周期时触发 ADC 并输出。

#endif  // 结束头文件保护区。
```

### 3.2 补全 `User/App/app_adc.c`

```c
#include "app_adc.h"  // 检查应用函数的声明和实现是否一致。
#include "adc_input.h"  // 通过驱动接口校准 ADC 和读取原始值。
#include "usart.h"    // 使用 USART1 发送文本日志。
#include <stdbool.h>  // 记录 ADC 校准是否成功。
#include <stdio.h>    // 使用 snprintf() 格式化采样结果。
#include <string.h>   // 使用 strlen() 获取日志字节数。

static const uint32_t vref_mv = 3300U;  // 估算电压使用的 VREF+，单位为 mV。
static bool adc_ready;                 // 校准完成后允许 ADC 采集。
static uint32_t last_sample_ms;         // 上一次开始采集的 HAL 毫秒计数。

static void send_line(const char *line) {  // 将一行日志统一从 USART1 发出。
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)line,  // 把字符串按字节发送。
                            (uint16_t)strlen(line), 20U);  // 发送有效字符，UART 超时参数为 20 ms。
}  // 采样结果由 ADC 状态决定，串口用于观察结果。

void app_adc_init(void) {  // 外设初始化完成后，校准 ADC1 一次。
    adc_ready = (adc_input_calibrate() == HAL_OK);  // 校准成功后才启用 ADC 采集。
    if (!adc_ready) {  // 只停用 ADC 采集。
        send_line("ADC CAL_ERR\r\n");  // 报告校准失败。
        return;  // 主循环仍继续调用 MPU6050 应用。
    }  // ADC 校准成功。
    last_sample_ms = HAL_GetTick();  // 从校准结束开始计时。
    send_line("ADC READY\r\n");      // 提示 ADC 采集就绪。
}  // 后续由主循环反复调用 app_adc_run()。

void app_adc_run(void) {  // 主循环反复调用，到期才执行采集。
    uint32_t now_ms = HAL_GetTick();  // 获取当前 HAL 毫秒计数。
    if (!adc_ready || now_ms - last_sample_ms < 100U) {  // 校准通过且已满 100 ms 才采样。
        return;  // 让主循环继续调用其他任务。
    }  // 无符号时间差适用于毫秒计数回绕。
    last_sample_ms = now_ms;  // 记录本轮读 ADC 的开始时间。

    uint32_t raw;  // 由 adc_input_read() 在转换完成后写入。
    if (adc_input_read(&raw) != HAL_OK) {  // 转换流程失败时跳过本轮数值输出。
        send_line("ADC READ_ERR\r\n");  // 报告一次读取失败。
        return;  // 下次达到采集间隔时继续尝试。
    }  // 继续执行时，raw 是本轮有效数据。

    uint32_t voltage_mv = (raw * vref_mv + 2047U) / 4095U;  // 以满量程比例估算电压并四舍五入。
    char line[40];  // 容纳一行原始值、电压和换行。
    int length = snprintf(line, sizeof(line), "ADC raw=%lu mv=%lu\r\n",  // 生成串口输出文本。
                          (unsigned long)raw, (unsigned long)voltage_mv);  // 参数类型与 %lu 对应。
    if (length > 0 && (size_t)length < sizeof(line)) {  // 只发送格式化成功且完整的一行。
        send_line(line);  // 输出这一帧原始值和估算电压。
    }  // 下一轮由毫秒计数控制。
}  // 结束本次采样。
```

电压换算属于应用层。`mv` 按 `vref_mv` 近似计算，原始值 `raw` 的范围是 `0～4095`。如果知道板上实际的 `VREF+`，将 `vref_mv` 改成对应毫伏数。[STM32F1 HAL 校准说明](https://www.st.com/resource/en/user_manual/dm00154093-description-of-stm32f1-hal-and-ll-drivers-stmicroelectronics.pdf)

## 4. 在 `Core/Src/main.c` 接入两路采集

按下面的位置，在每个 `USER CODE` 区增加 ADC 对应的一行。已有的 MPU6050 相关行各保留一份。

**① `USER CODE BEGIN Includes` 区，当前约第 27 行**

```c
#include "app_imu.h"  // 已有，声明 MPU6050 应用接口。
#include "app_adc.h"  // 新增，声明 ADC 应用接口。
```

**② `USER CODE BEGIN 2` 区，当前约第 95 行**

`MX_I2C1_Init()`、`MX_USART1_UART_Init()` 和 `MX_ADC1_Init()` 已在这个区之前完成。

```c
app_imu_init();  // 已有，初始化 MPU6050 应用。
app_adc_init();  // 新增，校准 ADC 并记录起始时间。
```

**③ `USER CODE BEGIN 3` 区，当前约第 106 行**

两行都放在 `while (1)` 的右花括号之前。主循环反复调用它们，各应用用自己的毫秒计数判断是否到期。两路单次 HAL 读写仍会占用主循环，实际间隔以串口观测为准。

```c
app_imu_run();  // 已有，按原周期采集 MPU6050。
app_adc_run();  // 新增，按自己的周期采集 PA1。
```

## 5. 核对 CMake

项目根目录的 `CMakeLists.txt` 已在 `target_sources` 中登记下列四个 ADC 文件。核对每个路径出现一次。当前 `target_include_directories` 已包含 `User/Drivers` 和 `User/App`，原有 MPU6050 文件也已在构建清单中。

```cmake
    User/Drivers/adc_input.c  # 编译 ADC 驱动层的实现。
    User/Drivers/adc_input.h  # 将驱动头文件关联到 CLion 项目目标。
    User/App/app_adc.c        # 编译 ADC 应用层的实现。
    User/App/app_adc.h        # 将应用头文件关联到 CLion 项目目标。
```

保存后在 CLion 执行 **Reload CMake Project**，然后用现有 Fire-DAP 配置构建和烧录。

## 6. 接线和验证

| 开发板 | 连接 |
|---|---|
| `PA1` | 模拟信号输出，初次测试可接开发板 `GND` |
| `GND` | 与模拟信号源共地 |
| `PA9 / USART1_TX` | 若使用独立 USB 转串口模块，接到模块 RX，并共地 |

PA1 输入应在 **0 至 VREF+** 范围内。开始测试前先断开 PA1 上其他模拟信号。用电位器验证时，若板上 3.3 V 与 ADC 参考电压相同，可将 **10 kΩ 电位器**两端分别接板上 3.3 V 和 GND，滑动端接 PA1。[STM32F103ZE 数据手册，表 59](https://www.st.com/resource/en/datasheet/stm32f103ze.pdf)

串口设置为 **115200、8N1**，打开后复位开发板。应看到两路启动信息，随后两类数据行交错输出。示例中 PA1 接地。

```text
MPU6050 READY id=0x68
ADC READY
IMU_RAW ms=210 AX=48 AY=-103 AZ=16321 GX=-18 GY=11 GZ=9
ADC raw=0 mv=0
IMU_RAW ms=310 AX=51 AY=-96 AZ=16330 GX=-17 GY=12 GZ=7
ADC raw=1 mv=1
```

| 验证动作或日志 | 如何判断 |
|---|---|
| 转动 MPU6050 | `IMU_RAW` 的六轴数据应随动作变化，同时还能看到 `ADC raw`。 |
| PA1 接 GND | `raw` 应接近 0。 |
| PA1 输入约 1.65 V，且 VREF+ 约 3.3 V | `raw` 应接近 2048。若用电位器，缓慢转动时数值应跟着变化。 |
| `ADC CAL_ERR` | ADC 校准失败，该路暂停采集。若 MPU6050 工作正常，仍应看到 `IMU_RAW`。 |
| `ADC READ_ERR` | 本轮转换未能完整结束。检查规则组为单通道软件触发，并确认 ADC 时钟和初始化已生效。 |
| 串口完全无输出 | 确认板上运行的是新固件，再检查 USB 串口、PA9/TX 和波特率。 |
