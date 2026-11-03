# 开发与 API

独立源码位于 [`src/`](../src/)：主入口为 `HanZombieShelter.sp`，环境组件为 `HanZombieShelterEnvironment.sp`，主玩法模块在 `HanZombieShelter/`，公开接口在 `include/`。

`src/` 只保存 SourcePawn 源码和头文件。`plugin/` 是安装内容，保留随插件提供的源码副本；编译不会直接改写安装目录。

## 编译

使用自己的 **SourceMod 1.12 或更高版本**编译环境，并使用该版本配套的 SourcePawn 编译器和标准 include。

1. 将 `src/HanZombieShelter.sp`、`src/HanZombieShelterEnvironment.sp` 和整个 `src/HanZombieShelter/` 目录复制到编译环境的 `addons/sourcemod/scripting/`，保留模块目录结构。
2. 将 `src/include/` 中的 `HanZombieScenarioAPI.inc`、`HanZombieShelterAPI.inc` 放入该环境的 `addons/sourcemod/scripting/include/`。保留 SourceMod 自带的标准头文件。
3. 在该环境的 `scripting/` 目录中，使用 SourceMod 自带编译器分别编译两个 `.sp` 入口。

例如，在 Windows PowerShell 中切换到自己的 `scripting/` 目录后运行：

```powershell
.\spcomp64.exe .\HanZombieShelter.sp -i.\include -o.\HanZombieShelter.smx
.\spcomp64.exe .\HanZombieShelterEnvironment.sp -i.\include -o.\HanZombieShelterEnvironment.smx
```

示例输出在当前 `scripting/` 目录。其他平台使用对应的 SourcePawn 编译器，源码和 include 的放置方式相同。检查编译结果后，按[安装与升级](安装与升级.md)将所需 SMX 部署到服务器。

需要自行编译可选追击插件时，将 [`HanZombieShelterNPCFollow.sp`](../optional/npc-follow-range/addons/sourcemod/scripting/HanZombieShelterNPCFollow.sp) 放到自己的编译环境 `scripting/` 后单独编译；该组件只需要 SourceMod 标准 include。安装要求见[独立追击插件说明](../optional/npc-follow-range/README.md)。

## 源码结构

主插件的项目输入为主 `.sp`、50 个模块 `.inc` 和两个 API 头文件；再加独立环境组件，共 54 个源码文件。模块之间保留相对 include 关系，标准 SourceMod 头文件由工具链提供。

| 范围 | 主要文件 |
| --- | --- |
| 数据与配置 | `Types.inc`、`Config.inc`、`RuntimeIndex.inc` |
| 地图与建造 | `Geometry.inc`、`MapAdjustments.inc`、`BuildAreas.inc`、`Construction.inc`、`RepairWeapons.inc` |
| 材料与合成 | `Inventory.inc`、`Gathering.inc`、`Drops.inc`、`Crafting.inc`、`CraftingMenu.inc` |
| 供电与防御 | `Power.inc`、`World.inc`、`Combat.inc` 及各设施模块 |
| NPC 与回合 | `NPC.inc`、`BossSpawns.inc`、`BossBridge.inc`、`Lifecycle.inc` |
| 技能与成长 | `Skills.inc`、`Progression.inc`、`Levels.inc` |
| 界面与扩展 | `HUD.inc`、`Commands.inc`、`Admin.inc`、`API.inc` |

部分模块由其他模块间接 include，不能只按主入口顶部的列表判断是否使用。环境组件根据公开环境 API 管理天空和灯光；独立追击插件不属于主插件构建或加载的前置。

## 翻译

正式文案位于 [`HanZombieShelter.phrases.txt`](../plugin/addons/sourcemod/translations/HanZombieShelter.phrases.txt)。修改时保持中英文键、格式参数和源码调用一致，确认没有遗漏其他功能正在使用的词条。

## 接入公开 API

以 [`src/include/HanZombieShelterAPI.inc`](../src/include/HanZombieShelterAPI.inc) 的声明和注释为准。库名为 `HanZombieShelter`，总体 API 版本为 **2**，环境 API 版本为 **1**。

API 提供玩法与环境状态、基地和建筑信息、电网、材料查询，以及基地选择菜单和安全回城。HZS 主体继续使用自身的 API。

| 接入规则 | 说明 |
| --- | --- |
| 调用前检查库和具体 native 是否可用 | 同一 API 版本内也可能增加能力 |
| 区分基地索引、建筑 UID、实体索引 | 三者不可互换；UID 只在当前提供方／地图会话内有效 |
| 查询建筑后检查 `alive` | 查询成功不代表建筑已完工或仍存活 |
| 收到通知后读取 Getter | 延后执行时再次查询，避免使用失效状态 |
| 旧材料数组只按固定五格使用 | 新材料通过 ID 接口查询 |
| 菜单回调重新检查玩家和 UID | 取消或过期可返回 UID 0；玩家离开时 client 也可能为 0 |

`HZSH_ShowBaseTeleportMenu` 只选择基地，不传送或扣费。`HZSH_TeleportPlayerToBase` 返回实际传送是否成功，调用方负责权限、付款和失败退款。

开发扩展时不要修改公共头文件中既有 native 的参数约定。新增配置、资源或接口时同步更新使用说明，发布前分别验证构建、配置兼容和实际游戏行为。
