# LHolo | 简体中文说明

[日本語 / 主页](../README.md) · [English](README_EN.md)

**LHolo** 是用于 Minecraft Bedrock Edition（Windows）的客户端结构投影与辅助建造 Mod，基于 [MarmieQi/LHolo](https://github.com/MarmieQi/LHolo)。

支持读取 Bedrock `.mcstructure` 与 Java Litematica `.litematic` 文件。本分支默认使用**日语**和居中的 Praxis 风格菜单。

> 当前公开预览版：`v26.60.0-port.6`。适用于 **Minecraft Bedrock 1.26.60.05 / LeviLamina Client 26.60.x / Windows x64**。面向 Minecraft 1.26.51 的旧版本（`v26.51.2-ja.x` 系列）可在历史标签中获取。

## 下载与安装

1. 安装与游戏版本匹配的 [LeviLamina Client](https://github.com/LiteLDev/LeviLamina)。
2. 从 [Releases](https://github.com/rometet/LHolo/releases) 下载对应版本的 Windows x64 客户端 ZIP。
3. 关闭游戏，将压缩包里的 `LHolo` 文件夹放入当前游戏实例的 `mods/` 文件夹；更新之前先备份原有文件。
4. 进入世界，按 **`Insert`** 或在聊天栏输入 `LHolo` 打开菜单。

## 主要功能

- **结构投影：** 加载 `.mcstructure` 和 `.litematic` 文件。
- **变换与图层：** 移动、旋转、镜像、分层显示及管理多个投影。
- **方块纠错：** 蓝色＝未放置，红色＝方块类型错误，黄色＝方向／状态错误，紫红色＝多余方块。
- **材料与 HUD：** 显示缺少的材料、建造进度、各类错误和相关信息。
- **建造辅助（实验性）：** 手动放置、Easy Place、范围放置等。
- **结构导出：** 将指定区域导出为 `.mcstructure`。

### 常用快捷键

| 功能 | 默认按键 |
| --- | --- |
| 打开／关闭菜单 | `Insert` |
| 水平移动投影 | `Ctrl + 方向键` |
| 上下移动投影 | `Shift + ↑ / ↓` |
| 切换图层 | `Alt + ↑ / ↓` |
| 沿视线方向移动 | `Alt + 鼠标滚轮` |

进入世界后，在 **「ファイル」（文件）** 页面选择结构文件；在 **「配置」「変形」** 页面调整位置，在 **「検証」「材料」** 页面查看纠错和材料信息。也可以在菜单里切换界面语言和修改快捷键。

## 实验性：无界面材料获取

从 ja.12 开始，可以在 **「実験的機能」（实验性功能）** 中开启 Silent Material Grab（默认关闭）。当投影有缺少的材料时，对包含这些材料的箱子按住 **`Alt` 并右键**，即可尝试获取所需物品。无需服务器附加组件，也无需手动创建标记文件。关闭此选项即可恢复普通箱子操作。

此功能尚未在所有服务器及箱子类型上验证，请先用不重要的物品测试。实验性建造辅助也可能受到服务器规则限制。

## 注意事项

- 建议关闭 **Vibrant Visuals（灵动视效）**，以减少投影渲染兼容性问题。
- 投影本身只在客户端显示；实际方块放置和物品移动仍受服务器控制。
- 当前预览版可能存在已知限制，请备份重要世界和物品。
- 旧版截图不一定与当前界面一致。

[更新历史](../CHANGELOG.md) · [版本发布](https://github.com/rometet/LHolo/releases) · [许可证](../LICENSE)
