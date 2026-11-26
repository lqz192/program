/**
 * @file    encoder.h
 * @brief   TIM8 正交编码器计数驱动接口。
 */

#ifndef ENCODER_H
#define ENCODER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/**
 * @brief  清零软件与硬件计数状态并启动 TIM8 编码器双通道。
 * @retval HAL_StatusTypeDef HAL 启动状态。
 */
HAL_StatusTypeDef encoder_start(void);

/**
 * @brief  读取当前累计脉冲数（带 16 位硬件计数器溢出回卷扩展）。
 * @retval int32_t 累计有符号计数值。
 */
int32_t encoder_read_count(void);

#endif  /* ENCODER_H */
