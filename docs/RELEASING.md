# 发布流程（Windows）

正式发行包在维护者本机打包，不使用 CI 产物。原因：完整法语词库来自每周更新的 kaikki.org 文件，`tools/bootstrap-french-dictionary.py` 只接受锁定散列的那一版，CI 无法重新获取；CI 只用 23 条样例词条测试查词。CI 仍然负责每次推送的构建、测试和打包校验。

## 步骤

1. **版本号**：修改 `CMakeLists.txt` 的 `project(LinguaPDF VERSION x.y.z)`。exe 版本信息、`--version` 输出和包名都从这里读取。
2. **文档**：更新 `CHANGELOG.md`，新增 `docs/release-notes/vx.y.z.md`。依赖有变化时，同步更新 `third_party/DISTRIBUTION.md` 和 `THIRD-PARTY-NOTICES.md`。
3. **提交并推送**，等待 GitHub Actions 中该提交的工作流通过。
4. **从干净工作区构建**（`git status` 为空），然后用固定真实教材做回归：
   ```powershell
   ./tools/build-windows.ps1 -BuildDir build/release -Test -Lint
   ```
   请在 Windows PowerShell 中直接运行，不要用 `*>` 重定向输出：5.1 版会把 CMake 写到 stderr 的警告当成错误。
5. **打包并校验**（输出目录必须不存在）：
   ```powershell
   ./tools/package-windows.ps1 -BuildDir build/release -OutputDir build/packages/LinguaPDF-x.y.z-windows-x64
   .tools/venv/Scripts/python -X utf8 tools/validate-windows-package.py --package build/packages/LinguaPDF-x.y.z-windows-x64 --build build/release --out benchmarks/results/release-x.y.z
   ```
   检查 `licenses/deployed-binaries.json`，不能出现 `Unmapped binary`。
6. **打标签并发布**：
   ```powershell
   git tag -a vx.y.z -m "LinguaPDF x.y.z"
   git push origin vx.y.z
   gh release create vx.y.z build/packages/LinguaPDF-x.y.z-windows-x64.zip build/packages/LinguaPDF-x.y.z-windows-x64.sha256 --title "LinguaPDF x.y.z" --notes-file docs/release-notes/vx.y.z.md --verify-tag
   ```
7. 确认标签推送触发的工作流通过，并下载 Release 中的 ZIP 核对 SHA-256。

## 签名

当前版本不签名。如果以后需要，可以申请 [SignPath Foundation](https://signpath.org/) 的开源项目免费签名，接入时在步骤 5 之后签名 exe 和 DLL，再重新生成 manifest。
