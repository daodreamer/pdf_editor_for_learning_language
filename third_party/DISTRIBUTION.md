# LinguaPDF 1.0.0 Windows 发行许可终审

审查日期：2026-11-13。范围：`LinguaPDF-1.0.0-windows-x64` 便携 ZIP，由 `tools/package-windows.py` 从已提交的 `v1.0.0` 源码生成，免费公开分发，不收费、不签名。macOS 包不在本次范围内。

审查方法：逐个核对实际部署的文件（以包内 `licenses/deployed-binaries.json` 和 `manifest.json` 为准），对照所附许可文本中的分发条件，检查本项目是否已满足每一项义务。本文是项目维护者的工程审查记录，不是法律意见。

## 结论

**可以按当前方式免费公开分发 Windows 版。** 没有发现阻断项。所有第三方组件都允许免费再分发，各自的义务都已在包内落实；只有一处剩余风险（ECDICT 上游数据来源）记录在下面，不影响发布。

不签名不涉及任何许可义务，只会让用户首次运行时看到 Windows SmartScreen 提示（见“已知限制”）。

## 逐组件义务与落实情况

| 组件 | 许可 | 分发义务 | 落实方式 | 结论 |
| --- | --- | --- | --- | --- |
| LinguaPDF 源码 | MIT | 保留版权和许可 | 包根 `LICENSE`；exe 版本信息带版权 | 通过 |
| Qt 6.8.3 | LGPL-3.0-only | 动态链接、可替换；提供许可全文和通知；提供对应源码或获取方式；不限制逆向工程 | 独立 DLL 和 `qt.conf`；`licenses/qt/` 带 LGPL/GPL 全文；供应商 SPDX 和通知超集；`THIRD-PARTY-NOTICES.md` 写明源码地址和替换权利；应用源码公开（`source.zip` 和 GitHub）；未修改 Qt 源码 | 通过 |
| PDFium 及内含库 | BSD-3-Clause、Apache-2.0、FreeType、ICU、libjpeg-turbo/IJG 等 | 保留版权、条件和免责声明；Apache-2.0 需附许可文本 | `licenses/pdfium/` 原样保留供应商包的全部通知 | 通过 |
| Tesseract、Leptonica、libpng、zlib | Apache-2.0、BSD-2-Clause、libpng、zlib | 静态链接也需附许可文本和版权 | `licenses/ocr/`；上游没有 Apache NOTICE 文件需要转载 | 通过 |
| tessdata_fast 模型 | Apache-2.0 | 附许可 | `licenses/ocr/tessdata_fast-LICENSE.txt` | 通过 |
| Supertonic 3 模型 | BigScience OpenRAIL-M | §4a 在分发条款中写入可执行的使用限制并告知接收者；§4b 附许可全文；§4c 修改文件显著标注；§4d 保留原有声明 | §4a：包根和仓库根 `THIRD-PARTY-NOTICES.md` 第 1 节把 Attachment A 写成分发条款；§4b：`licenses/speech/Supertonic-OpenRAIL-M.txt`；§4c：`Supertonic-modifications.txt` 逐文件说明；§4d：附模型卡 | 通过。注意：OpenRAIL-M 不是 OSI 开源许可，因此对外表述是“开源程序 + 带使用限制的模型”，不说“全部组件开源” |
| Supertonic 参考代码 | MIT | 保留声明 | `Supertonic-code-MIT.txt` | 通过 |
| ONNX Runtime 1.23.2 | MIT + 第三方声明 | 保留声明 | `ONNXRuntime-LICENSE.txt`、`ONNXRuntime-ThirdPartyNotices.txt` | 通过 |
| SoundTouch 2.3.3 | LGPL-2.1-or-later | 独立可替换库；附许可；提供对应源码；对本项目的修改需说明 | 独立 DLL；许可全文；源码归档 `voices/supertonic3-fr/licenses/soundtouch-2.3.3-source.tar.gz` 随包；唯一改动（资源头 `afxres.h`→`winresrc.h`）记录在 `tools/bootstrap-speech.py` | 通过 |
| ECDICT | MIT | 保留版权和许可 | `licenses/ECDICT-MIT.txt`，词库旁另有 `dictionary/LICENSE.txt` | 通过，有剩余风险（见下） |
| 法语词典（Wiktionary 派生） | CC BY-SA 4.0 / GFDL 双许可，本项目选 CC BY-SA 4.0 | 署名；注明修改；提供许可链接；派生数据以相同许可分发；不得加技术限制 | `Wiktionary-CC-BY-SA.txt` 和 `dictionary/fr-wiktionary-NOTICE.txt` 写明来源、修改和许可；SQLite 未加密；`THIRD-PARTY-NOTICES.md` 写明 ShareAlike 只约束词库 | 通过。选择 CC BY-SA 分支后不需要附 GFDL 全文。程序与词库是并列分发的聚合关系，ShareAlike 不传染到 MIT 程序 |
| Noto Sans SC | SIL OFL 1.1 | 随附 OFL；不得单独售卖字体；派生字体不得使用保留字体名 | `fonts/OFL.txt`、`licenses/Noto-OFL.txt`；免费分发；字体的保留字体名是 “Source”（字体 name 表：© 2014-2021 Adobe, with Reserved Font Name 'Source'），本项目的 wght=400 静态实例沿用 “Noto Sans SC”，不含该保留名 | 通过 |
| Lucide 图标路径 | ISC（部分 MIT） | 保留版权和许可 | `licenses/Lucide-ISC.txt` | 通过 |
| C++/WinRT 头文件 | MIT | 只在编译期使用；保留声明 | `licenses/CppWinRT-MIT.txt` | 通过 |
| MSVC 运行库（`Microsoft.VC14x.CRT`） | Visual Studio Community 的 Distributable Code | 发行人须持有 VS 许可；文件不得修改；须在自己的程序中加入实质功能；显示自己的版权声明；对接收者施加不弱于微软的保护条款；不得用微软商标暗示背书 | 构建用的是 **Visual Studio Community 2026**：个人开发者以及 OSI 开源项目可以免费使用，不需要付费许可。运行库只从 `VC\Redist\MSVC\<版本>\x64\Microsoft.VC*.CRT` 原样拷贝（manifest 中有散列）；从不分发 `debug_nonredist`；保护条款写在 `THIRD-PARTY-NOTICES.md` 的“Microsoft 组件”一节 | 通过 |
| `d3dcompiler_47.dll` | Windows SDK Redist | 原样随应用分发；附 SDK 许可 | 只取 `Redist/D3D/x64`；附 `sdk_license.rtf` 和第三方声明 | 通过 |

Visual Studio 可再发行文件清单的依据是 <https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution>。该页确认 `VC\redist` 目录可以原样随程序分发，`debug_nonredist` 除外。Community 许可正文（<https://visualstudio.microsoft.com/license-terms/vs2026-ga-community/>）由网页脚本渲染，自动抓取拿不到正文；上表的使用和分发条件依据的是 Visual Studio Community 历来公开的条款。如需逐字核对，在 Visual Studio 安装程序的许可对话框里查看即可。

## 剩余风险与已知限制

1. **ECDICT 数据来源。** 上游仓库声明 MIT，但其 README 说明词条汇集自多种材料，本项目没有逐条核实权利链。ECDICT 多年来被大量开源项目以 MIT 方式再分发，风险较低。如果权利方提出异议，可以移除该词库，不影响其他功能。
2. **未签名。** 用户首次运行会看到 SmartScreen 的“未知发布者”提示，需要点“更多信息 → 仍要运行”。免费的可选方案是 [SignPath Foundation](https://signpath.org/) 的开源项目免费签名，需要申请审核，不影响本次发布。
3. **依赖版本。** Qt 6.8.3 和 PDFium chromium/8100 是本项目锁定并完成验证的版本，不是各自的最新安全补丁版本。LinguaPDF 只打开用户自己选择的本地 PDF，但打开来源不明的 PDF 仍然有解析器漏洞的一般风险。升级依赖需要重新完成真实教材回归，列为后续工作。
4. **macOS** 不在本次发布范围，没有对应的部署或签名审查。

## 维护规则

- 新增或升级随包组件时，更新对应锁文件、`notices/` 和本表，再重新打包验证。打包脚本会把无法归属的 DLL 标为 `Unmapped binary`；只要出现这一项，就不能发布。
- `third_party/notices/**` 按字节保存（见 `.gitattributes`），语音通知的散列记录在 `speech.lock.json`，修改后必须同步更新。

历史阶段的候选包审查记录见 git 历史和 [DEPENDENCIES.md](DEPENDENCIES.md)。
