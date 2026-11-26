/**
 * @file    app_node_link.h
 * @brief   F103 从节点板间通信与双槽采样缓存应用接口。
 */

#ifndef APP_NODE_LINK_H
#define APP_NODE_LINK_H

#include <stdint.h>

/**
 * @brief  从节点上报给 Hub 的两个传感器数据槽位。
 */
typedef enum
{
    node_first = 0,     // 第 1 路传感器数据槽位。
    node_second = 1     // 第 2 路传感器数据槽位。
} node_slot;

/**
 * @brief  初始化板间通信节点并启动 USART2 接收中断。
 * @param  node_id 本机在总线协议中的节点编号。
 */
void app_node_link_init(uint16_t node_id);

/**
 * @brief  主循环轮询处理 Hub 发来的 PING / GET 请求并回传响应。
 */
void app_node_link_run(void);

/**
 * @brief  更新指定传感器槽位的最新采样文本与时间戳。
 * @param  slot 目标槽位（node_first 或 node_second）。
 * @param  line 采样文本（自动裁剪末尾换行符）；传入 NULL 表示清除该槽有效数据。
 */
void app_node_sample_update(node_slot slot, const char *line);

#endif  /* APP_NODE_LINK_H */
