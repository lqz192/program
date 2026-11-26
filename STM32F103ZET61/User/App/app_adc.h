/**
 * @file    app_adc.h
 * @brief   ADC 周期采集与发布应用接口。
 */

#ifndef APP_ADC_H
#define APP_ADC_H

/**
 * @brief  校准 ADC1 并记录初始化时间戳。
 */
void app_adc_init(void);

/**
 * @brief  主循环轮询，按周期采样 PA1 电压或在校准失败后定时重试。
 */
void app_adc_run(void);

#endif  /* APP_ADC_H */
