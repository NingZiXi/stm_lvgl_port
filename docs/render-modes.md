# 渲染模式与缓冲所有权

本说明对应 port v1.1.0 的通用接口扩展，需要 stm_lcd v1.1.0；原 port v1.0.0 只有 PARTIAL。两种模式均为 RGB565，默认 PARTIAL。应用/平台负责实际内存布局、DCache 与扫描硬件；port 只通过通用面板能力连接 LVGL。

## PARTIAL：紧密区域

一块或两块外部缓冲，每块至少一整行，大小不超过 UINT32_MAX，对齐符合 LVGL；双缓冲等大且不重叠。LVGL 渲染局部区域，port 把包含端点转成半开矩形交给面板，不附加未渲染像素。

同步返回后输入可复用；SPI DMA 则 flush → panel → IO → HAL 邮箱 → 主循环安全完成 → panel 完成 → LVGL ready。失败但硬件 busy 保留请求和缓冲，不提前 ready。`rgb565_swap` 仅按传输要求交换一次。全宽/多行对齐发生在 INVALIDATE_AREA 渲染前。

ILI9881C PARTIAL 使用平台 `draw_region` 同步按行复制紧密区域至其 framebuffer；目的帧行跨度由平台处理，CPU 写完 clean 所需 cache。此路径不承诺 VSYNC 原子整帧无撕裂，不自动添加 DMA2D。

## DIRECT：完整扫描帧

必须两块完整等大 RGB565 帧；每块精确 width×height×2，地址对齐且区间不重叠，LVGL 实际行跨度须为 width×2。选择 `LVGL_PORT_RENDER_DIRECT`，不混用 draw_async、rgb565_swap、refresh_full_width 或大于 1 的 refresh_align_rows。

`lvgl_port_create` 自行完成 LVGL 冷启动初始化，再读取实际行跨度；应用无需提前调用 `lv_init()`。PARTIAL 和 DIRECT 均支持首次创建。

port 配置真实 LVGL DIRECT 模式，LVGL 负责渲染/同步两个整帧的脏区；只在一轮最后 flush 提交完整帧首地址。中间 flush 即时 ready 不表示整帧已上屏。IO 必须提供整套 present/process/busy/stop_scanout；command-only IO 不支持。

| 时刻 | 平台/框架行为 | 允许应用/LVGL 做什么 |
| --- | --- | --- |
| 首次提交 A | 保守借用 A，安排安全生效 | 等完成，不修改 A |
| VSYNC 完成 A | process 确认生效，complete 一次；A 持续扫描 | ready 后可渲染 B |
| 提交 B | A 仍扫描，B 等待换帧，两帧均受保护 | 等完成，不修改 A/B |
| VSYNC 完成 B | A 停止扫描，B 开始扫描 | ready 后可复用 A；B 仍借用 |
| stop_scanout 成功 | 扫描与任何 DMA/预取均不再引用帧 | 可以删除/重建并释放两帧 |
| 失败/停止未成功 | 首个错误保留，缓冲/对象继续保留 | 继续 process，禁止清零/释放/复用 |

`busy` 表示换帧/硬件访问在途；正常持续扫描本身不是 busy。没有 pending 不等于 framebuffer 已归还，框架另记 scanout 借用。成功完成释放的是旧帧；新帧直至下次成功切换或 stop 才释放。

## 平台边界与错误

`present` 先处理本帧所有 CPU 写入的 DCache，再安排 LTDC/DSI/其他控制器安全切换。VSYNC/IRQ 只记录邮箱，`process` 主循环检查旧帧已不被扫描/预取后调用 `stm_lcd_io_complete` 恰好一次。DMA 复制完成或设置寄存器不等于 VSYNC 完成。

应用保证外部 SDRAM 已初始化、两个帧已准备、内存可扫描且不与其他使用区重叠；自行配置时钟、layer、像素格式、cache-line 对齐及相关同步。port/芯片不实现 HAL 中断、链接脚本、DCache 或板级电源操作。

失败可在提交前、提交仍忙或完成时发生。框架不假设失败硬件自动停止：持续服务，保留首个错误，确认 stop 成功后才通知错误完成。DIRECT 错误强制锁存，阻止后续 LVGL 渲染；停止失败时 wait 仍等待并持续服务，不能为避免等待而提前 ready。平台超时/停止策略应保证最终能安全停止；无法停止则保留缓冲直到应用处理故障。

删除 DIRECT port 先 stop，成功后退订；失败返回底层错误、保留完整对象可重试。不在本轮加入自动重启、FULL、任意 stride、DMA2D、RTOS 或多屏。

## 验证边界

主机故障注入覆盖异步/扫描停止、原始错误保留、非法缓冲与删除；真实 LVGL 9.3.0 软件 LTDC 模型检查局部渲染、整帧交替和当前扫描帧不被 CPU 修改。STM32H757 的 HAL 示例编译证明接入语法，不是完整固件链接或实板回归。F407/AXS 的历史 PARTIAL 与 H757 旧 DIRECT 板测均不能替代本次扩展验收。
