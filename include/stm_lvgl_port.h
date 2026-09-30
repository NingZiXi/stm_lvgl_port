/** @file stm_lvgl_port.h @brief LVGL 9 同步显示与触摸接入。 */
#ifndef STM_LVGL_PORT_H
#define STM_LVGL_PORT_H
#include <stddef.h>
#include <stdint.h>
#include "stm_err.h"
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct lvgl_port_context *lvgl_port_handle_t;
/** @brief 同步绘制 [x1,x2)×[y1,y2)，紧密排列 RGB565；返回前完成像素使用。 */
typedef stm_err_t (*lvgl_port_draw_cb)(void *context, uint16_t x1, uint16_t y1,
    uint16_t x2, uint16_t y2, const void *pixels);
/** @brief 每次输入轮询调用，成功设置 pressed(0/1)、逻辑坐标；失败上报松开。 */
typedef stm_err_t (*lvgl_port_touch_cb)(void *context, int *pressed, uint16_t *x, uint16_t *y);
typedef struct {
    uint16_t width, height; /**< 非零逻辑尺寸。 */
    void *draw_buffer; /**< 借用 RGB565 缓冲，满足 LV_DRAW_BUF_ALIGN（未定义时至少 2 字节对齐）。 */
    size_t draw_buffer_bytes; /**< 至少完整一行，最大 UINT32_MAX。 */
    void *display_context; /**< 借用同步绘图上下文。 */
    lvgl_port_draw_cb draw; /**< 必需，DMA 和缓存一致性由板级处理。 */
    void *touch_context; /**< 借用输入上下文。 */
    lvgl_port_touch_cb touch; /**< 可选，NULL 时不创建 indev。 */
} lvgl_port_config_t;
typedef struct {
    stm_err_t last_display_error, last_touch_error; /**< 最近一次组件回调结果，初始为 STM_OK。 */
} lvgl_port_status_t;
/** @brief 创建控制对象、PARTIAL RGB565 display 及可选 indev，配置被复制。
 * @param out 句柄地址，*out 必须 NULL；失败保持 NULL，回收所有本次创建资源。
 * @return 参数错误 INVALID_ARG，配置错误 INVALID_CONFIG，非空句柄 INVALID_STATE，分配失败 NO_MEM。
 * @note 先调用 lv_init；外部缓冲与上下文须保持有效。tick/handler、锁、任务由应用管理。
 * 所有 API 与 LVGL 必须串行调用，禁止中断中调用，不默认线程安全。
 */
stm_err_t lvgl_port_create(const lvgl_port_config_t *config, lvgl_port_handle_t *out);
/** @brief 停止所有并发调用后释放 indev、display、控制对象，清空句柄。
 * @note 不释放外部缓冲或上下文；空句柄也成功，其他句柄别名及借用对象同时失效。
 */
stm_err_t lvgl_port_delete(lvgl_port_handle_t *handle);
/** @brief 获取借用的 display；不得删除或替换其 user_data，不能超出 port 生命周期。
 * @note 板级可在首次渲染前设置 DIRECT 缓冲和自定义 flush；该 flush 的错误由板级独立记录。
 */
stm_err_t lvgl_port_get_display(lvgl_port_handle_t port, lv_display_t **display);
/** @brief 获取借用的 indev；未配置触摸时成功返回 NULL，不得删除或替换其 user_data。 */
stm_err_t lvgl_port_get_indev(lvgl_port_handle_t port, lv_indev_t **indev);
/** @brief 获取组件自身回调错误快照，不清除错误、不包含板级替换后的 flush 结果。 */
stm_err_t lvgl_port_get_status(lvgl_port_handle_t port, lvgl_port_status_t *status);
#ifdef __cplusplus
}
#endif
#endif
