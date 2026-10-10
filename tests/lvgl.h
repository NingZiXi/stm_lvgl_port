/**
 * @file    lvgl.h
 * @brief   提供主机测试使用的 LVGL 接口替身。
 */
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
    uint32_t period; // 模拟定时器周期。
} lv_timer_t;
typedef struct lv_indev_t lv_indev_t;

typedef struct
{
    int32_t x1, y1, x2, y2; // 包含端点矩形。
} lv_area_t;

typedef struct
{
    int32_t x, y; // 模拟坐标。
} lv_point_t;

typedef struct
{
    int state;        // 模拟按下状态。
    lv_point_t point; // 模拟触点快照。
} lv_indev_data_t;

#define LV_COLOR_FORMAT_RGB565 1
#define LV_DISPLAY_RENDER_MODE_PARTIAL 1
#define LV_DISPLAY_RENDER_MODE_DIRECT 2
#define LV_INDEV_TYPE_POINTER 1
#define LV_INDEV_STATE_PRESSED 1
#define LV_INDEV_STATE_RELEASED 0

struct lv_display_t
{
    void *user;                                                  // 模拟回调上下文。
    void (*flush)(lv_display_t *, const lv_area_t *, uint8_t *); // 模拟刷新函数。
    void (*wait)(lv_display_t *);                                // 模拟等待函数。
    void *buffers[2];                                            // 借用的像素缓冲。
    int ready;                                                   // 安全完成次数。
    int mode, not_last;             // 模拟渲染模式与末次刷新。
    void (*event_cb)(lv_event_t *); // 模拟事件函数。
    void *event_user;               // 模拟事件上下文。
    uint32_t event_count;           // 模拟事件数量。
};

struct lv_indev_t
{
    void *user;                                    // 模拟回调上下文。
    void (*read)(lv_indev_t *, lv_indev_data_t *); // 模拟输入函数。
    lv_timer_t timer;                              // 模拟输入定时器。
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
int lv_display_flush_is_last(lv_display_t *d);
lv_indev_t *lv_indev_create(void);
void lv_indev_delete(lv_indev_t *i);
void lv_indev_set_type(lv_indev_t *i, int t);
void lv_indev_set_display(lv_indev_t *i, lv_display_t *d);
void lv_indev_set_user_data(lv_indev_t *i, void *p);
void *lv_indev_get_user_data(lv_indev_t *i);
void lv_indev_set_read_cb(lv_indev_t *i, void (*cb)(lv_indev_t *, lv_indev_data_t *));

struct lv_event_t
{
    lv_area_t *area; // 模拟脏区域。
    void *user;      // 模拟回调上下文。
};

#define LV_EVENT_INVALIDATE_AREA 1
void *lv_event_get_user_data(lv_event_t *e);
lv_area_t *lv_event_get_invalidated_area(lv_event_t *e);
uint32_t lv_display_get_event_count(lv_display_t *d);
void lv_display_add_event_cb(lv_display_t *d, void (*cb)(lv_event_t *), int code, void *user);
lv_timer_t *lv_indev_get_read_timer(lv_indev_t *i);
void lv_timer_set_period(lv_timer_t *t, uint32_t period);
void lv_draw_sw_rgb565_swap(void *pixels, uint32_t count);
uint32_t lv_draw_buf_width_to_stride(uint32_t width, int color_format);
#endif
