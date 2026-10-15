# 阶段 2 Windows 交付报告

日期：2026-10-15。**Windows 范围的阶段 2 已交付；macOS 按用户要求延至整个项目完成后检查。未进入阶段 3。** 本轮使用 goal 模式，基于 Git `1fc4658` 的工作区增量；源码及二进制 SHA-256 见本地结果目录 `manifest.json`，不把未提交变更描述为已有提交。

## 成果

- 正式应用动态链接固定 PDFium 157.0.8100.0（沿用阶段 0 许可与来源证据），Qt 6.8.3 / C++20 / CMake；无新增产品第三方依赖。
- 可通过系统文件对话框或命令行打开、切换、关闭本地 PDF。当前页先显示 480 像素宽预览，再替换按窗口页宽与 DPR 请求的清晰图像。提供上一页/下一页、可输入页码、页数、适应页宽及页内垂直滚动。
- 文档管理位于 ShellState，唯一 PDFium worker 按需创建库、只读 QFile 回调与持续文档会话；只加载当前页信息。QML 不访问原生句柄。页面/位图用 RAII 释放，跨线程传递 QImage 不可变快照。
- session/revision 与 latest-only 邮箱处理排队需求替换、在途过期结果丢弃；接收端再次检查。关闭窗口先异步清理原生资源再结束线程。同步 PDFium 调用不能被强制中断，繁重页面仍可能延迟切换/退出；不把结果丢弃称为底层中止。
- PageItem 用公开场景图 API，在渲染执行域创建/释放纹理；资源失效后从 CPU 图像重建。默认 D3D11、浅深主题及高 DPI 保留。每张图像最多 64 MiB、每边最多 8192；超预算降低分辨率并反馈，无法容纳的极端比例明确失败。
- 密码、损坏/不受支持的安全格式、文件访问、大小、页面与渲染错误分类。空/不可读输入不会静默显示成功。Windows 文件接口明确限制在 4 GiB 以内；密码输入不属于本阶段，受保护文件明确提示。

架构契约、路线图、README、构建说明、性能口径和依赖审查已同步。Windows CI 配置增加固定 PDFium 的下载散列校验和真实阅读器测试；未声称远程工作流已执行。

## 实际验证

证据根目录：`benchmarks/results/phase2-20261015/`（本地保存、Git 忽略，无真实教材内容）。环境：Windows 11 Pro 10.0.26200、Core Ultra 7 265K / 20 逻辑核、50,873,622,528 bytes 系统内存、RTX 5070 驱动 32.0.16.1692，另有 Intel Graphics；环境 JSON 保留实际枚举。MSVC 19.51.36257.0 / Windows SDK 10.0.26100.0、Qt 6.8.3 MSVC2022 动态库、CMake 3.31.6 / Ninja 1.11.1.3、Release。未把显卡枚举当作所有显示输出的独立证明。

| 检查 | 实际结果 | 证据 |
| --- | --- | --- |
| 新构建目录 `build/phase2` | 配置、编译、链接和 CTest 2/2 通过；后续修复继续验证 | `build-test.log`、`budget-fixed.log` |
| 默认正式构建 `./tools/build-windows.ps1 -Test` | 原型 OFF，CTest 2/2；交互套件含 init/cleanup 共 7 passed、0 failed、0 skipped | `final-build-test.log`、`final-interactions.txt` |
| 保留原型回归 `./tools/build-windows.ps1 -Phase0 -Test` | 正式测试与 phase0_contracts 共 3/3 通过 | `default-build-regression.log` |
| 最终全新构建 `build/phase2-final` | 配置/编译/链接与 CTest 2/2；7 passed、0 failed、0 skipped | `clean-final-build.log`、`clean-final-update.log`、`clean-final-interactions.txt` |
| QML lint | 在 MSVC 开发环境中执行 all_qmllint，退出 0、无 QML 诊断 | `clean-final-qmllint.log` |
| Unicode/只读文件与原文件保护 | 中文大写扩展名路径真实打开、只读复制件、错误/关闭后原始及复制件 SHA-256 不变 | documentLifecycle、selectionBoundary |
| 真实 PDF 内容 | 原创 8 页文字/色卡验证红绿蓝像素、固有 90° 旋转；image-only scan.pdf 显示；password.pdf 分类 password，损坏文件分类 format | documentLifecycle |
| 分配上限 | geometry 和 scan 在请求 8192 时返回非空图像、无错误且实际 bytes ≤ 64 MiB；先复现取整缺陷，再修复 | `budget-reproduction.log`、`budget-fixed.log` |
| 文件切换与过期交付 | 预览交付时 A→B；最终 B 为正确单页，明确确认 discarded 1；关闭等待本次新增 worker 确认且页面保持为空；shutdownOnPreview 另确认 worker 丢弃在途请求后正常退出 | final-interactions |
| 50 次开关 | 每次真实打开并完成后关闭，等待实际 RAII 文档/页/位图句柄计数为 0；shutdown 后同样归零 | final-interactions |
| 场景图与高 DPI | 默认 DPR 1.5、注入 QT_SCALE_FACTOR=2 后有效 DPR 3，graphics API=4（D3D11）；销毁场景图并重显后窗口截图仍包含页面红/蓝像素；浅深截图实际查看 | `visual/`、`visual-dpi2/`、clean-final-interactions |
| 大文件按需读取 | 本地原创 raw 图像 PDF：839,214,902 bytes、800 页；首页/末页真实显示、线程正常退出 | largeFileSmoke；散列见 manifest |
| 依赖获取增量 | `bootstrap.py --platform windows --pdfium-only` 实际核对锁定归档 SHA-256 并解压成功 | 终端记录；dependencies.lock.json |
| 独立代码审查 | 发现预算整数取整和失效测试缺口两个 P2，补回归复现并修复；只读复查未留 P1/P2 | 审查对话与复现日志 |

最终全新目录的高 DPI 注入实际 DPR 是 **3**（系统 1.5 × 环境 2），不是 DPR 2；结果目录名 `visual-dpi2` 表示注入值，不能据此声称 DPR 2 实机测试。截图只含原创色卡。原生 OS 文件对话框点击选取未在本轮人工复验，Qt 对话框取消/重开及 accepted 信号到真实引擎的路由已覆盖。

构建仍有 Qt 6.8.3 头文件在 qmlcachegen 生成代码实例化时产生的 MSVC C4702 unreachable-code 警告；未修改供应商头文件或关闭全局警告。曾单独在未初始化 MSVC 环境的 shell 执行 lint，触发依赖重建缺少 type_traits；随后在正确开发环境重执行成功。该失败的终端输出保留，不当作产品编译缺陷或通过记录。

## 内存与性能证据边界

默认正式回归在 50 次循环第 1/10/25/50 次关闭后记录 CPU private bytes 为 13,451,264 / 13,463,552 / 13,520,896 / 13,561,856；working set 为 38,129,664 / 38,150,144 / 38,227,968 / 38,268,928。进程此前还运行过大样本及 64 MiB 边界用例，peak working set 为 117,489,664。第 1 到第 50 次 private 增加 110,592 bytes，没有每次保留整张图像的增长迹象；短循环不等于长期泄漏证明，系统内存和引擎内部缓存也不等于应用句柄数。

大样本最终默认回归单次首页预览/目标累计 worker 时间 3.719/9.789 ms，末页 12.680/18.994 ms。早期运行分别出现打开约 375.805 ms 和末页约 2.9 s，说明文件缓存/IO 状态会显著改变结果。本轮仅做功能和资源回归，没有受控冷启动、统计性能基线或 A/B 优化实验，不能据此承诺首页/启动耗时或 60 FPS。原始日志和口径见 PERFORMANCE.md。

图像预算只限制应用单张 CPU 缓冲，不约束 PDFium 解码峰值、进程总内存或 GPU 实际显存。阶段 3 才实现正式缓存与调度，现有日志纹理字节是尺寸估算；GPU 显存、呈现帧时间 P95/P99 和缓存命中率未测。

## 已知限制与下一步

macOS、远程 CI、ASan/UBSan 未执行；未制作发行包或宣称许可证闭环。合成 800 页样本没有 JPEG/JPX 解压，真实大压缩扫描教材和损坏文件的更广覆盖仍需后续性能验收。复杂交互表单/XFA、密码输入不在当前能力中，PDFium 可能无法完整呈现需要专用表单环境的文件；普通扫描/文字页为本阶段目标。

尚未实现连续页面虚拟布局、缩放操作、多页缓存、原生文字选择、标注、自动保存与产品导出。阶段 0 的字体/导出/恢复风险按既有报告继续跟踪。本轮停止在阶段 2；下一步建议在获得授权后推进阶段 3 的连续布局、虚拟滚动、调度与缓存，并用真实压缩扫描教材建立可重复基线。
