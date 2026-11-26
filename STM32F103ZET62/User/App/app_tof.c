/**
 * @file    app_tof.c
 * @brief   VL53L0X 激光测距周期轮询与发布应用实现。
 */

#include "app_tof.h"
#include "app_node_link.h"
#include "debug_uart.h"
#include "stm32f1xx_hal.h"
#include "tof.h"
#include <stdbool.h>
#include <stdio.h>

#define TOF_POLL_MS         10U     // 正常轮询就绪状态周期，单位 ms。
#define TOF_RETRY_MS        1000U   // 启动或通信故障后的重连周期，单位 ms。
#define TOF_RESULT_TMO_MS   500U    // 持续无新结果判定超时的阈值，单位 ms。

static bool sensor_ready;           // 传感器初始化与运行就绪标志。
static uint32_t last_poll_ms;       // 上次轮询或重连的毫秒时刻。
static uint32_t last_result_ms;     // 上次收到有效或无效测距结果的毫秒时刻。

void app_tof_init(void)
{
    sensor_ready = tof_start();
    if (!sensor_ready)
    {
        send_line("TOF START_ERR\r\n");
    }
    last_poll_ms = HAL_GetTick();
    last_result_ms = last_poll_ms;
}

void app_tof_run(uint32_t now_ms)
{
    uint32_t period_ms = sensor_ready ? TOF_POLL_MS : TOF_RETRY_MS;
    if (now_ms - last_poll_ms < period_ms)
    {
        return;
    }
    if (!sensor_ready)
    {
        app_tof_init();
        return;
    }

    last_poll_ms = now_ms;
    uint16_t distance_mm = 0U;

    switch (tof_poll(&distance_mm))
    {
    case tof_pending:
        if (now_ms - last_result_ms >= TOF_RESULT_TMO_MS)
        {
            sensor_ready = false;
            app_node_sample_update(node_second, NULL);  // 结果超时，撤销第二槽旧数据。
            send_line("TOF TIMEOUT\r\n");
        }
        break;

    case tof_valid:
    {
        last_result_ms = now_ms;
        char line[24];
        int length = snprintf(line, sizeof(line), "TOF mm=%u\r\n", (unsigned)distance_mm);
        if (length > 0 && (size_t)length < sizeof(line))
        {
            app_node_sample_update(node_second, line);
            send_line(line);
        }
        break;
    }

    case tof_invalid:
        last_result_ms = now_ms;
        app_node_sample_update(node_second, NULL);  // 状态无效，撤销第二槽旧数据。
        send_line("TOF INVALID\r\n");
        break;

    case tof_io_error:
        sensor_ready = false;
        app_node_sample_update(node_second, NULL);  // 通信失败，撤销第二槽旧数据。
        send_line("TOF I2C_ERR\r\n");
        break;
    }
}
