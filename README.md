# LHolo v26.51.2-ja.11-rc.1 — Rendering Performance, Bubble & Outline Update

**Prerelease** for Minecraft Bedrock Windows **1.26.51.01**, LeviLamina Client **26.51.5** (Windows x64).

Changes: upstream-derived frustum-aware section rendering, draw-list caching, stable schematic load origin, native bubble-column geometry fallback, and shared selection/correction outline handling. Client-only Alt + right-click material grab remains available with its opt-in flag.

See [ja.11 release details](docs/RELEASE_JA11_RENDER_20261026.md) and [GitHub Releases](https://github.com/rometet/LHolo/releases).

---
# LHolo v26.51.2-ja.10-rc.1 — Client-only Material Grab

**Prerelease** for Minecraft Bedrock Windows **1.26.51.01** + LeviLamina Client **26.51.5** (x64).

- **Schematic material grab:** Hold **Alt + right-click** on a chest while a schematic has missing materials. LHolo requests only needed items without a **server-side add-on**.
- The silent container path keeps the world HUD visible during the grab. The user confirmed this in a local world and an external server on 2026-10-26.
- Other changes include Easy Place rotation and responsiveness, material/verifier HUD consistency, and bubble/waterlogged projection improvements.
- The client-only grab path is **opt-in**: create the empty file `mods/LHolo/config/client_only_grab.enabled` under your LeviLauncher instance and restart Minecraft. Without it, the previous hidden-UI grab path remains active.
- This is a prerelease. Test first with disposable inventory. Not all containers, world types and servers have been tested.
- See [release details](docs/RELEASE_JA10_CLIENT_ONLY_20261026.md).

---

# LHolo Glass R5 + Praxis Resource ABI v4

Experimental prerelease, designed for PraxisCompanion v0.1.3-rc.1 (R33).

Includes the genuine native LHolo resource_reload_v4 participant with
GPU/owner lifetime checks, plus Glass R5 projection rendering fixes.

2026-10-21: user-confirmed that RP OFF in a disposable offline cloned world
left the third-person avatar visible with Praxis R33. Both participants
resumed the new resource epoch. This is not tested for multiplayer, servers
or general production worlds. The current Praxis gate admits only one
ordinary local texture pack in the exact disposable clone.

LHolo settings-save Access is denied and hook teardown warnings remain.
See docs/R5_V4_RESOURCE_RELOAD_RELEASE.md.

---

# LHolo

[日本語](docs/README_JA.md) | [English](docs/README_EN.md)

> 此分支为日语/Praxis 风格界面分支。默认语言为日语，默认菜单键为 `Insert`。

`LHolo` 是一个 [LeviLamina](https://github.com/LiteLDev/LeviLamina) 客户端投影模组

## 版本号说明

示例版本：`v26.51.2`

- `26.51`：对应的 Minecraft 游戏版本
- `.2`：模组在该游戏版本下的第 2 个版本

## 快速上手

[安装教程](https://www.bilibili.com/opus/1239631121935761412)

1. 安装本模组以后,聊天栏输入 `LHolo`,或按 `Insert` 打开模组菜单
2. 本模组交互为GUI与快捷键(具体快捷键直接看模组菜单)
3. 使用本模组时,请关闭灵动视效

## 主要功能

**文件支持**

- 支持 `.mcstructure` 和 `.litematic`(Java 版 Litematica 蓝图)两种文件

**纠错**

- 蓝色 = 未放置投影方块
- 红色 = 方块放置错误
- 黄色 = 朝向或状态错误
- 品红色 = 多余方块

**分层建造**

- X或Y轴分层显示
- 材料分层显示

**结构变换**

- 移动
- 旋转 
- 镜像

**HUD 信息显示**

- 屏幕角落可显示:蓝图文件名、当前显示的层、建造进度(已放/总数)、各类错误数量、当前辅助放置模式等。
- 位置可放在屏幕四角,每一项都能单独开关。

**多语言支持**

- 日本語（此分支默认）
- 简体中文
- 繁體中文
- English
- Русский

## 模组演示

以下截图来自旧版界面；本分支使用日语默认语言和中央 Praxis 风格菜单。

**模组菜单页面**

![image-20260918202008186](docs/images/image-20260918202008186.png)

**加载结构**

![image-20260918200440220](docs/images/image-20260918200440220.png)

**纠错**

![image-20260918201002570](docs/images/image-20260918201002570.png)

**材料清单**

![image-20260918201318315](docs/images/image-20260918201318315.png)

**语言设置**

入口：打开模组主菜单 -> 界面设置 -> 语言

![image-20260918202312106](docs/images/image-20260918202312106.png)



****
