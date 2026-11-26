/**
 * @file    app_lps22hb.h
 * @brief   LPS22HB 传感器周期采集与发布应用接口。
 */

#ifndef APP_LPS22HB_H
#define APP_LPS22HB_H

/**
 * @brief  初始化 I2C1 上的 LPS22HB 传感器并记录起始时间。
 */
void app_lps22hb_init(void);

/**
 * @brief  主循环轮询，按周期采样 LPS22HB 或在故障后定时重试初始化。
 */
void app_lps22hb_run(void);

#endif  /* APP_LPS22HB_H */
