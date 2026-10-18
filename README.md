# Langlern PDF Editor

阅读时可点底部 **双页** 或“··· → 横向双页布局”，并排查看1–2、3–4等相邻页面；上一/下一按钮按组翻页，页码输入和缩略图仍可定位指定页。奇数末页居中，右页支持选择原文、输入和手写。切回单页恢复此前缩放。

当前界面按用户提供的 **LinguaPDF 学习原型**迭代：书架、阅读、生词本、导入四页；页面/书签侧栏，六种工具，顶部/悬浮/侧边工具栏，浅色/纸张色/深色和中英文界面。真实功能与设计稿的差异见 [设计适配记录](docs/DESIGN_ALIGNMENT.md)。

自动模式下空白单击输入、空白拖动书写、原文点击查词；滚轮阅读。A/P/T/C/R/E 切换自动/书写/打字/批改/点读/橡皮（输入框有焦点时不抢占输入）。按住鼠标右键临时擦除笔迹，松开恢复原模式。新文本框约为 “this” 的宽度，随输入向右扩展；到达页边界或按 Enter / Shift+Enter 换行后固定宽度。Ctrl+Enter 完成答案，Tab 在本页已有文字答案间切换。Alt 单击文字答案批改；批改为手动判断。所有标注继续自动保存并可导出新 PDF。

打开或拖入 PDF 默认直接阅读原版，不自动进行优化转换。拍摄或扫描件需要处理时，从阅读器右上角“··· → 扫描优化…”进入预览，再手动另存为新 PDF。扫描优化逐页生成新的图像 PDF（最高 150 DPI），不会保留原文字层或带入练习标注；保留原始文件和原练习数据。

Windows 新增本机 OCR、SAPI 朗读和本地音频播放；OCR 使用已安装的系统语言包，当前按页识别；英汉查词使用 ECDICT，缺少词条可手填释义。macOS 新服务尚未实现，既有跨平台核心保留。

完成输入后，将鼠标移到文字框上会渐显范围和四角手柄；拖动角点调整大小与相邻边的位置，拖动上方“移动”柄整体移动。手动调整后的宽度固定，高度至少容纳文字内容；位置与尺寸自动保存，可撤销重做。

选中文字答案中的一段文字时，选区上方会显示苹果风圆角格式浮窗，空间不足时移到下方。可调整六种颜色、8–72 字号、加粗和斜体，仅改变选中文字；支持编辑器撤销重做、本地保存恢复和 PDF 导出。粗体与斜体由随附字体合成，不引入系统字体依赖。

准备可选离线词库（仅此开发命令需要联网；应用运行完全离线）：

```powershell
.tools/venv/Scripts/python tools/bootstrap-dictionary.py
./tools/build-windows.ps1 -Test -Lint
./tools/run-windows.ps1
```

词库约 80 MB，按固定源数据转换为只读 SQLite 索引，不在启动时全量载入内存。未安装时查词会说明缺少词库，不阻断阅读。

面向 Windows / macOS 的离线 PDF 阅读与语言练习工具。固定技术栈为 C++20、Qt Quick、PDFium、CMake。

当前已实现阶段 0–7 的 Windows 阅读、原生文字选择、文字答案、自由笔迹、撤销重做、本地自动保存/恢复和新 PDF 导出。本轮阶段 8 提供轻量线性图标、紧凑与自适应工具栏、完整快捷键说明、延后创建阅读控件、保存/导出独立状态，以及可复现的 Windows 便携候选包流程。实际验收数据与已知限制见 [阶段 8 报告](docs/PHASE8_REPORT.md)。macOS 按用户安排在项目完成后运行检查。

使用工具栏切换模式；`Ctrl+Shift+P` 画笔、`Ctrl+Shift+E` 整笔擦除、`Ctrl+Shift+M` 选择/移动笔迹，`Ctrl+Shift+A/S/T` 自动/原文选择/文字输入。选择笔迹后按 Delete 删除，Ctrl+Z/Y 撤销/重做（编辑文字时作用于编辑器局部历史）。macOS 对应 Command 尚待实机验证。

已有本项目固定 Qt/MSVC 开发依赖时：

```powershell
./tools/build-windows.ps1 -Test
./tools/run-windows.ps1
```

默认构建正式应用，需要固定版本 PDFium。首次依赖获取、保留原型的构建选项及验证限制见编译说明。开发启动脚本使用 SDK 运行库。创建无需 Qt SDK 的便携候选包：

```powershell
./tools/package-windows.ps1 -OutputDir build/packages/windows-x64
.tools/venv/Scripts/python -X utf8 tools/validate-windows-package.py --package build/packages/windows-x64 --out benchmarks/results/my-package-check
```

输出目录必须不存在；不要覆盖旧证据。使用、更新和数据保留说明见 [Windows 候选包说明](docs/WINDOWS_PACKAGE.md)，许可状态见 [分发说明](third_party/DISTRIBUTION.md)。

- [编译与实验复现](docs/BUILDING.md)
- [架构](docs/ARCHITECTURE.md)、[路线图](docs/ROADMAP.md)、[性能方法](docs/PERFORMANCE.md)
- [依赖和许可审查](third_party/DEPENDENCIES.md)
- [贡献说明](CONTRIBUTING.md)

项目自有代码采用 MIT；第三方代码、字体和项目 skills 保留各自许可。此声明不表示整个发行包已获合规确认。答案字体为随附 OFL Noto Sans SC，不支持的字符会使导出明确失败。诊断输出必须使用新的运行目录；导出拒绝已有目标，原始 PDF 不应被修改。
