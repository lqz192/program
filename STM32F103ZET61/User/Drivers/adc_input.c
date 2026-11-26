/**
 * @file    adc_input.c
 * @brief   ADC1 单通道（PA1）校准与单次采样驱动实现。
 */

#include "adc_input.h"
#include "adc.h"

#define ADC_POLL_TIMEOUT_MS 10U  // 轮询单次转换完成的超时时间，单位 ms。

HAL_StatusTypeDef adc_input_calibrate(void)
{
    return HAL_ADCEx_Calibration_Start(&hadc1);
}

HAL_StatusTypeDef adc_input_read(uint32_t *raw)
{
    HAL_StatusTypeDef status = HAL_ADC_Start(&hadc1);
    if (status != HAL_OK)
    {
        return status;
    }

    status = HAL_ADC_PollForConversion(&hadc1, ADC_POLL_TIMEOUT_MS);
    if (status == HAL_OK)
    {
        *raw = HAL_ADC_GetValue(&hadc1);
    }

    HAL_StatusTypeDef stop_status = HAL_ADC_Stop(&hadc1);
    return (status == HAL_OK) ? stop_status : status;
}
