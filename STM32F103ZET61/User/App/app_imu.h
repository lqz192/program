/**
 * @file    app_imu.h
 * @brief   MPU6050 六轴周期采集与发布应用接口。
 */

#ifndef APP_IMU_H
#define APP_IMU_H

/**
 * @brief  初始化 I2C1 上的 MPU6050 传感器并记录起始时间。
 */
void app_imu_init(void);

/**
 * @brief  主循环轮询，按周期采样六轴数据或在故障后定时重连。
 */
void app_imu_run(void);

#endif  /* APP_IMU_H */
