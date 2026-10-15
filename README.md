# Langlern PDF Editor

面向 Windows / macOS 的离线 PDF 阅读与语言练习工具。固定技术栈为 C++20、Qt Quick、PDFium、CMake。

当前已实现**阶段 5 Windows 文字答案与页面坐标系统**：空白单击输入中英答案，多行编辑、粘贴、移动和删除文字框，未选中透明无边框；缩放/窗口调整/旋转与裁剪样本保持坐标。保留原生文字拖选与复制、连续阅读、25%–400% 缩放和有预算缓存，编辑答案不重解码底图。**答案仅存在当前会话，关闭或换文件后丢失**；尚无自动保存、手写和 PDF 导出。实际测试与边界见 [阶段 5 报告](docs/PHASE5_REPORT.md)。macOS 按用户安排延至整个项目完成后检查，阶段 6–8 未实施。原型与字体导出等既有风险见 [阶段 0 报告](docs/PHASE0_REPORT.md)。

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
