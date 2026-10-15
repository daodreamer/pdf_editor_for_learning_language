# 阶段 4 Windows 验证报告

日期：2026-10-15。用户授权使用 goal 模式完成阶段 4；macOS 实机检查延至整个项目完成后。阶段 5–8 未实施。

## 完成内容

Windows 范围的原生 PDF 文字选择与复制已完成：按页异步文字快照、命中、正反向跨行/跨页拖选、上下边缘自动滚动、独立场景图高亮、Ctrl+C/复制按钮、Esc 清空和已知空白拖动滚动。扫描页明确提示没有原生文字，不提供 OCR。文字与底图分离，拖选不重新解码或上传页面纹理。

继续使用唯一 PDFium 工作线程、session/revision 失效拒绝及 RAII。文字快照预算 16 MiB，每页最多 65536 个原生字符索引，一次最多 128 页；失败/超限明确提示并禁止复制旧内容，不静默截断。选区离开可见邻域仍按需读取，但不持有整本文字句柄。预算与额外在途/几何分配口径见 ARCHITECTURE.md 阶段 4 补充。

Qt 6.8.3 / PDFium 157.0.8100.0，MSVC x64 Release，未增加产品依赖。新增原创文字、上限测试 PDF，以及嵌入锁定 OFL Noto 字体子集的中文测试 PDF；来源与许可证见 third_party/DEPENDENCIES.md。

## 实际构建与测试

证据目录：`benchmarks/results/phase4-20261015/`（原始运行由 Git 忽略）。默认运行目录 `build/windows` 已更新。

| 检查 | 结果及证据 |
| --- | --- |
| 新目录构建 | `./tools/build-windows.ps1 -BuildDir build/phase4 -Test` 配置/编译/链接成功；final-build-test.log 的当时 CTest 4/4 通过 |
| 默认构建 | `./tools/build-windows.ps1 -BuildDir build/windows -Test` 配置/编译/链接成功；最终上限样本修正后以 accepted-ctest.log 为准，4/4 通过 |
| 最终完整回归 | accepted-ctest.log，17.73 s，设置 LANGLERN_LARGE_PDF 为既有 839214902 bytes/800 页 synthetic-800.pdf，执行大文件 smoke；未启动可选长压力 |
| 独立文字套件 | accepted-text-tests.log：9 passed、0 failed、0 skipped；含初始化/清理，7 个测试方法 |
| QML | final-qmllint.log，all_qmllint 退出 0，无 QML 诊断 |
| 图形后端 | final-text-tests.log 实际记录 threaded render loop、QRhi D3D11、NVIDIA RTX 5070 adapter；graphics-backend.log 为重定向输出配套文件 |
| 高 DPI | dpi2-tests.log，鼠标/剪贴板/跨页/边缘及几何测试通过；QT_SCALE_FACTOR=2，截图宽 3360 像素对应 1120 逻辑像素窗口宽，实际 DPR 3（高度由可用桌面空间约束）；普通截图 DPR 1.5 |
| 像素边界 | 正常与附加 DPI 窗口均对选区前后截图作像素差；视口所有变化像素位于投影字符框外扩 1 设备像素内，并确认实际发生高亮变化；8 个旋转/UserUnit 页另有几何与内容测试 |
| 人工视觉复核 | visual-final/ 与 visual-dpi2/ 的英文/真实中文选区截图已目视检查；中文由真实嵌入字体绘制，不以 ToUnicode 映射测试替代字形检查 |
| 独立只读审查 | 修复下列 3 项 P2 后复核，无剩余已确认 P1/P2；审查未独立运行测试，不替代上述执行记录 |

覆盖内容包括：0/90/180/270° 固有旋转、UserUnit 1/2、非零 MediaBox/CropBox；跨行/跨页正反选、裁剪区外隐藏字、空白/扫描页、中文/emoji 代理对/组合字符/连字展开、Qt 标准剪贴板、换文档/关闭的失效结果、句柄归零、缓存预算和真实超限页。标准英文复制为 `Hello World`；真实中文样本复制含 `中文练习 Bonjour`（仅测试比较时归一字体原有 NBSP，实际剪贴板保留原值）。

审查发现并修复：文字任务在加载页失败时没有独立分类导致反复重试；全视口 MouseArea 阻断空白拖动；终页全为裁剪区外字符时丢失前页有效选区。三项均补回归。原始超限样本单个 65537 字符字符串被 PDFium 限制到 32767 个索引，未触发应用上限；改为三个 30000 字符文本对象，真实验证应用拒绝该页。开发失败日志保留，不把失败计为通过。

编译曾出现 Qt 生成 QML 缓存/Qt 头内 C4702 unreachable code 警告；没有把此构建称为零警告。WrapVulkanHeaders 缺失不阻断本机 D3D11。ASan/UBSan、远程 CI、macOS 均未执行。

## 性能与边界

环境见 environment.json：Windows 11 Pro 10.0.26200，Core Ultra 7 265K，约 47.4 GiB RAM，RTX 5070，平衡电源；固定 Qt/PDFium/MSVC Release。沿用阶段 3 同设备，未重做 30 分钟压力、系统 GPU 峰值或启动基线。

accepted-text-tests.log 中，65536 个合成原生字符的快照计费 4194336 bytes，100 次 GUI 选区更新合计 314.949 ms，平均约 3.15 ms/次。这是一次微型诊断，包含命中/字符串生成，不含 OS 输入到屏幕延迟、QML/SG 全链路，也不是 P95/P99 或 A/B 改善证据。真实 90000 字符超限页文字任务约 25.283 ms 后返回明确超限；PDFium LoadPage/Text_LoadPage/CountChars 内部分配及时间不受应用快照上限保证，底层同步调用仍不可立即抢占。

阶段 3 的热启动 <300 ms 未达、真实压缩大型教材/物理触控板/跨屏 DPR/高刷新率输入呈现、进程内存分配归因等待办原样保留。未据本轮短测试宣布整体 60 FPS、无泄漏或总内存上限达标。

复杂多栏、竖排、缺失/错误 ToUnicode 映射遵循 PDFium 返回流，不保证语义阅读顺序；固有旋转可能改变该顺序。同框展开按字形整体选择，不单独选择半个 emoji 或半个连字。没有实现 OCR、用户新增页面旋转、文本编辑、标注、保存或导出。macOS 按用户安排待项目完成后检查。

下一步为阶段 5 的文字答案编辑与坐标/输入仲裁，需要用户进一步授权。
