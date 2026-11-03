# V1.0.1 GitHub 上传与发布说明

本次仓库：<https://github.com/70SeptZero/InkHorn>。以下命令均在 **InkHorn 仓库根目录**执行，即包含 `CMakeLists.txt` 的目录。

## 本次发布信息

- 标签：`V1.0.1`，保持既有 `V1.0.0` 的大写标签格式。
- Release 标题：`InkHorn V1.0.1 — 窗口拖出合并与正文搜索`。
- 发布类型：开发预览版，勾选 **Set as a pre-release**。
- 程序版本：`1.0.1`，Windows 文件版本与清单版本为 `1.0.1.0`。
- Release 正文：直接复制 [RELEASE_NOTES_2026-11-03.md](./RELEASE_NOTES_2026-11-03.md)。

## 上传源码

本次包括窗口拖出/合并、正文搜索/复制、触摸板识别及对应检查；应把本次尚未提交的相关源码一起提交。新建的头文件与测试文件也需要加入 Git。

```powershell
git status --short
git diff --check

$releasePaths = @(
  'CMakeLists.txt', 'README.md', 'assets/app.rc', 'assets/app.manifest',
  'src/app.cpp', 'src/input_diagnostics.cpp', 'src/input_diagnostics.hpp',
  'src/pdf_engine.cpp', 'src/pdf_engine.hpp', 'src/pdf_text.hpp',
  'src/pen_input.hpp', 'src/ui.cpp', 'src/window_routing.hpp',
  'tests/input_tests.cpp', 'tests/pdf_tests.cpp', 'tests/text_tests.cpp', 'tests/window_tests.cpp',
  'docs/GITHUB_UPLOAD_2026-11-03.md', 'docs/RELEASE_NOTES_2026-11-03.md',
  'docs/PORTABLE_README_zh-CN.txt'
)
git add -- $releasePaths
git diff --cached --check
git diff --cached --stat
git diff --cached --name-only
```

暂存内容应为上述源码、测试、版本资源与文档。`dist/`、`build/`、依赖缓存、`InkHorn-data/`、本地 PDF、日志与 IDE 配置留在本机，EXE 和 ZIP 作为 Release 附件上传。外层 PdfBorad 工作区不属于本仓库。

确认暂存内容后提交；若使用 GitHub Desktop，同样选择 InkHorn 仓库，检查上述文件再 Commit / Push。

建议提交标题：`feat: add window drag merging and PDF text search`。

建议提交说明：

```text
Add single-instance file routing and in-memory document transfer between windows.
Support file-list tear-off, hover-to-merge with insertion feedback, multi-monitor
hit testing, and 60% detached windows from maximized/fullscreen mode.

Add PDF text search, single-page selection/copy, touchpad source recognition,
and regression coverage. Set release and Windows resource versions to 1.0.1.
```

```powershell
git commit -m "feat: add window drag merging and PDF text search"
git fetch origin
git status -sb
git log --oneline HEAD..origin/main
```

若远端 `main` 有新增提交，先整合并完成验证，再推送。远端没有待整合提交时：

```powershell
git push origin main
git tag -a V1.0.1 -m "InkHorn V1.0.1"
git push origin V1.0.1
```

标签应指向本次完整源码提交。若提交前后又修改了会影响程序的源码或版本资源，需要重新构建、测试和制作附件，使标签与二进制对应。

## 发布附件

已准备好的附件位于本地 `dist/releases/V1.0.1/`：

| 附件 | 用途 |
| --- | --- |
| `InkHorn-1.0.1-windows-x64.zip` | 推荐下载包，含 EXE、新版使用说明、MIT 许可与完整第三方许可 |
| `InkHorn.exe` | 供已有用户直接替换程序；与 ZIP 内的 EXE 一致 |
| `SHA256SUMS.txt` | ZIP 与独立 EXE 的 SHA-256 |

ZIP 内只有 `InkHorn.exe`、`使用说明.txt`、`LICENSE.txt`、`THIRD_PARTY_NOTICES.txt` 四个文件。制作附件时只取这些公开文件，不打包整个本地 dist 目录。

本地 `dist/使用说明.txt` 原文件保留；发布 ZIP 使用 [新版便携使用说明](./PORTABLE_README_zh-CN.txt)。完整第三方许可取自本次构建使用的 PDFium SDK，并已同时嵌入 EXE。

## 在 GitHub 创建 Release

打开 [新建 Release](https://github.com/70SeptZero/InkHorn/releases/new)，选择已推送的 `V1.0.1` 标签，填写上述标题，粘贴 Release 正文，上传表中的三个附件，勾选预览版后发布。

程序与便携包主要面向 Windows 11 x64。GitHub 自动提供的 Source code ZIP/TAR 是源码包，普通用户应下载手工上传的 Windows x64 附件。

## 本地验证记录

- 版本同步后，Release、Debug 构建与各 12 项 CTest 均通过。
- dist 通过 CMake install 更新，EXE 与 Release 构建产物 SHA-256 一致，dist 的 `--smoke-test` 通过。
- dist 原有的个人数据与附带文件在更新前后逐项核对路径、长度与 SHA-256；发布包仅包含公开文件。
- 用户已确认本轮窗口拖出/合并、副屏修复及搜索间距表现正常。两块自动检查用屏幕均为 150% 缩放，不同 DPI 组合及更多设备仍需后续验证。

本说明只准备上传步骤与材料；提交、推送、创建远端标签和发布 Release 由维护者执行。
