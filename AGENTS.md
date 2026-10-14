# 本项目必须记住的现场约束

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
