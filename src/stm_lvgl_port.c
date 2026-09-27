#include "stm_lvgl_port.h"
#include <limits.h>
#include <string.h>
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    stm_lvgl_port_t *port = (stm_lvgl_port_t *)lv_display_get_user_data(display);
    if (!port) { lv_display_flush_ready(display); return; }
    port->last_display_error = -1;
    if (area && pixels && area->x1 >= 0 && area->y1 >= 0 &&
        area->x1 <= area->x2 && area->y1 <= area->y2 &&
        area->x2 < port->config.width && area->y2 < port->config.height)
        port->last_display_error = port->config.draw(port->config.display_context,
            (uint16_t)area->x1, (uint16_t)area->y1,
            (uint16_t)(area->x2 + 1), (uint16_t)(area->y2 + 1), pixels);
    /* The draw callback is synchronous; release even on error to avoid LVGL hanging. */
    lv_display_flush_ready(display);
}
static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    stm_lvgl_port_t *port = (stm_lvgl_port_t *)lv_indev_get_user_data(indev);
    uint16_t x = 0, y = 0;
    int pressed = 0;
    port->last_touch_error = port->config.touch(port->config.touch_context, &pressed, &x, &y);
    data->state = LV_INDEV_STATE_RELEASED;
    if (!port->last_touch_error && pressed && x < port->config.width && y < port->config.height) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}
int stm_lvgl_port_attach(stm_lvgl_port_t *port, const stm_lvgl_port_config_t *config)
{
    if (!port || !config || port->display || !config->draw || !config->draw_buffer ||
        !config->width || !config->height ||
        config->draw_buffer_bytes < (size_t)config->width * 2u ||
        config->draw_buffer_bytes > UINT32_MAX) return -1;
    memset(port, 0, sizeof(*port));
    port->config = *config;
    port->display = lv_display_create(config->width, config->height);
    if (!port->display) return -2;
    lv_display_set_color_format(port->display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(port->display, port);
    lv_display_set_buffers(port->display, config->draw_buffer, NULL,
                           (uint32_t)config->draw_buffer_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(port->display, flush);
    if (config->touch) {
        port->indev = lv_indev_create();
        if (!port->indev) { stm_lvgl_port_detach(port); return -2; }
        lv_indev_set_type(port->indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(port->indev, port->display);
        lv_indev_set_user_data(port->indev, port);
        lv_indev_set_read_cb(port->indev, read_touch);
    }
    return 0;
}
void stm_lvgl_port_detach(stm_lvgl_port_t *port)
{
    if (!port) return;
    if (port->indev) lv_indev_delete(port->indev);
    if (port->display) lv_display_delete(port->display);
    memset(port, 0, sizeof(*port));
}
