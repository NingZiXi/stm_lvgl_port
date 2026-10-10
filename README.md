# stm_lvgl_port

LVGL 9 的轻量显示/输入粘合层。接收通用 IO、面板、可选触摸、外部 RGB565 缓冲及运行策略，自动连接刷新完成和触摸服务。无芯片判断、HAL 依赖、后台任务或大块帧缓冲自动分配。

## 🤖 让 Agent 帮助接入

复制以下 Prompt，将 `<...>` 替换为实际需求：

> 将 [stm32-hal-lib](https://github.com/NingZiXi/stm32-hal-lib) 中的 `stm_lvgl_port` 接入当前工程，实现 `<功能>`。硬件：`<MCU/器件型号、外设与引脚、屏幕尺寸与方向（按需填写）>`。先读组件 AGENTS.md 和 README，核对配置，保留已有代码；缺失信息先询问，不猜接线。完成后说明版本、编译结果和未验证项；未经确认不烧录或擦写。

💡 可使用 [Gitee 镜像](https://gitee.com/nzxhg/stm32-hal-lib)。需要业务入口和日志时，参考 [skills/README.md](https://github.com/NingZiXi/stm32-hal-lib/blob/main/skills/README.md)（仅适用于已有 CubeMX1 CMake 工程）；编译通过不代表实板验证通过。

## 目录与接入边界

- `include/stm_lvgl_port.h`：配置、生命周期、process、设备及状态查询。
- `src/`：LVGL 接入、协作式调度、刷新所有权与输入策略。
- `examples/stm32_hal/`：通用句柄接入标签 UI 的最小示例。
- `docs/render-modes.md`：[渲染与缓冲所有权](docs/render-modes.md)。
- `tests/`：mock LVGL、分配故障、可替换设备与依赖解析回归。

应用只需要创建板级 IO/面板/触摸、配置 port、创建 UI，并在主循环调用 process。不要另写 draw、wait、touch、flush_ready 包装，也不要引入重复 display_service。

## CMake 与依赖

C11、CMake 3.22+、`stm_lcd`、`stm_common` 和 LVGL 9；实际验证基线为 LVGL 9.3.0。

```cmake
# 先提供真实 stm_common/stm_lcd/lvgl target，或按下表准备源码。
add_subdirectory(Lib/stm_lvgl_port)
target_link_libraries(your_firmware PRIVATE stm_lvgl_port)
```

`stm_lcd` 只复用已有 target 或同级源码，缺失明确报错，port 不自动下载框架。`stm_common` 优先 target/同级源码，否则可固定获取 v1.0.0 提交 `ce3d186dde2d374a8e9c7b9068a7b88f97d57dc1`；可用 `STM_COMMON_FETCH=OFF` 关闭下载。

LVGL 解析顺序为已有 `lvgl` target（含 alias） → 显式源码 → FetchContent 离线覆盖 → 固定获取：

| 选项 | 用途 |
| --- | --- |
| `STM_LVGL_PORT_LVGL_SOURCE_DIR` | 显式离线源码目录，无效时不回退下载 |
| `FETCHCONTENT_SOURCE_DIR_LVGL` | FetchContent 离线源码覆盖 |
| `STM_LVGL_PORT_FETCH_LVGL=OFF` | 缺少 target/本地源码时明确失败 |
| `STM_LVGL_PORT_LVGL_GIT_REPOSITORY` | 可信镜像；固定 `c033a98afddd65aaafeebea625382a94020fe4a7`（9.3.0） |

应用提供 `lv_conf.h` / `LV_BUILD_CONF_PATH`。默认关闭额外 demos/examples/ThorVG 构建，但不强制覆盖应用已设置的选项。不要通过修改供应商 LVGL 源码接入；完全离线时同时准备所有依赖。

## 最小接入过程

下例为已验证 STM32F4 配置片段，不包含硬件初始化；传入设备必须已创建，面板必须已成功 init 并开启显示。两块缓冲静态保存于普通 SRAM，不能放入 CCM。

```c
#include "stm_lvgl_port.h"
#include "stm32f4xx_hal.h"

static LV_ATTRIBUTE_MEM_ALIGN uint8_t buffer_a[170 * 16 * 2];
static LV_ATTRIBUTE_MEM_ALIGN uint8_t buffer_b[170 * 16 * 2];

stm_err_t create_display(stm_lcd_io_handle_t io,
                         stm_lcd_panel_handle_t panel,
                         stm_lcd_touch_handle_t touch,
                         lvgl_port_handle_t *port)
{
    lvgl_port_config_t config =
    {
        .io = io,
        .panel = panel,
        .touch = touch,
        .width = 170,
        .height = 560,
        .draw_buffer = buffer_a,
        .draw_buffer_bytes = sizeof buffer_a,
        .draw_buffer2 = buffer_b,
        .clock_ms = HAL_GetTick,
        .draw_async = 1,
        .rgb565_swap = 1,
        .refresh_full_width = 1,
        .refresh_align_rows = 8,
        .latch_display_error = 1,
    };
    return lvgl_port_create(&config, port);
}
```

调用前 `*port` 必须为 NULL，检查返回值后再创建应用 UI。`touch = NULL` 可仅接显示。无 DMA 时改为 `draw_async = 0`，可省略第二缓冲。UI 可通过 `lvgl_port_get_display()` 获取 display 并使用其活动 screen；不能由应用单独删除 port 持有的 display/indev。

主循环使用 `lvgl_port_process(port, HAL_GetTick())`，**即使出现锁存错误也要继续服务**，否则在途缓冲可能无法安全归还。port 自动初始化 LVGL（必要时）并绑定 tick；不要同时调用 `lv_tick_inc()` 或另行运行 `lv_timer_handler()`。最小标签示例见 [example.c](examples/stm32_hal/example.c) 与[示例说明](examples/stm32_hal/README.md)。

## 配置与运行契约

| 配置 | 约束 / 默认 |
| --- | --- |
| `io` / `panel` | 必须匹配同一个 IO，尺寸必须与配置一致，一个面板仅一个刷新所有者 |
| `draw_buffer` / `draw_buffer2` | RGB565、至少完整一行、每块不超过 UINT32_MAX；双缓冲等大、不重叠、满足 LV_DRAW_BUF_ALIGN（未定义时至少 2 字节对齐） |
| `draw_async` | 布尔值；面板和 IO 都须支持异步完成服务 |
| `clock_ms` / `idle` | 必需毫秒时钟；idle 可选，单步执行，不重入 LVGL/process |
| `rgb565_swap` | 传输前交换字节；不要在其他层重复交换 |
| `refresh_full_width` | 在渲染前扩展到全宽，不在传输时拼接像素 |
| `refresh_align_rows` | 0/1 关闭；启用时高度和缓冲可容纳的整行数须整除对齐行数 |
| `handler_period_ms` | 0 使用 10 ms；表示调度周期，不是保证帧率 |
| `touch_period_ms` / `touch_stale_ms` / `touch_retry_ms` | 0 分别使用 20 / 200 / 1000 ms |
| `latch_display_error` | 首次显示错误后停止新刷新，但继续服务在途 IO |
| `flush_observer` / `touch_observer` | 可选主循环诊断回调，不调用 LVGL、process 或 delete |

- 异步模式下触摸 IO 必须与面板 IO 分离，否则构造拒绝；配置触摸逻辑边界应与显示一致。
- process 先服务 IO 和输入，再运行到期 LVGL handler；DMA 等待期间继续服务独立 I2C 输入，不递归进入 handler。
- 自动完成链为 IO 安全停止 → 面板请求结束 → LVGL flush_ready。立即完成与失败提交也必须保持一次归还语义。
- 输入读取回调只取缓存。IO/TIMEOUT 导致离线；VERIFY 释放输入但不直接判为总线离线。过期释放及探测恢复不自动复位设备。
- 所有 API 与 LVGL 在串行主循环使用；禁止 ISR、递归 process、观察回调中删除或操作 LVGL。对象存活时不替换全局 tick；不提供多屏管理。
- 删除前必须无在途刷新，port 自动解除面板订阅和触摸借用，不删除外部设备或缓冲。

## 状态、错误与迁移

观察回调的 `context` 来自 `observer_context`。刷新观察中的 `pixels` 为本次像素数，`complete=0` 表示准备提交（不等于提交成功），`complete=1` 表示此次刷新结束及最终结果；不得据观察回调自行归还缓冲。触摸观察中的 down 为零时，x/y 保留最近有效坐标，不能据此判为按下。

`lvgl_port_get_status()` 返回显示/触摸最近错误、在途刷新、在线/按下状态及坐标；查询不清错。`last_handler_ms` 是本次 handler 耗时（含 DMA 等待），本次未运行时为零，不直接等于 CPU 百分比。

process 的返回值为锁存显示错误；触摸错误应从状态读取。参数/配置错误检查尺寸、IO 一致性、缓冲对齐、重叠及能力；INVALID_STATE 检查设备占用和生命周期；INVALID_CONTEXT 检查 process 重入。

旧 draw/wait/touch 配置改为通用句柄，不保留兼容包装。板级仅保留硬件配置、构造/初始化和共享复位协调；UI、业务与可选诊断留在应用。不修改 CubeMX/HAL/供应商 LVGL，也不在此次接入中混入 SPI 提速或 FULL/DIRECT 模式。

## 主机测试

需要 CMake 3.22+、Ninja、支持 C11/C++17 的主机编译器。从**组件根目录**执行：

```sh
cmake -S tests -B build/tests -G Ninja
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

测试时需将 `stm_common` 和 `stm_lcd` 放在本组件同级（`stm_lcd` 自身只需同级 `stm_common`），或按测试 CMake 的要求准备依赖。嵌入式交叉编译器不用于运行主机测试。可显式传入 `-DCMAKE_C_COMPILER=<主机C编译器>` 和 `-DCMAKE_CXX_COMPILER=<主机C++编译器>`。

mock 测试不替代真实 LVGL 渲染。独立依赖检查（默认不访问网络）可从组件根目录执行：

```sh
python tests/test_dependency.py --lcd-source /absolute/path/to/stm_lcd --common-source /absolute/path/to/stm_common
```

加 `--lvgl-source /absolute/path/to/lvgl` 可验证真实离线 LVGL 的编译、链接与运行；仅显式 `--fetch` 才检查真实网络获取。消费工程另维护真实渲染、DMA/输入安全及构建矩阵回归。

## 验证范围与许可

2026-10-10，消费工程在 STM32F407 + AXS15231B、21 MHz SPI、170×560 原生竖屏、RGB565、两个 16 行普通 SRAM 缓冲的配置下完成显示和触摸验收。当前文档整理不改变这一运行配置。

该结果不等于其他模组、其他 MCU 或长时间稳定性验收；既有触摸畸形帧问题不能标记为已修复。统一接口已替换旧 API，接入时以实际检出的公开头文件及提交为准，不将其视为旧版本兼容补丁，也不把依赖固定提交当作正式 Release。

维护者的 MIT 许可证保持原内容，见 [LICENSE](LICENSE)。源码中已有的第三方来源及许可说明保持保留。Agent 工作约束见 [AGENTS.md](AGENTS.md)。
