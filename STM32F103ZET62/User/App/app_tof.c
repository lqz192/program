#include "app_tof.h"
#include "app_node_link.h"  // 发布有效的距离结果。
#include "debug_uart.h"
#include "tof.h"
#include "stm32f1xx_hal.h"  // 引入 HAL_GetTick() 毫秒时基接口

#include <stdbool.h>
#include <stdio.h>

static const uint32_t poll_period_ms = 10U;
static const uint32_t result_timeout_ms = 500U;

static bool tof_ready;
static uint32_t last_poll_ms;
static uint32_t last_result_ms;

void app_tof_init(void)
{
    tof_ready = tof_start();
    if (!tof_ready)
    {
        send_line("TOF START_ERR\r\n");  // 输出测距启动错误日志
    }

    last_poll_ms = HAL_GetTick();
    last_result_ms = last_poll_ms;
}

void app_tof_run(uint32_t now_ms)
{
    if (!tof_ready || now_ms - last_poll_ms < poll_period_ms)
    {
        return;
    }
    last_poll_ms = now_ms;
    uint16_t distance_mm;

    switch (tof_poll(&distance_mm))
    {
    case tof_pending:
        if (now_ms - last_result_ms >= result_timeout_ms)
        {
            tof_ready = false;
            app_node_sample_update(node_second, NULL);  // 结果超时，清除旧距离。
            send_line("TOF TIMEOUT\r\n");
        }
        break;
    case tof_valid:
    {
        last_result_ms = now_ms;
        char line[24];
        int length = snprintf(line, sizeof(line), "TOF mm=%u\r\n",
            (unsigned)distance_mm);  // 格式化毫米距离并取得所需长度。
        if (length > 0 && (size_t)length < sizeof(line))
        {  // 只发送格式化成功且完整的一行。
            app_node_sample_update(node_second, line);  // 保存有效距离供调度器读取。
            send_line(line);  // 通过调试串口输出有效距离。
        }  // 结束日志长度检查。
        break;
    }
    case tof_invalid:
        last_result_ms = now_ms;
        app_node_sample_update(node_second, NULL);  // 状态无效，清除旧距离。
        send_line("TOF INVALID\r\n");  // 输出无效结果状态
        break;
    case tof_io_error:
        tof_ready = false;
        app_node_sample_update(node_second, NULL);  // 通信失败，清除旧距离。
        send_line("TOF I2C_ERR\r\n");
        break;
    }
}
