# 阶段 5 Windows 验证报告

日期：2026-10-15。用户授权以 goal 模式完成阶段 5；macOS 构建和实机验证统一延至整个项目完成后。本次未进入阶段 6–8。

## 完成内容

文字答案现已接入正式应用：空白单击建框并直接输入，编辑中英文/多行/粘贴，选择已有答案，拖动移动柄，删除整框；未选中透明无边框。自动模式保留原生文字拖选，输入模式可在原文处建框，选择模式禁止空白创建。扫描页同样可以填写答案，不生成 OCR 文字。界面明确显示答案只在本次会话中，关闭或换文件会丢失。

新增 GUI 线程 AnnotationModel、UUID 稳定标识、原始 PDF 锚点/局部方向/版式数据及纯值 snapshot 接口。PDFium worker 用 DeviceToPage 获取有效裁剪/固有旋转后的双精度基底，GUI 仅做坐标映射，不调用 PDFium。原始页面单位与物理 points 区分，UserUnit 不静默归一。页面缩放和窗口调整只改变显示投影，文本布局仍使用原始单位的固定宽度，标注编辑不触发底图重解码。

IME preedit 留在 TextEdit，快照仅接收已提交字符。TextEdit 内局部撤销/重做独立，离开编辑提交一次模型事务；模型历史支持创建、编辑、移动和删除，最多 256 条 before/after 命令。空框完成编辑后移除。只有活跃页面创建编辑器，滚动/缩放/元信息修正引起活跃范围变化时同步更新；编辑页仍活跃时保留编辑，退出前请求提交 IME。文档会话重置会清空标注与历史。

未增加产品第三方依赖。沿用 Qt 6.8.3、PDFium 157.0.8100.0、C++20、MSVC x64 Release、Qt RHI D3D11。字体使用系统字体及 Qt 回退，没有分发新字体；具体字形和 PDF 嵌入输出仍待阶段 7 验证，不能据本轮显示宣布整个发行包许可合规。

## 实际验证

原始证据根目录：`benchmarks/results/phase5-20261015/`（按仓库规则留在本地，不默认提交原始运行数据）。

| 检查 | 执行及结果 |
| --- | --- |
| 独立目录 | `./tools/build-windows.ps1 -BuildDir build/phase5 -Test` 构建/链接及 CTest 5/5 通过，accepted-build-ctest.log；这是焦点扩展回归前的中间版本，最终版本以默认构建结果为准 |
| 默认构建 | `./tools/build-windows.ps1 -BuildDir build/windows -Test`，最终结果见 final-default-build-ctest.log；配置/编译/链接成功，CTest 5/5 通过，19.60 s |
| 标注详细套件 | final-pixel-annotations.log，6 passed / 0 failed / 0 skipped（含初始化/清理、4 个测试方法），测试模型事务、真实页面坐标、活跃页焦点、真实 QML 鼠标/键盘/IME 路径 |
| QML | `cmake --build build/windows --target all_qmllint`，final-qmllint.log，退出 0、无 QML 诊断；C++ commit/resetInput 接口避免 Qt.inputMethod 的 QObject 静态类型诊断 |
| 高 DPI | final-pixel-dpi2.log，6 passed / 0 failed / 0 skipped，QT_SCALE_FACTOR=2，实际 DPR=3；窗口逻辑尺寸记录在日志中，visual-dpi2/text-answers.png 已目视复核 |
| 视觉与后端 | visual/text-answers.png 普通 DPR=1.5、visual-dpi2/ 高 DPI 均复核中英/组合字符/emoji/多行及无背景无边框（截图来自中间 UI 构建，最终功能以上述日志为准）；accepted-annotations.log 记录 threaded render loop、QRhi D3D11、RTX 5070 |
| 大文件 | 默认最终 CTest 设置 LANGLERN_LARGE_PDF 为现有 839214902 bytes/800 页 synthetic-800.pdf，执行既有 largeFileSmoke；没有重新执行 30 分钟压力 |
| 独立审查 | 使用 requesting-code-review 技能进行只读审查；修复活跃页滚动失焦、移动后 Ctrl+Z 路由、自然失焦抢回焦点，最终复核没有剩余已确认 P1/P2；审查没有独立运行 GUI |

坐标测试使用真实 text.pdf 的 8 页：0/90/180/270° × UserUnit 1/2，MediaBox [-20 -30 600 820]、CropBox [10 20 580 800]。独立检查各方向的原始角点（误差 <0.001 原始单位），仿射矩阵往返 <1e-6 原始单位；同一原始锚点缩放/窗口调整后，实际 GUI 投影相对数据中原始锚点的期望值误差 ≤1 设备像素（排除最初鼠标点整数舍入）。这不是另一次全视口像素差验证；原生高亮的像素差方法保留在阶段 4。

GUI 测试覆盖真实鼠标空白建框、创建后键入、原生文字优先/三模式、页内空白拖动滚动、移动柄拖动后 Ctrl+Z、Ctrl+Delete/撤销恢复、页码输入和两个标注间焦点切换、虚拟化重建、扫描页填写、换文件清空。Qt 输入法事件覆盖 preedit、中文 commit、取消、局部 Ctrl+Z/Y；粘贴覆盖多行、组合字符和 emoji。编辑前后 renderJobs 相同；编辑器在焦点内不从模型重新赋值正文，避免破坏局部 undo/IME。

## 修复与限制

开发过程中实际发现并修复：缩放后文字框宽度依赖通知不足；全视口 MouseArea 接管空白拖动；TapHandler 将内容偏移重复加到创建坐标；模型主动完成编辑后焦点悬空或没有回到全局快捷键层；自然失焦又不应抢走页码/另一编辑器焦点；动态几何改变时标注活跃范围需同步。像素断言收紧后只重编标注测试并运行普通/高 DPI 套件，产品代码相对完整 CTest 未再变更。相应失败日志与中间版本仍保留，未当作最终通过结果。

环境详见 environment.json：Windows 11 Pro 10.0.26200，Core Ultra 7 265K，约 47.4 GiB RAM，RTX 5070，平衡电源。Qt 生成缓存/Qt 头可能产生 C4702 警告，配置仍提示缺少可选 WrapVulkanHeaders；不把成功构建称为零警告。一次直接从普通 shell 构建 lint 因未初始化 MSVC INCLUDE 而失败，随后使用 DevShell 修正；保留 qmllint-first.log，不归因于产品代码。ASan/UBSan、远程 CI、macOS 均未执行。

Windows 系统拼音候选窗、物理触控板、真实跨屏 DPR 迁移尚未人工操作；本轮 Qt 事件注入不能代替该实机验收。高 DPI 是独立进程启动配置，不宣称跨屏通过。页面支持 PDF 固有旋转，没有实现用户新增页面旋转。超出页面底部的文字按页面边界裁剪，文字仍留在数据中；尚无框宽调整或字体样式工具栏。系统字体排版不承诺跨设备逐字形一致。

本轮只验证标注正确性与“不重新渲染底图”的行为，没有重新测量启动、滚动 P95/P99 或进程/GPU 峰值；阶段 3 的热启动 <300 ms 未达及真实大型压缩教材/长期资源待办保留。256 条命令是历史数量上限，不是用户文字或进程总内存上限。

答案仍仅在内存中；本地自动保存、异常恢复和导出新 PDF 留在阶段 7。阶段 0 的中文字体实际导出等未解决风险继续保留。下一步建议阶段 6 自由画笔、橡皮擦与统一历史，需用户进一步授权。
