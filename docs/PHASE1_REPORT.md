# 阶段 1 Windows 交付报告

日期：2026-10-15。**Windows 范围的阶段 1 已交付；macOS 按用户明确要求延至整个项目完成后检查。没有进入阶段 2。** 本报告不表示双平台验收、300 ms 启动目标或完整阅读器已完成。

## 本次成果与边界

- `langlern` 正式入口、Qt Quick 基础窗口、轻量文字工具区、无文档状态及本地文件选择。
- 浅色、深色、跟随系统主题 token；中文系统字体、Tab 焦点、帮助弹窗；Ctrl+O 选择、Ctrl+W 清除、Ctrl+Shift+L 切换主题、F1 帮助。平台默认关窗/退出保留；Qt 的 Ctrl 描述在 Apple 平台映射为 Command，但未实机验证。
- 文件选择仅显示文件名和“尚未加载”。只检查本地路径元信息，不读取 PDF 内容、不建立文档会话、不创建 PDF worker/缓存；无 PDFium 链接或加载。原型继续通过显式构建选项保留，adapter 迁移延至阶段 2。
- 根 CMake 复用固定工具链及警告配置；独立 QML 模块、Windows 启动脚本、CTest、启动/空闲采样脚本与 Windows CI 配置。无新增外部供应商依赖；Qt Quick Dialogs 增量许可摘录已归档。

主题仅在本次进程内保留，不写配置文件。没有 PDF 显示、标注、自动保存或导出产品功能。开发运行依赖 Qt SDK PATH，尚未制作独立发行包。

## 实际构建与验证

设备：Windows 11 Pro 10.0.26200，Core Ultra 7 265K（20 逻辑核），50,873,622,528 bytes 系统内存，RTX 5070 驱动 32.0.16.1692；3840×2160 / 160 Hz，默认 DPR 1.5，平衡电源方案。MSVC 19.51 / SDK 10.0.26100.0，Qt 6.8.3 官方 MSVC2022 动态库，C++20，CMake 3.31.6 / Ninja 1.11.1.3，Release。设备与电源原始信息保存在结果目录，不控制温度及所有后台负载。

本次源码基于 Git `9eeb159` 的工作区增量；精确 C++/QML/CMake 与可执行文件 SHA-256 见启动 manifest，不将未提交增量冒充已有提交。以下证据相对于 `benchmarks/results/phase1-20261015/`，原始运行目录本地保存并受 Git ignore，不包含真实教材。

| 检查 | 实际结果 | 证据 |
| --- | --- | --- |
| 全新 app-only 构建：`./tools/build-windows.ps1 -BuildDir build/phase1-clean -Test` | 配置/编译/链接退出 0；CTest 2/2 通过；Cache 中原型 OFF，无 PDFium_DIR | `clean-build.log`、`build/phase1-clean/CMakeCache.txt` |
| 正式交互测试 | 2 个实质测试 + init/cleanup = 4 passed；本地中文/大写扩展名、无效/远程/取消状态、文件不变、主题循环、Tab、帮助开关、选择结果路由、重开、Ctrl+W、640×480 缩放 | `interactions-pass.txt`；最终同逻辑在 clean CTest 重跑通过 |
| DPR 1.5 / 2、浅/深主题 | 自动测试通过，默认尺寸两主题截图已实际查看，无明显裁切；DPR 2 由环境缩放注入，不等于跨显示器移动测试 | `visual-final/`、`dpi2-pass.txt` |
| 最终 QML lint | `all_qmllint` 退出 0，无诊断警告 | `qmllint-final-clean.log` |
| 保留阶段 0 的构建 | `./tools/build-windows.ps1 -Phase0 -Test` 退出 0；CTest 3/3 通过 | `phase0-regression-build.log` |
| 阶段 0 真实引擎回归 | 8 页几何、3 个独立导出、4 类失败、100 次需求替换通过 | `phase0-integration/integration.json` |
| 空闲与 PDFium 隔离 | 5 秒 CPU 时间差在采样精度内为 0；已加载 DLL 列表无 PDFium | `accepted-idle/idle.json` |
| 独立静态审查 | 默认目录、性能元数据及平台快捷键问题已修复；复查未留 P1/P2 | 审查对话记录；Windows 测试另行执行 |
| macOS、远程 CI、sanitizer | 未执行；不宣称通过 | 按用户要求延后 macOS；无远程 CI 执行证据 |

编译仍有 MSVC 19.51 对 Qt 6.8.3 头文件及 QML 生成代码触发的 C4702（unreachable code）告警，没有屏蔽；本项目手写源没有新增编译警告。缺 Vulkan headers 对本次 D3D11 非阻断。未执行 ASan/UBSan，不将普通 Release 测试等同 sanitizer。

文件对话框边界：产品默认采用 Qt 的原生系统对话框。早期构造/取消实验暴露立即销毁原生 helper 的清理告警，现改为按需创建后复用；最终确定性自动化使用 `AA_DontUseNativeDialogs`。重开/取消由 Qt 对话框验证，文件结果由设置 selectedFile 并合成 accepted 信号验证路由，**未完成原生系统对话框真实点击选文件的人工验收**。不将程序调用 accept/reject 等同系统选择行为。测试中发现 StandardKey.Close 在 Windows 主绑定为 Ctrl+F4，已把文档约定的 Ctrl+W 显式绑定并验证。

## 实际性能数据与差距

权威启动数据：`accepted-startup/`。每次新进程，3 次预热丢弃 + 30 次正式样本，OS/Qt 缓存为热；无截图或 QSG verbose 日志。每条记录含 Release、实际 Qt 6.8.3、graphics_api=4（D3D11）和首帧 DPR 1.5。日志中 main_ms 以 main 内 QElapsedTimer 为原点；外部接收计时从 Popen 前开始，包括创建进程与日志管道调度。

| 指标 | 中位数 | P95 nearest-rank | 范围 |
| --- | --- | --- | --- |
| 外部启动请求 → 首帧提交后的合成 F6 响应标记接收 | 377.60 ms | 388.56 ms | 370.40–389.71 ms |
| main → 同一合成响应 | 358.18 ms | 368.71 ms | 351.23–369.56 ms |

**仍未达到热启动 300 ms 目标**：外部响应代理 P95 高约 88.56 ms。它还不是系统真实输入与屏幕呈现的完整可交互指标，不能由此宣布完整可交互性能达标。未做缓存清除/重启控制，冷启动及 800 ms 目标未验证。

分段定位：30 次 QML ready 内部时间平均 74.62 ms（71.75–81.84）；`window_show_requested` 到 `window_show_returned` 平均 283.51 ms（276.75–293.04），主要耗时集中在窗口显示调用。这里包含平台窗口、字体/图形相关初始化，尚无足够系统 trace 证据将其全部归因某个驱动。后续性能任务：在独立诊断构建中用系统采样分解 show 路径，区分文本字体与 D3D11 初始化，再做相同样本口径 A/B；阶段 8 补冷启动和系统呈现测量。不以缩短计时起点或改后端来绕过目标。

同次空闲样本：等待 2 秒后采样 5.0008 秒，CPU 增量 0 秒（系统计时粒度内）；末点 RSS 87,379,968 bytes（83.33 MiB），private 97,878,016 bytes（93.34 MiB）。不是峰值、缓存预算、GPU 显存或长期零 CPU 的证明。普通运行无应用自建持续 Timer/动画/后台预热；诊断 F6 与一次性 watchdog 仅在显式 probe 模式运行。无 PDF 文档，文档首次显示、滚动帧时间、缓存命中率均不适用。

初始 `startup/`、中间 `startup-final/` / `startup-release/` 及失败开发日志仅作诊断，不混入权威 30 次统计。源码调整包括系统字体、按钮与首窗显示顺序，未做单因素实验，不能仅凭前后差值给单项优化下因果结论。

## 下一步及保留事项

默认构建和运行见 README。下一阶段是用户另行授权的阶段 2：正式文档服务及 adapter 迁移、唯一 PDF 工作线程、异步单页显示及错误反馈。本次在阶段 1 停止。

原生文件对话框实际选择、跨屏 DPR、真实系统输入、冷启动、300 ms 优化、远程 CI 与最终 Qt 部署许可闭环继续保留。macOS 构建、Metal、快捷键、IME 与导出由用户在整个项目完成后实机检查；不因本次 Windows 结果标记 macOS 通过。
