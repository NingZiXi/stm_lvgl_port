#include "example.h"

static uint32_t last_tick_ms;

int stm_lvgl_port_example_start(stm_lvgl_port_t *port,
                                const stm_lvgl_port_config_t *config)
{
    int result;
    lv_init(); /* 工程尚未初始化 LVGL 时调用一次；其他模块已调用过则移至应用统一入口。 */
    last_tick_ms = HAL_GetTick();
    result = stm_lvgl_port_attach(port, config);
    if (result != 0) return result;
    lv_obj_t *label = lv_label_create(lv_screen_active());
    if (label) {
        lv_label_set_text(label, "STM32 LVGL");
        lv_obj_center(label);
    }
    return 0;
}

void stm_lvgl_port_example_step(void)
{
    uint32_t now = HAL_GetTick();
    lv_tick_inc(now - last_tick_ms);
    last_tick_ms = now;
    (void)lv_timer_handler();
}
