/**
 * @file    app_adc.c
 * @brief   ADC 周期采集与发布应用实现。
 */

#include "app_adc.h"
#include "app_node_link.h"
#include "adc_input.h"
#include "debug_uart.h"
#include <stdbool.h>
#include <stdio.h>

#define ADC_VREF_MV         3300U   // ADC 参考电压，单位 mV。
#define ADC_SAMPLE_MS       100U    // 正常采样周期，单位 ms。
#define ADC_RETRY_MS        1000U   // 校准失败后的重试周期，单位 ms。

static bool sensor_ready;           // ADC 校准就绪标志。
static uint32_t last_action_ms;     // 上次采样或校准重试的毫秒时刻。

void app_adc_init(void)
{
    sensor_ready = (adc_input_calibrate() == HAL_OK);
    last_action_ms = HAL_GetTick();
    if (!sensor_ready)
    {
        send_line("ADC CAL_ERR\r\n");
        return;
    }
    send_line("ADC READY\r\n");
}

void app_adc_run(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t period_ms = sensor_ready ? ADC_SAMPLE_MS : ADC_RETRY_MS;

    if (now_ms - last_action_ms < period_ms)
    {
        return;
    }
    if (!sensor_ready)
    {
        app_adc_init();
        return;
    }

    last_action_ms = now_ms;
    uint32_t raw = 0U;
    if (adc_input_read(&raw) != HAL_OK)
    {
        app_node_sample_update(node_second, NULL);  // 读取失败时撤销第二槽旧样本。
        send_line("ADC READ_ERR\r\n");
        return;
    }

    uint32_t voltage_mv = (raw * ADC_VREF_MV + 2047U) / 4095U;  // 12 位码值四舍五入换算 mV。
    char line[40];
    int length = snprintf(line, sizeof(line), "ADC raw=%lu mv=%lu\r\n",
                          (unsigned long)raw, (unsigned long)voltage_mv);
    if (length > 0 && (size_t)length < sizeof(line))
    {
        app_node_sample_update(node_second, line);
        send_line(line);
    }
}
