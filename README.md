# LinguaPDF

免费、开源、完全离线的 PDF 阅读与语言练习工具，面向数百 MB、上千页的扫描教材。可以直接在页面上填写答案、手写标记，并查词、朗读、OCR，最后导出带答案的新 PDF。

**[下载 Windows 版](https://github.com/daodreamer/pdf_editor_for_learning_language/releases/latest)** · [更新日志](CHANGELOG.md) · [第三方许可](THIRD-PARTY-NOTICES.md)

当前版本 1.0.0，仅提供 Windows x64 版，macOS 版计划中。

## 功能

- **流畅阅读大文件**：只渲染可见页面，带有界缓存；支持连续滚动、缩放、双页布局、缩略图和书签；浅色、纸张色、深色主题；高 DPI。
- **在页面上作答**：单击任意位置输入；悬停在教材的横线或虚线上可以直接在线上作答；文字框透明无边框，可以移动、调整大小、改格式。
- **手写**：多色画笔（带平滑和笔锋）、橡皮擦、统一撤销和重做。
- **语言工具**：复制原文；离线 OCR（法、英、简中）；英汉和法语离线词典，法语支持变形还原和常用时态变位表；法语自然朗读（男声/女声，变速不变调）。
- **可靠保存**：本地自动保存，可从异常退出中恢复；导出新 PDF，从不修改原文件。
- **完全离线**：不需要账号、云服务或网络连接。

## 使用

下载 Release 中的 `LinguaPDF-<版本>-windows-x64.zip`，解压后运行 `LinguaPDF.exe`。程序没有代码签名，首次运行如果出现 SmartScreen 提示，点“更多信息 → 仍要运行”。详细操作、数据位置、更新和卸载方法见 [Windows 使用说明](docs/WINDOWS_PACKAGE.md)。

常用快捷键：A/P/T/C/R/E 依次切换自动、书写、打字、批改、点读、橡皮；Ctrl+Z / Ctrl+Y 撤销 / 重做；F1 查看全部快捷键。

## 从源码构建（Windows）

技术栈：C++20、Qt 6.8.3（Qt Quick，Direct3D 11）、PDFium、CMake + Ninja。需要 Visual Studio 2022 或 2026 的 C++ 工具链，以及 Python 3.13。

```powershell
python -m venv .tools/venv
.tools/venv/Scripts/python -m pip install -r tools/requirements-phase0.lock.txt
.tools/venv/Scripts/python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O .deps/Qt --archives qtbase qtdeclarative qttools
.tools/venv/Scripts/python tools/bootstrap.py --platform windows --pdfium-only
.tools/venv/Scripts/python tools/bootstrap-dictionary.py
.tools/venv/Scripts/python tools/bootstrap-french-dictionary.py
./tools/build-windows.ps1 -Test -Lint
./tools/run-windows.ps1
```

依赖下载都有固定版本和散列校验；程序运行时完全离线。打包和发布流程见 [发布流程](docs/RELEASING.md)，更多构建细节见 [编译说明](docs/BUILDING.md)。

## 文档

- [架构](docs/ARCHITECTURE.md) · [路线图](docs/ROADMAP.md) · [性能测试方法](docs/PERFORMANCE.md)
- [保存格式](docs/STORAGE_FORMAT.md) · [OCR 与选文](docs/OCR_SELECTION.md) · [自然法语朗读](docs/NATURAL_SPEECH.md)
- [依赖审查](third_party/DEPENDENCIES.md) · [1.0.0 发行许可终审](third_party/DISTRIBUTION.md)
- [贡献指南](CONTRIBUTING.md)

## 许可

LinguaPDF 源码采用 [MIT](LICENSE) 许可。发行包随附的第三方组件保留各自的许可，详见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。其中法语语音模型 Supertonic 3 采用 OpenRAIL-M 许可，带有使用限制；法语词典数据采用 CC BY-SA 4.0。
