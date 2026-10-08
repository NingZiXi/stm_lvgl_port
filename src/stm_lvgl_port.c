/**
 * @file stm_lvgl_port.c
 *
 * @brief 同步刷新、输入及 LVGL 对象生命周期。
 */
#include "stm_lvgl_port.h"
#include <limits.h>
#include <stdlib.h>

#ifndef LV_DRAW_BUF_ALIGN
#define LV_DRAW_BUF_ALIGN 2u
#endif

struct lvgl_port_context
{
    lvgl_port_config_t config;
    lv_display_t *display;
    lv_indev_t *indev;
    lvgl_port_status_t status;
};

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    lvgl_port_handle_t port = lv_display_get_user_data(display);
    if (!port)
    {
        lv_display_flush_ready(display);
        return;
    }
    port->status.last_display_error = STM_ERR_INVALID_ARG;
    if (area && pixels && area->x1 <= area->x2 && area->y1 <= area->y2)
    {
        if (area->x1 < 0 || area->y1 < 0 || area->x2 >= port->config.width ||
            area->y2 >= port->config.height)
            port->status.last_display_error = STM_ERR_OUT_OF_RANGE;
        else
            port->status.last_display_error = port->config.draw(
                port->config.display_context, (uint16_t)area->x1, (uint16_t)area->y1,
                (uint16_t)(area->x2 + 1), (uint16_t)(area->y2 + 1), pixels);
    }
    // 同步回调报错时仍交还缓冲，避免 LVGL 卡在刷新中。
    lv_display_flush_ready(display);
}

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    lvgl_port_handle_t port = lv_indev_get_user_data(indev);
    if (!data)
        return;
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = 0;
    data->point.y = 0;
    if (!port)
        return;
    uint16_t x = 0, y = 0;
    int pressed = 0;
    stm_err_t err = port->config.touch(port->config.touch_context, &pressed, &x, &y);
    if (err == STM_OK && pressed != 0 && pressed != 1)
        err = STM_ERR_INVALID_ARG;
    if (err == STM_OK && pressed && (x >= port->config.width || y >= port->config.height))
        err = STM_ERR_OUT_OF_RANGE;
    port->status.last_touch_error = err;
    if (err == STM_OK && pressed)
    {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

stm_err_t lvgl_port_create(const lvgl_port_config_t *config, lvgl_port_handle_t *out)
{
    if (!config || !out)
        return STM_ERR_INVALID_ARG;
    if (*out)
        return STM_ERR_INVALID_STATE;
    if (!config->draw || !config->draw_buffer || !config->width || !config->height ||
        config->draw_buffer_bytes < (size_t)config->width * 2u ||
        config->draw_buffer_bytes > UINT32_MAX ||
        (uintptr_t)config->draw_buffer % LV_DRAW_BUF_ALIGN)
        return STM_ERR_INVALID_CONFIG;
    lvgl_port_handle_t port = calloc(1, sizeof(*port));
    if (!port)
        return STM_ERR_NO_MEM;
    port->config = *config;
    port->display = lv_display_create(config->width, config->height);
    if (!port->display)
    {
        free(port);
        return STM_ERR_NO_MEM;
    }
    lv_display_set_color_format(port->display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(port->display, port);
    lv_display_set_buffers(port->display, config->draw_buffer, NULL,
                           (uint32_t)config->draw_buffer_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(port->display, flush);
    if (config->touch)
    {
        port->indev = lv_indev_create();
        if (!port->indev)
        {
            lvgl_port_delete(&port);
            return STM_ERR_NO_MEM;
        }
        lv_indev_set_type(port->indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(port->indev, port->display);
        lv_indev_set_user_data(port->indev, port);
        lv_indev_set_read_cb(port->indev, read_touch);
    }
    *out = port;
    return STM_OK;
}

stm_err_t lvgl_port_delete(lvgl_port_handle_t *handle)
{
    if (!handle)
        return STM_ERR_INVALID_ARG;
    lvgl_port_handle_t port = *handle;
    if (!port)
        return STM_OK;
    if (port->indev)
        lv_indev_delete(port->indev);
    if (port->display)
        lv_display_delete(port->display);
    free(port);
    *handle = NULL;
    return STM_OK;
}

stm_err_t lvgl_port_get_display(lvgl_port_handle_t port, lv_display_t **display)
{
    if (!port || !display)
        return STM_ERR_INVALID_ARG;
    *display = port->display;
    return STM_OK;
}

stm_err_t lvgl_port_get_indev(lvgl_port_handle_t port, lv_indev_t **indev)
{
    if (!port || !indev)
        return STM_ERR_INVALID_ARG;
    *indev = port->indev;
    return STM_OK;
}

stm_err_t lvgl_port_get_status(lvgl_port_handle_t port, lvgl_port_status_t *status)
{
    if (!port || !status)
        return STM_ERR_INVALID_ARG;
    *status = port->status;
    return STM_OK;
}
