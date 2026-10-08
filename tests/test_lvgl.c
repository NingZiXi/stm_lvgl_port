#include "stm_lvgl_port.h"
#include "test_allocator.h"
#include <assert.h>
#include <string.h>
static lv_display_t displays[4];
static lv_indev_t inputs[4];
static int display_used[4], input_used[4], fail_display, fail_input;
static unsigned live_displays, live_inputs;
static char deletion_order[16];
static unsigned deletion_count;

lv_display_t *lv_display_create(int32_t w, int32_t h)
{
    assert(w == 2 && h == 2);
    if (fail_display)
        return NULL;
    for (unsigned i = 0; i < 4; ++i)
        if (!display_used[i])
        {
            display_used[i] = 1;
            ++live_displays;
            memset(&displays[i], 0, sizeof displays[i]);
            return &displays[i];
        }
    assert(0);
    return NULL;
}

void lv_display_delete(lv_display_t *d)
{
    for (unsigned i = 0; i < 4; ++i)
        if (d == &displays[i])
        {
            assert(display_used[i]);
            display_used[i] = 0;
            --live_displays;
            deletion_order[deletion_count++] = 'd';
            return;
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
    (void)d;
    assert(a && !b && n >= 4 && mode == LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void lv_display_set_flush_cb(lv_display_t *d,
                             void (*cb)(lv_display_t *, const lv_area_t *, uint8_t *))
{
    d->flush = cb;
}

void lv_display_flush_ready(lv_display_t *d)
{
    ++d->ready;
}

lv_indev_t *lv_indev_create(void)
{
    if (fail_input)
        return NULL;
    for (unsigned i = 0; i < 4; ++i)
        if (!input_used[i])
        {
            input_used[i] = 1;
            ++live_inputs;
            memset(&inputs[i], 0, sizeof inputs[i]);
            return &inputs[i];
        }
    assert(0);
    return NULL;
}

void lv_indev_delete(lv_indev_t *indev)
{
    for (unsigned i = 0; i < 4; ++i)
        if (indev == &inputs[i])
        {
            assert(input_used[i]);
            input_used[i] = 0;
            --live_inputs;
            deletion_order[deletion_count++] = 'i';
            return;
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
    unsigned drawings;
    stm_err_t draw_error, touch_error;
    int pressed;
    uint16_t x, y;
} mock_t;

static stm_err_t
draw(void *ctx, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const void *pixels)
{
    mock_t *m = ctx;
    assert(x1 == 0 && y1 == 0 && x2 == 2 && y2 == 1 && pixels);
    ++m->drawings;
    return m->draw_error;
}

static stm_err_t touch(void *ctx, int *pressed, uint16_t *x, uint16_t *y)
{
    mock_t *m = ctx;
    *pressed = m->pressed;
    *x = m->x;
    *y = m->y;
    return m->touch_error;
}

static void direct_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

int main(void)
{
    mock_t m = {.pressed = 1, .x = 1, .y = 1}, m2 = {0};
    uint16_t buffer[2] = {0};
    lvgl_port_config_t cfg = {2, 2, buffer, sizeof buffer, &m, draw, &m, touch}, bad;
    lvgl_port_handle_t port = NULL, other = NULL;
    lv_display_t *display = NULL, *other_display = NULL;
    lv_indev_t *indev = NULL;
    lvgl_port_status_t status = {0};
    assert(lvgl_port_create(NULL, &port) == STM_ERR_INVALID_ARG);
    assert(lvgl_port_create(&cfg, NULL) == STM_ERR_INVALID_ARG);
    assert(lvgl_port_delete(NULL) == STM_ERR_INVALID_ARG);
    assert(lvgl_port_delete(&port) == STM_OK);
    bad = cfg;
    bad.draw = NULL;
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
    cfg.display_context = &m2;
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
    indev->read(indev, &data);
    assert(data.state == LV_INDEV_STATE_PRESSED && data.point.x == 1 && data.point.y == 1);
    m.touch_error = STM_ERR_IO;
    indev->read(indev, &data);
    assert(data.state == LV_INDEV_STATE_RELEASED && data.point.x == 0);
    assert(lvgl_port_get_status(port, &status) == STM_OK && status.last_touch_error == STM_ERR_IO);
    m.touch_error = STM_OK;
    m.x = 2;
    indev->read(indev, &data);
    assert(data.state == LV_INDEV_STATE_RELEASED);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_touch_error == STM_ERR_OUT_OF_RANGE);
    m.x = 1;
    m.pressed = 2;
    indev->read(indev, &data);
    assert(lvgl_port_get_status(port, &status) == STM_OK &&
           status.last_touch_error == STM_ERR_INVALID_ARG);
    m.pressed = 0;
    indev->read(indev, &data);
    assert(lvgl_port_get_status(port, &status) == STM_OK && status.last_touch_error == STM_OK &&
           data.state == LV_INDEV_STATE_RELEASED);
    lv_display_set_flush_cb(display, direct_flush);
    display->flush(display, &area, (uint8_t *)buffer);
    assert(display->ready == 6); /* 借用对象允许板级替换刷新路径。 */
    assert(lvgl_port_delete(&port) == STM_OK && !port && deletion_count == 2 &&
           deletion_order[0] == 'i' && deletion_order[1] == 'd');
    assert(lvgl_port_delete(&port) == STM_OK);
    assert(lvgl_port_delete(&other) == STM_OK && !test_alloc_live && !live_displays &&
           !live_inputs);
    cfg.touch = touch;
    assert(lvgl_port_create(&cfg, &port) == STM_OK);
    assert(lvgl_port_delete(&port) == STM_OK && !test_alloc_live);
    return 0;
}
