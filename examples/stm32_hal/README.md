# 最小接入示例

使用当前通用 `stm_lcd` API，无旧回调包装。示例不是可直接烧录的完整板级项目。

屏幕/触摸示例依赖 `stm_lcd` 的可选 STM32F4 HAL 适配，需应用实现 `board_init`、时钟/引脚/外设和复位协调。LVGL示例仅接收配置，port自动初始化LVGL和tick；主循环调用 example_step(port, now)，不要另行调用lv_tick_inc或handler。

HAL回调由应用显式转发，IRQ中禁止调用通用完成/LVGL。删除port后才删除设备，最后deinit IO；缓冲静态由应用持有。
