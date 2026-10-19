# Pocket TTS 法语 Estelle

用户最终选择截图中的 Pocket TTS / French / Estelle，覆盖之前 1.6B / CML 12977。法语自然朗读已接入页面原生/OCR 选区及答案文本框选区，全部在本地 CPU 运行，不需 Windows 法语 TTS 包、账号、云 API、Python/PyTorch、CUDA 或运行时联网下载。英语等其他声音保留 SAPI 路径。

## 使用与实现

选择文字点击“朗读”；“··· → OCR 与朗读设置”默认法语，声音列表提供 Estelle，底部朗读条可暂停/继续/停止，语速 0.5–2×且保持音调。首次点击校验资源并加载模型，有准备提示；模型保留供后续朗读，空闲三分钟后卸载。窗口关闭先取消声音，再按正确线程释放模型。选区文字不会因朗读被改写。

独立 C++20 适配器使用 ONNX Runtime 普通 Run 和动态状态，不共享可变输入/输出；每段最多 50 token，逐帧生成/解码，超限未完成 EOS 时报告错误。2 MiB PCM 队列接独占 WinMM/SoundTouch 播放线程；暂停停止独立于推理，已经不能中断的模型调用仍需完成并丢弃过期结果。首段 PCM 时间不是声卡实际呈现时刻；停止完成测量包括后台返回，不是声学延迟。

新模型固定为 Vincweb `84541283b060685dbc7f9f9d8066f4b00235820c` 的法语 6 层/约 100M 参数导出。官方权重 `kyutai/pocket-tts-without-voice-cloning` 提交 `1e08e6a23401048648a9fdcfde2f89348215c2a7`，新法语词表 `8843db76457a91db32077edf8dfcd1c0e3e755fd`。Estelle 为同版官方预计算 KV，原录音 `unmute-prod-website/developpeuse-3.wav`。不再编码参考 WAV，不实现任意声音克隆。

## 依赖、许可与维护

| 组件 | 用途、维护及分发 |
| --- | --- |
| 项目原生适配器 | MIT；C++20、既有 Qt Core 做 Unicode/元数据；独立实现图调用契约，不复制第三方无单独授权的导出脚本 |
| Kyutai 模型 / Vincweb 转换 | CC BY 4.0；保留 Kyutai、Vincweb、原始版本、ONNX/INT8 适配说明；更新必须同步模型、词表、语音状态和状态清单 |
| Estelle | 官方录音 CC0，预计算状态固定来源记录在模型 manifest；保留原录音声明和格式转换归属 |
| ONNX Runtime 1.23.2 CPU | MIT 及实际二进制 ThirdPartyNotices；独立 DLL，更新重新验收状态/内存/PCM |
| SentencePiece 0.2.1 | Apache-2.0 及嵌入 protobuf-lite/abseil 通知；静态链接推理 DLL |
| SoundTouch 2.3.3 | LGPL-2.1-or-later，独立可替换 DLL；随包原始源码 tar、许可证及项目修改/构建脚本，允许调试和重新链接 |

`third_party/pocket.lock.json` 固定所有下载文件字节数、SHA-256、源码和通知。SoundTouch 唯一源码修改为资源编译头从 afxres.h 改为 Windows SDK winresrc.h。JSON Unigram 词表的编号/分数/byte fallback 保持不变，转换为本地 SentencePiece 格式（identity、无 NFKC）；派生文件也锁定散列，运行内置清单校验，不信任随包清单来改变预期值。自然语言 Unicode token 已用官方 tokenizers 对照 500 组输入，重音/NFD/NBSP/多空格/换行/emoji/中英/数字混排均一致，证据 tokenizer-parity.json。特殊模型控制标记字面量按普通文字处理，不作为控制 token 注入。

所有通知和模型随包；旧核心/dr_libs 通知仅留作历史审查材料，当前不链接或运行这些代码。许可证逐项记录不等于整个发行包合规确认，Qt/PDFium/词库既有待审项保留。macOS 自然朗读未实现/未验收。

## 修复证据与本机验证

旧 lookbe 法语导出 + 社区 PocketTTS.cpp 虽可生成 WAV，但真实短句出现 0.32 秒过早 EOS 或静音，不能因按钮工作就视为完成。独立 Python/ORT 参考复现；官方 6 帧 EOS 保护、参考录音末尾静音、采样温度及阈值调整不能稳定修复，双缓冲实验排除了当时的别名假设。没有用补音频时长或更换测试句掩盖问题。改用对应官方新权重的更小导出和预计算 Estelle 后，旧异常短句重复通过；失败日志保留在 benchmarks/results/pocket-review-short-20261019。

Windows 11 build 26200 / Core Ultra 7 265K（20 核）/ 47.4 GiB 可用物理内存 / MSVC 19.51 / Qt 6.8.3 Release。`natural_speech_tests` 验证实际模型、非静音有限 PCM、中文路径、损坏资源拒绝、重复短句与复用、真实 WinMM 播放、暂停稳定、继续/1.5×、停止、有界队列和关闭；固定真实教材第 34 页 OCR 后点击页面朗读，答案文本框选区朗读保持焦点。原教材 SHA-256 前后相同；诊断和截图仅放 ignored results。

三轮独立进程每轮完成 9 次实际短句生成及真实教材/答案按钮播放，无失败/跳过。`benchmarks/run-speech.py --run benchmarks/results/<新目录> --repeat 3` 可复现，记录 CPU 时间、进程内存及模块路径；本次证据 pocket-estelle-20261019-03。OS 文件缓存热态，不代表机器冷启动；表中进程内存包含 Qt、PDF、OCR、图像等，不能当作语音独占内存。

| 轮次 | 首请求模型加载 ms | 首段 PCM ms（含加载） | 已加载模型首段 PCM 范围 ms | 进程峰值 RSS / private MiB |
| --- | ---: | ---: | ---: | ---: |
| 1 | 1185.656 | 1217.569 | 29.383–37.091 | 431.6 / 420.0 |
| 2 | 1235.288 | 1275.612 | 30.017–38.689 | 433.4 / 422.9 |
| 3 | 1230.034 | 1269.091 | 29.226–37.104 | 435.1 / 424.4 |

停止测试后台完成为 62 ms；这是一轮实际结果，不是任意设备延迟承诺。模型/词表/声音共 121016074 字节（约 115.4 MiB），当前完整语音目录约 130.4 MiB（包含 DLL、许可证、SoundTouch 源码），无需下载整套 Python 或 GPU 依赖。

独立 Whisper tiny/base 对生成 PCM 做回读：完整法语句子及截图句子均能复述，极短专名短语的 ASR 有误识别，因此不宣称逐字准确或以 ASR 代替人耳自然度评价。TTS 自身可能误读专名/孤立短词，OCR 错误也会照读；保留实际 WAV 供用户试听。未执行长时间压力、sanitizer、GPU 显存/声卡呈现延迟、干净 VM、macOS 或远端 CI；没有推送。

最终 Release 全量 CTest 10/10 组通过（105.61 s），natural_speech 6 项通过、无跳过，QML lint、Python 工具语法和 git diff --check 通过。500 例 tokenizer 对照与独立 12 次短句生成通过，1 MiB 选区首回调取消后可继续请求。最终运行包另做 SDK 隔离验证，结果补记在本节后。

最终本地候选 `build/packages/Pocket-Estelle-20261019-final.zip` 为 165664033 字节（约 158.0 MiB），SHA-256 `366c2908afa9c8d15976e9ce3e45aa9e2dcca086d0565299226b138937db6487`。本包已包含模型、所有依赖与 OCR/词库；解压整个目录运行 langlern.exe。打包校验模型清单、编译来源及 DLL；SDK 隔离运行结果待下段补记。

启动 A/B 使用每版本 3 次丢弃预热 + 3 个独立进程测量、交替顺序：新包同源构建窗口/合成 F6 往返代理中位 500.32 ms，旧 OCR 候选 516.48 ms，不能用这三个热态样本宣称性能提升或冷启动达标。启动未加载语音模型，证据 pocket-estelle-20261019-04/startup。

最终 SDK 隔离验证通过：仅 Windows 系统 PATH，产品实际启动、Qt 模块均来自候选目录，逐文件/源码快照校验 1440 项及 9/9 实际运行套件成功；natural_speech 6 项通过、无跳过，包含固定真实教材和关闭中朗读。证据 pocket-estelle-20261019-04/package-validation/summary.json。此为开发机独立运行目录，不是干净虚拟机。包内源码快照保留生成包时的记录；本次体积、启动及隔离结果在生成包后补记，产品代码相同。

提交前复核：用户已删除旧 `.deps/kyutai-probe`（约 8.7 GB），当前程序不引用该目录。发现早期 CC0 通知只保存了上游跳转文件名，现补为 Creative Commons 官方 CC0 完整法律正文（11404 字节），锁文件同步更新来源及散列；旧 final.zip 保留为历史产物，后续 reviewed/CI 包包含此修正。此次为许可材料修正，不改变模型、声音或交互。
