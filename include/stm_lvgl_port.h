/**
 * @file    stm_lvgl_port.h
 * @brief   提供 LVGL 9 协作式显示和输入接入接口。
 */
#ifndef STM_LVGL_PORT_H
#define STM_LVGL_PORT_H
#include "stm_lcd.h"
#include "lvgl.h"
#ifdef __cplusplus
extern "C"
{
#endif
typedef struct lvgl_port_context *lvgl_port_handle_t;
typedef void (*lvgl_port_flush_observer_cb)(void *context,
                                            uint32_t pixels,
                                            stm_err_t result,
                                            int complete);
typedef void (*lvgl_port_touch_observer_cb)(
    void *context, uint32_t now, stm_err_t result, int down, uint16_t x, uint16_t y);

typedef struct
{
    stm_lcd_io_handle_t io;                     // 借用的面板 IO，须与 panel 的 IO 一致。
    stm_lcd_panel_handle_t panel;               // 借用的通用面板，仅一个刷新所有者。
    stm_lcd_touch_handle_t touch;               // 可选借用触摸，NULL 时无输入对象。
    uint16_t width, height;                     // 逻辑尺寸，须与面板一致。
    void *draw_buffer;                          // 调用方持有的对齐 RGB565 SRAM 缓冲。
    size_t draw_buffer_bytes;                   // 至少一整行，不超过 UINT32_MAX 字节。
    void *draw_buffer2;                         // 可选等大、不重叠且对齐的第二缓冲。
    uint32_t (*clock_ms)(void);                 // 必需的毫秒时钟，同时绑定 LVGL tick。
    void (*idle)(void *);                       // 可选单步空闲回调，不重入 LVGL 或 process。
    void *runtime_context;                      // 借用的空闲回调上下文。
    uint8_t draw_async;                         // 布尔异步标志，要求 IO 完成服务。
    uint8_t rgb565_swap;                        // 传输前交换 RGB565 字节序。
    uint8_t refresh_full_width;                 // 渲染前扩展脏区域至全宽。
    uint16_t refresh_align_rows;                // 0/1 不对齐，其他值在渲染前对齐行数。
    uint32_t handler_period_ms;                 // 零时使用 10 ms handler 周期。
    uint32_t touch_period_ms;                   // 零时使用 20 ms 触摸采样周期。
    uint32_t touch_stale_ms;                    // 零时使用 200 ms 触摸过期释放。
    uint32_t touch_retry_ms;                    // 零时使用 1000 ms 离线探测周期。
    uint8_t latch_display_error;                // 首次错误后停止新绘图，仍服务在途 IO。
    lvgl_port_flush_observer_cb flush_observer; // 可选刷新观察，不调用 LVGL、delete 或 process。
    lvgl_port_touch_observer_cb touch_observer; // 可选触摸观察，不转移所有权。
    void *observer_context;                     // 借用的观察上下文。
} lvgl_port_config_t;

typedef struct
{
    stm_err_t last_display_error, last_touch_error;     // 最近结果，查询不清除。
    uint8_t flush_pending, touch_online, touch_pressed; // 只读刷新及输入状态。
    uint16_t touch_x, touch_y;                          // 最近有效坐标，释放时保留。
    uint32_t last_handler_ms; // 本次 handler 耗时含 DMA 等待，未运行时为零。
} lvgl_port_status_t;

/**
 * @brief 创建协作式 port，初始化 LVGL 并绑定时钟、独占借用设备。
 *
 * @param config 设备、外部缓冲、时钟及刷新策略；不分配帧缓冲
 * @param out 输出句柄地址；初始必须为 NULL
 * @return STM_OK；配置、内存或设备借用错误时失败，不转移外部资源所有权。
 */
stm_err_t lvgl_port_create(const lvgl_port_config_t *config, lvgl_port_handle_t *out);

/**
 * @brief 删除空闲 port 并解除设备借用，不删除外部设备或缓冲。
 *
 * @param handle 句柄地址；空句柄视为已删除
 * @return STM_OK；在途、process 或观察回调期间返回 STM_ERR_INVALID_STATE。
 */
stm_err_t lvgl_port_delete(lvgl_port_handle_t *handle);

/**
 * @brief 串行主循环服务 IO、触摸及到期 handler；错误后仍服务 IO。
 *
 * @param port 已创建 port；禁止 ISR 或递归调用
 * @param now 与 clock_ms 同源的毫秒时间，可按 uint32_t 回绕
 * @return STM_OK 或锁存显示错误；重入返回 STM_ERR_INVALID_CONTEXT，触摸错误从状态读取。
 */
stm_err_t lvgl_port_process(lvgl_port_handle_t port, uint32_t now);

/**
 * @brief 获取借用的 LVGL display。
 *
 * @param port 已创建 port
 * @param display 输出 display 地址；不得由调用方单独删除
 * @return STM_OK；空参数返回 STM_ERR_INVALID_ARG。
 */
stm_err_t lvgl_port_get_display(lvgl_port_handle_t port, lv_display_t **display);

/**
 * @brief 获取借用的 LVGL 输入对象。
 *
 * @param port 已创建 port
 * @param indev 输出 indev 地址；未配置触摸时为 NULL
 * @return STM_OK；空参数返回 STM_ERR_INVALID_ARG。
 */
stm_err_t lvgl_port_get_indev(lvgl_port_handle_t port, lv_indev_t **indev);

/**
 * @brief 获取只读诊断快照，不清除错误或改变所有权。
 *
 * @param port 已创建 port
 * @param status 输出状态副本
 * @return STM_OK；空参数返回 STM_ERR_INVALID_ARG。
 */
stm_err_t lvgl_port_get_status(lvgl_port_handle_t port, lvgl_port_status_t *status);
#ifdef __cplusplus
}
#endif
#endif
