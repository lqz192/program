/**
 * @file    mpu6050.h
 * @brief   MPU6050 六轴传感器 I2C 驱动接口。
 */

#ifndef MPU6050_H
#define MPU6050_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/**
 * @brief  MPU6050 操作返回状态。
 */
typedef enum
{
    mpu6050_ok = 0,     // 通信与操作成功。
    mpu6050_i2c_error,  // HAL I2C 读写失败。
    mpu6050_bad_id      // WHO_AM_I 与预期值 0x68 不符。
} mpu6050_status;

/**
 * @brief  MPU6050 单帧原始传感器数据。
 */
typedef struct
{
    int16_t ax;         // X 轴加速度原始值。
    int16_t ay;         // Y 轴加速度原始值。
    int16_t az;         // Z 轴加速度原始值。
    int16_t gx;         // X 轴角速度原始值。
    int16_t gy;         // Y 轴角速度原始值。
    int16_t gz;         // Z 轴角速度原始值。
    int16_t temp_raw;   // 芯片内部温度原始值。
} mpu6050_data;

/**
 * @brief  校验 MPU6050 器件 ID 并配置电源与采样参数。
 * @param  i2c         HAL I2C 句柄指针。
 * @param  device_addr 7 位 I2C 从机地址（AD0 接地为 0x68，接高为 0x69）。
 * @retval mpu6050_status 初始化状态。
 */
mpu6050_status mpu6050_init(I2C_HandleTypeDef *i2c, uint8_t device_addr);

/**
 * @brief  连续读取 MPU6050 六轴与温度共 14 字节原始数据。
 * @param  i2c         HAL I2C 句柄指针。
 * @param  device_addr 7 位 I2C 从机地址。
 * @param  out         输出数据结构指针（读取失败时保持原值不变）。
 * @retval mpu6050_status 读取结果状态。
 */
mpu6050_status mpu6050_read_data(I2C_HandleTypeDef *i2c, uint8_t device_addr,
                                 mpu6050_data *out);

#endif  /* MPU6050_H */
