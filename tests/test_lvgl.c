/**
 * @file    test_lvgl.c
 * @brief   验证组件主机回归，不替代实板验收。
 */
#include "stm_lvgl_port.h"
#include "test_allocator.h"
#include "stm_lcd_impl.h"
#include <assert.h>
#include <string.h>
static lv_display_t displays[4];
static lv_indev_t inputs[4];
static int display_used[4], input_used[4], fail_display, fail_input;
static unsigned live_displays, live_inputs;
static char deletion_order[256];
static unsigned deletion_count;

lv_display_t *lv_display_create(int32_t w, int32_t h)
{
    assert(w == 2 && h == 2);
    if (fail_display)
    {
        return NULL;
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        if (!display_used[i])
        {
            display_used[i] = 1;
            ++live_displays;
            memset(&displays[i], 0, sizeof displays[i]);
            return &displays[i];
        }
    }
    assert(0);
    return NULL;
}

void lv_display_delete(lv_display_t *d)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        if (d == &displays[i])
        {
            assert(display_used[i]);
            display_used[i] = 0;
            --live_displays;
            deletion_order[deletion_count++] = 'd';
            return;
        }
    }
    assert(0);
}

void lv_display_set_color_format(lv_display_t *d, int f)
{
    (void)d;
    assert(f == LV_COLOR_FORMAT_RGB565);
}

void lv_display_set_user_data(lv_display_t *d, void *p)
{
    d->user = p;
}

void *lv_display_get_user_data(lv_display_t *d)
{
    return d->user;
}

void lv_display_set_buffers(lv_display_t *d, void *a, void *b, uint32_t n, int mode)
{
    assert(a && n >= 4);
    d->mode = mode;
    d->buffers[0] = a;
    d->buffers[1] = b;
}

void lv_display_set_flush_cb(lv_display_t *d,
                             void (*cb)(lv_display_t *, const lv_area_t *, uint8_t *))
{
    d->flush = cb;
}

void lv_display_set_flush_wait_cb(lv_display_t *d, void (*cb)(lv_display_t *))
{
    d->wait = cb;
}

uint32_t lv_draw_buf_width_to_stride(uint32_t width, int color_format)
{
    assert(color_format == LV_COLOR_FORMAT_RGB565);
    return lv_is_initialized() ? width * 2u : 0;
}

int lv_display_flush_is_last(lv_display_t *d)
{
    return !d->not_last;
}

void lv_display_flush_ready(lv_display_t *d)
{
    ++d->ready;
}

lv_indev_t *lv_indev_create(void)
{
    if (fail_input)
    {
        return NULL;
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        if (!input_used[i])
        {
            input_used[i] = 1;
            ++live_inputs;
            memset(&inputs[i], 0, sizeof inputs[i]);
            return &inputs[i];
        }
    }
    assert(0);
    return NULL;
}

void lv_indev_delete(lv_indev_t *indev)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        if (indev == &inputs[i])
        {
            assert(input_used[i]);
            input_used[i] = 0;
            --live_inputs;
            deletion_order[deletion_count++] = 'i';
            return;
        }
    }
    assert(0);
}

void lv_indev_set_type(lv_indev_t *i, int t)
{
    (void)i;
    assert(t == LV_INDEV_TYPE_POINTER);
}

void lv_indev_set_display(lv_indev_t *i, lv_display_t *d)
{
    (void)i;
    assert(d);
}

void lv_indev_set_user_data(lv_indev_t *i, void *p)
{
    i->user = p;
}

void *lv_indev_get_user_data(lv_indev_t *i)
{
    return i->user;
}

void lv_indev_set_read_cb(lv_indev_t *i, void (*cb)(lv_indev_t *, lv_indev_data_t *))
{
    i->read = cb;
}

typedef struct
{
    struct stm_lcd_io io, touch_io;                            // 通用 IO 与借用状态。
    struct stm_lcd_panel panel;                                // 模型面板。
    struct stm_lcd_touch touch;                                // 模型触摸。
    lvgl_port_handle_t port;                                   // 当前受测 port。
    unsigned waits;                                            // 等待服务次数。
    unsigned inline_complete;                                  // 提交内立即完成标志。
    unsigned drawings, touch_reads, delay_polls, observations; // 绘图、采样与观察计数。
    stm_err_t draw_error, touch_error;                         // 注入绘图/触摸错误。
    int pressed;                                               // 模拟触摸按下。
    uint16_t x, y;                                             // 模拟坐标。
} mock_t;

static stm_err_t draw(stm_lcd_panel_handle_t panel,
                      uint16_t x1,
                      uint16_t y1,
                      uint16_t x2,
                      uint16_t y2,
                      const void *pixels)
{
    mock_t *m = panel->io->context;
    assert(x1 == 0 && y1 == 0 && x2 == 2 && y2 == 1 && pixels);
    ++m->drawings;
    return m->draw_error;
}

static stm_err_t touch_read(stm_lcd_touch_handle_t t, stm_lcd_touch_point_t *p, size_t *count)
{
    mock_t *m = t->io->context;
    ++m->touch_reads;
    *count = m->pressed ? 1 : 0;
    p->x = m->x;
    p->y = m->y;
    p->id = 0;
    if (m->pressed != 0 && m->pressed != 1)
    {
        return STM_ERR_VERIFY;
    }
    return m->touch_error;
}

static void direct_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static uint32_t tick;

static void async_wait(void *context)
{
    mock_t *m = context;
    ++m->waits;
    if (m->delay_polls)
    {
        --m->delay_polls;
        tick += 20;
        return;
    }
    assert(stm_lcd_io_complete(&m->io, m->draw_error) == STM_OK);
}

static int initialized;

void lv_init(void)
{
    initialized = 1;
}

int lv_is_initialized(void)
{
    return initialized;
}

void lv_tick_set_cb(uint32_t (*cb)(void))
{
    assert(cb);
}

uint32_t lv_timer_handler(void)
{
    return 10;
}

static uint32_t clock_ms(void)
{
    return tick;
}

static void destroy_panel(stm_lcd_panel_handle_t p)
{
    (void)p;
}

static void destroy_touch(stm_lcd_touch_handle_t t)
{
    (void)t;
}

static stm_err_t io_submit(void *context, uint8_t cmd, const void *p, size_t n)
{
    mock_t *m = context;
    (void)cmd;
    (void)p;
    (void)n;
    if (m->inline_complete)
    {
        assert(stm_lcd_io_complete(&m->io, STM_ERR_IO) == STM_OK);
        return STM_OK;
    }
    return m->draw_error;
}

static stm_err_t draw_async(stm_lcd_panel_handle_t p,
                            uint16_t x1,
                            uint16_t y1,
                            uint16_t x2,
                            uint16_t y2,
                            const void *pixels)
{
    mock_t *m = p->io->context;
    assert(x1 == 0 && y1 == 0 && x2 == 2 && y2 == 1 && pixels);
    ++m->drawings;
    return stm_lcd_io_tx_color_async(p->io, 0x2c, pixels, 4);
}

static const stm_lcd_io_ops_t io_ops = {.tx_color_async = io_submit, .process = async_wait};
static const stm_lcd_io_ops_t sync_io_ops = {0};
static const stm_lcd_touch_ops_t touch_ops = {.read_data = touch_read, .destroy = destroy_touch};
static const stm_lcd_panel_ops_t panel_ops = {
    .draw = draw, .draw_async = draw_async, .destroy = destroy_panel};

static void init_mock(mock_t *m)
{
    assert(stm_lcd_io_init(&m->io, &sync_io_ops, m) == STM_OK);
    assert(stm_lcd_io_init(&m->touch_io, &sync_io_ops, m) == STM_OK);
    assert(stm_lcd_panel_base_init(&m->panel, &panel_ops, &m->io, 2, 2) == STM_OK);
    stm_lcd_touch_config_t c = {.x_max = 2, .y_max = 2};
    assert(stm_lcd_touch_base_init(&m->touch, &touch_ops, &m->touch_io, &c) == STM_OK);
}

static void sample(lvgl_port_handle_t p)
{
    tick += 1001;
    assert(lvgl_port_process(p, tick) == STM_OK);
}

static void guarded_observer(void *context, uint32_t pixels, stm_err_t result, int complete)
{
    mock_t *m = context;
    (void)pixels;
    (void)result;
    (void)complete;
    ++m->observations;
    assert(lvgl_port_process(m->port, tick) == STM_ERR_INVALID_CONTEXT);
    lvgl_port_handle_t alias = m->port;
    assert(lvgl_port_delete(&alias) == STM_ERR_INVALID_STATE && alias == m->port);
}

static int fail_event;

void *lv_event_get_user_data(lv_event_t *e)
{
    return e->user;
}

lv_area_t *lv_event_get_invalidated_area(lv_event_t *e)
{
    return e->area;
}

uint32_t lv_display_get_event_count(lv_display_t *d)
{
    return d->event_count;
}

void lv_display_add_event_cb(lv_display_t *d, void (*cb)(lv_event_t *), int code, void *user)
{
    assert(code == LV_EVENT_INVALIDATE_AREA);
    if (fail_event)
    {
        return;
    }
    d->event_cb = cb;
    d->event_user = user;
    ++d->event_count;
}

lv_timer_t *lv_indev_get_read_timer(lv_indev_t *i)
{
    return &i->timer;
}

void lv_timer_set_period(lv_timer_t *t, uint32_t period)
{
    t->period = period;
}

void lv_draw_sw_rgb565_swap(void *pixels, uint32_t count)
{
    uint8_t *p = pixels;
    for (uint32_t i = 0; i < count; ++i, p += 2)
    {
        uint8_t tmp = p[0];
        p[0] = p[1];
        p[1] = tmp;
    }
}

int main(void)
{
    mock_t m = {.pressed = 1, .x = 1, .y = 1}, m2 = {0};
    init_mock(&m);
    init_mock(&m2);
    uint16_t buffer[2] = {0};
    lvgl_port_config_t cfg = {.width = 2,
                              .height = 2,
                              .draw_buffer = buffer,
                              .draw_buffer_bytes = sizeof buffer,
                              .io = &m.io,
                              .panel = &m.panel,
                              .clock_ms = clock_ms,
                              .touch = &m.touch},
                       bad;
    lvgl_port_handle_t port = NULL, other = NULL;
    lv_display_t *display = NULL, *other_display = NULL;
    lv_indev_t *indev = NULL;
    lvgl_port_status_t status = {0};
    assert(lvgl_port_create(NULL, &port) == STM_ERR_INVALID_ARG);
    assert(lvgl_port_create(&cfg, NULL) == STM_ERR_INVALID_ARG);
    assert(lvgl_port_delete(NULL) == STM_ERR_INVALID_ARG);
    assert(lvgl_port_delete(&port) == STM_OK);
    bad = cfg;
    bad.panel = NULL;
    assert(lvgl_port_create(&bad, &port) == STM_ERR_INVALID_CONFIG);
    bad = cfg;
    bad.width = 0;
    assert(lvgl_port_create(&bad, &port) == STM_ERR_INVALID_CONFIG);
    bad = cfg;
    bad.draw_buffer_bytes = 3;
    assert(lvgl_port_create(&bad, &port) == STM_ERR_INVALID_CONFIG);
    bad = cfg;
    bad.draw_buffer = (uint8_t *)buffer + 1;
    assert(lvgl_port_create(&bad, &port) == STM_ERR_INVALID_CONFIG);
#if SIZE_MAX > UINT32_MAX
    bad = cfg;
    bad.draw_buffer_bytes = (size_t)UINT32_MAX + 1;
    assert(lvgl_port_create(&bad, &port) == STM_ERR_INVALID_CONFIG);
#endif
    test_alloc_fail = 1;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_NO_MEM && !port && !live_displays);
    test_alloc_fail = 0;
    fail_display = 1;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_NO_MEM && !port && !test_alloc_live);
    fail_display = 0;
    fail_input = 1;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_NO_MEM && !port && !test_alloc_live &&
           !live_displays);
    fail_input = 0;
    deletion_count = 0;
    assert(lvgl_port_create(&cfg, &port) == STM_OK && test_alloc_live == 1 && live_inputs == 1);
    lvgl_port_handle_t saved = port;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_INVALID_STATE && port == saved &&
           live_displays == 1);
    assert(lvgl_port_get_display(port, &display) == STM_OK && display);
    assert(lvgl_port_get_indev(port, &indev) == STM_OK && indev);
    assert(lvgl_port_get_status(port, &status) == STM_OK && status.last_display_error == STM_OK &&
           status.last_touch_error == STM_OK);
    assert(lvgl_port_get_status(NULL, &status) == STM_ERR_INVALID_ARG);
    assert(lvgl_port_get_status(port, NULL) == STM_ERR_INVALID_ARG);
    assert(lvgl_port_get_display(port, NULL) == STM_ERR_INVALID_ARG);
    assert(lvgl_port_get_indev(NULL, &indev) == STM_ERR_INVALID_ARG);
    cfg.io = &m2.io;
    cfg.panel = &m2.panel;
    cfg.touch = NULL;
    assert(lvgl_port_create(&cfg, &other) == STM_OK && test_alloc_live == 2);
    assert(lvgl_port_get_display(other, &other_display) == STM_OK && other_display != display);
    lv_indev_t *missing = indev;
    assert(lvgl_port_get_indev(other, &missing) == STM_OK && !missing);
    lv_area_t area = {0, 0, 1, 0};
    display->flush(display, &area, (uint8_t *)buffer);
    assert(m.drawings == 1 && m2.drawings == 0 && display->ready == 1);
    m.draw_error = STM_ERR_TIMEOUT;
    display->flush(display, &area, (uint8_t *)buffer);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_display_error == STM_ERR_TIMEOUT && display->ready == 2);
    area.x2 = 2;
    display->flush(display, &area, (uint8_t *)buffer);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_display_error == STM_ERR_OUT_OF_RANGE && display->ready == 3 &&
           m.drawings == 2);
    display->flush(display, NULL, (uint8_t *)buffer);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_display_error == STM_ERR_INVALID_ARG && display->ready == 4);
    m.draw_error = STM_OK;
    area.x2 = 1;
    display->flush(display, &area, (uint8_t *)buffer);
    assert(lvgl_port_get_status(port, &status) == STM_OK && status.last_display_error == STM_OK);
    lv_indev_data_t data = {0};
    sample(port);
    indev->read(indev, &data);
    assert(data.state == LV_INDEV_STATE_PRESSED && data.point.x == 1 && data.point.y == 1);
    m.touch_error = STM_ERR_IO;
    m.x = m.y = 0;
    data.point.x = data.point.y = 0;
    sample(port);
    indev->read(indev, &data);
    assert(data.state == LV_INDEV_STATE_RELEASED && data.point.x == 1 && data.point.y == 1);
    assert(lvgl_port_get_status(port, &status) == STM_OK && status.last_touch_error == STM_ERR_IO);
    m.touch_error = STM_OK;
    m.x = 2;
    sample(port);
    indev->read(indev, &data);
    assert(data.state == LV_INDEV_STATE_RELEASED && data.point.x == 1 && data.point.y == 1);
    assert(lvgl_port_get_status(port, &status) == STM_OK && status.last_touch_error == STM_OK);
    m.x = 1;
    m.pressed = 2;
    sample(port);
    indev->read(indev, &data);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_touch_error == STM_ERR_VERIFY);
    m.pressed = 0;
    m.x = m.y = 0;
    sample(port);
    indev->read(indev, &data);
    assert(data.point.x == 1 && data.point.y == 1);
    assert(lvgl_port_get_status(port, &status) == STM_OK && status.last_touch_error == STM_OK &&
           data.state == LV_INDEV_STATE_RELEASED);
    lv_display_set_flush_cb(display, direct_flush);
    display->flush(display, &area, (uint8_t *)buffer);
    assert(display->ready == 6); // 借用对象允许板级替换刷新路径。
    assert(lvgl_port_delete(&port) == STM_OK && !port && deletion_count == 2 &&
           deletion_order[0] == 'i' && deletion_order[1] == 'd');
    assert(lvgl_port_delete(&port) == STM_OK);
    assert(lvgl_port_delete(&other) == STM_OK && !test_alloc_live && !live_displays &&
           !live_inputs);
    cfg.touch = &m.touch;
    cfg.io = &m.io;
    cfg.panel = &m.panel;
    assert(lvgl_port_create(&cfg, &port) == STM_OK);
    assert(lvgl_port_delete(&port) == STM_OK && !test_alloc_live);
    uint16_t second[2] = {0};
    cfg.io = &m.io;
    cfg.panel = &m.panel;
    cfg.touch = NULL;
    cfg.draw_async = 1;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_INVALID_CONFIG); // 异步模式必须提供完成服务。
    m.io.ops = &io_ops;
    cfg.draw_async = 2;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_INVALID_CONFIG);
    cfg.draw_async = 1;
    cfg.draw_buffer2 = buffer;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_INVALID_CONFIG); // 拒绝相同缓冲地址。
    cfg.draw_buffer2 = (uint8_t *)buffer + 2;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_INVALID_CONFIG); // 拒绝重叠缓冲。
    cfg.draw_buffer2 = (uint8_t *)second + 1;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_INVALID_CONFIG); // 拒绝未对齐缓冲。
    cfg.draw_buffer2 = second;
    assert(lvgl_port_create(&cfg, &port) == STM_OK);
    m.port = port;
    assert(lvgl_port_get_display(port, &display) == STM_OK);
    assert(display->wait && display->buffers[0] == buffer && display->buffers[1] == second);
    assert(stm_lcd_io_complete(NULL, STM_OK) == STM_ERR_INVALID_ARG);
    assert(stm_lcd_io_complete(&m.io, STM_OK) == STM_ERR_INVALID_STATE);
    m.draw_error = STM_OK;
    display->flush(display, &area, (uint8_t *)buffer);
    assert(display->ready == 0); // 提交成功不等于完成。
    assert(lvgl_port_delete(&port) == STM_ERR_INVALID_STATE && port);
    unsigned before = m.drawings;
    display->flush(display, &area, (uint8_t *)second);
    assert(m.drawings == before && display->ready == 0); // 不允许重叠提交。
    display->wait(display);
    assert(display->ready == 1 && m.waits == 1);
    assert(stm_lcd_io_complete(&m.io, STM_OK) == STM_ERR_INVALID_STATE);
    display->wait(display);
    assert(m.waits == 1); // 已完成的等待不重复通知。
    display->flush(display, &area, (uint8_t *)second);
    m.draw_error = STM_ERR_IO;
    display->wait(display);
    assert(display->ready == 2);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_display_error == STM_ERR_IO);
    display->flush(display, &area, (uint8_t *)buffer);
    assert(display->ready == 3); // 立即提交失败仅归还一次。
    m.inline_complete = 1;
    display->flush(display, &area, (uint8_t *)second);
    assert(display->ready == 4);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_display_error == STM_ERR_IO);
    assert(lvgl_port_delete(&port) == STM_OK && !test_alloc_live && !live_displays);
    // 验证可选刷新策略与默认配置。
    cfg.draw_async = 0;
    cfg.draw_buffer2 = NULL;
    m.io.ops = &sync_io_ops;
    cfg.refresh_align_rows = 2;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_INVALID_CONFIG); // 单行缓冲。
    uint16_t full_buffer[4] = {0};
    cfg.draw_buffer = full_buffer;
    cfg.draw_buffer_bytes = sizeof full_buffer;
    cfg.refresh_full_width = 2;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_INVALID_CONFIG);
    cfg.refresh_full_width = 1;
    cfg.touch = &m.touch;
    cfg.io = &m.io;
    cfg.panel = &m.panel;
    cfg.touch_period_ms = 17;
    fail_event = 1;
    assert(lvgl_port_create(&cfg, &port) == STM_ERR_NO_MEM && !port && !test_alloc_live);
    fail_event = 0;
    assert(lvgl_port_create(&cfg, &port) == STM_OK);
    assert(lvgl_port_get_display(port, &display) == STM_OK);
    assert(lvgl_port_get_indev(port, &indev) == STM_OK && indev->timer.period == 17);
    lv_area_t rounded = {1, 1, 1, 1};
    lv_event_t event = {&rounded, display->event_user};
    display->event_cb(&event);
    assert(rounded.x1 == 0 && rounded.x2 == 1 && rounded.y1 == 0 && rounded.y2 == 1);
    assert(lvgl_port_delete(&port) == STM_OK);
    cfg.refresh_align_rows = 0;
    cfg.refresh_full_width = 0;
    cfg.rgb565_swap = 1;
    cfg.latch_display_error = 1;
    cfg.touch = NULL;
    assert(lvgl_port_create(&cfg, &port) == STM_OK);
    m.port = port;
    m.inline_complete = 0;
    m.draw_error = STM_ERR_IO;
    assert(lvgl_port_get_display(port, &display) == STM_OK);
    full_buffer[0] = 0x1234;
    display->flush(display, &area, (uint8_t *)full_buffer);
    assert(full_buffer[0] == 0x3412);
    before = m.drawings;
    m.draw_error = STM_OK;
    display->flush(display, &area, (uint8_t *)full_buffer);
    assert(m.drawings == before && display->ready == 2 && full_buffer[0] == 0x3412);
    assert(lvgl_port_get_status(port, &status) == STM_OK && !status.flush_pending &&
           status.last_display_error == STM_ERR_IO);
    assert(lvgl_port_delete(&port) == STM_OK && !test_alloc_live && !live_displays);
    // 等待慢速 IO 完成时仍采样触摸，不递归 LVGL。
    cfg.latch_display_error = 0;
    cfg.draw_async = 1;
    cfg.touch = &m.touch;
    cfg.touch_period_ms = 20;
    cfg.flush_observer = guarded_observer;
    cfg.observer_context = &m;
    m.io.ops = &io_ops;
    m.draw_error = STM_OK;
    m.delay_polls = 3;
    m.pressed = 1;
    m.x = m.y = 1;
    assert(lvgl_port_create(&cfg, &port) == STM_OK);
    m.port = port;
    assert(lvgl_port_get_display(port, &display) == STM_OK);
    before = m.touch_reads;
    display->flush(display, &area, (uint8_t *)full_buffer);
    display->wait(display);
    assert(m.touch_reads >= before + 3 && m.observations == 2 && display->ready == 1);
    assert(lvgl_port_get_status(port, &status) == STM_OK && status.touch_pressed &&
           !status.flush_pending);
    assert(lvgl_port_delete(&port) == STM_OK);
    return 0;
}
