/**
 * @file    debug_uart.h
 * @brief   板级调试串口（USART1）日志发送接口。
 */

#ifndef DEBUG_UART_H
#define DEBUG_UART_H

/**
 * @brief  通过调试串口（USART1）阻塞发送一行文本日志。
 * @param  line 以 NUL 结尾的字符串指针；传入 NULL 时直接忽略。
 */
void send_line(const char *line);

#endif  /* DEBUG_UART_H */
