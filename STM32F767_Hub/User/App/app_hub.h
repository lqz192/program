#ifndef APP_HUB_H  // 防止重复包含。
#define APP_HUB_H  // 定义头文件保护。
void app_hub_init(void);  // 初始化两条 UART 接收链路。
void app_hub_run(void);  // 运行一轮 F767 调度。
#endif  // 结束头文件保护。
