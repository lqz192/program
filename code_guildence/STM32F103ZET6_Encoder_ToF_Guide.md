# STM32F103ZET6 旋转编码器和激光测距代码编写指南

用 CubeMX 为第二块 STM32F103ZET6 建立独立的 CMake 工程，例如 `STM32F103ZET62`。旋转编码器和测距分别采集，在电脑的串口终端分别查看 `ENC count=...` 和 `TOF mm=...`。用 ST-Link 下载，通过 3.3 V USB 转串口查看日志。

## 1. CubeMX 从新建工程到生成代码

下面以现有工程使用的 STM32CubeMX 6.18.1 界面为参照。新工程使用内部 `HSI 8 MHz`，与第一块板项目的时钟配置一致。该配置可用于本指南的硬件编码器、100 kHz I²C 和串口日志。 [STM32CubeMX 6.18.1 官方操作手册](https://dev.st.com/stm32cube-docs/stm32cubemx/6.18.1/en/docs/markup/CubeMX_UserManual/chapters/04_4_stm32cubemx_user_interface.html)

### 1.1 新建并选择芯片

1. 打开 STM32CubeMX，点 `File → New Project`，进入 `MCU/MPU Selector`。
2. 在 `Part Number` 输入 `STM32F103ZET6`。选中匹配的型号，确认封装为 `LQFP144`，Flash 为 `512 KB`，点 `Start Project`。
3. 进入 `Pinout & Configuration`，右侧是芯片引脚图，左侧按类别列出外设。后面先在左边启用外设，再回右边核对引脚。

### 1.2 保留 SWD 并配置时钟

1. 在 `Pinout & Configuration → System Core → SYS`，将 `Debug` 设为 `Serial Wire`，将 `Timebase Source` 设为 `SysTick`。引脚图应显示 `PA13 = SWDIO`、`PA14 = SWCLK`，供 ST-Link 下载与调试。
2. 在 `System Core → RCC` 保持 `High Speed Clock (HSE)` 和 `Low Speed Clock (LSE)` 为 `Disable`，本方案使用内部 RC 时钟。
3. 切到顶部 `Clock Configuration`。在 `System Clock Mux` 选 `HSI`，确认 `SYSCLK = 8 MHz`，`AHB Prescaler = /1`、`APB1 Prescaler = /1`、`APB2 Prescaler = /1`。
4. 在时钟图确认 `HCLK`、`PCLK1`、`PCLK2` 均为 `8 MHz`。I²C2 用 APB1，USART1 与 TIM8 用 APB2。应用中的 `HAL_GetTick()` 使用 SysTick 毫秒时基。

### 1.3 配置 TIM8 编码器模式

1. 打开 `Pinout & Configuration → Timers → TIM8`。在 `Mode` 区把 `Combined Channels` 设为 `Encoder Mode`。
2. 在引脚图确认 `PC6 = TIM8_CH1`、`PC7 = TIM8_CH2`。实际接线是编码器 `A → PC6`、`B → PC7`，编码器模块和核心板共地。确认模块输出有确定的高低电平，若模块已将 `C` 接地，连接模块 `GND` 即可。
3. 在 TIM8 的 `Parameter Settings` 填入下表。输入捕获选项可能分成通道 1 和通道 2 两组，两组都要检查。

| 参数 | 值 |
| --- | --- |
| `Prescaler` | `0` |
| `Counter Mode` | `Up`。运行方向由 A/B 相序决定 |
| `Counter Period` | `65535` |
| `Internal Clock Division` | `DIV1` |
| `Repetition Counter` | `0` |
| `Auto-Reload Preload` | `Disable` |
| `Encoder Mode` | `Encoder Mode TI1 and TI2`，有些界面显示为 `TI1 and TI2` |
| `IC1/IC2 Polarity` | 两路均为 `Rising Edge` |
| `IC1/IC2 Selection` | 两路均为 `Direct TI` |
| `IC1/IC2 Prescaler` | 两路均为 `DIV1` |
| `IC1/IC2 Filter` | 两路先填 `8` |

4. 在 `GPIO Settings` 核对 PC6、PC7。已确认模块带上下拉时，可用 `No pull`。如果实测 A/B 悬空，先按模块电路补上合适的上拉或下拉。`NVIC Settings` 保持 TIM8 中断未勾选，应用会定时读取硬件计数器。

`TI1 and TI2` 模式使用两路边沿计数，`Rising Edge` 对应这里的非反相输入设置。`Filter = 8` 是滤波档位，实际能抑制的脉冲宽度取决于定时器时钟。上板后用慢转一格、正反转验证，再调整这两项滤波值。 [STM32F103ZE 引脚与外设数据手册](https://www.st.com/resource/en/datasheet/stm32f103ze.pdf) · [RM0008 TIM1/TIM8 编码器接口](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101-stm32f102-stm32f103-stm32f105-and-stm32f107-advanced-armbased-mcus-stmicroelectronics.pdf)

### 1.4 配置 I²C2 测距总线

1. 打开 `Pinout & Configuration → Connectivity → I2C2`，在 `Mode` 选 `I2C`。
2. 在 `Parameter Settings` 将 `I2C Speed Mode` 设为 `Standard Mode`，将频率设为 `100 kHz`。若界面以 Hz 显示，填 `100000`。地址位宽选 `7-bit`，其余地址设置保持默认。应用作为主机，传感器的地址在驱动中传入。
3. 在引脚图与 `GPIO Settings` 核对 `PB10 = I2C2_SCL`、`PB11 = I2C2_SDA`，两路使用复用开漏。`NVIC Settings` 和 `DMA Settings` 保持未启用，代码采用带超时的 HAL 轮询操作。
4. Yahboom `YB-MVV18 VER1.0` 测距模块使用 VL53L0X。按插座丝印接 `VCC → 3V3`、`GND → GND`、`SCL → PB10`、`SDA → PB11`。首版使用四针接口轮询，`XSH`、`INT` 留空。上电后确认总线空闲时 SCL、SDA 均为高电平。 [Yahboom 模块说明](https://www.yahboom.net/study/LaserRange/4862)

### 1.5 配置 USART1 调试输出

1. 打开 `Pinout & Configuration → Connectivity → USART1`，将 `Mode` 设为 `Asynchronous`，硬件流控选 `Disable`。
2. 在 `Parameter Settings` 填 `Baud Rate = 115200`、`Word Length = 8 Bits`、`Parity = None`、`Stop Bits = 1`。收发方向保留 `Receive and Transmit`，调试代码只使用发送。
3. 在引脚图确认 `PA9 = USART1_TX`、`PA10 = USART1_RX`。USART1 的中断和 DMA 保持未启用。
4. 仅看日志时，把 F103 的 `PA9` 接 3.3 V USB 转串口的 `RX`，两端共地。电脑串口终端也设置 `115200、8N1`、无流控。

### 1.6 生成独立的 CMake 工程

1. 进入 `Project Manager → Project`。`Project Name` 填 `STM32F103ZET62`，`Project Location` 选父目录 `E:\desk\program`。输出目录应为 `E:\desk\program\STM32F103ZET62`。
2. `Toolchain / IDE` 选 `CMake`。出现 `Default Compiler/Linker` 时选 `GCC`，与现有工程的 `arm-none-eabi-gcc` 工具链对应。STM32Cube 固件包选 STM32F1，已安装 `STM32Cube FW_F1 V1.8.7` 时可直接沿用。
3. 在 `Project Manager → Code Generator` 选 `Copy only the necessary library files`，勾选 `Generate peripheral initialization as a pair of '.c/.h' files per peripheral` 和 `Keep User Code when re-generating`。不同小版本可能在第一项勾选框的文字里加上 `and middleware`。
4. 在 `Project Manager → Advanced Settings` 确认 TIM8、I2C2、USART1 的驱动为 `HAL`，三个 `MX_*_Init` 均启用生成和调用。点右上角 `GENERATE CODE`。若提示缺少 STM32CubeF1 固件包，先安装再生成。 [STM32CubeMX 工程和生成设置](https://dev.st.com/stm32cube-docs/stm32cubemx/6.18.1/en/docs/markup/CubeMX_UserManual/chapters/04_4_stm32cubemx_user_interface.html#project-manager-view)

### 1.7 核对生成结果并先编译空工程

| 打开文件 | 核对内容 |
| --- | --- |
| `STM32F103ZET62.ioc` | 芯片是 `STM32F103ZET6`，TIM8、I2C2、USART1 和 SWD 引脚与上面一致 |
| `Core/Src/main.c` | `HAL_Init()` 后调用 `SystemClock_Config()`，进入 `while` 前调用 `MX_TIM8_Init()`、`MX_I2C2_Init()`、`MX_USART1_UART_Init()` |
| `Core/Src/tim.c` | `htim8.Instance = TIM8`，`Period = 65535`，`EncoderMode = TIM_ENCODERMODE_TI12`，`IC1Filter` 和 `IC2Filter` 均为 `8` |
| `Core/Src/i2c.c` | `hi2c2.Instance = I2C2`，`ClockSpeed = 100000`，地址模式为 `I2C_ADDRESSINGMODE_7BIT`，SCL/SDA 为开漏 |
| `Core/Src/usart.c` | `huart1.Instance = USART1`，波特率 `115200`，8 数据位、1 停止位、无校验 |
| `Core/Inc` 与工程根目录 | 有 `tim.h`、`i2c.h`、`usart.h`，根目录有 `CMakeLists.txt` 与 `CMakePresets.json` |

在生成的工程根目录运行以下命令，先验证工具链和 CubeMX 工程，再按后续章节添加 `User/` 下的代码。

```powershell
cmake --list-presets  # 列出当前工程可用的配置预设。
cmake --preset Debug  # 用 Debug 预设生成构建文件。
cmake --build --preset Debug  # 编译 Debug 固件。
```

如果实际预设名与 `Debug` 不同，以第一条命令列出的名称及 `CMakePresets.json` 为准。缺少 `arm-none-eabi-gcc`、CMake 或 Ninja 时，先让命令行或 CLion 的工具链能找到这些程序。

## 2. 工程结构和代码规范

| 层 | 文件 | 职责 |
| --- | --- | --- |
| 外设配置 | `Core/Src/tim.c`、`i2c.c`、`usart.c` | CubeMX 生成的外设、引脚和时钟配置 |
| 板级接口 | `User/Bsp/debug_uart.c/.h`、所选 ST 驱动的 I²C 平台文件 | 将串口输出和传感器总线读写接到本板外设 |
| 设备驱动 | `User/Drivers/encoder.c/.h` | 启动 TIM8、处理硬件计数器回卷、返回累计计数 |
| 设备驱动 | `User/Drivers/tof.c/.h` | 调用已选定的测距芯片驱动，返回数据就绪、有效距离或错误状态 |
| 应用 | `User/App/app_encoder.c/.h` | 管理编码器采样与输出周期，只持有本模块的状态 |
| 应用 | `User/App/app_tof.c/.h` | 管理测距轮询、超时和输出，只持有本模块的状态 |
| 入口 | `Core/Src/main.c` | 初始化外设和应用，向两个应用传递本轮的毫秒时间 |

`main.c` 调用两个应用。应用通过各自的驱动接口取得数据，再调用同一个 `send_line()` 输出日志。`htim8` 留在编码器驱动内，`hi2c2` 留在测距平台层，`huart1` 留在板级串口模块。

变量和自写函数使用小写下划线，例如 `previous_count`、`distance_mm` 和 `encoder_start`。驱动保留必要的 HAL 返回值和数据状态检查，由应用层打印日志。定时参数用有单位的名字，注释解释回卷、量程或超时等约束。CubeMX 生成文件中的用户改动放在 `USER CODE BEGIN/END` 区内。

## 3. 编码器驱动

`User/Drivers/encoder.h`

```c
#ifndef ENCODER_H  // 检查编码器头文件是否已经包含。
#define ENCODER_H  // 定义头文件保护宏。

#include <stdint.h>  // 引入 int32_t 等固定宽度整数类型。

#include "stm32f1xx_hal.h"  // 引入 HAL_StatusTypeDef 返回类型。

HAL_StatusTypeDef encoder_start(void);  // 声明启动 TIM8 编码器计数的接口。
int32_t encoder_read_count(void);  // 声明读取累计有符号计数的接口。

#endif  // 结束编码器头文件保护。
```

`User/Drivers/encoder.c`

```c
#include "encoder.h"  // 包含编码器驱动的公开接口。
#include "tim.h"  // 引入 CubeMX 生成的 htim8 句柄。

static uint16_t previous_count;  // 保存上一次读取的 16 位计数。
static uint32_t accumulated_count;  // 以无符号累加定义 32 位回卷行为。

HAL_StatusTypeDef encoder_start(void)  // 启动编码器驱动并返回 HAL 状态。
{  // 开始初始化编码器计数状态。
    __HAL_TIM_SET_COUNTER(&htim8, 0U);  // 将 TIM8 硬件计数器清零。
    previous_count = 0U;  // 将上一次计数初始化为零。
    accumulated_count = 0U;  // 将软件累计计数初始化为零。
    return HAL_TIM_Encoder_Start(&htim8, TIM_CHANNEL_ALL);  // 启动 TIM8 的两个编码器输入通道。
}  // 结束编码器启动函数。

int32_t encoder_read_count(void)  // 读取编码器从启动以来的有符号累计计数。
{  // 开始本次计数读取。
    uint16_t current_count = (uint16_t)__HAL_TIM_GET_COUNTER(&htim8);  // 读取当前的 16 位硬件计数。
    uint16_t wrapped_delta = (uint16_t)(current_count - previous_count);  // 用模 65536 的差值处理初步回卷。
    int32_t count_delta = wrapped_delta;  // 扩展到 32 位，便于还原有符号增量。

    // 将 16 位差值还原为跨越计数器边界时的有符号增量。
    if (wrapped_delta >= 0x8000U) {  // 最高位为 1 时按负增量处理。
        count_delta -= 0x10000L;  // 将 16 位回卷差值还原为负数。
    }  // 结束负增量转换。

    previous_count = current_count;  // 保存本次计数，供下一次求差。
    accumulated_count += (uint32_t)count_delta;  // 将本次增量加到总计数。

    if (accumulated_count <= INT32_MAX) {  // 检查累计值能否直接转换成 int32_t。
        return (int32_t)accumulated_count;  // 直接返回可表示的非负计数。
    }  // 结束非负计数分支。
    return -1 - (int32_t)(UINT32_MAX - accumulated_count);  // 将上半区映射到负值，避免超范围转换。
}  // 结束计数读取函数。
```

每次读数之间的实际增量绝对值须小于 `32768`。手转编码器时，每约 `10 ms` 读取一次即可。`65535 → 0` 应得到 `+1`，`0 → 65535` 应得到 `-1`。返回值以开机为零点，单位为 TIM8 计数。每格有多少计数由实物决定。

单测时，在 `main.c` 的用户代码区包含 `encoder.h`，在 `MX_TIM8_Init()` 后调用 `encoder_start()`，确认返回 `HAL_OK`。在主循环里把 `encoder_read_count()` 的结果赋给临时变量，用调试器观察，或先按第 5 节接入 USART1 日志。计数方向与约定相反时交换 A/B 接线。

## 4. 测距驱动

### 4.1 定义应用接口

`User/Drivers/tof.h`

```c
#ifndef TOF_H  // 检查测距头文件是否已经包含。
#define TOF_H  // 定义头文件保护宏。

#include <stdbool.h>  // 引入 bool 类型。
#include <stdint.h>  // 引入固定宽度的整数类型。

typedef enum {  // 定义一次测距轮询可能返回的状态。
    tof_pending,  // 传感器还没有新结果。
    tof_valid,  // 本次距离有效，输出参数已写入。
    tof_invalid,  // 本次有结果，但距离状态无效。
    tof_io_error  // I²C 或驱动操作失败。
} tof_status;  // 将枚举类型命名为 tof_status。

bool tof_start(void);  // 声明有时限的测距启动接口。
tof_status tof_poll(uint16_t *distance_mm);  // 声明轮询接口，只有结果有效时才写入距离。

#endif  // 结束测距头文件保护。
```

在 `tof.c` 里实现实物对应的一套驱动。`tof_start()` 做一次有时限的启动和配置。`tof_poll()` 每次只检查一次就绪状态。有新数据则读距离和范围状态，按官方 API 清除传感器内部中断状态，轮询方式也需要这一步。只有 `tof_valid` 才写入 `*distance_mm`。首版可以只接受芯片返回的范围状态 `0`。

### 4.2 接入 VL53L0X 驱动

导入 ST 的 `STSW-IMG005` 驱动。示例用 `VL53L0X_Dev_t.I2cDevAddr` 保存 `0x52`。将驱动平台层的 I²C 读写接到 `hi2c2`，采用带有限超时的 `HAL_I2C_Mem_Read/Write` 和 `I2C_MEMADD_SIZE_8BIT`。平台层直接把设备句柄中的 `0x52` 交给 HAL，保留官方驱动的多字节字节序处理。检查 SDK 初始化和校准中的等待循环，保证总线错误或一直未就绪时能够退出。 [STSW-IMG005](https://www.st.com/en/embedded-software/stsw-img005.html) · [VL53L0X API 手册](https://www.st.com/resource/en/user_manual/um2039-world-smallest-timeofflight-ranging-and-gesture-detection-sensor-application-programming-interface-stmicroelectronics.pdf) · [Yahboom STM32 参考代码](https://github.com/YahboomTechnology/Laser-ranging-module)

`User/Drivers/tof.c`

```c
#include "tof.h"  // 包含本项目的测距接口和状态类型。
#include "vl53l0x_api.h"  // 引入 ST VL53L0X 官方驱动接口。
#include "vl53l0x_platform.h"  // 引入读取芯片 ID 所需的平台读接口。

static VL53L0X_Dev_t tof_device;  // 保存芯片状态和通信地址。
static const uint8_t tof_hal_addr = 0x29U << 1;  // 将 7 位地址转换为 HAL 需要的格式。
static const uint32_t measurement_period_ms = 100U;  // 设置连续测距的更新间隔。

bool tof_start(void)  // 检查身份、初始化并启动传感器。
{  // 开始配置 VL53L0X。
    uint8_t model_id;  // 保存型号 ID。
    uint32_t spad_count;  // 保存参考 SPAD 校准给出的数量。
    uint8_t spad_is_aperture;  // 保存参考 SPAD 类型。
    uint8_t vhv_settings;  // 保存 VHV 参考校准结果。
    uint8_t phase_cal;  // 保存相位参考校准结果。
    tof_device.I2cDevAddr = tof_hal_addr;  // 设置设备句柄使用的 I²C 地址。

    if (VL53L0X_RdByte(&tof_device, VL53L0X_REG_IDENTIFICATION_MODEL_ID,  // 读取型号寄存器。
                       &model_id) != VL53L0X_ERROR_NONE || model_id != 0xEEU) {  // 检查读状态和型号 ID。
        return false;  // 通信失败或型号不符时退出。
    }  // 结束型号检查。

    return VL53L0X_DataInit(&tof_device) == VL53L0X_ERROR_NONE &&  // 复位后执行一次数据初始化。
           VL53L0X_StaticInit(&tof_device) == VL53L0X_ERROR_NONE &&  // 载入测距配置。
           VL53L0X_PerformRefSpadManagement(&tof_device, &spad_count,  // 配置参考 SPAD。
                                           &spad_is_aperture) == VL53L0X_ERROR_NONE &&  // 接收 SPAD 类型并检查结果。
           VL53L0X_PerformRefCalibration(&tof_device, &vhv_settings,  // 执行 VHV 和相位参考校准。
                                        &phase_cal) == VL53L0X_ERROR_NONE &&  // 接收相位结果并检查状态。
           VL53L0X_SetDeviceMode(&tof_device, VL53L0X_DEVICEMODE_CONTINUOUS_TIMED_RANGING)  // 启用连续定时测距。
               == VL53L0X_ERROR_NONE &&  // 切换模式成功后才配置间隔。
           VL53L0X_SetInterMeasurementPeriodMilliSeconds(&tof_device, measurement_period_ms)  // 设置两次测量的间隔。
               == VL53L0X_ERROR_NONE &&  // 间隔设置成功后才启动测量。
           VL53L0X_StartMeasurement(&tof_device) == VL53L0X_ERROR_NONE;  // 启动测量并返回结果。
}  // 结束测距启动函数。

tof_status tof_poll(uint16_t *distance_mm)  // 轮询一次 VL53L0X 测距结果。
{  // 开始检查新结果。
    uint8_t data_ready;  // 接收传感器的数据就绪标志。
    if (VL53L0X_GetMeasurementDataReady(&tof_device, &data_ready) != VL53L0X_ERROR_NONE) {  // 检查新测量是否就绪。
        return tof_io_error;  // 将就绪状态读取失败上报为通信错误。
    }  // 结束就绪状态错误分支。
    if (data_ready == 0U) {  // 判断传感器是否尚未产生新结果。
        return tof_pending;  // 本轮返回待测状态，由应用稍后轮询。
    }  // 结束未就绪分支。

    VL53L0X_RangingMeasurementData_t measurement;  // 保存同一帧的距离和范围状态。
    if (VL53L0X_GetRangingMeasurementData(&tof_device, &measurement) != VL53L0X_ERROR_NONE ||  // 读取整帧测距数据。
        VL53L0X_ClearInterruptMask(&tof_device, 0U) != VL53L0X_ERROR_NONE) {  // 清除芯片内的就绪状态。
        return tof_io_error;  // 将读数或清除状态失败上报为通信错误。
    }  // 结束测距读取错误分支。
    if (measurement.RangeStatus != 0U) {  // 只接受传感器报告的范围状态 0。
        return tof_invalid;  // 上报无效结果，保留调用方原有距离。
    }  // 结束范围状态判断。

    *distance_mm = measurement.RangeMilliMeter;  // 将有效的毫米距离写入输出参数。
    return tof_valid;  // 告知应用本次距离有效。
}  // 结束测距轮询函数。
```

## 5. 板级串口和应用模块

### 5.1 板级串口 `debug_uart`

`User/Bsp/debug_uart.h` 加头文件保护，声明 `void send_line(const char *line);`。沿用 MPU6050 和 ADC 的写法，用 `strlen()` 计算发送长度，USART1 超时参数为 `20 ms`。两路应用共用这一个实现，需要换行时由调用方在字符串末尾加 `\r\n`。

`User/Bsp/debug_uart.c`

```c
#include "debug_uart.h"  // 包含板级调试串口的公开接口。
#include "usart.h"  // 引入 CubeMX 生成的 huart1 句柄。

#include <string.h>  // 引入 strlen() 以计算文本字节数。

void send_line(const char *line)  // 通过 USART1 输出一行日志。
{  // 开始发送调用方提供的字符串。
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)line,  // 将字符串按字节发送。
                            (uint16_t)strlen(line), 20U);  // 发送有效字符，超时参数为 20 ms。
}  // 返回调用方。
```

### 5.2 编码器应用 `app_encoder`

`User/App/app_encoder.h` 加头文件保护并包含 `<stdint.h>`，声明 `void app_encoder_init(uint32_t now_ms);` 和 `void app_encoder_run(uint32_t now_ms);`。

`User/App/app_encoder.c`

```c
#include "app_encoder.h"  // 包含编码器应用的公开接口。
#include "debug_uart.h"  // 通过板级串口模块输出日志。
#include "encoder.h"  // 通过驱动接口启动编码器并读取位置。

#include <stdbool.h>  // 引入 bool 类型。
#include <stdio.h>  // 引入 snprintf() 以格式化日志。

static const uint32_t sample_period_ms = 10U;  // 设定编码器的取样间隔为 10 ms。
static const uint32_t log_period_ms = 100U;  // 设定编码器的日志间隔为 100 ms。

static bool encoder_ready;  // 记录编码器是否启动成功。
static uint32_t last_sample_ms;  // 保存上一次读取计数的时刻。
static uint32_t last_log_ms;  // 保存上一次输出日志的时刻。

void app_encoder_init(uint32_t now_ms)  // 初始化编码器应用，参数为当前毫秒时间。
{  // 开始初始化本应用的状态。
    last_sample_ms = now_ms;  // 用当前时间初始化采样计时。
    last_log_ms = now_ms;  // 用当前时间初始化日志计时。
    encoder_ready = encoder_start() == HAL_OK;  // 启动编码器并记录 HAL 返回结果。
    if (!encoder_ready) {  // 发现编码器启动失败时输出诊断。
        send_line("ENC START_ERR\r\n");  // 发送编码器启动错误日志。
    }  // 结束启动失败分支。
}  // 结束编码器应用初始化。

void app_encoder_run(uint32_t now_ms)  // 执行一轮编码器应用任务。
{  // 开始判断本轮是否采样。
    if (!encoder_ready || now_ms - last_sample_ms < sample_period_ms) {  // 编码器不可用，或距上次采样不足一个周期时跳过。
        return;  // 把控制权交还给主循环。
    }  // 结束采样条件判断。

    last_sample_ms = now_ms;  // 记录本次采样时刻。
    int32_t count = encoder_read_count();  // 读取累计有符号计数。
    if (now_ms - last_log_ms >= log_period_ms) {  // 达到日志周期时输出本次计数。
        last_log_ms = now_ms;  // 记录本次日志的输出时刻。
        char line[32];  // 为一行计数日志分配栈上缓冲区。
        int length = snprintf(line, sizeof(line), "ENC count=%ld\r\n", (long)count);  // 格式化计数日志并取得所需长度。
        if (length > 0 && (size_t)length < sizeof(line)) {  // 只发送格式化成功且完整的一行。
            send_line(line);  // 通过调试串口输出本次计数。
        }  // 结束日志长度检查。
    }  // 结束本轮计数日志输出。
}  // 结束本轮编码器应用任务。
```

### 5.3 测距应用 `app_tof`

`User/App/app_tof.h` 加头文件保护并包含 `<stdint.h>`，声明 `void app_tof_init(void);` 和 `void app_tof_run(uint32_t now_ms);`。测距初始化可能耗时，所以在启动后读取时间，作为第一次轮询和结果超时的起点。

`User/App/app_tof.c`

```c
#include "app_tof.h"  // 包含测距应用的公开接口。
#include "debug_uart.h"  // 通过板级串口模块输出日志。
#include "tof.h"  // 引入测距驱动的启动、轮询和状态定义。
#include "stm32f1xx_hal.h"  // 引入 HAL_GetTick() 毫秒时基接口。

#include <stdbool.h>  // 引入 bool 类型。
#include <stdio.h>  // 引入 snprintf() 以格式化距离日志。

static const uint32_t poll_period_ms = 10U;  // 设定测距就绪检查间隔为 10 ms。
static const uint32_t result_timeout_ms = 500U;  // 设定等待新测距结果的超时为 500 ms。

static bool tof_ready;  // 记录测距功能当前是否启用。
static uint32_t last_poll_ms;  // 保存上一次检查数据就绪的时刻。
static uint32_t last_result_ms;  // 保存上一次收到测距结果的时刻。

void app_tof_init(void)  // 初始化测距应用。
{  // 开始启动并初始化本应用的状态。
    tof_ready = tof_start();  // 调用驱动启动传感器并保存结果。
    if (!tof_ready) {  // 检查传感器是否启动失败。
        send_line("TOF START_ERR\r\n");  // 输出测距启动错误日志。
    }  // 结束启动失败分支。

    last_poll_ms = HAL_GetTick();  // 在传感器启动结束后记录轮询起点。
    last_result_ms = last_poll_ms;  // 从同一时刻开始计算等待结果的超时。
}  // 结束测距应用初始化。

void app_tof_run(uint32_t now_ms)  // 执行一轮测距应用任务。
{  // 开始判断本轮是否轮询。
    if (!tof_ready || now_ms - last_poll_ms < poll_period_ms) {  // 测距已停用，或距上次轮询不足一个周期时跳过。
        return;  // 把控制权交还给主循环。
    }  // 结束轮询条件判断。

    last_poll_ms = now_ms;  // 记录本次轮询时刻。
    uint16_t distance_mm;  // 为驱动的有效距离输出预留变量。

    switch (tof_poll(&distance_mm)) {  // 轮询传感器一次并按返回状态处理。
    case tof_pending:  // 传感器尚无新结果。
        if (now_ms - last_result_ms >= result_timeout_ms) {  // 检查等待新结果是否已经超时。
            tof_ready = false;  // 停用本轮上电后的后续测距轮询。
            send_line("TOF TIMEOUT\r\n");  // 输出测距超时日志。
        }  // 结束超时检测。
        break;  // 结束待测状态处理。
    case tof_valid: {  // 在本分支内格式化并发送有效距离。
        last_result_ms = now_ms;  // 刷新最近一次收到结果的时间。
        char line[24];  // 为一行测距日志分配栈上缓冲区。
        int length = snprintf(line, sizeof(line), "TOF mm=%u\r\n", (unsigned)distance_mm);  // 格式化毫米距离并取得所需长度。
        if (length > 0 && (size_t)length < sizeof(line)) {  // 只发送格式化成功且完整的一行。
            send_line(line);  // 通过调试串口输出有效距离。
        }  // 结束日志长度检查。
        break;  // 结束有效结果处理。
    }  // 结束有效结果分支的局部作用域。
    case tof_invalid:  // 传感器有新结果，但范围状态无效。
        last_result_ms = now_ms;  // 刷新结果到达时间，等待后续测量。
        send_line("TOF INVALID\r\n");  // 输出无效结果状态。
        break;  // 结束无效结果处理。
    case tof_io_error:  // 驱动报告通信或读数操作失败。
        tof_ready = false;  // 停用后续测距轮询。
        send_line("TOF I2C_ERR\r\n");  // 输出测距通信错误日志。
        break;  // 结束通信错误处理。
    }  // 结束测距状态分派。
}  // 结束本轮测距应用任务。
```

编码器硬件持续计数，应用约每 `10 ms` 读取一次，每 `100 ms` 打印一次。测距应用约每 `10 ms` 检查是否有新结果，传感器以自己的测量间隔更新。通信错误或超过 `500 ms` 没有结果时，只停止测距轮询。无效距离会输出状态，传感器继续测量。

## 6. 接入 `main.c` 和 CMake

在 `main.c` 的 `USER CODE BEGIN Includes` 区包含 `app_encoder.h` 和 `app_tof.h`。CubeMX 生成代码初始化 TIM8、I2C2 和 USART1 后，在 `USER CODE BEGIN 2` 区初始化两个应用。

```c
app_encoder_init(HAL_GetTick());  // 用当前毫秒时间初始化编码器应用。
app_tof_init();  // 初始化测距应用并启动传感器。
```

在主循环的 `USER CODE BEGIN 3` 区调度两个应用。

```c
uint32_t now_ms = HAL_GetTick();  // 在主循环本轮读取一次当前毫秒时间。
app_encoder_run(now_ms);  // 用本轮时间调度编码器采集与日志。
app_tof_run(now_ms);  // 用同一时刻调度测距轮询与日志。
```

在工程根 `CMakeLists.txt` 加入用户文件。将 `STSW-IMG005` 的 VL53L0X 驱动源文件、移植后的平台层源文件和相应头文件目录也注册到该目标。

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE  # 将以下源文件加入当前固件目标，仅供本目标使用。
    User/Bsp/debug_uart.c  # 编译板级调试串口实现。
    User/Drivers/encoder.c  # 编译编码器计数驱动。
    User/Drivers/tof.c  # 编译所选测距芯片的驱动适配。
    User/App/app_encoder.c  # 编译编码器采集应用。
    User/App/app_tof.c  # 编译测距采集应用。
)  # 结束用户源文件列表。

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE  # 为固件目标配置私有头文件搜索路径。
    User/Bsp  # 加入板级接口头文件目录。
    User/Drivers  # 加入设备驱动头文件目录。
    User/App  # 加入应用接口头文件目录。
)  # 结束头文件搜索路径列表。
```

先接入 `debug_uart.c`、`encoder.c` 和 `app_encoder.c`，在 `main.c` 中调用编码器应用即可验证旋钮。VL53L0X 平台层接好后，再把测距驱动、应用和初始化调用加入同一个工程。

## 7. 上板验收

1. **编码器**。连续同向转动时计数稳定增减，反转时符号相反。慢转一格、快速回转和静止都检查一遍。逆向从零开始时，串口应显示负数。
2. **距离**。固定模块，用平整目标在规格范围内选两个已知距离，确认读数随目标移动而变化，移开目标时核对范围状态。
3. **故障独立性**。断电后拔下测距模块再上电，确认日志报告 `TOF START_ERR`，同时仍能转动旋钮并看到 `ENC count` 更新。
