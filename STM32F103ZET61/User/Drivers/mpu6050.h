#ifndef MPU6050_H  // 首次包含时处理下方声明。
#define MPU6050_H  // 保护类型和函数声明，避免重复定义。

#include <stdint.h>  // 提供固定宽度整数类型。
#include "stm32f1xx_hal.h"  // 提供 I2C_HandleTypeDef。

typedef enum {  // 驱动对上层提供的三种结果。
    mpu6050_ok,         // 操作成功，读帧结果可用。
    mpu6050_i2c_error,  // HAL 读寄存器或写寄存器失败。
    mpu6050_bad_id      // 读到了 ID，但与预期的 0x68 不符。
} mpu6050_status;  // 将操作结果作为函数返回值。

typedef struct {  // 保存一次整帧读取的数据。
    int16_t ax, ay, az;  // 三轴加速度原始值。
    int16_t gx, gy, gz;  // 三轴角速度原始值。
    int16_t temp_raw;   // 芯片内部温度原始值。
} mpu6050_data;  // 由应用分配结构体，驱动负责填充。

mpu6050_status mpu6050_init(I2C_HandleTypeDef *i2c, uint8_t device_addr);  // 检查芯片身份并配置寄存器。
mpu6050_status mpu6050_read_data(I2C_HandleTypeDef *i2c, uint8_t device_addr,  // 指定 I²C 句柄和 7 位地址。
                              mpu6050_data *out);  // 整帧读取成功后填充 out。

#endif  // 结束 MPU6050_H 保护区。