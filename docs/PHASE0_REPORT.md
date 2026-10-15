# 阶段 0 实施与风险报告

日期：2026-10-15。**本次 Windows 可执行工作已交付；原路线图的双平台完整验收尚未通过。** 用户已明确选择 macOS 保留待验证。没有推进阶段 1，没有正式阅读器、连续滚动、标注工具或完整自动保存功能。

## 补充验收记录（2026-10-15）

以下记录补充并更新后文首次报告中的“真实 IME 未测”和“桌面查看器未检查”状态。用户随后明确要求不再控制电脑；此后没有继续 UI 操作。macOS 由用户在后续实机检查，本次不执行。

- Windows 微软拼音真实按键：`n`、`i` 进入预编辑，显示系统候选；空格提交“你”。日志中 preedit 长度 1→2→0，提交长度变为 1，composing 清除。Ctrl+Z 撤销到空文本，Ctrl+Y 恢复“你”；Esc 取消下一段预编辑后仍为“你”。失焦时本机 IME 将未完成的 `n` 提交为字面 `n`，最终为“你n”，没有重复提交。切换英文模式后按 `a` 得到“你na”；Tab/Shift+Tab 的焦点变化有日志。这是原生键盘输入与系统候选检查，不是合成 QInputMethodEvent。Qt UIA 的 focused_element 一直误报顶部 Label，因此以编辑值、候选画面和 QML activeFocus/composing 日志交叉确认。
- Acrobat 与 PDF24 实际打开了普通导出样本，中文、蓝色文字、半透明红色笔迹和色卡可见。Acrobat 另检查了 90 度旋转页；UserUnit=2 页的中文和笔迹也可见。
- 查看器运行期间多次出现 `Font Capture: Windows - Application Error`，原因未确定；不能称为无异常的完整验收。PDF24 的旋转页检查在用户按 Esc 停止 Computer Use 时中断，尚未完成，UserUnit 页也未完成交叉检查。
- 为输入验收增加了 PHASE0_MANUAL、输入框焦点/预编辑长度日志与焦点按钮；对应 Windows 构建和 CTest 已重新执行成功。没有增加产品功能。

原始证据保存在 `benchmarks/results/acceptance-20261015/`，选取的 build.log、ime-native.log 和文本值观察 JSON 可随源码归档。截图和未筛选的本地运行产物不发布；测试输入均为原创样本和固定测试文字，未上传真实教材。阶段 0 作为技术验证交付收尾，不表示剩余验收项通过。

## 已交付

- 审阅并更新架构、依赖路线图、可重复性能方案；原型契约先于编码记录。
- C++20 / CMake，Qt 6.8.3、PDFium 157.0.8100.0 固定包、来源、散列、实际构建配置及许可证据。
- 共用 PdfWorker 的 CLI 和 Qt Quick 两个技术 probe：按需文件读取、唯一 PDF 线程、RAII、像素预算、最新需求 mailbox、过期结果 worker/UI 双重拒绝、异步退出。
- 真实 PDFium BGRA → QImage → QQuickItem / QSG 纹理路径；公开 API，D3D11 实测；隐藏释放再重建。
- 旋转/裁剪/单位色卡，中文字体现实文件导出、半透明路径、重新打开及独立 MuPDF 校验。
- QSaveFile 提交、取消、子进程在提交前被终止的恢复小实验；合成 IME 事件检查；文件别名保护回归。
- Windows/macOS CI 配置、编译说明、README、贡献指南、MIT 自有代码声明及第三方通知草案。没有远程 CI 执行记录。

## 实际环境和构建

Windows 11 Pro 10.0.26200，Core Ultra 7 265K，约 47.4 GiB 可用物理内存总量，RTX 5070 驱动 32.0.16.1692；3840×2160、160 Hz、DPR 1.5，平衡电源方案。环境原始记录在 `benchmarks/results/phase0-20261015/environment.json`，存储设备清单另存。后台负载、温度和精确磁盘映射未控制，因此仅作为初始基线。

MSVC 19.51.36257.0 / Windows SDK 10.0.26100.0，CMake 3.31.6，Ninja 1.11.1.3，Python 3.13.5，Release，动态 Qt。Qt SDK 自身为 MSVC2022 构建，在本机 MSVC2026 上实际编译链接成功。无 Git 元数据；不提供不存在的 commit/PR。

执行入口和输出：

| 项目 | 真实结果 | 证据 |
| --- | --- | --- |
| `tools/build-windows.ps1 -Test` | 配置、编译、链接、CTest 成功，退出 0 | `build.log` |
| Qt Test 契约 | 3 个实质测试，加 init/cleanup：5 passed / 0 failed | `contracts.txt` |
| `all_qmllint` | 执行成功，无诊断警告 | `qmllint.log` |
| `tests/integration.py` | 8 页几何、3 个导出、4 类错误、100 次 mailbox 替换、日志/PNG 硬链接保护通过 | `verified/integration.json` |
| `tests/artifact-safety.py` | GUI 截图硬链接拒绝；benchmark 拒绝既有目录；源散列不变 | `artifact-safety/safety.json` |
| QML 中文合成事件 | 预编辑不进入 text；提交为两个中文字符，composing 清除 | `integration-final/quick.log` |
| Windows GPU | Qt 记录真实 QRhi D3D11、RTX 5070；资源重建两次；截图成功 | 两份 quick.log / quick.summary.json |
| macOS / Metal | 未执行，按用户选择保留 | 无成功记录 |
| sanitizer | ASan/UBSan 未执行 | 本报告不声明通过 |

表中相对证据路径均位于 `benchmarks/results/phase0-20261015/`。基础测试覆盖矩阵往返（四种旋转、DPR 1/1.25/1.5/2、误差≤1e-6）、原子快照取消/提交、进程被终止前未提交写入保留旧文件、无效目录错误。没有验证磁盘真正写满、ACL 拒绝、断电或文件系统硬件持久性，不把正常 cancel 等同于全部异常恢复。

首次实现曾出现 PDFium package 缺 ConfigVersion、Windows min/max 宏、QML 类型不可 final、合法 PDF 无 CropBox 和 QML lint 缺依赖等问题，均已修复后实际重建。导出初检失败由空格/NBSP 别名导致，详见下方风险。独立代码审查两轮发现诊断产物可能覆盖输入、worker 计时漏记清理；修复后实际运行碰撞回归。最终静态复查在限定范围内没有剩余 P1/P2；这不是形式化证明。

## 样本与真实测量

真实样本由用户指定，仅本地读取：119,895,877 bytes（约 114.34 MiB），66 页；首页无文字层（text_characters=0）。源 SHA-256：`745299d35e2dbe31713b873f7e45f87dc172c254426ba6d3e81541a62ad14a03`，实验前后相同。没有上传样本；私有画面位于忽略的 private 目录。

`baseline/` 是本报告真实教材的权威 CLI 基线；旧的 textbook/textbook-final 目录是开发中的诊断记录，不混合统计。先读取源散列会预热 OS 文件缓存；每次同一进程内初始化/打开/渲染/关闭，3 次预热 + 30 次正式采样，不能称为 30 次新进程热启动。宽 1400、高 1984，QImage 11,110,400 bytes。逐次原始数据 events.jsonl，10 ms 进程内存采样 resources.json；短峰值可能漏采。

| 指标 | 中位数 | P95（nearest-rank） | 范围 |
| --- | --- | --- | --- |
| 打开阶段（库初始化、文件打开、FPDF_LoadCustomDocument） | 0.397 ms | 0.535 ms | 0.348–0.987 ms |
| FPDF_RenderPageBitmap | 149.440 ms | 161.004 ms | 139.326–177.059 ms |
| 完整 worker 任务（含页解析、坐标/文本、原生清理；正式轮不写 PNG） | 154.814 ms | 165.063 ms | 144.695–181.363 ms |

采样到的进程峰值 RSS 为 **116,535,296 bytes（约 111.14 MiB）**。这是整个 CLI 进程工作集，不是自有缓存、GPU 显存或任意文档内存上限。未采集 GUI RSS、系统 GPU 显存；纹理仅按像素字节数估算，不含驱动、atlas 和替换重叠。没有正式缓存，命中率为“不适用”，不是 100%。

另生成了 800 页、839,214,902 bytes（约 800.34 MiB）的**合成 raw 灰度图像文件**：每页独立固定种子噪声，不是重复空页或尾部填充。相同 3+30 轮，首页渲染中位 4.911 ms、P95 5.287 ms；任务中位 8.768 ms，采样峰值 RSS 32,645,120 bytes。它验证大文件偏移/页树/按需读取可行性，因无 JPEG/JPX 解码，绝不能用它宣称真实 GB 扫描教材解码达标。生成器和记录见 `tools/make-large-fixture.py`、`synthetic-800/`；真实 500 MiB–2 GiB、800–1500 页扫描教材仍缺覆盖。

Quick 真实教材的单次诊断轨迹：首个窗口 frameSwapped 590.443 ms；700 像素预览图像交付 699.012 ms；最终 1400 像素交付 1069.16 ms。都是内部提交/交付事件，**不是可交互时间、首像素系统呈现或冷启动**。带 QSG 日志和截图，只有一次，不拿它验证 300/800 ms 目标。滚动帧时间、P95/P99、输入到屏幕和 60 FPS 均未测。

任务 2（2600×3684，约 36.54 MiB 像素）开始后替换为任务 3：任务 2 占用 worker 223.698 ms，替换到任务 3 开始 224.744 ms。日志记录任务 2 丢弃，没有交付到 UI。started 信号只标记任务开始，不标记精确进入底层 Render API；时间戳能证明需求在该任务执行期间被替换。同步 PDFium 调用没有被中断。该尾延迟需要阶段 3 的渐进渲染/分块实验，不能用更换 mailbox 冒充抢占成功。

## 图像、坐标和导出结论

8 页原创小型 PDF 覆盖 MediaBox 负原点、非零 CropBox、0/90/180/270 度、UserUnit=1/2。36 个 PageToDevice/DeviceToPage 检查/页的最大设备误差为 0 px。纯 QTransform 往返与引擎对照分别测试，不把矩阵自逆测试当作完整产品映射实现。

原型白底 BGRA→ARGB32 色卡精确匹配红/绿/蓝；半透明红约为 (255,127,127)，误差≤1。输出图像拥有自身 QImage 缓冲，FPDF_BITMAP 包装在发信号前释放。纹理由节点拥有，在渲染执行域创建/销毁，隐藏失效后从 CPU 图像恢复。没有统计实际驱动纹理字节或进行图形验证层测试。

第 0、1、4 页导出真实新 PDF，加入蓝色 `Bonjour 中文练习` 和 3 页面单位、alpha=128/255 的红色路径；原文件散列不变。PDFium 重开提取通过；独立 MuPDF 提取、路径宽度/透明度检查通过，并实际查看了普通、旋转和 UserUnit=2 三张 MuPDF 渲染图，中文字、位置和笔迹可见。自有生成样本位于 tests/fixtures，导出结果留在 verified/page-N。没有声称完成两个桌面查看器的人工交互验收。

确认的注意点：

1. CropBox 可省略，须回退 MediaBox。
2. PDFium 对本样本的 UserUnit=2 不把 width/height 翻倍；保存的坐标不能不加说明地当作物理 points。MuPDF 显示时正确翻倍，原始坐标导出保持相对位置。
3. Noto 的空格和 NBSP 共用 glyph，PDFium 自动 ToUnicode 可导出为 U+00A0；当前测试仅在比较时归一空白，中文字符原样通过。复杂字体映射、组合字符、连字和 Qt/PDF 排版完全一致仍需专门方案，不能直接用于产品的逐字符无损承诺。
4. 嵌入全量字体后，微小样本导出约 6.35 MB。子集化、静态派生字体正确命名及多字体 fallback 待后续验证。
5. 原型只导出固定一行文字和一条路径，未做一般标注排版、完整 undo/redo 或原 PDF 内容编辑。

## 文件、资源与许可边界

输入经 QFile + FPDF_FILEACCESS 按需读，不把整本 PDF 读入内存。Windows 当前接口限制长度≤ULONG_MAX，不能宣称无限文件大小。页面边长≤8192、单图像≤64 MiB；超限分配前拒绝。PDFium 内部解码分配不受此自有位图预算约束。

所有原生句柄在 worker 局部 RAII 范围内创建/销毁；日志 native_scope_closed 表示该作用域已退出，**不是独立测得的零句柄计数**。同一进程 33 次打开/关闭提供初步压力结果；未运行 sanitizer、长时间泄漏分析或 30 分钟完整交互轨迹。

导出拒绝已有路径（包括源文件、硬链接），QSaveFile 提交新文件；CLI events/PNG 和 Quick 截图独占创建。benchmark 要求新目录，日志和 JSON 独占创建。测试真实创建硬链接验证输入散列不变。该机制适用于单用户实验，未实现通用并发导出锁、所有文件系统别名和完整产品保存错误 UI。

实际包与通知审查见 [DEPENDENCIES.md](../third_party/DEPENDENCIES.md)。已归档 PDFium 17 份包内第三方通知加主许可、字体 OFL、Qt 对应许可和 301 个 SDK package 的供应商 SBOM 摘要。候选采用动态 Qt LGPL 路径、PDFium BSD-style、OFL 字体，开发验证 MuPDF 不进入产品。尚未制作发行包，最终部署闭包、对应源码、runtime 和平台签名仍需逐包复核，不宣布整体发行合规。

## 后续门槛与停止位置

本次实施在阶段 0 停止。可复用的技术路径已在 Windows 证实，但原路线图的完整门槛仍保留：

| 待办 | 状态/下一步 |
| --- | --- |
| macOS arm64 构建、Metal、资源生命周期、输入/导出 | 用户明确保留待验证；有设备后按 BUILDING 重放，并锁定 mac Qt archive |
| 真实 Windows/macOS 中文 IME 预编辑、候选、焦点和系统撤销 | 仅 Qt 合成事件通过；需要真实输入法人工操作 |
| 两个独立桌面查看器检查导出 | 已有 PDFium/MuPDF 引擎和渲染图证据；桌面查看器交互尚未做 |
| 真实 GB 级复杂扫描、图像内部峰值与渐进取消 | 已有 114 MiB 真实和 800 MiB 合成样本；不可抢占延迟已确认 |
| 保存满盘/ACL/断电、完整恢复格式和 document identity | 原子提交/进程终止小实验通过；完整故障矩阵留阶段 7 |
| 最终发行许可闭环、干净系统部署、CI 执行 | 方案和配置已准备；没有对应执行证据 |

建议下一步先补外部平台/IME/查看器验收，再讨论阶段 1。可以延后到对应阶段调优分块、预加载、缓存和字体子集；不能把本原型直接称为已完成 MVP。后续实施仍需用户明确指示。
