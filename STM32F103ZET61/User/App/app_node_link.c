/**
 * @file    app_node_link.c
 * @brief   F103 从节点板间通信与双槽采样缓存应用实现。
 */

#include "app_node_link.h"
#include "link_uart.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define NODE_SAMPLE_EXPIRE_MS   500U    // 缓存样本的有效期，超时后对 Hub 上报 "NA"。
#define NODE_SAMPLE_TEXT_SIZE   96U     // 单槽位缓存文本的最大长度（含 NUL）。

/**
 * @brief  单槽采样缓存记录。
 */
typedef struct
{
    char text[NODE_SAMPLE_TEXT_SIZE];   // 去除末尾换行后的传感器文本，空串表示无效。
    uint32_t sample_ms;                 // 最近一次更新的毫秒时间戳。
} node_sample_record;

static link_uart node_link;                     // 板间协议串口（USART2）接收上下文。
static uint16_t local_node;                     // 本机节点编号。
static bool link_ready;                         // USART2 接收中断初始化状态。
static node_sample_record records[2];           // 两路传感器的最新采样缓存。

/**
 * @brief  获取指定槽位的有效样本文本；若已失效或超时则返回 "NA"。
 * @param  slot   目标槽位。
 * @param  now_ms 当前系统毫秒时间。
 * @retval 指向样本字符串或常量 "NA" 的指针。
 */
static const char *sample_text(node_slot slot, uint32_t now_ms)
{
    if (slot > node_second ||
        records[slot].text[0] == '\0' ||
        now_ms - records[slot].sample_ms > NODE_SAMPLE_EXPIRE_MS)
    {
        return "NA";
    }
    return records[slot].text;
}

void app_node_sample_update(node_slot slot, const char *line)
{
    if (slot > node_second)
    {
        return;
    }
    if (line == NULL)
    {
        records[slot].text[0] = '\0';  // 传感器出错时立即撤销旧缓存。
        return;
    }

    size_t length = strcspn(line, "\r\n");  // 剥离本地调试输出末尾的换行符。
    if (length >= sizeof(records[slot].text))
    {
        records[slot].text[0] = '\0';  // 超长文本视为异常并置为无效。
        return;
    }

    memcpy(records[slot].text, line, length);
    records[slot].text[length] = '\0';
    records[slot].sample_ms = HAL_GetTick();
}

void app_node_link_init(uint16_t node_id)
{
    local_node = node_id;
    link_ready = link_uart_start(&node_link, &huart2);
}

void app_node_link_run(void)
{
    char line[LINK_LINE_CAPACITY];
    link_frame request;

    // 先调用 link_uart_take() 驱动低频接收恢复，避免启动失败时被 link_ready 短路。
    bool has_line = link_uart_take(&node_link, line, NULL);
    link_ready = !node_link.rx_restart_needed;
    if (!link_ready || !has_line || !link_parse(line, &request))
    {
        return;
    }
    if (request.node != local_node || request.payload != NULL)
    {
        return;  // 仅响应发往本节点且不携带多余正文的请求帧。
    }

    int length;
    if (strcmp(request.type, "PING") == 0)
    {
        length = snprintf(line, sizeof(line), "PONG,%u,%u",
                          (unsigned)local_node, (unsigned)request.seq);
    }
    else if (strcmp(request.type, "GET") == 0)
    {
        uint32_t now_ms = HAL_GetTick();  // 统一时间基准判断双槽数据是否过期。
        length = snprintf(line, sizeof(line), "DATA,%u,%u,%s|%s",
                          (unsigned)local_node, (unsigned)request.seq,
                          sample_text(node_first, now_ms), sample_text(node_second, now_ms));
    }
    else
    {
        return;
    }

    if (length > 0 && (size_t)length < sizeof(line))
    {
        (void)link_uart_send(&huart2, line);  // 未发出的异常应答由 Hub 端超时兜底。
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart2)
    {
        link_uart_on_rx(&node_link);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart2)
    {
        link_uart_on_error(&node_link);
    }
}
