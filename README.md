# Langlern PDF Editor

面向 Windows / macOS 的离线 PDF 阅读与语言练习工具。固定技术栈为 C++20、Qt Quick、PDFium、CMake。

当前已实现**阶段 1 桌面应用外壳**：基础窗口、本地文件选择、浅色/深色/系统主题、键盘快捷键和启动诊断。选中文件会明确提示“尚未加载”；PDF 显示、连续阅读和标注尚未实现。Windows 构建与自动化验证见 [阶段 1 报告](docs/PHASE1_REPORT.md)。macOS 按用户要求延至整个项目完成后检查，阶段 2–8 尚未实施。原型与既有风险见 [阶段 0 报告](docs/PHASE0_REPORT.md)。

已有本项目固定 Qt/MSVC 开发依赖时：

```powershell
./tools/build-windows.ps1 -Test
./tools/run-windows.ps1
```

默认构建正式应用，不要求 PDFium。首次依赖获取、保留原型的构建选项及验证限制见编译说明。当前使用开发环境的 Qt 运行库；尚未提供可脱离 Qt SDK 双击运行的发行包。

- [编译与实验复现](docs/BUILDING.md)
- [架构](docs/ARCHITECTURE.md)、[路线图](docs/ROADMAP.md)、[性能方法](docs/PERFORMANCE.md)
- [依赖和许可审查](third_party/DEPENDENCIES.md)
- [贡献说明](CONTRIBUTING.md)

项目自有代码采用 MIT；第三方代码、字体和项目 skills 保留各自许可。此声明不表示整个发行包已获合规确认。原型的诊断输出必须使用新的运行目录；导出拒绝已有目标，原始 PDF 不应被修改。
