#include "app_imu.h"  // 对照应用接口检查以下函数的定义。
#include "app_node_link.h"  // 发布 MPU6050 的采样结果。
#include "mpu6050.h"  // 使用驱动接口、数据结构和返回状态。
#include "i2c.h"      // 获取 CubeMX 生成的 hi2c1。
#include "usart.h"    // 获取 CubeMX 生成的 huart1。
#include <stdbool.h>  // 提供 bool、true 和 false。
#include <stdio.h>    // 提供 snprintf，格式化六轴日志。
#include <string.h>   // 提供 strlen，计算日志的有效字节数。

static const uint8_t device_addr = 0x68U;  // AD0 接低用 0x68，接高改为 0x69。
static bool sensor_ready;                // 记录传感器是否已成功初始化。
static uint32_t last_action_ms;          // 记录上次读帧开始或初始化结束的时间。

static void send_line(const char *line)
{  // 统一通过 USART1 输出一条日志。
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)line,  // 发送字符串的字节内容。
                            (uint16_t)strlen(line), 20U);  // 不发送末尾的空字符，串口等待参数为 20 ms。
}  // 采集流程不依据串口发送结果改变传感器状态。

void app_imu_init(void)
{  // 开机和断线后共用同一套配置流程。
    mpu6050_status status = mpu6050_init(&hi2c1, device_addr);  // 访问 I2C1 上的 MPU6050。
    sensor_ready = (status == mpu6050_ok);  // 只有初始化成功才开始读帧。
    if (sensor_ready)
    {  // 驱动已读到并核对了芯片 ID。
        send_line("MPU6050 READY id=0x68\r\n");  // 在串口报告传感器就绪。
    } else
    {  // 让串口显示失败原因。
        send_line(status == mpu6050_bad_id ? "MPU6050 INIT_ERR BAD_ID\r\n"  // 已应答，但 ID 不符。
                                          : "MPU6050 INIT_ERR I2C\r\n"); // I²C 读写失败。
    }  // 两种结果都重新开始计时。
    last_action_ms = HAL_GetTick();  // 在初始化结束后记录当前毫秒数。
}  // 返回主循环。

void app_imu_run(void)
{  // 主循环持续调用，内部检查是否到点。
    uint32_t now_ms = HAL_GetTick();  // 读取当前系统毫秒数。
    uint32_t period_ms = sensor_ready ? 100U : 1000U;  // 正常时按采集周期，失败时按重试周期。
    if (now_ms - last_action_ms < period_ms)
    {  // 用无符号时间差判断间隔，也适用于计数回绕。
        return;  // 时间未到，马上交还主循环。
    }  // 到期后执行本轮操作。

    if (!sensor_ready)
    {  // 开机初始化失败，或上一次读帧失败。
        app_imu_init();   // 重新核对 ID 并写入配置。
        return;  // 下一轮再按新的就绪状态判断。
    }  // 设备已就绪时才读数据。

    last_action_ms = now_ms;  // 记下本次读帧开始时间。
    mpu6050_data raw_data;    // 存放一帧完整数据。
    if (mpu6050_read_data(&hi2c1, device_addr, &raw_data) != mpu6050_ok)
    {  // 失败的帧不能使用。
        sensor_ready = false;  // 下一次到期时重新初始化传感器。
        app_node_sample_update(node_first, NULL);  // 本轮读帧失败，清除旧样本。
        send_line("MPU6050 READ_ERR I2C\r\n");  // 表明这次读帧失败。
        return;  // 跳过本帧数据输出。
    }  // 继续执行时，raw_data 中的字段均有效。

    char line[96];  // 容纳最长的 32 位毫秒数和六个有符号 16 位值。
    int length = snprintf(line, sizeof(line),  // 将日志写入固定大小的缓冲区。
                          "IMU_RAW ms=%lu AX=%d AY=%d AZ=%d GX=%d GY=%d GZ=%d\r\n",  // 以一行输出时间戳和六轴。
                          (unsigned long)now_ms, raw_data.ax, raw_data.ay, raw_data.az,  // 时间戳和三轴加速度。
                          raw_data.gx, raw_data.gy, raw_data.gz);  // 三轴角速度。
    if (length > 0 && (size_t)length < sizeof(line))
    {  // 只发送格式化成功且完整的日志。
        app_node_sample_update(node_first, line);  // 保存有效的六轴数据供调度器读取。
        send_line(line);  // 将当前帧交给串口。
    }  // 返回主循环等待下一帧。
}  // 结束本轮应用调用
