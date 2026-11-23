#include "adc_input.h"
#include "adc.h"

//再初始化外设完成后执行一次校准
HAL_StatusTypeDef adc_input_calibrate(void)
{
    return HAL_ADCEx_Calibration_Start(&hadc1);
}

HAL_StatusTypeDef adc_input_read(uint32_t *raw)
{
    // 读取一次 PA1 的 12 位原始值。
    HAL_StatusTypeDef status = HAL_ADC_Start(&hadc1);  // 软件触发一次规则组转换。
    if (status != HAL_OK) {  // 启动失败时，本轮无法取得有效结果。
        return status;  // 把错误交给调用者。
    }

    status = HAL_ADC_PollForConversion(&hadc1, 10);//等待准换完成，10us超时
    if (status == HAL_OK)
    {
        *raw = HAL_ADC_GetValue(&hadc1);
    }//等待失败不使用原始值
    HAL_StatusTypeDef stop_status = HAL_ADC_Stop(&hadc1);
    return status == HAL_OK ? HAL_OK : stop_status;
}