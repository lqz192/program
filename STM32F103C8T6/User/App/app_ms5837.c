/**
 * @file    app_ms5837.c
 * @brief   MS5837-02BA 传感器非阻塞周期采集与发布应用实现。
 */

#include "app_ms5837.h"
#include "app_node_link.h"
#include "debug_uart.h"
#include "i2c.h"
#include "ms5837.h"
#include <stdbool.h>
#include <stdio.h>

#define MS5837_SAMPLE_MS    200U    // 正常采样周期，单位 ms。
#define MS5837_RETRY_MS     1000U   // 初始化/通信故障后的重连周期，单位 ms。

static bool sensor_ready;           // 传感器初始化状态标志。
static bool sampling;               // 非阻塞采样进行中标志。
static uint32_t last_action_ms;     // 上次初始化结束或开始采样的毫秒时刻。

void app_ms5837_init(void)
{
    sampling = false;
    sensor_ready = ms5837_init(&hi2c2);
    if (!sensor_ready)
    {
        send_line("MS5837 INIT_ERR\r\n");
    }
    last_action_ms = HAL_GetTick();
}

void app_ms5837_run(void)
{
    uint32_t now_ms = HAL_GetTick();

    if (!sensor_ready)
    {
        if (now_ms - last_action_ms >= MS5837_RETRY_MS)
        {
            app_ms5837_init();
        }
        return;
    }

    if (!sampling)
    {
        if (now_ms - last_action_ms < MS5837_SAMPLE_MS)
        {
            return;
        }
        last_action_ms = now_ms;
        sampling = true;
    }

    int32_t pressure_pa = 0;
    int32_t temperature_centi = 0;
    ms5837_status status = ms5837_poll(&hi2c2, now_ms, &pressure_pa, &temperature_centi);
    if (status == ms5837_pending)
    {
        return;  // ADC 转换等待期间让出主循环，保障串口应答实时性。
    }

    sampling = false;
    if (status != ms5837_ok)
    {
        sensor_ready = false;
        app_node_sample_update(node_second, NULL);  // 通信失败立即撤销第二槽旧数据。
        send_line("MS5837 READ_ERR\r\n");
        return;
    }

    char line[80];
    int length = snprintf(line, sizeof(line), "MS5837 p_pa=%ld t_centi=%ld\r\n",
                          (long)pressure_pa, (long)temperature_centi);
    if (length > 0 && (size_t)length < sizeof(line))
    {
        app_node_sample_update(node_second, line);
        send_line(line);
    }
}
