# 本项目必须记住的现场约束

## 已确认的本地测试入口：直接沿用，不再询问或重新搜索

- 本地服务端：`D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server`。
- 本地 CS:S 客户端：`D:\SteamLibrary\steamapps\common\Counter-Strike Source`，执行文件 `cstrike_win64.exe`。
- 客户端命令沿用 `cstrike_win64.exe -hijack -game cstrike +exec <本次测试cfg>`；截图沿用客户端 `jpeg`，菜单沿用实际客户端输入/menuselect。
- 真人客户端联调沿用 `tools/Start-LocalShelter.ps1` 的网络方式：服务端监听 `0.0.0.0`，客户端连接局域网地址（当前 `192.168.0.102`）；v0.5 隔离联调为 `192.168.0.102:27139`。RCON仍可走127.0.0.1。不要将离线 runner 的127.0.0.1游戏监听直接搬来替代已有真人联调方法。
- 服务端控制沿用 RCON。已有 `tools/local_rcon.py`、`tools/client_keyboard.py`、`tools/test_client_ui_v03.py` 是现成方法来源；先复用与改造，不再把已知路径/方法当作待确认信息。不得读取或打印既有 RCON 密码；独立 runner 内部生成的临时口令不输出。
- 旧版测试证据继续保留，按改动补新版回归；不能说旧测试全部作废。旧 runner 的自动 BOT/假客户端部分必须先移除，不能因此否定 hijack/RCON 方法。
- v0.5 单客户端入口：`tools/test_v05_real_client.py`，步骤文件 `tests/v05_real_client_steps.json`；服务端断言入口：`tools/test_local_server.py`。隔离服目录 `tests/runtime/v05_server`、端口 27139；27129 是录制服，保持零 BOT，不用于开发试错。
- 用户明确要求：测试完毕交付时保留服务器和普通 Steam 客户端，供用户本人复测，不要全部关闭。自动化夹具可先清理，再用 `tools/start_v05_manual_review.py` 启动正式 SMX 的隔离复测会话并保持运行；交付后除非用户要求停止，不发送 quit、disconnect、sm_zsh_fail 或结束这些进程。交付说明给出连接地址和实际运行状态。

先读 `docs/BOT与人质NPC引擎限制.md`，现成修复源码在
`D:\github\git\css\hzs-supporting-plugin\addons\sourcemod\scripting\hzs_botaddfix.sp`，
配套gamedata与README同仓库。已知问题无需重新加BOT复现。

- 用户录视频和调昼夜配置不需要BOT；27129录制服必须保持0机器人，使用真人客户端。
- HZS以人质实体作NPC。未确认对应引擎修复已生效时，NPC存在后禁止加BOT或复活BOT；不要下发bot_add/bot_add_ct/bot_add_t或自动补BOT设置，也不要用创建假客户端绕过保护。
- 主体源码已经有Cmd_BlockAddBot保护。空服休眠或未达到活动人数目标不能作为添加BOT的理由。不要把隔离回归的BOT流程搬到录制服。
- hzs_botaddfix通过Reset人质循环字节补丁跳过InitializeHostageInfo，规避硬编码数组越界；当前录制服没有安装它。文档任务不扩展成安装补丁、修改主体或测试崩溃。
- 索敌目标选择与引擎跟随距离是两层：Han_Think不设选目标距离上限，2000/4000跟随限制由配套hzs_zombieskill中的另一个补丁处理。没有加载该补丁时不能声称引擎全图跟随已修。
- 运行换图使用changelevel。用户明确暂不修复本地换图崩溃；初次进程启动+map与运行换图不同。
- 当前录制配置为白天20/夜晚60、普通100HP、低成本采集建造；保留用户现场数值，不用发行默认整套覆盖。

## 本轮复测后的最终辅助补丁边界

用户已明确辅助补丁失败后不再追查。guarded BOT 与 focused follow 两项试验补丁最终停用，不继续调试或重新安装；不能把初始 ready 写成远距离跟随已修好。最终留服只加载正式 HanZombieShelter 与未修改 HZS 两个插件，保持零 BOT。NPC 使用外部 B 计划补最近有效真人目标，未醒 Boss 不补目标、不因兜底而唤醒，隐藏池／暂停／停车逻辑继续保留。

本轮持续复用普通 Steam 客户端 PID `560`，未停止客户端；最终服务器正在切换正式构建，不沿用旧服务端 PID，交付时按新的留服 session 核对。`followup_probe_09_final` 尚在最终回归，不预判通过数；原测试与补丁失败证据继续保留。以上不改变 27129、现场数值和既有 BOT／换图限制。

## 本轮恢复、信标与人口扩展：此段优先于较早隔离人口说明

- 先读 `docs/v0.5_恢复信标与基地断电_实施记录.md`。本轮仅 27139 隔离配置使用普通原生池 250、外部普通上限 250、所有天白天 100/夜晚 250，`MAX_NPCS=512`；27129 录制服仍保持原白天 20/夜晚 60、普通基础 100HP、低成本和 M4A1 `+0.0`。发行原 `days.cfg` / `zombies.cfg` 不改成人口测试配置，旧时长保留。
- 新四配置已存在时按字节保留。只用受标记的 `tools/apply_support_building_config.py` 处理 27139，禁止整套发行默认覆盖现场。保留审计在 `tests/reports/v05/support_buildings/config_preservation.json`；其中录制服 22 哈希是旧审计证据对照，本轮未读取录制路径。
- 防御工事与照明当前步进为 ±10°。`support_buildings/probe_02` 已实际 307/0/0，另 `live_03` 独立 18/0/0，包含保护夹具下生产放行达到日 100/夜 250、原生 251/Boss 状态保留及真实 `DamageBuilding` 基地损毁/实体移除/全网断电。人口夹具的 god、禁伤、建筑临时 10000000HP 等保护不代表正常战斗负载；首轮 `probe_01` 故障证据保留。最终干净正式留服以 `tests/reports/v05/support_buildings/manual_handoff_final/session.json` 为准，已核对 ready=true、kept_running=true、仅两正式插件与零 BOT，不沿用旧进程或旧人口成绩扩大验收范围。
- guarded BOT 和 focused follow 两项辅助补丁继续永久停用，不再自动加载、安装或调试。所有测试保持零 BOT，不能绕过主体保护，不创建 FakeClient 或复活 NPC；nearest 兜底不代表引擎远距离跟随已修好。
- 保留现有普通 Steam 客户端（本轮持续复用 PID 560），继续固定路径 `-hijack` 与已有 RCON 方法；交付保留服务器和客户端。服务器 PID、构建哈希及 ready/kept_running 以最新最终 session 核实，不沿用旧进程号或未经核实的旧在线状态。

## 2026-10-19 v0.7 录制与动画恢复：此段为最新现场入口

- 先读 `D:\gmod昼夜求生资源\文档\进度文档\v0.7.0_进度文档\README.md`。用户现只保留一个测试服，地址为 `192.168.0.102:27169`；27129/27139 的历史配置和测试证据保留，不重新启动第二个留服。
- 当前运行目录为 `D:\gmod昼夜求生资源\文档\制作计划\v0.7.0\preview_update\runtime\server\cstrike`。实际进程、插件、库存及动画报警复核以 `D:\gmod昼夜求生资源\文档\制作计划\v0.7.0\recording_update\reports\final_session.json` 为准；不要沿用历史 PID。交付继续保留服务端和普通 Steam 客户端，不代用户开始或停止 demo。
- 用户要求从零自然采集录制，已移除百万补给 ReviewTools，换成不含供给逻辑的 Recording Access 权限助手。初始个人七项及六基地库存均为0；木/铁/铜/银/金基础有效采集10，斧头倍率2.0为20。当前昼夜180/180秒、第一天日8/夜50、普通100HP及既有低成本保留，不能用27129的20/60人口或发行默认整套覆盖。
- 当前普通丧尸模型 `models/player/putonghe/tank_zombi_host.mdl` 的五组动画和三组音效已从两份一致的用户原配置恢复；`idle1,idle2`、`attack1`、`getattack1`、`xxx.wav` 是旧测试副本的错误模板，禁止再次覆盖回来。只恢复了这八个字段，普通100HP、Boss和其他字段保留；HZS与Shelter源码/二进制未因本次报警改动。证据见进度文档06及 `npc_configuration_restored.json`。旧坏副本只用于历史审计，不能当部署模板。
- 完整静态资源日期包为 `D:\金源地图移植cs起源\csol地图移植\移植\服务器模型资源\2026年10月19日资源`，父目录 `LATEST.json` 指向当前版本。仓库只有源码、配置和增量音效/材质，没有完整地图/模型；后续发布同时核对日期资源包及其既有缺件清单。继续遵守零BOT、禁用两项失败补丁和不扩展本地换图修复的边界。
