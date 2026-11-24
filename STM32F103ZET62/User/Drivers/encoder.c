#include "encoder.h"
#include "tim.h"

static uint16_t precious_count;
static uint32_t accumulated_count;

HAL_StatusTypeDef encoder_start(void)
{
    __HAL_TIM_SET_COUNTER( &htim8, 0U );//将 TIM8 硬件计数器清零。
    precious_count  = 0U;
    accumulated_count = 0U;
    return HAL_TIM_Encoder_Start(&htim8, TIM_CHANNEL_ALL );//启动 TIM8 的两个编码器输入通道。
}

int32_t encoder_read_count(void)
{
    uint16_t current_count =(uint16_t)__HAL_TIM_GET_COUNTER(&htim8);//// 读取当前的 16 位硬件计数。
    uint16_t wrapped_delta = (current_count - precious_count);// 用模 65536 的差值处理初步回卷。
    int32_t count_delta = wrapped_delta;// 扩展到 32 位，便于还原有符号增量。

    // 将 16 位差值还原为跨越计数器边界时的有符号增量。
    if (wrapped_delta >= 0x8000U)
    {  // 最高位为 1 时按负增量处理。
        count_delta -= 0x10000L;  // 将 16 位回卷差值还原为负数。
    }  // 结束负增量转换。

    precious_count = current_count;// 保存本次计数，供下一次求差。
    accumulated_count += (uint32_t )count_delta;// 将本次增量加到总计数。

    if (accumulated_count <= INT32_MAX)
    {
        return (int32_t)accumulated_count;// 直接返回可表示的非负计数。
    }
    return -1 - (int32_t)(UINT32_MAX - accumulated_count); // 将上半区映射到负值，避免超范围转换。
}