#ifndef ADC_INPUT_H  // 第一次包含时导出驱动接口。
#define ADC_INPUT_H  // 防止重复包含。

#include <stdint.h>  // 提供原始值类型 uint32_t。
#include "stm32f1xx_hal.h"  // 提供 HAL_StatusTypeDef。

HAL_StatusTypeDef adc_input_calibrate(void);
HAL_StatusTypeDef adc_input_read(uint32_t *raw);


#endif
