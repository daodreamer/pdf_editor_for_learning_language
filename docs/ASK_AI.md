# 截图问 AI

阅读器右上角的“问 AI”：框选页面的一部分 → 选择问题类型、按需修改提示词 → 打开 Claude 或 ChatGPT，截图和提示词已放进输入框。**不会自动发送**，由用户检查后自己发送。

LinguaPDF 本身不联网、不调用任何 AI 接口、不需要 API Key；对话在用户自己的 Claude / ChatGPT（订阅或免费账号）里进行。这是应用外的可选功能，核心阅读与练习仍完全离线。

## 使用

1. 点“问 AI”，鼠标变为十字，拖动框选要问的区域（Esc 或再次点按钮取消）。松开后恢复原来的工具。
2. 弹窗显示截图预览和一份完整的提示词（可直接编辑），没有问题类型可选。
3. 选择 Claude 或 ChatGPT（已安装客户端显示“桌面版”，否则“网页版”），点“打开 …”。上次的目标和提示词语言会被记住。

## 提示词

一次要求 AI 回答全部内容，并按固定标题（Markdown `##`）输出：

1. 翻译：逐句翻译（书本身就是提示词语言时省略，后面的编号顺延）
2. 讲解：练什么，关键词汇和表达的意思与用法
3. 语法点：每条附 1–2 个例句
4. 易错点：常见错误和避免方法
5. 易混淆点：对比易混的词、形式、结构
6. 使用场景与频率：日常 / 学术 / 工作 / 很少使用，以及正式程度
7. 检查答案：逐题 ✓ / ✗；答错的给出正确答案，并说明错在哪里、为什么错；未作答的给出参考答案；可以接受多种答案时全部列出

开头写明角色（经验丰富、要求严格的语言老师）、目标语言（来自文档语言检测，未知时让 AI 根据截图判断）、书名（去掉 .pdf）和页码。结尾要求严格批改拼写、变音符号、性数配合、动词变位等细节，同时突出重点、各部分简洁。以下做法借鉴自公开的语言教学提示词和反馈研究：

- 明确要求严格批改：普遍反映模型默认会放过小错误。
- 每处纠正都要解释原因，并给出带译文的例句。
- 说明语体和正式程度。
- 重点优先、避免堆砌：Lancaster 大学 CASS 的 AI 反馈研究认为“少即是多”。
- 截图看不清时直接说明，不要猜测：针对 OCR 和手写识别的风险。

参考：[learnprompting.org](https://learnprompting.org/blog/ai-prompt-language-learning)、[FreeAcademy 语言学习提示词](https://freeacademy.ai/books/how-to-learn-a-language-with-ai/ai-language-prompts)、[CASS: When Less is More](https://cass.lancs.ac.uk/when-less-is-more-ai-feedback-for-language-learners/)、[gptprompts.ai 工具对比](https://gptprompts.ai/ai-chat-tools-for-language-learning)。

提示词语言（弹窗里的“提示词语言”）：默认“跟随应用”，也就是界面语言（中文或英文）；也可以选“跟随系统”，即系统首选语言列表中第一个受支持的语言，都不支持时退回界面语言；或者直接指定一种语言。受支持的语言有简体中文、繁體中文、English、Français、Deutsch、Español、Italiano、日本語、한국어；`zh-Hant`、`zh-TW`、`zh-HK`、`zh-MO` 对应繁体。讲解和译文使用提示词语言，例句使用目标语言。全部文本在 `src/app/aiprompt.cpp`，每种语言单独撰写，不做运行时机器翻译。

截图来自窗口实际显示内容，包括打字答案、手写笔迹和当前主题（夜间调暗时截图也较暗）。分辨率等于屏幕像素（Retina 为 2 倍）；需要更清晰时先放大页面再截图。

## 各目标的处理方式

| 目标 | 打开方式 | 提示词 | 截图 |
| --- | --- | --- | --- |
| Claude 桌面版 | `claude://claude.ai/new?q=…`，新建对话 | 链接预填 | 自动粘贴 |
| Claude 网页版 | `https://claude.ai/new?q=…`（默认浏览器） | 链接预填 | 自动粘贴 |
| ChatGPT 桌面版（仅 macOS） | 打开应用 | 第二次自动粘贴 | 自动粘贴 |
| ChatGPT 网页版 | `https://chatgpt.com/?prompt=…` | 链接预填 | 自动粘贴 |

提示词用完整百分号编码（`+`、`&`、`#`、换行都保留为文字）。截图先放入剪贴板，再打开目标；等目标应用（网页则为默认浏览器）切到前台后，再等待输入框就绪（应用 1 s、网页 3.5 s），然后发送 ⌘V / Ctrl+V。两次粘贴间隔 0.7 s，避免目标应用读取到已被替换的剪贴板。粘贴前每次都会检查前台应用；如果用户已切到别的应用，就不会粘贴。10 s 内目标没有到前台，就放弃自动粘贴。

自动粘贴失败或不被允许时，截图仍留在剪贴板，阅读器底部提示用户按 ⌘V / Ctrl+V 手动粘贴。ChatGPT 桌面版只能靠粘贴来接收提示词，因此不能自动粘贴时改用 ChatGPT 网页版（链接预填）。

### 权限

- macOS：模拟按键需要“系统设置 → 隐私与安全性 → 辅助功能”中允许 LinguaPDF。每次运行第一次发送时会请求一次，系统会弹出自己的授权对话框；弹窗和提示里也有“打开设置”。开发构建每次重新签名后，系统可能要求重新授权。
- Windows：`SendInput` 不需要权限。Claude 桌面版通过已注册的 `claude:` 协议识别（`AssocQueryString`）；ChatGPT Windows 应用没有公开的链接协议，所以总是使用网页版。

## 实现

- `src/app/aihandoff.*`：`AiHandoff`（QML 元素，UI 线程）。`capture()` 用 `QQuickWindow::grabWindow()` 截取当前窗口，再按项坐标 × 实际设备像素比裁剪。截取通过 `Qt.callLater` 延后执行，此时拖动框已经隐藏，所以不会出现在截图里。预览缩小后写入临时目录的 PNG（Qt Quick `Image` 不解码 `data:` URL），关闭弹窗时删除。交接状态机由 100 ms `QTimer` 驱动。系统相关操作经 `Backend` 注入，测试中替换为假系统。
- `src/app/aiplatform_{mac.mm,win.cpp,other.cpp}`：已安装应用（`NSWorkspace` bundle id / 协议注册）、前台应用（bundle id / 前台窗口进程名）、默认浏览器、辅助功能检查（`AXIsProcessTrustedWithOptions`）、粘贴按键（`CGEventPost` V 键码 + ⌘ / `SendInput` Ctrl+V）。
- `DocumentView.qml`：`regionPurpose` 区分 OCR 区域识别和问 AI 截图。问 AI 截图结束（或 Esc、取消）后恢复原来的 `inputMode`。
- `src/app/aiprompt.*`：提示词文本表、语言选择（应用 / 系统 / 指定）和组装。
- `ReaderPane.qml`：按钮、弹窗、提示词语言选择、状态提示。`WorkspaceState` 保存 `aiTarget` 和 `aiPromptLanguage`（`app` / `system` / 语言代码）。

不引入新的第三方依赖；macOS 额外链接系统 AppKit、ApplicationServices 框架，Windows 链接 shlwapi。

## 验证（2026-11-26，macOS 27 Apple Silicon，Qt 6.8.3 Release）

- 手动确认：本机 Claude 桌面版 7.37 打开 `claude://claude.ai/new?q=测试预填` 后，会在 Chat 中新建对话并把文字填入输入框，不会发送，界面另外显示“Use caution before running this prompt”提示。`https://chatgpt.com/?prompt=…` 在未登录的网页上同样只预填、不发送。
- 完整中文提示词加长后的链接（6221 字符）在 Claude 桌面版中完整预填到最后一行，没有被截断，也没有发送。
- `langlern_tests ai`（10 项含 init/cleanup）：提示词语言优先级与系统语言映射；9 种语言的提示词都包含 7 个部分（或在书本身是提示词语言时为 6 个）、✓ / ✗、书名和页码；中文提示词包含全部要求的内容；未知目标语言的写法；链接编码；用假系统验证 Claude 桌面版、ChatGPT 桌面版两次粘贴的顺序、无权限回退到网页、目标不到前台时超时、切走后不粘贴、取消；截图裁剪按设备像素；固定真实教材第 34 页在阅读器中加入打字答案后点按钮、拖选，然后检查截图尺寸、内容、预览加载、提示词（目标语言法语、书名、页码、“检查答案”部分），切换到法语提示词后内容随之改写并被记住，关闭后删除临时预览、恢复原工具、Esc 取消，以及原 PDF 大小和修改时间不变。截图与弹窗图在 `benchmarks/results/ask-ai-20261126/`（不提交教材内容）。
- 用户实测（开发构建，真实教材）：第一次发送时还没有辅助功能权限，按设计只预填了提示词，截图留在剪贴板。授权后再次发送，Claude 桌面版的新对话里提示词和截图都已就位，没有被发送。
- **未验证**：Claude 网页版链接预填（内置浏览器未登录，推断与桌面版相同）；ChatGPT 桌面版（本机未安装）；Windows 全部路径（未编译、未运行）。
- 限制：粘贴使用 V 的物理键码，Dvorak 等布局下 ⌘ 快捷键按字符映射的应用可能收不到；网页加载超过 3.5 s 时，截图可能没有粘贴上，需要手动粘贴；Claude / ChatGPT 的链接参数不是公开的稳定接口，将来可能变化。
