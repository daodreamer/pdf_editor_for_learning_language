# Langlern PDF Editor

面向 Windows / macOS 的离线 PDF 阅读与语言练习工具。固定技术栈为 C++20、Qt Quick、PDFium、CMake。

当前已实现**阶段 2 Windows 单页 PDF 阅读**：打开、切换和关闭本地 PDF，预览后替换清晰图像，基础翻页、页码输入、适应页宽、浅色/深色主题。PDFium 在唯一专用线程运行，切换文档会拒绝旧结果。构建与真实验证见 [阶段 2 报告](docs/PHASE2_REPORT.md)。macOS 按用户要求延至整个项目完成后检查；阶段 3–8 尚未实施，连续多页阅读、文字选择、标注和保存导出留后续阶段。原型与既有风险见 [阶段 0 报告](docs/PHASE0_REPORT.md)。

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
