# InkHorn 程序 Logo

## 托尔元素版

以用户提供的 `Tohru.png` 为角色参考，用内置 imagegen 将 Q 版托尔头像融入原 Logo：金发、分节龙角、女仆头饰和眨眼表情，保留文档、触控笔、手写笔迹及荧光标记。

- `logo-tohru.png`：1254 × 1254 透明 RGBA 原图。
- `app-tohru.ico`：16、24、32、48、64、128、256 像素的 32 位 Alpha Windows 图标。
- `png/logo-tohru-{尺寸}.png`：上述七档尺寸，另含 512、1024 像素 PNG。
- `preview-tohru.png`：深浅背景及小尺寸预览。
- `generation-prompt-tohru.txt`：本版本的完整编辑提示词。

原 Logo 文件保留。所有新版 ICO 帧已验证与同尺寸 PNG 像素一致。

## 初版

设计以白色折角文档、蓝色手写笔迹和触控笔、黄色荧光标记组成，表现 PDF 手写批注功能。背景透明，文档区域为不透明白色。

## 文件

- `logo.png`：1254 × 1254 像素透明原图，RGBA。
- `app.ico`：Windows 应用图标，内含 16、24、32、48、64、128、256 像素，每档均为 32 位 Alpha。
- `png/logo-{尺寸}.png`：16、24、32、48、64、128、256、512、1024 像素透明 PNG。
- `preview.png`：浅色、深色背景及小尺寸预览，仅用于检查效果。
- `generation-prompt.txt`：生成原图使用的完整提示词。

原图使用内置 imagegen 工具生成；PNG 缩放及 ICO 封装保留 Alpha。所有 ICO 帧已验证与同尺寸 PNG 的像素一致。

当前项目从 `assets/icons/app.ico` 嵌入程序图标；本目录保存 Logo 资源。需要应用到 EXE 时，可将本目录 `app.ico` 复制到该路径，然后重新加载 CMake 并构建。
