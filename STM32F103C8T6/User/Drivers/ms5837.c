/**
 * @file    ms5837.c
 * @brief   MS5837-02BA 水压温度传感器非阻塞 I2C 驱动实现。
 */

#include "ms5837.h"

#define MS5837_ADDR             (0x76U << 1U)   // STM32 HAL 使用左移 1 位后的 I2C 地址。
#define MS5837_CMD_RESET        0x1EU           // 器件复位命令。
#define MS5837_CMD_ADC_READ     0x00U           // 读取 24 位 ADC 结果命令。
#define MS5837_CMD_CONV_D1_1024 0x44U           // OSR1024 压力转换命令。
#define MS5837_CMD_CONV_D2_1024 0x54U           // OSR1024 温度转换命令。
#define MS5837_CMD_PROM_BASE    0xA0U           // PROM 校准字基地址。
#define MS5837_TIMEOUT_MS       10U             // 单次 I2C 事务超时时间，单位 ms。
#define MS5837_CONV_WAIT_MS     4U              // OSR1024 转换等待时间（含时基余量），单位 ms。
#define MS5837_RESET_WAIT_MS    10U             // 上电/复位后等待 PROM 装载的时间，单位 ms。

/**
 * @brief  MS5837 非阻塞转换状态机阶段。
 */
typedef enum
{
    ms5837_state_idle = 0,  // 空闲态，下次轮询立即发起 D1 转换。
    ms5837_state_wait_d1,   // 等待压力 D1 转换完成。
    ms5837_state_wait_d2    // 等待温度 D2 转换完成。
} ms5837_conv_state;

static uint16_t calibration[8];                         // PROM 0~6 校准字及 CRC 计算用补零字。
static ms5837_conv_state conv_state = ms5837_state_idle;
static uint32_t conv_start_ms;                          // 当前转换命令发出的毫秒时刻。
static uint32_t d1_raw;                                 // 暂存压力原始 24 位 ADC 值。

/**
 * @brief  向 MS5837 发送单字节命令。
 */
static bool send_command(I2C_HandleTypeDef *bus, uint8_t command)
{
    return HAL_I2C_Master_Transmit(bus, MS5837_ADDR, &command, 1U, MS5837_TIMEOUT_MS) == HAL_OK;
}

/**
 * @brief  按厂商规格计算 PROM 的 4 位 CRC 校验码（不修改原数组）。
 */
static uint8_t prom_crc4(const uint16_t *prom)
{
    uint16_t remainder = 0U;
    for (uint8_t index = 0U; index < 16U; ++index)
    {
        uint16_t word = prom[index / 2U];
        if (index < 2U)
        {
            word &= 0x0FFFU;  // 第 0 字高 4 位为原 CRC 字段，计算时置零。
        }
        if (index >= 14U)
        {
            word = 0U;  // 第 7 字按协议补零参与计算。
        }
        remainder ^= (index & 1U) ? (word & 0x00FFU) : (word >> 8U);
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            remainder = (uint16_t)((remainder & 0x8000U) ? ((remainder << 1U) ^ 0x3000U)
                                                         : (remainder << 1U));
        }
    }
    return (uint8_t)((remainder >> 12U) & 0x0FU);
}

/**
 * @brief  读取当前已转换完成的 24 位 ADC 结果。
 */
static bool read_adc_result(I2C_HandleTypeDef *bus, uint32_t *value)
{
    uint8_t data[3];
    if (!send_command(bus, MS5837_CMD_ADC_READ))
    {
        return false;
    }
    if (HAL_I2C_Master_Receive(bus, MS5837_ADDR, data, sizeof(data), MS5837_TIMEOUT_MS) != HAL_OK)
    {
        return false;
    }
    *value = ((uint32_t)data[0] << 16U) | ((uint32_t)data[1] << 8U) | data[2];
    return *value != 0U;  // ADC 转换未就绪或总线异常时读回全零。
}

bool ms5837_init(I2C_HandleTypeDef *bus)
{
    conv_state = ms5837_state_idle;
    if (!send_command(bus, MS5837_CMD_RESET))
    {
        return false;
    }
    HAL_Delay(MS5837_RESET_WAIT_MS);

    for (uint8_t index = 0U; index < 7U; ++index)
    {
        uint8_t data[2];
        if (!send_command(bus, (uint8_t)(MS5837_CMD_PROM_BASE + index * 2U)) ||
            HAL_I2C_Master_Receive(bus, MS5837_ADDR, data, sizeof(data), MS5837_TIMEOUT_MS) != HAL_OK)
        {
            return false;
        }
        calibration[index] = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
    }
    calibration[7] = 0U;

    uint16_t coefficient_or = 0U;
    for (uint8_t index = 1U; index <= 6U; ++index)
    {
        coefficient_or |= calibration[index];
    }
    // 防止全零校准系数误通过 CRC4=0 校验。
    return coefficient_or != 0U && prom_crc4(calibration) == (uint8_t)(calibration[0] >> 12U);
}

ms5837_status ms5837_poll(I2C_HandleTypeDef *bus, uint32_t now_ms,
                          int32_t *pressure_pa, int32_t *temperature_centi)
{
    switch (conv_state)
    {
    case ms5837_state_idle:
        if (!send_command(bus, MS5837_CMD_CONV_D1_1024))
        {
            return ms5837_io_error;
        }
        conv_start_ms = now_ms;
        conv_state = ms5837_state_wait_d1;
        return ms5837_pending;

    case ms5837_state_wait_d1:
        if (now_ms - conv_start_ms < MS5837_CONV_WAIT_MS)
        {
            return ms5837_pending;
        }
        if (!read_adc_result(bus, &d1_raw) || !send_command(bus, MS5837_CMD_CONV_D2_1024))
        {
            conv_state = ms5837_state_idle;
            return ms5837_io_error;
        }
        conv_start_ms = now_ms;
        conv_state = ms5837_state_wait_d2;
        return ms5837_pending;

    case ms5837_state_wait_d2:
    {
        if (now_ms - conv_start_ms < MS5837_CONV_WAIT_MS)
        {
            return ms5837_pending;
        }
        uint32_t d2_raw = 0U;
        conv_state = ms5837_state_idle;
        if (!read_adc_result(bus, &d2_raw))
        {
            return ms5837_io_error;
        }

        // 以下一阶与二阶补偿公式仅适用于 MS5837-02BA。
        int64_t delta_t = (int64_t)d2_raw - (int64_t)calibration[5] * 256LL;
        int64_t temperature = 2000LL + delta_t * calibration[6] / 8388608LL;
        int64_t sensitivity = (int64_t)calibration[1] * 65536LL + (int64_t)calibration[3] * delta_t / 128LL;
        int64_t offset = (int64_t)calibration[2] * 131072LL + (int64_t)calibration[4] * delta_t / 64LL;

        if (temperature < 2000LL)  // 低于 20 °C 时启用低温二阶补偿。
        {
            int64_t temperature_delta = temperature - 2000LL;
            int64_t temperature_square = temperature_delta * temperature_delta;
            temperature -= 11LL * delta_t * delta_t / 34359738368LL;
            offset -= 31LL * temperature_square / 8LL;
            sensitivity -= 63LL * temperature_square / 32LL;
        }

        *pressure_pa = (int32_t)(((int64_t)d1_raw * sensitivity / 2097152LL - offset) / 32768LL);
        *temperature_centi = (int32_t)temperature;
        return ms5837_ok;
    }
    }

    conv_state = ms5837_state_idle;
    return ms5837_io_error;
}
