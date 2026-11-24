#include "app_node_link.h"  // 本模块的公开接口。
#include "link_uart.h"  // 使用本板的 UART 通信接口。
#include "usart.h"  // 引入 F103 上的 huart2。
#include <stdio.h>  // 使用 snprintf 格式化响应。
#include <string.h>  // 使用 strcspn、memcpy 和 strcmp。

static const uint32_t sample_expire_ms = 500U;
static link_uart node_link;
static uint16_t local_node;
static bool link_ready;

static struct
{
    char text[96];
    uint32_t sample_ms;
}records[2];

void app_node_sample_update(node_slot slot, const char *line)  // 原采样模块在主循环更新缓存。
{  // 调用方传入一个合法的槽位。
    if (line == NULL)
    {  // 传感器明确报错或结果无效。
        records[slot].text[0] = '\0';  // 立即撤销该槽的有效值。
        return;  // 等待下一条有效样本。
    }  // 下面保存文本。
    size_t length = strcspn(line, "\r\n");  // 去掉原有串口日志末尾的换行。
    if (length >= sizeof(records[slot].text))
    {  // 确保能保存完整记录。
        records[slot].text[0] = '\0';  // 超长记录标为无效。
        return;  // 下一次采样可覆盖它。
    }  // 缓存空间足够。
    memcpy(records[slot].text, line, length);  // 复制采样字段。
    records[slot].text[length] = '\0';  // 添加字符串结束符。
    records[slot].sample_ms = HAL_GetTick();  // 用于判断样本是否过期。
}  // 采样缓存更新完成。

static const char *sample_text(node_slot slot, uint32_t now_ms)  // 按当前时刻取有效数据。
{  // 使用无符号时间差，支持毫秒计数回卷。
    if (records[slot].text[0] == '\0' || now_ms - records[slot].sample_ms > sample_expire_ms)
    {  // 检查样本有效性。
        return "NA";  // 继续应答请求并标记缺失值。
    }  // 数据可使用。
    return records[slot].text;  // 返回最近的有效采样记录。
}  // 结束有效性检查。

void app_node_link_init(uint16_t node_id)  // 在 MX_USART2_UART_Init 后调用。
{  // 初始化只运行一次。
    local_node = node_id;  // 保存节点编号。
    link_ready = link_uart_start(&node_link, &huart2);  // 在 USART2 上启动字节接收中断。
}  // 结束节点初始化。

void app_node_link_run(void)
{
    char line[LINK_LINE_CAPACITY];  // 保存接收的请求行和随后生成的响应行。
    link_frame request;  // 保存解析后的请求帧头。
    if (!link_ready || !link_uart_take(&node_link, line, NULL) || !link_parse(line, &request))
    {
        return;
    }
    if (request.node != local_node || request.payload != NULL)
    {  // 本节点只接无正文的请求。
        return;  // 忽略其他节点或有异常正文的帧。
    }  // 该请求属于本节点。
    int length;
    if (strcmp(request.type, "PING") == 0)
    {  // 链路探测请求。
        length = snprintf(line, sizeof(line), "PONG,%u,%u", (unsigned)local_node, (unsigned)request.seq);  // 回显请求序号。
    }
    else if (strcmp(request.type, "GET") == 0)
    {  // 读取两条采样记录。
        uint32_t now_ms = HAL_GetTick();  // 在同一时刻判断两条样本是否有效。
        length = snprintf(line, sizeof(line), "DATA,%u,%u,%s|%s", (unsigned)local_node, (unsigned)request.seq,  // 构造响应帧头。
                          sample_text(node_first, now_ms), sample_text(node_second, now_ms));  // 追加两条缓存记录。
    }
    else
    {  // 命令类型未知。
        return;  // 忽略并继续采样。
    }  // 应答已准备好。
    if (length > 0 && (size_t)length < sizeof(line))
    {  // 确认应答完整。
        (void)link_uart_send(&huart2, line);  // 在请求对应的板间串口回复。
    }  // 没有发出的应答由 F767 超时处
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)  // HAL 串口接收完成回调。
{  // 每个工程只能有一份此回调定义。
    if (uart == &huart2)
    {  // 仅处理协议串口。
        link_uart_on_rx(&node_link);  // 传入字节并继续接收。
    }  // 其他串口留给项目已有处理。
}  // 接收分派结束。

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)  // HAL 串口错误回调。
{  // 每个工程只能有一份此回调定义。
    if (uart == &huart2)
    {  // 仅恢复协议串口。
        link_uart_on_error(&node_link);  // 舍弃损坏的行并继续监听。
    }  // 串口分派结束。
}  // 错误分派结束。
