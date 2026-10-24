# NPC追击距离补丁（可选）

`HanZombieShelterNPCFollow.smx` 将 `server.dll` **运行内存**中的两条条件跳转各改为6字节NOP，解除直线距离超过2000、路径长度超过4000时放弃跟随的分支。磁盘DLL不变，Idle原逻辑保留，不安装Idle hook。

目标选择仍由HZS等现有逻辑负责，寻路和移动仍由引擎完成。补丁作用于进程内的原生人质跟随流程；解除距离条件不保证NAV可达、穿越障碍或所有路线都能追到玩家，也不会自行唤醒Boss。

## 依赖与平台

- CS:S Windows x64，当前已审计的 `server.dll` SHA256：`b42edb61cfa13aef6397eb0278fb38656f1eb71e7ac306f00343c671ab2a6cae`。
- SourceMod 1.12；本包使用1.12.0.7250的 `spcomp64` 构建。
- 随包 `hzsh_npc_follow.gamedata.txt`。不依赖上游套件的gamedata、SMX、资源或仓库，也没有HZS/ZSH API native硬依赖；未安装主玩法时仍可加载。

Win32、Linux尚未适配；服务器引擎更新后需要重新核验签名和原字节，不能仅凭平台名称判断兼容。

## 安装与迁移

1. 先确认没有其他插件正在写入同一补丁。`LocalHZSContinuousFollow.smx` 与本插件互斥：使用旧插件的 `sm_hzs_follow_enabled 0` 恢复原字节，以 `sm_hzs_follow_status` 核对恢复结果，再卸载旧插件。不要直接同时安装两个版本。
2. 原版 `hzs_zombieskill` 等可能也写入同一位置，应先停用其对应入口。部分旧插件卸载时不会恢复内存；这种情况需在正常维护时停用旧写入者并重启服务端，取得干净进程，再启用新版。仅凭插件文件名不能判断其补丁是否已关闭。
3. **只将本目录的 `addons` 合并复制到服务器 `cstrike`**。包含 `plugins/HanZombieShelterNPCFollow.smx`、`gamedata/hzsh_npc_follow.gamedata.txt` 和 `scripting/HanZombieShelterNPCFollow.sp`，无需复制上级目录或整套仓库。
4. 由SourceMod正常加载，或在服务端控制台执行 `sm plugins load HanZombieShelterNPCFollow.smx`。插件加载后在校验通过时自动启用；用 `sm_zsh_npc_follow_status` 确认两处读回及Idle原字节校验通过。

本地服务端仍只能使用项目约定的“昼夜求生本地测试服务器”入口；本子包不创建或迁移服务器。

## 命令与恢复

| 服务端控制台命令 | 用途 |
| --- | --- |
| `sm_zsh_npc_follow_status` | 查看启用、所有权及字节读回状态。 |
| `sm_zsh_npc_follow_enabled 0` | 恢复自己修改的两处距离分支。 |
| `sm_zsh_npc_follow_enabled 1` | 重新校验后启用两处距离补丁。 |

写入前检查两处原字节；已有NOP、未知改动或Idle入口变化均拒绝接管。写后读回，失败分别尝试恢复；关闭或卸载时只恢复自己拥有且仍符合预期的字节，不覆盖其他写入者的未知修改。地图开始时只读复核，不重新抢占补丁。

缺少gamedata、平台不符或预检失败时停止本可选插件加载，主玩法不依赖它才能加载。它仍涉及引擎内存写入，不能承诺任意运行异常都只影响单个插件。

## 从源码编译

在本子目录打开PowerShell，将第一行改为自己的SourceMod 1.12 `spcomp64.exe` 路径；仅使用该工具链自带的标准include，不需要原套件或主玩法的include：

```powershell
$compiler = 'C:\sourcemod\addons\sourcemod\scripting\spcomp64.exe'
$smInclude = Join-Path (Split-Path -Parent $compiler) 'include'
& $compiler '.\addons\sourcemod\scripting\HanZombieShelterNPCFollow.sp' ('-i' + $smInclude) '-o.\addons\sourcemod\plugins\HanZombieShelterNPCFollow.smx'
if ($LASTEXITCODE -ne 0) { throw '编译失败，请保留原插件并检查输出。' }
```

## 验证范围与来源

此前本地同算法的实测不能计作本目录新版本的验收。本次改名和独立gamedata版本的验证范围为编译、静态核对，尚未进行新版真人、完整路线或多人回归。

仓库旧 `hzsh_followrange_fix` 含失败的Idle detour试验，`hzs_botaddfix_guarded` 也不是已验证BOT修复，均不在本包安装范围。来源、Ducheese与H-AN署名及MIT许可见 [CREDITS](CREDITS.md)。
