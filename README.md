# Langlern PDF Editor

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
