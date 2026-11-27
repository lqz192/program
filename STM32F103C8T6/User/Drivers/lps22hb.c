/**
 * @file    lps22hb.c
 * @brief   LPS22HB 气压温度传感器 I2C 驱动实现。
 */

#include "lps22hb.h"

#define LPS22HB_WHO_AM_I_REG        0x0FU   // 器件身份寄存器地址。
#define LPS22HB_WHO_AM_I_VALUE      0xB1U   // LPS22HB 固定 ID。
#define LPS22HB_CTRL_REG1           0x10U   // 控制寄存器 1（ODR 与 BDU）。
#define LPS22HB_CTRL_REG2           0x11U   // 控制寄存器 2（地址自增与 FIFO）。
#define LPS22HB_STATUS_REG          0x27U   // 数据就绪状态寄存器。
#define LPS22HB_PRESS_OUT_XL        0x28U   // 压力最低字节寄存器。
#define LPS22HB_PRESS_OUT_L         0x29U   // 压力中间字节寄存器。
#define LPS22HB_PRESS_OUT_H         0x2AU   // 压力最高字节寄存器（BDU=1 时最后读取以解锁）。
#define LPS22HB_TEMP_OUT_L          0x2BU   // 温度低字节寄存器。
#define LPS22HB_TEMP_OUT_H          0x2CU   // 温度高字节寄存器。
#define LPS22HB_I2C_TIMEOUT_MS      5U      // 单次 I2C 事务超时时间，单位 ms。

bool lps22hb_init(I2C_HandleTypeDef *bus, uint8_t device_addr)
{
    uint16_t hal_addr = (uint16_t)(device_addr << 1U);
    uint8_t id = 0U;

    if (HAL_I2C_Mem_Read(bus, hal_addr, LPS22HB_WHO_AM_I_REG, I2C_MEMADD_SIZE_8BIT,
                         &id, 1U, LPS22HB_I2C_TIMEOUT_MS) != HAL_OK ||
        id != LPS22HB_WHO_AM_I_VALUE)
    {
        return false;
    }

    uint8_t ctrl2 = 0x00U;  // 关闭 FIFO 与地址自增（IF_ADD_INC=0），满足 BDU=1 下的 I2C 单字节顺序读要求。
    uint8_t ctrl1 = 0x32U;  // 配置 25 Hz 连续采样并启用 BDU 锁存保护。
    return HAL_I2C_Mem_Write(bus, hal_addr, LPS22HB_CTRL_REG2, I2C_MEMADD_SIZE_8BIT,
                             &ctrl2, 1U, LPS22HB_I2C_TIMEOUT_MS) == HAL_OK &&
           HAL_I2C_Mem_Write(bus, hal_addr, LPS22HB_CTRL_REG1, I2C_MEMADD_SIZE_8BIT,
                             &ctrl1, 1U, LPS22HB_I2C_TIMEOUT_MS) == HAL_OK;
}

lps22hb_status lps22hb_read(I2C_HandleTypeDef *bus, uint8_t device_addr,
                            int32_t *pressure_pa, int32_t *temperature_centi)
{
    // 按 ST 手册第 37 页 BDU=1 要求：先读温度，再依次单读压力，最后一笔必须为 0x2A。
    static const uint8_t regs[5] = {
        LPS22HB_TEMP_OUT_L,
        LPS22HB_TEMP_OUT_H,
        LPS22HB_PRESS_OUT_XL,
        LPS22HB_PRESS_OUT_L,
        LPS22HB_PRESS_OUT_H
    };

    uint16_t hal_addr = (uint16_t)(device_addr << 1U);
    uint8_t status = 0U;

    if (HAL_I2C_Mem_Read(bus, hal_addr, LPS22HB_STATUS_REG, I2C_MEMADD_SIZE_8BIT,
                         &status, 1U, LPS22HB_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return lps22hb_io_error;
    }
    if ((status & 0x03U) != 0x03U)
    {
        return lps22hb_not_ready;  // bit0(P_DA) 与 bit1(T_DA) 均置位后才读取输出。
    }

    uint8_t data[sizeof(regs)];
    for (uint16_t index = 0U; index < sizeof(regs); ++index)
    {
        if (HAL_I2C_Mem_Read(bus, hal_addr, regs[index], I2C_MEMADD_SIZE_8BIT,
                             &data[index], 1U, LPS22HB_I2C_TIMEOUT_MS) != HAL_OK)
        {
            return lps22hb_io_error;
        }
    }

    int32_t t_raw = (int32_t)data[0] | ((int32_t)data[1] << 8U);
    int32_t p_raw = (int32_t)data[2] | ((int32_t)data[3] << 8U) | ((int32_t)data[4] << 16U);

    if ((p_raw & 0x800000) != 0)
    {
        p_raw -= 0x1000000;  // 24 位补码符号扩展。
    }
    if ((t_raw & 0x8000) != 0)
    {
        t_raw -= 0x10000;  // 16 位补码符号扩展。
    }

    *pressure_pa = (p_raw * 100) / 4096;  // 灵敏度 4096 LSB/hPa，1 hPa = 100 Pa。
    *temperature_centi = t_raw;           // 灵敏度 100 LSB/°C，即直接对应 0.01 °C。
    return lps22hb_ok;
}
