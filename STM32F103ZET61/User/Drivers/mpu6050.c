/**
 * @file    mpu6050.c
 * @brief   MPU6050 六轴传感器 I2C 驱动实现。
 */

#include "mpu6050.h"

#define MPU6050_REG_SMPLRT_DIV      0x19U   // 采样配置起始寄存器（0x19~0x1C）。
#define MPU6050_REG_ACCEL_XOUT_H    0x3BU   // 六轴与温度输出起始寄存器（14 字节）。
#define MPU6050_REG_PWR_MGMT_1      0x6BU   // 电源管理寄存器起始地址（0x6B~0x6C）。
#define MPU6050_REG_WHO_AM_I        0x75U   // 器件身份寄存器。
#define MPU6050_WHO_AM_I_VALUE      0x68U   // WHO_AM_I 固定返回值（与 AD0 无关）。
#define MPU6050_I2C_TIMEOUT_MS      20U     // 单次阻塞式 I2C 事务超时时间，单位 ms。

/**
 * @brief  将大端序高低字节组合还原为有符号 16 位整数。
 */
static int16_t decode_int16(uint8_t high, uint8_t low)
{
    int32_t value = (int32_t)(((uint32_t)high << 8U) | low);
    return (int16_t)(value >= 0x8000 ? value - 0x10000 : value);
}

mpu6050_status mpu6050_init(I2C_HandleTypeDef *i2c, uint8_t device_addr)
{
    uint16_t hal_addr = (uint16_t)(device_addr << 1U);
    uint8_t power[] = { 0x01U, 0x00U };  // 0x6B 唤醒并选 PLL 时钟，0x6C 使能全部六轴。
    uint8_t config[] = {
        0x09U,  // 0x19 SMPLRT_DIV：1 kHz / (1 + 9) = 100 Hz。
        0x03U,  // 0x1A CONFIG：DLPF 带宽档位 3。
        0x00U,  // 0x1B GYRO_CONFIG：量程 ±250 °/s。
        0x00U   // 0x1C ACCEL_CONFIG：量程 ±2 g。
    };

    uint8_t device_id = 0U;
    if (HAL_I2C_Mem_Read(i2c, hal_addr, MPU6050_REG_WHO_AM_I, I2C_MEMADD_SIZE_8BIT,
                         &device_id, 1U, MPU6050_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return mpu6050_i2c_error;
    }
    if (device_id != MPU6050_WHO_AM_I_VALUE)
    {
        return mpu6050_bad_id;
    }

    if (HAL_I2C_Mem_Write(i2c, hal_addr, MPU6050_REG_PWR_MGMT_1, I2C_MEMADD_SIZE_8BIT,
                          power, sizeof(power), MPU6050_I2C_TIMEOUT_MS) != HAL_OK ||
        HAL_I2C_Mem_Write(i2c, hal_addr, MPU6050_REG_SMPLRT_DIV, I2C_MEMADD_SIZE_8BIT,
                          config, sizeof(config), MPU6050_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return mpu6050_i2c_error;
    }

    // 唤醒后的稳定时间由上层应用的采样节拍非阻塞保证，此处不阻塞主循环。
    return mpu6050_ok;
}

mpu6050_status mpu6050_read_data(I2C_HandleTypeDef *i2c, uint8_t device_addr,
                                 mpu6050_data *out)
{
    uint8_t data[14];  // 加速度(6B) + 温度(2B) + 角速度(6B)。
    if (HAL_I2C_Mem_Read(i2c, (uint16_t)(device_addr << 1U), MPU6050_REG_ACCEL_XOUT_H,
                         I2C_MEMADD_SIZE_8BIT, data, sizeof(data), MPU6050_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return mpu6050_i2c_error;
    }

    out->ax = decode_int16(data[0], data[1]);
    out->ay = decode_int16(data[2], data[3]);
    out->az = decode_int16(data[4], data[5]);
    out->temp_raw = decode_int16(data[6], data[7]);
    out->gx = decode_int16(data[8], data[9]);
    out->gy = decode_int16(data[10], data[11]);
    out->gz = decode_int16(data[12], data[13]);
    return mpu6050_ok;
}
