# 阶段 6 Windows 交付报告

日期：2026-10-15。用户授权以 goal 模式完成阶段 6；macOS 按用户要求在整个项目完成后实机检查。本次未实现阶段 7/8。

## 完成内容

- 多色自由画笔（蓝/红/绿/黑）、1–16 原始页面单位粗细、点按圆点，鼠标与 Qt tablet 事件共用数据模型；压力非 MVP 前提。
- 整条笔迹橡皮擦：8 逻辑像素半径随缩放换算，连续扫过线段命中，一次手势一次撤销。取消恢复全部被暂时隐藏的笔迹。
- 笔迹选择、拖动移动、Delete/按钮删除；覆盖和命中按创建顺序，缩放及虚拟化后保持一致。文字与笔迹共用模型历史，保留文字编辑器局部撤销的边界。
- PDF 原始点列、UUID、创建序号、线宽及颜色的纯值快照；未提交绘制/移动不进入快照，擦除在途快照保持原状态。当前会话关闭/换文档清空，不代表已持久化。
- 最多 256 条、16 MiB 容量计费的历史；新命令清空 redo，淘汰仅释放历史。计费包含字符串/点列/列表预留容量，隐式共享保守重复计费；不含分配器头、临时副本、当前标注、TextEdit 局部历史或进程总内存。单笔超过 65536 点取消并提示。
- 活跃笔迹通过独立 Scene Graph 层分为 128 点块，增量替换末块，完成后复用几何。绘制、擦除和移动不调用 PDFium，不提交底图渲染。滚动/缩放/工具/样式切换、Escape、窗口失焦和抓取丢失取消在途手势，越出原页/视口同样取消。

快捷键：Ctrl+Shift+P/E/M 为画笔/整笔擦除/移动笔迹，Ctrl+Shift+A/S/T 为自动/选择原文/文字答案；Ctrl+Z/Y 为撤销/重做，选中笔迹后 Delete 删除。macOS 的 Command 映射保留，未实机验收。

未增加产品第三方依赖。沿用 C++20、Qt 6.8.3、PDFium 157.0.8100.0、MSVC x64 Release；Qt RHI D3D11、threaded render loop 与 RTX 5070 的实际日志在 visual-tests.log 和 final-long-stroke/ink-1.log。不将本次构建解释为整个发行包许可合规，原字体/打包风险仍见阶段 0。

## 验证与证据

原始证据留在本地 `benchmarks/results/phase6-20261015/`，按仓库规则不默认提交。代码与二进制/样本散列见 final-long-stroke/manifest.json。

| 检查 | 证据及结果 |
| --- | --- |
| 构建 | `./tools/build-windows.ps1 -BuildDir build/windows -Test`，final-build-ctest.log；最终测试轨迹调整后重新编译见 bounded-trace-fixed-build.log，配置/编译/链接成功 |
| 完整回归 | 最终 `ctest --test-dir build/windows --output-on-failure`，final-ctest-bounded.log；6/6 通过（29.73 s），含本地 839214902 bytes / 800 页 synthetic-800.pdf，详情见该日志 |
| 笔迹详细套件 | final-bounded-ink-tests.log：9 passed / 0 failed / 0 skipped，含初始化/清理与 7 个测试方法 |
| 统一历史 | fiftyCompleteMixedRoundTrips 对九个文字/笔迹创建、编辑、移动、删除、擦除命令做 50 次完整 undo/redo；每步比对文本和完整笔迹值，不只检查数量；另验 redo 分支失效、数量/字节淘汰与当前模型保留 |
| 坐标与输入 | 8 页真实 PDF 的 0/90/180/270° × UserUnit 1/2、CropBox 样本，点列投影误差 <1e-6 归一显示坐标；真实窗口鼠标、注入 tablet、快捷键、失焦/Escape/工具/滚动/越界取消、缩放擦除；renderJobs 不增加 |
| 像素与重建 | visual-tests.log、visual/ink-colors.png；蓝/红实际像素、粗细数据、交叉覆盖在缩放/虚拟化/选择/undo 后保持，截图已目视复核 |
| 高 DPI | final-dpi2-ink-tests.log：9 passed / 0 failed / 0 skipped，QT_SCALE_FACTOR=2，实际 DPR=3，逻辑窗口 1120×710；visual-dpi2-final/ 截图已目视复核。非真实跨屏迁移 |
| QML | `cmake --build build/windows --target all_qmllint`，final-qmllint.log，退出 0，无 QML 诊断 |
| 大文件 | large-file.log，既有 largeFileSmoke 3 passed / 0 failed / 0 skipped；最终 CTest 同样设置该样本。没有重新执行大型真实压缩教材或 30 分钟压力 |
| 审查 | requesting-code-review 技能独立只读审查发现三项 P2，已修复并用同页滚动取消、capacity 计费、交叉像素重建回归覆盖。独立复核调用因额度失败，未记为通过；主代理自行检查修复后的实现 |

开发中保留首次 QMap 反向迭代编译错误、早期测试轨迹越出视口、Windows min 宏冲突等失败日志；修正为兼容接口、实际可见边界轨迹和带括号 std::min。高 DPI 首轮的长轨迹因系统缩短逻辑窗口而越界，dpi2-ink-tests.log 不计通过，最终结果只看 final-dpi2-ink-tests.log。产品取消逻辑没有为让测试通过而放宽。

## 长笔迹测量

复现：`.tools/venv/Scripts/python benchmarks/run-ink.py --run benchmarks/results/<新的运行目录> --repeat 3`。三个独立进程，每次注入 8192 次 Qt mouse move，64 个事件一批并 qWait(8)，约 2.04 s，最终储存 7679 点；坐标按实际可见区域限制。输入为已有原创 scan.pdf（1337321 bytes，1 页，无文字层）；不代表手持设备的真实采样频率。

环境：Windows 11 Pro 10.0.26200、Intel Core Ultra 7 265K、约 47.4 GiB RAM、RTX 5070，显示 vsync 日志为 6.25 ms；Release、DPR=1.5。OS/CPU/GPU 详情在 os.json/cpu.json/gpu.json，电源模式未在本轮重新采集。独立进程、OS 文件缓存自然已热；未做冷启动或 A/B 比较。

| 指标 | 三轮范围 |
| --- | --- |
| GUI 事件处理 P95 | 0.002–0.002 ms |
| GUI 事件处理 P99 | 0.042–0.044 ms |
| frameSwapped 间隔 P95 | 32.486–33.372 ms |
| frameSwapped 间隔 P99 | 33.103–39.334 ms |
| 结束时 working set | 207.656–242.906 MiB |
| 结束时 private bytes | 231.695–265.516 MiB |
| OS 进程峰值 working set | 235.570–242.906 MiB |
| 最终受管历史容量 | 131600 bytes |

每次输入的单调时钟与 handler_ms、逐帧间隔存 final-long-stroke/ink-*.json。事件处理计时只包围 sendEvent 的 GUI 处理，不含几何同步/绘制；frameSwapped 在渲染线程按 steady_clock 记录，包含测试输入突发和等待节奏，并非系统 scanout 或硬件输入到屏幕延迟。这些 P95/P99 不能解释为日常书写稳定 60 FPS；帧间隔仍需阶段 8 按真实输入轨迹优化和验收。working set/private 为结束样本，peak working set 为该进程生命周期 OS 高水位；GPU 系统显存、私有内存峰值均未采集，不能当作零。

## 限制及下一步

物理触控笔、触控板、实际跨屏 DPR 和系统 IME 人工检查仍待实机；事件注入不替代设备验收。macOS/Metal、远程 CI、ASan/UBSan 未执行。本轮沿用构建中的可选 Vulkan headers 提示及 Qt 生成代码 C4702 警告，不称零警告。

画笔为不透明固定粗细圆端，不提供压力/半透明；整笔擦除不拆分线段。移动以笔迹中心限制在页面，边缘半径由页面裁剪。当前标注总量未设持久化上限；极密笔迹几何/大量擦除的长期性能仍需阶段 8 压力数据。阶段 3 启动未达 300 ms、长期滚动与真实压缩教材待办保留。本阶段没有重测启动或宣称整体性能达标。

本地自动保存、恢复、中文字体与 PDF 笔迹实际导出仍在阶段 7；关闭当前会话会丢失所有答案及笔迹。下一步建议授权阶段 7，本次未自动推进。
