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
