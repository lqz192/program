/**
 * @file    ms5837.h
 * @brief   MS5837-02BA 水压温度传感器非阻塞 I2C 驱动接口。
 */

#ifndef MS5837_H
#define MS5837_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  MS5837-02BA 非阻塞轮询状态。
 * @note   驱动内一阶/二阶补偿公式仅适用于 MS5837-02BA 型号，不适用于 30BA。
 */
typedef enum
{
    ms5837_ok = 0,    // 本轮 D1/D2 转换与补偿计算已完成，输出有效。
    ms5837_pending,   // 转换仍在进行中，需后续继续调用 ms5837_poll。
    ms5837_io_error   // I2C 通信或 ADC 读数异常。
} ms5837_status;

/**
 * @brief  复位 MS5837-02BA，读取并校验 PROM 校准系数。
 * @param  bus HAL I2C 总线句柄指针。
 * @retval true 初始化及 CRC4 校验成功；false 通信失败或 PROM 数据无效。
 */
bool ms5837_init(I2C_HandleTypeDef *bus);

/**
 * @brief  推进 MS5837-02BA 非阻塞采样状态机，完成后输出气压与温度。
 * @param  bus               HAL I2C 总线句柄指针。
 * @param  now_ms            当前系统毫秒时间戳。
 * @param  pressure_pa       输出绝对气压，单位 Pa。
 * @param  temperature_centi 输出温度，单位 0.01 °C。
 * @retval ms5837_status     当前轮询状态。
 */
ms5837_status ms5837_poll(I2C_HandleTypeDef *bus, uint32_t now_ms,
                          int32_t *pressure_pa, int32_t *temperature_centi);

#endif  /* MS5837_H */
