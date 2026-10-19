# 构建与复现

## 2026-10-19 Pocket TTS / Estelle 构建

`tools/build-windows.ps1 -Test -Lint` 先运行 `bootstrap-pocket.py`，获取锁定的 ONNX Runtime 1.23.2、SentencePiece 0.2.1、SoundTouch 2.3.3 及法语模型，首次开发构建需联网；应用运行无网络下载。模型/语音/通知的 URL、版本、字节数及 SHA-256 均在 `third_party/pocket.lock.json`。原生 DLL、模型及许可证复制至 `build/windows/voices/pocket-fr-estelle-v2`，窗口启动时不加载；首次点击朗读才进行校验和模型加载。

不需要 Windows 法语 TTS 包、Python/PyTorch 或 CUDA 运行环境。保留整套 voices 子目录；`LANGLERN_POCKET_DIR` 可指定同样结构的语音目录。完整验收、CPU/内存/体积、维护与许可说明见 [NATURAL_SPEECH.md](NATURAL_SPEECH.md)。macOS 自然朗读尚未接入。


## 2026-10-19 离线 OCR 构建

Windows 使用 `tools/build-windows.ps1 -Test -Lint`，会在 MSVC 环境运行 `tools/bootstrap-ocr.py`。首次准备联网下载锁定源码与模型，验证 SHA-256 后按顺序编译 zlib → libpng → Leptonica → Tesseract，静态库和 CMake 配置位于 `.deps/ocr`；后续准备核对模型散列并复用构建签名。应用运行无网络依赖。Qt/PDFium 和原有编译器不变。

法语、英语、简中模型复制到构建目录的 `tessdata/`，Windows 打包脚本同样复制到候选包。`LANGLERN_TESSDATA` 可指定已有模型目录；缺模型明确报错，不自动下载安装。完整行为、许可证和验证边界见 [OCR_SELECTION.md](OCR_SELECTION.md)。

macOS 待验证：在原文后述 macOS 工具链环境设置 PATH 后，先运行 `.tools/venv/bin/python tools/bootstrap-ocr.py`，再配置应用。没有在 Apple Silicon 执行这些新增依赖/接口，不宣称双平台通过。

## 阅读器文档菜单回归（2026-10-18）

“···”菜单以按钮为 Popup 父项，在按钮下方 8 个逻辑像素处右对齐展开，使用 8 像素窗口边距。明确使用 Qt 6.8 的 `Popup.Item`，保持自定义样式一致；菜单沿用 Qt Quick Controls Menu/MenuItem 的键盘和选项触发机制。圆角面板、轻阴影、分组线、线性图标、蓝色高亮与快捷键提示适配浅深色。

本机 Windows x64 / Qt 6.8.3 / Release 构建与 qmllint 成功（Qt 头文件 C4702 警告仍存在）。完整 `design_tests.exe` 17 项通过、0 失败、0 跳过（25.638 s）；新增 `documentMenuAnchorsToButton` 使用合成扫描样本和指定真实法语教材，实际点击按钮，在 1440×900、900×700、700×580 窗口和深色模式检查菜单与按钮的右边缘及上下间距、窗口边界、鼠标高亮、键盘导航与 Esc、双页选项触发、点击外部关闭、再次点击按钮收起，未出现 QML warning。额外设置 `QT_SCALE_FACTOR=2` 后，同一菜单回归 4 项通过（含初始化/清理，11.140 s），这是缩放因子验证，非物理跨屏验证。实际窗口裁剪图已检查浅色、深色与高亮效果。

日志与截图保存在 `benchmarks/results/2026-10-18-document-menu/`，真实教材与截图不提交。macOS、sanitizer、长压力和性能 A/B 本轮未执行。

## 默认原版阅读回归（2026-10-18）

打开/拖入 PDF 直接阅读原版，扫描优化入口位于阅读器“··· → 扫描优化…”。本机 Windows x64 / Qt 6.8.3 / Release 执行 `./tools/build-windows.ps1 -Test -Lint` 成功，CTest 8/8 组通过（71.36 s），qmllint 通过。构建仍有 Qt 头文件内的 MSVC C4702 警告。测试中的可选大型文件及长压力用例因未配置 `LANGLERN_LARGE_PDF` 等变量跳过；macOS 和 sanitizer 未执行。

新增 `design_tests.exe originalPdfOpensBeforeOptionalOptimization`，分别使用合成扫描样本和指定的真实法语教材（291516513 bytes）验证：资料库直接发起选文件、打开后进入阅读器且无优化页/优化预览、显式菜单进入优化预览、取消返回阅读、原生拖入直接阅读、源文件 SHA-256 前后一致。练习数据按用例隔离到临时目录。完整转换另存与取消由已有扫描优化用例回归；未对整本真实教材执行转换或性能 A/B。日志与样本摘要保存在 `benchmarks/results/2026-10-18-original-pdf-default/`，教材及提取图像未提交。

## 阶段 8 Windows 交付

```powershell
./tools/build-windows.ps1 -Test -Lint
./tools/package-windows.ps1 -OutputDir build/packages/windows-x64
.tools/venv/Scripts/python -X utf8 tools/validate-windows-package.py --package build/packages/windows-x64 --out benchmarks/results/my-package-check
```

构建脚本保存产品输入及 EXE 散列，打包前检查对应关系。打包脚本使用已锁定 Qt 的 windeployqt 扫描 QML，复制 PDFium、字体、对应运行库与通知，保存实际源码快照、逐文件散列和 ZIP；拒绝已有目录/压缩包。校验器先核对散列，再从独立副本运行产品启动探针并检查 Qt 加载路径，最后加入测试专用 Qt6Test.dll 和测试 exe 跑六个功能套件。测试专用文件不会加入产品 ZIP。此校验清除 SDK/Qt 环境路径，但使用同一开发机，不能冒充干净 VM。

阅读/切换/标注/保存恢复 30 分钟组合压力：

```powershell
.tools/venv/Scripts/python -X utf8 benchmarks/run-reading.py --exe build/windows/reading_tests.exe --pdf .deps/samples/synthetic-800.pdf --run benchmarks/results/my-workflow --seconds 1800 --repeat 1 --workflow
```

普通重复阅读基线不加 --workflow，使用 --seconds 65 --repeat 5。帧数据是 Qt 提交代理，系统内存与受管预算分开。最终执行结果和未达到的目标见 [PHASE8_REPORT.md](PHASE8_REPORT.md)；候选包操作见 [WINDOWS_PACKAGE.md](WINDOWS_PACKAGE.md)。


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


## LinguaPDF 设计迭代增量（2026-10-16）

固定Qt/PDFium/MSVC版本沿用阶段8。新增Qt Sql来自原qtbase安装，不需要新增Qt下载模块。可选离线词库准备与重建：

```powershell
.tools/venv/Scripts/python tools/bootstrap-dictionary.py
./tools/build-windows.ps1 -Test -Lint
```

词库源、许可证散列及提交在 third_party/dictionary.lock.json；开发命令首次联网取源，应用没有联网取词库功能。未安装词库时阅读正常，真实词库测试明确skipped，正式CI配置已加入准备步骤，但本轮未执行远端CI。

新增design_tests使用Windows实际OCR语言包，缺引擎/英文包时明确skipped；本机实际识别已通过。语音以本机SAPI离线包为准，UK/US包缺失时明确报错。音频测试使用临时生成的静音WAV，不用假进度。上述本机Windows后端不代表macOS已支持。

截图复现（输出目录先创建且保持独立）：在Qt bin PATH下设置 LANGLERN_SCREENSHOT_DIR，运行 `build/windows/design_tests.exe screenLayoutsAndScreenshots`。整套验证 `build/windows/design_tests.exe` 包括词库、OCR和真实导出；CTest的design_workflows默认包含这些检查。
## GitHub Actions 故障修复（2026-10-16）

`43707cc` 的 Windows CI 详情确认三处问题：首次生成词库时 SQLite 事务上下文不负责关闭连接，导致 `.pending` 重命名报 WinError 32；无 WinMM 输出设备的 runner 无法执行真实播放；文字框手柄在渐显首帧前被 `visible: opacity > 0` 禁用。词库连接现以 `closing` 显式释放，支持 `--output-dir` 隔离验证；手柄在需要显示时立即参与命中；音频测试仅在系统枚举不到输出设备时明确 QSKIP，有设备时仍要求真实播放、定位与循环。

两个 Windows workflow 启用原生命令失败传播，依赖初始化失败不再继续构建；构建脚本在 CTest 失败时打印具体 Qt 断言（包括原生命令直接抛错的情况）。本机 Release 构建、8/8 CTest 组（56.10 s）与 qmllint 通过；在新的隔离目录用锁定 CSV 实际生成并重复打开 770611 词条的 SQLite 数据库成功，未触碰现有开发词库。无音频设备分支须以 GitHub runner 的实际执行结果为准，不能当作播放功能通过。

远端首次复跑确认词库和音频环境处理已生效，另发现测试前置条件不足：原始坐标测试需等待 `storageReady` 后才可直接写标注；自动输入测试应点击确定的空白区域，避开原 PDF 第三行文字在不同 DPI 舍入后的命中边缘。已补齐等待并使用空白位置，不改动原文选择优先级。



## 2026-10-19 首推 CI 的环境与时序修复

`c2ebf38` 的两个 Windows 工作流完成构建后暴露测试问题：英文 Windows 的窄字符 `qputenv(...toLocal8Bit())` 无法保存中文模型目录；阶段 0 中部分测试在存储恢复尚未结束时，就把文档切换、关闭和错误反馈当成同步完成。桌面工作流只有中文环境变量用例失败；阶段 0 另有关闭时必须出现丢帧、跨文档立即清空和立即显示错误的过早断言。

生产路径读取保留 Qt 的 Windows 宽字符 `qEnvironmentVariable`，测试设置统一使用 `_wputenv_s`（POSIX UTF-8），原值以 QString 保存并在离开作用域时恢复。会话切换测试等待目标 URL 和新 session，再立即检查选区清空及剪贴板不变；循环切换逐次等待提交和清空。关闭测试继续要求预览触发关闭、PDFium 句柄归零及关闭后无图像；不再把“正常完成后关闭”误判为必须丢弃渲染，实际快速失效丢帧仍由阅读调度测试覆盖。未修改生产行为或跳过失败测试。

首推 CI 的原生 Estelle PCM/重复请求通过；真实教材和音频设备在 runner 不可用时明确跳过，以本机实测记录为准。失败原始日志保留在本机 benchmarks/results/github-ci-39328385844 和 github-ci-39328385790，修复后的远端结论以对应新提交 SHA 的工作流为准。

修复后本机 Release 全量 CTest 10/10 组通过（110.25 s），包含固定真实教材 OCR/选区和 Estelle 实际播放，QML lint 与差异校验通过；独立静态复审确认等待条件与环境还原覆盖提前返回。远端复跑结果见后续提交对应 GitHub Actions。
