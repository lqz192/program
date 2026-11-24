#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef encoder_start(void);
int32_t encoder_read_count(void);

#endif
