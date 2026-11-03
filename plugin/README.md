# 插件安装文件

将本目录内的 `addons/` 合并到 CS:S 服务端 `cstrike/`。默认配置采用作者测试服设置，包含沙漠 DN 和死城 DN 的完整地图组合；已有服务器升级前先备份并比较。

| 目录 | 用途 |
| --- | --- |
| `addons/sourcemod/plugins` | 主玩法和独立昼夜环境组件 |
| `addons/sourcemod/configs/HanZombieShelter` | 当前玩法默认、两图基地／Boss／资源点、灯光校验 |
| `addons/sourcemod/configs/HanZombieScenario` | 当前默认 NPC 定义及对应 Stage 等 HZS 配套设置 |
| `addons/sourcemod/configs/HanZombieShelterEnvironment` | 昼夜环境设置 |
| `addons/sourcemod/translations` | 正式翻译 |
| `addons/sourcemod/scripting` | 主源码、模块与 API，服务器只运行时可不安装 |

还需要安装[主资源和地图](../resources/README.md)。直接使用当前默认丧尸阵容时，需安装 [server-compat](../optional/server-compat/README.md) 中对应的额外 NPC 资源；武器和外部技能功能按需安装相应插件与配置。

本目录不包含 HZS 主体或 HWS／刀具／丧尸技能插件本体。`HanZombieShelterEnvironment.smx` 提供昼夜视觉；可选追击补丁单独放在 `optional/`，不要与旧同类补丁叠加。

完整说明见[安装与升级](../docs/安装与升级.md)。