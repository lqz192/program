#ifndef TOF_H  // 检查测距头文件是否已经包含。
#define TOF_H  // 定义头文件保护宏。

#include <stdbool.h>  // 引入 bool 类型。
#include <stdint.h>  // 引入固定宽度的整数类型。

typedef enum
{
    tof_pending,  // 传感器还没有新结果。
    tof_valid,  // 本次距离有效，输出参数已写入。
    tof_invalid,  // 本次有结果，但距离状态无效。
    tof_io_error  // I²C 或驱动操作失败。
} tof_status;

bool tof_start(void);
tof_status tof_poll(uint16_t *distance_mm);

#endif
