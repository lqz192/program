# 两块 STM32F103 与野火 F767 串口联调代码编写指南

用两条独立的 3.3 V UART 把两块 F103 接到 F767。F103 维持原先的采样周期，每次收到请求就回复当前缓存。F767 按周期请求两路数据，通过板载 CH340 向电脑输出汇总结果。

| 工程 | MCU | 职责 |
| --- | --- | --- |
| `STM32F103ZET61` | STM32F103ZET6 | MPU6050 六轴原始量和 PA1 ADC，节点号 `1` |
| `STM32F103ZET62` | STM32F103ZET6 | 编码器计数和 VL53L0X 测距，节点号 `2` |
| `STM32F767_Hub` | STM32F767IGT6 | 调度、接收响应、超时统计和串口转发 |

工作区根目录是 `E:\desk\program`。第二块 F103 的目录和 CMake 目标是 `STM32F103ZET62`，CubeMX 文件仍叫 `STM32F103VET62.ioc`。F767 的 `.ioc` 已选 `STM32F767IGT6`，接线前按底板 V1/V2 实物核对排针。 [野火挑战者 V1/V2 资料](https://doc.embedfire.com/stm32_products/must_read/zh/latest/doc/introduction_of_stm32/STM32/ebf_stm32f767_tiaozhanzhe_v1_v2/stm32f767_tiaozhanzhe_v1_v2.html)

## 1. 接线

| 链路 | F767 → F103 | F103 → F767 | 公共参考 |
| --- | --- | --- | --- |
| 节点 1 | F767 `PD5`（USART2 TX）→ F103 #1 `PA3`（RX） | F103 #1 `PA2`（TX）→ F767 `PD6`（USART2 RX） | 两端 `GND` |
| 节点 2 | F767 `PC6`（USART6 TX）→ F103 #2 `PA3`（RX） | F103 #2 `PA2`（TX）→ F767 `PC7`（USART6 RX） | 两端 `GND` |
| F767 → 电脑 | 按底板默认跳帽把 USART1 `PA9/PA10` 接到板载 CH340 | 电脑接底板标注为 USB 转串口的接口 | 板内已共地 |

三块板各自供电，板间只接数据和地。使用 MCU 侧的 **3.3 V TTL** 引脚。挑战者的 `PD5/PD6` 可能通过跳帽连到了板载 RS-232 或 RS-485 芯片，直连 F103 前拔掉相应跳帽。使用 `PC6/PC7` 前确认没有连接 LCD、摄像头或占用这两脚的扩展模块。优先用丝印及底板原理图确认排针，V1、V2 的跳帽位置可能不同。 [野火底板接口与跳帽说明](https://doc.embedfire.com/stm32_products/must_read/zh/latest/doc/introduction_of_stm32/STM32/ebf_stm32f767_tiaozhanzhe_v1_v2/stm32f767_tiaozhanzhe_v1_v2.html) · [F767 引脚复用表](https://www.st.com/resource/en/datasheet/stm32f767ig.pdf) · [F103 引脚复用表](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)

## 2. CubeMX 完整配置步骤

三个 `.ioc` 都使用 **STM32CubeMX 6.18.1**。两个 F103 使用 **STM32Cube FW_F1 V1.8.7**，F767 使用 **STM32Cube FW_F7 V1.17.4**。当前三个工程已配置板间 USART 和接收中断，可以对照本节核对。 [STM32CubeMX 6.18.1 手册](https://dev.st.com/stm32cube-docs/stm32cubemx/6.18.1/en/docs/markup/CubeMX_UserManual/chapters/04_4_stm32cubemx_user_interface.html) · [STM32CubeF7 包版本](https://github.com/STMicroelectronics/STM32CubeF7/blob/master/package.xml)

### 2.1 两块 F103 分别修改各自的 `.ioc`

1. 启动 CubeMX，依次通过 `File → Open Project` 打开 `STM32F103ZET61/STM32F103ZET61.ioc` 和 `STM32F103ZET62/STM32F103VET62.ioc`，每块板分别执行本节。右上方器件应为 `STM32F103ZET6`、`LQFP144`。
2. 在 `Pinout & Configuration → System Core → SYS` 确认 `Debug = Serial Wire`、`Timebase Source = SysTick`。在 `Clock Configuration` 中确认 `SYSCLK = HSI 8 MHz`、`AHB/APB1/APB2` 分频均为 `/1`。第一块板原有 ADC 时钟为 `PCLK2/2`。
3. 在 `Connectivity → USART2` 选择 `Asynchronous`。在引脚图确认 `PA2 = USART2_TX`、`PA3 = USART2_RX`。在 `Parameter Settings` 设 **115200 bit/s、8 Bits、None、1 Stop Bit、Receive and Transmit、无硬件流控**。
4. 在 `USART2 → NVIC Settings` 勾选 `USART2 global interrupt`。裸机首版可用抢占优先级 `5`、子优先级 `0`。`DMA Settings` 留空。`USART1` 继续使用 `PA9/PA10` 与同样的串口参数，只用于调试打印，无需额外勾选 USART1 中断。Middleware 页面无需添加组件。
5. 核对原有采样外设。第一块板的 `I2C1` 为 `PB6/PB7`、`100 kHz`，ADC1 使用 `PA1/IN1`、`55.5 cycles`，软件触发。第二块板的 `I2C2` 为 `PB10/PB11`、`100 kHz`，TIM8 编码器为 `PC6/PC7`、`TI1 and TI2`、周期 `65535`。当前第二块板的 `IC1Filter = 8`、`IC2Filter = 0`。在 `TIM8 → Parameter Settings` 把通道 2 的输入滤波设为 `8`，保存进 `.ioc`，生成后核对 `tim.c` 两路一致。
6. 在 `Project Manager → Project` 确认 `Toolchain/IDE = CMake`、`Default Compiler/Linker = GCC`。在 `Code Generator` 勾选分外设生成 `.c/.h`，并勾选 `Keep User Code when re-generating`。保存 `.ioc`，点 `GENERATE CODE`。
7. 核对生成的 `Core/Inc/usart.h` 有 `huart2`。`Core/Src/usart.c` 有 `MX_USART2_UART_Init()`，速率为 `115200`。`Core/Src/stm32f1xx_it.c` 里的 `USART2_IRQHandler()` 应调用 `HAL_UART_IRQHandler(&huart2)`。`main.c` 在用户初始化区之前调用 `MX_USART2_UART_Init()`。重生成后再核对两块板原有采样调用仍在各自的 `USER CODE` 区。

原有采集代码与上板方法见 [MPU6050 指南](STM32F103ZET6_MPU6050_Guide.md)、[ADC 指南](STM32F103ZET6_ADC_Guide.md)和[编码器及测距指南](STM32F103ZET6_Encoder_ToF_Guide.md)。

### 2.2 F767 调度器工程

1. 对当前工程，在 `File → Open Project` 打开 `STM32F767_Hub/STM32F767_Hub.ioc`。从空目录重建时，使用 `File → New Project → MCU/MPU Selector`，搜索 `STM32F767IGT6`，确认 `LQFP176` 后点 `Start Project`。
2. `Pinout & Configuration → System Core → SYS` 设置 `Serial Wire` 和 `SysTick`。首版 `Clock Configuration` 选 `HSI 16 MHz` 做 `SYSCLK`，`AHB`、`APB1`、`APB2` 均为 `/1`，无需开启 PLL。若出现 USART 时钟选择，`USART2` 选 `PCLK1`，`USART1/6` 选 `PCLK2`。 [F767 时钟说明](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-mcus.pdf)
3. `Connectivity` 内开启下表三个串口。逐个设置 `Asynchronous`、`115200 bit/s`、`8 Bits`、`None`、`1 Stop Bit`、`Receive and Transmit`、禁用硬件流控、`Oversampling = 16`。

| 串口 | 引脚 | NVIC |
| --- | --- | --- |
| `USART1` | `PA9 TX`、`PA10 RX` | 调试输出，关闭 USART1 中断 |
| `USART2` | `PD5 TX`、`PD6 RX` | `USART2 global interrupt`，抢占优先级 `5`、子优先级 `0` |
| `USART6` | `PC6 TX`、`PC7 RX` | `USART6 global interrupt`，抢占优先级 `5`、子优先级 `0` |

4. 在 SPLIT 视图或 `GPIO Settings` 核对引脚复用。若 CubeMX 自动选了别的脚，单击引脚图选择表中的 USART 信号。三个串口均不配置 DMA，Middleware 保持关闭。当前接线占用的引脚若已启用 `LTDC`、`DCMI` 或其他复用外设，先处理冲突再生成。
5. `Project Manager → Project` 将 `Project Name` 填为 `STM32F767_Hub`，`Project Location` 选工作区的 `E:\desk\program`，选择 `CMake` 和 `GCC`。`Code Generator` 勾选外设分文件与保留用户代码。保存 `.ioc`，点 `GENERATE CODE`。
6. 核对 `usart.c` 生成 `MX_USART1_UART_Init()`、`MX_USART2_UART_Init()`、`MX_USART6_UART_Init()`，并核对 `stm32f7xx_it.c` 的 USART2、USART6 中断函数都调用各自的 `HAL_UART_IRQHandler()`。在 `main.c` 中确认三个初始化都发生在 `USER CODE BEGIN 2` 前。

## 3. 模块和通信协议

### 创建与修改位置

表中的路径均从 `E:\desk\program` 开始。先在各工程内建立缺少的 `User/Comm`、`User/App` 目录。F767 目前还需要创建 `User` 目录。每行对应一个文件。

| 操作 | 文件路径 | 对应代码 |
| --- | --- | --- |
| 新建 | `STM32F103ZET61/User/Comm/link_uart.h` | 第 4 节共享接口 |
| 新建 | `STM32F103ZET61/User/Comm/link_uart.c` | 第 4 节共享实现 |
| 新建 | `STM32F103ZET61/User/App/app_node_link.h` | 第 5.1 节节点应用接口 |
| 新建 | `STM32F103ZET61/User/App/app_node_link.c` | 第 5.1 节节点应答实现 |
| 修改 | `STM32F103ZET61/User/App/app_imu.c` | 第 5.2 节，更新节点 1 的第一条记录 |
| 修改 | `STM32F103ZET61/User/App/app_adc.c` | 第 5.2 节，更新节点 1 的第二条记录 |
| 修改 | `STM32F103ZET61/Core/Src/main.c` | 第 5.3 节，节点号填 `1U` |
| 修改 | `STM32F103ZET61/CMakeLists.txt` | 第 5.3 节，添加新文件和头文件目录 |
| 新建 | `STM32F103ZET62/User/Comm/link_uart.h` | 把第 4 节的头文件也放进第二块板 |
| 新建 | `STM32F103ZET62/User/Comm/link_uart.c` | 把第 4 节的实现也放进第二块板 |
| 新建 | `STM32F103ZET62/User/App/app_node_link.h` | 与第一块板第 5.1 节的头文件相同 |
| 新建 | `STM32F103ZET62/User/App/app_node_link.c` | 与第一块板第 5.1 节的实现相同 |
| 修改 | `STM32F103ZET62/User/App/app_encoder.c` | 第 5.2 节，更新节点 2 的第一条记录 |
| 修改 | `STM32F103ZET62/User/App/app_tof.c` | 第 5.2 节，更新节点 2 的第二条记录 |
| 修改 | `STM32F103ZET62/Core/Src/main.c` | 第 5.3 节，节点号填 `2U` |
| 修改 | `STM32F103ZET62/CMakeLists.txt` | 第 5.3 节，添加新文件和头文件目录 |
| 新建 | `STM32F767_Hub/User/Comm/link_uart.h` | 把第 4 节的头文件也放进 F767 |
| 新建 | `STM32F767_Hub/User/Comm/link_uart.c` | 把第 4 节的实现也放进 F767 |
| 新建 | `STM32F767_Hub/User/App/app_hub.h` | 第 6 节调度器接口 |
| 新建 | `STM32F767_Hub/User/App/app_hub.c` | 第 6 节调度器实现 |
| 修改 | `STM32F767_Hub/Core/Src/main.c` | 第 6 节，调用 F767 调度器 |
| 修改 | `STM32F767_Hub/CMakeLists.txt` | 第 6 节，添加新文件和头文件目录 |

串口和中断文件 `Core/Inc/usart.h`、`Core/Src/usart.c`、`Core/Src/stm32f1xx_it.c` 或 `Core/Src/stm32f7xx_it.c` 由各自的 CubeMX 工程生成。两块 F103 已有的采样驱动、第二块板的 `User/Bsp/debug_uart.c/.h` 和 `ThirdParty/vl53l0x/` 直接复用。后续代码块中的 `User/...` 路径从该板工程根目录计算。

### 模块职责

| 模块 | 放置位置 | 职责 |
| --- | --- | --- |
| `link_uart.c/.h` | 三个工程各一份，`User/Comm/` | 接收一行、发送一行、解析文本帧头 |
| `app_node_link.c/.h` | 两个 F103，`User/App/` | 缓存各自的两个采样记录、回复 `PING` 和 `GET` |
| 原采样模块 | 两个 F103，原路径 | 按原节拍更新记录和本机日志 |
| `app_hub.c/.h` | F767，`User/App/` | 两路独立定时请求、检查编号和序号、向电脑转发 |
| 各工程 `main.c` | `Core/Src/` | 初始化并调用应用入口 |

线上的协议使用 ASCII，每帧以 `LF` 结束，接收端也接受 `CRLF`。帧内节点号和请求序号都是 `uint16_t` 的十进制表示。`PING,1,0` 对应 `PONG,1,0`，`GET,2,42` 对应 `DATA,2,42,ENC count=16|TOF mm=250`。`|` 左右分别是两个独立的采样记录。某个传感器无有效样本、报错或数据超过 `500 ms` 时，F103 在相应位置回 `NA`。F767 报 `TIMEOUT n=...` 表示整条链路没等到有效响应，与 `NA` 含义不同。

F767 对每条 UART 同时只发一条待回复的请求，检查序号后才接纳回复。这样每条链路用一个接收行缓冲即可。回调只收字节，主循环解析和处理；发送为带限时的阻塞式 HAL 发送。本方案用于两路短线和低频轮询。

## 4. 三个工程共用的通信代码

`User/Comm/link_uart.h`

```c
#ifndef LINK_UART_H  // 防止重复包含。
#define LINK_UART_H  // 定义头文件保护。
#include "main.h"  // 使用当前工程的 STM32 HAL 类型。
#include <stdbool.h>  // 提供 bool。
#include <stdint.h>  // 提供固定宽度整数。
#define LINK_LINE_CAPACITY 224U  // 一行最多 223 字节，另留字符串结束符。
typedef struct {  // 保存一条 UART 的接收状态。
    UART_HandleTypeDef *uart;  // 对应 CubeMX 的 UART 句柄。
    uint8_t rx_byte;  // HAL 每次接收的一个字节。
    volatile char line[LINK_LINE_CAPACITY];  // 中断写入、主循环读取的行缓冲。
    volatile bool ready;  // 完整行发布后，中断保留该缓冲区。
    uint16_t used;  // 当前行长度，只由接收中断维护。
    bool discard;  // 丢弃当前不完整的行直至下一个换行。
} link_uart;  // UART 接收上下文。
typedef struct {  // 保存解析后的帧头。
    char type[5];  // 命令最多四字符，末尾加 NUL。
    uint16_t node;  // 节点号。
    uint16_t seq;  // 请求序号。
    const char *payload;  // 指向原字符串的数据正文，空指针表示无正文。
} link_frame;  // 文本帧的解析结果。
bool link_uart_start(link_uart *link, UART_HandleTypeDef *uart);  // 开启字节接收中断。
void link_uart_on_rx(link_uart *link);  // HAL 接收完成回调的处理函数。
void link_uart_on_error(link_uart *link);  // HAL 通信错误回调的处理函数。
bool link_uart_take(link_uart *link, char line[LINK_LINE_CAPACITY]);  // 取一条完整行。
bool link_uart_send(UART_HandleTypeDef *uart, const char *line);  // 追加 LF 后发送。
bool link_parse(const char *line, link_frame *frame);  // 解析帧头及正文位置。
#endif  // 结束头文件保护。
```

`User/Comm/link_uart.c`

```c
#include "link_uart.h"  // 引入共享串口接口。
#include <stdio.h>  // 使用 snprintf 生成传输行。
#include <string.h>  // 使用 strchr 和 memcpy 处理帧头。
bool link_uart_start(link_uart *link, UART_HandleTypeDef *uart)  // 上电时绑定并启动接收。
{  // 初始化必须在该串口接收开始前调用。
    *link = (link_uart){ .uart = uart };  // 清空行状态并保存串口句柄。
    return HAL_UART_Receive_IT(uart, &link->rx_byte, 1U) == HAL_OK;  // 接收首个字节。
}  // 初始化结束。
void link_uart_on_rx(link_uart *link)  // 接收中断每次提交一个字节。
{  // 这里避免格式化和阻塞发送。
    uint8_t byte = link->rx_byte;  // 保存本次 HAL 收到的值。
    if (byte == '\n') {  // LF 标志一帧结束。
        if (!link->ready && !link->discard && link->used != 0U) {  // 仅发布完整的非空行。
            link->line[link->used] = '\0';  // 终止 C 字符串。
            link->ready = true;  // 发布缓冲区，等主循环复制。
        }  // 完成帧发布判断。
        link->used = 0U;  // 下一帧从头开始。
        link->discard = false;  // 换行后退出丢弃状态。
    } else if (byte != '\r' && !link->discard) {  // 兼容 CRLF，并跳过已损坏的行。
        if (link->ready || byte < 32U || byte > 126U || link->used >= LINK_LINE_CAPACITY - 1U) {  // 检查占用、ASCII 与长度。
            link->discard = true;  // 丢到换行并保留已经发布的行。
        } else {  // 当前字节可以追加。
            link->line[link->used++] = (char)byte;  // 保存字符并递增长度。
        }  // 完成本字节入队。
    }  // 完成字节分类。
    (void)HAL_UART_Receive_IT(link->uart, &link->rx_byte, 1U);  // 继续接收下一个字节。
}  // 接收回调结束。
void link_uart_on_error(link_uart *link)  // 处理 UART 接收错误。
{  // 从下一行的边界恢复。
    link->used = 0U;  // 丢掉当前未完成的长度。
    link->discard = true;  // 等待 LF，避免把残片当成新帧。
    (void)HAL_UART_Receive_IT(link->uart, &link->rx_byte, 1U);  // HAL 结束接收时重启，仍在接收时会返回忙。
}  // 错误回调结束。
bool link_uart_take(link_uart *link, char line[LINK_LINE_CAPACITY])  // 主循环调用，单消费者。
{  // 已发布的源缓冲在 ready 清除前不会被中断改写。
    if (!link->ready) {  // 检查是否有完整行。
        return false;  // 没有完整行就继续其他任务。
    }  // 本次有一帧可读。
    uint16_t index = 0U;  // 从首字节开始复制。
    do {  // 复制正文和 NUL。
        line[index] = link->line[index];  // 读取中断发布的字符。
    } while (line[index++] != '\0');  // 遇到字符串结束符停止。
    link->ready = false;  // 释放共享行缓冲。
    return true;  // 调用方得到一条完整行。
}  // 结束取行。
bool link_uart_send(UART_HandleTypeDef *uart, const char *line)  // 共享的 USART 发送接口。
{  // 传入正文由函数统一加换行。
    char wire[LINK_LINE_CAPACITY + 1U];  // 可容纳最长正文、LF 和 NUL。
    int length = snprintf(wire, sizeof(wire), "%s\n", line);  // 形成单条 ASCII 帧。
    if (length <= 0 || (size_t)length >= sizeof(wire)) {  // 发送前排除格式化失败或截断。
        return false;  // 让调用方处理发送失败。
    }  // 缓冲区现有完整帧。
    return HAL_UART_Transmit(uart, (uint8_t *)wire, (uint16_t)length, 30U) == HAL_OK;  // 发送有效字节并限制等待时间。
}  // 发送结束。
static bool read_u16(const char **cursor, uint16_t *out)  // 解析无符号十进制字段。
{  // 成功时游标停在下一个分隔符。
    const char *field = *cursor;  // 从当前字段首字符开始。
    uint32_t value = 0U;  // 用较宽类型累积，便于做范围检查。
    if (*field < '0' || *field > '9') {  // 至少需要一位数字。
        return false;  // 拒绝空字段和符号。
    }  // 首字符合法。
    do {  // 逐位转换。
        value = value * 10U + (uint32_t)(*field++ - '0');  // 将新数字追加到数值。
        if (value > UINT16_MAX) {  // 拒绝溢出的节点号或序号。
            return false;  // 不对超范围的值做窄化转换。
        }  // 当前值可表示。
    } while (*field >= '0' && *field <= '9');  // 读完该字段。
    *cursor = field;  // 保存下一个分隔符的位置。
    *out = (uint16_t)value;  // 返回合法字段值。
    return true;  // 数值解析完成。
}  // 结束数值解析。
bool link_parse(const char *line, link_frame *frame)  // 解析 TYPE,node,seq[,payload]。
{  // 由应用层判断是否支持该 TYPE。
    const char *comma = strchr(line, ',');  // 查找类型字段末尾。
    if (comma == NULL || comma == line || (size_t)(comma - line) >= sizeof(frame->type)) {  // 检查类型长度和分隔符。
        return false;  // 帧头缺失或类型过长。
    }  // 类型长度合法。
    size_t type_length = (size_t)(comma - line);  // 计算类型字符串长度。
    memcpy(frame->type, line, type_length);  // 拷贝类型字段。
    frame->type[type_length] = '\0';  // 为 strcmp 补上 NUL。
    const char *field = comma + 1;  // 指到节点号字段。
    if (!read_u16(&field, &frame->node) || *field != ',') {  // 检查节点号及其后的逗号。
        return false;  // 节点号无效。
    }  // 准备解析序号。
    ++field;  // 跳过节点号末尾的逗号。
    if (!read_u16(&field, &frame->seq)) {  // 读取请求序号。
        return false;  // 序号无效。
    }  // 序号合法。
    if (*field == '\0') {  // PING、PONG、GET 没有正文。
        frame->payload = NULL;  // 用空指针表示正文缺失。
        return true;  // 接收该帧头。
    }  // 还有字符时需要正文分隔符。
    if (*field != ',' || field[1] == '\0') {  // 正文前应有逗号，正文自身不能为空。
        return false;  // 拒绝异常尾部。
    }  // 正文格式初步有效。
    frame->payload = field + 1;  // 正文仍指向调用者的行缓冲。
    return true;  // 帧头解析完成。
}  // 结束帧解析。
```

## 5. 两块 F103 的采集与应答

### 5.1 两个工程共用节点应用

在两个 F103 工程都建立以下文件。USART2 负责协议，原有 USART1 的 `send_line()` 仍负责本机日志。

`User/App/app_node_link.h`

```c
#ifndef APP_NODE_LINK_H  // 防止重复包含。
#define APP_NODE_LINK_H  // 定义头文件保护。
#include <stdint.h>  // 提供节点号所用的整数类型。
typedef enum { node_first, node_second } node_slot;  // 两条数据各占一个缓存位。
void app_node_link_init(uint16_t node_id);  // 设置本机编号并开启接收。
void app_node_link_run(void);  // 主循环取一条请求并应答。
void app_node_sample_update(node_slot slot, const char *line);  // 传 NULL 时使该采样值失效。
#endif  // 结束头文件保护。
```

`User/App/app_node_link.c`

```c
#include "app_node_link.h"  // 本模块的公开接口。
#include "link_uart.h"  // 共享 UART 收发和帧解析。
#include "usart.h"  // 引入 F103 上的 huart2。
#include <stdio.h>  // 使用 snprintf 格式化响应。
#include <string.h>  // 使用 strcspn、memcpy 和 strcmp。
static const uint32_t sample_expire_ms = 500U;  // 超过此时间的样本用 NA 应答。
static link_uart node_link;  // 本节点的板间串口状态。
static uint16_t local_node;  // 本节点的地址。
static bool link_ready;  // 记录 USART2 接收是否启动成功。
static struct {  // 记录两路独立采集的状态。
    char text[96];  // 包含最长的现有 IMU 日志及结束符。
    uint32_t sample_ms;  // 本地记录更新时间。
} records[2];  // 启动时静态清零，两条记录都无效。
void app_node_sample_update(node_slot slot, const char *line)  // 原采样模块在主循环更新缓存。
{  // 调用方传入一个合法的槽位。
    if (line == NULL) {  // 传感器明确报错或结果无效。
        records[slot].text[0] = '\0';  // 立即撤销该槽的有效值。
        return;  // 等待下一条有效样本。
    }  // 下面保存文本。
    size_t length = strcspn(line, "\r\n");  // 去掉原有串口日志末尾的换行。
    if (length >= sizeof(records[slot].text)) {  // 确保能保存完整记录。
        records[slot].text[0] = '\0';  // 超长记录标为无效。
        return;  // 下一次采样可覆盖它。
    }  // 缓存空间足够。
    memcpy(records[slot].text, line, length);  // 复制采样字段。
    records[slot].text[length] = '\0';  // 添加字符串结束符。
    records[slot].sample_ms = HAL_GetTick();  // 用于判断样本是否过期。
}  // 采样缓存更新完成。
static const char *sample_text(node_slot slot, uint32_t now_ms)  // 按当前时刻取有效数据。
{  // 使用无符号时间差，支持毫秒计数回卷。
    if (records[slot].text[0] == '\0' || now_ms - records[slot].sample_ms > sample_expire_ms) {  // 检查样本有效性。
        return "NA";  // 继续应答请求并标记缺失值。
    }  // 数据可使用。
    return records[slot].text;  // 返回最近的有效采样记录。
}  // 结束有效性检查。
void app_node_link_init(uint16_t node_id)  // 在 MX_USART2_UART_Init 后调用。
{  // 初始化只运行一次。
    local_node = node_id;  // 保存节点编号。
    link_ready = link_uart_start(&node_link, &huart2);  // 在 USART2 上启动字节接收中断。
}  // 结束节点初始化。
void app_node_link_run(void)  // 从主循环频繁调用。
{  // 一次至多取一条请求。
    char line[LINK_LINE_CAPACITY];  // 保存收到的请求。
    link_frame request;  // 保存解析后的帧头。
    if (!link_ready || !link_uart_take(&node_link, line) || !link_parse(line, &request)) {  // 检查收帧及解析结果。
        return;  // 未收到合法请求就交还主循环。
    }  // 继续检查帧地址和格式。
    if (request.node != local_node || request.payload != NULL) {  // 本节点只接无正文的请求。
        return;  // 忽略其他节点或有异常正文的帧。
    }  // 该请求属于本节点。
    int length;  // 保存应答格式化结果。
    if (strcmp(request.type, "PING") == 0) {  // 链路探测请求。
        length = snprintf(line, sizeof(line), "PONG,%u,%u", (unsigned)local_node, (unsigned)request.seq);  // 回显请求序号。
    } else if (strcmp(request.type, "GET") == 0) {  // 读取两条采样记录。
        uint32_t now_ms = HAL_GetTick();  // 在同一时刻判断两条样本是否有效。
        length = snprintf(line, sizeof(line), "DATA,%u,%u,%s|%s", (unsigned)local_node, (unsigned)request.seq,  // 构造响应帧头。
                          sample_text(node_first, now_ms), sample_text(node_second, now_ms));  // 追加两条缓存记录。
    } else {  // 命令类型未知。
        return;  // 忽略并继续采样。
    }  // 应答已准备好。
    if (length > 0 && (size_t)length < sizeof(line)) {  // 确认应答完整。
        (void)link_uart_send(&huart2, line);  // 在请求对应的板间串口回复。
    }  // 没有发出的应答由 F767 超时处理。
}  // 本轮节点任务结束。
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)  // HAL 串口接收完成回调。
{  // 每个工程只能有一份此回调定义。
    if (uart == &huart2) {  // 仅处理协议串口。
        link_uart_on_rx(&node_link);  // 传入字节并继续接收。
    }  // 其他串口留给项目已有处理。
}  // 接收分派结束。
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)  // HAL 串口错误回调。
{  // 每个工程只能有一份此回调定义。
    if (uart == &huart2) {  // 仅恢复协议串口。
        link_uart_on_error(&node_link);  // 舍弃损坏的行并继续监听。
    }  // 串口分派结束。
}  // 错误分派结束。
```

如果项目已经有这两个 HAL 回调，把相应的 `if (uart == &huart2)` 合并进去。保留 CubeMX 生成的中断函数，由 HAL 统一调用回调。

### 5.2 从现有采样代码更新缓存

下面的调用全部放在主循环调用的采样函数内。各文件顶部加入共享应用接口。

```c
#include "app_node_link.h"  // 引入采样缓存及节点应答接口。
```

| 工程与文件 | 放入的位置 | 有效样本调用 | 无效样本位置与调用 |
| --- | --- | --- | --- |
| #1 `User/App/app_imu.c` | `app_imu_run()` 的 `snprintf` 长度检查内，紧挨 `send_line(line)` | `app_node_sample_update(node_first, line);` | MPU6050 读帧失败分支调用 `app_node_sample_update(node_first, NULL);` |
| #1 `User/App/app_adc.c` | `app_adc_run()` 的 `snprintf` 长度检查内，紧挨 `send_line(line)` | `app_node_sample_update(node_second, line);` | `adc_input_read()` 失败分支调用 `app_node_sample_update(node_second, NULL);` |
| #2 `User/App/app_encoder.c` | `app_encoder_run()` 的计数日志长度检查内，紧挨 `send_line(line)` | `app_node_sample_update(node_first, line);` | `encoder_start()` 失败时没有生成有效样本，开机缓存为空 |
| #2 `User/App/app_tof.c` | `case tof_valid` 内的日志长度检查内，紧挨 `send_line(line)` | `app_node_sample_update(node_second, line);` | `tof_invalid`、实际 `TOF TIMEOUT` 分支及 `tof_io_error` 分别调用 `app_node_sample_update(node_second, NULL);` |

有效调用例子，放在 F103 #1 的 MPU6050 日志长度判断内。同位置的 `send_line(line)` 保留一份。

```c
app_node_sample_update(node_first, line);  // 缓存这次 MPU6050 原始值供 F767 查询。
send_line(line);  // 在本机 USART1 输出同一条采样日志。
```

`tof_pending` 表示测量进行中。这个分支只有实际超时时才把前一条有效结果设为无效。调度期间编码器硬件持续计数，当前代码每约 `100 ms` 向缓存发布一条编码器记录。

### 5.3 两个 F103 的 `main.c` 和 CMake

在 `Core/Src/main.c` 的 `USER CODE BEGIN Includes` 区加入应用接口。

```c
#include "app_node_link.h"  // 引入节点通信应用接口。
```

在 `USER CODE BEGIN 2`、所有 `MX_*_Init()` 后加入下面**对应本板**的一行。原有采集模块初始化继续保留。

```c
app_node_link_init(1U);  // F103 #1 使用节点号 1。
```

```c
app_node_link_init(2U);  // F103 #2 使用节点号 2。
```

在 `USER CODE BEGIN 3` 每轮调用。可放在原有采样调用之前，先处理已经收到的请求。

```c
app_node_link_run();  // 有完整请求时用缓存回应。
```

把以下内容追加到两个工程根 `CMakeLists.txt` 的现有 `target_sources` 与 `target_include_directories`，也可以在原块之后单独写一组。

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE  # 将板间通信编入本工程的固件目标。
    User/Comm/link_uart.c  # 共享串口通信逻辑。
    User/Comm/link_uart.h  # 共享通信接口，加入 IDE 项目树。
    User/App/app_node_link.c  # F103 节点应答和缓存代码。
    User/App/app_node_link.h  # F103 节点接口，加入 IDE 项目树。
)  # 结束源文件列表。
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE  # 设置当前目标的头文件搜索路径。
    User/Comm  # 可直接包含 link_uart.h。
    User/App  # 可直接包含 app_node_link.h。
)  # 结束头文件目录列表。
```

## 6. F767 的调度器代码

调度器有两个独立状态。两路请求间隔均为 `100 ms`，初始错开约 `50 ms`。先发 `PING`，匹配 `PONG` 后进入 `GET` 轮询。发送完成后开始 `75 ms` 应答计时。一次超时只影响当前节点，后续继续请求，节点重新接入即可恢复。

`User/App/app_hub.h`

```c
#ifndef APP_HUB_H  // 防止重复包含。
#define APP_HUB_H  // 定义头文件保护。
void app_hub_init(void);  // 初始化两条 UART 接收链路。
void app_hub_run(void);  // 运行一轮 F767 调度。
#endif  // 结束头文件保护。
```

`User/App/app_hub.c`

```c
#include "app_hub.h"  // 本模块的公开接口。
#include "link_uart.h"  // 共用收发和帧解析逻辑。
#include "usart.h"  // 引入 huart1、huart2、huart6。
#include <stdio.h>  // 使用 snprintf 生成请求和状态日志。
#include <string.h>  // 使用 strcmp 核对帧类型。
typedef struct {  // 每条链路持有独立的调度状态。
    link_uart link;  // 接收状态和外设句柄。
    uint16_t node;  // 对端节点号。
    uint16_t seq;  // 本节点的请求序号。
    uint32_t last_request_ms;  // 上一次安排请求的时间。
    uint32_t sent_ms;  // 当前请求发送完成时间。
    bool enabled;  // 接收中断是否初始化成功。
    bool waiting;  // 是否正在等待此序号的应答。
    bool probed;  // 收到匹配的 PONG 后开始 GET。
} hub_node;  // 一条 F103 链路的上下文。
static hub_node nodes[2];  // 分别对应节点 1 和节点 2。
static const uint32_t poll_period_ms = 100U;  // 每条链路的请求间隔。
static const uint32_t response_timeout_ms = 75U;  // 发送完成后的等待上限。
static void log_event(const char *event, const hub_node *node)  // 把链路状态输出到电脑。
{  // 格式化一条包含节点和序号的诊断。
    char line[48];  // 容纳诊断名和最大序号。
    int length = snprintf(line, sizeof(line), "%s n=%u seq=%u", event, (unsigned)node->node, (unsigned)node->seq);  // 生成日志。
    if (length > 0 && (size_t)length < sizeof(line)) {  // 只输出完整日志。
        (void)link_uart_send(&huart1, line);  // 通过 CH340 发送到电脑。
    }  // 完成诊断输出。
}  // 日志函数结束。
void app_hub_init(void)  // 在三个 UART 初始化之后调用。
{  // 两路启动结果分别保存。
    uint32_t now_ms = HAL_GetTick();  // 读取共同的起始时刻。
    nodes[0].node = 1U;  // USART2 接节点 1。
    nodes[0].last_request_ms = now_ms - poll_period_ms;  // 让第一路立即到期。
    nodes[0].enabled = link_uart_start(&nodes[0].link, &huart2);  // 开启 USART2 接收。
    nodes[1].node = 2U;  // USART6 接节点 2。
    nodes[1].last_request_ms = now_ms - poll_period_ms / 2U;  // 第二路稍后到期。
    nodes[1].enabled = link_uart_start(&nodes[1].link, &huart6);  // 开启 USART6 接收。
    for (unsigned index = 0U; index < 2U; ++index) {  // 检查两路启动状态。
        if (!nodes[index].enabled) {  // 某一路接收启动失败。
            log_event("RX_START_ERR", &nodes[index]);  // 标记失败节点并保留另一条链路。
        }  // 处理完该节点的启动结果。
    }  // 完成启动检查。
}  // 调度器初始化结束。
static void service_node(hub_node *node, uint32_t now_ms)  // 处理一条链路的一轮任务。
{  // 各链路独立处理接收、超时和发送。
    if (!node->enabled) {  // 跳过接收初始化失败的串口。
        return;  // 让另一路继续服务。
    }  // 当前链路可用。
    if (node->waiting && now_ms - node->sent_ms >= response_timeout_ms) {  // 检查等待是否到期。
        node->waiting = false;  // 当前序号失效。
        log_event("TIMEOUT", node);  // 向电脑报告超时。
    }  // 继续检查接收缓冲。
    char line[LINK_LINE_CAPACITY];  // 用同一缓冲接收应答或构造请求。
    link_frame reply;  // 保存已经解析的应答头。
    if (link_uart_take(&node->link, line) && node->waiting && link_parse(line, &reply) &&  // 取行并解析。
        reply.node == node->node && reply.seq == node->seq) {  // 只接纳匹配当前请求的响应。
        bool pong = !node->probed && strcmp(reply.type, "PONG") == 0 && reply.payload == NULL;  // 验证探测应答。
        bool data = node->probed && strcmp(reply.type, "DATA") == 0 && reply.payload != NULL &&  // 验证数据应答。
                    strchr(reply.payload, '|') != NULL;  // 检查两条样本有分隔符。
        if (pong || data) {  // 收到预期的应答类型。
            node->waiting = false;  // 完成本次请求。
            node->probed = true;  // 后续按数据请求模式轮询。
            (void)link_uart_send(&huart1, line);  // 把原应答转发给电脑。
        }  // 畸形应答继续等待，最终由超时处理。
    }  // 完成本轮接收检查。
    if (node->waiting || now_ms - node->last_request_ms < poll_period_ms) {  // 每路最多一条请求在途。
        return;  // 等现有请求完成或下个周期。
    }  // 本轮可以安排新请求。
    node->last_request_ms = now_ms;  // 记录本次请求调度时刻。
    ++node->seq;  // 递增 uint16_t 序号，允许自然回卷。
    int length = snprintf(line, sizeof(line), "%s,%u,%u", node->probed ? "GET" : "PING",  // 选择数据或探测请求。
                          (unsigned)node->node, (unsigned)node->seq);  // 加入目标节点和请求序号。
    if (length > 0 && (size_t)length < sizeof(line) && link_uart_send(node->link.uart, line)) {  // 发送完整请求。
        node->sent_ms = HAL_GetTick();  // 发送完成后开始等待计时。
        node->waiting = true;  // 等待匹配的应答。
    } else {  // 格式化或串口发送失败。
        log_event("TX_ERR", node);  // 记录失败并在下个周期重试。
    }  // 完成发送结果处理。
}  // 一轮链路任务结束。
void app_hub_run(void)  // 主循环连续调用。
{  // 用每条链路实际被处理时的时间调度。
    service_node(&nodes[0], HAL_GetTick());  // 处理 USART2 与节点 1。
    service_node(&nodes[1], HAL_GetTick());  // 处理 USART6 与节点 2。
}  // 两条链路都已处理。
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)  // HAL 字节接收回调。
{  // 按实际 UART 分派。
    if (uart == &huart2) {  // 节点 1 的链路。
        link_uart_on_rx(&nodes[0].link);  // 保存字节并继续接收。
    } else if (uart == &huart6) {  // 节点 2 的链路。
        link_uart_on_rx(&nodes[1].link);  // 保存字节并继续接收。
    }  // 忽略其他串口的回调。
}  // 接收分派结束。
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)  // HAL 串口错误回调。
{  // 按发生错误的 UART 恢复。
    if (uart == &huart2) {  // 节点 1 的链路。
        link_uart_on_error(&nodes[0].link);  // 丢弃损坏的行。
    } else if (uart == &huart6) {  // 节点 2 的链路。
        link_uart_on_error(&nodes[1].link);  // 丢弃损坏的行。
    }  // 其余串口按项目已有逻辑处理。
}  // 错误分派结束。
```

在 F767 的 `main.c` 分别把下列三行放在 `USER CODE BEGIN Includes`、`USER CODE BEGIN 2` 和 `USER CODE BEGIN 3` 区域。

```c
#include "app_hub.h"  // 放入 Includes 区，由 main 调用调度器。
app_hub_init();  // 放入 USER CODE 2 区，在三个串口初始化后执行。
app_hub_run();  // 放入 while 循环的 USER CODE 3 区，每轮执行。
```

把 F767 新文件加入根 `CMakeLists.txt`。

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE  # 将共享通信和调度应用加入 F767 固件。
    User/Comm/link_uart.c  # 接收、解析与发送实现。
    User/Comm/link_uart.h  # 共享 UART 公开接口。
    User/App/app_hub.c  # F767 两路轮询和转发。
    User/App/app_hub.h  # F767 调度接口。
)  # 结束源文件列表。
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE  # 配置新增头文件目录。
    User/Comm  # 查找 link_uart.h。
    User/App  # 查找 app_hub.h。
)  # 结束头文件目录列表。
```

## 7. 构建、烧录和联调

三个工程各自的 `CMakePresets.json` 都有 `Debug` 预设。工程根 `CMakeLists.txt` 决定源码和目标名，`cmake/gcc-arm-none-eabi.cmake` 指定本板 ARM 工具链参数和链接脚本。在 CLion 分别打开各工程目录，选择对应的 `Debug` 预设。

| 板子 | `Debug` 构建成功后的烧录文件 |
| --- | --- |
| F103 #1 | `STM32F103ZET61/build/Debug/STM32F103ZET61.elf` |
| F103 #2 | `STM32F103ZET62/build/Debug/STM32F103ZET62.elf` |
| F767 | `STM32F767_Hub/build/Debug/STM32F767_Hub.elf` |

1. 三个项目分别在各自工程根目录运行下方命令。若 `.ioc` 改过，先生成代码再构建。第二块板目录迁移前留下的 `STM32F103VET62.elf` 属于旧目标。若配置仍引用旧目录，在 `STM32F103ZET62` 下先用 `cmake --fresh --preset Debug` 重新生成缓存，再构建当前目标。

```powershell
cmake --list-presets  # 核对当前工程定义的构建预设。
cmake --preset Debug  # 生成当前板的 Debug 构建配置。
cmake --build --preset Debug  # 编译当前板的 ELF 固件。
```

2. 逐板用 ST-Link 的 SWD 接口连接，在 STM32CubeProgrammer 选择各自 `build/Debug/` 下的新 `.elf`。按项目名核对后再点 `Start Programming`。 [ST 烧录步骤](https://dev.st.com/stm32cube-docs/stm32cube-getting-started/1.1.1/en/markup/Ecosystem_Code_Programming/Ecosystem_Code_Programming.html)
3. 只接节点 1，电脑打开 F767 板载 CH340 对应的 COM 口，`115200、8N1`。先看到 `PONG,1,...`，随后看 `DATA,1,...`。移动 MPU6050 或变化 PA1 输入，核对响应载荷变化。未接的节点 2 会有 `TIMEOUT n=2`。
4. 接入节点 2，旋转编码器和移动测距目标，分别检查两条记录。拔掉测距模块或使测距无效时，节点 2 仍应答，测距记录变为 `NA`。在 F767 侧拔掉节点 1 的通信线，节点 2 应继续输出数据。恢复连线后，确认节点 1 可以继续回包。
5. 分别验证 `PONG`、`DATA`、`NA` 与 `TIMEOUT`。若完全没有 `PONG`，检查 TX/RX 交叉、三板共地、F767 的底板跳帽、两个 USART 中断及 `HAL_UART_Receive_IT()`。若有 `DATA` 而字段一直是 `NA`，先看对应 F103 原有 USART1 采样日志，再核对 `app_node_sample_update()` 的插入位置。
