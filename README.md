# stm_lvgl_port

LVGL 9 粘合组件：板级代码通过 `draw` 回调连接已确认的 SPI 面板芯片驱动或 RGB LTDC 帧缓冲，`touch` 回调可选。组件不依赖某个具体屏幕或触摸库。提供已命名的 `lvgl` CMake 目标后才能添加本组件；CubeMX/HAL 和 LVGL 的具体接法见[显示与触摸接入指南](https://github.com/NingZiXi/stm32-hal-lib/blob/main/docs/display-components.md)。

完整中文示例：[`examples/stm32_hal/README.md`](examples/stm32_hal/README.md)（含 LVGL tick、handler 与屏幕/触摸回调）。

```c
static stm_lvgl_port_t port = {0};
static uint8_t draw_buffer[BOARD_LCD_WIDTH * 20u * 2u]; /* RGB565，20 行 */
stm_lvgl_port_config_t cfg = {
    .width = BOARD_LCD_WIDTH, .height = BOARD_LCD_HEIGHT,
    .draw_buffer = draw_buffer, .draw_buffer_bytes = sizeof(draw_buffer),
    .display_context = &panel, .draw = board_draw,
    /* 已确认 FT5206 时再设置 .touch_context 与 .touch。 */
};
lv_init();
int rc = stm_lvgl_port_attach(&port, &cfg);
/* 应用提供 lv_tick_inc(实际经过的毫秒) 并定期调用 lv_timer_handler()。 */
```

`board_draw(context, x1, y1, x2, y2, pixels)` 的右下端点**不包含**；端口会把 LVGL 的包含式坐标转换好。缓冲区至少为完整一行 RGB565，回调要同步完成使用后再返回；异步 DMA 必须在返回前等待且处理缓存一致性。`touch(context, pressed, x, y)` 每次读指针时调用。最近一次错误保存在 `last_display_error` / `last_touch_error`，刷新失败也会交还 LVGL 缓冲。组件不负责 tick、任务同步、颜色转换或面板复位；退出前可调用 `stm_lvgl_port_detach`。目前主机测试通过，仍需在实板上核对方向、像素序和触摸范围。

```cmake
# 先提供 LVGL 9 的目标 lvgl。
add_subdirectory(Lib/stm_lvgl_port)
target_link_libraries(app PRIVATE stm_lvgl_port) # app 改为你的实际目标名
```
