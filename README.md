# Langlern PDF Editor

面向 Windows / macOS 的离线 PDF 阅读与语言练习工具。固定技术栈为 C++20、Qt Quick、PDFium、CMake。

当前已实现**阶段 6 Windows 基础标注**：多色自由画笔、粗细调节、整笔橡皮擦、笔迹选择/移动/删除，与文字答案共用撤销/重做。保留中英文字框、原生文字拖选与复制、连续阅读、25%–400% 缩放和有预算缓存；标注操作不重解码底图。**文字和笔迹仅存在当前会话，关闭或换文件后丢失**；自动保存和 PDF 导出留在阶段 7。实际测试与边界见 [阶段 6 报告](docs/PHASE6_REPORT.md)。macOS 按用户安排延至整个项目完成后检查，阶段 7–8 未实施。字体导出等既有风险见 [阶段 0 报告](docs/PHASE0_REPORT.md)。

使用工具栏切换模式；`Ctrl+Shift+P` 画笔、`Ctrl+Shift+E` 整笔擦除、`Ctrl+Shift+M` 选择/移动笔迹，`Ctrl+Shift+A/S/T` 自动/原文选择/文字输入。选择笔迹后按 Delete 删除，Ctrl+Z/Y 撤销/重做（编辑文字时作用于编辑器局部历史）。macOS 对应 Command 尚待实机验证。

已有本项目固定 Qt/MSVC 开发依赖时：

```powershell
./tools/build-windows.ps1 -Test
./tools/run-windows.ps1
```

默认构建正式应用，需要固定版本 PDFium。首次依赖获取、保留原型的构建选项及验证限制见编译说明。当前使用开发环境的 Qt 运行库；尚未提供可脱离 Qt SDK 双击运行的发行包。

- [编译与实验复现](docs/BUILDING.md)
- [架构](docs/ARCHITECTURE.md)、[路线图](docs/ROADMAP.md)、[性能方法](docs/PERFORMANCE.md)
- [依赖和许可审查](third_party/DEPENDENCIES.md)
- [贡献说明](CONTRIBUTING.md)

项目自有代码采用 MIT；第三方代码、字体和项目 skills 保留各自许可。此声明不表示整个发行包已获合规确认。原型的诊断输出必须使用新的运行目录；导出拒绝已有目标，原始 PDF 不应被修改。
