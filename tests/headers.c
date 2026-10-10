/**
 * @file    headers.c
 * @brief   验证公开头文件的独立消费编译。
 */
#include "stm_lvgl_port.h"
_Static_assert(sizeof(stm_err_t) == 4, "error width");
