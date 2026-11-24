#include "debug_uart.h"  // 包含板级调试串口的公开接口。
#include "usart.h"  // 引入 CubeMX 生成的 huart1 句柄。

#include <string.h>  // 引入 strlen() 以计算文本字节数。

void send_line(const char *line)  // 通过 USART1 输出一行日志。
{  // 开始发送调用方提供的字符串。
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)line,  // 将字符串按字节发送。
                            (uint16_t)strlen(line), 20U);  // 发送有效字符，超时参数为 20 ms。
}  // 返回调用方。
