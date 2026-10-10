# stm_lvgl_port

LVGL 9 的显示领域粘合层：接收通用 IO/panel/可选 touch、外部 RGB565 缓冲、clock/idle 与刷新策略。无芯片判断、HAL 依赖、后台任务或大块缓冲自动分配。

```c
lvgl_port_config_t cfg = {
    .io = devices.io, .panel = devices.panel, .touch = devices.touch,
    .width = 170, .height = 560,
    .draw_buffer = buffer_a, .draw_buffer2 = buffer_b,
    .draw_buffer_bytes = sizeof buffer_a,
    .clock_ms = HAL_GetTick, .draw_async = 1,
    .rgb565_swap = 1, .refresh_full_width = 1, .refresh_align_rows = 8,
    .latch_display_error = 1,
};
lvgl_port_create(&cfg, &port);
/* create application UI once */
/* main loop: */
lvgl_port_process(port, HAL_GetTick());
```

create 自动初始化 LVGL（若未初始化）、绑定 tick，自动订阅面板完成并创建输入对象；应用不再提供 draw/wait/touch 包装或 flush_complete。不得再 lv_tick_inc 重复计时。

process 服务 IO、输入，再按 handler_period_ms（默认10ms）运行到期 handler。异步等待期间继续服务独立 I2C 输入，不递归 LVGL。IO停止→panel完成→LVGL ready 内部连通。出错仍服务在途 IO；latch 停止后续绘图。

输入默认20ms采样、200ms过期释放、IO/TIMEOUT离线、1000ms探测恢复。VERIFY畸形帧释放但保持在线，不自动执行设备reset。读回调只读取缓存，不访问总线。

借用设备/缓冲，每个 panel 只有一个port刷新所有者。删除必须无在途刷新并解除自身订阅/触摸借用；不删除外部设备。不支持 ISR/递归process或观察回调中操作 LVGL/删除对象。时钟不可在对象存活时替换，无多屏管理功能。

缓冲至少一行、满足 LV_DRAW_BUF_ALIGN、双缓冲大小相同且不重叠；行对齐要求尺寸和容量可整除。全宽/行对齐在渲染前的 INVALIDATE_AREA 事件扩展，不能传输时拼接未渲染像素。

## CMake 与依赖

优先复用已有 `lvgl` target；否则保留 v0.3.0 的固定 LVGL 9.3.0 获取支持：
- `STM_LVGL_PORT_LVGL_SOURCE_DIR`：显式离线源码，优先于 FetchContent 覆盖。
- `FETCHCONTENT_SOURCE_DIR_LVGL`：FetchContent 离线源码覆盖。
- `STM_LVGL_PORT_FETCH_LVGL=OFF`：没有 target/离线源码时明确失败。
- `STM_LVGL_PORT_LVGL_GIT_REPOSITORY`：可信镜像；固定提交 `c033a98afddd65aaafeebea625382a94020fe4a7`。
- 应用提供 `lv_conf.h` / `LV_BUILD_CONF_PATH`；不会强制覆盖应用的 demos/examples/ThorVG 选项。

新增 `stm_lcd` 只复用已有 target 或同级源码，缺失报错，不自动获取未发布框架。
独立 mock 测试、隔离依赖测试和工程真实 LVGL 渲染测试同时维护。见 [刷新模式说明](docs/render-modes.md)。

本次提交断开旧 draw/touch 回调 API；没有兼容包装，不是 v0.3.0 的兼容补丁。

## 验证与许可

2026-10-10，在 STM32F407 + AXS15231B 消费工程以 21 MHz SPI、170×560 原生竖屏、RGB565、两个 16 行普通 SRAM 缓冲运行，用户确认显示与触摸正常。该结论仅覆盖上述配置，未作为独立长时间 soak 验收，也不宣称既有触摸畸形帧问题已修复。当前为未发布开发提交，未创建新 tag 或 Release。

自有源码沿用维护者原 MIT，见 [LICENSE](LICENSE)。
