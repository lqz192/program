#ifndef APP_TOF_H
#define APP_TOF_H

#include <stdint.h>

void app_tof_init(void);
void app_tof_run(uint32_t now_ms);

#endif

