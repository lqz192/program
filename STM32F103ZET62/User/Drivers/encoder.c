/**
 * @file    encoder.c
 * @brief   TIM8 正交编码器计数驱动实现。
 */

#include "encoder.h"
#include "tim.h"

static uint16_t previous_count;     // 上次采样的 16 位硬件计数器快照。
static uint32_t accumulated_count;  // 无符号 32 位累计值，避免有符号溢出未定义行为。

HAL_StatusTypeDef encoder_start(void)
{
    __HAL_TIM_SET_COUNTER(&htim8, 0U);
    previous_count = 0U;
    accumulated_count = 0U;
    return HAL_TIM_Encoder_Start(&htim8, TIM_CHANNEL_ALL);
}

int32_t encoder_read_count(void)
{
    uint16_t current_count = (uint16_t)__HAL_TIM_GET_COUNTER(&htim8);
    uint16_t wrapped_delta = (uint16_t)(current_count - previous_count);
    int32_t count_delta = (int32_t)wrapped_delta;

    // 16 位无符号差值 >= 0x8000 时，表示发生反向计数回卷。
    if (wrapped_delta >= 0x8000U)
    {
        count_delta -= 0x10000L;
    }

    previous_count = current_count;
    accumulated_count += (uint32_t)count_delta;

    if (accumulated_count <= INT32_MAX)
    {
        return (int32_t)accumulated_count;
    }
    return -1 - (int32_t)(UINT32_MAX - accumulated_count);  // 安全还原二补码负区间。
}
