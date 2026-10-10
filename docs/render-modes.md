# 渲染与缓冲所有权

当前统一接口只接入 RGB565 PARTIAL 软件渲染，单缓冲同步或外部双缓冲异步。170×560参考板保持两个16行普通SRAM缓冲，每个5440字节。

INVALIDATE_AREA 在渲染前扩展全宽、8行对齐；传输只发送已渲染区域，右下边界不包含。不在flush阶段改变区域或复制未渲染的像素。

异步链：flush提交→通用panel→通用IO→HAL DMA邮箱→主循环检查安全停止→IO完成→panel完成→port flush_ready。port等待时服务独立触摸IO，不递归handler。buffer被借用时禁止重用或删除；提交错误但硬件busy时不提前释放。

关闭诊断仅移除可选展示/统计，不关闭超时、abort、错误锁存或停止确认。不混入SPI提速、FULL/DIRECT渲染或多屏扩展。
