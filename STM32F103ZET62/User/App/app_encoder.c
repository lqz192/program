/**
 * @file    app_encoder.c
 * @brief   正交编码器周期采样与发布应用实现。
 */

#include "app_encoder.h"
#include "app_node_link.h"
#include "debug_uart.h"
#include "encoder.h"
#include <stdbool.h>
#include <stdio.h>

#define ENCODER_SAMPLE_MS   10U     // 硬件计数器采样周期（防 16 位溢出），单位 ms。
#define ENCODER_LOG_MS      100U    // 发布与日志输出周期，单位 ms。

static bool sensor_ready;           // 编码器启动状态标志。
static uint32_t last_sample_ms;     // 上次采样时刻，单位 ms。
static uint32_t last_log_ms;        // 上次发布日志时刻，单位 ms。

void app_encoder_init(uint32_t now_ms)
{
    last_sample_ms = now_ms;
    last_log_ms = now_ms;
    sensor_ready = (encoder_start() == HAL_OK);
    if (!sensor_ready)
    {
        send_line("ENC START_ERR\r\n");
    }
}

void app_encoder_run(uint32_t now_ms)
{
    if (!sensor_ready || now_ms - last_sample_ms < ENCODER_SAMPLE_MS)
    {
        return;
    }
    last_sample_ms = now_ms;

    int32_t count = encoder_read_count();
    if (now_ms - last_log_ms >= ENCODER_LOG_MS)
    {
        last_log_ms = now_ms;
        char line[32];
        int length = snprintf(line, sizeof(line), "ENC count=%ld\r\n", (long)count);
        if (length > 0 && (size_t)length < sizeof(line))
        {
            app_node_sample_update(node_first, line);
            send_line(line);
        }
    }
}
