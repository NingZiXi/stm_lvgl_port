/** @file example.h @brief One cooperative process entry; clock is in configuration. */
#ifndef LVGL_PORT_EXAMPLE_H
#define LVGL_PORT_EXAMPLE_H
#include "stm_lvgl_port.h"
stm_err_t lvgl_port_example_start(const lvgl_port_config_t *config, lvgl_port_handle_t *port);
stm_err_t lvgl_port_example_step(lvgl_port_handle_t port, uint32_t now);
#endif
