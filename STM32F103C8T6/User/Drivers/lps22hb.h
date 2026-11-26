/**
 * @file    lps22hb.h
 * @brief   LPS22HB 气压温度传感器 I2C 驱动接口。
 */

#ifndef LPS22HB_H
#define LPS22HB_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  LPS22HB 单次读取状态。
 */
typedef enum
{
    lps22hb_ok = 0,     // 气压与温度均已更新并读取成功。
    lps22hb_not_ready,  // 传感器尚未完成本轮转换，非通信故障。
    lps22hb_io_error    // I2C 通信失败。
} lps22hb_status;

/**
 * @brief  核对 LPS22HB 器件 ID 并启动 25 Hz 连续采样。
 * @param  bus         HAL I2C 总线句柄指针。
 * @param  device_addr 7 位 I2C 从机地址（SDO 接地为 0x5C，接高为 0x5D）。
 * @retval true 初始化成功；false 器件未响应或寄存器配置失败。
 */
bool lps22hb_init(I2C_HandleTypeDef *bus, uint8_t device_addr);

/**
 * @brief  读取 LPS22HB 的气压与温度补偿值。
 * @param  bus               HAL I2C 总线句柄指针。
 * @param  device_addr       7 位 I2C 从机地址。
 * @param  pressure_pa       输出绝对气压，单位 Pa。
 * @param  temperature_centi 输出温度，单位 0.01 °C。
 * @retval lps22hb_status    读取结果状态。
 */
lps22hb_status lps22hb_read(I2C_HandleTypeDef *bus, uint8_t device_addr,
                            int32_t *pressure_pa, int32_t *temperature_centi);

#endif  /* LPS22HB_H */
