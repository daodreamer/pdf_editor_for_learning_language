# 阶段 0 构建与复现

## 本机已执行的 Windows 路径

Windows 11 x64，Visual Studio Community 2026 / MSVC 19.51.36257.0 / SDK 10.0.26100.0；Qt 6.8.3 官方 MSVC2022 动态库在本工具链上实际编译链接成功。这不是对所有工具链组合的兼容承诺。CMake 3.31.6、Ninja 1.11.1.3、Python 3.13.5。源码、工具、依赖分别位于工作目录、`.tools/venv/` 和 `.deps/`。

首次获取（会下载开发依赖，不安装全局工具）：

```powershell
python -m venv .tools/venv
.tools/venv/Scripts/python -m pip install -r tools/requirements-phase0.lock.txt
.tools/venv/Scripts/python tools/bootstrap.py --platform windows
./tools/build-windows.ps1 -Test
.tools/venv/Scripts/python tools/make-fixtures.py
```

实际本机 Qt 获取使用 bootstrap 内同一条 aqt 命令；PDFium 与字体原先直接从锁文件 URL 下载。bootstrap 的整体干净安装尚未重放；单个来源和散列已核对。不要求联网运行原型。

执行真实集成检查（每次使用**新的**输出目录，避免覆盖已有产物）：

```powershell
$env:PATH = "$pwd/.deps/Qt/6.8.3/msvc2022_64/bin;" + $env:PATH
.tools/venv/Scripts/python tests/integration.py --exe build/windows/phase0_pdf_probe.exe --font .deps/fonts/NotoSansSC-Regular.ttf --out benchmarks/results/my-new-run/integration
.tools/venv/Scripts/python benchmarks/run-probe.py 'D:/samples/book.pdf' --run benchmarks/results/my-new-run/book --repeat 33
$env:QSG_INFO = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
./build/windows/phase0_quick_probe.exe tests/fixtures/geometry.pdf benchmarks/results/my-new-run/quick.png
```

Quick 原型没有文件对话框，用命令行指定 PDF。自动执行预览→高清在途失效→当前高清→隐藏/资源释放→重显。传入截图路径时自动保存后退出；不传截图路径时可手工检查 IME，关窗时异步结束 worker。60 秒 watchdog 为实验上限，不是即时取消 PDFium 的能力。

手工输入验收可设置 `PHASE0_MANUAL=1` 后启动且不传截图路径：保持窗口打开，跳过自动隐藏重建，便于真实输入法和焦点检查。结束时关闭窗口；环境变量仅用于原型实验。输入框日志记录预编辑长度、composing、提交长度和焦点，不记录输入正文。按钮只用于焦点验收，不是产品工具栏。

`phase0_pdf_probe --help` 给出页码、像素宽、重复次数、字体和新 PDF 导出参数。单张图像最多 64 MiB，边长最多 8192；超限在分配前失败。阶段 0 不做正式缓存和分块。输出包括 events.jsonl、首轮 page.png；不记录文档正文。所有 PDFium API 的正常和异常清理都在 worker 内完成。

本机构建日志中缺 Vulkan headers 不阻断 D3D11；未使用 Vulkan。`qt-cmake` 重复配置时可能提示 CMAKE_TOOLCHAIN_FILE 未使用；实际编译器、Qt 和 SDK 以 CMakeCache 与日志为准。没有执行 ASan/UBSan；MSVC 此次为 Release 普通构建，不得称为 sanitizer 通过。

## macOS Apple Silicon：准备路径，尚未运行

需要实际 arm64 设备、Xcode CLI Tools 和 Python 3.13。PDFium arm64 包已下载检查配置和散列；Qt macOS 包、链接、运行时搜索路径、Metal 尚未实测。

```sh
python3 -m venv .tools/venv
.tools/venv/bin/python -m pip install aqtinstall==3.3.0 cmake==3.31.6 ninja==1.11.1.3 fonttools==4.60.1 PyMuPDF==1.26.5
.tools/venv/bin/python tools/bootstrap.py --platform mac
export PATH="$PWD/.tools/venv/bin:$PATH"
.deps/Qt/6.8.3/macos/bin/qt-cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DPDFium_DIR="$PWD/.deps/pdfium-mac"
cmake --build build/macos
ctest --test-dir build/macos --output-on-failure
```

以上是待验证命令，不是构建成功记录。macOS 不使用 Windows 的 DLL copy，需在真机核实 PDFium dylib install-name/RPATH；若失败保留日志并修复。CI macOS job 使用 self-hosted/macOS/ARM64 标签，尚无已注册 runner 的证据。YAML 未在远程执行；CI 不代替 GPU/IME 真机验收。
