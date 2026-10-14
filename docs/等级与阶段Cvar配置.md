# 等级与阶段 Cvar 配置

## 配置任意最高等级

建筑等级按配置即时计算。没有固定的 16、100 或其他玩法等级数组；100 级只需要设置最高等级和每级增长，不必复制 100 段配置。整数表示与引擎伤害安全范围仍有边界，配置解析会拒绝溢出。

| 建筑 | 文件与配置路径 |
| --- | --- |
| 基地 | `bases.cfg` → `Profiles/shelter_basic` |
| 普通/重炮塔 | `turrets.cfg` → `Types/machinegun_basic` / `machinegun_heavy` |
| 火焰喷射器、围栏、地雷 | 对应独立配置 → `Types/<类型>` |
| 增强器 | `amplifiers.cfg` → `Types/damage_amplifier` |

各类型保留现有 `Levels` 后，通过以下同级 `Growth` 扩展：

```text
"Growth"
{
    "max_level" "100"
    "anchor_level" "4"
    "Stats"
    {
        "max_hp_per_level" "1500"
    }
    "UpgradeCost"
    {
        "wood_per_level" "5"
        "iron_per_level" "5"
        "copper_per_level" "2"
        "silver_per_level" "1"
    }
}
```

此例用于默认基地：保留 1–4 级，4 级最大血量 5000。从第 5 级开始，`本级属性 = 锚点属性 + 每级增长 × (本级 - 锚点级)`，100 级最大血量为 149000。100 级升级费用从 4 级 `upgrade_cost` 起算，铁为 `100 + 5 × 96 = 580`。这是升到该级的一次费用，不是从 1 级累计到该级的总费用。

普通炮塔对应的锚点是 2，重炮塔与增强器的锚点是 3。修改各自已有 `Growth/max_level` 即可延伸交付配置中的曲线；不要把基地锚点 4 原样复制给只有 1–2 级显式数据的炮塔。

每个 `Levels/<级数>/<字段>` 都优先于公式。可以仅给 `Levels/50` 增加一项 `damage`，其他属性继续按锚点公式；51 级继续从原锚点算，并不从 50 级覆盖值累加。若某级显式提供 `upgrade_cost`，该级使用这一整组费用，未写的材料视为零；公式费用仅用于没有显式 `upgrade_cost` 的等级。

`Stats` 支持 `<字段>_per_level`，以及可选的 `<字段>_min` / `<字段>_max`。例如火力提高但射击间隔缩短可用 `fire_interval_per_level=-0.005`、`fire_interval_min=0.1`。初始生命值不得超过最大生命值；射程、间隔、伤害和整数计算均有引擎安全边界。升级材料增量必须为非负整数，费用溢出会拒绝加载，不会变成负数退款。

默认交付仍以原来的最高等级结束，例如基地 4、普通炮塔 2、增强器 3。扩展曲线预先提供但没有自动开启到 100，现有平衡保持不变。

## 团队伤害成长与增强器

`progression.cfg` 管理最高存活基地等级带来的团队伤害加成。它独立于基地本身的最大血量，也独立于炮塔自己的基础伤害。四通道分别为 `Player/Ordinary`、`Player/Boss`、`Defense/Ordinary`、`Defense/Boss`。

要开启至 100 级，将 `Rules/base_level_cap` 改为 `100`。各通道保留原 `BaseMultipliers` 的 1–4 级，现有 Growth 即开始生效：

```text
"Growth"
{
    "anchor_level" "4"
    "multiplier_per_level" "0.10"
    "multiplier_min" "0.0"
    "multiplier_max" "100.0"
}
"BaseMultipliers"
{
    "1" "0.0"
    "2" "0.25"
    "3" "0.75"
    "4" "2.0"
    "75" "9.25"
}
```

此 Boss 通道示例的 100 级基础倍率为 11.6，75 级单独覆盖为 9.25。还要检查同通道 `maximum_multiplier`：若它仍是默认 8，即使基础倍率算到 11.6，最终也只能是 8。`maximum_amplifier_bonus` 另行限制增强器加成之和，`maximum_final_damage` 再限制单次最终扣血。

每个通道的 `BaseMultipliers` 要连续写到锚点；锚点之后仅写需要覆盖的级数。没有 `Growth` 的旧配置仍可使用，但必须提供每一级。加载只扫描实际配置条目，运行只计算当前级别，不随 `base_level_cap` 的数字分配海量数组。

增强器使用同一套建筑 `Growth`，可增长 `minimum_base_level` 和以下四项：

- `player_ordinary_bonus`：玩家攻击普通丧尸的倍率增量。
- `player_boss_bonus`：玩家攻击 Boss 的倍率增量。
- `defense_ordinary_bonus`：设施攻击普通丧尸的倍率增量。
- `defense_boss_bonus`：设施攻击 Boss 的倍率增量。

例如增强器保留三阶数据并设置 `max_level=100`、`anchor_level=3`、`player_boss_bonus_per_level=0.15`，100 阶该项增量为 `1.50 + 0.15 × 97 = 16.05`，之后仍受通道 `maximum_amplifier_bonus` 和 `maximum_multiplier` 限制。默认基地门槛增长为 0，即三阶以后仍要求基地至少四级；若改成每阶增长 1，100 阶要求基地 101 级。应相应提高基地/伤害进度上限，或配置 `minimum_base_level_max`，避免门槛超过进度上限而被配置校验拒绝。

初始 1–4 级数值、加成叠加顺序和前置伤害 API 说明见 [伤害成长与增强器](伤害成长与增强器.md)。

## 验证状态

按天设置数量、复活时间、击杀奖励等主体选项，见[按天丧尸数量与主体Cvar](按天丧尸数量与主体Cvar.md)。其中说明开局完整池与阶段放行的区别，以及默认50/80/90/100的配置。

`tests/ProgressionProbe.inc` 中临时配置场景覆盖实际加载函数、100级四通道伤害、显式覆盖、增强器等级/费用/HP、越界和坏数值拒绝、大整数等级按需计算。测试结束恢复原配置句柄，不写服务器配置文件。这些场景已纳入 `tests/reports/v03/growth_verified` 的真实专服回归，整组324通过、0失败；该数包含其他玩法场景，不是单独的等级用例数。

真实客户端在 `growth_ui/zsh03_20261014_160033_*` 的8张截图中验证了基地和普通炮塔99→100的E菜单、材料报价与确认结果。基地HP147500→149000，费用木480/铁580/铜222/银101/金1；炮塔HP25000→25250，费用木309/铁515。背包扣除与报价一致，100级继续升级项禁用；增强器中英文百分号已恢复为`+10% / +25%`。截图中原生绿色购物车图标局部遮挡左侧操作文字，关键数字可读，不能称为完全无遮挡。逐图记录见 `tests/reports/v03/growth_ui/visual_review_160033.json`。

包含上述成长场景与新增资源耗尽重抽的最终联合回归已记录在 `tests/reports/v03/resource_verified`：367通过、0失败，运行时错误0、Updater错误块0。324项成长版原始报告仍保留，不把两轮重复测试相加。

真人安全与管理员场景另有76通过、0失败，资源真实刀采重抽另有25通过、0失败；默认35张、高等级8张、资源7张合计50张逐图复核，与服务端断言分别计数。资源图可见额度、到账和模型隐藏/恢复，但暗处颜色不单凭截图确认。正式生产SMX已在 `production_final` 通过8→50→8普通怪活动数与固定Boss检查，运行时及Updater错误均为0。最终部署哈希与完整证据索引见[0.3开发与验收记录](0.3开发与验收记录.md)。
