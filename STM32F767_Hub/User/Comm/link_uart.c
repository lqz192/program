/**
 * @file    link_uart.c
 * @brief   板间 ASCII 协议帧的串口中断收发与帧头解析实现。
 */

#include "link_uart.h"
#include <stdio.h>
#include <string.h>

#define LINK_TX_TIMEOUT_MS 30U  // 阻塞发送单帧数据的最大等待时间，单位 ms。
#define LINK_RX_RETRY_MS   100U // 接收中断挂载失败后的低频重试周期，单位 ms。

/**
 * @brief  重新挂载单字节中断接收，失败时置位恢复标志。
 * @param  link 接收上下文指针。
 * @retval true 挂接成功；false 挂接失败。
 */
static bool restart_rx_it(link_uart *link)
{
    if (HAL_UART_Receive_IT(link->uart, &link->rx_byte, 1U) != HAL_OK)
    {
        link->rx_restart_needed = true;
        link->last_restart_ms = HAL_GetTick();
        return false;
    }
    link->rx_restart_needed = false;
    return true;
}

/**
 * @brief  从字符串游标处解析一个十进制 uint16_t 字段。
 * @param  cursor 输入输出参数，传入起始位置，成功时推进到数字末尾之后。
 * @param  out    输出解析得到的 16 位无符号整数。
 * @retval true 解析成功且未溢出；false 字段为空、非数字或超出 uint16_t 范围。
 */
static bool read_u16(const char **cursor, uint16_t *out)
{
    const char *field = *cursor;
    uint32_t value = 0U;

    if (*field < '0' || *field > '9')
    {
        return false;  // 至少需要一位数字，拒绝空字段或符号前缀。
    }

    do
    {
        value = value * 10U + (uint32_t)(*field++ - '0');
        if (value > UINT16_MAX)
        {
            return false;
        }
    } while (*field >= '0' && *field <= '9');

    *cursor = field;
    *out = (uint16_t)value;
    return true;
}

bool link_uart_start(link_uart *link, UART_HandleTypeDef *uart)
{
    *link = (link_uart){ .uart = uart };
    __HAL_UART_CLEAR_OREFLAG(uart);  // 启动前清除可能残留的溢出错误标志。
    return restart_rx_it(link);
}

void link_uart_on_rx(link_uart *link)
{
    if (HAL_UART_GetError(link->uart) != HAL_UART_ERROR_NONE)
    {
        // 保留 HAL ErrorCode 供中断后续判断（如 ORE 判定），由 ErrorCallback 统一丢帧并恢复。
        return;
    }

    uint8_t byte = link->rx_byte;
    if (byte == '\n')
    {
        if (!link->ready && !link->discard && link->used != 0U)
        {
            link->line[link->used] = '\0';
            link->received_ms = HAL_GetTick();
            link->ready = true;  // 发布完整行，供主循环读取。
        }
        link->used = 0U;
        link->discard = false;
    }
    else if (byte != '\r' && !link->discard)
    {
        // 缓冲区已被上层占用、遇到非可打印 ASCII 或行长超限时丢弃至行尾。
        if (link->ready || byte < 32U || byte > 126U || link->used >= LINK_LINE_CAPACITY - 1U)
        {
            link->discard = true;
        }
        else
        {
            link->line[link->used++] = (char)byte;
        }
    }

    (void)restart_rx_it(link);
}

void link_uart_on_error(link_uart *link)
{
    __HAL_UART_CLEAR_OREFLAG(link->uart);
    link->used = 0U;
    link->discard = true;  // 丢弃出错帧的残余字节，直至下一个换行符。
    if ((HAL_UART_GetState(link->uart) & HAL_UART_STATE_BUSY_RX) != HAL_UART_STATE_BUSY_RX)
    {
        (void)restart_rx_it(link);  // 仅在 HAL 接收已停止时重新挂接，避免与仍在进行的 RX 冲突。
    }
}

bool link_uart_take(link_uart *link, char line[LINK_LINE_CAPACITY], uint32_t *received_ms)
{
    if (link->rx_restart_needed && HAL_GetTick() - link->last_restart_ms >= LINK_RX_RETRY_MS)
    {
        (void)restart_rx_it(link);  // 启动或中断侧挂接失败时由主循环低频恢复接收。
    }
    if (!link->ready)
    {
        return false;
    }

    uint16_t index = 0U;
    do
    {
        line[index] = link->line[index];
    } while (line[index++] != '\0');

    if (received_ms != NULL)
    {
        *received_ms = link->received_ms;
    }
    link->ready = false;  // 必须在拷贝完数据后再释放行缓冲区。
    return true;
}

bool link_uart_send(UART_HandleTypeDef *uart, const char *line)
{
    char wire[LINK_LINE_CAPACITY + 1U];
    int length = snprintf(wire, sizeof(wire), "%s\n", line);
    if (length <= 0 || (size_t)length >= sizeof(wire))
    {
        return false;
    }
    return HAL_UART_Transmit(uart, (uint8_t *)wire, (uint16_t)length, LINK_TX_TIMEOUT_MS) == HAL_OK;
}

bool link_parse(const char *line, link_frame *frame)
{
    const char *comma = strchr(line, ',');
    if (comma == NULL || comma == line || (size_t)(comma - line) >= sizeof(frame->type))
    {
        return false;
    }

    size_t type_length = (size_t)(comma - line);
    memcpy(frame->type, line, type_length);
    frame->type[type_length] = '\0';

    const char *field = comma + 1;
    if (!read_u16(&field, &frame->node) || *field != ',')
    {
        return false;
    }
    ++field;  // 跳过节点号后的逗号。

    if (!read_u16(&field, &frame->seq))
    {
        return false;
    }

    if (*field == '\0')
    {
        frame->payload = NULL;  // 无正文的控制帧（PING / PONG / GET）。
        return true;
    }
    if (*field != ',' || field[1] == '\0')
    {
        return false;  // 正文前必须有逗号且正文不能为空。
    }

    frame->payload = field + 1;  // 正文直接指向调用方提供的行缓冲。
    return true;
}
