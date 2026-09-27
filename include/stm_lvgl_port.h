#ifndef STM_LVGL_PORT_H
#define STM_LVGL_PORT_H
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The board connects a chosen chip driver or LTDC framebuffer using callbacks.
 * draw must finish using pixels before it returns; 0 means success. */
typedef int (*stm_lvgl_port_draw_cb)(void *context, uint16_t x1, uint16_t y1,
                                      uint16_t x2, uint16_t y2, const void *pixels);
/* Called on each LVGL pointer poll; return 0 on success and set pressed/x/y. */
typedef int (*stm_lvgl_port_touch_cb)(void *context, int *pressed, uint16_t *x, uint16_t *y);
typedef struct {
    uint16_t width, height;
    void *draw_buffer;
    size_t draw_buffer_bytes;
    void *display_context;
    stm_lvgl_port_draw_cb draw;
    void *touch_context;
    stm_lvgl_port_touch_cb touch; /* optional */
} stm_lvgl_port_config_t;
typedef struct {
    stm_lvgl_port_config_t config;
    lv_display_t *display;
    lv_indev_t *indev;
    int last_display_error, last_touch_error;
} stm_lvgl_port_t;
/* LVGL 9 only; initialize port to zero before first attach; call lv_init first.
 * Caller owns buffers and callback contexts.
 * The application provides LVGL tick and invokes lv_timer_handler periodically. */
int stm_lvgl_port_attach(stm_lvgl_port_t *port, const stm_lvgl_port_config_t *config);
void stm_lvgl_port_detach(stm_lvgl_port_t *port);
#ifdef __cplusplus
}
#endif
#endif
