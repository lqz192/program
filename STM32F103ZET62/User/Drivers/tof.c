#include "tof.h"
#include "i2c.h"
#include "vl53l0x_api.h"
#include "vl53l0x_platform.h"

static VL53L0X_Dev_t tof_device;
static const uint8_t tof_hal_addr = 0x29U << 1;
static const uint32_t measurement_period_ms = 100U;

bool tof_start(void)
{
    uint8_t model_id;  // 保存型号 ID。
    uint32_t spad_count;  // 保存参考 SPAD 校准给出的数量。
    uint8_t spad_is_aperture;  // 保存参考 SPAD 类型。
    uint8_t vhv_settings;  // 保存 VHV 参考校准结果。
    uint8_t phase_cal;  // 保存相位参考校准结果。
    tof_device.I2cDevAddr = tof_hal_addr;
    tof_device.I2cHandle = &hi2c2;

    if (VL53L0X_RdByte(&tof_device, VL53L0X_REG_IDENTIFICATION_MODEL_ID,
                       &model_id) != VL53L0X_ERROR_NONE || model_id != 0xEEU)
    {
        return false;
    }
    return VL53L0X_DataInit(&tof_device) == VL53L0X_ERROR_NONE &&  // 复位后执行一次数据初始化。
           VL53L0X_StaticInit(&tof_device) == VL53L0X_ERROR_NONE &&  // 载入测距配置。
           VL53L0X_PerformRefSpadManagement(&tof_device, &spad_count,  // 配置参考 SPAD。
                                           &spad_is_aperture) == VL53L0X_ERROR_NONE &&  // 接收 SPAD 类型并检查结果。
           VL53L0X_PerformRefCalibration(&tof_device, &vhv_settings,  // 执行 VHV 和相位参考校准。
                                        &phase_cal) == VL53L0X_ERROR_NONE &&  // 接收相位结果并检查状态。
           VL53L0X_SetDeviceMode(&tof_device, VL53L0X_DEVICEMODE_CONTINUOUS_TIMED_RANGING)  // 启用连续定时测距。
               == VL53L0X_ERROR_NONE &&  // 切换模式成功后才配置间隔。
           VL53L0X_SetInterMeasurementPeriodMilliSeconds(&tof_device, measurement_period_ms)  // 设置两次测量的间隔。
               == VL53L0X_ERROR_NONE &&  // 间隔设置成功后才启动测量。
           VL53L0X_StartMeasurement(&tof_device) == VL53L0X_ERROR_NONE;  // 启动测量并返回结果。
}  // 结束测距启动函数。

tof_status tof_poll(uint16_t *distance_mm)
{
    uint8_t data_ready;
    if (VL53L0X_GetMeasurementDataReady(&tof_device, &data_ready) != VL53L0X_ERROR_NONE)// 检查新测量是否就绪。
    {
        return tof_io_error;
    }
    if (data_ready == 0U)
    {
        return tof_pending;
    }
    VL53L0X_RangingMeasurementData_t measurement;
    if (VL53L0X_GetRangingMeasurementData(&tof_device, &measurement) != VL53L0X_ERROR_NONE ||  // 读取整帧测距数据。
        VL53L0X_ClearInterruptMask(&tof_device, 0U) != VL53L0X_ERROR_NONE)
    {  // 清除芯片内的就绪状态。
        return tof_io_error;  // 将读数或清除状态失败上报为通信错误。
    }  // 结束测距读取错误分支。
    if (measurement.RangeStatus != 0U)
    {
        return tof_invalid;
    }
    *distance_mm = measurement.RangeMilliMeter;
    return tof_valid;
}
