/** @file example.c @brief 由应用统一初始化 LVGL 后注册 display/input。 */
#include "example.h"
static uint32_t last_tick_ms;
stm_err_t lvgl_port_example_start(const lvgl_port_config_t *config,lvgl_port_handle_t *port) {
    stm_err_t err=lvgl_port_create(config,port);
    if (err!=STM_OK) return err;
    lv_display_t *display=NULL;
    err=lvgl_port_get_display(*port,&display);
    if (err!=STM_OK) { lvgl_port_delete(port); return err; }
    lv_obj_t *label=lv_label_create(lv_display_get_screen_active(display));
    if (!label) { lvgl_port_delete(port); return STM_ERR_NO_MEM; }
    lv_label_set_text(label,"STM32 LVGL");
    lv_obj_center(label);
    last_tick_ms=HAL_GetTick();
    return STM_OK;
}
void lvgl_port_example_step(void) {
    uint32_t now=HAL_GetTick();
    lv_tick_inc(now-last_tick_ms); last_tick_ms=now;
    (void)lv_timer_handler();
}
