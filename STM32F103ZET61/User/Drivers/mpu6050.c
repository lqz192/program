#include "mpu6050.h"  // 让函数实现使用头文件中的类型和声明。

#define REG_WHO_AM_I     0x75U  // 芯片身份寄存器。
#define REG_PWR_MGMT_1   0x6BU  // 两个电源管理寄存器的起始地址。
#define REG_SMPLRT_DIV   0x19U  // 四个采样配置寄存器的起始地址。
#define REG_ACCEL_XOUT_H 0x3BU  // 六轴与温度输出寄存器的起始地址。
#define I2C_TIMEOUT_MS   20U    // 传给每次阻塞式 HAL I²C 调用的超时参数。

static int16_t decode_int16( uint8_t high ,uint8_t low )
{
    int32_t value = ((uint32_t) high << 8 ) | low;//高字节放高8位，低字节放低8位
    return (int16_t)(value >= 0x8000 ? value - 0x10000 : value);  // 按 16 位补码还原负数。
}

mpu6050_status mpu6050_init(I2C_HandleTypeDef *i2c, uint8_t device_addr)
{
    uint16_t hal_addr = (uint16_t) (device_addr << 1);//STM32F1 HAL 接收左移一位后的设备地址。
    uint8_t power[] = {0x01U, 0x00U};  // 0x6B 退出睡眠并选择 PLL，0x6C 清除六轴待机位。
    uint8_t config[] = {  // 按 0x19～0x1C 的顺序存放要写入的值。
        0x09U,  // 0x19，1 kHz 除以 (1 + 9)，寄存器更新频率为 100 Hz。
        0x03U,  // 0x1A，选择数字低通滤波档位 3。
        0x00U,  // 0x1B，选择陀螺仪量程 ±250 °/s。
        0x00U   // 0x1C，选择加速度计量程 ±2 g。
    };  // 这些值分别写入四个连续寄存器。

    uint8_t device_id;  // 接收 WHO_AM_I 寄存器返回的一个字节。

    if (HAL_I2C_Mem_Read(i2c, hal_addr, REG_WHO_AM_I,
        I2C_MEMADD_SIZE_8BIT,&device_id, 1U, I2C_TIMEOUT_MS)!=HAL_OK)
    {
        return mpu6050_i2c_error;
    }
    if (device_id != 0x68U) {  // AD0 接高时，WHO_AM_I 的期望值仍是 0x68。
        return mpu6050_bad_id;  // 芯片身份不符时停止配置。
    }  // 通过 ID 检查后再写控制寄存
    if (HAL_I2C_Mem_Write(i2c, hal_addr, REG_PWR_MGMT_1, I2C_MEMADD_SIZE_8BIT,  // 从 0x6B 开始。
                         power, sizeof(power), I2C_TIMEOUT_MS) != HAL_OK)
    {  // 连续写入两个电源寄存器。
        return mpu6050_i2c_error;  // 电源配置失败时返回。
    }  // 传感器已退出睡眠，进入采样配置。
    if (HAL_I2C_Mem_Write(i2c, hal_addr, REG_SMPLRT_DIV, I2C_MEMADD_SIZE_8BIT,  // 从 0x19 开始。
                         config, sizeof(config), I2C_TIMEOUT_MS) != HAL_OK)
    {  // 连续写入四个配置寄存器。
        return mpu6050_i2c_error;  // 采样配置失败时返回。
    }  // 两组寄存器均已写入。
    HAL_Delay(100U);  // 给唤醒后的传感器留出稳定时间。
    return mpu6050_ok;  // 配置完成，应用可以开始读帧。
}

mpu6050_status mpu6050_read_data(I2C_HandleTypeDef *i2c, uint8_t device_addr,  // 指定 I²C 句柄和 7 位地址。
                              mpu6050_data *out)
{
    uint8_t data[14];  // 三轴加速度、温度、三轴角速度各占两个字节。
    if (HAL_I2C_Mem_Read(i2c, (uint16_t)(device_addr << 1), REG_ACCEL_XOUT_H,  // 从 X 轴加速度高字节开始。
                       I2C_MEMADD_SIZE_8BIT, data, sizeof(data), I2C_TIMEOUT_MS) != HAL_OK)
    {  // 一次读 14 字节。
        return mpu6050_i2c_error;  // 失败时保留 out 原有内容。
    }  // 读取成功后才解码和写入结果。

    out->ax = decode_int16(data[0], data[1]);       // X 轴加速度。
    out->ay = decode_int16(data[2], data[3]);       // Y 轴加速度。
    out->az = decode_int16(data[4], data[5]);       // Z 轴加速度。
    out->temp_raw = decode_int16(data[6], data[7]); // 芯片温度原始值。
    out->gx = decode_int16(data[8], data[9]);       // X 轴角速度，起始下标在温度之后。
    out->gy = decode_int16(data[10], data[11]);    // Y 轴角速度。
    out->gz = decode_int16(data[12], data[13]);    // Z 轴角速度。
    return mpu6050_ok;  // 七个字段全部有效。
}
