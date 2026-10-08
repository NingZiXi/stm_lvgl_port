# stm_lvgl_port：LVGL 9 同步粘合层

独立于屏幕芯片，通过同步 draw 与可选 touch 回调接入 LVGL 9，默认 RGB565 PARTIAL。板级负责 tick、handler、总线、锁、色序和缓存。

接入方式、缓冲归属和切帧边界见 [PARTIAL / DIRECT 指南](docs/render-modes.md)。默认路径不需要板级 DIRECT 文件。

## 最小调用

```c
lvgl_port_handle_t port = NULL;
lvgl_port_config_t cfg = {
    .width=BOARD_LCD_WIDTH, .height=BOARD_LCD_HEIGHT,
    .draw_buffer=draw_buffer, .draw_buffer_bytes=sizeof(draw_buffer),
    .display_context=&board_display, .draw=board_draw,
    .touch_context=&board_touch, .touch=board_touch_read, /* 无触摸设 NULL。 */
};
lv_init();
stm_err_t err = lvgl_port_create(&cfg, &port);
lvgl_port_status_t status;
if (err == STM_OK) err = lvgl_port_get_status(port, &status);
/* 应用提供 tick 并周期调用 lv_timer_handler；退出时 lvgl_port_delete(&port)。 */
```

## 错误与资源契约

公开操作、draw 和 touch 返回 `stm_err_t`，成功为 `STM_OK`，失败检查 `err != STM_OK`。HAL 适配将 `HAL_TIMEOUT` 映射为 `STM_ERR_TIMEOUT`，`HAL_ERROR/HAL_BUSY` 映射为 `STM_ERR_IO`；组件原样记录回调错误。

| 情况 | 错误 |
| --- | --- |
| 空 API 参数、非法刷新参数或 pressed 值 | `STM_ERR_INVALID_ARG` |
| 缺少 draw/缓冲、尺寸为零、缓冲大小或对齐不合法 | `STM_ERR_INVALID_CONFIG` |
| 创建输出句柄非空 | `STM_ERR_INVALID_STATE` |
| 控制对象/LVGL 对象分配失败 | `STM_ERR_NO_MEM` |
| 刷新矩形或有效触点越界 | `STM_ERR_OUT_OF_RANGE` |
| draw/touch 适配失败 | 原始 `stm_err_t`，不折叠成统一通信错误 |

`create(config, &handle)` 要求 handle 初始为 NULL；复制配置，以 calloc/free 管理控制对象和 LVGL 对象。创建失败保持输出为空；非空输出被拒绝且原值不变。组件没有面板 reset/init 生命周期，创建前应用自行准备屏幕、总线和 tick。`delete(&handle)` 仅回收拥有的对象，成功清空 handle，空句柄也成功；NULL 句柄地址是参数错误。删除前停止并发访问，其他别名不会被自动清空。

板级拥有 HAL、总线、GPIO、背光、外部缓冲和回调上下文；组件不释放或重新配置这些资源。实例使用期间上下文必须有效，可用 NULL io 表示无上下文。应用串行调用，组件不默认线程安全，不在中断中调用阻塞操作，不增加日志/RTT/RTOS 依赖。同步传输返回前必须用完输入缓冲；共享总线在整笔事务外加锁，DMA/DCache 一致性由板级管理。

## LVGL 对象与错误状态

缓冲至少一行 RGB565，字节数不超过 UINT32_MAX，满足 LV_DRAW_BUF_ALIGN（未定义时至少 2 字节对齐）。LVGL 闭区间矩形由组件转换为半开区间一次。同步 draw 失败也调用 flush_ready 交还缓冲；输入错误、非法 pressed 或越界坐标上报松开，并由 get_status 返回最近错误，下一次成功更新为 STM_OK。

create 分配控制对象、display、可选 indev，任何失败完整回收；delete 先释放 indev、再 display、再控制对象。应用必须先调用 lv_init，并停止所有 LVGL/组件并发访问后删除。

get_display/get_indev 返回借用对象；无触摸时 indev 为 NULL。不得自行删除对象或替换 user_data，借用对象在 port 删除后失效。H757 板级在第一次渲染前通过 get_display 修改为 DIRECT 双缓冲、自定义 flush；其刷新错误独立记录，get_status 仅覆盖组件自身回调。组件不包含 DIRECT、DMA 或 VSYNC 自动配置。

## CMake 与依赖

依赖 `stm_common` 的 `stm_err.h`，不复制公共错误码。优先复用已有 `stm_common` target，其次找同级源码；缺失时自动下载固定 v1.0.0 提交 `ce3d186dde2d374a8e9c7b9068a7b88f97d57dc1`。可设置 `STM_COMMON_FETCH=OFF` 禁止下载，`STM_COMMON_GIT_REPOSITORY=https://gitee.com/nzxhg/stm_common.git` 指定镜像，或 `FETCHCONTENT_SOURCE_DIR_STM_COMMON` 指定离线源码。已有 target/同级源码无需网络。

LVGL 仅支持 **9.x**，依赖解析顺序如下：

1. 已有名为 `lvgl` 的 target：直接复用，不下载，也不改变其配置。
2. 设置 `STM_LVGL_PORT_LVGL_SOURCE_DIR`：使用该离线源码目录；不下载。
3. 设置 `FETCHCONTENT_SOURCE_DIR_LVGL`：通过 CMake FetchContent 使用离线源码；不下载。
4. 均未提供时：默认从官方仓库获取 **v9.3.0** 固定提交 `c033a98afddd65aaafeebea625382a94020fe4a7`，下载与构建内容留在构建目录，不追踪 `main` 或最新 tag。

| CMake 选项 | 默认值 | 用途 |
| --- | --- | --- |
| `STM_LVGL_PORT_FETCH_LVGL` | `ON` | 允许缺失时下载；`OFF` 不影响已有 target 或显式离线源码 |
| `STM_LVGL_PORT_LVGL_SOURCE_DIR` | 空 | 离线 LVGL 9 源码根目录（含 `CMakeLists.txt`） |
| `STM_LVGL_PORT_LVGL_GIT_REPOSITORY` | `https://github.com/lvgl/lvgl.git` | 官方仓库或含固定提交的可信镜像；不改变固定版本 |

自动获取/离线源码路径使用 LVGL 上游 CMake；应用必须在添加组件前提供自己的 `lv_conf.h`，可用 `LV_BUILD_CONF_PATH` 指定文件，或 `LV_BUILD_CONF_DIR` 指定其目录（二者不能同时设置）。未指定时，上游寻找应用顶层目录的 `lv_conf.h`；缺失会配置失败。组件不会生成或修改配置，也不会设置 tick、handler、HAL、显存或 DIRECT 模式。

这两条路径默认关闭 `CONFIG_LV_BUILD_DEMOS`、`CONFIG_LV_BUILD_EXAMPLES` 和 `CONFIG_LV_USE_THORVG_INTERNAL`，但尊重应用提前设置的值。使用 ThorVG 的应用须同时配置对应 LVGL 功能和 C++ 工具链；上游 CMake 本身要求 C/C++/ASM 工具链。已有 `lvgl` target 的选项和工具链仍由应用原接入方式管理。

```cmake
# 应用准备适合自身资源和功能的 lv_conf.h（不要直接启用整个模板）。
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Config/lv_conf.h" CACHE PATH "LVGL application config")
add_subdirectory(Lib/stm_lvgl_port)
target_link_libraries(your_firmware PRIVATE stm_lvgl_port)
```

离线示例（已有源码时可完全关闭下载）：

```cmake
set(STM_LVGL_PORT_FETCH_LVGL OFF CACHE BOOL "Disable LVGL download")
set(STM_LVGL_PORT_LVGL_SOURCE_DIR "/absolute/path/to/lvgl" CACHE PATH "Offline LVGL 9")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Config/lv_conf.h" CACHE PATH "LVGL application config")
add_subdirectory(Lib/stm_lvgl_port)
```

关闭 LVGL 下载且未提供 target/离线源码时，配置阶段明确报错。LVGL 与 `stm_common` 的下载开关独立；完全离线还需提供 `stm_common` target/同级源码/源码覆盖并按需关闭其下载。手动集成时添加组件 include/源码、`stm_common` 头文件目录及 LVGL 9 库和配置。

### 依赖与主机测试

```sh
cmake -S tests -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
python tests/test_dependency.py --lvgl-source /absolute/path/to/lvgl-9.3.0
python tests/test_dependency.py --fetch
```

CTest 覆盖现有回调/生命周期契约、C11/C++17 头文件和离线 CMake 依赖解析（不访问网络）。`--lvgl-source` 额外使用真实 LVGL 9 源码编译、链接并运行最小主机消费程序；`--fetch` 额外检查下载及实际固定提交，可用 `--repository` 指定可信镜像。独立运行 Python 脚本时默认从同级 `stm_common` 读取公共头文件；若依赖位于其他位置，传入 `--common-source /absolute/path/to/stm_common`。主机检查不能替代实板显示、触摸、时序或缓存验证。

## 从 v0.1.0 迁移

| 旧接口 | 当前接口 |
| --- | --- |
| `stm_lvgl_port_t` 公开结构体 | `lvgl_port_handle_t`，初始 NULL |
| `stm_lvgl_port_config_t` | `lvgl_port_config_t` |
| `attach(实例地址, config)` | `lvgl_port_create(config, &handle)` |
| 直接访问结构体 / 无销毁接口 | `get_display/get_indev/get_status，lvgl_port_delete(&handle)` |
| int 与负数错误码 | `stm_err_t`，`err != STM_OK`，回调同步迁移 |

旧 attach/detach 接口移除，不保留兼容包装。

2026-10-07，`v0.2.0` 接口在 STM32H757XIH6 CB V1.0、WKS101HD031-WCT 10.1 寸 800×1280 模组（ILI9881C/GT9271）、LVGL 9.3.0 上完成回归：诊断显示持续刷新、按钮与触摸输入、五次连续软件复位及 Release 启动通过；官方 Widgets 的滑动、点击由用户现场确认正常，读取状态中刷新、输入和切帧错误均为 0。板级使用 RGB565 DIRECT 双缓冲，触摸 mirror_x=0、mirror_y=0；结论限于该组合，不代表其他模组已验证。 硬件回归使用 get_display 获取的借用对象进行板级 DIRECT 扩展；组件默认同步 PARTIAL 路径本轮通过主机测试，未单独进行实板验收。

## 软件验证与发布状态

```sh
cmake -S tests -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

主机测试覆盖参数/配置、分配失败、资源回收、多实例和错误传递，并编译 C11/C++17 公共头文件。测试分配器仅用于测试构建，不加入产品固件。中文 HAL 示例见 [examples/stm32_hal/README.md](examples/stm32_hal/README.md)。许可证见 [LICENSE](LICENSE)。

`v0.2.0` 采用不透明句柄、`create/delete` 和统一 `stm_err_t`，包含破坏性接口迁移，不保留旧接口包装。`v0.1.0` 继续保留；升级前按上表迁移类型、回调和生命周期。此版本的主机测试、C11/C++17 头文件、中文 HAL 示例及 H757 Debug/Release 集成构建已通过。

## v0.2.1 文档补丁

补充同步 PARTIAL 与板级 DIRECT 双缓冲的接入指南，明确帧缓冲归属、DCache clean、VSYNC 切帧、flush_ready 时机和独立错误记录；中文 HAL 示例同步更新。源码、公开 API 和 CMake 与 v0.2.0 完全相同，无需再次迁移接口。

2026-10-07，H757 配套 ILI9881C/GT9271、LVGL 9.3.0 的板级呈现模块整理后完成独立回归：诊断 Debug 显示与触摸、连续五次复位、诊断 Release 启动及按钮计数、官方 Widgets Debug 滑动/点击正常，显示及输入错误均为 0。默认存储固件恢复后启动正常。本轮 PARTIAL 仅主机验证，未单独记录各角坐标、实测 FPS 基准或长期稳定性；结论限于该板级 RGB565 DIRECT 配置。

## v0.3.0 LVGL 固定版本自动获取

新增 CMake 依赖解析：已有 `lvgl` target 优先，其次使用显式离线源码，缺失时自动获取 LVGL v9.3.0 固定提交。下载开关、离线源码覆盖和可信镜像均可由应用配置；C API 与 v0.2.x 保持兼容。应用仍负责 `lv_conf.h`、tick、handler、HAL 和缓冲配置。

2026-10-08，通过主机契约与 C11/C++17 头文件检查、11 种隔离 CMake 依赖/失败路径检查，以及真实 LVGL v9.3.0 的离线源码、FetchContent 源码覆盖和在线固定提交获取后的编译、链接与最小实例创建/销毁测试；H757 HAL 示例和真实 LVGL 头文件/实现编译通过。本版未重新完成消费工程全固件链接或实板回归，v0.2.x 的硬件记录不作为 v0.3.0 验收结论。
