/**
 * @file    app_hub.c
 * @brief   STM32F767 主控三路 F103 节点轮询调度器实现。
 */

#include "app_hub.h"
#include "link_uart.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define HUB_POLL_PERIOD_MS          100U    // 单路节点轮询周期，单位 ms。
#define HUB_RESPONSE_TIMEOUT_MS     75U     // 单次请求等待应答的超时阈值，单位 ms。
#define HUB_MAX_TIMEOUTS            5U      // 连续超时达到此次数后退回 PING 重新握手。
#define HUB_NODE_COUNT              (sizeof(nodes) / sizeof(nodes[0]))

/**
 * @brief  单路 F103 节点链路的调度上下文。
 */
typedef struct
{
    link_uart link;             // 串口中断接收上下文与外设句柄。
    uint16_t node;              // 对端从节点编号。
    uint16_t seq;               // 当前请求序号（允许 uint16_t 自然回卷）。
    uint32_t last_request_ms;   // 上一次发送/调度请求的毫秒时刻。
    uint32_t sent_ms;           // 本轮请求发送完成的毫秒时刻。
    bool enabled;               // 串口接收中断初始化成功标志。
    bool waiting;               // 是否正在等待当前序号的应答。
    bool probed;                // true 表示已通过 PING/PONG 握手，进入 GET 模式。
    uint8_t timeout_count;      // 连续超时计数器。
} hub_node;

/**
 * @brief  三路串口节点的静态硬件与交错起始配置表。
 */
typedef struct
{
    UART_HandleTypeDef *uart;   // 链路绑定的 HAL UART 句柄。
    uint16_t node_id;           // 目标节点编号。
    uint32_t init_lead_ms;      // 初始化时的时间提前量，用于错开多路轮询相位。
} hub_node_config;

static const hub_node_config node_configs[] = {
    { &huart2, 1U, HUB_POLL_PERIOD_MS },        // 节点 1（USART2）：立即发起首次轮询。
    { &huart6, 2U, HUB_POLL_PERIOD_MS / 2U },   // 节点 2（USART6）：错开约 50 ms。
    { &huart3, 3U, HUB_POLL_PERIOD_MS / 4U }    // 节点 3（USART3）：错开约 75 ms。
};

static hub_node nodes[sizeof(node_configs) / sizeof(node_configs[0])];

/**
 * @brief  通过串口句柄查找对应的节点上下文。
 * @param  uart HAL UART 句柄指针。
 * @retval 匹配的节点指针；未找到时返回 NULL。
 */
static hub_node *find_node_by_uart(const UART_HandleTypeDef *uart)
{
    for (size_t index = 0U; index < HUB_NODE_COUNT; ++index)
    {
        if (nodes[index].link.uart == uart)
        {
            return &nodes[index];
        }
    }
    return NULL;
}

/**
 * @brief  向 PC 调试串口（USART1）输出一条链路诊断事件。
 */
static void log_event(const char *event, const hub_node *node)
{
    char line[48];
    int length = snprintf(line, sizeof(line), "%s n=%u seq=%u",
                          event, (unsigned)node->node, (unsigned)node->seq);
    if (length > 0 && (size_t)length < sizeof(line))
    {
        (void)link_uart_send(&huart1, line);
    }
}

/**
 * @brief  推进单路节点的接收匹配、超时处理与下轮请求调度。
 */
static void service_node(hub_node *node, uint32_t now_ms)
{
    char line[LINK_LINE_CAPACITY];
    uint32_t received_ms = 0U;
    link_frame reply;

    // 即使首次启动接收失败，也必须进入 link_uart_take() 执行低频恢复重试。
    bool has_line = link_uart_take(&node->link, line, &received_ms);
    node->enabled = !node->link.rx_restart_needed;
    if (!node->enabled)
    {
        return;
    }

    if (has_line && node->waiting &&
        received_ms - node->sent_ms < HUB_RESPONSE_TIMEOUT_MS &&
        link_parse(line, &reply) &&
        reply.node == node->node && reply.seq == node->seq)
    {
        bool is_pong = !node->probed && strcmp(reply.type, "PONG") == 0 && reply.payload == NULL;
        bool is_data = node->probed && strcmp(reply.type, "DATA") == 0 &&
                       reply.payload != NULL && strchr(reply.payload, '|') != NULL;
        if (is_pong || is_data)
        {
            node->waiting = false;
            node->probed = true;
            node->timeout_count = 0U;
            (void)link_uart_send(&huart1, line);  // 将合法应答原样转发至上位机。
        }
    }

    if (node->waiting && now_ms - node->sent_ms >= HUB_RESPONSE_TIMEOUT_MS)
    {
        node->waiting = false;
        ++node->timeout_count;
        if (node->timeout_count >= HUB_MAX_TIMEOUTS)
        {
            node->probed = false;  // 连续多次超时后退回 PING，以探测节点重启或重新上线。
            node->timeout_count = 0U;
        }
        log_event("TIMEOUT", node);
    }

    if (node->waiting || now_ms - node->last_request_ms < HUB_POLL_PERIOD_MS)
    {
        return;
    }

    node->last_request_ms = now_ms;
    ++node->seq;
    int length = snprintf(line, sizeof(line), "%s,%u,%u",
                          node->probed ? "GET" : "PING",
                          (unsigned)node->node, (unsigned)node->seq);
    if (length > 0 && (size_t)length < sizeof(line) &&
        link_uart_send(node->link.uart, line))
    {
        node->sent_ms = HAL_GetTick();
        node->waiting = true;
    }
    else
    {
        log_event("TX_ERR", node);
    }
}

void app_hub_init(void)
{
    uint32_t now_ms = HAL_GetTick();

    for (size_t index = 0U; index < HUB_NODE_COUNT; ++index)
    {
        nodes[index].node = node_configs[index].node_id;
        nodes[index].last_request_ms = now_ms - node_configs[index].init_lead_ms;
        nodes[index].enabled = link_uart_start(&nodes[index].link, node_configs[index].uart);
        if (!nodes[index].enabled)
        {
            log_event("RX_START_ERR", &nodes[index]);
        }
    }
}

void app_hub_run(void)
{
    for (size_t index = 0U; index < HUB_NODE_COUNT; ++index)
    {
        service_node(&nodes[index], HAL_GetTick());
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    hub_node *node = find_node_by_uart(uart);
    if (node != NULL)
    {
        link_uart_on_rx(&node->link);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    hub_node *node = find_node_by_uart(uart);
    if (node != NULL)
    {
        link_uart_on_error(&node->link);
    }
}
