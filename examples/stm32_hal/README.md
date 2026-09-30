# lvgl_port：STM32 HAL 接入示例

本目录提供可复制到已有 HAL 应用的 `example.c` / `example.h`，不是完整 CubeMX 工程。先初始化板级时钟、GPIO 和总线，再传入实际 HAL 句柄/引脚。代码使用 STM32H7 HAL，其他系列自行替换头文件；示例不会自动编入组件库。

当前示例对应未发布的新 API；软件验证通过后仍需按实物回归。v0.1.0 与 H757、LVGL 9.3.0、ILI9881C/GT9271 已完成持续刷新、交互和复位观察；软件迁移版本尚未实板回归。

## 接入步骤

1. 将组件和 stm_common 加入 CMake；LVGL port 先提供 LVGL 9 target。
2. 将本目录两个源码文件复制到应用，替换 HAL 头文件和实际板级参数。
3. 以 NULL 初始化句柄，按 example.h 的 start 接口创建；板级结构体必须持久有效。
4. 循环绘图/读取触点或调用 LVGL handler，检查每一步 `err != STM_OK`。
5. 停止所有访问后调用 `lvgl_port_delete(&handle)`。

```cmake
target_sources(your_firmware PRIVATE App/example.c)
target_include_directories(your_firmware PRIVATE App)
target_link_libraries(your_firmware PRIVATE stm_lvgl_port)
```

HAL_TIMEOUT 映射为 STM_ERR_TIMEOUT，HAL_ERROR/HAL_BUSY 映射为 STM_ERR_IO，start 失败保留首个错误并回收本次创建的对象，重复 start 不覆盖已有句柄。传输同步完成后才能复用缓冲；阻塞 API 不从中断调用。

## LVGL 最小主循环

```c
lvgl_port_handle_t port = NULL;
/* 先完成板级屏幕初始化，提供持久配置 cfg、同步 draw 和可选 touch。 */
lv_init(); /* 应用统一调用一次。 */
stm_err_t err = lvgl_port_example_start(&cfg, &port);
if (err != STM_OK) return;
for (;;) {
    lvgl_port_example_step(); /* HAL tick 的实际时间差 + lv_timer_handler。 */
    HAL_Delay(5);
}
```

缓冲需满足 LV_DRAW_BUF_ALIGN、至少一行 RGB565。示例基于借用的 display 创建标签，避免依赖全局默认屏幕；应用不要在其他入口重复推进 tick。draw 接收半开矩形和同步像素；touch 成功写 pressed/x/y。lvgl_port_get_status 查询错误，get_display 获取借用对象供板级设置 DIRECT；替换 flush 后独立记录其错误。

完整 API、错误和资源契约见[中文主页](../../README.md)。
