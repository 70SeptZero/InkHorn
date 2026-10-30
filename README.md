<p align="center">
  <img src="./assets/logo/png/logo-tohru-128.png" alt="InkHorn 图标" width="96" height="96">
</p>

# InkHorn

面向手写笔的 Windows PDF 阅读与批注工具。

InkHorn 将连续页面阅读、压感书写与 PDF 批注保存放在一个原生桌面应用中，适合阅读讲义、标记资料和手写笔记。采用 C++20、Win32、Direct2D / DirectWrite 与 PDFium 构建，中文界面，支持单 EXE 便携运行。

**当前版本：0.1.0 · 开发中。** 阅读、书写和保存的基础流程已实现；真实设备手感、复杂文档性能及跨阅读器兼容性仍在持续验证。

## 功能

| 方向 | 已支持 |
| --- | --- |
| PDF 阅读 | 多文档切换、纵向连续页面、页码跳转、缩放、全屏、最近文件与阅读位置记忆 |
| 手写批注 | 两支独立配置的压感笔、线宽与颜色设置、自由荧光笔、自定义 HEX 颜色 |
| 图形与编辑 | 矩形、椭圆、直线、箭头，整笔擦除，选中后移动或删除，撤销与重做 |
| 输入与布局 | Windows Pointer / Windows Ink、笔侧键临时橡皮、鼠标与笔自动切换工具、左手模式、可拖动工具栏 |
| 阅读手感 | 平滑滚轮、页面空白区域拖动、触摸平移与惯性滚动 |
| 保存与恢复 | 保存原文件或另存为 PDF、批注显隐、后台恢复记录、工具偏好记忆 |

笔迹会随批注写入 PDF，同时保存标准 Ink 轨迹、矢量外观和 InkHorn 的可编辑数据。重新用 InkHorn 打开后可继续编辑；在其他阅读器中的显示仍需结合具体软件验证。

## 开始使用

使用构建得到的 `InkHorn.exe`，将它放在可写目录中双击运行。正式 Release 构建静态链接 PDFium 和 MSVC 运行库，图标与第三方许可文本嵌入程序，运行时无需另装 Python、PDFium DLL 或 VC++ 运行库。

当前主要验证平台为 **Windows 11 x64**；Windows 10 x64 是兼容目标，ARM64 尚未验证。源码仓库不包含已忽略的 `dist/` 和构建产物，源码编译步骤见下文。

1. 点击首页“打开 PDF”，或按 `Ctrl+O` 选择文档。
2. 选择笔、荧光笔或形状工具，在页面上批注。再次点击已选工具可展开颜色、线宽或形状设置。
3. 使用整笔橡皮擦除笔迹，或切换选择工具后移动、删除已有对象。
4. 按 `Ctrl+S` 保存，或按 `Ctrl+Shift+S` 另存为。

使用压感笔时，请在驱动中启用 Windows Ink。侧键临时橡皮依赖设备报告的按键标志；若侧键被驱动映射为快捷键，需检查驱动配置。

鼠标处于指针工具时，笔进入感应范围会自动切换到笔 1；若已手动选择其他绘图工具，则沿用所选工具。笔输入后重新使用真实鼠标会回到指针工具。

### 常用快捷键

| 快捷键 | 操作 |
| --- | --- |
| `Ctrl+O` | 打开 PDF |
| `Ctrl+S` / `Ctrl+Shift+S` | 保存 / 另存为 |
| `Ctrl+W` | 关闭当前文档 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 切换到下一个 / 上一个文档 |
| `Ctrl+Z` / `Ctrl+Y` | 撤销 / 重做 |
| `Ctrl+G` | 输入页码 |
| `Ctrl+滚轮` | 缩放页面 |
| `1` / `2` / `3` | 选择工具 / 笔 1 / 笔 2 |
| `E` / `H` / `S` | 橡皮 / 荧光笔 / 形状 |
| `Delete` | 删除选中对象 |
| `F11` | 切换全屏 |
| `Esc` | 关闭面板、取消当前操作或退出全屏 |

### 文件与个人数据

设置、最近文件、阅读位置、恢复记录和诊断日志保存在 **EXE 同级的 `InkHorn-data/`**，因此程序所在目录需要可写。迁移个人状态时，将这个目录与 EXE 一起复制；分享已保存的批注时，只需分享 PDF。

保存采用临时文件写入、重开检查和替换原文件的流程。恢复记录用于异常退出后找回未保存笔记，不能代替主动保存；源 PDF 在外部发生变化时，程序会拒绝静默覆盖。应用内隐藏批注只影响当前查看，正常保存的 PDF 仍显示笔记。

## 从源码构建

### 环境要求

- Windows x64。
- Visual Studio 或 Build Tools，安装“使用 C++ 的桌面开发”与 MSVC x64 工具链。当前开发环境使用 Visual Studio 2026。
- **Windows SDK 10.0.28000.0**：当前锁定的 PDFium 构建脚本会检查此版本。
- CMake **3.25 或更新版本**、Ninja、Git for Windows、Python 3。
- PowerShell；CLion 可选。

正式 PDF 功能使用 MSVC ABI 的静态 PDFium，CLion 中应选择 **Visual Studio / amd64** 工具链。

### 1. 准备 PDFium

在仓库根目录（即包含 `CMakeLists.txt` 的 `InkHorn` 目录）打开 PowerShell，执行：

```powershell
.\scripts\Prepare-Pdfium.ps1 -CheckOnly
.\scripts\Prepare-Pdfium.ps1 -Jobs 4
```

脚本根据 [`dependencies.lock.json`](./dependencies.lock.json) 获取锁定版本的 PDFium 与 depot_tools，并从源码构建静态库。首次准备需要联网下载源码及工具，耗时和磁盘占用明显高于主程序构建。

依赖源码和缓存位于 `third_party/source/`，生成的 SDK 位于 `third_party/pdfium/`，两者都已加入忽略规则。依赖源码路径不能含空格；必要时通过 `-DependencyDirectory` 指定其他目录。需要代理时可传入 `-Proxy http://127.0.0.1:端口`；依赖已完整同步、仅重新编译时可使用 `-SkipSync`。

### 2. 编译并运行测试

继续在仓库根目录执行：

```powershell
.\scripts\Build.ps1 -Preset release -Test
```

脚本会载入 Visual Studio amd64 开发环境，执行 CMake 配置、编译和 CTest。CMake 与 Ninja 可放入 `PATH`，也可通过 `CLION_HOME` 或脚本参数 `-CMake`、`-Ninja` 指定位置。

Release 程序输出到：

```text
build/release/bin/InkHorn.exe
```

需要调试版本时执行：

```powershell
.\scripts\Build.ps1 -Preset debug -Test
```

Debug 输出到 `build/clion-debug/`。在 CLion 中打开仓库根目录的 `CMakeLists.txt`，选择 Visual Studio / amd64 工具链，加载 `release` 或 `debug` 预设并运行 `InkHorn` 目标。

### 3. 整理分发文件

在仓库根目录执行以下命令；`cmake` 需在 `PATH` 中，也可替换为其完整路径：

```powershell
cmake --install build/release --prefix dist
```

生成的 `dist/InkHorn.exe` 可单独复制运行。首次分发无需携带源码、构建缓存或个人 `InkHorn-data/`。

## 当前限制

- 橡皮目前为整笔擦除；局部擦除、套索和多选尚未完成。
- 已有笔迹支持移动、删除，暂不支持缩放、旋转、复制或修改颜色与线宽。
- 文字吸附高亮、正文搜索与复制、目录与书签、干净副本导出、停顿拉直、双指缩放、完整无障碍支持及外来批注编辑尚未完成。
- 加密 PDF 的密码输入界面尚未实现；签名 PDF 或不允许批注的文档会进入保护性拒绝保存状态。
- 真机长时间书写、复杂 PDF 性能以及 Adobe / Edge 等阅读器的批注显示仍需继续验证，历史报告的偶发停顿尚不能视为完全解决。

2026-10-21 的开发验证记录为 Release、Debug 各 10 项自动检查通过，覆盖笔迹与历史、恢复存储、输入、PDF 保存重开、界面冒烟及绘制质量等。合成性能测试不代表真实手写设备到屏幕的端到端延迟；当前检出的代码可通过上述 `-Test` 命令重新验证。

## 项目结构

```text
.
├── README.md
├── .gitignore
├── src/                       # 原生界面、输入、笔迹、PDF 与存储
├── assets/                    # 图标、应用资源与许可资源模板
├── scripts/                   # 依赖准备、构建与 SVG 资源编译
├── tests/                     # 核心、输入、存储与 PDF 等测试
├── CMakeLists.txt
├── CMakePresets.json
├── dependencies.lock.json
├── THIRD_PARTY_NOTICES.txt
└── LICENSE                    # MIT 许可证
```

## 开发入口

- [依赖准备脚本](./scripts/Prepare-Pdfium.ps1)
- [编译与测试脚本](./scripts/Build.ps1)
- [构建预设](./CMakePresets.json)
- [自动化测试](./tests/)
- [工具图标说明](./assets/icons/README.md)

需要诊断笔侧键输入时，按 `Ctrl+Shift+D` 启停记录，按 `Ctrl+Shift+L` 打开日志目录。日志位于 EXE 同级的 `InkHorn-data/diagnostics/`，属于本地运行数据，已忽略。

GitHub 仓库以本目录为根目录；只提交本目录下的源码和资源即可。构建目录、依赖缓存、个人运行数据和本地配置由 [`.gitignore`](./.gitignore) 排除，发布 EXE 可单独上传到 GitHub Releases。

## 许可证

项目代码采用 [MIT License](./LICENSE)。

PDFium 及其第三方依赖遵循各自的许可证，见 [第三方声明](./THIRD_PARTY_NOTICES.txt)。依赖准备脚本会收集实际检出的许可文本，正式构建将完整声明嵌入 EXE，可从应用功能菜单查看。
