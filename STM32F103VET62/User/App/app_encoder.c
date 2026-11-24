#include "app_encoder.h"
#include "debug_uart.h"
#include "encoder.h"

#include <stdbool.h>
#include <stdio.h>

static const uint32_t sample_period_ms = 10U;
static const uint32_t log_period_ms = 100U;

static bool encoder_ready;
static uint32_t last_sample_ms;
static uint32_t last_log_ms;

void app_encoder_init(uint32_t now_ms)
{
    last_sample_ms = now_ms;
    last_log_ms = now_ms;
    encoder_ready = encoder_start() == HAL_OK;
    if (!encoder_ready)
    {
        send_line("ENC START_ERR\r\n");  // 发送编码器启动错误日志。
    }
}

void app_encoder_run(uint32_t now_ms)
{
    if (!encoder_ready || now_ms - last_sample_ms < sample_period_ms)
    {
        return;
    }
    last_sample_ms = now_ms;
    int32_t count = encoder_read_count();
    if (now_ms - last_log_ms >= log_period_ms)
    {
        last_log_ms = now_ms;
        char line[32];// 为一行计数日志分配栈上缓冲区。
        int length = snprintf(line, sizeof(line), "ENC count=%ld\r\n", (long)count);  // 格式化计数日志并取得所需长度。
        if (length > 0 && (size_t)length < sizeof(line))
        {  // 只发送格式化成功且完整的一行。
            send_line(line);  // 通过调试串口输出本次计数。
        }  // 结束日志长度检查。
    }  // 结束本轮计数日志输出。
}

