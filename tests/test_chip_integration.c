/**
 * @file test_chip_integration.c
 * @brief 用同一 port 验证五种器件及 DIRECT 整帧失败路径。
 */
#define main original_mock_contracts
#include "test_lvgl.c"
#undef main
#include "stm_lcd_st7789.h"
#include "stm_lcd_st7796.h"
#include "stm_lcd_ili9881c.h"
#include "stm_lcd_touch_ft5206.h"
#include "stm_lcd_touch_gt9271.h"

typedef struct
{
    struct stm_lcd_io io;                                     // 通用 IO 与借用状态。
    unsigned commands, colors, copies, presents, stops, acks; // 协议与生命周期调用计数。
    int busy, auto_complete, immediate;                       // 模拟在途状态。
    const void *next, *active;                                // 当前扫描帧与待切换帧。
    stm_err_t submit_error, stop_error, completion_error;     // 注入错误与最终结果。
    uint8_t gt_status;                                        // 模拟 GT ready/点数。
} chip_mock_t;

static stm_err_t command(void *ctx, uint8_t cmd, const uint8_t *data, size_t n)
{
    chip_mock_t *m = ctx;
    (void)cmd;
    assert(!n || data);
    ++m->commands;
    return STM_OK;
}

static stm_err_t color(void *ctx, uint8_t cmd, const void *data, size_t n)
{
    chip_mock_t *m = ctx;
    assert(cmd == 0x2c && data && n == 8);
    ++m->colors;
    return STM_OK;
}

static stm_err_t
region(void *ctx, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const void *data)
{
    chip_mock_t *m = ctx;
    assert(x1 == 0 && y1 == 0 && x2 == 2 && y2 == 2 && data);
    ++m->copies;
    return STM_OK;
}

static void chip_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    assert(ms > 0);
}

static int chip_busy(void *ctx)
{
    return ((chip_mock_t *)ctx)->busy;
}

static stm_err_t chip_present(void *ctx, const void *data, uint16_t w, uint16_t h)
{
    chip_mock_t *m = ctx;
    assert(data && data != m->active && w == 2 && h == 2);
    ++m->presents;
    m->next = data;
    m->busy = !m->immediate;
    if (m->immediate)
    {
        m->active = m->next;
        assert(stm_lcd_io_complete(&m->io, m->completion_error) == STM_OK);
        assert(stm_lcd_io_complete(&m->io, STM_OK) == STM_ERR_INVALID_STATE);
    }
    return m->submit_error;
}

static stm_err_t chip_stop(void *ctx)
{
    chip_mock_t *m = ctx;
    ++m->stops;
    if (m->stop_error != STM_OK)
    {
        return m->stop_error;
    }
    m->active = m->next = NULL;
    return STM_OK;
}

static void chip_process(void *ctx)
{
    chip_mock_t *m = ctx;
    if (m->auto_complete && m->io.pending)
    {
        m->busy = 0;
        m->active = m->next;
        stm_err_t err = stm_lcd_io_complete(&m->io, m->completion_error);
        assert(err == STM_OK || err == m->stop_error);
    }
}

static stm_err_t reg_read(void *ctx, uint16_t reg, uint8_t width, uint8_t *data, size_t n)
{
    chip_mock_t *m = ctx;
    memset(data, 0, n);
    if (width == 1)
    {
        if (reg == 2)
        {
            assert(n == 1);
            data[0] = 1;
        }
        else
        {
            assert(reg == 3 && n == 6);
            data[1] = 1;
            data[3] = 1;
        }
    }
    else
    {
        assert(width == 2);
        if (reg == 0x8140)
        {
            assert(n == 4);
            memcpy(data, "9271", 4);
        }
        else if (reg == 0x814e)
        {
            assert(n == 1);
            data[0] = m->gt_status;
        }
        else
        {
            assert(reg == 0x814f && n == 8);
            data[1] = 1;
            data[3] = 1;
        }
    }
    return STM_OK;
}

static stm_err_t reg_write(void *ctx, uint16_t reg, uint8_t width, const uint8_t *data, size_t n)
{
    chip_mock_t *m = ctx;
    assert(width == 2 && reg == 0x814e && n == 1 && *data == 0);
    ++m->acks;
    m->gt_status = 0;
    return STM_OK;
}

static const stm_lcd_io_ops_t display_ops = {.tx_param = command,
                                             .tx_color = color,
                                             .draw_region = region,
                                             .present = chip_present,
                                             .process = chip_process,
                                             .busy = chip_busy,
                                             .stop_scanout = chip_stop};
static const stm_lcd_io_ops_t input_ops = {.read_reg = reg_read, .write_reg = reg_write};

static void run_partial(unsigned model)
{
    chip_mock_t m = {0}, t = {.gt_status = 0x81};
    stm_lcd_panel_handle_t panel = NULL;
    stm_lcd_touch_handle_t touch = NULL;
    assert(stm_lcd_io_init(&m.io, &display_ops, &m) == STM_OK);
    assert(stm_lcd_io_init(&t.io, &input_ops, &t) == STM_OK);
    if (model == 0)
    {
        lcd_st7789_config_t c = {.io = &m.io, .width = 2, .height = 2, .delay_ms = chip_delay};
        assert(lcd_st7789_create(&c, &panel) == STM_OK);
        lcd_touch_ft5206_config_t tc = {.io = &t.io, .coordinates = {.x_max = 2, .y_max = 2}};
        assert(lcd_touch_ft5206_create(&tc, &touch) == STM_OK);
    }
    else if (model == 1)
    {
        lcd_st7796_config_t c = {.io = &m.io, .width = 2, .height = 2, .delay_ms = chip_delay};
        assert(lcd_st7796_create(&c, &panel) == STM_OK);
        lcd_touch_gt9271_config_t tc = {.io = &t.io, .coordinates = {.x_max = 2, .y_max = 2}};
        assert(lcd_touch_gt9271_create(&tc, &touch) == STM_OK);
    }
    else
    {
        lcd_ili9881c_config_t c = {.io = &m.io, .width = 2, .height = 2, .delay_ms = chip_delay};
        assert(lcd_ili9881c_create(&c, &panel) == STM_OK);
    }
    assert(stm_lcd_panel_init(panel) == STM_OK && m.commands);
    uint32_t pixels[2] = {0};
    lvgl_port_handle_t port = NULL;
    lvgl_port_config_t c = {.io = &m.io,
                            .panel = panel,
                            .touch = touch,
                            .width = 2,
                            .height = 2,
                            .draw_buffer = pixels,
                            .draw_buffer_bytes = sizeof pixels,
                            .clock_ms = clock_ms};
    assert(lvgl_port_create(&c, &port) == STM_OK);
    lv_display_t *d = NULL;
    assert(lvgl_port_get_display(port, &d) == STM_OK);
    lv_area_t a = {0, 0, 1, 1};
    d->flush(d, &a, (uint8_t *)pixels);
    assert(d->ready == 1 && (model < 2 ? m.colors == 1 : m.copies == 1));
    tick += 20;
    assert(lvgl_port_process(port, tick) == STM_OK);
    if (touch)
    {
        lv_indev_t *i = NULL;
        assert(lvgl_port_get_indev(port, &i) == STM_OK);
        lv_indev_data_t data = {0};
        i->read(i, &data);
        assert(data.state == LV_INDEV_STATE_PRESSED && data.point.x == 1 && data.point.y == 1);
        tick += 20;
        assert(lvgl_port_process(port, tick) == STM_OK);
        i->read(i, &data);
        assert(data.state == LV_INDEV_STATE_PRESSED);
    }
    assert(lvgl_port_delete(&port) == STM_OK);
    assert(stm_lcd_panel_delete(&panel) == STM_OK);
    assert(stm_lcd_touch_delete(&touch) == STM_OK);
    assert(stm_lcd_io_deinit(&m.io) == STM_OK && stm_lcd_io_deinit(&t.io) == STM_OK);
}

static void run_direct(void)
{
    chip_mock_t m = {0};
    stm_lcd_panel_handle_t panel = NULL;
    lvgl_port_handle_t port = NULL;
    assert(stm_lcd_io_init(&m.io, &display_ops, &m) == STM_OK);
    lcd_ili9881c_config_t pc = {.io = &m.io, .width = 2, .height = 2, .delay_ms = chip_delay};
    assert(lcd_ili9881c_create(&pc, &panel) == STM_OK && stm_lcd_panel_init(panel) == STM_OK);
    uint32_t pixels[4] = {0};
    lvgl_port_config_t c = {.render_mode = LVGL_PORT_RENDER_DIRECT,
                            .io = &m.io,
                            .panel = panel,
                            .width = 2,
                            .height = 2,
                            .draw_buffer = pixels,
                            .draw_buffer2 = pixels + 2,
                            .draw_buffer_bytes = 8,
                            .clock_ms = clock_ms};
    lvgl_port_config_t bad = c;
    bad.draw_buffer2 = NULL;
    assert(lvgl_port_create(&bad, &port) == STM_ERR_INVALID_CONFIG);
    bad = c;
    bad.draw_buffer_bytes = 4;
    assert(lvgl_port_create(&bad, &port) == STM_ERR_INVALID_CONFIG);
    bad = c;
    bad.rgb565_swap = 1;
    assert(lvgl_port_create(&bad, &port) == STM_ERR_INVALID_CONFIG);
    bad = c;
    bad.draw_buffer2 = pixels + 1;
    assert(lvgl_port_create(&bad, &port) == STM_ERR_INVALID_CONFIG);
    fail_display = 1;
    assert(lvgl_port_create(&c, &port) == STM_ERR_NO_MEM && !port && !m.io.scanout);
    fail_display = 0;
    assert(lvgl_port_create(&c, &port) == STM_OK);
    lv_display_t *d = NULL;
    assert(lvgl_port_get_display(port, &d) == STM_OK && d->mode == LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_area_t a = {0, 0, 0, 0};
    d->not_last = 1;
    d->flush(d, &a, (uint8_t *)pixels);
    assert(d->ready == 1 && !m.presents);
    d->not_last = 0;
    d->flush(d, &a, (uint8_t *)pixels);
    assert(d->ready == 1 && m.presents == 1 && m.next == pixels && m.io.scanout);
    assert(lvgl_port_delete(&port) == STM_ERR_INVALID_STATE && port);
    m.auto_complete = 1;
    d->wait(d);
    assert(d->ready == 2 && m.active == pixels && m.io.scanout);
    d->flush(d, &a, (uint8_t *)(pixels + 2));
    d->wait(d);
    assert(d->ready == 3 && m.active == pixels + 2);
    m.stop_error = STM_ERR_TIMEOUT;
    assert(lvgl_port_delete(&port) == STM_ERR_TIMEOUT && port && live_displays == 1);
    m.stop_error = STM_OK;
    assert(lvgl_port_delete(&port) == STM_OK && !m.active && !m.io.scanout);
    assert(lvgl_port_create(&c, &port) == STM_OK);
    assert(lvgl_port_get_display(port, &d) == STM_OK);
    m.submit_error = STM_ERR_IO;
    m.stop_error = STM_ERR_TIMEOUT;
    d->flush(d, &a, (uint8_t *)pixels);
    tick += 20;
    assert(lvgl_port_process(port, tick) == STM_OK && d->ready == 0 && m.io.pending);
    m.stop_error = STM_OK;
    tick += 20;
    assert(lvgl_port_process(port, tick) == STM_ERR_IO && d->ready == 1 && !m.active &&
           !m.io.scanout);
    lvgl_port_status_t status = {0};
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_display_error == STM_ERR_IO && !status.flush_pending);
    assert(lvgl_port_delete(&port) == STM_OK);
    m.submit_error = STM_OK;
    assert(lvgl_port_create(&c, &port) == STM_OK);
    assert(lvgl_port_get_display(port, &d) == STM_OK);
    d->flush(d, &a, (uint8_t *)pixels);
    d->wait(d);
    assert(m.io.scanout);
    m.stop_error = STM_ERR_IO;
    uint32_t wrong[2] = {0};
    d->flush(d, &a, (uint8_t *)wrong);
    assert(d->ready == 1 && m.io.scanout);
    m.stop_error = STM_OK;
    tick += 20;
    assert(lvgl_port_process(port, tick) == STM_ERR_INVALID_ARG && d->ready == 2 && !m.io.scanout);
    assert(lvgl_port_delete(&port) == STM_OK);
    // 立即完成及重复完成不得重复归还，也不能覆盖首次错误。
    m.immediate = 1;
    assert(lvgl_port_create(&c, &port) == STM_OK);
    assert(lvgl_port_get_display(port, &d) == STM_OK);
    d->flush(d, &a, (uint8_t *)pixels);
    assert(d->ready == 1 && !m.io.pending && m.active == pixels);
    assert(lvgl_port_delete(&port) == STM_OK);
    m.completion_error = STM_ERR_TIMEOUT;
    assert(lvgl_port_create(&c, &port) == STM_OK);
    assert(lvgl_port_get_display(port, &d) == STM_OK);
    d->flush(d, &a, (uint8_t *)pixels);
    tick += 20;
    assert(lvgl_port_process(port, tick) == STM_ERR_TIMEOUT && d->ready == 1);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_display_error == STM_ERR_TIMEOUT && !status.flush_pending);
    assert(lvgl_port_delete(&port) == STM_OK);
    // 完成失败且不能停止扫描时，保留当前帧和待完成状态。
    m.immediate = 0;
    m.completion_error = STM_ERR_VERIFY;
    m.stop_error = STM_ERR_IO;
    assert(lvgl_port_create(&c, &port) == STM_OK);
    assert(lvgl_port_get_display(port, &d) == STM_OK);
    d->flush(d, &a, (uint8_t *)pixels);
    tick += 20;
    assert(lvgl_port_process(port, tick) == STM_OK && d->ready == 0 && m.io.scanout);
    assert(lvgl_port_delete(&port) == STM_ERR_INVALID_STATE);
    m.stop_error = STM_OK;
    m.completion_error = STM_OK;
    tick += 20;
    assert(lvgl_port_process(port, tick) == STM_ERR_VERIFY && d->ready == 1 && !m.io.scanout);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_display_error == STM_ERR_VERIFY);
    assert(lvgl_port_delete(&port) == STM_OK);
    assert(stm_lcd_panel_delete(&panel) == STM_OK && stm_lcd_io_deinit(&m.io) == STM_OK);
}

int main(void)
{
    for (unsigned model = 0; model < 3; ++model)
    {
        run_partial(model);
    }
    run_direct();
    assert(live_displays == 0 && live_inputs == 0 && test_alloc_live == 0);
    return 0;
}
