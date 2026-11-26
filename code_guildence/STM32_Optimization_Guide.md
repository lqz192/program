# STM32 三板采集系统代码优化编写指南

适用于 `STM32F103ZET61`（节点 1）、`STM32F103ZET62`（节点 2）和 `STM32F767_Hub`。按以下顺序分批修改，每批分别编译和上板验证。文件位置均以仓库根目录为起点，使用函数名定位，以免行号随修改漂移。

## 1. 让 UART 接收失败后自动恢复

**现有问题与风险。** 三套工程的 `User/Comm/link_uart.c` 在字节接收和错误回调中丢弃重挂接返回值。启动失败时，Hub 的 `enabled` 和 F103 的 `link_ready` 会让主循环一直跳过链路。一次失败可能导致持续无应答。单行缓冲被占用时，后续帧也可能被丢弃。

**修改位置与方法。** 在三套工程各自的 `User/Comm/link_uart.h` 中，向 `link_uart` 结构体末尾添加以下字段，并在函数声明区添加服务函数。计数用于调试器观察，`dropped_lines` 只统计因缓冲占用而被丢弃的行。

```c
volatile bool rx_retry;                  // 中断或启动代码通知主循环重试接收。
uint32_t last_rx_retry_ms;               // 记录主循环最近一次恢复尝试。
volatile uint32_t rx_errors;             // 累计进入 HAL 错误回调的次数。
volatile uint32_t rx_rearm_failures;     // 累计挂接单字节接收失败的次数。
volatile uint32_t dropped_lines;         // 累计因上一行未被取走而丢弃的行数。
```

```c
void link_uart_service(link_uart *link); // 由主循环定期处理接收恢复。
```

在三个 `link_uart.c` 中，把下面的辅助函数放在 `link_uart_start()` 前，替换原来的 `link_uart_start()`，并在其后加入 `link_uart_service()`。首次启动后仍保留返回值供诊断。

```c
static bool link_uart_arm(link_uart *link)                                           // 集中处理单字节接收的启动结果。
{                                                                                    // 此函数在 UART 回调或关中断保护的主循环中调用。
    bool armed = HAL_UART_Receive_IT(link->uart, &link->rx_byte, 1U) == HAL_OK;        // 挂接下一个字节。
    if (!armed) {                                                                    // HAL 拒绝挂接时留下诊断信息。
        ++link->rx_rearm_failures;                                                    // 累计挂接失败次数。
    }                                                                                // 结束失败计数。
    link->rx_retry = !armed;                                                         // 成功时清除恢复请求，失败时交给主循环。
    return armed;                                                                    // 将结果返回给启动调用方。
}                                                                                    // 结束挂接函数。
bool link_uart_start(link_uart *link, UART_HandleTypeDef *uart)                       // 每条链路仅在初始化时调用一次。
{                                                                                    // 建立 UART 句柄和初始接收状态。
    *link = (link_uart){ .uart = uart, .last_rx_retry_ms = HAL_GetTick() - 20U };       // 清零上下文并允许首次失败后立即重试。
    uint32_t primask = __get_PRIMASK();                                               // 保存调用前的中断屏蔽状态。
    __disable_irq();                                                                 // 避免回调在挂接结果更新前运行。
    bool armed = link_uart_arm(link);                                                 // 启动本链路接收。
    __set_PRIMASK(primask);                                                           // 恢复原来的中断屏蔽状态。
    return armed;                                                                    // 供启动诊断判断。
}                                                                                    // 结束首次启动。
void link_uart_service(link_uart *link)                                              // 每轮主循环服务一次本链路。
{                                                                                    // 正常接收时快速返回。
    uint32_t now_ms = HAL_GetTick();                                                  // 获取当前毫秒数。
    if (!link->rx_retry || now_ms - link->last_rx_retry_ms < 20U) {                    // 限制恢复频率并支持时基回卷。
        return;                                                                      // 接收正常或尚未到重试时间。
    }                                                                                // 结束快速判断。
    uint32_t primask = __get_PRIMASK();                                               // 保存进入临界区前的中断状态。
    __disable_irq();                                                                 // 保护 HAL 接收状态和恢复标志。
    if (link->rx_retry) {                                                            // 防止进入临界区前接收已被回调恢复。
        link->last_rx_retry_ms = now_ms;                                              // 记录本次恢复尝试时间。
        (void)link_uart_arm(link);                                                    // HAL 仍未就绪时保留重试请求。
    }                                                                                // 结束恢复操作。
    __set_PRIMASK(primask);                                                           // 退出临界区并恢复中断状态。
}                                                                                    // 结束主循环服务。
```

接着按以下位置修改现有代码。

1. 替换 `link_uart_on_rx()` 最后一条 HAL 接收调用，使用下方代码。保留函数开头的 HAL 错误检查。把原来的 `if (link->ready || ...)` 拆成两个分支，先处理 `link->ready`，在该分支增加 `++link->dropped_lines;` 并设置 `link->discard = true;`，再用 `else if` 处理原有的非法字符和超长判断。外层 `!link->discard` 使同一行只计一次。

```c
if (!link_uart_arm(link)) {    // 重新挂接失败时，恢复前可能丢失字节。
    link->used = 0U;           // 清除正在组装的行长度，保留已发布的完整行。
    link->discard = true;      // 恢复后丢弃残余字节，直到遇到下一个换行。
}                              // 主循环会根据 rx_retry 继续尝试挂接。
```

2. `link_uart_on_error()` 增加 `++link->rx_errors;`，保留清零 `used` 和设置 `discard`。将最后的 HAL 接收调用替换为下方代码。F1/F7 HAL 的非终止错误可以继续原接收，此时无需再次挂接。保留已经发布的完整行及其时间戳。

```c
if (link->uart->RxState != HAL_UART_STATE_BUSY_RX) {  // 终止型错误结束原接收后才立即重挂接。
    (void)link_uart_arm(link);                        // 挂接失败时保留主循环恢复请求。
}                                                     // 非终止型错误交由当前接收继续处理。
```

3. 两个 F103 的 `User/App/app_node_link.c` 删除文件级 `link_ready`，初始化改为 `(void)link_uart_start(&node_link, &huart2);`。在 `app_node_link_run()` 开头调用 `link_uart_service(&node_link);`，从取帧判断中去掉 `!link_ready ||`。
4. Hub 的 `User/App/app_hub.c` 把 `service_node()` 开头的 `if (!node->enabled)` 整块替换为 `link_uart_service(&node->link);`。现有 `enabled` 只用于输出首次启动错误，可改名为 `rx_started`，同步更新 `app_hub_init()` 中三处引用。

**解决的问题与验证。** 接收挂接失败后主循环仍能重试。上板断开并接回节点串口，验证有效应答恢复。调试版本中可让一次接收挂接返回失败，确认重试路径能恢复。收到下一个换行后应重新同步，已经发布的完整行仍可取走，另一条 Hub 链路应继续工作。

**单行缓冲的验收。** 用串口工具一次发送两条能被解析的短帧，第二帧开始进入时若第一帧仍未取走，应看到 `dropped_lines` 增加。再单独发送一条超长行和一条正常帧，检查超长行被丢弃后能重新同步。正常三板轮询及故障恢复测试中若反复出现占用丢帧，再扩大缓冲。修改点仍集中在三套工程的 `link_uart.h/.c`，将接收组帧缓冲和完整帧队列分开，并让 `link_uart_take()` 每次只取队首。

- 在 `link_uart.h` 的 `link_uart` 中，将单个已发布行及 `ready` 替换为两个完整帧槽。每个槽存放文本和到达时间，另加生产索引和消费索引。保留独立的组帧缓冲和 `used`，可先用 2 个槽验证，再依据实测决定容量。
- 在 `link_uart_on_rx()` 的换行分支处理发布。先检查队列是否有空间。有空间就复制完整行和当前时刻，经 `__DMB()` 后发布生产索引。队列满时增加 `dropped_lines` 并丢弃本行，保留先到的帧。中断回调只写生产索引，主循环只写消费索引。索引使用 `volatile uint32_t`，通过无符号加法自然回卷，取槽时对 2 取模，用生产索引减消费索引判断队列占用。启用队列时，删除逐字节分支中因原 `ready` 占用而立即丢帧的判断，改由换行分支统一处理队列满。
- 在 `link_uart_take()` 替换原先读取 `ready` 的代码。进入短临界区检查队列是否为空并读取队首槽号，然后退出临界区。复制文本和时间戳期间保持消费索引不变，让中断不会覆盖队首。复制完成后在短临界区推进消费索引。UART 错误回调只重置尚未发布的组帧状态。
- 验证两帧连续到达时能按顺序取出，第三帧在队列满时只增加一次丢帧计数。对比修改前后的 UART 中断耗时和 RAM 占用，再决定是否保留这项修改。

## 2. 区分板间回复失败与 Hub 转发失败

**现有问题与风险。** 两块 F103 在 `app_node_link_run()` 中忽略 PONG/DATA 的发送结果，回复失败在 Hub 上通常只表现为超时。F767 在 `service_node()` 接收并验证应答后，也忽略通过 USART1 转发到电脑的发送结果。后一种故障发生时，板间请求已正常完成，电脑端仍会少一帧。

**修改位置与方法。**

1. 在两个 F103 工程的 `User/App/app_node_link.c` 文件级变量区追加下方计数器。定位 `app_node_link_run()` 末尾、长度合法判断内部的 `(void)link_uart_send(&huart2, line);`，仅替换这一行。外层长度检查继续约束可发送帧。

```c
static volatile uint32_t reply_tx_failures;  // 累计该节点合格应答的发送失败次数，供调试器观察。
```

```c
if (!link_uart_send(&huart2, line)) {  // 在板间 USART2 发送本次 PONG 或 DATA。
    ++reply_tx_failures;              // 发生发送失败时留下节点侧证据。
}                                     // 本次请求只尝试发送一次。
```

2. 在 `STM32F767_Hub/User/App/app_hub.c` 的 `hub_node` 结构体末尾加下方字段。定位 `service_node()` 内 `if (pong || data)` 分支，保留 `node->waiting = false;` 和 `node->probed = true;`，把其后的 `(void)link_uart_send(&huart1, line);` 替换为下方判断。每个节点分别累计自己的转发失败次数。

```c
volatile uint32_t host_tx_failures;        // 累计此节点的应答在 Hub 至电脑链路上的发送失败。
```

```c
if (!link_uart_send(&huart1, line)) {     // 尝试把已验证的板间应答转发给电脑。
    ++node->host_tx_failures;            // 记录 USART1 转发失败，保持板间请求已完成的状态。
}                                       // 下一次轮询沿用原来的调度和序号。
```

这两个失败分支只更新计数。同步发送可能已发出帧前缀，原地重发有可能把前后字节拼成一条无效行，还会延长主循环阻塞。

**解决的问题与验证。** 通过调试器分别查看节点侧 `reply_tx_failures`、Hub 的 `host_tx_failures` 和第 1 节的接收计数。用调试版本分别让两处目标发送调用返回一次失败，确认只有对应计数增加。节点应答失败时 Hub 应能进入本次超时处理并继续发下一次请求。Hub 转发失败时板间请求应仍标记为完成，另一路和后续轮询继续运行。仅拔掉电脑 USB 串口线一般无法可靠验证 HAL 发送失败，MCU 可能仍正常移出字节。

## 3. 去掉 IMU 重启时的 100 ms 主循环等待

**现有问题与风险。** 节点 1 读帧失败后会重做初始化。驱动在重配置成功时调用 `HAL_Delay(100U)`，期间节点主循环无法取走 GET 并应答。这段等待本身已超过 Hub 的 75 ms 应答窗口。

**修改位置与方法。** 在 `STM32F103ZET61/User/Drivers/mpu6050.c` 的 `mpu6050_init()` 末尾删除 `HAL_Delay(100U)`，保留原来的返回语句，并把 `mpu6050.c/.h` 中该接口的注释改为“寄存器配置完成，调用方等待 100 ms 后读帧”。在 `User/App/app_imu.c` 中保留 `sensor_ready`、`last_action_ms` 和 `app_imu_run()` 现有的 100 ms 与 1000 ms 判断。将 `app_imu_init()` 中记录 `last_action_ms` 的语句移到配置状态赋值之后、日志之前，并按下方方式放置就绪日志。

```c
sensor_ready = (status == mpu6050_ok);          // 配置成功后进入由时间戳约束的首次读帧等待。
last_action_ms = HAL_GetTick();                 // 从寄存器配置结束开始计算稳定时间或重试间隔。
if (!sensor_ready) {                           // 配置失败时直接输出原因。
    send_line(status == mpu6050_bad_id         // 根据芯片 ID 检查结果选择诊断。
              ? "MPU6050 INIT_ERR BAD_ID\r\n"  // 报告身份不符。
              : "MPU6050 INIT_ERR I2C\r\n");   // 报告 I²C 读写失败。
}                                              // 配置成功时由主循环等待稳定。
```

如需保留原来的 READY 日志，在 `app_imu.c` 的文件级变量区增加 `static bool first_frame_pending;`，并在上方代码的 `sensor_ready` 赋值后设置 `first_frame_pending = sensor_ready;`。在 `app_imu_run()` 的读帧错误分支之后、格式化数据之前添加下方代码。

```c
if (first_frame_pending) {               // 首次成功读帧说明稳定期已过且数据读取正常。
    first_frame_pending = false;         // 每轮重新配置后只输出一次就绪提示。
    send_line("MPU6050 READY\r\n");        // 此刻才报告传感器已能采样。
}                                        // 后续样本沿用现有发布路径。
```

**解决的问题与验证。** 主循环在等待稳定的 100 ms 内仍可应答，IMU 槽沿用现有的 `NA` 逻辑。保留 I²C 事务的超时，并上板记录重配置期间的最大 GET 应答延迟。若单次配置调用实测仍接近 75 ms，再把读 ID、写电源寄存器和写采样寄存器拆成每轮一个步骤。

## 4. 恢复 ToF 采样并控制查询频率

**现有问题与风险。** `STM32F103ZET62/User/App/app_tof.c` 将启动失败、无结果超时和 I²C 错误变成持续停采。恢复接线后仍可能长期返回 `NA`。此外，当前每 10 ms 查询一次就绪，而驱动以 100 ms 为测量周期。

**修改位置与方法。**

1. 在 `User/Drivers/tof.c` 的 `tof_start()` 之前增加下面的短步骤复位函数，供应用重试使用。在 `User/Drivers/tof.h` 声明 `bool tof_reset_set(bool released);`。仓库的 ST 库 `VL53L0X_ResetDevice()` 内部轮询没有统一截止时间，因此这里使用一次寄存器写入，并由主循环分段等待。现有 `tof_start()` 在进入校准等步骤前保留型号 ID 检查。

```c
bool tof_reset_set(bool released)                                          // 执行一次软件复位控制寄存器写入。
{                                                                          // 使用与 tof_start 相同的设备地址和 I²C 句柄。
    tof_device.I2cDevAddr = tof_hal_addr;                                   // 设置左移后的 HAL 地址。
    tof_device.I2cHandle = &hi2c2;                                          // 指向板上的 I²C2。
    return VL53L0X_WrByte(&tof_device, VL53L0X_REG_SOFT_RESET_GO2_SOFT_RESET_N, // 选择 VL53L0X 的软件复位寄存器。
                          released ? 1U : 0U) == VL53L0X_ERROR_NONE;         // 1 释放复位，0 进入复位。
}                                                                          // 将单次 I²C 写入结果交给应用。
```

2. 在 `User/App/app_tof.c` 的文件级变量区添加下方代码。将 `app_tof_init()` 函数体替换为 `tof_schedule_retry();`，初次启动也走同一条复位和初始化路径。在 `app_tof_run()` 开头先处理 `!tof_ready`，调用 `tof_retry_run(now_ms);` 后立即返回，原有的采样间隔判断保留在其后。

```c
typedef enum { tof_retry_wait, tof_reset_release, tof_start_wait } tof_retry_state; // 三个重启阶段依次运行。
static tof_retry_state retry_state;                                                 // 保存当前重启阶段。
static uint32_t retry_since_ms;                                                      // 保存进入该阶段的时刻。
static void tof_schedule_retry(void)                                                 // 启动或故障后安排重启。
{                                                                                    // 统一处理停采和样本失效。
    tof_ready = false;                                                               // 停止结果读取。
    app_node_sample_update(node_second, NULL);                                       // 立即撤销旧距离。
    retry_state = tof_retry_wait;                                                    // 重新从断言复位开始。
    retry_since_ms = HAL_GetTick();                                                  // 从此刻开始计算重试间隔。
}                                                                                    // 结束重启安排。
static void tof_retry_run(uint32_t now_ms)                                           // 每轮执行至多一个复位或启动步骤。
{                                                                                    // 在等待期间及时交还主循环。
    switch (retry_state) {                                                          // 根据当前阶段执行一次操作。
    case tof_retry_wait:                                                             // 等待 1 s 再尝试访问传感器。
        if (now_ms - retry_since_ms >= 1000U) {                                       // 重试间隔到期才访问器件。
            if (tof_reset_set(false)) {                                               // 尝试令芯片进入复位。
                retry_state = tof_reset_release;                                      // 复位写入成功，下轮开始等待释放。
            }                                                                        // 写入失败则留在重试等待阶段。
            retry_since_ms = HAL_GetTick();                                           // 从本次 I²C 写入返回时开始计时。
        }                                                                            // 完成本阶段检查。
        break;                                                                       // 回主循环服务 UART。
    case tof_reset_release:                                                          // 保留复位期间的等待。
        if (now_ms - retry_since_ms >= 5U) {                                          // 5 ms 为待上板验证的初始等待值。
            if (!tof_reset_set(true)) {                                               // 尝试退出复位。
                tof_schedule_retry();                                                // 写失败时回到重试间隔。
                break;                                                               // 结束本轮操作。
            }                                                                        // 复位释放成功。
            retry_state = tof_start_wait;                                             // 准备等待芯片启动。
            retry_since_ms = HAL_GetTick();                                           // 从释放成功开始计时。
        }                                                                            // 完成本阶段检查。
        break;                                                                       // 回主循环服务 UART。
    case tof_start_wait:                                                             // 等待芯片启动后再配置。
        if (now_ms - retry_since_ms >= 10U) {                                         // 10 ms 为待上板验证的初始等待值。
            if (!tof_start()) {                                                      // 型号检查、配置或测距启动失败。
                tof_schedule_retry();                                                // 保持样本无效并延后重试。
                break;                                                               // 本轮不读取测量结果。
            }                                                                        // 连续测距已启动。
            last_poll_ms = last_result_ms = HAL_GetTick();                             // 从启动完成时刻重建轮询和超时基准。
            tof_ready = true;                                                        // 下一轮按周期检查结果。
        }                                                                            // 完成本阶段检查。
        break;                                                                       // 结束启动阶段。
    }                                                                                // 结束重启状态分派。
}                                                                                    // 结束本轮重启服务。
```

3. 将 `tof_pending` 超时分支和 `tof_io_error` 分支里“设 `tof_ready = false`、清缓存”的两条语句替换为一次 `tof_schedule_retry();`，保留各自日志。`tof_invalid` 仍只清除样本并刷新结果时间。把文件顶部的 `poll_period_ms` 从 10 ms 改为 **25 ms** 作为初始值，在结果产生后最多增加约 25 ms 的查询等待，主循环阻塞会增加实际等待。
4. `tof_start()` 仍串行调用多项校准与配置。先测重启时的最大应答延迟。若超过 75 ms，再把 `tof_start()` 的短路表达式按调用顺序拆成初始化步骤，每轮执行一项，并对耗时超标的单项单独处理。软件复位依赖 I²C 可访问，验证应覆盖总线恢复后重启。若 SDA 持续拉低，需按板上硬件条件处理总线或器件复位。

**解决的问题与验证。** 节点在可恢复故障后自动恢复测距，重启阶段仍用 `NA` 表示距离不可用，并减少就绪查询。测试遮挡或无效结果、传感器断开后接回、正常连续测距三种情形。记录节点 2 在故障期间的应答延迟、编码器读取间隔和 ToF 恢复时间。

## 5. 限制调试串口对采样和应答的影响

**现有问题与风险。** 节点 1 的 `app_adc.c`、`app_imu.c` 和节点 2 的 `User/Bsp/debug_uart.c` 都调用阻塞式 `HAL_UART_Transmit()`。周期性日志和错误日志会占用同一个主循环，增加应答和编码器读取的间隔。

**修改位置与方法。** 第一轮保留启动和错误日志，将周期样本日志改为可关闭。每个文件的文件级常量区声明独立开关。节点 1 在 `User/App/app_adc.c` 与 `app_imu.c` 各加一份，节点 2 在 `User/App/app_encoder.c` 与 `app_tof.c` 各加一份。在成功样本的 `app_node_sample_update(...)` 调用之后，仅用下面的判断包住该分支的 `send_line(line)`。

```c
static const bool sample_log_enabled = false;  // 联调时可改为 true，默认关闭周期样本的调试输出。
```

```c
if (sample_log_enabled) {   // 只控制发往本地调试串口的周期日志。
    send_line(line);        // 串口调试需要时才发送这个已发布的样本。
}                            // 样本已进入板间协议缓存。
```

对于反复出现的错误日志，在节点 1 的 `User/App/app_adc.c` 文件级变量区添加下方时间戳。把 `app_adc_run()` 的 ADC 读取失败分支中发送 `ADC READ_ERR` 的语句替换为下方判断。原有清空样本和提前返回的语句保留在判断外。然后在节点 2 的 `User/App/app_tof.c` 对 `tof_invalid` 分支做同样修改，使用该文件自己的时间戳，将示例中的错误字符串替换为原有的 `TOF INVALID` 日志，保留 `last_result_ms` 更新和清空样本。

```c
static uint32_t last_error_log_ms = UINT32_MAX - 999U;  // 让开机后的首条错误日志可立即输出。
```

```c
if (now_ms - last_error_log_ms >= 1000U) {  // 持续故障时每秒最多输出一次该日志。
    last_error_log_ms = now_ms;             // 保存本次日志时间，支持毫秒计数回卷。
    send_line("ADC READ_ERR\r\n");           // 在 ADC 读取失败分支输出诊断。
}                                          // 清除失效样本仍由原有代码每次执行。
```

Hub 的 USART1 同时转发协议数据，保留 `app_hub.c` 的合法 PONG/DATA 转发。它的发送失败由第 2 节的计数器记录。

**解决的问题与验证。** 正常采集少占用阻塞式串口时间。分别关闭和开启样本日志，比较两台节点的最大 GET 应答延迟，确认关闭本地日志后电脑仍能从 Hub 收到四路数据。

## 6. 给编码器计数留出可验证的速度余量

**现有问题与风险。** `STM32F103ZET62/User/Drivers/encoder.c` 从 TIM8 的 16 位计数差还原正反转，单次净位移必须小于 32768 计数。名义上的 10 ms 调度会受 I²C 和 UART 阻塞影响，读取间隔过长可能把正转识别为反转。

**修改位置与方法。** 在 `User/App/app_encoder.c` 的文件级变量区增加 `static volatile uint32_t max_sample_gap_ms;`，替换 `app_encoder_run()` 中从周期判断到 `encoder_read_count()` 的部分。保留后面每 100 ms 发布一次数据的代码。这里从函数内获取读取时间，避免使用这一轮主循环更早取得的时间。

```c
uint32_t read_ms = HAL_GetTick();                       // 在实际读取计数附近取得时间。
uint32_t gap_ms = read_ms - last_sample_ms;             // 计算距离上次读取经过的毫秒数。
if (!encoder_ready || gap_ms < sample_period_ms) {     // 启动成功且到达采样间隔时才读取。
    return;                                            // 继续执行主循环中的其他任务。
}                                                      // 本轮可读取 TIM8。
last_sample_ms = read_ms;                              // 保存当前读数的时间基准。
int32_t count = encoder_read_count();                   // 读取并累计本次有符号增量。
if (gap_ms > max_sample_gap_ms) {                      // 检查本次间隔是否创下新上限。
    max_sample_gap_ms = gap_ms;                         // 供调试器读出实测最大值。
}                                                      // 后续沿用原来的发布逻辑。
```

用电机最高转速除以 60，再乘以每转实际计数，得到最大计数率（计数/秒）。乘以最大实测间隔（秒），结果须小于 32768，并给可预见的故障重试留出余量。每转计数用手动转一圈确认，包含硬件解码倍率。若仍超限，先完成前面的阻塞优化并复测。确需保证更短、稳定的读取间隔时，再使用固定周期中断，仅在中断内读取并累计计数，在主循环取快照和输出数据。

**解决的问题与验证。** 这一步用实测确定当前差分算法的工作范围。验证低速、最高转速、反向和跨越计数边界，并在 ToF 重启时重复测量。

## 7. 修正 ADC 超时单位并支持校准重试

**现有问题与风险。** 节点 1 的 `adc_input.c` 把 HAL 轮询的 10 ms 注释成了 10 µs。启动校准失败时，`app_adc_run()` 会持续退出。单次读数失败已有清缓存并继续下一周期的处理。

**修改位置与方法。** 在 `STM32F103ZET61/User/Drivers/adc_input.c` 的 `adc_input_read()` 中替换原轮询调用行，保留已有启动、取值和停止顺序。

```c
status = HAL_ADC_PollForConversion(&hadc1, 10U);  // 等待规则组转换完成，HAL 超时参数为 10 ms。
```

在 `User/App/app_adc.c` 的 `app_adc_init()` 中，将 `last_sample_ms = HAL_GetTick();` 移到 `adc_ready` 赋值紧后，让校准失败也会更新计时基准。随后把 `app_adc_run()` 开头的合并判断替换为下方代码。原有采样时间更新和读数处理紧接其后。

```c
if (!adc_ready) {                              // 校准失败时暂时停止采样。
    if (now_ms - last_sample_ms >= 1000U) {     // 每隔至少 1 s 才重试校准。
        app_adc_init();                        // 沿用初始化及其错误日志，并更新计时基准。
    }                                          // 未到重试时刻时立即返回。
    return;                                    // 本轮只处理校准，下一轮再判断采样时刻。
}                                              // 只有 ADC 就绪时才进入正常采样。
if (now_ms - last_sample_ms < 100U) {           // 保留原来的 100 ms 采样周期。
    return;                                    // 尚未到下一次采样。
}                                              // 继续执行原来的单次转换。
```

**解决的问题与验证。** HAL 超时单位与注释一致，暂时性校准失败后可以自动恢复。用调试版本模拟第一次校准失败，确认节点仍应答、ADC 槽先返回 `NA`，随后恢复发布 `ADC raw=… mv=…`。

## 分批验收

1. 每次先编译涉及的板。UART 改动涉及全部三套工程，须逐一编译并检查编译警告。
2. 三板联机后，检查两条链路能分别收到 PONG，随后持续收到带正确节点号和序号的 DATA。
3. 逐个引入串口接收异常、节点回复失败、Hub 转发失败、IMU 重初始化、ToF 脱离和 ADC 校准失败。分别观察接收及发送计数、Hub 超时、故障槽位的 `NA`、正常槽位的数据和恢复时刻。
4. 对照 Hub 的 75 ms 应答窗口，记录正常及故障路径的最大应答时间。以 F767 发完请求到收完整响应为准，包含 F103 主循环等待和响应在线路上的发送时间。记录最高转速和 ToF 重试期间的编码器最大读取间隔，与第 6 节的计数上限核算结果对照。
5. 本指南中的修改可用现有外设配置，在 `User` 目录实现。验证时保留当前 `.ioc`，通过各工程现有的 CMake 配置生成并编译固件。
