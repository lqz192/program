/**
 * @file    adc_input.h
 * @brief   ADC1 单通道（PA1）校准与单次采样驱动接口。
 */

#ifndef ADC_INPUT_H
#define ADC_INPUT_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/**
 * @brief  执行 ADC1 自校准（应在 MX_ADC1_Init 完成后调用）。
 * @retval HAL_StatusTypeDef HAL 校准执行状态。
 */
HAL_StatusTypeDef adc_input_calibrate(void);

/**
 * @brief  软件触发一次 ADC1 规则通道采样并读取 12 位原始值。
 * @param  raw 输出 12 位原始转换码指针。
 * @retval HAL_StatusTypeDef HAL 启动/轮询/停止状态。
 */
HAL_StatusTypeDef adc_input_read(uint32_t *raw);

#endif  /* ADC_INPUT_H */
