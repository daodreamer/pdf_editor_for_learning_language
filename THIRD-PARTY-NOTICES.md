# LinguaPDF 第三方组件与使用条款 / Third-Party Notices and Terms

LinguaPDF 自有源码采用 [MIT License](LICENSE)。Windows 发行包同时随附下列第三方组件，它们**不受 MIT 覆盖**，各自的许可证全文在发行包 `licenses/` 目录（源码仓库 `third_party/notices/`）。逐组件的审查依据见 [third_party/DISTRIBUTION.md](third_party/DISTRIBUTION.md)。

LinguaPDF's own source code is MIT licensed. The Windows package also bundles the components below, which are **not** covered by the MIT License. Full license texts are in the package's `licenses/` folder (`third_party/notices/` in the source tree).

## 1. 法语语音模型的使用限制 / Use restrictions for the French speech model

随附的法语朗读模型 Supertonic 3（Supertone Inc.，含本项目量化/半精度派生文件）按 **BigScience OpenRAIL-M** 许可提供。该许可不是 OSI 开源许可，带有使用限制。使用、复制或再分发 LinguaPDF 时，你同意遵守以下条款：

The bundled Supertonic 3 speech model (Supertone Inc.) and the derivatives LinguaPDF ships are licensed under the **BigScience OpenRAIL-M** license. By using, copying or redistributing LinguaPDF you agree to the following, which forms part of the terms under which LinguaPDF is distributed:

> You may not use the Supertonic 3 Model or its Derivatives (the files in `voices/supertonic3-fr/models` and `voices/supertonic3-fr/voices`) for any of the restricted uses listed in Attachment A of `licenses/speech/Supertonic-OpenRAIL-M.txt`. This includes, among others: violating any applicable law; harming minors; generating or disseminating verifiably false information to harm others; generating personal information that can be used to harm an individual; disseminating generated content without disclosing that it is machine generated; defaming or harassing others; impersonating others without their consent; fully automated decision making that adversely affects an individual's legal rights; discrimination; providing medical advice; or uses for the administration of justice, law enforcement, immigration or asylum processes. Anyone you redistribute the Model or its Derivatives to must receive a copy of that license and is bound by the same restrictions (paragraph 5).

LinguaPDF 只把它用于本机朗读教材文字，生成的语音是机器合成音频。修改说明见 `licenses/speech/Supertonic-modifications.txt`。

## 2. 组件清单 / Components

| 组件 / Component | 用途 / Use | 许可 / License | 许可文本 / Text in `licenses/` |
| --- | --- | --- | --- |
| Qt 6.8.3（Core、Gui、Qml、Quick、Controls、Dialogs、Shapes、Sql 等） | 界面与运行时 / UI runtime | LGPL-3.0-only（动态链接 / dynamically linked） | `qt/`, `Qt-ThirdParty-Notices.txt`, `*.spdx.json` |
| PDFium 157.0.8100.0 及其内含库 | PDF 渲染、文字、导出 / PDF engine | BSD-3-Clause、Apache-2.0 及各内含库许可 | `pdfium/` |
| Tesseract 5.5.1、Leptonica 1.85.0、libpng 1.6.58、zlib 1.3.2 | 离线 OCR（静态链接） | Apache-2.0、BSD-2-Clause、libpng、zlib | `ocr/` |
| tessdata_fast（fra、eng、chi_sim） | OCR 模型 | Apache-2.0 | `ocr/tessdata_fast-LICENSE.txt` |
| Supertonic 3 模型 / 参考代码 | 法语朗读 / French speech | OpenRAIL-M（见第 1 节）/ MIT | `speech/` |
| ONNX Runtime 1.23.2 | 语音推理 / speech inference | MIT（另附第三方声明） | `speech/ONNXRuntime-*` |
| SoundTouch 2.3.3 | 变速不变调 / tempo change | LGPL-2.1-or-later（独立 DLL / separate DLL） | `speech/SoundTouch-LGPL.txt` |
| ECDICT | 英汉词典 / English dictionary | MIT | `ECDICT-MIT.txt` |
| 法语词典（派生自 English Wiktionary，经 Wiktextract/kaikki.org） | 法语查词与变位 / French lookup | CC BY-SA 4.0 | `Wiktionary-CC-BY-SA.txt` |
| Noto Sans SC | 界面答案与导出字体 / answer & export font | SIL OFL 1.1 | `Noto-OFL.txt` |
| Lucide 图标路径 | 工具栏图标 / icons | ISC（部分源自 Feather，MIT） | `Lucide-ISC.txt` |
| C++/WinRT 头文件 | Windows API 投影（仅编译期） | MIT | `CppWinRT-MIT.txt` |
| Microsoft Visual C++ 运行库、d3dcompiler_47.dll | 运行库 / runtime | Microsoft Distributable Code | `Microsoft-Redist.txt`, `windows-sdk/` |

### 法语词典署名 / French dictionary attribution

法语词典数据派生自 [English Wiktionary](https://en.wiktionary.org/)（贡献者见各词条页面历史），经 Tatu Ylönen 的 Wiktextract / [kaikki.org](https://kaikki.org/dictionary/French/) 抽取。本项目做了筛选和格式转换，派生数据库 `dictionary/fr-wiktionary.sqlite` 按 [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 分发。改动说明见 `Wiktionary-CC-BY-SA.txt`。ShareAlike 只约束这份词典数据，不影响 LinguaPDF 程序本身的 MIT 许可。

French dictionary data is derived from English Wiktionary (contributors in each entry's page history) via Wiktextract/kaikki.org, filtered and converted by this project, and distributed under CC BY-SA 4.0. ShareAlike applies to the dictionary data only.

### Qt 与 SoundTouch（LGPL）

Qt 和 SoundTouch 以独立 DLL 形式随附。你可以用兼容版本替换它们，并在 LGPL 允许的范围内修改、调试或逆向工程 LinguaPDF 来做到这一点。本项目没有附加任何限制这些权利的条款。对应源码：

- Qt 6.8.3：<https://download.qt.io/archive/qt/6.8/6.8.3/submodules/>（qtbase、qtdeclarative、qttools）
- SoundTouch 2.3.3：随包 `voices/supertonic3-fr/licenses/soundtouch-2.3.3-source.tar.gz`，以及 <https://codeberg.org/soundtouch/soundtouch>
- LinguaPDF 本身：包内 `source.zip` 与 <https://github.com/daodreamer/pdf_editor_for_learning_language>

Qt and SoundTouch are separate, replaceable DLLs; you may replace them and modify, debug or reverse engineer LinguaPDF as the LGPL permits. Corresponding source locations are listed above.

### Microsoft 组件 / Microsoft components

`msvcp140*.dll`、`vcruntime140*.dll`、`concrt140.dll` 等 Visual C++ 运行库，以及 `d3dcompiler_47.dll`，属于 Microsoft 的可再发行代码（Distributable Code），按原样提供，适用 Microsoft 的许可条款，不受 MIT 覆盖。除适用法律明确允许的情况外，不得修改、反向工程或脱离本程序单独再分发这些文件。

The Microsoft Visual C++ runtime DLLs and `d3dcompiler_47.dll` are Microsoft Distributable Code, provided unmodified and subject to Microsoft's license terms. They are not covered by the MIT License. You may not modify or reverse engineer them (except as applicable law expressly permits) or redistribute them separately from LinguaPDF except as Microsoft's terms allow.

## 3. 不随包的系统组件 / System components (not bundled)

Windows OCR（Windows.Media.Ocr）、SAPI 语音和 WinMM 音频由操作系统提供，LinguaPDF 只调用它们，不随包分发。查词卡片里的「法语助手」链接只在系统浏览器中打开外部网页，LinguaPDF 不下载、不嵌入其内容。
