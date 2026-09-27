#ifndef STM_LVGL_PORT_EXAMPLE_H
#define STM_LVGL_PORT_EXAMPLE_H
#include "stm32h7xx_hal.h"
#include "stm_lvgl_port.h"

/* HAL 已初始化，board->draw 为同步刷屏回调，touch 可为 NULL。 */
int stm_lvgl_port_example_start(stm_lvgl_port_t *port,
                                const stm_lvgl_port_config_t *config);
/* 主循环反复调用；同一 LVGL 实例只能由一个线程进入。 */
void stm_lvgl_port_example_step(void);
#endif
