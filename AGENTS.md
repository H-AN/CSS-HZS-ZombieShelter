## 2026-10-25 文档补全：未来地图Boss五点的阅读入口

- 用户要求把本轮测试与增项、以后地图移植时寻找五个Boss点的详细流程写清。已新增[地图移植：Boss五点选址与验证指南](D:/github/git/css/CSS-HZS-ZombieShelter/docs/地图移植_Boss五点选址与验证指南.md)与[进度06总结及移植入口](D:/gmod昼夜求生资源/文档/进度文档/v0.8.2/06_本轮增项总结与Boss五点移植入口.md)，未来移植地图、准备录点或复核路线时先读。指南包含玩家操作、管理员准备、缺槽导致主玩法禁用的处理、原生数量匹配、配置候选、真实几何／追击验收、失败处理、清场和可复制任务模板。
- 本次仅写文档及只读核对已有文件，不发送游戏命令、不读取实时游戏状态、不启停／切图／重载、不改源码、SMX、配置、BSP或冻结ZIP／随包说明。下方20:25与20:34等是前轮历史采样，不因本节发布而成为新的运行保证，也不能按其PID／库存复位。后续用户明确安排新地图实施或测试时，按新的授权推进，不能把这次文档范围当作永久禁止测试。
- 采点助手可跨图，记录存活真人落地后的脚底origin和yaw；5／5仅为槽位齐，不等于体积／支撑／路径通过。五槽不自动生成五只Boss，原生Stage同key数量仍须精确匹配有效定义。不得把沙漠／死城坐标套给另一个BSP，用户选的平台高度先验证，不擅自压到NAV地面。
- 现有deadcity候选生成器与批处理写死地图、Boss、基地、诊断身份和报告位置，不是通用新地图工具；未来先适配并使用独立报告。控制器目前只允许沙漠DN和死城DN，新图还需核对允许列表、BSP、Shelter地图配置与原生Stage；只使用固定ROOT／27169，不另开旧环境。全部测量仍零BOT／FakeClient，失败尝试保留，临时阵容／助手须恢复清理。
- 本轮15／15等真实成绩、30起的Boss近战、固定120建筑伤害、休眠／全部Boss胜利和冷启动修复的证据均集中在06；离线、受控路测、实际冷启动、未测范围分开。原四份v0.8.2冻结说明与既有ZIP保留，新增指南不回写旧包。

## 2026-10-25 死城五点与Boss近战：20:25正式交接历史入口

- 先读[05死城／伤害交接](D:/gmod昼夜求生资源/文档/进度文档/v0.8.2/05_死城采点准备与Boss伤害调整.md)、[冻结新说明](D:/github/git/css/CSS-HZS-ZombieShelter/docs/v0.8.2_Boss近战成长与死城点位.md)及`tests/reports/v082_deadcity_routes_20261025/verification_summary.json`、`final_session.json`、`cold_start_runtime_verification.json`、`preservation_verification.json`（固定ROOT同名reports双存）。本节优先；下方19:22、19:46准备及更旧“当前／助手保留／未部署”等均是历史，不据此回退现场。
- 正式主SMX `a02f00dcbe0ae4c054d83bda3b018da65ca1671ba3a9035783be5e1b67e06ae0`，52输入0错0警告，build／repo／live一致；8e007过期产物留history。仅Combat/NPC两源变，其余50保持。HZS原生配置仅boss_pangzi／boss_angela／boss_yixing的damage三叶10→30（d990005a…），20:18:37正式mapstart已读；HZS主体源码／API／gamedata／其他插件／BSP不改。Boss原生普通近战用一次性凭据按普通天数倍率，建筑120／技能原值保持；298／0仅离线VM，没有新增真人／AI逐天伤害实测。
- 同一GetGameTime的定位回调不扣次数，原5游戏秒／50有效尝试保持。164／0包含129旧＋35新，不再相加；实际空服0真人超过30秒pending，真人连接同epoch2成功胖子50000／dormant1／attempts2，无指令创建／复活NPC，旧50耗光失败保留。
- 死城五点原字节8625285b40f574cc5ad0787fc3acb1e1a385b48169b1fe9a2fa6e4fc03d65013保存在`server-support/map-point-updates/zsh_deadcity_han_dn/BossSpawnPoints.cfg`；正式地图686fb488fd09c51b87b4d49034d4cd1593c2ceeb1fb0ab679dd28b0bb42f5c88。交付恢复原单胖子guard_01、Stage原字节e61da159…；diag_angela／diag_yixing仅临时路测，不随包部署；点表不能跨BSP套用。
- 死城3类型×5点15／15到至少一基地，20尝试含15成功／3停滞／2超时，固定run `f3c53c74472242a9bcafbcfd03acb81a_e2_t1792929507_l27244`。胖子全A；安哥拉P1到C其余A，P1此前A停滞46.574秒／B超时180.075；异形P2到D其余A，P2此前A停滞45.734／B超时180.075／C停滞48.975。停滞是连续30秒无16单位位移，不把场景总秒数写30或全写180。独立审计事件／连续轨迹／同基地及实际六基地3D安全盒通过、invalid raw pass0；测量server31944／779e诊断／epoch2，不是最终留服或新伤害验证。保护／控时钟／Exposure暂关等边界保留，不称自然多人战斗通过。
- 最终回执20:25:08、游戏样本20:22:46：server13004／controller23652／原普通客户端30344，session730a60a669634e2c925510f18b034f6b，唯一192.168.0.102:27169死城服、ready／kept_running／window_minimized，28插件1真人0BOT；自然epoch3／phase1、6基地1胖子50000睡眠，基地01HP990不复位。助手全卸载删除、停怪0／JPEG已看，玩家快照god0／damage2／move2、七材料0／仅六基地；之后自然收益与用户操作不按零样本再重置。新SM错误0／动画0，历史失败与precache／性能提示不删、不称全修。
- live265文件3改（主SMX、死城图、HZS三叶配置）／262保持，Stage原字节恢复；启动参数与10mock另有deadcity_setup审计。本次正式冷启动通过固定入口，不扩大为任意地图回归。用户此前自行changelevel成功；本轮单胖子1／1后实际换图与长期续局未重测，旧换图边界不宣称修复。不擅自启停当前服务端／客户端、回放回调、重生NPC或代启停demo；唯一ROOT／27169、零BOT／FakeClient、失败补丁停用、JPEG门禁继续有效。
- 新包`dist/CSS-HZS-ZombieShelter-v0.8.2-boss-damage-deadcity-update.zip`为58文件／534971字节，SHA256 `89382446f7c5c691ee463637614b8fe60f9b60ab8a74b948edd15eed4f016b94`；boss_damage/package_verification.json包装65／0独立，不加玩法成绩。旧三ZIP含2c9a保持。随包新doc冻结hash `cd637c09e48cc40955f09fb542b063a8177a8abf85035c30945e44bcafb617e1`，ZIP hash只补外部进度和入口，不回写doc。

## 2026-10-25 Boss路线与回合修订：19:22正式交接历史入口

- 先读[04实际路线与回合交接](D:/gmod昼夜求生资源/文档/进度文档/v0.8.2/04_Boss点位实追击与回合规则.md)、[随包联测说明](D:/github/git/css/CSS-HZS-ZombieShelter/docs/v0.8.2_Boss点位联测与回合规则.md)、[最终汇总](D:/github/git/css/CSS-HZS-ZombieShelter/tests/reports/v082_boss_routes_20261025/verification_summary.json)与仓库`tests/reports/v082_boss_routes_20261025/final_session.json`、`victory_runtime_verification.json`、`preservation_verification.json`（固定ROOT报告同目录双存）。用户已授权选五点、实追击及回合修复；原12:54“未部署”和13:07“采点助手须保留”只对应旧时点，不能据此回退当前正式构建或重装助手。
- 正式主SMX SHA256 `374c50f3aabef6eeac1bc7b9a5da69e76477bbfad0f48605c9aebbf2e9e00276`，52生产输入、0错0警告，build／repo／live一致；19:18:17已热重载。控制器`control/session.json`中的368诊断启动hash保留历史，以`formal_deployment.json`核对当前正式身份。正式替换后的自然新回合三条HP50000／dormant1／attempts2，19:19:43状态三只awake0／bound1／positioned_hp50000，不因旧启动hash或旧报警清空／重启当前局。
- 三种Boss五点实追击15／15组合到至少一基地，P1／P4到A、P2／P3／P5到B；保留6次走A约180秒超时和1次未射击等候失败，不能写成所有尝试或两基地全通过。原epoch2 run `12cae4de51ca47e196d96fd33492f19f_e2_t1792924053_l174` 固定保存，后续epoch／scene重号不混算。NPC在测量中由HZS／游戏AI自己走，fixture只在准备／清理边界搬NPC，玩家回基地用生产安全传送；有保护／停时钟等诊断条件，不称完整自然战斗或多人压力通过。
- 新引擎回合清苏醒／绑定／定位标志但保留伤害及已死进度；真人死亡后紧接epoch4和后续epoch6均确认三只重新休眠，安哥拉49999保留。Boss击杀胜利要求所有有效启用定义死亡，旧required_boss_ids忽略；默认第30天独立救援及关闭该救援的0配置保持。epoch9三次真人USP击杀native death，前两只不胜、第三只才胜；实际changelevel因既有本地崩溃被夹具拦截，只验调度，不宣称换图成功。VM244／0、288／0、129／0独立，288夹具仍一条203 unused BossSpawnHullClear warning，不混称所有编译零警告。
- 用户原五点只叶级加入live `zsh_desert_han_dn.cfg`，10新增叶／旧叶保持，地图SHA `15acf009f11c20c52df5171d240b465721e3eb5958646605b66ce1fe9a705205`；两个铁块点Z=-1504.318725不降地。repo无DN整图配置，发行带`map_point_updates/zsh_desert_han_dn/BossSpawnPoints.cfg`独立片段，源在`server-support/map-point-updates/zsh_desert_han_dn/`，不能把五点写进desertplant或整套覆盖现场。旧其他地图候选未获本轮路线验收。
- 最终采样2026-10-25T19:22:42.346359+08:00：server28800／controller30480／普通Steam客户端30344，`192.168.0.102:27169`，ready／kept_running／window_minimized，28插件、1真人0BOT。四助手CssBossPointCapture、CssV082BossPointCheck、CssV082UserSnapshot、hzsh_v082_close_panels均卸载删除；官方hzs_stopzombie恢复0，完整JPEG已审。玩家god0／damage2／move2，临时七材料0、仅两地图基地；用户中断后主动关服与关客户端再授权18:23重开，没有重放旧day6／7／库存／自建发电机快照。此后保留自然收益与用户操作，不按零样本再复位，也不quit／disconnect／结束进程或代启停demo。
- 正式加载后新增SM错误0／动画0；历史错误保留，音频与客户端delta报警未宣称修复。旧产品审计repo103中5变／98保持（4inc+SMX），live266中3变／263保持（主SMX+DN地图+旧采点助手删除）；外部HZS、公共API、gamedata、其他插件、BSP及旧两个ZIP保持。新包`dist/CSS-HZS-ZombieShelter-v0.8.2-boss-routes-update.zip`为67文件／562186字节，SHA256 `2c9a82a4399f1b4e16cb10ed098d74c53b57c3c17ea0b590c57ea64e6d6c75c0`，包装核验75／0独立；随包doc冻结hash `4db83eb01515bf078f40eab13dc148db9a4b02048e114578cccd2c083495a485`，ZIP身份不回写它。唯一ROOT／27169、零BOT／FakeClient、失败补丁停用、不扩修换图崩溃、JPEG门禁继续有效。

## 2026-10-25 临时Boss采点助手：13:07历史补充

- 先读[03临时Boss采点指令](D:/gmod昼夜求生资源/文档/进度文档/v0.8.2/03_临时Boss采点指令.md)和[就绪回执](D:/github/git/css/CSS-HZS-ZombieShelter/tests/reports/v082_boss_spawnpoints_20261025/point_capture_ready.json)。13:07:12.086991+08:00已加载独立临时 `tests/CssBossPointCapture.smx`（SHA256 `015986dfeb916396822980e857bb39770f1880c27b85f68f9dbf3c5f803ba062`，编译0错0警告），插件列表确认29个；当时地图zsh_desert_han_dn记录0/5，服务端client0写点拒绝。没有代玩家采点或发送客户端输入。
- 用户以存活真人ROOT管理员、关闭noclip并落地后，在聊天输入 `!bosspoint 1` 至 `!bosspoint 5`，或自己控制台 `sm_bosspoint 1` 至 `5`；`!bosspoint list`查看。存脚下origin和水平yaw，首次有效采点才生成固定ROOT `cstrike/addons/sourcemod/data/zsh_boss_points/<map>.cfg`；同编号更新，保留全量旧文件备份和失败pending。助手不创建/移动NPC，不修改地图配置；五点记录不代表大Boss空间/通路验收。
- **暂时保留采点助手供用户录点，不能按旧夹具清场卸载。** 录齐通知后直接读文件检查，完成采点后再卸载助手且保留记录备份。主插件仍live3920、未重载，新a318点池代码仍未启用；不自动替用户选点或改当前局。本阶段已执行助手加载/列表/拒绝检查命令，下方12:54“无游戏命令、265文件不变”只对当时核对成立，不能扩为本次所有阶段。当前局内阶段/天数/Boss/库存/错误数本轮未重新采样；冻结随包说明和两ZIP保持。

## 2026-10-25 v0.8.2 Boss坐标点补充：最新入口，未启用live

- 先读[02补充交接](D:/gmod昼夜求生资源/文档/进度文档/v0.8.2/02_Boss坐标点池与循环分配.md)、[Boss坐标点说明](D:/github/git/css/CSS-HZS-ZombieShelter/docs/v0.8.2_Boss坐标点配置.md)及仓库`tests/reports/v082_boss_spawnpoints_20261025/verification_summary.json`、`final_session.json`（固定ROOT的reports同目录有副本）。功能代码、离线验证与修订包已完成，但用户明确下一步才选新的五个位置；当前只迁移旧坐标及重复占位，不能称五点安全、点位已修复或新构建已运行。
- 新正式SMX SHA256为`a31813761234a5f87d1248fecb5394ae460c0a603a8c7ad41705eda83cd7629e`，52生产输入、0错误0警告，build／repo／新包一致；live主SMX仍为`3920d6fcc9a5f4a490255153dd91a044ce532f637c3c6b066c050945d2dcfab1`，本轮没有部署、热重载或切换当前局。新增BossSpawns.inc、BossBridge.inc；公共API、外部HZS与BSP不改。
- `BossSpawnPoints`必须boss1..boss5，origin为三个有限数且各在±15000，angles可选默认0 0 0、填写时各在±360。按有效启用Boss条目配置顺序循环；>5允许共享点，每个ID独立状态。新代码完全忽略旧marker／info_target且无fallback；旧叶与live标记插件仍保留。原生Stage同key数量必须与有效Boss定义数量精确相等，增加／禁用定义需同步Stage；位置循环不会创建或复活NPC，原生CSV及共同NPC预算继续约束数量。
- 生产函数VM最终274／0，包含7真候选及3份live Stage读入，8份生产源码快照匹配。VM夹具有一条203 unused BossSpawnHullClear warning，不能称所有夹具零警告；使用显式native替身，不代表真实碰撞、同点显示、寻路或运行生成已通过。迁移工具16／0，七候选7／7，各自范围分别记录，不与前轮合成台147／0、75／0或20／0相加。
- 2026-10-25 12:54:46.862422 +08:00最新只读样本仅确认原server5492／普通client13940进程保留及live3920文件身份；本轮无RCON／游戏操作，无重载／移动／新位置选择。当前局内阶段、天数、Boss、库存和错误数均未采样；旧11:41／11:43的phase1／phase0、Boss绑定及3／4条报警只属历史，不得沿用成当前状态，也不能为校准到旧快照回放回调、重生NPC、换图、重启或清库存。继续保留用户服务端与普通客户端。
- live审计265文件SHA全部不变；repo原101文件中7项批准变化、94项保持。repo仅原地图cfg加入五槽；其他地图候选在`tests/reports/v082_boss_spawnpoints_20261025/candidate_maps/local/`、`release/`及`repo/`，保持现场三Boss与发行单Boss各自旧定义。不要整份互相覆盖，不写固定ROOT／地图成品／旧交付；迁移工具拒绝原地覆盖及固定ROOT输出，已有点表保留／拒绝，叶级diff及来源hash均有报告。
- 修订包`dist/CSS-HZS-ZombieShelter-v0.8.2-boss-spawnpoints-update.zip`为65文件／552785字节，SHA256`b9760a928398d7f5d8530d22b84ca01253aba117e0ed6a8b0994517dfdc66b4b`。逐成员／字节／CRC核验通过，无另编N项总数；随包两脚本独立导入／help通过。旧crafting包及冻结说明保留，新Boss说明也已冻结，ZIP自身hash只写外部进度与入口，不回写随包doc。
- 下一步按用户安排选择五个新位置后再验证／应用，本轮不自动部署。唯一ROOT／27169、零BOT／FakeClient、失败补丁停用、不扩修运行换图崩溃、不代启停demo、普通客户端JPEG门禁和保护当前自然局面约束继续有效。下面合成台及更旧节保留历史，其中“当前／最新”仅指当时采样，不代表本次未采样的局内状态。

## 2026-10-25 v0.8.2 合成台与武器材料合成：前轮阅读入口

- 先读[v0.8.2进度README](D:/gmod昼夜求生资源/文档/进度文档/v0.8.2/README.md)、[01合成台交接](D:/gmod昼夜求生资源/文档/进度文档/v0.8.2/01_合成台与武器材料合成.md)和[配置说明](D:/github/git/css/CSS-HZS-ZombieShelter/docs/v0.8.2_合成台配置说明.md)。本轮用户已明确授权实施、部署与普通客户端联测，旧v0.8.1“仅文档、双方进程已退出”是上一轮历史，不能据此关闭当前进程或复建旧局。
- 新“合成台”用内置`models/props/de_piranesi/pi_sundial.mdl`，生产合法放置偏移`0 0 0.5`（引擎minZ=-.28125）；仅近距瞄准已完工台按E进入武器／材料合成与管理升级，不加基地／快捷菜单入口，不接B购买、不改mp_buytime。独立3cfg，Types组名精确匹配武器显式台名；省略绑定第一个可用类型，required_level缺省1。材料配方所有台型／等级通用但仍需正常供电，只扣个人背包，容量不足整次拒绝。
- 最终合成台为十级、HP500→1400、需求50＋每级30→320，一基地组限1，建造木50铁20铜5。基础斧头合成费仅木50铁20；十刀实际HWS命令接入，apexshuangren为sm_shuangren；仅十刀修建／维修HP乘十，采集倍率、HWS攻击／动画／音效不动，密客刀继续禁用。command只接受单个无参数已注册命令名，不把它当命令链。HWS负责实体与切换，不以slot2新实体宣称自动装备。
- 正式主SMX SHA256为`3920d6fcc9a5f4a490255153dd91a044ce532f637c3c6b066c050945d2dcfab1`，50生产输入、0警告，build／repo／live／包身份已核对。固定ROOT的`reports/v082_crafting_20261025/formal_hotload.json`记录同进程正式热重载；控制器session.json保留启动诊断hash历史，用热替换回执核对当前正式身份，不改历史控制记录。只回放过一次既有HZS轮的Han_OnGameStart使Shelter新会话day1，无重启／换图／玩家或NPC重生，不能称自然生命周期验收，也不能再次自动回放来复位。
- 最新只读样本是`reports/v082_crafting_20261025/retained_state_latest.json`（仓库tests/reports同目录有副本）：2026-10-25 11:43:30 +08:00，server5492／controller5868／普通client13940、userid3，唯一0.0.0.0:27169／192.168.0.102:27169，1真人0BOT、28插件，ready1／phase0等待、day1／epoch6／tracked0／boss bound0。11:41:16的final_session.json是phase1／3条Boss缺失报警的历史快照；自然续跑新增Boss03实际Hull不适配目标、保持停车，最新错误总4（3missing＋1hull）。不得写SM错误0、当前phase1或全玩法通过，不擅自复活NPC／换图／重启／修Boss点位来消除报警。PID与状态都只是采样，后续先只读核对名称、路径、监听和自然变化。
- 分项证据为离线生产函数VM147／0、刀配置静态86／0、最终功耗静态59／0；带电业务73加清场2为嵌套累计75／0（不可重复相加），包含诊断状态与真实引擎生产函数，非75项纯真人操作。十刀真实客户端短按施工／维修另20／0。旧零耗电UI材料／斧头及九次升级、25／1菜单过期失败、旧搜索／工具错误继续保留历史，不继承为最终功耗或多人验收。最终汇总verification_summary.json及final_session.json均已生成；最新状态以上述retained_state_latest为准。
- 清场恢复七材料原0、精确删除自建台和发电机、白名单25件测试工具清理恢复普通刀、god0／takedamage2／move2；两助手卸载删除，官方hzs_stopzombie0有回执，最终完整JPEG已实际审阅无遮挡。之后自然收益、库存和用户操作不得再按零快照重置。保留唯一服务端与普通客户端，不quit／disconnect／sm_zsh_fail，不代启停demo，不按旧5插件清单删除外部兼容插件。
- 增量包`dist/CSS-HZS-ZombieShelter-v0.8.2-crafting-update.zip`为59文件／539301字节，SHA256`a9c3f549e2ba72cf452be59e5399582e516b7a451a832174257f4ae0818247eb`，package_verification.json为66／0（不追加为玩法成绩）。随包配置说明冻结hash`02f3d4f53f72aa0d18390c54ff9e40fa5e790ec9e3197ecfdd6571f1fb0d6ac2`；ZIP自身hash不回写随包doc。原HZS／API／gamedata和其他外部插件边界保持，Local连续追击旧版保留，不切新可选包；零BOT／FakeClient、失败补丁停用、唯一ROOT和27169、JPEG门禁、换图／Boss点位暂缓继续有效。

## 2026-10-24 v0.8.1 环境与兼容交接：最新阅读入口

- 先读[12 v0.8.1测试环境与外部插件兼容交接](D:/gmod昼夜求生资源/文档/进度文档/v0.8.0/12_v0.8.1_测试环境与外部插件兼容交接.md)。v0.8.1本轮没有新增主玩法，主SMX仍为59218基线；刀/HWS、丧尸技能和资源属于本地兼容，仓库新独立追击包尚未部署，不能把旧Local版的采样当新包运行验收。Boss点位修复、BOT修复和全图路径验收均未完成或暂缓。
- 用户明确已主动退出服务器和普通客户端，目前只要求写文档。10:48只读核对无两个游戏进程及27169监听；不启动、不重启、不重连。09:39/10:09及下方各轮ready、PID和留服记录为历史，不能据此恢复旧局。当前证据在固定ROOT的 `reports/v081_handoff_20261024/read_only_state.json`；旧PID存活字段与实际名称/路径/监听核对不一致时，以实际核对为准。

## 2026-10-24 可选引擎补丁必须外置

- 用户明确要求：NPC 追击距离等依赖引擎签名的可选修复必须作为独立伴生插件，置于 `server-support/optional-plugins/`；不得并入 `HanZombieShelter` 主玩法或成为其启动必需前置。签名变化、gamedata 缺失或原指令不匹配时，伴生插件拒绝加载，不能因此使主玩法无法加载。
- 当前子包 `npc-follow-range` 提供 `HanZombieShelterNPCFollow.sp/.smx` 与独立 `hzsh_npc_follow.gamedata.txt`，只改 2000 直线距离和 4000 路径长度两处跳转，Idle 保持原样，不使用 DHooks；保留 Ducheese 原始逆向/签名来源、华仔 H-AN 适配维护署名及上游 MIT 许可。原套件仓库、SMX、模型资源均不是此子包的运行前置。
- 新可选包不得与本地旧 `LocalHZSContinuousFollow.smx` 或其他提供同项补丁的实现叠加。源码、说明与独立编译核验不等于新版已实际部署；本次仓库整理不改当前运行服，不重启、不切换补丁。既有本机两距离分支实测与本次包装验证分开记录，不宣称所有 NPC 路径已走通。
- BOT 修复继续暂缓，不分发可用 SMX、不恢复 BOT/FakeClient 测试、不解除主体保护；旧 `hzsh_followrange_fix` 和 `hzs_botaddfix_guarded` 为失败试验历史，不作新发行基线。保留旧证据，不因新增可选目录而重新启用。

## 2026-10-24 本地测试服务器迁移：唯一现行入口

- 用户明确要求统一迁移本地测试环境，并规定以后只允许在 `D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器` 进行本地服务端测试。先读[进度11](D:/gmod昼夜求生资源/文档/进度文档/v0.8.0/11_本地测试服务器迁移与固定入口.md)。本节优先于下方各轮的旧目录、旧启动脚本和旧端口说明。
- 新root下的 `cstrike` 是唯一现行游戏、插件、配置、地图与资源目录。服务端程序仍是父安装目录的 `srcds_win64.exe`，必须通过新root的 `启动测试服务器.cmd` 启动，并显式传入新 `cstrike` 的绝对 `-game` 路径。不能直接启动父安装目录默认 `cstrike`，不能再在制作计划或repo的历史runtime创建测试服。
- 固定入口为新root的 `启动测试服务器.cmd`、`服务器状态.cmd`、`服务器命令.cmd`；三个CMD调用root内 `tools/python` 的内置Python与 `tools/server_control.py`（`--start`／`--status`／`--command '<命令>'`／`--console`）。新报告为 `reports/migration_20261024/migration_manifest.json`、`verification_summary.json` 和 `control/session.json`。迁移与正式插件启动核验25通过／0失败，以这些新报告为准，不将旧JSON的ready或PID当成新状态。
- 2026-10-24T07:47:04+08:00采样唯一server20188／controller12892，`192.168.0.102:27169`／沙漠图，ready／kept_running／window_minimized=true；5正式插件、0真人0BOT，技能configured/effective2／override-1、当日SM错误0／动画报警0、官方停怪恢复0有回执。本轮启动前无普通客户端，也未启动客户端。5插件只是本次采样，今后用户添加兼容插件不能据此删回5个。仅文件迁移、引擎路径与正式插件启动验证，不称玩法／真人／多人回归。
- 原 `D:\gmod昼夜求生资源\文档\制作计划\v0.7.0\preview_update\runtime\server` 及原安装目录默认 `cstrike` 只作历史备份／对照，不再作为部署或启动目标。历史01—10、旧报告、旧构建和失败证据保留。
- 迁移沿用最新正式主SMX `59218e6e94f1df6bf12b1eb26075fee894a6d4aa05ed91b710a194bf44aa1e3f` 及既有现场配置；不整套覆盖成发行默认，不更改HZS／API／gamedata。唯一27169、零BOT／FakeClient、两项失败补丁停用、不扩修运行换图崩溃、客户端最小化／关原生页／JPEG门禁和留服约束继续有效。

## 2026-10-23 扩大器原生动画及781资源：最新功能基线

- 先读[进度10](D:/gmod昼夜求生资源/文档/进度文档/v0.8.0/10_扩大器原生动画模型与资源包.md)与`native_expander_update/reports/final_session.json`、`verification_summary.json`。正式主SMX `59218e6e94f1df6bf12b1eb26075fee894a6d4aa05ed91b710a194bf44aa1e3f`，48来源0错0警告，四份一致；128文件包SHA `3b1f7de8f09d956dbe4148bc3d7506443b30f091640336db7670d81d75975ebd`。
- 用户明确反对OnGameFrame外观旋转，现Types/Config/World/Lifecycle四源仅删除旧回调、旋转队列和rotation_speed_degrees_per_second，无timer/RequestFrame替代。两扩大器新路径models/hzsh/han/build/range_expander.mdl，idle原生601帧/30fps/20秒，root底座固定、上部rotor转动；实体angles固定。旧.8实际可见尺寸与脚点烘焙，runtime model_scale1.0、offset0，删除两档旧ground/curve补偿，Downloads各6件；只有repo/live两份expanders.cfg变化。通用model_scale仍缺省1、.01..16与其他脚点规则保持；原dynamic非1 PHY限制未扩修。
- 静态源码73/0、真实场景外层16/0和最终原生续测外层20/0分开。场景2次与续测4次显式nativecheck各12/0，不再与外层叠加；客户端照片另审，cycle或离散图不保证所有渲染帧连续。保护/供给/白天延长/显式完工或重建属于诊断，不称多人负载。空文件/非KV审计工具中止已修，不当产品失败。旧09的4Hz/逐帧、碰撞、悬空、粗瞄报告均保留历史。
- 新完整静态包`2026年10月23日资源`共781件=旧773+原生模型6+原图标2，LATEST已按资源审计更新；旧_02与原8Vox缺件保持，六迫击炮仍随增量。新模型复用内置orrery纹理，无新VMT/VTF，用户提供原六件保持。HZS/API/gamedata/其他4SMX及其他玩法数值不动。
- 用户要求的可复现制作说明在[旋转建筑模型制作与贴地移植指南](D:/github/git/css/CSS-HZS-ZombieShelter/docs/旋转建筑模型制作与贴地移植指南.md)、[模型源README](D:/gmod昼夜求生资源/文档/制作计划/v0.8.0/native_expander_update/model_source/README.md)及[随源同版指南](D:/gmod昼夜求生资源/文档/制作计划/v0.8.0/native_expander_update/model_source/制作与贴地指南.md)。包含本轮实际建模／脚点／静态PHY／StudioMDL／验证命令，不只是交接概述；输入格式限制及本例特殊系数不可泛化。文档补项未改变model_build源清单、模型或报告。
- 最新采样2026-10-23T16:28:27.379852+08:00 server20928/controller23500/原client15452，唯一27169沙漠服，ready/kept_running=true，5正式插件/1真人0BOT/god0/takedamage2、助手已清、停怪恢复0、窗口最小化、SM错误/动画报警0。继续留服与普通客户端，只读核对并保留自然变化，不按旧PID/快照复位，不quit/disconnect或启停demo。BOT/失败补丁/换图边界保持。

## 2026-10-23 设施模型、可配置尺寸与信标透视：前轮09历史入口

- 先读[D:/进度09](D:/gmod昼夜求生资源/文档/进度文档/v0.8.0/09_设施模型颜色尺寸与信标恢复.md)及`model_polish_update/reports/final_session.json`。最终主SMX `2deff5bab4ac079a4f00cf35310dbba1e54f2d8544d43d7a6a415cb5d6b11d7a`，48源0错0警告，四份一致；122文件包SHA `1a6ee4a565af5a860fbac2bfbe36169f6062ee4e34af2f295a4f1fc781c1b479`。
- 两扩大器model_scale0.8，rotation_speed_degrees_per_second18，每服务器帧按实际间隔更新，仅遍历登记旋转实体队列；旧05轮0.25秒／4Hz旋转保留历史。增伤器保持0.3，旋转缺省0。通用尺寸缺省1，范围.01..16；Placement/model_ground_z与model_ground_curve_z默认0，localZ=offset.z-(ground*s+curve*s*(s-1))，普通curve0仍线性。扩大器ground=-63.9872694/curve=-44.1278944，.8计算抬高44.1293524，不硬码模型路径。原offset/功能范围/附件/塔坐标为独立配置，固定地图基地默认1。
- 医疗低白高绿、护甲低白高蓝、substation维修低白高橙黄；Radio idle原生旋转。旧wh_frame两live缺件恢复并随包，正常供电门禁不变；用户已确认地图墙透视，接受基地墙遮挡，原Marker保留，Construction normal／sort实验不交付。最终06缩放29/0、配置/缓存/碰撞63/0、信标12/0、逐帧旋转19/0分别记录；先前23/1、0.3/0.5悬空与夹具失败保留，不冒充多人压力或透视像素全通过。
- 最终48生产输入、批准改变7源（含Lifecycle）／6cfg，Construction/Beacon保持。原生dynamic非1实际PHY有限制，信标/Tesla仍1旧路径；本轮不加代理实体或动画序列替代，单骨可见形状与PHY尺度不完全一致，不把贴地校准扩大成全部视觉碰撞一致。
- 最新采样2026-10-23T15:56:07.709177+08:00 server16516/controller20328/原client15452，唯一27169沙漠服，ready/kept_running=true，5正式插件/1真人0BOT/god0/takedamage2，助手补给保护建筑清理，官方停怪恢复0、窗口最小化、SM错误/动画报警0。继续保留服务端与客户端，先只读核对，不按快照复位，不quit/disconnect或代启停demo。其余HZS/API/gamedata/BOT/失败补丁/换图边界保持。

# 本项目必须记住的现场约束

## 2026-10-23 v0.8.0 黑夜免疫成功提示音：最新入口

- 用户追加免疫成功时仅本人播放CS:S内置player/bhit_helmet-1.wav，并在看到当前129投入/2500管理调整后明确批准“现在结束当前局并应用新版”。已按该授权新开唯一正式会话，原普通Steam客户端15452保留。先读进度v0.8.0 README与08、制作immunity_sound_update/reports/final_session.json及verification_summary.json；旧07及各轮包/失败保留历史。
- 生产仅Audio.inc与Lifecycle.inc变：LoadInteractionSounds每图PrecacheSound内置sample，不加下载和cfg；ExposureTick现有免疫成功分支单次PlaySkillsImmunitySound再continue，helper ready/LiveHuman/非Fake守卫后EmitSoundToClient本人、AUTO/NONE/音量1。概率及纯判定不动，失败/安全基地/有效篝火/未到期等原门禁无声，声音不可用不取消免疫。
- 新正式SMX SHAce278a5c5153c69d8730069ab4b3c7ee1c9b86f528a83ba67c4edc5606fe5b74，48输入0错0警告，build/repo/live/delivery一致；100文件ZIP SHAcae5c24b12fcc08fbc9c6321f7bf47e77a7fc569f16ac389893b314c0ed8053c。所有旧cfg字节不变，HZS/API/gamedata/其他4SMX/773资源/LATEST/六模型保持。
- 私有exact函数32/0包括两个虚拟玩家；真实引擎8/0使用提取的ExposureTick/helper、虚拟位置/安全/概率/HP及真实SDK单播，SoundHook确证sample一次/recipient1/目标userid2。不是生产默认40%自然统计、真实多人或人耳听觉确认。早期离线探针编译失败/弃用警告保留；最后探针0错0警告，全部助手卸载删除。正式preflight JPEG已view_image确认原生页面无遮挡。
- 12:36正式采样server13256/controller4612/原client15452，唯一192.168.0.102:27169/zsh_desert_han_dn，passed/ready/kept_running=true，5正式插件1真人0BOT、god0/takedamage2、当前SM错误0/动画0、停怪恢复0、窗口最小化。默认每天2/常规20/重力9及快捷E继续在；本轮新局未加材料/技能/保护/建筑，当前自然状态不得按旧PID或快照再清空。零BOT/FakeClient、失败补丁停用、HZS不改、不扩换图修复、每次客户端最小化关页面JPEG门禁继续有效。

## 2026-10-23 v0.8.0 范围外E与新默认正式启用：最新入口

- 用户明确要求“不用保留当前，服务器应用新数据”，已授权结束旧局；旧默认轮“不要重启／保留旧余额”是该授权前的历史。先读进度v0.8.0的README和07，以及制作`quick_menu_update/reports/final_session.json`、`verification_summary.json`。正式新进程已实际配置2／有效2／override-1，常规20级、重力9级；全部数值仍由skills.cfg配置，本轮没有修改任何旧cfg。
- 非基地和非建造范围E打开两项快捷菜单：建造菜单（复用!build）和角色技能菜单（ShowSkills -1）。瞄准施工物／设施／塔／信标、基地与红圈原E优先，按住E不重复弹；nonce／serial／epoch／配置和技能会话revision／当前Menu句柄／实时区域复核，旧Cancel/End不能清新页面。仅Commands.inc和主sp新增生产逻辑。
- 新正式主SMX SHA0040f7eb5d923df54d4d3159d1f1406be3246493c447d265bd52200a66c14bed，48生产输入、0错0警告，诊断原始来源与正式／四份SMX一致。私有87/0、真人菜单和实际篝火施工28/0、真实逐级新默认33/0分别记录。受控day15预算30，speed20约1.8、gravity9到.1、HP一级+5，封顶拒买不扣点；不是自然15天、AI战斗或多人平衡成绩。旧移速格式显示79.9%属浮点展示精度，实际属性1.799999近似1.8，不伪称图像显示80.0%。
- 前轮13项后截图label中止、二轮24/1错误瞄准木节点、预览等待期间Exposure死亡等证据保留；god/takedamage和官方停怪不屏蔽夜间Exposure。后续修正可见空地方向并快速操作，全28/0；不因此改生产伤害或用补丁掩盖测试保护边界。16张完整JPEG已view_image审阅，正式四图原生页面无遮挡。
- 12:13正式采样唯一192.168.0.102:27169／zsh_desert_han_dn，server16660/controller20580/原普通client15452；passed/ready/kept_running=true、5正式插件1真人0BOT、god0/takedamage2/move2、SM错误0/动画0、助手SMX已卸载删除，官方hzs_stopzombie恢复0，服务器最小化。正式fresh清除诊断技能、材料、保护和测试建筑，当时13技能0／earned2／spent0／balance2／adjustment0，个人七项与两基地0；之后先只读核对并保留自然收益和用户操作，不按旧PID／状态再复位。继续留服务器与普通客户端。
- 947基线仅批准两源和repo/live主SMX变化，所有旧配置字节保持；HZS15源／3API／其他4SMX／gamedata／773资源／LATEST／六模型保持。100文件新ZIP quick_menu_update/v0.8.0_outdoor_quick_menu_update.zip SHA9102ea7cb81cd1f9f3f6999a86000f3f759689bfe1ca7b5450984a88230f379a，46项交付核验通过。旧包／报告保留；零BOT/FakeClient、失败补丁永久停用、不修运行换图崩溃、每次最小化关原生页JPEG门禁、非战斗停怪结束恢复0等边界持续有效。

## 2026-10-23 v0.8.0 默认平衡配置与重力实测：最新入口

- 用户已确认新发布默认：daily2、12常规技能max20/cost1，gravitymax9/amount.1，其余gather.05、repair/construct5、speed.04、immunity.02/cap.40、medic/logistics2、artisan10、selfheal/selfarmor2、maxhp/maxarmor5；原恢复范围256／周期60/30保持。仅skills.cfg可编辑默认，不硬编码到源码；用户特别要求测试后同步进度。先读进度v0.8.0的06及制作`balance_defaults_update/reports/verification_summary.json`、`final_session.json`，旧04/05和各轮旧包保留。
- 用户一度认为sv_gravity800对应个人.8，后明确授权现场对照后接受实测：同一真人raw0/SetEntityGravity1.0/.8各3跳，1.0与原值相同、.8更高更久，世界800全程未改。报告在`gravity_probe_20261023/reports/measurement_01.json`。个人1.0每级减.1需9级到.1，真实外部.8基准已有算法自动只允许7级。不要再把800直接等同SetEntityGravity.8，不硬设个人起点.8。
- 新配置95/0/0私有回归实际通过（真实现场文件Import+生产解析器/纯公式/虚拟未绑定账号），覆盖60点满3项20级、21拒绝不扣、重力9、服主自定义level/cost/amount/daily/interval/radius。不是新数值20级真人战斗/多人平衡通过；旧真人功能证据及本轮9跳分开记录。
- 本轮947基线仅repo/live两份skills.cfg各改27叶，其他文件/48生产来源/正式SMX保持c27fe538be0b54d930437f371c81fc605d8dd63ef64dbe1b41e31d6f29fd2d78。新100文件ZIP `balance_defaults_update/v0.8.0_default_balance_update.zip` SHA8d05b520126cf6e50773aa5895a2fdc4215c4f1918adc5bac36b4320b15bd0f6，含新版cfg与说明；原HZS/API/gamedata/其他4SMX/773资源/LATEST/六模型边界保持。
- **当前局保留旧已载入技能配置，新默认下个地图会话/正常新进程生效。** 没重启/换图/重载/调天或重置用户库存账号。11:23只读采样原server12712/controller18288/client15452、唯一27169沙漠、5正式插件1真人0BOT、god0/takedamage2、用户木549铁41铜117银41/其余0、技能余额102/调整100/投入0保持；技能configured1/effective1/-1是当前图锁定的正常旧状态，别误判部署失败再清空局面。继续留服与普通客户端，后续先只读核对实际状态。

## 2026-10-23 v0.8.0 篝火燃料与技能点增项：最新入口

- 本轮三项追加需求已实施测试、正式部署及留服。先读进度v0.8.0的README、04、05及制作目录`fuel_points_update/reports/final_session.json`、`verification_summary.json`。下方c0ea/47来源/默认5级是首版历史，旧报告保留。
- 最新正式主SMX SHA256 `c27fe538be0b54d930437f371c81fc605d8dd63ef64dbe1b41e31d6f29fd2d78`，48生产来源，0错0警告，诊断/正式来源及build/repo/live/newdelivery一致。100文件增量ZIP SHA `8a2610aa3b8c2f15a7e383a1d464c5f3a2833cd1854932d0ca3e3c13e2b82f8c`，父旧交付不覆盖。
- 正式09:10采样及09:16只读复核server12712/controller18288/原普通client15452，唯一27169沙漠图、5正式插件、1真人0BOT、running1、god0/takedamage2/move2；助手SMX全移除，官方停怪恢复0，窗口最小化，正式SM错误0/动画报警0。继续留服与客户端，不按采样PID/库存重置自然状态。
- 篝火HP200/300/400和范围160/192/224保留；燃料独立60/90/120，每秒扣1。刀/斧左键维修，右键新按下消耗个人木材添柴，默认每1木+10；59补60仍付完整1木，满不收费，缺木提示，0失效。新完工满燃料、升级保留实际剩余不免费填满；按类型/等级/Growth配置fuel_capacity/fuel_per_wood/fuel_wood_cost，未新增模型或配方。未完工右键仍取消，枪械不添柴。
- 13项配置max10，实际gravity0.1/免疫10%限制有效可买等级，默认免疫5，其余通常10，原每级贡献保持。`sm_zsh_skill_points_per_day -1/0/n`跟配置/暂停未来/覆盖未来新最高日，先按旧rate结清当前日，历史不重算，旧日不重发。最高日历史额度+个人管理调整-实际投入；同图Steam身份保留，不跨换图/重载/重启。
- 管理员`sm_zsh_admin`新增技能点管理，在线玩家列表、±1/10/100及自定义输入；用户明确超扣最多清空未用点数，已学技能保留，洗点不撤销管理员调整。校验账号/serial/revision/map/config/nonce及当前菜单句柄；死在线目标可选，实际只测一个真人自己账号，勿扩称多人目标已测。
- 最新私有燃料70/0（旧67/0保留）、账本73/0/0、管理员76/0；真人每日首9项通过后翻译SMC单行解析失败已改多行，02脚本错误旧额度预期保留，续轮23/0。真人火堆15/0包含实际施工、59→60收费、满/缺木、左修右补/按住/枪械/自然扣至0重燃、E2/3升级。保护/受控fuel/HP/deadline与自然等待分开，不称正常AI或多人压力。最终正式客户端菜单JPEG已看。
- 原946基线仅19已有文件批准变化，campfires/skills仅新燃料叶和13max按叶合并，其余旧现场配置字节保持；这两cfg仓库/live叶值相同，注释可不同。HZS15源/3API/其他4SMX/gamedata/773资源/LATEST/六模型保持。零BOT、失败补丁停用、本地换图边界与每次最小化/关原生页/JPEG门禁、非战斗停怪/结束恢复0继续有效。

## 2026-10-23 v0.8.0 玩家技能与生存目标：最新入口

- 用户已明确授权按 `D:/gmod昼夜求生资源/文档/制作计划/v0.8.0` 制作并测试；原计划“待审／未实施”是历史状态。先读 `D:/gmod昼夜求生资源/文档/进度文档/v0.8.0/README.md`、`03_最终交付与留服.md` 和制作目录的 `reports/final_session.json`、`verification_summary.json`。本轮启动前确认服务器／客户端均不存在，询问后用户明确授权按旧入口启动。
- 正式 v0.8.0 主 SMX 为 `c0ea1c18318aeea7bc574b9baf5c273ca00ae5cd8f61a480d9ba35e9d1f22147`，47生产输入、0错0警告，build/repo/live/delivery一致。唯一27169沙漠服、普通Steam客户端继续保留；08:04正式采样server7292/client15452，后续只读核实，不按历史PID重置。最终5正式插件、1真人0BOT、god0/takedamage2、助手SMX全移除，官方hzs_stopzombie恢复0、窗口最小化。
- 13项技能在同一Shelter插件，入口基地E／sm_zsh_skills。skills.cfg按图固定；最高生存天数累计预算，真实Steam身份同图内存保留，不跨换图／插件重载／进程重启。现场Auth64原生转换失败，已验证Steam2与账户号一致后转规范64，不使用serial或未经验证身份替代。默认个人重力raw0是Source未设置哨兵：零级保留raw0，有技能以有效1计算，卸载还原raw0。
- 默认每项5级／每级1点，目标30天；Campaign/rollback_days=0，survival_day_limit=30。第30夜自然到期救援或全部必需Boss完成胜利，目标0关闭按天救援且累计额度无硬上限。普通怪仅对人直接近战增加按天倍率，Boss和建筑公式不叠加。保留浮点交引擎，实测第30天20原伤变26.999998092，实际HP损失26；不偷加取整／epsilon来伪造27HP结果。
- 私有技能66/0/0、菜单73/0、生命周期伤害134/0分别记录。真人13项逐次购买、52/102生命、2/102护甲、102双上限、移速1.01／重力0.98、自恢复与50/20/5洗点有证据；首轮26/1和续轮7/0分开。实际篝火1HP施工至完工，木节点五次采集共101；工匠为显式诊断脉冲，不冒充60秒自然周期。战斗是现有真实NPC＋真人直接SDK伤害Hook，不是AI自主攻击；救援缩短为诊断3秒并截获换图调度，不扩修已知本地换图崩溃。
- 本轮仅一名真人；真实多人双向光环／持续负载、完整死亡重连出生补量未另实测，不能以私有模拟成绩冒充。旧失败与异常截图保留。最终JPEG首次副本取早截断，已从同一Steam原文件完整复制并查看 `_stable.jpg`；截图助手现强制JPEG结束标记和稳定大小后复制，依据final_visual_capture.json。
- 旧配置仅精确修改rollback_days、增加目标与12个伤害叶，另增skills.cfg；HZS15源、3API、其余4SMX、gamedata、773资源／LATEST与六迫击炮模型保持。增量包93文件SHA `c6e29ceb24b4c2da6f9f54480a29ef1e5762be8892621a79b4d2638cc7e6cabc`，仍依赖v0.7.2增量与完整 `_02`773文件和原8Vox缺件说明。
- 原BOT／FakeClient／失败补丁／运行换图边界不变；每次客户端前最小化、关闭原生页面、看JPEG；非战斗先官方停怪，战斗／留服恢复；实际建造先给足并读回授权测试材料。现场不清楚立即问，不能盲找或盲做。

## 真人测试必须先关闭遮挡页面（用户重复强调，2026-10-20）

- 每次服务端启动后立即最小化它的窗口，再操作普通客户端；不得让服务端控制台盖住客户端。使用 `tools/minimize_owned_server.py --pid <当前唯一27169服务端PID>`，只处理该服务端的窗口，确认minimized_or_hidden=true。每次客户端操作的前置步骤也复核；最小化不等于停服，不结束服务端或客户端。

- 每次真人客户端测试、重新连接或重新出生后，先自动关闭欢迎/MOTD、选队和角色选择页面，再开始菜单、建造、战斗或传送测试；不要把发送了关闭命令等同于页面确实消失。
- 本轮自动入口为 `D:\gmod昼夜求生资源\文档\制作计划\v0.7.2\scripts\client_input.py`，每次操作先调用 `close_native_panels.py`，经独立临时测试插件 `tools/fixtures/CssTestPanels.sp` 对既有真人发送 info/team/class_ct/class_ter 的隐藏消息，随后客户端执行 gameui_hide。不修改HZS主体。
- 测试开始前必须用客户端jpeg截图确认中央画面没有上述遮挡；仍有遮挡立即修复并重拍，不能继续计作有效真人UI/画面证据。门禁同时写入测试脚本，不能只写文档或靠记忆按回车。
- 原生页面关闭助手属于临时测试夹具，正式交付前卸载删除SMX；服务端和普通Steam客户端继续保留。
- 用户已明确授权功能联测时给测试角色足量材料（本轮七项各100万），实际建造/升级测试须先补给并读取库存确认，不能一直停留在“材料不足”的菜单。使用现有sm_zsh_grant/管理员入口，不改变发行经济配置；临时补给、保护和测试建筑在正式留服前清理并核实。
- 用户2026-10-20再次明确：菜单、模型、放置施工、照明等不需要丧尸的测试先通过RCON执行现有HZS管理员命令 `hzs_stopzombie 1`；战斗测试前和最终正常留服时执行 `hzs_stopzombie 0` 并记录恢复。不要反复让丧尸打毁测试场景再盲拍。该入口不需改HZS主体、补丁或人口配置；它是命令，不是可查询原值的ConVar。暂停不替代实际API/电网/截图断言，也不关闭夜间Exposure；若外部逻辑仍造成影响，记录实际结果，不能擅自宣称所有分支已停。

## v0.7.2计划变更边界（2026-10-20用户明确要求）

- 偏离已审阅计划的变更，特别是更换模型、调整功能行为、分组或数值方案，先说明原因、拟改内容与影响并征求用户意见，未获同意不实施该变更。不能把实施授权当成任意修改计划的授权。
- 按原计划实现和定位测试问题继续推进；发现原模型或设计可能需要替换时停在对应项目，不擅自换成其他资源。实际测试夹具/落点校准与计划变更分清记录，HZS主体不变约束始终有效。

## 已确认的本地测试入口：直接沿用，不再询问或重新搜索

- 唯一本地服务端root：`D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器`；游戏目录为其下 `cstrike`。启动用root的 `启动测试服务器.cmd`，程序指向父安装目录 `srcds_win64.exe`，`-game` 必须明确指向新游戏目录。旧隔离目录及旧版runner仅供查阅，后续如需复用先改为该root，不在别处启动测试服。
- 本地 CS:S 客户端：`D:\SteamLibrary\steamapps\common\Counter-Strike Source`，执行文件 `cstrike_win64.exe`。
- 客户端命令沿用 `cstrike_win64.exe -hijack -game cstrike +exec <本次测试cfg>`；截图沿用客户端 `jpeg`，菜单沿用实际客户端输入/menuselect。
- 真人客户端联调保持服务端监听 `0.0.0.0`，唯一地址为 `192.168.0.102:27169`；RCON仍可走127.0.0.1。仓库 `tools/Start-LocalShelter.ps1` 已委托现行root的同一控制器，`tools/local_rcon.py` 亦走该控制器。其他历史脚本不能按旧runtime或旧端口直接启动。不要将离线 runner 的127.0.0.1游戏监听直接搬来替代已有真人联调方法。
- 服务端控制沿用 RCON。已有 `tools/local_rcon.py`、`tools/client_keyboard.py`、`tools/test_client_ui_v03.py` 是现成方法来源；先复用与改造，不再把已知路径/方法当作待确认信息。不得读取或打印既有 RCON 密码；独立 runner 内部生成的临时口令不输出。
- 旧版测试证据继续保留，按改动补新版回归；不能说旧测试全部作废。旧 runner 的自动 BOT/假客户端部分必须先移除，不能因此否定 hijack/RCON 方法。
- v0.5 单客户端入口 `tools/test_v05_real_client.py`、步骤 `tests/v05_real_client_steps.json` 及服务端断言 `tools/test_local_server.py` 保留为历史方法来源。旧 `tests/runtime/v05_server`／27139和27129不得重新启动；任何后续本地服务端测试只使用现行root／27169，保持零BOT。
- 用户明确要求：测试完毕交付时保留服务器和普通 Steam 客户端，供用户本人复测，不要全部关闭。自动化夹具先清理，再通过现行root的 `启动测试服务器.cmd` 留服；交付后除非用户要求停止，不发送 quit、disconnect、sm_zsh_fail 或结束这些进程。交付说明给出连接地址和实际运行状态。

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

## 2026-10-20 v0.7.2 新道具与手动照明：最新接手入口

- 先读 `D:\gmod昼夜求生资源\文档\进度文档\v0.7.2\README.md` 及 `08_最终验收与留服.md`。本轮已实现并分项测试，等待用户真人复测；不能把旧v0.8.0待定或制作计划初始待审状态当成本轮未获授权。偏离计划的模型/玩法/数值变更仍须先征求用户意见。
- HZS主体源码、SMX、gamedata及原API完全不改；15份HZS源、3份API、其他4份正式SMX逐SHA保持。35份旧配置2918原叶不变（30份整文件字节保持、5份只增新项），保留恢复后的动画/声音、180/180、首日8/50、普通100HP、自然采集及低成本；原20配方保留，新增19项总39。
- 新增电塔、水平首碰单爆能量球炮、墙/尖刺、三种地雷变体、三档发电机、两款灯、中高自建基地、木/金属瞭望塔。既有两机枪/火焰/同模医疗不重复新增；原版CS:S/HL2光效，不加外部粒子。塔E上下与升级，基地候选仍是坐标而非候选实体。
- 灯具用户已明确授权独立恒亮、白天关灯需求0/夜间按原需求供电；又明确要求E手动关闭/恢复自动。手动关闭跨昼夜保持且始终0耗电，开启恢复白天关/夜间供电，不强制白天点亮。当前只审计deadcity地图的style13/14/15，其他地图灯安全关闭并报错，需另审计；不擅自写环境0或HZS1。
- 私有菜单102/0、战斗53/0、灯光及手动148/0（含旧73不相加）；真实塔96/0、战斗最新10/0，均按各报告夹具边界记录，非多人正常压力成绩。真实三灯夜间200/18/18/0、日0，手动10检查点0/18/13/0/13/0/0/18/18/0已通过并查看JPEG。旧失败及af95实际OFF照片继续保留，不以文件名on/powered误判亮灯。
- 正式主SMX `099aa8aa4f9ce2c9e86161a52d4ef8ae7a5aaa7425643a1e9e7afa4427960ad9`，39编译来源和build/仓库/live/delivery四份相符。增量ZIP78文件，仍依赖 `_02`773文件静态包；LATEST和8个既有Vox缺件说明不改。
- 唯一地址仍 `192.168.0.102:27169`，同一preview_update/runtime/server/cstrike。最终身份以 `D:\gmod昼夜求生资源\文档\制作计划\v0.7.2\reports\final_session.json` 为准：21:04采样server25068/controller29720/普通client29896，ready/kept_running=true，5正式插件、1真人/0BOT、god0/takedamage2、个人七项及六基地库存0、当前会话SM错误0/动画报警0；测试保护/补给/建筑/助手已清理，已恢复hzs_stopzombie0，窗口最小化。进程继续保留，不发quit/disconnect或代用户启停demo，续作先只读核对，不能沿用历史PID杀进程。
- 非战斗测试必须先hzs_stopzombie1，结束/正式留服恢复0，并记录命令回执；此为命令不是ConVar，不能伪造状态查询。继续每次关闭MOTD/队伍/角色页、用JPEG确认、启动后及客户端操作前最小化服务端、实际建造先给七项百万材料。临时相机锁定m_yaw/m_pitch已读回0.022/0.022000恢复。零BOT、失败补丁永久停用和不扩展运行换图崩溃修复继续有效。


## 2026-10-20 v0.7.2 真人反馈与沙漠图：最新接手入口

- 先读进度 `D:/gmod昼夜求生资源/文档/进度文档/v0.7.2/README.md`、09、10。08及21:04死城final_session保留历史；当前身份见 `D:/gmod昼夜求生资源/文档/制作计划/v0.7.2/review_fixes/reports/final_session.json`。23:29采样server40108/controller39120/client29896，唯一27169、zsh_desert_han_dn、ready/kept_running=true。继续留服务与普通客户端，不依历史PID重置、不启停demo。
- 用户明确所有地图统一使用插件生成的自定义移植实体和同一玩法，不接地图原生基地/建筑控制。地图差异只作为地形/布点数据。HZS主体源码/SMX/gamedata/原API不改；零BOT、不启用失败补丁、不修本地运行换图崩溃继续有效。
- 已实施离子炮固定残留场（半径96，每0.5秒10/15/20伤害、3/4/5秒，进入才伤、离开停止，无吸附减速）；初级v7/body1、高级原第一项Source基地，移除中级空架子，地图/模型碰撞不拦且拒绝任何NPC/Boss占位；木桩铁丝网及模型触及基地外沿归属；身上原版燃烧；塔次帧菜单刷新；独立迫击炮160/4.5秒高抛白拖尾、薄白圈、原榴弹声，原榴弹80/2.5秒红拖尾。偏离已审方案的模型/玩法/数值仍先征求用户意见。
- 火焰仅管理精确owned EntRef/标签/目标ref/epoch，原DoT不变，不接管原生flags/effect。CS:S无模型entityflame缓存DONTSEND，需明确清DONTSEND置ALWAYS，不能只凭attached属性声称可见。最新07橙火/残留场、红白拖尾和薄白圈实图已看；旧失败及粗圈图保留。
- 照明延续白天关闭需求0、夜间供电、手动关闭跨昼夜保持。旧地图代码白名单已取消；lightstyle_audits缓存绑定BSP/相关lump CRC，死城和沙漠同代码+占用检查，不写环境0/HZS1/33。新地图用通用audit_bsp_lightstyles.py生成资源证据。
- 私有菜单131/0、场68/0、放置36/0、塔97/0、燃烧67/0、迫击炮56/0及BSP12/0分别记录；沙漠战斗07实际31/0。双塔、贴墙蓝图/全额取消、未醒Boss拒绝、外沿铁路障及三灯手动开关有实际证据；真人<1秒冷却未量到，不冒充通过或多人压力成绩。
- 正式SMX `34cea73dad5981c8162be7336b09288bda32007b65423663cc4303e1d6a5d6d7`，0错0警告、43源、build/仓库/live/delivery四份一致；39配方/原20ID保留。62原配置中59字节保持，仅用户批准shelters/plasma/grenade三外部cfg变化，2灯审计缓存为新增。15HZS源/3API/其余4SMX/773资源/LATEST保持。ZIP84文件SHA1224e427ff98f7f07d92c01001b55b80da86036966394f5b6853b95e55e7c183，依赖完整_02包及原8Vox缺件说明。
- 最终5正式插件、1真人/0BOT、god0/takedamage2、当前会话SM错误0/动画报警0；助手/保护/测试建筑已清，hzs_stopzombie恢复0、窗口最小化。正式启动时七项及两基地实际0，恢复自然运行后23:29木1/其余0/HP50；保留自然收益，不重置来制造零库存成绩。相机m_yaw/m_pitch已实际读回0.022。
- 非战斗测试先hzs_stopzombie1并记回执，战斗/最终留服恢复0（命令非ConVar）；每次关闭MOTD/team/class并最小化服务器，真实建造前给足7项百万测试材料并读回，结束清夹具。minimize会FreeConsole，后续Python子进程stdin=DEVNULL避免WinError6。旧捕获失败报告继续保留。


## 2026-10-21 v0.7.2 独立迫击炮、塔支撑与菜单ref：最新接手入口

- 先读 [11最新交接](D:/gmod昼夜求生资源/文档/进度文档/v0.7.2/11_独立迫击炮模型与塔交互及离子轨迹.md)、[aim_update](D:/gmod昼夜求生资源/文档/制作计划/v0.7.2/aim_update/README.md) 与 [最终会话](D:/gmod昼夜求生资源/文档/制作计划/v0.7.2/aim_update/reports/final_session.json)。本轮已完成正式部署、分项实测及留服；旧09/10、review_fixes的34cea、08初版及失败证据保留历史，不再把最初计划待审／旧候选状态当当前未完成。
- 最终2026-10-21 01:00:23 +08:00采样：唯一 `192.168.0.102:27169`／`zsh_desert_han_dn`，server40956/controller40332/原普通Steam client29896，passed/ready/kept_running=true；5正式插件、1真人0BOT、god0/takedamage2/move2、HP100、个人七项及两基地当时0、summons0；SourceMod错误0/动画报警0，服务端已最小化。PID、库存、HP随自然运行变化，后续先只读核对，禁止据旧PID重置或清自然收益；继续保留服务器与客户端，不quit/disconnect、不代用户启停demo。
- 正式SMX `5ff3c6b3afd2ca1134cf84c0e06e4844f1d9767a85ce0e1209ba69d11187d3f9`，0错0警告、44生产来源，build/仓库/live/delivery四份一致。六独立模型repo/live/client/delivery SHA一致；strict final_identity_required/final_passed/passed=true。ZIP91文件SHA `9ae417a3eafc9b41aba19492c5772c20190a10be6b3399b1135810064c8554e5`（旧84+支撑模块1+模型6）；153及未部署、已撤销的8e中间版不是交付身份。
- 独立迫击炮 `models/hzsh/han/turret/mortar_lob.mdl` 只转炮管上仰60°，底座／UV／原材质保留，无新纹理；原榴弹炮继续旧水平模型。mortar_basic只改model、muzzle=`31.109646 -0.020000 113.814102`及六模型Downloads，Placement=`0 0 1.990009`不变；伤害／射速／费用／分组保持。六侧件随增量包，不能漏发或替换原炮模型。
- 塔地面E以实际body中心半径256圆及Z[-64,96]为界，准确瞄准指定塔，不绑bottom_anchor；实际body frame用于范围／平台／候选。九点后补有界网格，每点仍完整玩家Hull及精确本塔支撑。有空位可上；全占中文“塔顶已经被其他建筑物占据，无法上塔。”且不传送。原权限、冷却、nonce/serial/epoch/revision、UID/ref/generation与旧菜单清理保护保持。
- 新BuildingSupports按实际放置射线捕获平台／屋顶父UID/ref/gen，施工物／完工物及堆叠链随父毁按有界队列损毁；地面邻居不按高度误删。CleanWorld先Suspend保UID清旧ref，存活父缺模型/edict只延迟恢复，新body重绑ref/gen；死父UID不接管同slot新建筑。当前批准源差异为9份旧源与新增该模块，EntityBudget未改。
- 已确证一个设施持续Expired根因：近距准确瞄准UID7/gen6的合法ref=-2104229686，旧ParseLevelInteger的±20亿配置限制误拒。Commands仅EntRef字段使用ParseDefenseEntityRefToken完整signed32；其他9字段及等级／nonce／revision原限制不放宽，身份／权限／瞄准／一次消费仍保留。
- 用户最新明确已看到能量球粒子的小拖尾，不再补额外蓝色轨迹段；保留现有球／小尾，不重新加BeamPoints。普通蓝图拍到impact、慢动作图轨迹不明显，不能伪称飞行像素通过；用户确认与图片证据分开记录。host_timescale实际读回1.0，原首碰／伤害／残留场保持。
- [verification_summary](D:/gmod昼夜求生资源/文档/制作计划/v0.7.2/aim_update/reports/verification_summary.json)确认诊断44生产输入与最终manifest全部SHA相同，四张实图已看。四组私有45/0几何、97/0会话、56/0ref、61/0支撑共259项，和真实塔19/0、弹道9/0、不同实体三次升级3/3分开。额外3条Prune回调中断断言仅编译，不纳入61/0；不扩大成普通多人压力成绩。
- 真实tower_fast02：UID3塔和平台UID4/6/7原ref全部移除、地面UID5保留，全占中文／无悬空实图已看，cleanuptrue。真实plasma_upgrade_real01是UID8/9/10连续逐次E升级1→2并逐次恢复原NPC，不是三座同时留场。测试临时保护、源需求0、即时完工、百万材料不代表正式负载；正式fresh启动已清除测试现场及补给。
- 64现场配置中62整文件字节保持，只watchtowers与mortar_basic精确14叶批准变更；HZS15源／3API／其他4SMX／原773资源／LATEST保持。完整静态包仍是 `2026年10月19日资源_02`，原8Vox缺件说明不改，增量六模型不写回原日期包。灯仍同一BSP审计缓存+占用机制，无地图名称代码白名单；所有地图统一自定义实体玩法，地图仅地形／布点，最终仍沙漠图。
- 两次新capture失败与tower_fast01失败原样保留。零库存bootstrap可处于running0启动等待，最终当前必须running1；轮间phase0只读等待自然恢复，不能推进phase或清库存制造通过。最终bootstrap页面助手已卸载，只有5正式插件。原普通客户端29896从未停止。
- 继续非战斗先官方hzs_stopzombie1、战斗／留服恢复0并记回执（命令非可读ConVar）；关闭MOTD/选队/角色页并JPEG确认、最小化服务器、真人功能建造按授权先给足材料并结束清理。minimize会FreeConsole，后续Python子进程stdin=DEVNULL。零BOT/FakeClient、不复活NPC、不修改HZS源码/SMX/gamedata/原API、不启用失败补丁、不另启第二留服、不扩展运行换图崩溃修复继续有效。
- 用户未指定下一项新功能。本轮功能与留服已经完成，只按后续任务补新证据；偏离既定模型／行为／分组／数值必须先征求用户意见，不能以fixture为名放宽产品安全规则。
