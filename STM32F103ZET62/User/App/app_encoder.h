/**
 * @file    app_encoder.h
 * @brief   正交编码器周期采样与发布应用接口。
 */

#ifndef APP_ENCODER_H
#define APP_ENCODER_H

#include <stdint.h>

/**
 * @brief  启动编码器定时器并初始化采样与日志计时器。
 * @param  now_ms 当前系统毫秒时间戳。
 */
void app_encoder_init(uint32_t now_ms);

/**
 * @brief  主循环轮询，按 10 ms 采样编码器并每 100 ms 发布一次累计值。
 * @param  now_ms 当前系统毫秒时间戳。
 */
void app_encoder_run(uint32_t now_ms);

#endif  /* APP_ENCODER_H */
