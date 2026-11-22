#ifndef APP_IMU_H  // 首次包含时导入应用接口。
#define APP_IMU_H  // 防止重复处理本头文件的内容。

void app_imu_init(void);  // 外设初始化完成后启动采集应用。
void app_imu_run(void);   // 从主循环反复调用，按时间执行采集或重试。

#endif  // 结束 APP_IMU_H 保护区。
