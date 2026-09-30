/** @file example.h @brief LVGL tick/handler 最小接入。 */
#ifndef LVGL_PORT_EXAMPLE_H
#define LVGL_PORT_EXAMPLE_H
#include "stm32h7xx_hal.h"
#include "stm_lvgl_port.h"
/** @brief 应用先调用 lv_init；config 上下文/缓冲必须持久有效。 */
stm_err_t lvgl_port_example_start(const lvgl_port_config_t *config,lvgl_port_handle_t *port);
void lvgl_port_example_step(void);
#endif
