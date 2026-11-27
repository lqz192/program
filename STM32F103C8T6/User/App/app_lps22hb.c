/**
 * @file    app_lps22hb.c
 * @brief   LPS22HB 传感器周期采集与发布应用实现。
 */

#include "app_lps22hb.h"
#include "app_node_link.h"
#include "debug_uart.h"
#include "i2c.h"
#include "lps22hb.h"
#include <stdbool.h>
#include <stdio.h>

#define LPS22HB_DEV_ADDR        0x5CU   // SDO 接地为 0x5C，接高电平改为 0x5D。
#define LPS22HB_SAMPLE_MS       200U    // 正常采样周期，单位 ms。
#define LPS22HB_RETRY_MS        1000U   // 初始化/通信故障后的重连周期，单位 ms。
#define LPS22HB_RESULT_TMO_MS   1000U   // 持续未就绪判定传感器可能复位的超时阈值，单位 ms。

static bool sensor_ready;               // 传感器初始化状态标志。
static uint32_t last_action_ms;         // 上次初始化结束或开始采样的毫秒时刻。
static uint32_t last_result_ms;         // 上次成功读取有效数据的毫秒时刻。

void app_lps22hb_init(void)
{
    sensor_ready = lps22hb_init(&hi2c1, LPS22HB_DEV_ADDR);
    if (!sensor_ready)
    {
        send_line("LPS22HB INIT_ERR\r\n");
    }
    last_action_ms = HAL_GetTick();
    last_result_ms = last_action_ms;
}

void app_lps22hb_run(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t period_ms = sensor_ready ? LPS22HB_SAMPLE_MS : LPS22HB_RETRY_MS;

    if (now_ms - last_action_ms < period_ms)
    {
        return;
    }
    if (!sensor_ready)
    {
        app_lps22hb_init();
        return;
    }

    last_action_ms = now_ms;
    int32_t pressure_pa = 0;
    int32_t temperature_centi = 0;
    lps22hb_status status = lps22hb_read(&hi2c1, LPS22HB_DEV_ADDR,
                                         &pressure_pa, &temperature_centi);

    if (status == lps22hb_not_ready)
    {
        // 偶发未就绪继续等待；持续超时则认为传感器已掉电复位并退回重新初始化。
        if (now_ms - last_result_ms >= LPS22HB_RESULT_TMO_MS)
        {
            sensor_ready = false;
            app_node_sample_update(node_first, NULL);
            send_line("LPS22HB TIMEOUT\r\n");
        }
        return;
    }
    if (status != lps22hb_ok)
    {
        sensor_ready = false;
        app_node_sample_update(node_first, NULL);  // 通信失败立即撤销第一槽旧数据。
        send_line("LPS22HB READ_ERR\r\n");
        return;
    }

    last_result_ms = now_ms;
    char line[80];
    int length = snprintf(line, sizeof(line), "LPS22HB p_pa=%ld t_centi=%ld\r\n",
                          (long)pressure_pa, (long)temperature_centi);
    if (length > 0 && (size_t)length < sizeof(line))
    {
        app_node_sample_update(node_first, line);
        send_line(line);
    }
}
