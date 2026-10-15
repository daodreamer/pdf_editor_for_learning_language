# 阶段 7 Windows 交付报告

日期：2026-10-15。用户授权以 goal 模式完成阶段 7；macOS 实机检查按用户要求推迟到项目完成后。本轮不进入阶段 8。

## 完成内容

- 独立低优先级文件线程执行全内容 SHA-256、恢复读取、校验、JSON 编解码及原子保存。750 ms 去抖与 5 s 最长调度间隔，状态按实际落盘修订更新。
- 内容身份工作区支持移动后恢复，相同内容副本共享工作区；不同内容隔离，路径内容变化提示。QLockFile 防止多个进程同时写同一练习。
- schema 1、payload 校验、前一有效快照备份、损坏证据保留、未来版本只读；正常关闭/换文档异步刷新，失败留在原窗口重试。空文字编辑结束删除、恢复后两类标注叠放顺序均有持久化契约。
- 导出固定标注修订至新 PDF，允许继续编辑；唯一 PDFium 线程使用独立原文档会话，保留原内容和图像。拒绝已有目标、原路径和硬链接别名；临时写入、回读及源文件再次散列后提交。
- Qt 排版字形/基线 + PDFium 显式 CIDToGIDMap/ToUnicode 与字体嵌入，支持受控 Noto 字体中的中英文字、组合字符、多行和换行；笔迹保持圆端矢量、颜色、粗细与透明度。旋转、CropBox 和 UserUnit 使用现有原始坐标契约。
- 新增格式说明和独立文件验证脚本，更新 README、架构、路线图、构建及字体许可记录。

未引入新的产品库。沿用 C++20、Qt 6.8.3、PDFium 157.0.8100.0、MSVC x64 Release；新增部署既有固定来源的 OFL Noto Sans SC 字体和许可证。完整字体压缩嵌入一次，输出约增加 6 MiB。完整发行许可闭包、安装包、macOS bundle 与签名留在阶段 8，不宣称本轮完成整个发行包合规确认。

实现取舍：实测 Qt 6.8.3 Windows 的 QPdfWriter 会将应用字体文字导出为轮廓，不能提取，因此只让它输出笔迹矢量层；文字经 Qt 的 glyphRuns 得到位置后由 PDFium 写入。显式 Unicode 映射也避免了阶段 0 自动 ToUnicode 对空格的歧义。接口和所有权依据为本地锁定 PDFium 头文件，以及 [Qt QTextLayout](https://doc.qt.io/qt-6.8/qtextlayout.html)、[QSaveFile](https://doc.qt.io/qt-6.8/qsavefile.html)；Qt 源码对轮廓分支的依据为 `qtbase v6.8.3/src/gui/painting/qpdf.cpp` 的 `face_id.filename.isEmpty()` 检查。

## 实际验证

原始结果保留在本地 `benchmarks/results/phase7-20261015/`，按仓库规则不默认提交。最终标准导出样本在 `release-accepted/`，大文件结果在 `large/`；较早失败实验同样保留，不作为通过证据。

| 检查 | 结果与证据 |
| --- | --- |
| Windows Release 构建与完整回归 | `./tools/build-windows.ps1 -BuildDir build/windows -Test`；7/7 CTest 通过，最后完整回归记录 `build/phase7-release-final2.log`。其后只增加可选 PDFium 图片证据采集，重建见 `build/phase7-artifact-build.log`，`release-accepted/storage-tests.txt` 全部标准用例通过 |
| 标准阶段 7 套件 | 9 passed、0 failed；大文件可选项在标准套件中明确 skipped，随后单独实际执行通过。包含编解码/非法数据、备份回退与损坏文件保留、未来 schema、排他锁、真实提交失败、空文字删除、自动保存、移动与内容改变 |
| 真实崩溃恢复 | 子进程在最后一次自动保存后被 kill，新进程从同一工作区恢复；另一个子进程在 QSaveFile 部分写入后被 kill，旧主文件字节不变。新进程同时验证死亡进程锁可回收 |
| 关闭失败保护 | 用备份路径上的目录注入真实写入失败；关闭不继续、文字仍在内存；解除故障后关闭保存并成功退出 |
| 原文件与输出保护 | 原路径、已有输出、同盘真实硬链接均被拒绝；导出前后原文件 SHA-256 不变；不支持字符明确失败且没有目标文件 |
| 导出期间编辑 | 输出保留导出起点修订，不含之后输入；GUI 定时器在后台任务期间持续处理。成功状态注明修订 |
| PDFium 回读与显示 | 8 页逐页提取中文；保留 0/90/180/270 度、UserUnit 1/2；`pdfium-page-*.png` 为正式渲染路径，`reader-before-export.png` 为真实 QML 窗口 |
| 独立 MuPDF | `tools/verify-phase7-export.py` 通过：8 页 Unicode NFC 对照、字体流嵌入、笔迹颜色/alpha、原图像数量、旋转和物理尺寸；稀疏导出中 7 个未修改页的内容流与渲染像素完全相同。原始 JSON 与四页图片在 `release-accepted/` |
| 高 DPI | `QT_SCALE_FACTOR=2` 独立真实窗口导出通过，并经 MuPDF 再验；八页文字内容、基线原点和边界与默认缩放输出完全一致，见 `high-dpi/dpi-comparison.json`。这不是物理跨屏验收 |
| 代码审查 | 按 requesting-code-review 技能独立只读复核；修复清空文字漏报修订、工作区未就绪时错误禁用阅读，并复核最终字体/坐标/所有权路径 |
| MSVC AddressSanitizer | 独立 `build/phase7-asan` Release + `/fsanitize=address`，阶段 7 套件通过；见 `build/phase7-asan-final-build.log`、`build/phase7-asan-final-ctest.log`。预编译 Qt/PDFium 未被插桩，不能据此覆盖其内部；本次未启用调试信息，保留 C5072 提示；MSVC 未运行 UBSan |

可选大文件执行方式：Qt SDK PATH 下，设置 `LANGLERN_LARGE_PDF` 为 `.deps/samples/synthetic-800.pdf`，设置 `LANGLERN_PHASE7_ARTIFACT_DIR` 为新的已存在目录，运行 `build/windows/storage_tests.exe largeDocumentStorage -o <日志>,txt`。测试内容为打开、身份校验、小量答案保存、真实大文件导出、关闭重开恢复。实际结果如下。

## 大文件与计时

设备：Windows 11 Pro 10.0.26200，Intel Core Ultra 7 265K，NVIDIA RTX 5070 / Intel Graphics；Qt 6.8.3、MSVC Release。本阶段没有重新测量滚动 FPS、系统 GPU 显存或进程峰值，不宣称整体性能验收通过。

| 项目 | 实测 |
| --- | --- |
| 样本 | 839214902 bytes，800 页；已有原创 synthetic-800，SHA-256 `be2b255dbc4aa8a776248ebd03a795006cd450014ab554864efd6edf36b5e2c8` |
| 首张 CPU 图像到 GUI 回调 | 62.146 ms；这是图像可用代理，非屏幕首次呈现时间 |
| 全身份校验/工作区可编辑 | 8078.679 ms；校验前可阅读，练习编辑等待恢复 |
| 显式保存到成功回调 | 51.290 ms；含测试事件轮询粒度，不是纯磁盘耗时 |
| 大文件导出到成功回调 | 24573.139 ms；包含两次身份散列、字体准备、复制保存及回读，不是单个 PDFium API 耗时 |
| 输出 | 845821740 bytes，800 页；增加 6606838 bytes |
| GUI 心跳 | 打开至导出结束处理 3638 次 1 ms 请求定时器；仅证明事件循环持续处理，不代表帧率/输入延迟 |
| 独立大文件回读 | MuPDF 提取首页新增中文；末页原内容流和渲染像素保持相同；源/输出散列见 `large/independent-verification.json` |

标准 8 页样本在真实窗口证据运行中的导出总时间为 258.516 ms，期间 27 次 GUI 心跳；单次诊断，非性能优化 A/B。上述大文件仅运行一次，不能外推到真实压缩教材、冷缓存或其他设备。阶段 3 启动未达目标与长期滚动待办继续保留。

源码、正式 exe、测试 exe、字体与标准样本散列集中记录于本轮 `manifest.json`；构建与 ASan 日志副本也保留在同一结果根目录。工作区未自动提交或推送。

## 限制和下一步

异常结束只恢复最后成功提交修订，不承诺零丢失。未模拟真实断电或把系统盘填满；写入失败由真实文件提交故障及 Qt 的原子写入路径验证。数据文件不是加密容器。

导出字库只覆盖当前 Noto 支持的字符，缺失字符明确拒绝，不自动复制或嵌入系统字体。大量标注的长期内存、完整字体体积、复杂脚本/双向文字、极端长文本与大文件保存调度仍应在阶段 8 扩充压力数据；不承诺任意文字体系与第三方阅读顺序完全一致。用户若在会话中外部修改原 PDF，保存/导出会明确失败，需恢复原文件后重试；不自动把答案迁移到新内容。

macOS/Metal、物理设备与原生 IME 人工检查、真实跨屏、干净系统部署及最终许可闭包仍待后续验收。下一阶段建议为阶段 8 的性能与交互、Windows 打包，以及项目完成后的 macOS 检查；本轮未自动推进。
