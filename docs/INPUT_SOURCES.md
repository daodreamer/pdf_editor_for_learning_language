# 输入法跟随文档语言（2026-11-26）

## 使用

- 打开文档后，LinguaPDF 判断文档以哪种语言为主，在 LinguaPDF 处于前台时把系统输入法切到该语言；切到其他应用、关闭文档、关闭此功能或退出时，恢复切换前的输入法。默认开启。
- **设置 → 外观与布局 → 输入法**：开关“按文档语言切换系统输入法”；打开文档后可为当前语言选择输入法——“自动选择”或系统中已启用的任一输入法（例如法语文档用 ABC 扩展）。选择按语言保存，下次打开同语言文档沿用；所选输入法被系统移除后回到自动。
- **自动选择**：当前输入法已是该语言的就保持不动（例如已在用“法语 – PC”）；否则取系统顺序中第一个主语言相符的输入法。
- 在 LinguaPDF 里手动换了输入法，只要文档语言或选择不变就不会被改回去；离开应用时仍恢复切换前的输入法。
- **未安装**：开启此功能且系统没有该语言的输入法时，进入阅读界面约 0.8 s 后提示一次（每种语言每次运行一次），可直接打开系统键盘设置；勾选“不再提示”后该语言不再弹出（设置里仍显示红色提示）。在系统设置里添加后回到 LinguaPDF 即生效，无需重开文档。

偏好保存在书架数据 `library-v1.json`：`inputSwitching`（默认 true）、`inputChoices`（语言 → 系统输入法 ID）、`inputNoticesOff`（不再提示的语言）。

## 语言判断

`src/app/languagedetect.*`，按优先级：

1. **页面文字**：PDF 文字层或已完成的 OCR 文字，统计法/德/西/意/英常见功能词（每个记 2 分）与各语言独有字母（è ê ç œ → 法，ä ö ü ß → 德，ñ á í ó ú ¿ → 西，ò ì → 意，各 1 分）。累计已读到的页面；最高分 ≥ 20 且至少是第二名的 1.5 倍才采用。拉丁字母语言证据充足时优先于中文——中国出版的外语教材用中文讲解、用目标语言练习；只有汉字 ≥ 40 且没有足够拉丁语言证据时判为中文。
2. **文件名**：“法语/法文/French/Français”“英语/English”“德语/Deutsch”“西班牙语/Español”“意大利语/Italiano”。出现两种语言（“法英词典”）不采用。
3. **OCR 语言设置**（默认法语）。

选择框 ✓/✗ 的判断（`ShellState::crossMarks`）复用同一扫描函数，规则与之前一致。

固定验收教材《你好法语3学生用书》是没有文字层的纯扫描件，第 12、34 页文字层为 0 字符；判断来自文件名（测试中 OCR 语言故意设为英语以排除回退）。开启 OCR 选区后，识别出的文字也会参与第 1 步。

## 实现

- `InputSources`（`src/app/inputsources.h`）：macOS 用 Carbon Text Input Sources（`TISCreateInputSourceList` / `TISSelectInputSource` / `TISCopyCurrentKeyboardInputSource`，取 `kTISPropertyInputSourceLanguages` 第一项为主语言，跳过带模式输入法的父项，只列已启用且可选的键盘类输入源）；Windows 用 `GetKeyboardLayoutList` / `ActivateKeyboardLayout(KLF_SETFORPROCESS)` / `GetKeyboardLayout(0)`；其他平台为空实现（设置项隐藏）。只在 UI 线程调用。
- `InputSourceSwitcher`（QML 元素，Main.qml 持有）：输入为文档语言、是否有文档、开关和按语言的选择；监听 `QGuiApplication::applicationStateChanged`。变为前台时重新读取输入法列表。进入“接管”时记住当前输入法，结束时（失去前台、关文档、关功能、析构）恢复。测试通过可注入的 Backend 驱动假系统。
- 环境变量 `LANGLERN_NO_INPUT_SWITCH` 让切换器不触碰系统输入法；`langlern_tests` 启动时设置，避免 GUI 测试改动开发者的键盘。

## 验证（macOS Apple Silicon，Qt 6.8.3，Release，2026-11-26）

- `langlern_tests input`：7/7 通过——语言判断（法/英/德/中法混排/纯中文/证据不足/文件名/OCR 映射）、前台切换与恢复、手动切换不被覆盖、按语言选择与失效回退、缺失与稍后安装、真实教材（`LANGLERN_REAL_PDF` 指向本机《你好法语3学生用书第二版.pdf》）判为法语且关闭文档后清空。
- 相关回归：`annotation` 45 通过 / 1 跳过（按需开启的全书调查），`design` 18/18，`shell` 6 通过 / 1 跳过，`ocr` 8/8。
- 本机实际切换（隔离工作区）：系统启用德语、ABC、双拼、Canadian‑CSA。打开真实教材并置于前台 → `com.apple.keylayout.Canadian-CSA`；切到 Finder → 恢复 `com.apple.inputmethod.SCIM.Shuangpin`；切回 → 再次 Canadian‑CSA。文件名为“西班牙语练习.pdf”的文档弹出“未找到西班牙语输入法”，输入法保持不变。

## 未验证与限制

- **Windows 未在真机上运行**。Windows 10/11 默认所有应用共用输入语言；失去前台后再调用 `ActivateKeyboardLayout` 能否恢复其他应用的输入法尚未确认，需要在 Windows 设备上验证，必要时改为在 `WM_ACTIVATEAPP` 前后处理。Windows 只能区分语言/键盘布局，同一语言下的多个 TSF 输入法（如微软拼音与第三方拼音）无法单独选择。
- 设置面板中输入法部分的界面只经布局无警告检查，未截图人工核对。
- 语言判断只覆盖法、德、西、意、英、中；其他语言的文档按文件名或 OCR 语言回退，可能判断为空（不切换）。
- macOS 系统设置里“自动切换到文稿的输入法”开启时，系统也会按窗口切换输入法，两者可能叠加；未测试此组合。
