#ifndef APP_ENCODER_H
#define APP_ENCODER_H

#include <stdint.h>

void app_encoder_init(uint32_t now_ms);
void app_encoder_run(uint32_t now_ms);

#endif
