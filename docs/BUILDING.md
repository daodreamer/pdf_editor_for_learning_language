# 构建与复现

## 阶段 7 保存恢复与导出

```powershell
./tools/build-windows.ps1 -BuildDir build/windows -Test
./tools/run-windows.ps1 -BuildDir build/windows
```

CTest 共 7 项，新增 `storage_export`。CMake 将仓库 `assets/fonts/NotoSansSC-Regular.ttf` 与 `OFL.txt` 复制到构建目录 `fonts/`，开发运行需保留与 exe 的相对位置。尚不是完整发行包，macOS bundle 字体部署留待平台验证。

自动保存位置、格式和故障恢复见 [STORAGE_FORMAT.md](STORAGE_FORMAT.md)。Ctrl+S 可立即调度保存；写失败时关闭/切换不会继续，修复空间/目录权限后再保存或关闭即可重试。导出选择尚不存在的新 PDF 名称；已有目标一律拒绝。

独立文件验证：先将 `LANGLERN_PHASE7_ARTIFACT_DIR` 设为全新、已存在的目录，再于 Qt SDK PATH 下运行 `build/windows/storage_tests.exe -o <日志>,txt`，然后用开发环境 `.tools/venv/Scripts/python -X utf8 tools/verify-phase7-export.py <该目录>`。该脚本仅用现有开发用 MuPDF，不进入产品。失败运行应保留证据并换新目录重试。最终执行结果见 [PHASE7_REPORT.md](PHASE7_REPORT.md)。


## 阶段 5 文字答案

```powershell
./tools/build-windows.ps1 -BuildDir build/phase5 -Test
./tools/run-windows.ps1 -BuildDir build/phase5
```

默认 `build/windows` 同步更新。CTest 共 5 项，新增 `text_annotations`。SDK PATH 下可运行 `build/windows/annotation_tests.exe -o annotations.log,txt` 获取详细结果；高 DPI 附加设置 `QT_SCALE_FACTOR=2`，不是物理跨屏验证。`LANGLERN_SCREENSHOT_DIR` 指向已存在的新目录可生成文字答案截图，拒绝覆盖。实际结果见 [PHASE5_REPORT.md](PHASE5_REPORT.md)。

自动模式：空白单击建框并直接输入，原生文字拖动选择，空白拖动滚动；可切换选择/输入模式，输入模式允许覆盖原文处建框。单击已有答案编辑；选中后拖“移动”柄。Esc / Ctrl+Enter / 完成编辑结束事务；Ctrl+Delete 或删除按钮删除整框，普通 Delete 删除文字。Ctrl+Shift+A/S/T 切换自动/选择/输入。编辑器内 Ctrl+Z/Y 使用局部文字撤销，结束编辑后 Ctrl+Z/Y 及标注撤销/重做按钮使用事务历史。未选中不画边框或背景。答案仅存在当前会话，关闭/换文件后丢失；保存恢复和导出留在阶段 7。macOS 的快捷键前缀已按 Meta 路由，但构建/运行尚未验证。

## 阶段 4 原生文字选择与复制

```powershell
./tools/build-windows.ps1 -BuildDir build/phase4 -Test
./tools/run-windows.ps1 -BuildDir build/phase4
```

默认 `build/windows` 同样已更新。CTest 增加 `text_selection`，共 4 项；测试包含真实 QML 窗口鼠标拖选、Ctrl+C、跨页、边缘自动滚动、空白拖动、像素差检查、旋转/CropBox/UserUnit、Unicode、扫描页与失败生命周期。原来的大文件与长压力仍是可选独立入口，本轮未重跑 30 分钟压力。

左键从文字开始拖选；拖至上下边缘可自动滚动，滚轮和滚动条也可阅读。空白单击/Esc 清空，空白拖动交回滚动。选区就绪后用 Ctrl+C 或复制按钮；按原生字符流复制，不还原复杂多栏语义顺序。文本快照预算 16 MiB，每页上限 65536 个原生索引，一次最多 128 页；超限或错误明确提示。扫描页没有原生文字时不生成伪文本。

普通文字测试 PDF 由 `tools/make-text-fixture.py` 用 stdlib 生成；其 ToUnicode 映射测试不代表 Helvetica 能绘制中文/emoji。`chinese.pdf` 则使用已锁定的 Noto Sans SC 子集嵌入字体，重建用开发环境 `.tools/venv/Scripts/python tools/make-cjk-fixture.py`；字体依据见 `third_party/DEPENDENCIES.md` 和 `notices/Noto-OFL.txt`。运行和测试只需提交的 PDF，不要求安装 Python 字体工具。

附加 DPI 回归：在 SDK PATH 下设置 `QT_SCALE_FACTOR=2` 并运行 `build/windows/text_tests.exe`；本机实际 DPR 为 3。设置 `LANGLERN_SCREENSHOT_DIR` 到已存在的新目录可保存英文和中文选区截图，拒绝覆盖。测试运行结果见 [PHASE4_REPORT.md](PHASE4_REPORT.md)。该阶段当时未进入阶段 5；现阶段增量见本页开头。

## 阶段 3 连续阅读与缓存

阶段 3 沿用下文固定依赖，不增加产品第三方组件。独立验证目录：

```powershell
./tools/build-windows.ps1 -BuildDir build/phase3 -Test
./tools/run-windows.ps1 -BuildDir build/phase3
```

默认 `build/windows` 也可按同一命令重新构建。新增 reading_tests，CTest 共三个目标（shell_interactions、shell_startup、reading_invariants）。默认不执行压力轨迹；readingStress 和没有 LANGLERN_LARGE_PDF 的 largeFileSmoke 会明确 skipped。CTest 通过不等于长压力或大文件已执行。交互测试查找 Repeater delegate 时遍历视觉树，而不假定 delegate 的 QObject parent 是窗口。

真实 D3D11 连续轨迹（输出目录必须不存在）：

```powershell
.tools/venv/Scripts/python benchmarks/run-reading.py --pdf .deps/samples/synthetic-800.pdf --run benchmarks/results/my-reading --seconds 60 --repeat 5
.tools/venv/Scripts/python benchmarks/run-reading.py --pdf .deps/samples/synthetic-800.pdf --run benchmarks/results/my-stress --seconds 1800 --repeat 1
```

`--pdf` 必须是已存在的本地输入。合成样本不存在时，用 `tools/make-large-fixture.py .deps/samples/synthetic-800.pdf` 生成；不把 synthetic 当真实扫描教材。runner 为子进程配置 Qt PATH，超时上限为每轮 seconds+120。日志、每秒轨迹、逐帧间隔、源码/二进制/输入散列均留在新的本地运行目录。

可选系统 GPU 采样：在压力进程运行时取其准确 PID，另开开发终端运行以下命令；输出文件必须不存在。

```powershell
./benchmarks/sample-gpu-windows.ps1 -TargetProcessId <reading_tests_PID> -OutputPath <新的绝对JSONL路径>
```

计数器不可用时保留错误/空值；不以 GPU 像素估算代替系统 GPU 数据。无需该脚本即可运行正式阅读器与测试。计数器和大量诊断输出不用于无扰动基线。

阅读操作：拖动滚动条、滚轮或触控板连续阅读；页码输入可远跳；−/+ 或 Ctrl+滚轮缩放到 25%–400%，适应页宽回到 100%。窗口和 DPR 变化重新请求清晰图像。默认 CPU 图像 128 MiB、活跃 GPU 像素估算 64 MiB、前后各 1 页，参数在 src/pdf/readingconfig.h。低预算、超大页采用动态分辨率，尚无局部分块；不保证高倍放大始终原生清晰。

阶段 3 当轮实际执行结果及未验证边界以 PHASE3_REPORT.md 为准。macOS 运行检查按用户安排延后；阶段 4 增量见本页开头。

## 阶段 2 构建基础（历史记录）

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

`check-shell-idle.py` 保留为阶段 1 历史隔离实验，会要求不加载 PDFium，不适用于阶段 2 正式程序。macOS 和远程 CI 均未在本轮执行。Windows FPDF_FILEACCESS 长度为 unsigned long，明确拒绝空文件和超过 4 GiB 的文件；密码输入尚未提供，受密码保护文件显示明确反馈。阶段 2 当时只检查当前页；阶段 3 已提供连续布局与正式缓存，仍无分块。


## 阶段 6 标注与长笔迹复现

```powershell
./tools/build-windows.ps1 -Test
$env:PATH = "$pwd/.deps/Qt/6.8.3/msvc2022_64/bin;" + $env:PATH
./build/windows/ink_tests.exe -o benchmarks/results/my-new-ink-tests.log,txt
.tools/venv/Scripts/python benchmarks/run-ink.py --run benchmarks/results/my-new-ink-run --repeat 3
```

run-ink.py 要求全新的目录，记录二进制/源码/样本散列、三个独立进程的输入处理和 frameSwapped 原始 trace、Windows 进程内存。该脚本测试真实 Qt Quick/D3D11 上注入的长笔迹，不模拟硬件延迟。`LANGLERN_SCREENSHOT_DIR` 指向已存在的新目录时，ink_tests 额外保存 ink-tools.png 和 ink-colors.png；`QT_SCALE_FACTOR=2` 可执行高 DPI 回归，实际 DPR 以日志为准。`LANGLERN_INK_TRACE` 指定新文件保存单次 longStrokeTrace 的 JSON；默认 CTest 不写原始 trace。QML lint 需 MSVC DevShell，与已有构建脚本同一环境中运行 `cmake --build build/windows --target all_qmllint`。

阶段 6 不包含自动保存和导出。macOS、物理触控笔/触控板、实际跨屏 DPR 留待实机验证，详情见 PHASE6_REPORT。
