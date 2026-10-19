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

## 2026-10-19 v0.7.1 修复与增项：最新接手入口

- 用户已发布v0.7.0 Demo，v0.7.1工作规则与双模式电力已实施。先读进度目录 `08_v0.7.1_修复与增项.md` 和制作计划 `D:\gmod昼夜求生资源\文档\制作计划\v0.7.0\v0.7.1\README.md`，新报告在其reports，不覆盖旧证据。
- 只有当前active实体等于slot2刀槽且真实近战来源通过，才允许采集/施工/维修；未知刀可回退刀规则，枪械不能。CS:S真刀/HWS斧实际可能是DMG_BULLET|DMG_NEVERGIB，不可一概禁BULLET误杀刀；已用普通客户端命中确认。
- 默认优先供电；`power.cfg/Rules/overload_penalty=0/1`选默认规则，`sm_zsh_power_overload=-1/0/1`分别跟随配置/强制优先/强制超载全停，实时生效。交付恢复-1与配置0；建筑配额规则不改。HUD已分模式，过载停机仍保留完整需求。
- 榴弹炮塔命中随机单播explode3/4/5.wav，资源位于sound/hzsh/han。完整资源包LATEST已指向 `2026年10月19日资源_02`，773文件；旧770包及8个既有Vox缺件保留。勿再只部署旧包却遗漏新增声音。
- 当前仍唯一27169、同一preview_update/runtime/server/cstrike目录。交付身份与状态以 `v0.7.1/reports/final_session.json`核实，不沿用历史PID；19:31采样服务器12120、普通客户端12620，ready/kept_running=true、1真人/0BOT，临时夹具均已移除。保留服务与客户端，不发送quit/disconnect或代用户录制。
- 仅现场power/grenade配置新增5叶，其余33份被审计配置（含恢复后的NPC配置）字节保持；昼夜180/180、自然采集收益和低成本保留。两项失败补丁、BOT与运行换图崩溃的边界继续有效。

## 2026-10-19 v0.7.1 施工取消与菜单归类：最新工作入口

- 下一次AI续作v0.8.0先读 [10 AI接手说明](D:/gmod昼夜求生资源/文档/进度文档/v0.7.0_进度文档/10_v0.8.0_AI接手说明.md)。进度README及03/07已统一到最新v0.7.1基线，08/09保留两轮实际证据；v0.8.0具体功能尚未确定，以用户随后需求为准。PID/库存/局内状态只是已记时间的采样，续作时只读核实，不因历史记录而直接杀进程或重置现场。
- 先读 [09 施工取消与菜单归类](D:/gmod昼夜求生资源/文档/进度文档/v0.7.0_进度文档/09_v0.7.1_施工取消与菜单归类.md) 与 [新实施目录](D:/gmod昼夜求生资源/文档/制作计划/v0.7.0/v0.7.1/construction_menu_update/README.md)。旧08、Demo清单、ZIP及原测试证据保留。本轮无模型/材质/地图/音效增量，日期资源仍为773文件 `_02`，8个既有Vox缺件保持。
- 根菜单七类为自建庇护所基地、自律防御武器、恢复类设施、基地设施、照明设施、防御工事、篝火。发电机归基地设施，围栏/地雷归防御工事；原20配方不删不重，空组隐藏。变体/详情/确认返回路径及nonce/serial/epoch/revision、当前菜单句柄和所属组安全校验继续保留，旧Cancel/End不得清理新会话。
- 有效真人持当前slot2的CKnife刀/斧，瞄准交互距离内未完工施工建筑，右键新按下请求取消；同次按住占用右键，不能重击、连取消或同时分发E。完工建筑与固定地图基地不走取消入口；普通玩家不能取消他人有主施工物。
- 取消按实际invested全额退原支付者。原主在线但任一材料背包不足时整次拒绝、建筑和投入原样保留；原主离线/无有效归属走共享库存及既有溢出队列。管理员不能拿别人的退款，不使用完工拆除折扣或配方默认价伪造返款；消费身份后再移除/退款，防重复输入和重入。
- 仅显式取消未完工自建基地可释放动态墓碑槽，存在活跃建造/电网依赖则拒绝；回调结束前不复用。新建取得新UID/新基地身份，不移动其他活跃core；战斗摧毁的完工基地不当作已释放槽。API维持高水位索引范围，HUD/状态分母按实际存在基地统计；不提高各项配额。
- 新正式构建已编译并部署27169，仍为0.7.1，SHA256 `1038dc0e6d23978c18498088574dc2c45e0318af649b083baf51bd21fb3a2cbd`，35份现场配置字节保持。输入40/0、菜单101/0沿用已成功记录，取消夹具修正后55/0；首49/4错误证据保留。真人两次篝火取消均全额退木12/铁2，枪械右键保留蓝图，完工右键仍维修；根菜单与全部五分组、刀/枪HUD已实拍核对，动态基地槽/身份边界仅由私有回归覆盖，不扩大成真人成绩。
- 最终留服以 `D:\gmod昼夜求生资源\文档\制作计划\v0.7.0\v0.7.1\construction_menu_update\reports\final_session.json` 为准：实际采样server11880/client12620/controller4504，ready/kept_running=true，1真人/0BOT与5正式插件，34生产源/4份正式SMX一致，当前会话SourceMod错误0/动画报警0。临时观察与真人截图保护均已卸载删除，god0/takedamage2恢复；截图保护不代表正常战斗负载。不要沿用旧同版本或旧PID；继续保留服务器与客户端。
- 仍只保留唯一 `192.168.0.102:27169` 和普通Steam客户端，复用既有hijack/RCON/客户端jpeg方法；不读口令、不重启第二个留服、不代用户启停demo。两项失败补丁持续停用，恢复后的NPC配置及现场数值保留，不扩展运行换图崩溃修复。
