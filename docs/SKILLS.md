# 项目内 Skills 清单

安装日期：2026-10-15。范围：仅当前项目，目录为 `.agents/skills/`；没有安装到用户级 skills 目录，也没有更改全局 Codex 配置。

Codex 支持从项目的 `.agents/skills/` 发现技能，见 [OpenAI 官方说明](https://learn.chatgpt.com/docs/build-skills#where-codex-loads-local-skills)。安装后下一轮可用；若列表尚未刷新，重新打开本项目会话。

## 已安装与使用时机

| Skill | 用途 | 来源 | 固定提交 |
| --- | --- | --- | --- |
| [cpp-pro](../.agents/skills/cpp-pro/SKILL.md) | C++20 资源所有权、内存、并发和构建工具指导 | [Jeffallan/claude-skills](https://github.com/Jeffallan/claude-skills/tree/1be15d8064f88fc25216442406d40add8fd23b53/skills/cpp-pro) | `1be15d8064f88fc25216442406d40add8fd23b53` |
| [qt-cmake-project](../.agents/skills/qt-cmake-project/SKILL.md) | Qt 6 CMake、QML 模块和资源配置 | [TheQtCompanyRnD/agent-skills](https://github.com/TheQtCompanyRnD/agent-skills/tree/a784c48445e34f126760f17ba0600a25f2d19968/skills/qt-cmake-project) | `a784c48445e34f126760f17ba0600a25f2d19968` |
| [qt-qml](../.agents/skills/qt-qml/SKILL.md) | QML 实现、布局、绑定、组件与交互 | [TheQtCompanyRnD/agent-skills](https://github.com/TheQtCompanyRnD/agent-skills/tree/a784c48445e34f126760f17ba0600a25f2d19968/skills/qt-qml) | 同上 |
| [qt-qml-profiler](../.agents/skills/qt-qml-profiler/SKILL.md) | Qt Quick 2D trace 解析和热点诊断 | [TheQtCompanyRnD/agent-skills](https://github.com/TheQtCompanyRnD/agent-skills/tree/a784c48445e34f126760f17ba0600a25f2d19968/skills/qt-qml-profiler) | 同上 |

安装内容包括各自的引用资料和 profiler 的 Python trace 解析器，没有启动这些技能的开发/分析流程，没有安装 Qt、PDFium、MCP 服务或任何应用运行时依赖。

本轮执行的是搜索、来源与指令审阅、定点下载安装和文件完整性检查。尚未用实际应用验证技能生成的 CMake/QML，也未使用真实 trace 验证解析器的统计准确性。

## 筛选依据

先检查 skills.sh 目录，再使用 Skills CLI 搜索 `qt qml`、`cmake cpp`、`cpp performance`、`cmake` 和 `pdfium`。安装前读取候选 `SKILL.md`、检查引用与可执行内容，并核对来源仓库和许可证。

搜索时的近似快照，不是质量保证或实时统计：

| 来源 / 技能 | 搜索所见安装数 | GitHub API 所见仓库星数 |
| --- | --- | --- |
| Jeffallan / cpp-pro | 约 5.1K | 11,880 |
| Qt / qt-cmake-project | 445 | 481 |
| Qt / qt-qml | 971 | 481 |
| Qt / qt-qml-profiler | 628 | 481 |

选择 Qt 团队维护的直接技术栈技能，辅以使用量较高的 C++ 技能；不按排行榜批量安装。Qt 候选的安装量低于 1K，但维护来源和具体内容比广泛的 Web 开发技能更适合本项目。所有来源都按提交固定，不跟随浮动 `main` 更新。

本轮未引入：

- PDFium 专项候选：搜索结果中的使用量很低，不能证明其内容和维护成熟度；线程、字体和导出仍优先核对官方源码。
- Qt 的独立 review/test 系列：当前无实现代码，部分流程要求较重或包含额外工具假设；先使用项目已制定的验证方案，后续按真实需求补充。
- Web、游戏引擎、OCR、AI、云和通用 PDF 制作文档技能：与固定技术栈或第一版范围不匹配。

## 项目适配与已知边界

保留上游技能内容，项目适配规则集中在 [AGENTS.md 第 13 节](../AGENTS.md)。特别注意：

- C++23、智能指针、sanitizer 和编译器 flags 示例必须按 C++20 / QObject / QSG / MSVC / Clang 的实际情况适配。
- 通用 QML 指导有简化或过度概括的性能描述。`readonly` 不是只计算一次；`clip` 和普通 `opacity` 不是必然离屏渲染的证据。修改前需查所用 Qt 版本与实测。
- profiler 的解析器使用 trace 动画统计生成估计值，不能替代性能方案中的真实帧/系统呈现与进程资源测量。其单次报告模板不限制项目做 A/B 比较。
- 现阶段没有可运行应用，安装 profiler skill 不代表已经能执行性能采样；预览 MCP 也不是必需依赖。
- 本地安装的技能不会改变“只规划 / 仅阶段 0 实施”的边界，不自动推动后续阶段。

## 许可证与来源保留

| 内容 | 上游许可 | 本项目保留方式 |
| --- | --- | --- |
| cpp-pro 及引用资料 | MIT | 技能目录补充从同一上游提交取得的 `LICENSE.txt`，不改写原始许可文本 |
| 三项 Qt skills 及资料/解析器 | `LicenseRef-Qt-Commercial OR BSD-3-Clause` | 本次按 BSD-3-Clause 选项保留各目录原有 `LICENSE.txt` 与版权声明 |

这些是开发辅助技能的许可证，不是 Qt SDK、PDFium、项目应用或最终发行包的许可证审查结论。产品依赖许可仍属于阶段 0 的独立任务。

`.agents/skills.lock.json` 保存仓库、提交、技能路径、许可证和安装文件 SHA-256。文件校验用于发现本地修改和复现安装，不能证明上游内容没有缺陷。

## 更新与移除

1. 更新前先审查目标提交中技能、引用资料、脚本和许可证的差异，检查项目适配规则是否仍然适用。
2. 将候选下载到临时审阅目录，确认后只替换当前项目对应目录；不使用全局安装选项，不盲目运行批量更新。
3. 保留随包许可证，更新锁文件中的提交与文件散列，并记录本地修改。
4. 移除时只删除 `.agents/skills/<对应名称>/`，同步更新清单、锁文件与 AGENTS 引用；不触及用户级 skills。

手动复现可用自带 `skill-installer` 的 `install-skill-from-github.py`，传入清单中的 `--repo`、完整 `--ref`、`--path skills/<名称>` 和指向本项目 `.agents/skills` 的 `--dest`。该脚本遇到已存在目录会拒绝覆盖，应先在独立临时目录审查，不能以递归删除整个 `.agents` 目录解决。
