/**
 * @file    app_ms5837.h
 * @brief   MS5837-02BA 传感器非阻塞周期采集与发布应用接口。
 */

#ifndef APP_MS5837_H
#define APP_MS5837_H

/**
 * @brief  初始化 I2C2 上的 MS5837-02BA 传感器并记录起始时间。
 */
void app_ms5837_init(void);

/**
 * @brief  主循环轮询，按周期非阻塞采样 MS5837-02BA 或定时重试初始化。
 */
void app_ms5837_run(void);

#endif  /* APP_MS5837_H */
