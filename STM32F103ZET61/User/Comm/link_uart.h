#ifndef LINK_UART_H
#define LINK_UART_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#define LINK_LINE_CAPACITY 224U

typedef struct
{
    // 保存一条 UART 的接收状态。
    UART_HandleTypeDef *uart;  // 对应 CubeMX 的 UART 句柄。
    uint8_t rx_byte;  // HAL 每次接收的一个字节。
    volatile char line[LINK_LINE_CAPACITY];  // 中断写入、主循环读取的行缓冲。
    volatile uint32_t received_ms;  // 最后一个字节到达时的毫秒数。
    volatile bool ready;  // 完整行发布后，中断保留该缓冲区。
    uint16_t used;  // 当前行长度，只由接收中断维护。
    bool discard;  // 丢弃当前不完整的行直至下一个换行。
}link_uart;  // UART 接收上下文。

typedef struct {  // 保存解析后的帧头。
    char type[5];  // 命令最多四字符，末尾加 NUL。
    uint16_t node;  // 节点号。
    uint16_t seq;  // 请求序号。
    const char *payload;  // 指向原字符串的数据正文，空指针表示无正文。
} link_frame;  // 文本帧的解析结果。

bool link_uart_start(link_uart *link, UART_HandleTypeDef *uart);
void link_uart_on_rx( link_uart *link);
void link_uart_on_error( link_uart *link);
bool link_uart_take(link_uart *link, char line[LINK_LINE_CAPACITY], uint32_t *received_ms);
bool link_uart_send(UART_HandleTypeDef *uart, const char *line);
bool link_parse(const char *line, link_frame *frame);

#endif
