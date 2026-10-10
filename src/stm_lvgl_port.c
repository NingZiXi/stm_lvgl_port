/**
 * @file    stm_lvgl_port.c
 * @brief   实现同步及异步刷新、输入服务和 LVGL 对象生命周期。
 */
#include "stm_lvgl_port.h"
#include <limits.h>
#include <stdlib.h>

#ifndef LV_DRAW_BUF_ALIGN
#define LV_DRAW_BUF_ALIGN 2u
#endif

struct lvgl_port_context
{
    lvgl_port_config_t config; // 复制配置，借用设备和像素缓冲。
    lv_display_t *display;     // 本对象拥有的显示实例。
    lv_indev_t *indev;         // 可选输入实例，先于显示删除。
    lvgl_port_status_t status; // 可查询的诊断快照。
    uint8_t flush_pending;     // 缓冲仍被传输层借用，仅主循环访问。
    uint32_t transfer_pixels;  // 当前脏区像素数，供观察回调使用。
    stm_err_t display_fault;   // 禁止新刷新后的首个显示错误。
    stm_err_t flush_result;    // 当前刷新首个错误，完成通知不得覆盖。
    uint32_t last_poll, last_probe, last_handler; // 同源毫秒调度时间。
    uint8_t online, pressed, processing, observing, subscribed,
        touch_claimed;                   // 输入、重入及借用状态。
    uint16_t last_touch_x, last_touch_y; // 最后有效按下坐标，释放时保留。
};

// 在渲染前扩展脏区域。
static void round_area(lv_event_t *event)
{
    lvgl_port_handle_t port = lv_event_get_user_data(event);
    lv_area_t *area = lv_event_get_invalidated_area(event);
    if (!port || !area)
    {
        return;
    }
    if (port->config.refresh_full_width)
    {
        area->x1 = 0;
        area->x2 = port->config.width - 1;
    }
    int32_t rows = port->config.refresh_align_rows;
    if (rows > 1)
    {
        area->y1 = area->y1 / rows * rows;
        area->y2 = (area->y2 / rows + 1) * rows - 1;
        if (area->y2 >= port->config.height)
        {
            area->y2 = port->config.height - 1;
        }
    }
}

static void observe(lvgl_port_handle_t port, stm_err_t result, int complete)
{
    if (port->config.flush_observer)
    {
        port->observing = 1;
        port->config.flush_observer(port->config.observer_context, port->transfer_pixels, result,
                                    complete);
        port->observing = 0;
    }
}

// 未启用软件渲染时使用通用字节交换。
static void swap_rgb565(uint8_t *pixels, uint32_t count)
{
#if defined(LV_USE_DRAW_SW) && LV_USE_DRAW_SW
    lv_draw_sw_rgb565_swap(pixels, count);
#else
    for (uint32_t i = 0; i < count; ++i, pixels += 2)
    {
        uint8_t value = pixels[0];
        pixels[0] = pixels[1];
        pixels[1] = value;
    }
#endif
}

static stm_err_t flush_complete(lvgl_port_handle_t port, stm_err_t result);

// 出错后确认停止扫描，再归还 LVGL 双缓冲。
static void release_flush(lvgl_port_handle_t port, stm_err_t result)
{
    if (port->flush_result == STM_OK)
    {
        port->flush_result = result;
    }
    if (port->config.render_mode == LVGL_PORT_RENDER_DIRECT && port->flush_result != STM_OK)
    {
        if (stm_lcd_panel_stop_scanout(port->config.panel) != STM_OK)
        {
            return;
        }
    }
    (void)flush_complete(port, port->flush_result);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    lvgl_port_handle_t port = lv_display_get_user_data(display);
    if (!port)
    {
        lv_display_flush_ready(display);
        return;
    }
    if (port->flush_pending)
    {
        port->status.last_display_error = STM_ERR_INVALID_STATE;
        return; // 上一笔传输完成前不得归还像素缓冲。
    }
    if (port->config.render_mode == LVGL_PORT_RENDER_DIRECT && !lv_display_flush_is_last(display))
    {
        lv_display_flush_ready(display);
        return;
    }
    port->flush_pending = 1;
    port->flush_result = STM_OK;
    port->transfer_pixels = 0;
    port->status.last_display_error = STM_ERR_INVALID_ARG;
    if (area && pixels && area->x1 <= area->x2 && area->y1 <= area->y2)
    {
        if (area->x1 < 0 || area->y1 < 0 || area->x2 >= port->config.width ||
            area->y2 >= port->config.height)
        {
            port->status.last_display_error = STM_ERR_OUT_OF_RANGE;
        }
        else
        {
            port->transfer_pixels =
                (uint32_t)(area->x2 - area->x1 + 1) * (uint32_t)(area->y2 - area->y1 + 1);
            if (port->transfer_pixels > port->config.draw_buffer_bytes / 2u)
            {
                port->transfer_pixels = 0;
                release_flush(port, STM_ERR_OUT_OF_RANGE);
                return;
            }
            observe(port, STM_OK, 0);
            if (port->display_fault != STM_OK)
            {
                release_flush(port, port->display_fault);
                return;
            }
            if (port->config.rgb565_swap)
            {
                swap_rgb565(pixels, port->transfer_pixels);
            }
            stm_err_t result;
            if (port->config.render_mode == LVGL_PORT_RENDER_DIRECT)
            {
                if (pixels != port->config.draw_buffer && pixels != port->config.draw_buffer2)
                {
                    release_flush(port, STM_ERR_INVALID_ARG);
                    return;
                }
                result = stm_lcd_panel_present(port->config.panel, pixels);
            }
            else
            {
                result = (port->config.draw_async ? stm_lcd_panel_draw_bitmap_async
                                                  : stm_lcd_panel_draw_bitmap)(
                    port->config.panel, (uint16_t)area->x1, (uint16_t)area->y1,
                    (uint16_t)(area->x2 + 1), (uint16_t)(area->y2 + 1), pixels);
            }
            // 保留提交内立即完成的错误结果。
            if (port->flush_pending)
            {
                port->status.last_display_error = result;
                if (port->flush_result == STM_OK)
                {
                    port->flush_result = result;
                }
            }
        }
    }
    if (port->flush_pending &&
        ((!port->config.draw_async && port->config.render_mode != LVGL_PORT_RENDER_DIRECT) ||
         (port->status.last_display_error != STM_OK && !stm_lcd_panel_busy(port->config.panel))))
    {
        release_flush(port, port->status.last_display_error);
    }
}

static void sample_touch(lvgl_port_handle_t port, uint32_t now)
{
    if (!port->config.touch)
    {
        return;
    }
    uint32_t gap = now - port->last_poll;
    if (gap > port->config.touch_stale_ms)
    {
        port->pressed = 0;
        port->status.last_touch_error = STM_ERR_TIMEOUT;
    }
    if (!port->online)
    {
        if ((uint32_t)(now - port->last_probe) < port->config.touch_retry_ms)
        {
            return;
        }
        port->last_probe = now;
        stm_err_t probe = stm_lcd_io_probe(stm_lcd_touch_get_io(port->config.touch));
        if (probe != STM_OK && probe != STM_ERR_NOT_SUPPORTED)
        {
            return;
        }
        port->online = 1;
        port->last_poll = now - port->config.touch_period_ms;
    }
    if ((uint32_t)(now - port->last_poll) < port->config.touch_period_ms)
    {
        return;
    }
    port->last_poll = now;
    stm_err_t err = stm_lcd_touch_read_data(port->config.touch);
    stm_lcd_touch_point_t point = {0};
    size_t count = 0;
    if (err == STM_OK)
    {
        err = stm_lcd_touch_get_data(port->config.touch, &point, 1, &count);
    }
    if (err == STM_OK && count && (point.x >= port->config.width || point.y >= port->config.height))
    {
        err = STM_ERR_OUT_OF_RANGE;
    }
    port->pressed = err == STM_OK && count;
    if (port->pressed)
    {
        port->last_touch_x = point.x;
        port->last_touch_y = point.y;
    }
    port->status.last_touch_error = err;
    if (err == STM_ERR_IO || err == STM_ERR_TIMEOUT)
    {
        port->online = 0;
        port->last_probe = now;
    }
    if (port->config.touch_observer)
    {
        port->observing = 1;
        port->config.touch_observer(port->config.observer_context, now, err, port->pressed,
                                    port->last_touch_x, port->last_touch_y);
        port->observing = 0;
    }
}

static void service(lvgl_port_handle_t port, uint32_t now)
{
    stm_lcd_io_process(port->config.io); // 错误锁存后仍服务在途 IO。
    sample_touch(port, now);             // 服务独立 I2C 输入，不递归进入 LVGL。
    if (port->flush_pending && !stm_lcd_panel_busy(port->config.panel) &&
        port->flush_result != STM_OK)
    {
        release_flush(port, port->flush_result);
    }
}

static void wait_flush(lv_display_t *display)
{
    lvgl_port_handle_t port = lv_display_get_user_data(display);
    while (port && port->flush_pending)
    {
        service(port, port->config.clock_ms());
        if (port->flush_pending && port->config.idle)
        {
            port->config.idle(port->config.runtime_context);
        }
    }
}

static void panel_complete(void *context, stm_err_t result)
{
    lvgl_port_handle_t port = context;
    release_flush(port, result);
}

static stm_err_t flush_complete(lvgl_port_handle_t port, stm_err_t result)
{
    if (!port)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (!port->flush_pending)
    {
        return STM_ERR_INVALID_STATE;
    }
    port->status.last_display_error = result;
    if ((port->config.latch_display_error || port->config.render_mode == LVGL_PORT_RENDER_DIRECT) &&
        result != STM_OK && port->display_fault == STM_OK)
    {
        port->display_fault = result;
    }
    port->flush_pending = 0;
    observe(port, result, 1);
    lv_display_flush_ready(port->display);
    return STM_OK;
}

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    lvgl_port_handle_t port = lv_indev_get_user_data(indev);
    if (!data)
    {
        return;
    }
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = 0;
    data->point.y = 0;
    if (!port)
    {
        return;
    }
    data->point.x = port->last_touch_x;
    data->point.y = port->last_touch_y;

    uint32_t now = port->config.clock_ms();
    if ((uint32_t)(now - port->last_poll) > port->config.touch_stale_ms)
    {
        port->pressed = 0;
        port->status.last_touch_error = STM_ERR_TIMEOUT;
    }
    if (port->online && port->status.last_touch_error == STM_OK && port->pressed)
    {
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

stm_err_t lvgl_port_create(const lvgl_port_config_t *config, lvgl_port_handle_t *out)
{
    if (!config || !out)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (*out)
    {
        return STM_ERR_INVALID_STATE;
    }
    if (!config->panel || !config->io || !config->clock_ms || !config->draw_buffer ||
        !config->width || !config->height ||
        config->draw_buffer_bytes < (size_t)config->width * 2u ||
        config->draw_buffer_bytes > UINT32_MAX ||
        (uintptr_t)config->draw_buffer % LV_DRAW_BUF_ALIGN)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    if (config->render_mode != LVGL_PORT_RENDER_PARTIAL &&
        config->render_mode != LVGL_PORT_RENDER_DIRECT)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    if (config->render_mode == LVGL_PORT_RENDER_DIRECT)
    {
        if ((size_t)config->width > SIZE_MAX / config->height / 2u ||
            !stm_lcd_panel_supports_present(config->panel) || !config->draw_buffer2 ||
            config->draw_buffer_bytes != (size_t)config->width * config->height * 2u ||
            config->rgb565_swap || config->draw_async || config->refresh_full_width ||
            config->refresh_align_rows > 1)
        {
            return STM_ERR_INVALID_CONFIG;
        }
    }
    else if (!config->draw_async && !stm_lcd_panel_supports_draw(config->panel))
    {
        return STM_ERR_INVALID_CONFIG;
    }
    if (config->draw_async > 1 ||
        (config->draw_async && !stm_lcd_panel_supports_async(config->panel)))
    {
        return STM_ERR_INVALID_CONFIG;
    }
    if (config->rgb565_swap > 1 || config->refresh_full_width > 1 ||
        config->latch_display_error > 1)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    uint16_t width = 0, height = 0;
    if (stm_lcd_panel_get_io(config->panel) != config->io ||
        stm_lcd_panel_get_size(config->panel, &width, &height) != STM_OK ||
        width != config->width || height != config->height ||
        (config->touch && stm_lcd_touch_get_io(config->touch) == config->io &&
         (config->draw_async || config->render_mode == LVGL_PORT_RENDER_DIRECT)))
    {
        return STM_ERR_INVALID_CONFIG;
    }
    if (config->handler_period_ms > INT32_MAX || config->touch_period_ms > INT32_MAX ||
        config->touch_stale_ms > INT32_MAX || config->touch_retry_ms > INT32_MAX)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    uint16_t rows = config->refresh_align_rows;
    if (rows > 1 && (config->height % rows ||
                     (config->draw_buffer_bytes / ((size_t)config->width * 2u)) % rows))
    {
        return STM_ERR_INVALID_CONFIG;
    }
    if (config->draw_buffer2)
    {
        uintptr_t a = (uintptr_t)config->draw_buffer;
        uintptr_t b = (uintptr_t)config->draw_buffer2;
        size_t n = config->draw_buffer_bytes;
        if (b % LV_DRAW_BUF_ALIGN || a > UINTPTR_MAX - n || b > UINTPTR_MAX - n ||
            (a < b + n && b < a + n))
        {
            return STM_ERR_INVALID_CONFIG;
        }
    }
    if (!lv_is_initialized())
    {
        lv_init();
    }
    // LVGL 在初始化后才建立行跨度处理器。
    if (config->render_mode == LVGL_PORT_RENDER_DIRECT &&
        lv_draw_buf_width_to_stride(config->width, LV_COLOR_FORMAT_RGB565) !=
            (uint32_t)config->width * 2u)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    lvgl_port_handle_t port = calloc(1, sizeof(*port));
    if (!port)
    {
        return STM_ERR_NO_MEM;
    }
    port->config = *config;
    if (!port->config.handler_period_ms)
    {
        port->config.handler_period_ms = 10;
    }
    if (!port->config.touch_period_ms)
    {
        port->config.touch_period_ms = 20;
    }
    if (!port->config.touch_stale_ms)
    {
        port->config.touch_stale_ms = 200;
    }
    if (!port->config.touch_retry_ms)
    {
        port->config.touch_retry_ms = 1000;
    }
    stm_err_t err = stm_lcd_panel_subscribe(config->panel, panel_complete, port);
    if (err != STM_OK)
    {
        free(port);
        return err;
    }
    port->subscribed = 1;
    if (config->touch)
    {
        err = stm_lcd_touch_claim(config->touch, port);
        if (err != STM_OK)
        {
            (void)lvgl_port_delete(&port);
            return err;
        }
        port->touch_claimed = 1;
    }
    lv_tick_set_cb(config->clock_ms);
    port->last_handler = port->last_poll = port->last_probe = config->clock_ms();
    if (config->touch)
    {
        err = stm_lcd_io_probe(stm_lcd_touch_get_io(config->touch));
        port->online = err == STM_OK || err == STM_ERR_NOT_SUPPORTED;
        port->status.last_touch_error = port->online ? STM_OK : err;
    }
    port->display = lv_display_create(config->width, config->height);
    if (!port->display)
    {
        (void)lvgl_port_delete(&port);
        return STM_ERR_NO_MEM;
    }
    lv_display_set_color_format(port->display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(port->display, port);
    lv_display_set_buffers(port->display, config->draw_buffer, config->draw_buffer2,
                           (uint32_t)config->draw_buffer_bytes,
                           config->render_mode == LVGL_PORT_RENDER_DIRECT
                               ? LV_DISPLAY_RENDER_MODE_DIRECT
                               : LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(port->display, flush);
    if (config->draw_async || config->render_mode == LVGL_PORT_RENDER_DIRECT)
    {
        lv_display_set_flush_wait_cb(port->display, wait_flush);
    }
    if (config->refresh_full_width || config->refresh_align_rows > 1)
    {
        uint32_t count = lv_display_get_event_count(port->display);
        lv_display_add_event_cb(port->display, round_area, LV_EVENT_INVALIDATE_AREA, port);
        if (lv_display_get_event_count(port->display) != count + 1u)
        {
            lvgl_port_delete(&port);
            return STM_ERR_NO_MEM;
        }
    }
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
        if (config->touch_period_ms)
        {
            lv_timer_set_period(lv_indev_get_read_timer(port->indev), port->config.touch_period_ms);
        }
    }
    *out = port;
    return STM_OK;
}

stm_err_t lvgl_port_delete(lvgl_port_handle_t *handle)
{
    if (!handle)
    {
        return STM_ERR_INVALID_ARG;
    }
    lvgl_port_handle_t port = *handle;
    if (!port)
    {
        return STM_OK;
    }
    if (port->flush_pending || port->processing || port->observing ||
        stm_lcd_panel_busy(port->config.panel))
    {
        return STM_ERR_INVALID_STATE;
    }
    if (port->subscribed && port->config.render_mode == LVGL_PORT_RENDER_DIRECT)
    {
        stm_err_t err = stm_lcd_panel_stop_scanout(port->config.panel);
        if (err != STM_OK)
        {
            return err;
        }
    }
    if (port->subscribed)
    {
        stm_err_t err = stm_lcd_panel_unsubscribe(port->config.panel, port);
        if (err != STM_OK)
        {
            return err;
        }
        port->subscribed = 0;
    }
    if (port->touch_claimed)
    {
        stm_err_t err = stm_lcd_touch_release(port->config.touch, port);
        if (err != STM_OK)
        {
            return err;
        }
        port->touch_claimed = 0;
    }
    if (port->indev)
    {
        lv_indev_delete(port->indev);
    }
    if (port->display)
    {
        lv_display_delete(port->display);
    }
    free(port);
    *handle = NULL;
    return STM_OK;
}

stm_err_t lvgl_port_get_display(lvgl_port_handle_t port, lv_display_t **display)
{
    if (!port || !display)
    {
        return STM_ERR_INVALID_ARG;
    }
    *display = port->display;
    return STM_OK;
}

stm_err_t lvgl_port_get_indev(lvgl_port_handle_t port, lv_indev_t **indev)
{
    if (!port || !indev)
    {
        return STM_ERR_INVALID_ARG;
    }
    *indev = port->indev;
    return STM_OK;
}

stm_err_t lvgl_port_get_status(lvgl_port_handle_t port, lvgl_port_status_t *status)
{
    if (!port || !status)
    {
        return STM_ERR_INVALID_ARG;
    }
    *status = port->status;
    status->flush_pending = port->flush_pending;
    status->touch_online = port->online;
    status->touch_pressed = port->pressed;
    status->touch_x = port->last_touch_x;
    status->touch_y = port->last_touch_y;
    return STM_OK;
}

stm_err_t lvgl_port_process(lvgl_port_handle_t port, uint32_t now)
{
    if (!port)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (port->processing || port->observing)
    {
        return STM_ERR_INVALID_CONTEXT;
    }
    port->processing = 1;
    port->status.last_handler_ms = 0; // 未运行 handler 时清零本次耗时。
    service(port, now);
    if ((uint32_t)(now - port->last_handler) >= port->config.handler_period_ms &&
        port->display_fault == STM_OK)
    {
        port->last_handler = now;
        uint32_t started = port->config.clock_ms();
        (void)lv_timer_handler();
        port->status.last_handler_ms = port->config.clock_ms() - started;
    }
    port->processing = 0;
    return port->display_fault;
}
