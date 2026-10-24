# 可选服务端插件

本目录提供独立安装的服务端伴生插件。主玩法不将它们作为加载前置；按需要选择一个子目录，只复制该子目录的 `addons` 到服务器 `cstrike`。

| 项目 | 状态与用途 |
| --- | --- |
| [npc-follow-range](npc-follow-range/README.md) | 可选：解除原生人质NPC跟随流程中的直线2000、路径4000距离放弃分支。保留Idle，只适配当前已审计的CS:S Windows x64构建。 |
| BOT添加/复活修复 | 暂缓，不在本目录分发。拆分插件或增加字节校验不等于已解决现有崩溃，也不解除项目的BOT限制。 |

追击插件随带自己的gamedata，不依赖原套件仓库、SMX、模型音效资源或HZS/ZSH API。未安装主玩法时也可加载，功能仅限引擎跟随规则。

同一补丁只能保留一个写入者。现行本地 `LocalHZSContinuousFollow.smx` 与本目录追击插件互斥，迁移前须恢复旧补丁并停用旧插件；详见子目录说明。仓库内 `hzsh_followrange_fix`、`hzs_botaddfix_guarded` 属于旧失败试验，不作为本目录安装项。

各子包保留自己的来源和许可。追击子包的MIT来源见 [CREDITS](npc-follow-range/CREDITS.md)，主仓库根目录GPL许可证保持不变。
