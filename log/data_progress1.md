这组数据在你的项目里走的是 **传感器和定时器 → F103 驱动 → 应用层转成文本 → 节点缓存 → F767 发 `GET` 取数据 → F767 USART1 输出到电脑**。四路数据分成两条 `DATA` 帧，每个节点各回一条。F767 收到后会核对节点号和请求序号，再原样转发应答。

我选了一组包含 **IMU 负数、ADC 半量程、编码器跨零回卷、ToF 有效距离**的输入，按当前源码逐步推演，并用脚本复核了数值、帧文本和字节长度。下面的时间是用于说明先后顺序的示例，各板使用自己的 `HAL_GetTick()`，这次没有上板采集。

## 1. 先看三块板各自做什么

| 工程                     | 两个采样槽位                                   | 板间通信                         | 本地输出                       |
| ------------------------ | ---------------------------------------------- | -------------------------------- | ------------------------------ |
| `STM32F103ZET61`，节点 1 | `node_first` 为 MPU6050，`node_second` 为 ADC  | USART2 ↔ F767 USART2             | USART1 输出 IMU、ADC 日志      |
| `STM32F103ZET62`，节点 2 | `node_first` 为编码器，`node_second` 为 ToF    | USART2 ↔ F767 USART6             | USART1 输出编码器、ToF 日志    |
| `STM32F767_Hub`          | 为两条链路分别维护请求序号、等待状态和超时计时 | USART2 对节点 1，USART6 对节点 2 | USART1 经 CH340 输出应答和诊断 |

两块 F103 的 `main()` 都先初始化自己的采样模块，再初始化节点链路。进入 `while (1)` 后，每轮都**先处理串口请求，再执行采样应用**。所以，如果 `GET` 已经到达，而本轮新样本还没执行到缓存写入的位置，这次就会返回上一条有效样本。F767 的主循环则依次服务两条链路，每条链路各自判断是否到达轮询时间。 [节点 1 main.c (line 99)](/E:/desk/program/STM32F103ZET61/Core/Src/main.c:99) · [节点 2 main.c (line 99)](/E:/desk/program/STM32F103ZET62/Core/Src/main.c:99) · [F767 app_hub.c (line 102)](/E:/desk/program/STM32F767_Hub/User/App/app_hub.c:102)

## 2. 节点 1 怎样把 MPU6050 和 ADC 数据写进缓存

### MPU6050 从 14 个 I²C 字节变成六轴文本

假设 `app_imu_run()` 在节点 1 的本地 `now_ms = 210` 时到期。它正常时约每 100 ms 读一次。调用 `mpu6050_read_data()` 后，驱动通过 I2C1，从 MPU6050 的 `0x3B` 寄存器开始，一次读取 14 字节。器件的 7 位地址为 `0x68`，传给 HAL 的地址是左移后的 `0xD0`。 [app_imu.c (line 35)](/E:/desk/program/STM32F103ZET61/User/App/app_imu.c:35) · [mpu6050.c (line 50)](/E:/desk/program/STM32F103ZET61/User/Drivers/mpu6050.c:50)

这次假设读到 `04 00 FE 00 40 00 00 00 00 83 FF 7D 00 00`。驱动每两个字节解出一个有符号 16 位数，高字节在前。

| 字节下标 | 原始字节 | 写入字段   | 解码结果 |
| -------- | -------- | ---------- | -------- |
| 0、1     | `04 00`  | `ax`       | 1024     |
| 2、3     | `FE 00`  | `ay`       | -512     |
| 4、5     | `40 00`  | `az`       | 16384    |
| 6、7     | `00 00`  | `temp_raw` | 0        |
| 8、9     | `00 83`  | `gx`       | 131      |
| 10、11   | `FF 7D`  | `gy`       | -131     |
| 12、13   | `00 00`  | `gz`       | 0        |

例如 `FE 00` 拼成 `0xFE00 = 65024`。它达到 `0x8000`，驱动减去 `65536`，得到 `-512`。七个字段先写进 `raw_data`，随后 `app_imu_run()` 用六轴值和 `now_ms` 生成下面这行。`temp_raw` 已经解码，当前格式串只输出六轴。`AX` 到 `GZ` 保留传感器原始计数。 [mpu6050.c (line 9)](/E:/desk/program/STM32F103ZET61/User/Drivers/mpu6050.c:9) · [app_imu.c (line 60)](/E:/desk/program/STM32F103ZET61/User/App/app_imu.c:60)

```
IMU_RAW ms=210 AX=1024 AY=-512 AZ=16384 GX=131 GY=-131 GZ=0
```

这行文本有两个去向。`app_node_sample_update(node_first, line)` 先把正文复制进缓存，然后 `send_line()` 通过节点 1 的 USART1 发送原来的调试行。文本里的 `ms=210` 是**本次读帧开始时**的本地时间，缓存内部另存的 `sample_ms` 是**写入缓存时**的本地时间，过期判断用后者。 [app_imu.c (line 50)](/E:/desk/program/STM32F103ZET61/User/App/app_imu.c:50) · [app_node_link.c (line 18)](/E:/desk/program/STM32F103ZET61/User/App/app_node_link.c:18)

### ADC 从 `2048` 变成 `1650 mV`

假设 PA1 本轮转换得到 `raw = 2048`。`app_adc_run()` 到期后调用 `adc_input_read()`，驱动通过 `HAL_ADC_Start()` 启动单次转换，等待完成，调用 `HAL_ADC_GetValue()` 取值，再停止 ADC。读取和停止都成功后，应用才使用这个值。这里 `HAL_ADC_PollForConversion(&hadc1, 10)` 的超时参数单位是 **毫秒**，现有行尾注释写的是 `10us`。 [app_adc.c (line 31)](/E:/desk/program/STM32F103ZET61/User/App/app_adc.c:31) · [adc_input.c (line 10)](/E:/desk/program/STM32F103ZET61/User/Drivers/adc_input.c:10) · [HAL ADC 实现 (line 1190)](/E:/desk/program/STM32F103ZET61/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc.c:1190)

应用按 3300 mV 参考值和 12 位满量程 `4095` 做整数换算。带入数据后，`(2048 × 3300 + 2047) ÷ 4095 = 6,760,447 ÷ 4095`，整数结果是 `1650`。`2047` 用于让整数除法近似四舍五入。生成的 `ADC raw=2048 mv=1650` 写入 `node_second`，然后通过本地 USART1 打印。 [app_adc.c (line 48)](/E:/desk/program/STM32F103ZET61/User/App/app_adc.c:48)

## 3. 节点 2 怎样把编码器和距离写进缓存

### 编码器从 TIM8 的回卷计数得到累计值 `1`

`encoder_start()` 把 TIM8 计数器、上次硬件计数 `precious_count` 和软件累计值清零。随后 TIM8 根据 A/B 相输入自动加减计数。`app_encoder_run()` 约每 10 ms 取一次硬件计数，约每 100 ms 才发布一次累计值。 [encoder.c (line 7)](/E:/desk/program/STM32F103ZET62/User/Drivers/encoder.c:7) · [app_encoder.c (line 27)](/E:/desk/program/STM32F103ZET62/User/App/app_encoder.c:27)

假设启动后先反向走 2 个计数，再正向走 4 个，然后反向走 1 个。为方便演示，假设最后一次有效读取刚好到达发布时刻。

| 读取   | TIM8 上次 → 本次 | 转成 `uint16_t` 的差值 | 还原的有符号增量     | 驱动返回的累计值 |
| ------ | ---------------- | ---------------------- | -------------------- | ---------------- |
| 第一次 | `0 → 65534`      | 65534                  | `65534 − 65536 = -2` | -2               |
| 第二次 | `65534 → 2`      | 4                      | +4                   | 2                |
| 第三次 | `2 → 1`          | 65535                  | `65535 − 65536 = -1` | 1                |

第二次读取是关键。直接用整数做 `2 − 65534` 得到 `-65532`，赋给 `uint16_t wrapped_delta` 后按模 `65536` 保留为 `4`。软件因此把这次跨零识别为正向前进 4 个计数。累计变量内部是 `uint32_t`，第一次的 `-2` 在内部对应 `0xFFFFFFFE`，函数返回时再映射为有符号的 `-2`。这种方向还原要求两次读取之间的实际净变化严格小于半圈计数范围，也就是绝对值小于 `32768`。 [encoder.c (line 15)](/E:/desk/program/STM32F103ZET62/User/Drivers/encoder.c:15)

第三次得到累计值 `1`，应用把 `ENC count=1` 写入节点 2 的 `node_first`，再从本地 USART1 打印。如果本轮只到 10 ms 读取点、还没到 100 ms 发布点，驱动累计值会更新，链路缓存仍保留上一次发布的文本。 [app_encoder.c (line 33)](/E:/desk/program/STM32F103ZET62/User/App/app_encoder.c:33)

### ToF 从测量结构体取出 `735 mm`

VL53L0X 启动后按配置连续定时测距，间隔设为 100 ms。`app_tof_run()` 约每 10 ms 调用 `tof_poll()` 检查是否有新结果。假设本次 `data_ready = 1`，库成功返回 `RangeStatus = 0` 和 `RangeMilliMeter = 735`，清除芯片的就绪标志也成功。驱动便执行 `*distance_mm = 735` 并返回 `tof_valid`。应用进入有效分支，刷新 `last_result_ms`，把 `TOF mm=735` 写入节点 2 的 `node_second`，然后打印。 [tof.c (line 29)](/E:/desk/program/STM32F103ZET62/User/Drivers/tof.c:29) · [app_tof.c (line 29)](/E:/desk/program/STM32F103ZET62/User/App/app_tof.c:29)

## 4. 四条样本如何从缓存装进两条 `DATA` 帧

每个节点有自己的 `records[2]`。每个槽位保存**最近一条文本和写入时间**。`app_node_sample_update()` 找到原有调试行的 `\r` 或 `\n`，只复制前面的正文，再补上 `\0`。所以下表的四条文本可以直接拼进一行协议帧。下一条有效采样会覆盖同一个槽位。 [app_node_link.c (line 12)](/E:/desk/program/STM32F103ZET61/User/App/app_node_link.c:12)

| 节点 | 槽位          | `GET` 到达前的缓存正文                                       |
| ---- | ------------- | ------------------------------------------------------------ |
| 1    | `node_first`  | `IMU_RAW ms=210 AX=1024 AY=-512 AZ=16384 GX=131 GY=-131 GZ=0` |
| 1    | `node_second` | `ADC raw=2048 mv=1650`                                       |
| 2    | `node_first`  | `ENC count=1`                                                |
| 2    | `node_second` | `TOF mm=735`                                                 |

F767 给每条链路单独维护 `seq`、`waiting` 和 `probed`。节点 1 启动后立即到期，节点 2 的首次调度约晚 50 ms。每条链路的请求周期为 100 ms。假设两条链路第一次探测都成功，那么**各自**会经历下面的请求顺序。这里假设四个样本在对应的 `GET` 被处理前已写入缓存，且仍有效。 [F767 app_hub.c (line 36)](/E:/desk/program/STM32F767_Hub/User/App/app_hub.c:36) · [节点应答逻辑 (line 51)](/E:/desk/program/STM32F103ZET61/User/App/app_node_link.c:51)

| 链路   | 首次探测，序号 `1`                    | 下次取样，序号 `2`                       |
| ------ | ------------------------------------- | ---------------------------------------- |
| 节点 1 | F767 发 `PING,1,1`，节点回 `PONG,1,1` | F767 发 `GET,1,2`，节点回 `DATA,1,2,...` |
| 节点 2 | F767 发 `PING,2,1`，节点回 `PONG,2,1` | F767 发 `GET,2,2`，节点回 `DATA,2,2,...` |

以 `GET,1,2` 为例，沿着代码走一遍。

1. **F767 发出请求。** `service_node()` 发现节点 1 已探测成功、没有在途请求并且已到期，就把序号从 `1` 加到 `2`。`link_uart_send()` 给 `GET,1,2` 追加 `\n`，通过 F767 USART2 发送。发送成功后，F767 记录 `sent_ms`，置 `waiting = true`。 [F767 app_hub.c (line 83)](/E:/desk/program/STM32F767_Hub/User/App/app_hub.c:83) · [link_uart.c (line 70)](/E:/desk/program/STM32F767_Hub/User/Comm/link_uart.c:70)
2. **节点 1 的中断把字节拼成一行。** `USART2_IRQHandler()` 调用 `HAL_UART_IRQHandler()`，HAL 再进入 `HAL_UART_RxCpltCallback()`。`link_uart_on_rx()` 每次收一个字节。收到 `G`、`E`、`T` 等字符时依次填入缓冲，收到 `\n` 时把它作为行结束，在正文末尾写入 `\0`，记录完整行的到达时间并置 `ready = true`。本例正文长度为 7 字节。 [节点 UART 中断入口 (line 204)](/E:/desk/program/STM32F103ZET61/Core/Src/stm32f1xx_it.c:204) · [节点接收回调 (line 84)](/E:/desk/program/STM32F103ZET61/User/App/app_node_link.c:84) · [link_uart.c (line 11)](/E:/desk/program/STM32F103ZET61/User/Comm/link_uart.c:11)
3. **节点 1 主循环取走请求。** `link_uart_take()` 把完整行复制到应用层的局部 `line`，随后释放接收缓冲。`link_parse()` 得到 `type = "GET"`、`node = 1`、`seq = 2`、`payload = NULL`。节点检查目标编号和正文后，用同一次 `HAL_GetTick()` 检查两个槽位的有效期，并把它们用 `|` 拼起来。`GET` 执行到这里读取的是缓存文本，采样仍由主循环里的各应用按各自周期执行。 [link_uart.c (line 51)](/E:/desk/program/STM32F103ZET61/User/Comm/link_uart.c:51) · [帧头解析 (line 102)](/E:/desk/program/STM32F103ZET61/User/Comm/link_uart.c:102) · [节点应答逻辑 (line 51)](/E:/desk/program/STM32F103ZET61/User/App/app_node_link.c:51)
4. **节点 1 应答，F767 验收并转发。** 节点从自己的 USART2 发回 `DATA,1,2,...`。F767 也按字节接收，等到 `\n` 后发布完整行。`service_node()` 要求当前确实在等待，完整行到达时间与 `sent_ms` 的差值小于 75 ms，节点号和序号匹配，帧类型为 `DATA`，且正文含有 `|`。通过后清除 `waiting`，把收到的整行从 F767 USART1 发到电脑。节点 2 走相同流程，F767 侧使用 USART6。 [F767 验收逻辑 (line 55)](/E:/desk/program/STM32F767_Hub/User/App/app_hub.c:55) · [F767 接收分派 (line 108)](/E:/desk/program/STM32F767_Hub/User/App/app_hub.c:108)

因此，电脑端会分别收到下面两行。节点 1 的帧含行尾 `\n` 共 **90 字节**，节点 2 的帧共 **32 字节**。两条链路独立调度，实际输出顺序取决于各板的启动、采样和应答时间。

```
DATA,1,2,IMU_RAW ms=210 AX=1024 AY=-512 AZ=16384 GX=131 GY=-131 GZ=0|ADC raw=2048 mv=1650
DATA,2,2,ENC count=1|TOF mm=735
```

## 5. 用同一组数据看几个边界

| 情况                                                         | 代码里的状态变化                                             | F767 电脑端结果                                           |
| ------------------------------------------------------------ | ------------------------------------------------------------ | --------------------------------------------------------- |
| 节点 2 下一次 ToF 轮询得到无效结果                           | `tof_invalid` 刷新 `last_result_ms`，清空 `node_second`，继续轮询。假设下一次请求为 `GET,2,3`，此时编码器缓存仍为 `1` | `DATA,2,3,ENC count=1|NA`                                 |
| 某槽位一直没有更新                                           | 从缓存写入时刻算起，年龄恰好 `500 ms` 时仍可用。超过 `500 ms` 后，`sample_text()` 返回 `NA` | 对应字段变成 `NA`，另一槽位照常返回                       |
| ToF 一直没有新结果                                           | 当 `tof_pending` 分支发现距上次结果已达 `500 ms`，清空距离槽位、停止后续轮询，并从节点 2 的本地 USART1 输出 `TOF TIMEOUT` | 后续 `GET` 的距离字段为 `NA`                              |
| 节点 2 的 `DATA,2,3,...` 到达时已满 `75 ms`，或序号与当前请求不匹配 | F767 不接纳该帧。当前请求在等待时间到达 `75 ms` 时结束       | F767 输出 `TIMEOUT n=2 seq=3`                             |
| 四路采样都正常，但 `GET` 刚好比缓存更新早                    | 该轮主循环先用旧缓存应答，再执行后面的采样和缓存覆盖         | 这条 `DATA` 带上一条有效样本。下一次 `GET` 有机会取得新值 |

`NA` 和 `TIMEOUT` 对应两处不同的状态。**`NA` 表示节点已经应答，但某个采样槽位当时没有有效值。`TIMEOUT` 表示 F767 这一轮没收到按时且匹配的应答。** 即便 `DATA,2,3,NA|NA` 也可以通过 F767 的协议检查，因为它具备匹配的帧头和 `|` 分隔符。两项数据的值和有效期由 F103 管理，F767 管理的是这次请求有没有得到有效应答。 [节点缓存判断 (line 36)](/E:/desk/program/STM32F103ZET62/User/App/app_node_link.c:36) · [ToF 状态处理 (line 38)](/E:/desk/program/STM32F103ZET62/User/App/app_tof.c:38) · [F767 协议验收与超时 (line 64)](/E:/desk/program/STM32F767_Hub/User/App/app_hub.c:64)







































板子之间用的是 **USART 串口上的自定义 ASCII 文本协议**。F767 主动发请求，两块 F103 分别应答。两条链路都配置为 **115200 波特率、8 个数据位、无校验、1 个停止位（8N1）**。

| 链路                  | F767 端 | F103 端                 |
| --------------------- | ------- | ----------------------- |
| 节点 1，MPU6050 + ADC | USART2  | `STM32F103ZET61` USART2 |
| 节点 2，编码器 + ToF  | USART6  | `STM32F103ZET62` USART2 |

### 帧格式

每条消息是一行 ASCII 文本，以换行符 `\n` 结束。逗号分隔帧头字段，`|` 分隔 `DATA` 帧里的两项样本。

| 消息   | 方向        | 示例                              | 含义                                |
| ------ | ----------- | --------------------------------- | ----------------------------------- |
| `PING` | F767 → F103 | `PING,2,1`                        | 探测节点 2，请求序号为 1            |
| `PONG` | F103 → F767 | `PONG,2,1`                        | 节点 2 回显请求序号，确认链路可用   |
| `GET`  | F767 → F103 | `GET,2,2`                         | 请求节点 2 当前缓存的两项样本       |
| `DATA` | F103 → F767 | `DATA,2,2,ENC count=1|TOF mm=735` | 节点 2 回应序号 2，返回编码器和距离 |

### 请求怎样配对

F767 在每条链路上先发 `PING`，收到匹配的 `PONG` 后切换为 `GET`，按 **100 ms** 周期调度请求。两条链路各有自己的序号，每条链路同一时刻最多有一个请求在等待。F767 核对应答的**节点号、序号和消息类型**，并要求整帧在请求发送完成后 **小于 75 ms** 到达。

F103 收到 `GET` 就读取已有缓存。某项样本无效或过期时，节点仍会应答，例如 `DATA,2,2,ENC count=1|NA`。如果 F767 到期仍未收到匹配的应答，它会通过自己的 USART1 向电脑输出 `TIMEOUT n=2 seq=2`。当前帧格式没有 CRC 校验字段。