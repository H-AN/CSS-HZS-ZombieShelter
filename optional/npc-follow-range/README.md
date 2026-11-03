# NPC 追击距离补丁（可选）

解除原生人质 NPC 因直线距离超过 2000、路径长度超过 4000 而放弃跟随的两项限制。目标选择仍由 HZS 等插件负责，寻路和移动由游戏引擎完成；补丁不会自行唤醒 Boss，也不能让 NPC 穿越障碍或到达没有可用路径的位置。

## 适用环境

- Counter-Strike: Source Windows x64、SourceMod 1.12 或更高版本。
- 配套 `hzsh_npc_follow.gamedata.txt`。
- 已核对的 `server.dll` SHA256：`b42edb61cfa13aef6397eb0278fb38656f1eb71e7ac306f00343c671ab2a6cae`。

Win32、Linux 尚未适配。游戏更新后应重新核对签名和原始指令；不能只凭平台名称判断兼容。该组件不是 Shelter 主玩法的加载前置，也不依赖上游套件的其他插件、模型或声音。

## 安装

1. 检查是否已安装同类补丁。`LocalHZSContinuousFollow.smx` 以及提供相同功能的 HZS 技能套件不能与本插件同时修改同一位置。
2. 更换旧补丁时，先按旧插件说明恢复其修改，再停用旧实现。某些旧补丁卸载时不会恢复内存，需要在维护时机停用它并重启服务端。
3. 将本目录的 `addons/` 合并到服务端 `cstrike/`，包含插件、gamedata 和源码；运行时只需 SMX 与 gamedata。
4. 正常加载插件，或在服务端控制台执行 `sm plugins load HanZombieShelterNPCFollow.smx`。使用下面的状态命令确认校验结果。

缺少 gamedata、签名不匹配、原始指令不符或其他插件已修改目标位置时，本组件拒绝启用。不要通过关闭校验来强行加载。

## 命令

| 服务端控制台命令 | 用途 |
| --- | --- |
| `sm_zsh_npc_follow_status` | 查看启用状态、两处距离分支和原始指令校验结果 |
| `sm_zsh_npc_follow_enabled 0` | 恢复本插件拥有的两处修改 |
| `sm_zsh_npc_follow_enabled 1` | 重新校验后启用 |

关闭或卸载时仅恢复本插件仍拥有的修改，不覆盖其他插件后来写入的未知内容。本组件保留原生 Idle 行为，不提供 BOT 添加／复活修复，也不保证所有地图路线可达。

## 源码与许可

源码位于 `addons/sourcemod/scripting/HanZombieShelterNPCFollow.sp`，放入自己的 SourceMod 1.12 或更高版本编译环境，使用该版本的编译器及标准 include 编译即可。来源、作者和许可见 [CREDITS](CREDITS.md) 与 [MIT 许可](LICENSE.upstream-MIT.txt)。
