# 构建与复现

## 阶段 2 正式应用（Windows 已验证）

复用固定 Qt 6.8.3 / MSVC / CMake / Ninja 环境，正式应用现在动态链接 PDFium 157.0.8100.0；无需原型字体。已具备 Qt 的环境可仅获取锁定引擎：

```powershell
.tools/venv/Scripts/python tools/bootstrap.py --platform windows --pdfium-only
```

该增量命令在本机执行并核对归档 SHA-256，通过后解压固定包。全新环境仍需下文 venv/锁定工具与 Qt 获取步骤。

```powershell
./tools/build-windows.ps1 -Test
./tools/run-windows.ps1
```

两个脚本的默认目录均为 `build/windows`。指定另一目录时，两处都传入相同 `-BuildDir`。正式目标 `langlern.exe`，自动化目标 `shell_tests.exe`。开发启动脚本为子进程补充 Qt bin PATH；发行部署留阶段 8。

阶段 2 新目录 `build/phase2` 和默认 `build/windows` 的实际构建/回归见 PHASE2_REPORT。启动采样（必须使用新输出目录）：

```powershell
.tools/venv/Scripts/python benchmarks/run-startup.py --exe build/windows/langlern.exe --run benchmarks/results/my-new-startup
```

启动脚本默认 3 次预热 + 30 个新进程样本；不刷新系统缓存，不能称冷启动。指标是首帧提交后合成 F6 快捷键往返的接收代理，详见 PERFORMANCE。普通运行不会发送 F6，也无诊断 watchdog；`--startup-probe --quit-after-ready` 仅供采样。

CTest 的文件对话框自动化使用 `AA_DontUseNativeDialogs`，覆盖 Qt 对话框取消/重开、选中结果信号路由；产品默认使用原生系统对话框。原生对话框实际点击选文件仍需人工检查，不能把合成 accepted 信号算成 OS 选文件通过。

原型现在须显式启用：`./tools/build-windows.ps1 -Phase0 -Test`。直接 CMake 使用 `-DLANGLERN_BUILD_PHASE0=ON -DPDFium_DIR=<固定包路径>`；默认 OFF。下文旧复现命令中的构建步骤须加 `-Phase0`，PDF/字体实验命令不变。

## 本机已执行的 Windows 路径

Windows 11 x64，Visual Studio Community 2026 / MSVC 19.51.36257.0 / SDK 10.0.26100.0；Qt 6.8.3 官方 MSVC2022 动态库在本工具链上实际编译链接成功。这不是对所有工具链组合的兼容承诺。CMake 3.31.6、Ninja 1.11.1.3、Python 3.13.5。源码、工具、依赖分别位于工作目录、`.tools/venv/` 和 `.deps/`。

首次获取（会下载开发依赖，不安装全局工具）：

```powershell
python -m venv .tools/venv
.tools/venv/Scripts/python -m pip install -r tools/requirements-phase0.lock.txt
.tools/venv/Scripts/python tools/bootstrap.py --platform windows
./tools/build-windows.ps1 -Phase0 -Test
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


## 阶段 2 真实引擎回归

```powershell
$env:LANGLERN_LARGE_PDF = "$pwd/.deps/samples/synthetic-800.pdf"
./tools/build-windows.ps1 -Test
$env:PATH = "$pwd/.tools/venv/Scripts;" + $env:PATH
.tools/venv/Scripts/cmake --build build/windows --target all_qmllint
```

大型样本由 `tools/make-large-fixture.py` 生成；不存在时不要设置环境变量，普通测试仍可运行，largeFileSmoke 会明确标记 skipped。本轮实际运行了该本地样本。扫描页/密码测试输入已提交，来源为原创 geometry；再生脚本 `tools/make-phase2-fixtures.py` 使用已有开发依赖 PyMuPDF，不属于产品运行依赖。

应用支持 `langlern.exe <本地PDF路径>`，窗口显示后异步打开。使用开发 SDK PATH 后可运行 `./build/windows/langlern.exe ./tests/fixtures/geometry.pdf`；启动脚本仍为无参数文件对话框入口。

`LANGLERN_SCREENSHOT_DIR` 指向已存在的新目录时，交互测试保存空窗口浅/深主题、PDF 和旋转深色截图，拒绝覆盖旧图。`QT_SCALE_FACTOR=2` 可用于附加 DPI 回归。文件对话框的原生 OS 点击选取仍属于人工检查；自动化验证 Qt 对话框及 accepted 到真实引擎的路由。

`check-shell-idle.py` 保留为阶段 1 历史隔离实验，会要求不加载 PDFium，不适用于阶段 2 正式程序。macOS 和远程 CI 均未在本轮执行。Windows FPDF_FILEACCESS 长度为 unsigned long，明确拒绝空文件和超过 4 GiB 的文件；密码输入尚未提供，受密码保护文件显示明确反馈。只检查当前页，暂无连续布局、正式缓存或分块。
