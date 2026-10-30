# 图标资源

## 程序接入状态（2026-10-18）

应用现名 InkHorn。新增 `eraser-cursor.svg`：32 DIP 图形，左上 `(4,4)` DIP 小圈中心为光标热点，主体位于右下；独立于工具栏 `eraser.svg`。当前整笔橡皮不展示展开箭头或二级菜单，命中直径固定 4 DIP。该光标同样编译嵌入 EXE，并按 DPI 缓存。
界面现在优先使用此目录的 SVG，以 Direct2D 矢量路径绘制；无需把图标转换为 ICO，也不需要两份 `pen1.svg` / `pen2.svg`。两支笔共用 `pen.svg`，分别应用各自颜色。`rounded-rectangle`、`circle`、`line`、`arrow` 已接入形状工具；`chevron-left.svg` 用作已选工具左下角的展开箭头。

`pen-body` / `pen-tip`、`highlighter-body` / `highlighter-tip` 的填色随当前颜色改变，描边和白色握柄保持原样；编辑 SVG 时请保留这些 ID。SVG 缺失时才使用对应 ICO 与遮罩回退。提供的 ICO/PNG 原文件保持保留，不要求额外复制到 EXE 旁边。

应用窗口、任务栏与 EXE 的图标优先使用 `../logo/app-tohru.ico`；首页空状态也使用这套图标。它与工具 SVG 各司其职，Windows 应用图标仍需要 ICO。

构建时由 `scripts/Compile-Svg.py` 将当前图形编译为路径数据，Python 仅用于开发构建，终端用户无需安装。支持本批资源使用的 path `M/L/H/V/C/Q/A/Z`、rect、circle 和 translate/scale/matrix；不支持的复杂元素/属性会在构建时报错，不能按通用 SVG 浏览器处理任意资源。

## 新图标（SVG 源文件）

参考提供的工具栏截图绘制：细黑描边、白色握柄、蓝色笔身与笔尖、黄色荧光笔、灰色外壳和淡紫色橡皮。笔和荧光笔均水平朝左。当前 eraser.svg 已按最新要求改为传统斜块与底部横线，保留灰/淡紫配色和统一细描边；旧 ICO/PNG 预览仍为先前造型，运行时优先使用更新后的 SVG。

所有 SVG 使用 `viewBox="0 0 32 32"`，背景透明，可以任意缩放。SVG 是矢量格式，不区分 32 位位深；配套 PNG 为 8 位 RGBA（共 32 位），ICO 的每一帧均为 32 位 BGRA，并保留 Alpha 透明度。

| SVG | 图标 |
|---|---|
| `select.svg` | 鼠标指针 |
| `pen.svg` | 笔，水平朝左 |
| `eraser.svg` | 传统斜块橡皮擦与底部横线 |
| `highlighter.svg` | 荧光笔，水平朝左 |
| `rounded-rectangle.svg` | 圆角矩形 |
| `circle.svg` | 圆形 |
| `line.svg` | 左下至右上的直线 |
| `arrow.svg` | 指向右上的斜箭头 |
| `undo.svg` / `redo.svg` | 撤销 / 重做 |
| `save.svg` | 保存 |
| `chevron-left.svg` | 向左的 `<`，两条线夹角为 90°，直角连接 |

### 尺寸与预览

- `preview.png`：图标总览及四种颜色的 mask 叠加效果。预览中的棋盘格仅用于展示透明度，不包含在图标文件中。
- `png/16`、`png/24`、`png/32`、`png/48`、`png/64`、`png/256`：每个图标及遮罩的六档透明 PNG。
- 每个 SVG 都有同名 ICO；每个 ICO 内含 16、24、32、48、64、256 六档尺寸。
- 为兼容当前程序，另提供 ICO 副本：`pen1` / `pen2` 对应 `pen`，`pen1-mask` / `pen2-mask` 对应 `pen-mask`，`shapes` 对应 `rounded-rectangle`。
- 重新加载 CMake 并构建后，程序直接使用这些 SVG；圆形、直线、箭头和展开箭头均已接入。

### SVG 改色遮罩

`pen-mask.svg` 和 `highlighter-mask.svg` 是独立的白色 Alpha 遮罩，与基础图标的画布、坐标完全一致，只覆盖笔身和笔尖的彩色内部区域，保留黑色轮廓与白色握柄。

使用方式：先绘制基础图标，再将 mask 的非透明区域着为目标颜色，并以相同位置和尺寸叠加。遮罩的 RGB 不影响结果，Alpha 决定覆盖程度。它们也可以直接作为 CSS `mask-image` 的外部 SVG 图像使用；不能直接以白色图像叠加，否则会把彩色区域变白。

需要直接修改 SVG 时，可分别调整基础 SVG 中 `id="pen-body"` / `id="pen-tip"` 或 `id="highlighter-body"` / `id="highlighter-tip"` 元素的 `fill`。配套 mask 的笔身、笔尖也具有独立 ID，便于分区改色。

## 程序的 ICO 加载约定

把 ICO 文件放在这个目录，在 CLion 重新加载 CMake 后构建即可自动嵌入 EXE，无需分发外部图标目录。文件名约定：

| 文件 | 用途 |
|---|---|
| `app.ico` | 窗口与应用图标 |
| `select.ico` | 指针选择 |
| `pen1.ico` | 笔 1 |
| `pen2.ico` | 笔 2 |
| `eraser.ico` | 橡皮 |
| `highlighter.ico` | 荧光笔 |
| `shapes.ico` | 形状 |
| `undo.ico` / `redo.ico` | 撤销 / 重做 |
| `save.ico` | 保存 |

笔 1、笔 2、荧光笔在未选中时也显示各自当前颜色。没有相应 ICO 时显示程序自带的圆角矢量图标。

## 着色方式

- **简单方式**：提供带透明背景的单色 ICO。程序保留透明度，将整个笔图标着为当前颜色，原始 RGB 不参与着色。
- **只让笔尖或色条变色**：另提供 `pen1-mask.ico`、`pen2-mask.ico`、`highlighter-mask.ico`。基础图标原色保留，mask 只绘制可变色区域；mask 的透明度控制该区域着色。基础图标和 mask 的画布大小、边距和坐标必须完全一致。
- `select`、`eraser`、`shapes`、`undo`、`redo`、`save` 保留原始颜色。

建议图标包含 16、24、32、48、64、256 像素版本，使用 32 位带 alpha 的 ICO。主体留少量边距，避免贴边或不透明底色。当前图标单元约 24 DIP，程序以 64 像素解码后平滑缩放。

首次加入文件请重新加载 CMake；替换同名图标后执行正常构建即可。尚未编译时，已有 EXE 不会反映资源变化。
