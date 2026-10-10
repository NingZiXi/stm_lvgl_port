/**
 * @file    example.h
 * @brief   提供最小 LVGL 示例启动与处理入口。
 */
#ifndef LVGL_PORT_EXAMPLE_H
#define LVGL_PORT_EXAMPLE_H
#include "stm_lvgl_port.h"
/**
 * @brief 创建 port 并在其活动屏幕上创建标签示例。
 *
 * @param config 有效设备、缓冲及运行配置
 * @param port 初始为空的输出句柄地址
 * @return STM_OK；创建失败返回对应错误并回滚 port。
 */
stm_err_t lvgl_port_example_start(const lvgl_port_config_t *config, lvgl_port_handle_t *port);

/**
 * @brief 运行一次协作式显示处理。
 *
 * @param port 已创建示例 port
 * @param now 与配置时钟同源的毫秒时间
 * @return STM_OK 或 port 处理错误。
 */
stm_err_t lvgl_port_example_step(lvgl_port_handle_t port, uint32_t now);
#endif
