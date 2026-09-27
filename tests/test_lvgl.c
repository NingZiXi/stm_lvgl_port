#include "stm_lvgl_port.h"
#include <assert.h>
#include <string.h>
static lv_display_t display;
static lv_indev_t indev;
static int drawings, released, result;
lv_display_t *lv_display_create(int32_t w,int32_t h) { assert(w==2 && h==2); memset(&display,0,sizeof display); return &display; }
void lv_display_delete(lv_display_t *d) { assert(d==&display); }
void lv_display_set_color_format(lv_display_t *d,int f) { (void)d; assert(f==LV_COLOR_FORMAT_RGB565); }
void lv_display_set_user_data(lv_display_t *d,void *p) { d->user=p; }
void *lv_display_get_user_data(lv_display_t *d) { return d->user; }
void lv_display_set_buffers(lv_display_t *d,void *a,void *b,uint32_t n,int mode) { (void)d;(void)b;assert(a && n>=4 && mode==LV_DISPLAY_RENDER_MODE_PARTIAL); }
void lv_display_set_flush_cb(lv_display_t *d,void (*cb)(lv_display_t *,const lv_area_t *,uint8_t *)) { d->flush=cb; }
void lv_display_flush_ready(lv_display_t *d) { ++d->ready; }
lv_indev_t *lv_indev_create(void) { memset(&indev,0,sizeof indev); return &indev; }
void lv_indev_delete(lv_indev_t *i) { assert(i==&indev); }
void lv_indev_set_type(lv_indev_t *i,int t) { (void)i; assert(t==LV_INDEV_TYPE_POINTER); }
void lv_indev_set_display(lv_indev_t *i,lv_display_t *d) { (void)i;assert(d==&display); }
void lv_indev_set_user_data(lv_indev_t *i,void *p) { i->user=p; }
void *lv_indev_get_user_data(lv_indev_t *i) { return i->user; }
void lv_indev_set_read_cb(lv_indev_t *i,void (*cb)(lv_indev_t *,lv_indev_data_t *)) { i->read=cb; }
static int draw(void *ctx,uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,const void *pixels)
{ (void)ctx; assert(x1==0 && y1==0 && x2==2 && y2==1 && pixels); ++drawings; return result; }
static int touch(void *ctx,int *pressed,uint16_t *x,uint16_t *y)
{ (void)ctx; *pressed=1; *x=1; *y=1; return 0; }
int main(void)
{
    stm_lvgl_port_t port={0}; uint8_t buffer[4]={0};
    stm_lvgl_port_config_t cfg={2,2,buffer,sizeof buffer,NULL,draw,NULL,touch};
    lv_area_t area={0,0,1,0}; lv_indev_data_t input={0};
    assert(stm_lvgl_port_attach(&port,&cfg)==0);
    display.flush(&display,&area,buffer);
    assert(drawings==1 && display.ready==1 && port.last_display_error==0);
    result=-8; display.flush(&display,&area,buffer);
    assert(display.ready==2 && port.last_display_error==-8);
    area.x2=2; display.flush(&display,&area,buffer);
    assert(display.ready==3 && drawings==2 && port.last_display_error==-1);
    indev.read(&indev,&input);
    assert(input.state==LV_INDEV_STATE_PRESSED && input.point.x==1 && input.point.y==1);
    stm_lvgl_port_detach(&port);
    return 0;
}
