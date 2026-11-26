/**
 * @file    app_hub.h
 * @brief   STM32F767 主控三路 F103 节点轮询调度器接口。
 */

#ifndef APP_HUB_H
#define APP_HUB_H

/**
 * @brief  初始化三路从节点链路（USART2/USART6/USART3）并启动中断接收。
 */
void app_hub_init(void);

/**
 * @brief  主循环轮询各从节点链路状态，处理应答转发、超时判定与周期请求。
 */
void app_hub_run(void);

#endif  /* APP_HUB_H */
