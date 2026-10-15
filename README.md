# Langlern PDF Editor

面向 Windows / macOS 的离线 PDF 阅读与语言练习工具。固定技术栈为 C++20、Qt Quick、PDFium、CMake。

当前已实现**阶段 7 Windows 保存恢复与导出**：文字答案与笔迹异步自动保存，正常关闭与异常结束后可恢复最后成功落盘修订；内容散列验证原 PDF 身份，移动文件后仍可恢复。支持备份、损坏/未来版本保护与工作区冲突提示。“导出 PDF…”生成包含可提取中英文字与矢量笔迹的新文件，拒绝覆盖原文件及已有目标。原有阅读、标注和撤销功能保留。实际结果与边界见 [阶段 7 报告](docs/PHASE7_REPORT.md) 和 [保存格式](docs/STORAGE_FORMAT.md)。macOS 按用户安排延至整个项目完成后检查；阶段 8 尚未实施。

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

项目自有代码采用 MIT；第三方代码、字体和项目 skills 保留各自许可。此声明不表示整个发行包已获合规确认。答案字体为随附 OFL Noto Sans SC，不支持的字符会使导出明确失败。诊断输出必须使用新的运行目录；导出拒绝已有目标，原始 PDF 不应被修改。
