/**
 * @file    tof.c
 * @brief   VL53L0X 激光测距传感器 I2C 驱动实现。
 */

#include "tof.h"
#include "i2c.h"
#include "vl53l0x_api.h"
#include "vl53l0x_platform.h"

#define TOF_HAL_I2C_ADDR    (0x29U << 1U)   // STM32 HAL 使用左移 1 位后的 7 位器件地址。
#define TOF_MODEL_ID_VALUE  0xEEU           // VL53L0X 固定型号 ID。
#define TOF_MEAS_PERIOD_MS  100U            // 连续定时测距间隔，单位 ms。

static VL53L0X_Dev_t tof_device;            // ST 官方 API 设备上下文。

bool tof_start(void)
{
    uint8_t model_id = 0U;
    uint32_t spad_count = 0U;
    uint8_t spad_is_aperture = 0U;
    uint8_t vhv_settings = 0U;
    uint8_t phase_cal = 0U;

    tof_device.I2cDevAddr = TOF_HAL_I2C_ADDR;
    tof_device.I2cHandle = &hi2c2;

    if (VL53L0X_RdByte(&tof_device, VL53L0X_REG_IDENTIFICATION_MODEL_ID,
                       &model_id) != VL53L0X_ERROR_NONE ||
        model_id != TOF_MODEL_ID_VALUE)
    {
        return false;
    }

    return VL53L0X_DataInit(&tof_device) == VL53L0X_ERROR_NONE &&
           VL53L0X_StaticInit(&tof_device) == VL53L0X_ERROR_NONE &&
           VL53L0X_PerformRefSpadManagement(&tof_device, &spad_count, &spad_is_aperture) == VL53L0X_ERROR_NONE &&
           VL53L0X_PerformRefCalibration(&tof_device, &vhv_settings, &phase_cal) == VL53L0X_ERROR_NONE &&
           VL53L0X_SetDeviceMode(&tof_device, VL53L0X_DEVICEMODE_CONTINUOUS_TIMED_RANGING) == VL53L0X_ERROR_NONE &&
           VL53L0X_SetInterMeasurementPeriodMilliSeconds(&tof_device, TOF_MEAS_PERIOD_MS) == VL53L0X_ERROR_NONE &&
           VL53L0X_StartMeasurement(&tof_device) == VL53L0X_ERROR_NONE;
}

tof_status tof_poll(uint16_t *distance_mm)
{
    uint8_t data_ready = 0U;
    if (VL53L0X_GetMeasurementDataReady(&tof_device, &data_ready) != VL53L0X_ERROR_NONE)
    {
        return tof_io_error;
    }
    if (data_ready == 0U)
    {
        return tof_pending;
    }

    VL53L0X_RangingMeasurementData_t measurement;
    if (VL53L0X_GetRangingMeasurementData(&tof_device, &measurement) != VL53L0X_ERROR_NONE ||
        VL53L0X_ClearInterruptMask(&tof_device, 0U) != VL53L0X_ERROR_NONE)
    {
        return tof_io_error;
    }
    if (measurement.RangeStatus != 0U)
    {
        return tof_invalid;
    }

    *distance_mm = measurement.RangeMilliMeter;
    return tof_valid;
}
