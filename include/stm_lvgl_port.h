/** @file stm_lvgl_port.h @brief LVGL 9 cooperative display and input integration. */
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
    stm_lcd_io_handle_t io;       /**< Borrowed panel IO, must equal panel's IO. */
    stm_lcd_panel_handle_t panel; /**< Borrowed generic panel, one refresh owner. */
    stm_lcd_touch_handle_t touch; /**< Optional borrowed generic touch. */
    uint16_t width, height;       /**< Logical size, must match panel. */
    void *draw_buffer;            /**< Caller-owned aligned RGB565 SRAM. */
    size_t draw_buffer_bytes;     /**< At least a row, at most UINT32_MAX. */
    void *draw_buffer2;           /**< Optional equally sized disjoint aligned buffer. */
    uint32_t (*clock_ms)(void);   /**< Required monotonic clock, also bound to LVGL tick. */
    void (*idle)(void *);         /**< Optional one-step CPU idle, must not reenter LVGL/process. */
    void *runtime_context;        /**< Borrowed idle context. */
    uint8_t draw_async;           /**< Boolean; requires IO completion service. */
    uint8_t rgb565_swap;          /**< Swap each pixel before transfer. */
    uint8_t refresh_full_width;   /**< Expand invalidation before rendering. */
    uint16_t refresh_align_rows;  /**< 0/1 disabled, otherwise render-time row alignment. */
    uint32_t handler_period_ms;   /**< Zero uses 10 ms cooperative scheduling. */
    uint32_t touch_period_ms;     /**< Zero uses 20 ms sampling. */
    uint32_t touch_stale_ms;      /**< Zero uses 200 ms release deadline. */
    uint32_t touch_retry_ms;      /**< Zero uses 1000 ms probe retry. */
    uint8_t latch_display_error;  /**< Stop new transfers after first error, keep draining IO. */
    lvgl_port_flush_observer_cb flush_observer; /**< Optional, cannot call LVGL/delete/process. */
    lvgl_port_touch_observer_cb touch_observer; /**< Optional sample diagnostics, no ownership. */
    void *observer_context;                     /**< Borrowed diagnostics context. */
} lvgl_port_config_t;

typedef struct
{
    stm_err_t last_display_error, last_touch_error;     /**< Latest results, not reset by get. */
    uint8_t flush_pending, touch_online, touch_pressed; /**< Read-only ownership/input snapshot. */
    uint16_t touch_x, touch_y; /**< Last valid coordinates retained on release. */
    uint32_t
        last_handler_ms; /**< Current process handler duration; zero when not run, includes DMA waits. */
} lvgl_port_status_t;

/* Initializes LVGL if necessary, binds clock, claims devices. No buffer allocation.
 * All API/LVGL calls serialized in main-loop; no ISR, callback deletion or recursion.
 * No multi-display manager is provided. Threads and changing the tick while live are unsupported. */
stm_err_t lvgl_port_create(const lvgl_port_config_t *config, lvgl_port_handle_t *out);
stm_err_t lvgl_port_delete(lvgl_port_handle_t *handle);
stm_err_t lvgl_port_process(lvgl_port_handle_t port, uint32_t now);
stm_err_t lvgl_port_get_display(lvgl_port_handle_t port, lv_display_t **display);
stm_err_t lvgl_port_get_indev(lvgl_port_handle_t port, lv_indev_t **indev);
stm_err_t lvgl_port_get_status(lvgl_port_handle_t port, lvgl_port_status_t *status);
#ifdef __cplusplus
}
#endif
#endif
