# 阶段 0 依赖与许可审查

## 阶段 7 产品字体增量

产品开始部署 `assets/fonts/NotoSansSC-Regular.ttf`（10,596,288 bytes；SHA-256 `2b4a06fde0544928c3715b0b71e2ded7e45981fb376bd0841c891668705cf629`），由下表固定上游 Noto VF 经已锁定 fonttools 实例化 wght=400 得到，与阶段 0 `regular_sha256` 一致。保留原始命名表，内部 PostScript 名称仍可显示 `NotoSansSC-Thin`；实际为固定 wght=400 实例，不据名字改用系统字体。PDFium 将该完整字体压缩嵌入一次（当前约增加 6 MiB），配合 Qt glyph ID/基线及显式 ToUnicode/CIDToGIDMap 保留可提取文字。

SIL OFL 1.1 允许随软件再分发及嵌入文档；`assets/fonts/OFL.txt` 和字体元数据保留版权许可，CMake 将两者复制到开发 exe 邻近 fonts 目录。项目 MIT 不覆盖字体。无新增产品库依赖；QPdfWriter 属既有 Qt Gui，PDFium 接口来自已锁定包。阶段 8 仍须核对完整部署闭包，本阶段不宣称整个发行包已完成合规确认。


## 阶段 4 文字选择增量

运行时依赖不变，使用锁定 PDFium 的公开文本接口和 Qt Gui 剪贴板、Qt Quick 场景图几何。`text.pdf`、`text-limit.pdf` 是 MIT 原创 stdlib 生成测试文件，不嵌入字体；`chinese.pdf` 为原创文本并子集嵌入已锁定的 Noto Sans SC，约 15 KiB，仅作测试，不加入产品资源。字体来源、固定提交、OFL 嵌入依据及原始许可证沿用下文与 [Noto-OFL.txt](notices/Noto-OFL.txt)；PDF 元数据指明嵌入字体来源，生成脚本和 manifest 提供追踪。PyMuPDF 仅为既有开发生成工具，不进入 CMake 依赖或产品发行。没有复制系统字体或引入图标资源；发行许可闭包仍待阶段 8 审查。

## 阶段 2 正式引擎集成增量

正式应用开始动态链接下表已审查的 PDFium 157.0.8100.0，来源、固定提交和许可证据沿用阶段 0，无新增供应商依赖。开发构建在 exe 目录复制 pdfium.dll；这只是开发运行路径，不是完整分发闭包。阶段 8 仍须随实际发行包核对 PDFium LICENSE/第三方通知、Qt 动态库及对应源码/替换义务、MSVC runtime。未向正式程序加入 Noto 字体或原型导出功能。`scan.pdf`、`password.pdf` 由 MIT 原创 geometry 派生，仅作测试；生成用 PyMuPDF 属已有开发工具，不链接/分发到应用。下文阶段 1 的“不链接 PDFium”是历史边界，本阶段已更新。


## 阶段 1 增量审查（2026-10-15）

正式外壳继续动态使用固定 Qt 6.8.3，新增 QuickDialogs2 公开模块用于本地系统文件选择；维护成本为随 Qt 一起更新/复测，不新增独立供应商。实际 SDK SBOM 中 QuickDialogs2、QuickDialogs2Utils、QuickDialogs2QuickImpl 和 qtquickdialogsplugin 均提供 LGPL-3.0-only 选项，摘录、SPDX ID 和来源 SHA-256 见 [qt-phase1-license-evidence.json](qt-phase1-license-evidence.json)。适用已有 Qt 通知、对应源码和动态替换义务；最终部署闭包仍需阶段 8 核对。应用没有直接包含或链接 Qt 私有 API。

Windows 界面指定系统 Microsoft YaHei UI 字体，不复制、打包或嵌入系统字体文件。没有新增图标或第三方美术资源，工具区使用文字标签；后续引入 Lucide 资源仍须审查。应用自有静态 QML 模块为 MIT 源码，不表示静态链接 Qt。阶段 0 PDFium/字体仅在显式构建原型时使用；阶段 1 正式程序不链接它们。尚未制作独立发行包，不能宣称发行许可闭环完成。

审查日期：2026-10-15。审查方式：本地包内容、供应商 SPDX、固定源码及通知文件核对。此文件记录技术和许可证据，不宣称尚未制作的发行包合规。

## 实际选用与来源

| 组件 | 固定版本/来源 | 用途与维护成本 | 链接/分发选择 |
| --- | --- | --- | --- |
| Qt Core / Gui / Qml / Quick / QuickControls2 / QuickTemplates2 | 6.8.3；官方 online repository，Windows MSVC2022 x64 | GUI、图形、输入、文件原子提交；成熟公开 API，仍需安全更新与跨平台复测 | 动态链接，拟选 LGPL-3.0-only 路径；不引入 GPL-only 产品模块 |
| Qt Test | 同一 Qt 6.8.3 SDK | 开发测试，不进入产品运行目录 | 保留 Qt 授权；测试程序不作为产品发行 |
| Qt tools、qmlcachegen、qmllint | 同一 Qt SDK 的开发工具 | 构建/检查，不需要把整个 SDK 分发给用户 | 工具及第三方规则与生成产物规则分别审查 |
| PDFium | 157.0.8100.0，chromium/8100，commit `f51279699cb59754d1f49aef10eb8e825fe56532` | 唯一 PDF 引擎；每次升级核对 API、线程、图像、导出及安全修复 | bblanchon 固定二进制包，经 C API 动态链接，BSD-style 与包内各第三方通知 |
| Noto Sans SC | noto-cjk commit `f8d157532fbfaeda587e826d4cd5b21a49186f7c` | 中文字体嵌入实验；约 6 MiB 静态派生字体全量嵌入会放大导出 | SIL OFL 1.1；保留 OFL 与字体元数据，非系统字体复制 |
| MSVC / Windows SDK / runtime | MSVC 19.51.36257.0；SDK 10.0.26100.0 | 本机工具链；Qt SDK 原构建为 MSVC2022，实测可链接 | 发行前按实际 runtime 版本核对微软可再发行许可和安装路径 |
| Apple SDK/Metal | 未使用，待真机 | Apple Silicon 平台构建与系统后端 | 由平台 SDK 规则与实际包审查，不声明已验证 |
| 图标 | 本原型没有引入图标资源 | 后续若采用 Lucide，固定资源及授权 | 本次无图标分发义务 |

Qt 选择 6.8.3 是为获得固定、公开可取且具有供应商 SBOM 的工具链，未宣称它是当前最新或已完成安全发行审查。升级与发行前必须重新核对维护、安全补丁和模块授权。仅用公开 Scene Graph API，不直接链接 Qt 私有 QRhi 接口。

`dependencies.lock.json` 记录下载 URL、字节数及 SHA-256。Windows PDFium 包 SHA 与 GitHub asset digest 一致；mac arm64 包已下载检查 VERSION/args.gn 并散列，但未运行。Qt 三个实际 Windows archive 已留存并记录散列；aqt 下载时核对官方 checksum。mac Qt archive 尚未下载/锁定，不冒充双平台完整锁文件。

## PDFium：实际包审查

实际 Windows `args.gn`：Release、x64、`pdf_enable_v8=false`、`pdf_enable_xfa=false`、`pdf_is_standalone=true`、`pdf_use_partition_alloc=false`。`is_component_build=false` 是供应商内部构建选项，实际交付仍有 pdfium.dll 和 import library。mac 包另含 `clang_use_chrome_plugins=false`，target_cpu=arm64。未从源码独立重建，供应链信任仍依赖该二进制供应商；正式发布需保存其构建脚本/提交、可获取源码及包散列。

包的 fpdfview.h 与固定上游头文件已逐文本比较：差异仅为供应商去掉 COMPONENT_BUILD 条件、始终提供 DLL export/import 宏；API 签名和线程声明相同。原始上游头文件留在 notices/pdfium/fpdfview-upstream.h，包散列保证实际头文件与库按包配套。它是证据副本，不是应用依赖头。

所有包内 notices 均保留于 `notices/pdfium/`，校验值见 `notices.sha256.json`：

| 包内组件 | 许可证证据 | 发布必须处理 |
| --- | --- | --- |
| PDFium 主体 | LICENSE、pdfium.txt（含 BSD 与 Apache 文本） | 保留版权、条件、免责与适用通知，不暗示背书 |
| Abseil | abseil.txt，Apache-2.0 | 许可证、适用 NOTICE、修改说明和专利条款 |
| AGG | agg23.txt，宽松授权 | 保留通知 |
| Dragonbox | dragonbox-Boost.txt / dragonbox-Apache2-LLVM.txt | 保留双许可证据，选择适用路径，不删除原通知 |
| fast_float | fast_float.txt，MIT | 保留版权与许可 |
| FreeType | freetype.txt，FreeType/BSD-like | 保留通知和要求的署名；不直接选择 GPL 替代授权 |
| HarfBuzz | harfbuzz.txt，MIT-like | 保留通知 |
| ICU | icu.txt，多组 Unicode/ICU/第三方通知 | 原样保留完整通知，不缩减为单行 MIT |
| Little CMS | lcms.txt，MIT | 保留通知 |
| libjpeg-turbo / IJG | libjpeg_turbo.md / .ijg，多组 BSD/IJG/zlib 规则 | 保留全部适用通知与 IJG 说明 |
| OpenJPEG | libopenjpeg.txt，BSD | 保留通知 |
| libpng | libpng.txt，libpng license | 保留声明，不篡改来源 |
| LLVM libc | llvm-libc.txt，Apache-2.0 with LLVM exception | 保留许可与例外 |
| simdutf | simdutf.txt，MIT 文本 | 保留包内完整通知 |
| zlib | zlib.txt，zlib license | 保留来源/修改声明 |

这是包内通知清单，不能据此证明它覆盖所有实际链接对象。供应商源码和构建变更、最终运行库闭包仍需随发行复核。

## Qt：实际模块与第三方证据

`qt-sbom-evidence.json` 从本地三个供应商 SPDX 提取了 301 个 SDK package 的名称、版本、来源、授权、版权/注释与自定义授权全文；原始 SPDX 文件散列一并记录。该清单包含未使用工具和可选插件，**不是**产品必须分发的 301 个组件。

实际 Core、Gui、Qml、Quick、QuickControls2、QuickTemplates2 的供应商声明均含 LGPL-3.0-only 选项。Qt 自身和诸如 FreeType、HarfBuzz、PCRE2、zlib、PNG/JPEG、ANGLE/DirectX 相关项、QML JavaScriptCore/masm 等，均需按照最终部署闭包筛选证据。SDK SBOM 中的 NOASSERTION 保持未知，不改写为已确认授权。

拟采用 LGPL 动态分发方案：保留 Qt 和随包第三方通知、LGPL/GPL 对应文本、准确对应源码的获取/提供方式；允许用户替换兼容动态库及进行许可允许的调试/逆向，不能在自有协议中禁止；若修改 Qt 库须提供对应修改源码；应用源码与构建说明公开。macOS 签名/公证和库替换权、Windows 可再发行 runtime、干净系统部署尚未做发行验证。动态链接本身不能代替这些义务。

已保存 Qt 6.8.3 的 LGPL-3.0、GPL-2.0、GPL-3.0、Qt GPL exception、BSD-3-Clause 文本。依据：[Qt LGPL 说明](https://doc.qt.io/qt-6/lgpl.html)、[Qt 6.8.3 源码 LICENSES](https://github.com/qt/qtbase/tree/v6.8.3/LICENSES)。实际模块和包授权优先于网页概览。

## 字体与开发工具边界

Noto 来源固定，源字体 SHA 已记录；使用 fonttools 4.60.1 固定 wght=400 得到静态 TTF。派生文件的时间戳可能随生成变化，lock 中 regular_sha256 表示本次实际产物，不能当作所有重放必然逐字节一致的承诺。静态实例内部名称仍含 Thin，发行前需规范派生字体命名与 OFL 条件；本次不发布字体包。OFL 允许嵌入，完整许可证在 `notices/Noto-OFL.txt`。实验导出 PDF 保留字体数据，但没有把系统字体纳入产品。

Python、aqtinstall、CMake、Ninja、fonttools、psutil 和 PyMuPDF 仅在开发环境使用，不链接进 C++ 原型。fonttools 为 MIT，aqt 为 MIT，CMake 为 BSD-3-Clause，Ninja 为 Apache-2.0，psutil 为 BSD-3-Clause；Python 有 PSF 许可及其随包通知，传递依赖精确安装记录见 tools/requirements-phase0.lock.txt。若打包开发工具，须另查实际各 wheel 的许可和通知。

PyMuPDF 1.26.5 / MuPDF 只做独立进程的本地验证，是 AGPL-3.0/商业双许可组件；它不进入产品、CMake 链接或发行目录，也不将其输出脚本变成产品依赖。若未来要把该工具分发、集成服务或链接到产品，必须重新解决其授权要求。没有为性能指标增加任何产品第三方库。

## 结论与待办

已形成可执行的候选分发路径和通知草案，但**没有制作或批准最终发行包**。阶段 0 当前不存在 Qt 核心模块或 Noto 实验使用的已知许可阻断；仍需核对最终部署闭包、相应源码可得性、微软 runtime、macOS 打包与签名，以及供应商构建的全部实际传递依赖。阶段报告保留这些风险，不能用 MIT 项目声明覆盖第三方义务。
