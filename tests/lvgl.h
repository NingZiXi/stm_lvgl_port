#ifndef LVGL_H
#define LVGL_H
#include <stdint.h>
void lv_init(void);
int lv_is_initialized(void);
void lv_tick_set_cb(uint32_t (*cb)(void));
uint32_t lv_timer_handler(void);
typedef struct lv_display_t lv_display_t;
typedef struct lv_event_t lv_event_t;

typedef struct
{
    uint32_t period;
} lv_timer_t;
typedef struct lv_indev_t lv_indev_t;

typedef struct
{
    int32_t x1, y1, x2, y2;
} lv_area_t;

typedef struct
{
    int32_t x, y;
} lv_point_t;

typedef struct
{
    int state;
    lv_point_t point;
} lv_indev_data_t;

#define LV_COLOR_FORMAT_RGB565 1
#define LV_DISPLAY_RENDER_MODE_PARTIAL 1
#define LV_INDEV_TYPE_POINTER 1
#define LV_INDEV_STATE_PRESSED 1
#define LV_INDEV_STATE_RELEASED 0

struct lv_display_t
{
    void *user;
    void (*flush)(lv_display_t *, const lv_area_t *, uint8_t *);
    void (*wait)(lv_display_t *);
    void *buffers[2];
    int ready;
    void (*event_cb)(lv_event_t *);
    void *event_user;
    uint32_t event_count;
};

struct lv_indev_t
{
    void *user;
    void (*read)(lv_indev_t *, lv_indev_data_t *);
    lv_timer_t timer;
};

lv_display_t *lv_display_create(int32_t w, int32_t h);
void lv_display_delete(lv_display_t *d);
void lv_display_set_color_format(lv_display_t *d, int f);
void lv_display_set_user_data(lv_display_t *d, void *p);
void *lv_display_get_user_data(lv_display_t *d);
void lv_display_set_buffers(lv_display_t *d, void *a, void *b, uint32_t n, int mode);
void lv_display_set_flush_cb(lv_display_t *d,
                             void (*cb)(lv_display_t *, const lv_area_t *, uint8_t *));
void lv_display_set_flush_wait_cb(lv_display_t *d, void (*cb)(lv_display_t *));
void lv_display_flush_ready(lv_display_t *d);
lv_indev_t *lv_indev_create(void);
void lv_indev_delete(lv_indev_t *i);
void lv_indev_set_type(lv_indev_t *i, int t);
void lv_indev_set_display(lv_indev_t *i, lv_display_t *d);
void lv_indev_set_user_data(lv_indev_t *i, void *p);
void *lv_indev_get_user_data(lv_indev_t *i);
void lv_indev_set_read_cb(lv_indev_t *i, void (*cb)(lv_indev_t *, lv_indev_data_t *));

struct lv_event_t
{
    lv_area_t *area;
    void *user;
};

#define LV_EVENT_INVALIDATE_AREA 1
void *lv_event_get_user_data(lv_event_t *e);
lv_area_t *lv_event_get_invalidated_area(lv_event_t *e);
uint32_t lv_display_get_event_count(lv_display_t *d);
void lv_display_add_event_cb(lv_display_t *d, void (*cb)(lv_event_t *), int code, void *user);
lv_timer_t *lv_indev_get_read_timer(lv_indev_t *i);
void lv_timer_set_period(lv_timer_t *t, uint32_t period);
void lv_draw_sw_rgb565_swap(void *pixels, uint32_t count);
#endif
