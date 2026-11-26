/**
 * @file    debug_uart.c
 * @brief   板级调试串口（USART1）日志发送实现。
 */

#include "debug_uart.h"
#include "usart.h"
#include <string.h>

#define DEBUG_UART_TX_TIMEOUT_MS 20U  // 调试串口阻塞发送超时时间，单位 ms。

void send_line(const char *line)
{
    if (line == NULL)
    {
        return;
    }
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)line,
                            (uint16_t)strlen(line), DEBUG_UART_TX_TIMEOUT_MS);
}
