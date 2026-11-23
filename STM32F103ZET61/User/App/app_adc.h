#ifndef APP_ADC_H  // 第一次包含时处理下面的声明。
#define APP_ADC_H  // 防止重复声明。

void app_adc_init(void);    // 外设初始化完成后，校准 ADC1 一次。
void app_adc_run(void);     // 到达采集周期时触发 ADC 并输出。

#endif
