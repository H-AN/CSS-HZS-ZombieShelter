# 配套与可选组件

| 内容 | 何时需要 |
| --- | --- |
| [server-compat](server-compat/README.md) | 当前默认丧尸／Boss的额外资源，以及斧头、十刀的兼容资源与 HWS／刀具配置 |
| [npc-follow-range](npc-follow-range/README.md) | 需要解除原生 NPC 两项追击距离限制时选用；仅适用于已审计的平台与引擎 |
| [hzs-translations](hzs-translations/README.md) | HZS 主体缺少对应翻译时补充，已有完整翻译不覆盖 |

**目录名 optional 不表示当前默认阵容可以缺少模型。** 如果直接使用主插件文件中的21普通类型和Boss定义，应安装 server-compat 中对应 NPC 资源。HWS、刀具和额外丧尸技能插件按所选择的功能安装；这些插件本体不随本目录提供。

追击补丁不是主玩法的必需前置，安装前检查同类补丁冲突。BOT 修复与旧失败试验不作为安装项，各组件的适用范围和许可按子目录说明执行。