# 通用设备接入 LVGL 示例

[example.h](example.h) / [example.c](example.c) 提供 start/step 两个示例入口：start 创建 port 并在其活动屏幕上放置标签，step 调用协作式 process。示例包装不是组件强制架构，应用也可直接调用公开 API。

调用前由应用配置并初始化 IO、面板、可选触摸，提供静态对齐 RGB565 缓冲、clock 和刷新策略，输出 port 初始为空。start 返回成功后主循环调用 `lvgl_port_example_step(port, now)`；now 必须与配置时钟同源。

port 自动初始化 LVGL/tick，连接完成和输入服务。不要另外运行 lv_tick_inc、lv_timer_handler，或创建 draw/wait/touch 包装。HAL 回调由应用显式转发到 IO 适配 IRQ 邮箱，不在 ISR 调用公共完成或 LVGL。

此示例不初始化 HAL、引脚或设备，也不是可直接烧录项目。不要猜测板级配置；先删除无在途操作的 port，再删除设备，最后 deinit IO。缓冲仍由应用持有。接入示例与参数见[组件 README](../../README.md)。
