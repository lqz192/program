/**
 * @file    tof.h
 * @brief   VL53L0X 激光测距传感器 I2C 驱动接口。
 */

#ifndef TOF_H
#define TOF_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  VL53L0X 单次轮询结果状态。
 */
typedef enum
{
    tof_pending = 0,    // 传感器尚未产生新测量结果。
    tof_valid,          // 本次测距结果有效，distance_mm 已更新。
    tof_invalid,        // 传感器已有结果，但 RangeStatus 表示无效测距。
    tof_io_error        // I2C 通信或 API 调用失败。
} tof_status;

/**
 * @brief  校验 VL53L0X 型号 ID、执行校准并启动连续定时测距。
 * @retval true 初始化并启动成功；false 器件未响应或校准失败。
 */
bool tof_start(void);

/**
 * @brief  非阻塞轮询 VL53L0X 测距数据就绪状态。
 * @param  distance_mm 输出测距值，单位 mm（仅当返回 tof_valid 时写入）。
 * @retval tof_status  本次轮询状态。
 */
tof_status tof_poll(uint16_t *distance_mm);

#endif  /* TOF_H */
