# stm_lvgl_port

LVGL 9 glue for a board-selected display and optional touch controller; independent of all chip packages. Zero-initialize `stm_lvgl_port_t`; call `lv_init()`, provide an RGB565 draw buffer of at least one full line, then call `stm_lvgl_port_attach`. Board callbacks may call `stm_lcd_st7789_draw_bitmap`, `stm_lcd_st7796_draw_bitmap`, or copy a rectangle into an LTDC framebuffer. `x2` and `y2` are exclusive. The draw callback must finish using the input pixels before returning (wait for DMA completion); the default port does not support asynchronous flush, color conversion, cache maintenance, tick or thread locking. `touch` is optional and supplies a pressed flag and in-range coordinates. Errors are recorded in `last_display_error` / `last_touch_error` and failed flushes still release LVGL's buffer.

```c
stm_lvgl_port_t port = {0};
stm_lvgl_port_config_t cfg = {
    .width = 240, .height = 320,
    .draw_buffer = draw_buffer, .draw_buffer_bytes = sizeof(draw_buffer),
    .display_context = &lcd,
    .draw = board_lcd_draw, // wraps the selected chip's draw_bitmap
    .touch_context = &touch, .touch = board_touch_read, // optional
};
lv_init();
stm_lvgl_port_attach(&port, &cfg);
/* App supplies lv_tick_inc and periodically invokes lv_timer_handler. */
```

```cmake
# The project first supplies an LVGL 9 target named lvgl.
add_subdirectory(Lib/stm_lvgl_port)
target_link_libraries(app PRIVATE stm_lvgl_port)
```

This is source and host-test validated only. Confirm actual screen, pixel-byte order, panel timing, LVGL version and input orientation on the hardware before release.
