# 参与开发

首先阅读 [AGENTS.md](AGENTS.md)。默认中文文档，代码标识符使用英文，C++20，不改全局配置或用户级 skills。用户最新授权为完成阶段 8 的 Windows 范围，macOS 检查延至整个项目完成后；未执行的 Windows 验收仍须明确记录。原阶段 0 未验证项目继续保留。

每次变更按风险运行构建、CTest 和真实 PDF 集成检查。线程、坐标、失效、文件保护等失败必须先修复。性能数据存于 `benchmarks/results/<新的 run-id>/`；不可把计划、CI YAML 或模拟图像描述为已运行证据。真实教材、个人答案和私有截图不提交。

使用固定依赖和源码散列；阶段 0 初次验证时没有 Git 元数据，旧报告不虚构当时的提交、分支或 PR。后续提交和 CI 结果以实际 Git/GitHub 记录为准。所有平台结果分别记录。


阶段 8 的 Windows 变更请运行 `./tools/build-windows.ps1 -Test -Lint`。打包或依赖变更还需使用新的输出目录运行 package-windows 和 validate-windows-package，核对 manifest 与源码快照。基准期间避免并行窗口测试/编译负载；诊断、sanitizer 与正式性能结果分目录。私有 PDF 和 `benchmarks/results/` 不进入源码快照或候选包。发布审批与 macOS 实机检查的未解决项见阶段 8 报告，不得把 CI 配置当作远程运行证据。
