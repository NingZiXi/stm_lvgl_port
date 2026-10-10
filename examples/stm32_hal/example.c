/**
 * @file    example.c
 * @brief   演示通用LVGL接入及板级配置边界。
 */
#include "example.h"

stm_err_t lvgl_port_example_start(const lvgl_port_config_t *config, lvgl_port_handle_t *port)
{
    stm_err_t err = lvgl_port_create(config, port);
    if (err != STM_OK)
        return err;
    lv_display_t *display = NULL;
    err = lvgl_port_get_display(*port, &display);
    if (err != STM_OK)
    {
        lvgl_port_delete(port);
        return err;
    }
    lv_obj_t *label = lv_label_create(lv_display_get_screen_active(display));
    if (!label)
    {
        lvgl_port_delete(port);
        return STM_ERR_NO_MEM;
    }
    lv_label_set_text(label, "STM32 LVGL");
    lv_obj_center(label);

    return STM_OK;
}

stm_err_t lvgl_port_example_step(lvgl_port_handle_t port, uint32_t now)
{
    return lvgl_port_process(port, now);
}
