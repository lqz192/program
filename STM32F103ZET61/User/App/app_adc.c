#include "app_adc.h"  // 检查应用函数的声明和实现是否一致。
#include "app_node_link.h"  // 发布 PA1 的采样结果。
#include "adc_input.h"  // 通过驱动接口校准 ADC 和读取原始值。
#include "usart.h"    // 使用 USART1 发送文本日志。
#include <stdbool.h>  // 记录 ADC 校准是否成功。
#include <stdio.h>    // 使用 snprintf() 格式化采样结果。
#include <string.h>   // 使用 strlen() 获取日志字节数。

static const uint32_t verf_mv = 3300U;
static bool adc_ready;
static uint32_t last_sample_ms;

static void send_line(const char *line)
{  // 将一行日志统一从 USART1 发出。
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)line,  // 把字符串按字节发送。
                            (uint16_t)strlen(line), 20U);  // 发送有效字符，UART 超时参数为 20 ms。
}  // 采样结果由 ADC 状态决定，串口用于观察结果

void app_adc_init(void)
{
    adc_ready = (adc_input_calibrate() == HAL_OK);  //校准成功后才启用ADC
    if (!adc_ready)
    {  // 只停用 ADC 采集。
        send_line("ADC CAL_ERR\r\n");  // 报告校准失败。
        return;  // 主循环仍继续调用 MPU6050 应用。
    }  // ADC 校准成功。
    last_sample_ms = HAL_GetTick();
    send_line("ADC READY\r\n");
}

void app_adc_run(void)
{
    uint32_t now_ms = HAL_GetTick();
    if (!adc_ready || now_ms - last_sample_ms < 100U)
    {
        return;
    }
    last_sample_ms = now_ms;

    uint32_t raw;
    if (adc_input_read( &raw ) != HAL_OK )
    {
        app_node_sample_update(node_second, NULL);  // 本轮读取失败，清除旧样本。
        send_line("ADC READ_ERR\r\n");
        return;
    }

    uint32_t voltage_ms = (raw * verf_mv + 2047U) / 4095U;
    char line[40];
    int length = snprintf(line, sizeof(line), "ADC raw=%lu mv=%lu\r\n",  // 生成串口输出文本。
                         (unsigned long)raw, (unsigned long)voltage_ms);  // 参数类型与 %lu 对应。
    if (length >0 && (size_t)length < sizeof(line))
    {
        app_node_sample_update(node_second, line);  // 保存有效的 ADC 数据供调度器读取。
        send_line(line);
    }
}
