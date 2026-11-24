#include "app_hub.h"

#include <stdint.h>

#include "link_uart.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

typedef struct {  // 每条链路持有独立的调度状态。
    link_uart link;  // 接收状态和外设句柄。
    uint16_t node;  // 对端节点号。
    uint16_t seq;  // 本节点的请求序号。
    uint32_t last_request_ms;  // 上一次安排请求的时间。
    uint32_t sent_ms;  // 当前请求发送完成时间。
    bool enabled;  // 接收中断是否初始化成功。
    bool waiting;  // 是否正在等待此序号的应答。
    bool probed;  // 收到匹配的 PONG 后开始 GET。
} hub_node;  // 一条 F103 链路的上下文。

static hub_node nodes[2];
static const uint32_t poll_period_ms = 100U;
static const uint32_t response_timeout_ms = 75U;

static void log_event(const char *event, const hub_node *node)  // 把链路状态输出到电脑。
{  // 格式化一条包含节点和序号的诊断。
    char line[48];  // 容纳诊断名和最大序号。
    int length = snprintf(line, sizeof(line), "%s n=%u seq=%u", event, (unsigned)node->node, (unsigned)node->seq);  // 生成日志。
    if (length > 0 && (size_t)length < sizeof(line))
    {  // 只输出完整日志。
        (void)link_uart_send(&huart1, line);  // 通过 CH340 发送到电脑。
    }  // 完成诊断输出。
}  // 日志函数结束。

void app_hub_init(void)
{
    uint32_t now_ms = HAL_GetTick();  // 读取共同的起始时刻。
    nodes[0].node = 1U;  // USART2 接节点 1。
    nodes[0].last_request_ms = now_ms - poll_period_ms;  // 让第一路立即到期。
    nodes[0].enabled = link_uart_start(&nodes[0].link, &huart2);  // 开启 USART2 接收。
    nodes[1].node = 2U;  // USART6 接节点 2。
    nodes[1].last_request_ms = now_ms - poll_period_ms / 2U;  // 第二路稍后到期。
    nodes[1].enabled = link_uart_start(&nodes[1].link, &huart6);  // 开启 USART6 接收。
    for (unsigned index = 0U; index < 2U; ++index)
    {  // 检查两路启动状态。
        if (!nodes[index].enabled)
        {  // 某一路接收启动失败。
            log_event("RX_START_ERR", &nodes[index]);  // 标记失败节点并保留另一条链路。
        }  // 处理完该节点的启动结果。
    }  // 完成启动检查。

}

static void service_node(hub_node *node, uint32_t now_ms)
{
    if (!node->enabled)
    {  // 跳过接收初始化失败的串口。
        return;  // 让另一路继续服务。
    }  // 当前链路可用。
    char line[LINK_LINE_CAPACITY];  // 用同一缓冲接收应答或构造请求。
    uint32_t received_ms;  // 由 UART 接收回调记录。
    link_frame reply;  // 保存已经解析的应答头。
    if (link_uart_take(&node->link, line, &received_ms) && node->waiting &&
        received_ms - node->sent_ms < response_timeout_ms && link_parse(line, &reply) &&
        reply.node == node->node && reply.seq == node->seq)
    {  // 只接纳匹配当前请求的响应。
        bool pong = !node->probed && strcmp(reply.type, "PONG") == 0 && reply.payload == NULL;  // 验证探测应答。
        bool data = node->probed && strcmp(reply.type, "DATA") == 0 && reply.payload != NULL &&  // 验证数据应答。
                    strchr(reply.payload, '|') != NULL;  // 检查两条样本有分隔符。
        if (pong || data)
        {  // 收到预期的应答类型。
            node->waiting = false;  // 完成本次请求。
            node->probed = true;  // 后续按数据请求模式轮询。
            (void)link_uart_send(&huart1, line);  // 把原应答转发给电脑。
        }  // 畸形应答继续等待，最终由超时处理。
    }  // 完成本轮接收检查。
    if (node->waiting && now_ms - node->sent_ms >= response_timeout_ms)
    {
        node->waiting = false;  // 当前序号失效。
        log_event("TIMEOUT", node);  // 未收到在期限内到达的匹配应答。
    }
    if (node->waiting || now_ms - node->last_request_ms < poll_period_ms)
    {  // 每路最多一条请求在途。
        return;  // 等现有请求完成或下个周期。
    }  // 本轮可以安排新请求。
    node->last_request_ms = now_ms;  // 记录本次请求调度时刻。
    ++node->seq;  // 递增 uint16_t 序号，允许自然回卷。
    int length = snprintf(line, sizeof(line), "%s,%u,%u", node->probed ? "GET" : "PING",  // 选择数据或探测请求。
                          (unsigned)node->node, (unsigned)node->seq);  // 加入目标节点和请求序号。
    if (length > 0 && (size_t)length < sizeof(line) && link_uart_send(node->link.uart, line))
    {  // 发送完整请求。
        node->sent_ms = HAL_GetTick();  // 发送完成后开始等待计时。
        node->waiting = true;  // 等待匹配的应答。
    }
    else
    {  // 格式化或串口发送失败。
        log_event("TX_ERR", node);  // 记录失败并在下个周期重试。
    }  // 完成发送结果处理。
}

void app_hub_run(void)  // 主循环连续调用。
{  // 用每条链路实际被处理时的时间调度。
    service_node(&nodes[0], HAL_GetTick());  // 处理 USART2 与节点 1。
    service_node(&nodes[1], HAL_GetTick());  // 处理 USART6 与节点 2。
}  // 两条链路都已处理。

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)  // HAL 字节接收回调。
{  // 按实际 UART 分派。
    if (uart == &huart2)
    {  // 节点 1 的链路。
        link_uart_on_rx(&nodes[0].link);  // 保存字节并继续接收。
    }
    else if (uart == &huart6)
    {  // 节点 2 的链路。
        link_uart_on_rx(&nodes[1].link);  // 保存字节并继续接收。
    }  // 忽略其他串口的回调。
}  // 接收分派结束。

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)  // HAL 串口错误回调。
{  // 按发生错误的 UART 恢复。
    if (uart == &huart2)
    {  // 节点 1 的链路。
        link_uart_on_error(&nodes[0].link);  // 丢弃损坏的行。
    }
    else if (uart == &huart6)
    {  // 节点 2 的链路。
        link_uart_on_error(&nodes[1].link);  // 丢弃损坏的行。
    }  // 其余串口按项目已有逻辑处理。
}  // 错误分派结束。
