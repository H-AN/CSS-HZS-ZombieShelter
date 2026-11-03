# 开发与 API

独立源码位于 [`src/`](../src/)：主入口为 `HanZombieShelter.sp`，环境组件为 `HanZombieShelterEnvironment.sp`，主玩法模块在 `HanZombieShelter/`，公开接口在 `include/`。

`src/` 只保存 SourcePawn 源码和头文件。`plugin/` 是安装内容，保留随插件提供的源码副本；编译不会直接改写安装目录。

## 编译

准备 SourceMod 1.12 的 SourcePawn 编译器及配套标准 include，在仓库根目录运行 PowerShell：

```powershell
.\build.ps1 -SourceModRoot 'C:\SourceMod\addons\sourcemod'
```

也可分别指定工具链文件：

```powershell
.\build.ps1 -Compiler 'C:\SourceMod\addons\sourcemod\scripting\spcomp64.exe' `
    -SourceModInclude 'C:\SourceMod\addons\sourcemod\scripting\include'
```

未指定编译器时，脚本依次检查 `-SourceModRoot`、环境变量 `SOURCEMOD_ROOT` 和 PATH。未指定 include 时，使用编译器旁的 `include/`。示例路径替换为自己的工具链位置。

| 目标 | 命令参数 | 输出 |
| --- | --- | --- |
| 主玩法 | 默认，或 `-Target Main` | `build/HanZombieShelter.smx` |
| 昼夜环境 | `-Target Environment` | `build/HanZombieShelterEnvironment.smx` |
| 独立追击插件 | `-Target NPCFollow` | `build/HanZombieShelterNPCFollow.smx` |

第三个目标需要仓库中的 `optional/npc-follow-range/` 源码。独立主源码包只包含 Main 和 Environment 所需内容。

构建输出统一在 `build/`。检查编译结果后，再按安装说明自行部署；脚本不会启动游戏或自动替换 `plugin/` 中的 SMX。

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
