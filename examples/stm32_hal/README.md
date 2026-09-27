# LVGL 9：STM32 HAL 接入示例

本示例演示 `lv_init` → `stm_lvgl_port_attach` → 创建标签 → 周期性 `lv_tick_inc` 与 `lv_timer_handler`。`draw` 为已初始化屏幕的同步刷新回调，`touch` 可选；板级 SPI、LTDC、DMA、像素缓存一致性及锁由应用提供。**尚未经过屏幕实板验证**。代码使用 H7 HAL 头文件，其他 STM32 系列应替换对应头文件；不是完整 CubeMX 工程，LVGL 9 必须先加入应用且提供 CMake `lvgl` 目标。

把 `example.c` 放入 CM7 应用目标，确保板级面板完成初始化后再调用以下示意。这里用 ST7789；实际是 ST7796 时改成对应组件。示例只借助 HAL tick 推进 LVGL，不在 `SysTick_Handler` 里重复调用 `lv_tick_inc`。

```c
#include "example.h"
#include "stm_lcd_st7789.h"
#include "stm_lcd_touch_ft5206.h" /* 没有 FT5206 时删除此行与下方可选触摸回调 */

#define LCD_WIDTH 240u /* 用真实模组宽高替换 */
#define LCD_HEIGHT 320u
static uint8_t lvgl_buffer[LCD_WIDTH * 20u * 2u]; /* 至少宽度×2 字节，需可写且长寿命 */
static stm_lvgl_port_t port;
extern stm_lcd_st7789_t panel; /* 已由板级初始化 */
extern stm_lcd_touch_ft5206_t touch; /* 可选；已由板级初始化 */

static int draw(void *context, uint16_t x1, uint16_t y1,
                uint16_t x2, uint16_t y2, const void *pixels)
{
    return stm_lcd_st7789_draw_bitmap((stm_lcd_st7789_t *)context,
                                       x1, y1, x2, y2, pixels);
}

static int read_touch(void *context, int *pressed, uint16_t *x, uint16_t *y)
{
    stm_lcd_touch_ft5206_t *device = (stm_lcd_touch_ft5206_t *)context;
    stm_lcd_touch_ft5206_point_t point;
    size_t count = 0;
    int rc = stm_lcd_touch_ft5206_read_data(device);
    if (rc == 0) rc = stm_lcd_touch_ft5206_get_data(device, &point, 1u, &count);
    if (rc != 0) return rc;
    *pressed = count > 0;
    if (count) { *x = point.x; *y = point.y; }
    return 0;
}

void app_main(void)
{
    /* 先在板级代码里初始化 SPI/GPIO、panel、I²C/touch。 */
    stm_lvgl_port_config_t cfg = {
        .width = LCD_WIDTH, .height = LCD_HEIGHT,
        .draw_buffer = lvgl_buffer, .draw_buffer_bytes = sizeof(lvgl_buffer),
        .display_context = &panel, .draw = draw,
        .touch_context = &touch, .touch = read_touch, /* 无触摸时两项都设为 NULL */
    };
    int rc = stm_lvgl_port_example_start(&port, &cfg);
    if (rc != 0) { /* 记录错误码并停止绘图。 */ return; }
    for (;;) {
        stm_lvgl_port_example_step();
        HAL_Delay(5);
    }
}
```

LVGL 颜色格式为 RGB565，端口回调收到的是排他右下角坐标；若面板传输需要交换 RGB565 字节序或 DCache 维护，应在板级处理。`draw()` 必须在返回前完成像素传输；不能直接启动 DMA 后立即返回。RTOS 多任务时序列化所有 LVGL API，避免两个任务同时进入 handler。确认屏幕方向、颜色和触摸方向后再做实板测试。

在 CM7 的 `CMakeLists.txt` 把复制到 `App/` 的示例源码加入应用目标（具体目录名按工程调整）：

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/App/example.c)
```

中文主页：[README.md](../../README.md)；[完整接入指南](https://github.com/NingZiXi/stm32-hal-lib/blob/main/docs/display-components.md)。
