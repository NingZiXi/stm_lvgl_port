# stm_lvgl_port

LVGL 9 的芯片无关粘合层，接收 `stm_lcd` 通用 IO/panel/可选 touch 句柄。默认 RGB565 PARTIAL，可显式选择 DIRECT 双整帧；应用提供缓冲、时钟与板级能力，port 不操作 HAL、不分配大帧缓冲、不新增线程。

**版本边界：** 已发布 `v1.0.0` 为通用句柄 PARTIAL 基线；本文描述当前提交的扩展，尚无新的正式版本。DIRECT 与原子寄存器需要匹配的 `stm_lcd` 提交（`STM_LCD_FRAMEBUFFER_API=1`），不能把本 README 套用到旧 tag。原 tag 保留，聚合仓库 gitlink 固定迁移组合；迁移提交同步 GitHub/Gitee，不新增 tag 或 Release。2026-10-11 的 H757 DIRECT Debug 实板范围见下文。

## 🤖 让 Agent 帮助接入

> 将本组件接入工程。先读 AGENTS.md、README、公开头和渲染说明，核对 MCU、HAL、面板/触摸、供电/接线、内存/缓存与 VSYNC 条件，保留已有改动；使用通用句柄，不添加芯片分支或绘图/输入包装。按实际选择 PARTIAL 或 DIRECT，报告源码版本、软件与硬件验证边界，未经确认不烧录或发布。

## 最小 PARTIAL 接入

设备先由各芯片组件创建和初始化，再创建 port：

```c
_Alignas(64) static uint16_t pixels[320 * 16];
static lvgl_port_handle_t port = NULL;
lvgl_port_config_t config =
{
    .io = panel_io,
    .panel = panel,
    .touch = touch, // 无触摸时为 NULL。
    .width = 320,
    .height = 240,
    .draw_buffer = pixels,
    .draw_buffer_bytes = sizeof(pixels),
    .clock_ms = board_clock_ms,
    // 零初始化的 render_mode 即 PARTIAL，同步 draw_async=0。
};
stm_err_t err = lvgl_port_create(&config, &port);
// 成功后通过 lvgl_port_get_display 获得 display 并创建 UI。
// 主循环唯一服务入口：lvgl_port_process(port, board_clock_ms())。
```

`create` 初始化 LVGL、绑定 tick、创建 display/可选 indev，自动订阅面板并借用触摸；失败释放本次所有资源，输出保持 NULL。应用不再调用 `lv_tick_inc` 或 `lv_timer_handler`，也不单独删除获取的 LVGL 对象。

PARTIAL 的面板提供同步紧密 RGB565 区域绘图；异步需设置 `draw_async=1` 且设备/IO 确实支持。SPI 字节顺序通过 `rgb565_swap` 选择，驱动不重复交换。`refresh_full_width/refresh_align_rows` 在渲染前扩展脏区，不在 flush 扩大发送未渲染像素。

## DIRECT 双帧

```c
_Alignas(64) static uint16_t frame_a[800 * 1280];
_Alignas(64) static uint16_t frame_b[800 * 1280];
// 实际由消费工程链接到 SDRAM 等可扫描内存；声明本身不决定存储位置。
config.render_mode = LVGL_PORT_RENDER_DIRECT;
config.width = 800;
config.height = 1280;
config.draw_buffer = frame_a;
config.draw_buffer2 = frame_b;
config.draw_buffer_bytes = sizeof(frame_a); // 每块必须精确为 width*height*2。
config.draw_async = 0; // DIRECT 自带整帧异步切换，不使用 SPI draw_async。
config.rgb565_swap = 0;
config.refresh_full_width = 0;
config.refresh_align_rows = 0;
```

要求两块完整、等大、不重叠、对齐、紧密 RGB565 帧，LVGL 实际 stride 必须等于 width×2；不可用任意行跨度、字节交换或 PARTIAL 对齐策略。面板必须支持 `present`，IO 必须实现 `present/process/busy/stop_scanout`。不满足则创建时返回 INVALID_CONFIG 或 NOT_SUPPORTED，不暗中退回 PARTIAL。

多脏区刷新只在 `lv_display_flush_is_last()` 时提交整帧，其余区域立即 ready。VSYNC 后旧帧安全释放，**新帧持续扫描，仍不能重用**。首次切帧/停止错误锁存，即使 `latch_display_error=0` 也停止新渲染；继续服务在途请求，停止失败不 ready、不删除、不归还缓冲。缓存、扫描时序、停止与恢复责任详见 [docs/render-modes.md](docs/render-modes.md)。不提供 FULL、多屏管理或自动恢复。

## 触摸与调度

同一 port 支持 AXS、FT5206、GT9271 通用快照，无芯片分支。默认每 20 ms 读取、200 ms 过期松开、1000 ms 离线探测。IO/TIMEOUT 标离线，VERIFY 释放但不直接离线；保留最近有效坐标。无新 GT 帧持续按住的行为由 GT 驱动保持。

`process` 串行服务显示 IO、触摸和到期 handler；DMA/VSYNC 等待期间服务独立触摸 IO，不递归进入 LVGL。异步/DIRECT 时面板与触摸不能共用一个 IO。IRQ 只写邮箱；观察/idle 回调不得调用 LVGL、process、delete 或重新配置借用对象。

## 生命周期与诊断

`create` 输出非空返回 INVALID_STATE，分配失败 NO_MEM，非法参数 INVALID_ARG，缺失配置 INVALID_CONFIG。缺失绘图/扫描能力 NOT_SUPPORTED，底层 `stm_err_t` 原样传递。

`get_display/get_indev` 返回借用对象；`get_status` 获取最近显示/触摸错误、输入状态和 handler 耗时，不清除状态。没有触摸时 indev 为 NULL。DIRECT 和配置了 `latch_display_error` 的 PARTIAL 返回锁存的显示错误；普通 PARTIAL 的最近错误查 status。

删除顺序 port → touch/panel → IO → HAL/帧缓冲；`delete(&port)` 空句柄成功，忙/回调中拒绝并保留实例。DIRECT 删除先停止扫描，失败原样返回并保留订阅/对象，成功后解除设备借用，先删除 indev 再删除 display。没有在途请求不等于扫描已停止。需要重建时先完成/停止、删除，再显式重新初始化/创建，不隐式复位其他器件。

## 构建与依赖

```cmake
add_subdirectory(Lib/stm_common)
add_subdirectory(Lib/stm_lcd) # 匹配本次扩展的本地框架
# 按所需型号添加芯片组件，完成 HAL IO 与设备创建。
# 应用提供 lv_conf.h 和 LV_BUILD_CONF_PATH，或已有 lvgl target。
set(STM_LVGL_PORT_LVGL_SOURCE_DIR /path/to/lvgl-9.3.0 CACHE PATH "")
set(STM_LVGL_PORT_FETCH_LVGL OFF CACHE BOOL "")
add_subdirectory(Lib/stm_lvgl_port)
target_link_libraries(your_firmware PRIVATE stm_lvgl_port)
```

LVGL 解析：已有 `lvgl` target → 显式源码 `STM_LVGL_PORT_LVGL_SOURCE_DIR` → FetchContent 离线覆盖 `FETCHCONTENT_SOURCE_DIR_LVGL` → 缺失时固定 9.3.0 提交 `c033a98afddd65aaafeebea625382a94020fe4a7`。默认关闭额外 LVGL demos/examples/internal ThorVG 构建，但尊重应用已设值；不修改供应商源码。镜像可设 `STM_LVGL_PORT_LVGL_GIT_REPOSITORY`，下载开关如上，无效显式目录失败。

`stm_lcd` 只复用已有 target 或同级源码，不由 port 下载。`stm_common` 优先已有 target/同级源码，否则固定 v1.0.0 提交 `ce3d186dde2d374a8e9c7b9068a7b88f97d57dc1`，支持 STM_COMMON_FETCH、STM_COMMON_GIT_REPOSITORY 与 FETCHCONTENT_SOURCE_DIR_STM_COMMON。公开链接 LVGL、common 与 lcd。已有 target 的实际版本由消费工程验证。

## 迁移与软件验证

| 原接口/用法 | 本工作区 |
| --- | --- |
| v0.3.0 draw/wait/touch 应用回调 | 通用 IO/panel/touch；不保留旧包装 |
| v1.0.0 零初始化配置 | 仍默认 PARTIAL，现有同步/异步行为保留 |
| 消费工程自行替换 DIRECT flush | 显式 render_mode + 通用 present 能力；不替换 port 回调 |
| 公开内部状态/手动 tick、handler | get_display/get_indev/get_status 与唯一 process |

```sh
cmake -S tests -B build/tests -G Ninja
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
python tests/test_dependency.py --lcd-source ../stm_lcd --common-source ../stm_common --lvgl-source /path/to/lvgl-9.3.0
cmake -S tests/real_lvgl -B build/real -G Ninja -DSTM_LVGL_PORT_LVGL_SOURCE_DIR=/path/to/lvgl-9.3.0
cmake --build build/real
ctest --test-dir build/real --output-on-failure
```

测试包括原有 PARTIAL/异步/输入契约、五驱动通用句柄集成、DIRECT 分配/配置/VSYNC/停止故障，真实 LVGL 的局部渲染及双帧同步检查。真实 LVGL 测试使用软件 LTDC 模型，PARTIAL 与 DIRECT 在独立进程中分别验证首次创建、渲染及销毁；不证明 STM32 DCache、DSI 时序或实际硬件稳定性。中文接入例见 [examples/stm32_hal](examples/stm32_hal/README.md)。

## 本地消费工程实板验证（2026-10-11）

H757 + ILI9881C/GT9271、LVGL 9.3.0、800×1280 RGB565 DIRECT 双缓冲和板级 DMA2D，
LVGL-Debug 诊断固件通过 ST-Link 双核烧录独立读回、持续刷新、触摸/按钮事件及五次软件复位，
用户确认显示及触摸正常。修复了首次 DIRECT create 在 LVGL 初始化前查询 stride 导致拒绝有效配置的问题；
create 自动完成初始化后再校验实际跨度，应用无需预先 lv_init()。

Debug/Release 消费固件已构建；本次未重新烧录 Release、Widgets 或 PARTIAL，未覆盖其他芯片实物、
掉电复位、长期稳定性、色序/边角坐标量化或性能基准。该记录仅对应本次迁移提交组合，原正式 tag 不含本次扩展。
日志、固件 SHA256、源码哈希和备份留在消费工程本地构建目录；迁移提交已同步 GitHub/Gitee，尚未发布包含扩展的新 tag 或 Release。

[MIT](LICENSE)。
