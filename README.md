# Langlern PDF Editor

面向 Windows / macOS 的离线 PDF 阅读与语言练习工具。固定技术栈为 C++20、Qt Quick、PDFium、CMake。

当前只有**阶段 0 技术验证原型**，没有正式阅读器、连续滚动或标注产品。Windows 本地构建、真实 PDF 和微软拼音输入验证已执行；macOS/Metal 留待用户实机检查，查看器完整验收与发行许可闭环尚未完成。见 [阶段报告](docs/PHASE0_REPORT.md)。阶段 1–8 未实施。

- [编译与实验复现](docs/BUILDING.md)
- [架构](docs/ARCHITECTURE.md)、[路线图](docs/ROADMAP.md)、[性能方法](docs/PERFORMANCE.md)
- [依赖和许可审查](third_party/DEPENDENCIES.md)
- [贡献说明](CONTRIBUTING.md)

项目自有代码采用 MIT；第三方代码、字体和项目 skills 保留各自许可。此声明不表示整个发行包已获合规确认。原型的诊断输出必须使用新的运行目录；导出拒绝已有目标，原始 PDF 不应被修改。
