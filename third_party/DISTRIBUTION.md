# Windows 阶段 8 候选包分发说明

本文件适用于本项目打包脚本产生的未签名、本地测试候选目录。许可文本、SDK SBOM 和实际文件清单随包提供；不据此宣称最终公开发行方式已经完成全部合规审查。

项目源码和原创 `LineIcon.qml` 线性图标采用根目录 MIT。图标以 Qt Quick Shapes / PathSvg 绘制，没有复制 Lucide 图形或引入 QtSvg 库。Qt Quick Shapes 是既有 Qt Declarative 模块，供应商 SBOM 中保留其声明。开发用 Python、MuPDF、fonttools、CMake、Ninja、测试 exe 不进入运行闭包；source.zip 中的脚本仅为源码。

Qt 6.8.3 动态库和 QML 插件采用供应商提供的 LGPL-3.0-only 选项。完整 LGPL/GPL 文本、第三方通知和原始供应商 SBOM 在 licenses 目录。`Qt-ThirdParty-Notices.txt` 是已安装 SDK 的通知超集，含未部署组件，不应误读为每项都被运行时链接。准确文件和 SHA-256 以 manifest.json 为准；源码配置和构建信息在原始 SBOM。Qt 未作源代码修改；windeployqt 可按官方流程修补 QtCore 中的部署路径，此改写以包内散列记录。

对应源码：

- QtBase 6.8.3：<https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz>
- QtDeclarative 6.8.3：<https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtdeclarative-everywhere-src-6.8.3.tar.xz>
- QtTools 6.8.3（部署工具源码）：<https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qttools-everywhere-src-6.8.3.tar.xz>。三个上游 v6.8.3 tag 的 peeled commit 均与供应商 SPDX 一致。原始归档 Qt6Core.dll、已安装 SDK 与部署副本逐字节相同；供应商 SHA1 与签名前规范化字节匹配。证据见 qt-phase8-origin.json。所有实际部署项仍逐文件保留来源、完整 SHA-256 和签名前比对，不能把任意不匹配自动归因于签名。
- PDFium `f51279699cb59754d1f49aef10eb8e825fe56532`：<https://pdfium.googlesource.com/pdfium/+/f51279699cb59754d1f49aef10eb8e825fe56532/>；二进制构建方脚本：<https://github.com/bblanchon/pdfium-binaries/tree/chromium/8100>。全部包内通知在 pdfium 子目录。
- Noto Sans SC 固定来源及派生命令见 dependencies.lock.json、项目 DEPENDENCIES.md。字体本体及 OFL 随包；项目不附带 Windows 系统字体。

允许用户替换兼容 Qt DLL / QML 插件并进行 LGPL 允许的调试、修改及逆向工程。本项目无额外 EULA 限制这些权利。对应本候选包的应用源码在 source.zip，可通过 BUILDING.md 和固定依赖重建；不要用仓库尚不存在的提交声称对应源码已公开。

MSVC CRT 只从已安装 Visual Studio 的 `VC/Redist/MSVC/<版本>/x64/Microsoft.VC*.CRT` 拷贝，以 manifest 的 runtime_source 和 DLL SHA-256 为证。Microsoft-Redist.txt 保留供应商清单入口；Microsoft 官方说明分发受 Visual Studio 许可与可分发清单约束。VS 2026 官方 redist 清单明确列出 VC/Redist 的分发路径；资格仍以发行主体持有的有效许可为条件，本记录不替用户作资格声明。

D3D11 编译器只取自 Windows SDK 10 的 Redist/D3D/x64/d3dcompiler_47.dll，附 sdk_license.rtf 与 sdk_third_party_notices.rtf；微软 SDK redist 清单列明可随 Classic Windows 应用分发该未修改文件，受 SDK 条款及有效许可限制。已通过部署选项排除不需要的 DXC/DXIL。来源和散列见 windows-runtime-evidence.json 及 manifest。

发布前仍需确认：发行主体的微软许可资格与最终分发方式、依赖安全更新、代码签名；macOS 构建/Metal/签名公证依用户要求延期。保留这些限制不等于已批准公开发布。

核对依据（2026-10-15）：[Qt 6.8 Windows deployment](https://doc.qt.io/qt-6.8/windows-deployment.html)、[Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)、[Microsoft runtime redistribution](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)。实际授权文本以随包组件为准。

进一步依据：[Windows SDK redist](https://learn.microsoft.com/en-us/legal/windows-sdk/redist)、[VS 2026 redist](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution)。本轮用户确认在当前本机验收，不把开发机结果称为干净 VM 证明。
