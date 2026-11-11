# Supertonic 3 法语自然朗读（男声 M1 / 女声 F3）

2026-11-11 用户反馈原 Pocket TTS / Estelle “声音太小、听起来不太清楚”，要求找免费、体积不大、运行快的替代模型，先做法语。经同句 A/B 评测后用户指定 Supertonic 3 的男声 M1 和女声 F3，默认男声，可在设置中切换。Estelle 实现、模型和通知已移除，历史记录见 git（提交 `3e9a384` 之前的本文件）。

## 使用

选择页面原生/OCR 文字或答案文本框文字后点击“朗读”。“··· → OCR 与朗读设置”中朗读语言为法语时，声音列表为“男声 · 自然法语（离线）”和“女声 · 自然法语（离线）”，默认男声；选择保存在书架数据 `library-v1.json` 的 `frenchVoice`，重启后保留。底部朗读条可暂停/继续/停止，语速 0.5–2× 保持音调。首次朗读校验并加载模型（约 1.7 s，有“正在准备声音…”提示），空闲三分钟卸载。其他语言仍使用系统 SAPI 声音。所有朗读均为本机 CPU 合成，属于机器生成语音。

## 选型评测（2026-11-11，本机）

同 8 句法语教材风格句子（含 2–3 词短句），Whisper base 回读计算词错率（WER），单线程 CPU；音频与指标在 `benchmarks/results/tts-fr-eval-20261111*`（未提交）。WER 只用于发现明显误读，不替代人耳评价。

| 候选 | 随包体积 | RTF（生成/音频） | WER | 原始有效响度 | 结论 |
| --- | ---: | ---: | ---: | ---: | --- |
| Pocket TTS / Estelle（原方案） | 115 MiB | 0.30 | 20.7% | −19.9 dBFS | “Bonjour”→“Aujourd'hui”、“pleuvoir”→“plus voir”等误读；语速偏快 |
| **Supertonic 3（随包派生）** | **约 131 MiB** | **0.21（4 线程 0.14）** | **0–1.6%** | −24 dBFS（归一前） | 采用 |
| Supertonic 3 原始 fp32 | 383 MiB | 0.37 | 0–2% | −24 dBFS | 体积过大 |
| Supertonic 3 全 int8（含声码器） | 104 MiB | 0.16 | 75–100% | — | 声音损坏，淘汰 |
| Supertonic 2 | 263 MiB | 0.13 | 11.6% | −26.8 dBFS | 不如 3 |
| Piper fr_FR siwis medium | 63 MiB | 0.02 | 7.9% | −13.9 dBFS | 需 GPL-3.0 espeak-ng 音素化 |
| Kokoro-82M ff_siwis | 92 MiB | 0.14 | 16.9% | −18.5 dBFS | 需 espeak-ng；法语一般 |

Piper/Kokoro 依赖 espeak-ng（GPL-3.0），与项目 MIT 分发方式冲突，且清晰度不及 Supertonic 3；Piper tom 声线数据为 AGPL，未纳入。评测表中 Supertonic 3 随包行为 ONNX Runtime 1.23.2 实测；最初在 Python ORT 1.31 上验证的 int8 逐通道方案在 1.23.2 缺少 int8 权重 ConvInteger 内核，改为 uint8 逐通道后重新评测。

## 实现

- `src/speech/supertonic_backend.cpp` → `langlern_speech.dll`：ONNX Runtime CPU 四图流程（时长预测 → 文本编码 → 8 步流匹配 → 声码器），语速系数 1.05，按名称绑定图输入。文本 NFKD 规范化、去 emoji、标点/空格修正、补句末标点并加 `<fr>…</fr>` 标签，字符经随包 `unicode_indexer.json` 映射，未知字符丢弃。逻辑为 Supertone MIT 参考代码的独立 C++ 移植。
- 切分：每句一段（超过 200 字符先按逗号/分号、再按空格切），句间 0.3 s 静音。单句最长 30 s，超出报错提示分段选择。
- 响度：每句独立归一到 −16 dBFS 有效 RMS（20 ms 帧、−45 dBFS 门限、增益 0.25–8×），零相位 3 ms 前瞻峰值限幅到 −1 dBFS（50 ms 释放）。
- `src/app/naturalspeech.*`：在既有 speechThread 延迟加载 DLL、校验内嵌锁文件中全部模型/声线散列、创建模型；PCM 以 ≤4096 样本切片经 2 MiB 有界队列交给独立 WinMM/SoundTouch 播放线程（44.1 kHz 单声道）。推理线程数 min(4, 逻辑核/2)。每次图调用之间轮询请求版本；正在执行的单次图调用不能中断，停止后丢弃其结果。
- 服务层 `OfflineServices`：声音 id `natural:fr:male`/`natural:fr:female`，`frenchVoice` 偏好；`WorkspaceState.frenchVoice` 持久化。

## 依赖、许可与派生

| 组件 | 用途与分发 |
| --- | --- |
| Supertonic 3（Supertone/supertonic-3 @ `3cadd1ee6394adea1bd021217a0e650ede09a323`） | 模型与 M1/F3 声线；OpenRAIL-M。允许免费使用、修改、再分发，但 Attachment A 使用限制必须随附并约束接收者，修改文件须显著标注。不是 OSI 开源许可 |
| 派生图 | `vector_estimator` 为 ORT 1.23.2 动态量化 uint8 逐通道（MatMul/Gemm/Conv）；其余三图仅把大权重存为 fp16 并插入 Cast，ORT 建会话时折叠，计算仍为 fp32。修改说明 `notices/speech/Supertonic-modifications.txt` |
| Supertonic 参考代码 @ `1e9799e964ea4c0dad7cde993b65c3c813a7b373` | MIT；文本处理移植依据 |
| ONNX Runtime 1.23.2 CPU | MIT + ThirdPartyNotices；独立 DLL |
| SoundTouch 2.3.3 | LGPL-2.1-or-later；独立可替换 DLL，源码 tar 随包 |

`third_party/speech.lock.json` 固定上游文件、派生文件和通知的字节数与 SHA-256。派生由 `tools/bootstrap-speech.py` 在隔离 venv 中用 `tools/requirements-speech.lock.txt` 固定版本执行，两次独立派生逐字节一致；任一散列不符即构建失败。整个发行包合规仍以 DEPENDENCIES.md 的既有待审项为准。

## 本机验证（2026-11-11）

Windows 11 / Core Ultra 7 265K（20 逻辑核）/ MSVC 19.51 / Qt 6.8.3 Release。

- `natural_speech_tests` 7 项全部通过、无跳过（`benchmarks/results/supertonic-20261111-02`）：两种声音真实 PCM、44.1 kHz、响度 −16.47 / −16.29 dBFS、峰值 −1.00 / −2.52 dBFS；中文路径；未知声线与损坏声线文件拒绝；真实 WinMM 播放、暂停稳定、继续 1.5×、停止后台完成 124 ms；男女声交替重复短句 9 次；偏好持久化；固定真实教材第 34 页 OCR 选区默认男声朗读、设置切到女声后答案文本框朗读，原教材 SHA-256 前后一致。
- 直接调用随包 DLL 对 3 句 × 2 声 × 6 次重复生成做 Whisper 回读，36/36 次均复述正确（个别专名 “Lyon” 听写有出入）；早先一次 “Bonjour !” 单独成句被回读为 “Bore !”，重复 12 次未再出现，视为偶发，保留样本供人耳复核。
- 已加载模型后短句首段 PCM 约 300–350 ms（整句非流式生成，Estelle 为流式约 30 ms 首帧）；首次请求含校验与加载约 1.7 s。PCM 交付时刻不是声卡实际发声时刻。

- Release 全量 CTest 10/10 组通过，QML lint 通过。
- `benchmarks/run-speech.py --repeat 3`（`benchmarks/results/supertonic-20261111-bench`）：三轮独立进程均成功、无跳过。首次请求加载 1732–1802 ms、首段 PCM 2068–2146 ms（含校验与加载）；已加载后首段 PCM 300–369 ms；进程峰值 RSS / private 524–540 / 516–532 MiB（含 Qt、PDF、OCR、界面，非语音独占），比 Estelle 记录（432–435 MiB）高约 100 MiB。OS 文件缓存为热态。
- 本地候选 `build/packages/supertonic-20261111.zip` 220217201 字节（SHA-256 `a9dfe8f298c342e3f8d14a55beaa24b1056d779b84c346ac4f0953006026b8c0`），`voices/supertonic3-fr` 约 146 MiB（含 DLL、许可证、SoundTouch 源码）；`validate-windows-package.py` 通过（清单、启动/模块来源与 9 个隔离运行套件）。包体积增大主要来自此前加入的法语词库（约 127 MiB），语音目录比 Estelle 版多约 16 MiB。

未执行：macOS、干净虚拟机、长时间压力、sanitizer、声卡呈现延迟、GPU 显存。远端 CI 无音频设备/教材时相关用例按既有规则跳过；尚未提交和推送，远端 CI 未验证。
