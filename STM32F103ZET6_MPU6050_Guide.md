# STM32F103ZET6 单节点采集 MPU6050 · 精简代码编写指南

适用于你的 `STM32F103ZET61` CMake 工程。CubeMX 已配置 `I2C1`（PB6/SCL、PB7/SDA，100 kHz）和 `USART1`（115200、8N1）。这一版使用阻塞式 HAL 接口，每 100 ms 读取并输出一帧六轴原始数据。

| 文件 | 作用 |
|---|---|
| `User/Drivers/mpu6050.h` | 定义驱动接口、原始数据和返回状态 |
| `User/Drivers/mpu6050.c` | 检查芯片 ID、配置寄存器、读取并解析整帧 |
| `User/App/app_imu.h` | 声明应用入口 |
| `User/App/app_imu.c` | 定时采集、输出日志和重试 |
| `Core/Src/main.c` | 在用户代码区调用应用 |
| 根目录 `CMakeLists.txt` | 将用户文件和头文件目录关联到固件目标 |

按顺序替换四个用户文件的内容，再接入 `main.c`。文件用 UTF-8 保存。变量名、参数名和函数名采用小写下划线，例如 `device_addr`、`raw_data` 和 `app_imu_run`。

## 1. `User/Drivers/mpu6050.h`

驱动提供两个函数。应用负责传入已初始化的 I²C 句柄、正确的 7 位地址和有效的输出指针。返回状态分为成功、通信失败和芯片 ID 不符。

```c
#ifndef MPU6050_H  // 首次包含时处理下方声明。
#define MPU6050_H  // 保护类型和函数声明，避免重复定义。

#include <stdint.h>  // 提供固定宽度整数类型。
#include "stm32f1xx_hal.h"  // 提供 I2C_HandleTypeDef。

typedef enum {  // 驱动对上层提供的三种结果。
    mpu6050_ok,         // 操作成功，读帧结果可用。
    mpu6050_i2c_error,  // HAL 读寄存器或写寄存器失败。
    mpu6050_bad_id      // 读到了 ID，但与预期的 0x68 不符。
} mpu6050_status;  // 将操作结果作为函数返回值。

typedef struct {  // 保存一次整帧读取的数据。
    int16_t ax, ay, az;  // 三轴加速度原始值。
    int16_t gx, gy, gz;  // 三轴角速度原始值。
    int16_t temp_raw;   // 芯片内部温度原始值。
} mpu6050_data;  // 由应用分配结构体，驱动负责填充。

mpu6050_status mpu6050_init(I2C_HandleTypeDef *i2c, uint8_t device_addr);  // 检查芯片身份并配置寄存器。
mpu6050_status mpu6050_read_data(I2C_HandleTypeDef *i2c, uint8_t device_addr,  // 指定 I²C 句柄和 7 位地址。
                              mpu6050_data *out);  // 整帧读取成功后填充 out。

#endif  // 结束 MPU6050_H 保护区。
```

## 2. `User/Drivers/mpu6050.c`

先读 `WHO_AM_I`，再写电源和采样配置。MPU6050 支持连续写寄存器，`0x6B～0x6C` 和 `0x19～0x1C` 各用一次事务。读取时从 `0x3B` 开始取整帧，HAL 返回成功后再填充 `out`。[MPU-6050 产品规格，第 9.3 节](https://ieee-sensors.org/wp-content/uploads/2018/05/MPU-6000-Datasheet1.pdf)

```c
#include "mpu6050.h"  // 让函数实现使用头文件中的类型和声明。

#define REG_WHO_AM_I     0x75U  // 芯片身份寄存器。
#define REG_PWR_MGMT_1   0x6BU  // 两个电源管理寄存器的起始地址。
#define REG_SMPLRT_DIV   0x19U  // 四个采样配置寄存器的起始地址。
#define REG_ACCEL_XOUT_H 0x3BU  // 六轴与温度输出寄存器的起始地址。
#define I2C_TIMEOUT_MS   20U    // 传给每次阻塞式 HAL I²C 调用的超时参数。

static int16_t decode_int16(uint8_t high, uint8_t low) {  // 把两个寄存器字节还原为一个有符号值。
    int32_t value = ((int32_t)high << 8) | low;  // 高字节放高八位，低字节放低八位。
    return (int16_t)(value >= 0x8000 ? value - 0x10000 : value);  // 最高位为 1 时按 16 位补码还原负数。
}  // 返回解码后的原始计数。

mpu6050_status mpu6050_init(I2C_HandleTypeDef *i2c, uint8_t device_addr) {  // 检查芯片身份，再配置采集。
    uint16_t hal_addr = (uint16_t)(device_addr << 1);  // STM32F1 HAL 接收左移一位后的设备地址。
    uint8_t device_id;  // 接收 WHO_AM_I 返回的一个字节。
    uint8_t power[] = {0x01U, 0x00U};  // 0x6B 退出睡眠并选择 PLL，0x6C 清除六轴待机位。
    uint8_t config[] = {  // 按 0x19～0x1C 的顺序存放要写入的值。
        0x09U,  // 0x19，1 kHz 除以 (1 + 9)，寄存器更新频率为 100 Hz。
        0x03U,  // 0x1A，选择数字低通滤波档位 3。
        0x00U,  // 0x1B，选择陀螺仪量程 ±250 °/s。
        0x00U   // 0x1C，选择加速度计量程 ±2 g。
    };  // 这些值分别写入四个连续寄存器。

    if (HAL_I2C_Mem_Read(i2c, hal_addr, REG_WHO_AM_I, I2C_MEMADD_SIZE_8BIT,  // 使用 8 位寄存器地址。
                        &device_id, 1U, I2C_TIMEOUT_MS) != HAL_OK) {  // 读取身份字节并检查事务结果。
        return mpu6050_i2c_error;  // 通信失败时，停止本次初始化。
    }  // 读取成功后才使用 device_id。
    if (device_id != 0x68U) {  // AD0 接高时，WHO_AM_I 的期望值仍是 0x68。
        return mpu6050_bad_id;  // 芯片身份不符时停止配置。
    }  // 通过 ID 检查后再写控制寄存器。

    if (HAL_I2C_Mem_Write(i2c, hal_addr, REG_PWR_MGMT_1, I2C_MEMADD_SIZE_8BIT,  // 从 0x6B 开始。
                         power, sizeof(power), I2C_TIMEOUT_MS) != HAL_OK) {  // 连续写入两个电源寄存器。
        return mpu6050_i2c_error;  // 电源配置失败时返回。
    }  // 传感器已退出睡眠，进入采样配置。
    if (HAL_I2C_Mem_Write(i2c, hal_addr, REG_SMPLRT_DIV, I2C_MEMADD_SIZE_8BIT,  // 从 0x19 开始。
                         config, sizeof(config), I2C_TIMEOUT_MS) != HAL_OK) {  // 连续写入四个配置寄存器。
        return mpu6050_i2c_error;  // 采样配置失败时返回。
    }  // 两组寄存器均已写入。

    HAL_Delay(100U);  // 给唤醒后的传感器留出稳定时间。
    return mpu6050_ok;  // 配置完成，应用可以开始读帧。
}  // 结束驱动初始化。

mpu6050_status mpu6050_read_data(I2C_HandleTypeDef *i2c, uint8_t device_addr,  // 指定 I²C 外设与设备地址。
                              mpu6050_data *out) {  // out 接收本次读取的完整数据。
    uint8_t data[14];  // 三轴加速度、温度、三轴角速度各占两个字节。
    if (HAL_I2C_Mem_Read(i2c, (uint16_t)(device_addr << 1), REG_ACCEL_XOUT_H,  // 从 X 轴加速度高字节开始。
                        I2C_MEMADD_SIZE_8BIT, data, sizeof(data), I2C_TIMEOUT_MS) != HAL_OK) {  // 一次读 14 字节。
        return mpu6050_i2c_error;  // 失败时保留 out 原有内容。
    }  // 读取成功后才解码和写入结果。

    out->ax = decode_int16(data[0], data[1]);       // X 轴加速度。
    out->ay = decode_int16(data[2], data[3]);       // Y 轴加速度。
    out->az = decode_int16(data[4], data[5]);       // Z 轴加速度。
    out->temp_raw = decode_int16(data[6], data[7]); // 芯片温度原始值。
    out->gx = decode_int16(data[8], data[9]);       // X 轴角速度，起始下标在温度之后。
    out->gy = decode_int16(data[10], data[11]);    // Y 轴角速度。
    out->gz = decode_int16(data[12], data[13]);    // Z 轴角速度。
    return mpu6050_ok;  // 七个字段全部有效。
}  // 结束本次采集。
```

芯片寄存器每 10 ms 更新一次。STM32 的读取间隔由应用层单独决定。量程、分频和数据顺序对应手册第 4.2–4.5、4.17–4.19、4.28–4.29 和 4.32 节。[MPU-6050 寄存器手册](https://static6.arrow.com/aropdfconversion/b605e0c1cc2c970b8773921014f060a7e/5rm-mpu-6000a-00v4.2.pdf)

## 3. `User/App/app_imu.h`

`main.c` 只需要知道两个应用入口。应用内部使用本工程的 `hi2c1` 和 `huart1`。

```c
#ifndef APP_IMU_H  // 首次包含时导入应用接口。
#define APP_IMU_H  // 防止重复处理本头文件的内容。

void app_imu_init(void);  // 外设初始化完成后启动采集应用。
void app_imu_run(void);   // 从主循环反复调用，按时间执行采集或重试。

#endif  // 结束 APP_IMU_H 保护区。
```

## 4. `User/App/app_imu.c`

应用只保存两个运行状态，分别是传感器是否就绪和上次操作时间。就绪时每 100 ms 读一帧。初始化或读帧失败时，约 1 秒后重新初始化，让重新上电的模块恢复配置。

```c
#include "app_imu.h"  // 对照应用接口检查以下函数的定义。
#include "mpu6050.h"  // 使用驱动接口、数据结构和返回状态。
#include "i2c.h"      // 获取 CubeMX 生成的 hi2c1。
#include "usart.h"    // 获取 CubeMX 生成的 huart1。
#include <stdbool.h>  // 提供 bool、true 和 false。
#include <stdio.h>    // 提供 snprintf，格式化六轴日志。
#include <string.h>   // 提供 strlen，计算日志的有效字节数。

static const uint8_t device_addr = 0x68U;  // AD0 接低用 0x68，接高改为 0x69。
static bool sensor_ready;                // 记录传感器是否已成功初始化。
static uint32_t last_action_ms;          // 记录上次读帧开始或初始化结束的时间。

static void send_line(const char *line) {  // 统一通过 USART1 输出一条日志。
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)line,  // 发送字符串的字节内容。
                            (uint16_t)strlen(line), 20U);  // 不发送末尾的空字符，串口等待参数为 20 ms。
}  // 采集流程不依据串口发送结果改变传感器状态。

void app_imu_init(void) {  // 开机和断线后共用同一套配置流程。
    mpu6050_status status = mpu6050_init(&hi2c1, device_addr);  // 访问 I2C1 上的 MPU6050。
    sensor_ready = (status == mpu6050_ok);  // 只有初始化成功才开始读帧。
    if (sensor_ready) {  // 驱动已读到并核对了芯片 ID。
        send_line("MPU6050 READY id=0x68\r\n");  // 在串口报告传感器就绪。
    } else {  // 让串口显示失败原因。
        send_line(status == mpu6050_bad_id ? "MPU6050 INIT_ERR BAD_ID\r\n"  // 已应答，但 ID 不符。
                                          : "MPU6050 INIT_ERR I2C\r\n"); // I²C 读写失败。
    }  // 两种结果都重新开始计时。
    last_action_ms = HAL_GetTick();  // 在初始化结束后记录当前毫秒数。
}  // 返回主循环。

void app_imu_run(void) {  // 主循环持续调用，内部检查是否到点。
    uint32_t now_ms = HAL_GetTick();  // 读取当前系统毫秒数。
    uint32_t period_ms = sensor_ready ? 100U : 1000U;  // 正常时按采集周期，失败时按重试周期。
    if (now_ms - last_action_ms < period_ms) {  // 用无符号时间差判断间隔，也适用于计数回绕。
        return;  // 时间未到，马上交还主循环。
    }  // 到期后执行本轮操作。

    if (!sensor_ready) {  // 开机初始化失败，或上一次读帧失败。
        app_imu_init();   // 重新核对 ID 并写入配置。
        return;  // 下一轮再按新的就绪状态判断。
    }  // 设备已就绪时才读数据。

    last_action_ms = now_ms;  // 记下本次读帧开始时间。
    mpu6050_data raw_data;    // 存放一帧完整数据。
    if (mpu6050_read_data(&hi2c1, device_addr, &raw_data) != mpu6050_ok) {  // 失败的帧不能使用。
        sensor_ready = false;  // 下一次到期时重新初始化传感器。
        send_line("MPU6050 READ_ERR I2C\r\n");  // 表明这次读帧失败。
        return;  // 跳过本帧数据输出。
    }  // 继续执行时，raw_data 中的字段均有效。

    char line[96];  // 容纳最长的 32 位毫秒数和六个有符号 16 位值。
    int length = snprintf(line, sizeof(line),  // 将日志写入固定大小的缓冲区。
                          "IMU_RAW ms=%lu AX=%d AY=%d AZ=%d GX=%d GY=%d GZ=%d\r\n",  // 以一行输出时间戳和六轴。
                          (unsigned long)now_ms, raw_data.ax, raw_data.ay, raw_data.az,  // 时间戳和三轴加速度。
                          raw_data.gx, raw_data.gy, raw_data.gz);  // 三轴角速度。
    if (length > 0 && (size_t)length < sizeof(line)) {  // 只发送格式化成功且完整的日志。
        send_line(line);  // 将当前帧交给串口。
    }  // 返回主循环等待下一帧。
}  // 结束本轮应用调用。
```

## 5. `Core/Src/main.c`

在当前文件的三个 `USER CODE` 区分别加入下面的代码。每个区只加对应的一行。

| 位置 | 作用 |
|---|---|
| `USER CODE BEGIN Includes`，约第 26 行 | 引入应用头文件 |
| `USER CODE BEGIN 2`，约第 93 行 | 在 `MX_I2C1_Init()` 和 `MX_USART1_UART_Init()` 之后启动应用 |
| `USER CODE BEGIN 3`，约第 103 行 | 在 `while (1)` 循环内运行应用 |

```c
#include "app_imu.h"  // USER CODE BEGIN Includes 区，声明下面调用的应用函数。
```

```c
app_imu_init();  // USER CODE BEGIN 2 区，等 I2C1 和 USART1 初始化完成后再调用。
```

```c
app_imu_run();  // USER CODE BEGIN 3 区，由主循环反复调用。
```

旧版指南中的 `AppImu_Init(...)` 和 `AppImu_Run()` 对应本版的 `app_imu_init()` 和 `app_imu_run()`。已经写过旧调用时，用本版两行替换对应位置。

## 6. 根目录 `CMakeLists.txt`

当前工程已有下列项目。核对每个路径只出现一次，再执行 CLion 的 **Reload CMake Project**。两份头文件也列入目标，供 CLion 补全、跳转和检查使用。

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE  # 将用户文件关联到固件目标。
    User/Drivers/mpu6050.c  # 编译 MPU6050 驱动。
    User/Drivers/mpu6050.h  # 供 CLion 关联驱动头文件。
    User/App/app_imu.c  # 编译定时采集与串口输出逻辑。
    User/App/app_imu.h  # 供 CLion 关联应用头文件。
)  # 结束用户文件列表。

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE  # 声明固件目标的头文件搜索路径。
    User/Drivers  # 用于找到 mpu6050.h。
    User/App  # 用于找到 app_imu.h。
)  # 结束搜索路径列表。
```

## 上板检查

串口助手设为 **115200、8N1**。复位后预期先看到 `READY`，再约每 100 ms 输出一行。

```text
MPU6050 READY id=0x68
IMU_RAW ms=206 AX=48 AY=-103 AZ=16321 GX=-18 GY=11 GZ=9
IMU_RAW ms=306 AX=51 AY=-96 AZ=16330 GX=-17 GY=12 GZ=7
```

| 观察结果 | 如何判断 |
|---|---|
| 平放时 `AZ` 的绝对值接近 `16384`，翻面静止后反号 | ±2 g 量程下，约 1 g 对应 `16384` 个计数，可借此验证加速度数据及符号。 [寄存器手册，第 4.17 节](https://static6.arrow.com/aropdfconversion/b605e0c1cc2c970b8773921014f060a7e/5rm-pmu-6000a-00v4.2.pdf) |
| `INIT_ERR I2C` | 检查供电、共地、PB6/SCL 与 PB7/SDA、I²C 上拉和 `AD0` 选定的地址。 |
| `INIT_ERR BAD_ID` | I²C 读取成功，返回的芯片身份值与 `0x68` 不符。 |
| `READ_ERR I2C` | 本次数据未输出。约 1 秒后重新初始化，成功后继续采集。 |
