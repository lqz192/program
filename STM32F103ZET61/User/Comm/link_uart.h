/**
 * @file    link_uart.h
 * @brief   板间 ASCII 协议帧的串口中断收发与帧头解析接口。
 */

#ifndef LINK_UART_H
#define LINK_UART_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#define LINK_LINE_CAPACITY 224U  // 单条协议行的最大缓冲区字节数（含结尾 NUL）。

/**
 * @brief  单路串口的接收上下文。
 */
typedef struct
{
    UART_HandleTypeDef *uart;                   // 关联的 HAL UART 外设句柄。
    uint8_t rx_byte;                            // 单字节中断接收缓存。
    volatile char line[LINK_LINE_CAPACITY];     // 中断写入、主循环读取的行缓冲区。
    volatile uint32_t received_ms;              // 完整行到达时的系统毫秒时间戳。
    volatile bool ready;                        // true 表示行缓冲区已有完整帧待取走。
    uint16_t used;                              // 当前正在接收的行长度（仅中断维护）。
    bool discard;                               // 丢弃当前异常行，直至下一个换行符恢复同步。
    volatile bool rx_restart_needed;            // 中断侧或启动时挂接接收失败，需主循环低频重试。
    uint32_t last_restart_ms;                   // 最近一次尝试挂载接收中断的毫秒时刻。
} link_uart;

/**
 * @brief  文本协议帧的解析结果。
 * @note   帧格式为 "TYPE,node,seq" 或 "TYPE,node,seq,payload"。
 */
typedef struct
{
    char type[5];           // 命令类型字符串（最多 4 字符 + NUL）。
    uint16_t node;          // 目标或源节点编号。
    uint16_t seq;           // 请求/应答序号。
    const char *payload;    // 指向原行缓冲中的正文起始位置；无正文时为 NULL。
} link_frame;

/**
 * @brief  初始化接收上下文并启动单字节中断接收。
 * @param  link 接收上下文指针。
 * @param  uart HAL UART 句柄指针。
 * @retval true 启动成功；false 启动失败（主循环可通过 link_uart_take 兜底重试）。
 */
bool link_uart_start(link_uart *link, UART_HandleTypeDef *uart);

/**
 * @brief  串口单字节接收完成回调处理，应在 HAL_UART_RxCpltCallback 中调用。
 * @param  link 接收上下文指针。
 */
void link_uart_on_rx(link_uart *link);

/**
 * @brief  串口错误回调处理，应在 HAL_UART_ErrorCallback 中调用。
 * @param  link 接收上下文指针。
 */
void link_uart_on_error(link_uart *link);

/**
 * @brief  从接收上下文取走一条完整协议行，并按需执行接收中断兜底恢复。
 * @param  link        接收上下文指针。
 * @param  line        输出行缓冲区，容量至少为 LINK_LINE_CAPACITY。
 * @param  received_ms 输出帧到达时间戳，不需要时可传 NULL。
 * @retval true 成功取走一行完整帧；false 当前无可用帧。
 */
bool link_uart_take(link_uart *link, char line[LINK_LINE_CAPACITY], uint32_t *received_ms);

/**
 * @brief  将单行文本附加换行符后通过指定串口阻塞发送。
 * @param  uart HAL UART 句柄指针。
 * @param  line 待发送的协议文本（不含结尾换行符）。
 * @retval true 发送成功；false 格式化超长或发送超时失败。
 */
bool link_uart_send(UART_HandleTypeDef *uart, const char *line);

/**
 * @brief  解析一条完整文本协议帧的帧头与正文指针。
 * @param  line  待解析的 NUL 结尾字符串。
 * @param  frame 输出解析结果结构体指针。
 * @retval true 格式合法；false 帧格式非法。
 */
bool link_parse(const char *line, link_frame *frame);

#endif  /* LINK_UART_H */
