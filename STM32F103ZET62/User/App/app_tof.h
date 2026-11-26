/**
 * @file    app_tof.h
 * @brief   VL53L0X 激光测距周期轮询与发布应用接口。
 */

#ifndef APP_TOF_H
#define APP_TOF_H

#include <stdint.h>

/**
 * @brief  初始化 VL53L0X 传感器并记录起始时间戳。
 */
void app_tof_init(void);

/**
 * @brief  主循环轮询测距结果，故障时按周期自动重连。
 * @param  now_ms 当前系统毫秒时间戳。
 */
void app_tof_run(uint32_t now_ms);

#endif  /* APP_TOF_H */
