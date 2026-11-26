/**
 * @file    app_imu.c
 * @brief   MPU6050 六轴周期采集与发布应用实现。
 */

#include "app_imu.h"
#include "app_node_link.h"
#include "debug_uart.h"
#include "i2c.h"
#include "mpu6050.h"
#include <stdbool.h>
#include <stdio.h>

#define IMU_DEV_ADDR        0x68U   // AD0 接地为 0x68，接高电平改为 0x69。
#define IMU_SAMPLE_MS       100U    // 正常采样周期，单位 ms。
#define IMU_RETRY_MS        1000U   // 初始化/通信故障后的重连周期，单位 ms。

static bool sensor_ready;           // 传感器初始化状态标志。
static uint32_t last_action_ms;     // 上次初始化结束或开始采样的毫秒时刻。

void app_imu_init(void)
{
    mpu6050_status status = mpu6050_init(&hi2c1, IMU_DEV_ADDR);
    sensor_ready = (status == mpu6050_ok);
    if (sensor_ready)
    {
        send_line("MPU6050 READY id=0x68\r\n");
    }
    else
    {
        send_line(status == mpu6050_bad_id ? "MPU6050 INIT_ERR BAD_ID\r\n"
                                           : "MPU6050 INIT_ERR I2C\r\n");
    }
    last_action_ms = HAL_GetTick();
}

void app_imu_run(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t period_ms = sensor_ready ? IMU_SAMPLE_MS : IMU_RETRY_MS;

    if (now_ms - last_action_ms < period_ms)
    {
        return;
    }
    if (!sensor_ready)
    {
        app_imu_init();
        return;
    }

    last_action_ms = now_ms;
    mpu6050_data raw_data;
    if (mpu6050_read_data(&hi2c1, IMU_DEV_ADDR, &raw_data) != mpu6050_ok)
    {
        sensor_ready = false;
        app_node_sample_update(node_first, NULL);  // 通信失败立即撤销第一槽旧样本。
        send_line("MPU6050 READ_ERR I2C\r\n");
        return;
    }

    char line[96];
    int length = snprintf(line, sizeof(line),
                          "IMU_RAW ms=%lu AX=%d AY=%d AZ=%d GX=%d GY=%d GZ=%d\r\n",
                          (unsigned long)now_ms,
                          raw_data.ax, raw_data.ay, raw_data.az,
                          raw_data.gx, raw_data.gy, raw_data.gz);
    if (length > 0 && (size_t)length < sizeof(line))
    {
        app_node_sample_update(node_first, line);
        send_line(line);
    }
}
