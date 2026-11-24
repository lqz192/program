#include "link_uart.h"
#include <stdio.h>
#include <string.h>

bool link_uart_start(link_uart *link, UART_HandleTypeDef *uart)
{
    *link = (link_uart){ .uart =uart};
    return HAL_UART_Receive_IT(uart, &link->rx_byte ,1U) == HAL_OK ;
}

void link_uart_on_rx( link_uart *link)
{
    if (HAL_UART_GetError(link->uart) != HAL_UART_ERROR_NONE)
    {
        return;  // 让 HAL 继续处理错误，并由错误回调恢复接收。
    }

    uint8_t byte = link->rx_byte;
    if (byte == '\n')
    {
        if (!link->ready && !link->discard && link->used != 0U)
        {
            link->line[link->used] = '\0';
            link->received_ms = HAL_GetTick();  // 记录完整应答到达时间。
            link->ready = true;  // 发布完整行，供主循环读取。
        }
        link->used = 0U;
        link->discard = false;
    }
    else if (byte != '\r' && !link->discard )
    {
        if (link->ready || byte < 32U || byte > 126U || link->used >= LINK_LINE_CAPACITY - 1U)
        {  // 检查占用、ASCII 与长度。
            link->discard = true;  // 丢到换行并保留已经发布的行。
        }
        else
        {  // 当前字节可以追加。
            link->line[link->used++] = (char)byte;  // 保存字符并递增长度。
        }  // 完成本字节入队。
    }  // 完成字节分类。
    (void)HAL_UART_Receive_IT(link->uart, &link->rx_byte, 1U);  // 继续接收下一个字节。
}

void link_uart_on_error(link_uart *link)
{
    link->used = 0U;  // 丢弃尚未发布的行。
    link->discard = true;  // 等到换行再接收下一帧。
    (void)HAL_UART_Receive_IT(link->uart, &link->rx_byte, 1U);  // 接收已终止时重新启动。
}

bool link_uart_take(link_uart *link, char line[LINK_LINE_CAPACITY], uint32_t *received_ms)
{
    if (!link->ready)
    {  // 检查是否有完整行。
        return false;  // 没有完整行就继续其他任务。
    }  // 本次有一帧可读。
    uint16_t index = 0U;  // 从首字节开始复制。
    do
    {  // 复制正文和 NUL。
        line[index] = link->line[index];  // 读取中断发布的字符。
    } while (line[index++] != '\0');  // 遇到字符串结束符停止。
    if (received_ms != NULL)
    {
        *received_ms = link->received_ms;  // 释放缓冲前取走该帧的到达时间。
    }
    link->ready = false;  // 释放共享行缓冲。
    return true;  // 调用方得到一条完整行。
}

bool link_uart_send(UART_HandleTypeDef *uart, const char *line)
{
    char wire[LINK_LINE_CAPACITY + 1U];
    int length = snprintf(wire, sizeof(wire), "%s\n", line);  // 形成单条 ASCII 帧。
    if (length <= 0 || (size_t)length >= sizeof(wire))
    {  // 发送前排除格式化失败或截断。
        return false;  // 让调用方处理发送失败。
    }  // 缓冲区现有完整帧。
    return HAL_UART_Transmit(uart, (uint8_t *)wire, (uint16_t)length, 30U) == HAL_OK;  // 发送有效字节并限制等待时间。
}

static bool read_u16(const char **custor, uint16_t *out)
{
    const char *field = *custor;
    uint32_t value = 0;
    if (*field < '0' || *field > '9')
    {  // 至少需要一位数字。
        return false;  // 拒绝空字段和符号。
    }  // 首字符合法。
    do
    {
        value = value * 10U + (uint32_t )(*field++ - '0');
        if (value > UINT16_MAX)
        {
            return false;
        }
    }while (*field >= '0' && *field <= '9');
    *custor = field;
    *out = (uint16_t )value;
    return true;
}

bool link_parse(const char *line, link_frame *frame)
{
    const char *comma = strchr(line,',');
    if (comma == NULL || comma == line || (size_t)(comma - line) >= sizeof(frame->type))
    {
        return false;
    }
    size_t type_length = (size_t)(comma - line);  // 计算类型字符串长度。
    memcpy(frame->type, line, type_length);  // 拷贝类型字段。
    frame->type[type_length] = '\0';  // 为 strcmp 补上 NUL。
    const char *field = comma + 1;  // 指到节点号字段。
    if (!read_u16(&field, &frame->node) || *field != ',')
    {  // 检查节点号及其后的逗号。
        return false;  // 节点号无效。
    }  // 准备解析序号。
    ++field;  // 跳过节点号末尾的逗号。
    if (!read_u16(&field, &frame->seq))
    {  // 读取请求序号。
        return false;  // 序号无效。
    }  // 序号合法。
    if (*field == '\0')
    {  // PING、PONG、GET 没有正文。
        frame->payload = NULL;  // 用空指针表示正文缺失。
        return true;  // 接收该帧头。
    }  // 还有字符时需要正文分隔符。
    if (*field != ',' || field[1] == '\0')
    {  // 正文前应有逗号，正文自身不能为空。
        return false;  // 拒绝异常尾部。
    }  // 正文格式初步有效。
    frame->payload = field + 1;  // 正文仍指向调用者的行缓冲。
    return true;  // 帧头解析完成。
}
