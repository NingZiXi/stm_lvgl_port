# stm_lvgl_port：Agent 工作指南

## 范围与阅读顺序

本文件约束本组件目录及子目录；与上级指令合并使用，存在更深层 AGENTS.md 时遵循其局部约束。README 面向使用者，本文件用于 Agent 的接入、修改和验证。

先检查消费工程构建方式、已存在的 target、外设初始化及用户未提交改动，再读本组件 README/CMake/公开头；不覆盖已有业务代码。

## 接入与不可破坏的约束

- 先读 `include/stm_lvgl_port.h`、`docs/render-modes.md`、`examples/stm32_hal/example.c`，再读 `src/` 与框架 panel/touch/IO 接口。
- 创建配置接收通用 IO/panel/可选 touch；不要恢复旧 draw/wait/touch 包装或在应用建立完成链。
- LVGL target 复用优先；离线源码优先于固定 9.3.0 拉取 `c033a98afddd65aaafeebea625382a94020fe4a7`。stm_lcd 只用已有 target/同级源码，不由 port 下载。
- 应用提供 lv_conf；不强制覆盖应用 CMake 选项，不改供应商 LVGL。当前 API 是破坏性迁移，不按旧 v0.3.0 兼容接口接入。
- create 自动初始化 LVGL/tick、订阅刷新及借用输入；应用只创建 UI 和调用 process，不再调用 lv_tick_inc/handler。
- 默认 RGB565 PARTIAL，外部对齐缓冲至少一行；显式 DIRECT 要求两块完整紧密 RGB565 帧与真实 VSYNC/停止能力。不要在应用替换 flush。细则集中在 docs/render-modes.md；不自动分配大帧缓冲。
- 全宽/8行扩展发生在渲染前，不在 flush 中扩大区域发送未渲染像素。IO停止 → panel完成 → LVGL ready 保持一次语义。
- process 主循环串行，错误后仍服务在途 IO；DMA等待服务独立I2C，但不递归LVGL。ISR/观察回调不能操作 LVGL、删除或递归process。
- 输入默认20ms采样、200ms过期释放、1000ms离线探测；IO/TIMEOUT离线，VERIFY释放但不直接离线，不自动共享复位。
- 每面板一个port；借用设备/缓冲；无在途且 DIRECT 停止扫描成功才删除并解除订阅。禁止对象存活时替换全局tick，不新增多屏/RTOS/队列。
- 主机测试需同级 stm_common/stm_lcd，mock 不替代真实 LVGL；消费工程必须额外验证真实渲染、异步和输入安全。

## 编码与注释

- 中文交流；C11；Allman 括号、4 空格缩进，按本目录 `.clang-format` 格式化，只处理自有代码。
- `.c` / `.h` 文件头仅有 `@file` 和一句 `@brief`，不写 author/date/version/copyright。
- 头文件结构体外的公开函数声明使用 Doxygen：brief、真实参数语义、关键返回值；void 不虚构返回值。
- struct/enum 可在上方写一句说明；成员和枚举项仅在同行右侧用简短 `//`，不使用 `/**< */`，不把字段注释写在上方。
- C 实现不写函数 Doxygen；函数前至多一句 `//`。复杂约束最多 1–3 行，不写教学块、历史或章节分隔线。
- 不改维护者 MIT LICENSE；保留现有第三方来源/许可说明，不把第三方内容直接改署名。

## 验证与交付

从本组件根目录执行，需要主机 C11/C++17 编译器、CMake 3.22+、Ninja：

```sh
cmake -S tests -B build/tests -G Ninja
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

没有同级依赖时先准备源码，不能删测试或伪造空 target 来通过。主机测试不用 MCU 交叉编译器。测试包含 C/C++ 头文件消费；改生命周期/协议时增加对应失败路径测试。

接入消费工程还须使用其工具链编译固件，并运行可用的真实 LVGL / HAL 回归。仅文档注释整理时校验非注释 C token 不变。报告实际依赖版本/提交、修改文件、执行命令、测试/编译结果及未验证项；编译成功不等于实板通过。

未经用户明确授权不烧录、擦写存储、推送、发布版本、修改远程仓库或自动升级依赖。缺少接线、共享复位、目标器件或构建配置时先询问，不猜测。不要修改消费者的 CubeMX 生成文件、HAL 或供应商 LVGL；必要配置列出操作步骤。

## 依赖与真实 LVGL 回归

```sh
python tests/test_dependency.py --lcd-source /absolute/path/to/stm_lcd --common-source /absolute/path/to/stm_common --lvgl-source /absolute/path/to/lvgl
```

需要 Python 3；无真实源码时可省略 --lvgl-source 仅检查 mock/解析并明确未验证真实渲染。默认不访问网络；--fetch 是显式网络测试，固定版本不可漂移。--build-dir 可指定新目录留存日志。

当前提交的扩展要求匹配的 stm_lcd 源码（STM_LCD_FRAMEBUFFER_API=1）；已发布 v1.0.0 不够。除 mock/依赖检查外，使用 `tests/real_lvgl` 的本地 LVGL 9.3.0 运行 PARTIAL/DIRECT 渲染回归；软件模型不等于 MCU 实板。
