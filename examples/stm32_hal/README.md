# 通用 LVGL 接入示例

[example.c](example.c) 接受已初始化设备/缓冲的 `lvgl_port_config_t`，创建 port 后建立简单标签，[example.h](example.h) 声明启动/周期服务入口。本示例不包含 HAL 初始化或芯片判断，可接 ST7789、ST7796、ILI9881C 等通用面板及 FT5206、GT9271 等通用触摸。

1. 添加匹配的 stm_common/stm_lcd、所需芯片组件、LVGL 9.3.0 与 stm_lvgl_port target；将本示例加入应用，提供 lv_conf.h。
2. 完成实际外设/供电配置；保存持续有效且清零的 IO，芯片 create → 可选 reset → init；GT 在 port 借用前读取 ID。
3. 配置 `.io/.panel/.touch`（可为空）、逻辑尺寸、外部对齐缓冲与 clock_ms；PARTIAL 默认为同步，SPI 字节顺序按实物设置。
4. 调用 `lvgl_port_example_start(&config, &port)`，主循环调用 `lvgl_port_example_step(port, now)`；不再手动 tick/handler 或采样触摸。
5. 退出先删除 port，再设备和 IO，最后硬件/缓冲。失败时按返回错误保留仍被借用的资源。

DIRECT 需显式 render_mode、两块完整紧密 RGB565 帧和平台整套 present/process/busy/stop_scanout；应用不替换 display 的 port flush/wait 回调。平台完成与 DCache/VSYNC 责任见[渲染模式](../../docs/render-modes.md)。

原 port v0.3.0 的回调连接及手动 tick 示例不适用于本工作区；原 v1.0.0 也不含 DIRECT，本示例使用 port v1.1.0 与 stm_lcd v1.1.0。2026-10-11，port 在 H757 + ILI9881C/GT9271 上通过 DIRECT 诊断 Debug 显示/触摸和五次软件复位；PARTIAL 与其他面板实板范围不变，见[组件 README](../../README.md)。create 自动初始化 LVGL 后检查行跨度，应用不需要预先 lv_init()。
