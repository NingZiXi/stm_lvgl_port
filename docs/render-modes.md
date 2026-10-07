# PARTIAL 与板级 DIRECT 接入

本组件创建同步 RGB565 PARTIAL display 及可选输入设备。DIRECT 是应用通过借用的 display 实现的板级扩展；组件不选择 LTDC、DMA、缓存或同步策略。公开 API 和实现与 v0.2.0 相同。

## 选择方式与责任

| 项目 | 组件默认 PARTIAL | 应用扩展 DIRECT 双缓冲 |
| --- | --- | --- |
| 典型硬件 | SPI/并口面板自带显存，或应用自行复制到扫描缓冲 | LTDC/DSI 持续扫描外部帧缓冲 |
| 缓冲 | 一块至少一行、通常几十行的 RGB565 临时缓冲 | 两块完整屏幕，配置明确的 stride |
| 像素含义 | 紧密排列的本次矩形，起点是矩形左上角 | 整帧基址，dirty area 不改变全屏 stride |
| 刷新完成 | draw 返回后组件调用 flush_ready | 板级 flush 自行调用 flush_ready |
| 双缓冲同步 | 无整帧同步 | LVGL 9.3.0 在 DIRECT 双缓冲间同步已渲染区域 |
| 扫描地址与旧缓冲释放 | 应用完成同步传输/复制 | 板级确认地址切换，旧前台不再被扫描后才能归还 |
| 缓存、色序、VSYNC | 板级决定，组件不操作 | 板级决定，不能由通用 port 推测 |
| 显示错误 | get_status.last_display_error | 板级独立状态；get_status 不会收到替换后的 flush 结果 |
| 输入错误 | get_status.last_touch_error，错误上报松开 | 同左，保留组件创建的 indev |

DIRECT 减少整屏复制，但需要两个全屏缓冲，不保证任意硬件上都更快。4 ms 的 LVGL 刷新周期只是处理周期，不能作为实际 FPS 或面板刷新率。

## 同步 PARTIAL：完成传输才返回

数据流为：`LVGL 渲染矩形 → port 转换坐标 → draw → 完成同步传输/复制 → draw 返回 → port flush_ready`。

组件将 LVGL 闭区间矩形转换为 `[x1,x2) × [y1,y2)`，回调不能再次给 x2/y2 加一。字节数为 `(x2-x1)*(y2-y1)*2`。临时缓冲没有整屏 stride，不能把它作为 LTDC 整帧地址。

应用分配并持有缓冲及 context，直至停止 handler 并删除 port；create 复制配置，不复制像素、不释放缓冲。缓冲满足 `LV_DRAW_BUF_ALIGN`，字节数至少完整一行。CPU 本机 RGB565 内存顺序不一定等于面板总线顺序，线上转换由板级 draw 负责。

若 draw 使用 DMA，必须等待 DMA 实际消费完像素后再返回。启动 DMA 后立即返回违反同步契约；此组件没有异步完成入口。draw 也不应自行调用 flush_ready，否则会重复归还。失败时返回原始 stm_err_t，组件仍归还缓冲并记录最近错误；这不代表像素回滚或自动重试。

若复制到正在被 LTDC 扫描的单缓冲，应用仍须自行解决撕裂；PARTIAL 本身不提供 VSYNC。应用设置 tick（如 lv_tick_set_cb(HAL_GetTick)），周期调用 lv_timer_handler，并串行访问 LVGL/port。

## DIRECT：首次渲染前完成配置

板级先初始化 SDRAM、MPU/DCache、两帧、LTDC 与屏幕，再创建 port。用 get_display 获取借用对象，**首次 lv_timer_handler 前**替换缓冲和 flush；此后不要动态切换模式。保留 display/indev user_data，不自行删除借用对象。

```c
lv_display_t *display = NULL;
/* port 已创建，cfg.draw 为会返回 INVALID_STATE 的保护回调。 */
stm_err_t err = lvgl_port_get_display(port, &display);
if (err != STM_OK) return err;
/* 初始化 LTDC 扫描 A，所以 LVGL 先画 B；大小为每帧字节数。 */
lv_display_set_buffers_with_stride(display, frame_b, frame_a,
    frame_bytes, stride_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
lv_display_set_flush_cb(display, board_flush_direct);
```

板级 flush 在 `lv_display_flush_is_last(display)` 时提交完整帧，其他块直接归还。提交顺序是 CPU 渲染完成、清理脏缓存并 DSB、等待安全切帧时机、切换 LTDC 地址、确认旧帧不再被扫描、flush_ready。若改用异步垂直消隐重载，写入 shadow 地址不代表切换完成，必须等硬件重载完成后才通知 LVGL；不能照搬同步示例立即归还。

成功和失败都必须完成 LVGL 的刷新握手。切帧失败时旧帧可能仍在扫描，**先锁存错误并禁止再次提交，当前 handler 返回后立即停止后续 handler**；不能只归还缓冲后继续渲染以为已恢复。触摸错误仍从 port 查询，板级显示错误不要被 port 的 PARTIAL 成功状态覆盖。停止 handler 后删除 port，再回收板级上下文和缓冲。

## DCache 和扫描边界

CPU 写、LTDC 读时，在 LTDC 读取新帧前 clean 脏数据；不能用 invalidate 丢弃 CPU 尚未写回的像素。LVGL 对两帧的 CPU 同步也会产生脏缓存，因此每次提交需覆盖所有待扫描的脏区域。简单可靠的起点是整帧 clean，后续局部 clean 必须包含 LVGL 同步区域。

H7 的缓存行是 32 字节。帧基址、清理区间和内存属性由应用保证；向外取整的局部区间不得误操作其他对象共享的缓存行。若引入 DMA 写入帧缓冲，还需重新设计 CPU/DMA 一致性，不能沿用仅 CPU 写入的规则。

H757 示例沿用厂商 CDSR VSYNC 轮询 + HAL_LTDC_SetAddress 立即重载，不是通用的异步 VSYNC 机制。该轮询以 100 ms 超时退出，依赖 HAL tick 工作，期间阻塞 handler；安全窗口由板级时序和调度保证。其既有实测不能证明所有负载下绝无撕裂。

## 验证范围

port 的 PARTIAL 生命周期、坐标转换、回调错误、刷新失败归还与输入释放由主机测试覆盖；主机桩不证明 DMA/cache/VSYNC 正确。H757 的 ILI9881C/GT9271、LVGL 9.3.0 DIRECT 路径在 2026-10-07 的 v0.2.0 配套实例完成显示、交互及复位回归。本文说明职责，不把该结果扩大为其他板、面板或 PARTIAL 实板验收。
