/**
 * @file test_real_render.c
 * @brief 使用真实 LVGL 验证渲染、双缓冲同步和扫描帧隔离。
 */
#include "stm_lvgl_port.h"
#include "stm_lcd_ili9881c.h"
#include "stm_lcd_impl.h"
#include <assert.h>
#include <string.h>
#define WIDTH 64u
#define HEIGHT 48u
_Alignas(64) static uint16_t frames[2][WIDTH * HEIGHT];
_Alignas(64) static uint16_t partial[WIDTH * 8u];
static uint32_t tick;

typedef struct
{
    struct stm_lcd_io io;             // 通用 IO 与借用状态。
    const void *active, *next;        // 当前扫描帧与待切换帧。
    uint32_t active_hash;             // 当前扫描帧校验值。
    unsigned presents, copies, stops; // 提交、复制与停止次数。
} backend_t;

static uint32_t checksum(const void *p)
{
    const uint8_t *b = p;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < WIDTH * HEIGHT * 2u; ++i)
    {
        hash = (hash ^ b[i]) * 16777619u;
    }
    return hash;
}

static void verify_scanout(backend_t *b)
{
    if (b->active)
    {
        assert(checksum(b->active) == b->active_hash);
    }
}

static stm_err_t command(void *ctx, uint8_t c, const uint8_t *data, size_t n)
{
    (void)ctx;
    (void)c;
    assert(!n || data);
    return STM_OK;
}

static void delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    assert(ms > 0);
}

static stm_err_t present(void *ctx, const void *pixels, uint16_t w, uint16_t h)
{
    backend_t *b = ctx;
    assert(w == WIDTH && h == HEIGHT && pixels != b->active);
    verify_scanout(b);
    assert(!b->next);
    b->next = pixels;
    ++b->presents;
    return STM_OK;
}

static int busy(void *ctx)
{
    (void)ctx;
    return 0;
}

static stm_err_t stop(void *ctx)
{
    backend_t *b = ctx;
    verify_scanout(b);
    b->active = b->next = NULL;
    ++b->stops;
    return STM_OK;
}

static void process(void *ctx)
{
    backend_t *b = ctx;
    verify_scanout(b);
    if (b->next)
    {
        b->active = b->next;
        b->next = NULL;
        b->active_hash = checksum(b->active);
        assert(stm_lcd_io_complete(&b->io, STM_OK) == STM_OK);
    }
}

static stm_err_t
copy(void *ctx, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const void *pixels)
{
    backend_t *b = ctx;
    assert(x1 < x2 && y1 < y2 && x2 <= WIDTH && y2 <= HEIGHT && pixels);
    const uint16_t *src = pixels;
    for (uint16_t y = y1; y < y2; ++y)
    {
        memcpy(&frames[1][y * WIDTH + x1], src, (x2 - x1) * 2u);
        src += x2 - x1;
    }
    ++b->copies;
    return STM_OK;
}

static uint32_t clock_ms(void)
{
    return tick;
}

static const stm_lcd_io_ops_t ops = {.tx_param = command,
                                     .draw_region = copy,
                                     .present = present,
                                     .stop_scanout = stop,
                                     .busy = busy,
                                     .process = process};

static void run(lvgl_port_render_mode_t mode)
{
    backend_t b = {0};
    stm_lcd_panel_handle_t panel = NULL;
    lvgl_port_handle_t port = NULL;
    memset(frames, 0, sizeof frames);
    assert(stm_lcd_io_init(&b.io, &ops, &b) == STM_OK);
    lcd_ili9881c_config_t pc = {.io = &b.io, .width = WIDTH, .height = HEIGHT, .delay_ms = delay};
    assert(lcd_ili9881c_create(&pc, &panel) == STM_OK && stm_lcd_panel_init(panel) == STM_OK);
    lvgl_port_config_t cfg = {
        .render_mode = mode,
        .io = &b.io,
        .panel = panel,
        .width = WIDTH,
        .height = HEIGHT,
        .draw_buffer = mode == LVGL_PORT_RENDER_DIRECT ? (void *)frames[0] : (void *)partial,
        .draw_buffer2 = mode == LVGL_PORT_RENDER_DIRECT ? frames[1] : NULL,
        .draw_buffer_bytes = mode == LVGL_PORT_RENDER_DIRECT ? sizeof frames[0] : sizeof partial,
        .clock_ms = clock_ms,
        .handler_period_ms = 4};
    assert(lvgl_port_create(&cfg, &port) == STM_OK);
    lv_display_t *display = NULL;
    assert(lvgl_port_get_display(port, &display) == STM_OK);
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_t *label = lv_label_create(screen);
    assert(label);
    lv_obj_center(label);
    for (unsigned i = 0; i < 20; ++i)
    {
        lv_label_set_text(label, (i & 1u) ? "A" : "B");
        lv_obj_set_style_bg_color(screen, lv_color_hex((i & 1u) ? 0xff0000 : 0x0000ff), 0);
        for (unsigned n = 0; n < 5; ++n)
        {
            tick += 10;
            assert(lvgl_port_process(port, tick) == STM_OK);
            verify_scanout(&b);
        }
    }
    process(&b);
    lvgl_port_status_t status = {0};
    assert(lvgl_port_get_status(port, &status) == STM_OK && status.last_display_error == STM_OK &&
           !status.flush_pending);
    if (mode == LVGL_PORT_RENDER_DIRECT)
    {
        assert(b.presents >= 20 && !b.copies && b.active);
    }
    else
    {
        assert(b.copies >= 20 && !b.presents);
    }
    assert(lvgl_port_delete(&port) == STM_OK && !b.active);
    assert(stm_lcd_panel_delete(&panel) == STM_OK && stm_lcd_io_deinit(&b.io) == STM_OK);
}

int main(int argc, char **argv)
{
    assert(argc == 2 && !lv_is_initialized());
    if (strcmp(argv[1], "partial") == 0)
    {
        run(LVGL_PORT_RENDER_PARTIAL);
    }
    else
    {
        assert(strcmp(argv[1], "direct") == 0);
        run(LVGL_PORT_RENDER_DIRECT);
    }
    assert(lv_is_initialized());
    lv_deinit();
    return 0;
}
