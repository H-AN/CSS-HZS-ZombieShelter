"""Build the dated v0.4 filesystem handoff from the current local evidence."""
from pathlib import Path
import datetime, hashlib, json, shutil

REPO=Path(__file__).resolve().parents[1]
OUT=Path(r'D:\gmod昼夜求生资源\文档\2026年10月15日_v0.4_进度文档')
SERVER=Path(r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')

def write(relative,text):
    path=OUT/relative;path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(text.strip()+'\n',encoding='utf-8-sig' if path.suffix=='.ps1' else 'utf-8')

def main():
    profile=json.loads((OUT/'03_录制演示配置/配置应用记录.json').read_text(encoding='utf-8'))
    write('README.md',r'''
# 2026年10月15日 v0.4 进度文档

这是当前接手入口，替代同级的0.3进度文件夹作为最新上下文。外部插件版本0.4.0，公开API v1。用户当前要录开发进度和玩法演示，27129测试服已切换为低成本录制配置；仓库默认配置和0.4发行包保持原平衡。请先分清这两套数值。

接手必须先读[BOT与人质NPC引擎限制](02_开发接手/BOT与人质NPC引擎限制.md)：已经直接阅读hzs_botaddfix源码和gamedata，记录Reset循环补丁机制，录制服始终0机器人。原仓库新增AGENTS.md，防止后续AI把机器人回归搬到录制服。该文档也更正此前只检查Han_Think就宣称无限索敌的结论：引擎跟随距离限制是另外一层，目前没有安装对应hzs_zombieskill补丁。

建议依次读：

1. [项目进度与玩法定稿](01_项目进度/项目进度与玩法定稿.md)：做了什么、确定规则、未完成项。
2. [新AI接手指南](02_开发接手/新AI接手指南.md)与[路径配置索引](02_开发接手/关键路径与配置索引.md)：源码入口、启动时序、构建与测试边界。
3. [当前录制配置与操作](03_录制演示配置/录制数值与演示操作.md)：服务器实际生效数值、恢复方法、丧尸异常血量原因。
4. [API接入说明](04_公开API/昼夜求生公开API.md)：独立天空模块及后续模块接口。
5. [验收与现场说明](05_验收证据/验收与现场说明.md)、JSON证据；最后读[下一步与已知问题](06_下一步/下一步与已知问题.md)。

可直接把[新窗口接手提示词](06_下一步/新窗口接手提示词.txt)交给下一个AI。本目录附参考源码/默认配置/实际演示配置及SHA256清单，可独立核对；开发仍以外部仓库为准，参考快照不是自动安装程序。没有保存RCON密码。

普通丧尸几万血已查明：原HZS示例武器配置中M4A1伤害规则为-1000.0，负伤害被扣血代码当成加血。现场读到73503和30152血的普通丧尸；已改为+0.0并冷启动清除旧实体，重启后读到普通池100只全部100血。30000只是旧阶段的放行血量上限，不能阻止负伤害继续加血。

换图仍使用changelevel；用户在其他插件也复现换图崩溃，怀疑本地服务器环境问题，明确暂不修复。此次配置生效采用安全关服后冷启动，没有再尝试换图崩溃复现。
''')
    write('01_项目进度/项目进度与玩法定稿.md',r'''
# v0.4 项目进度与玩法定稿

日期2026-10-15。项目为CS:S + SourceMod独立外部昼夜求生插件，依赖用户现有HZS大灾变主体。不是GMod Lua工程；资源目录沿用用户的GMod命名。外部版本0.4.0，主体运行版本2.3.2，公开API v1。

## 本轮已完成

| 分类 | 当前实现 | 进度/边界 |
| --- | --- | --- |
| 基地电网 | A/B/C分别供电；安全区内归本基地，区外归最近基地；实例保存归属 | 已实现，多台发电机产能相加 |
| 发电机 | 可建造、升级、受伤降产、刀修恢复、损毁清除、退天重建 | 默认3级，满血产能100/200/350，HP1000/1600/2400 |
| 供电分配 | 实际产能=满血产能×当前HP/最大HP；完整负载才开机 | 优先级小者先供，同级先建先供；不够一台可继续尝试较小负载 |
| 缺电炮塔 | 机枪、火焰、榴弹塔缺电停止新攻击，修好发电机自动恢复 | 已实现；既有燃烧/已发榴弹继续结算 |
| 灯具 | 可建造升级，模型+light_dynamic实际照明，缺电熄灭、复电点亮 | 2级，半径500/700、亮度2/3；客户端画面待真人复核 |
| 榴弹炮塔 | 自动锁定可见活动丧尸/已醒Boss，发实体榴弹并爆炸 | 已实现；不接入旧飞机控制逻辑，不主动唤醒Boss |
| 通用API | 10个只读native、5个全局forward，支持独立模块晚加载和依赖恢复 | 已实现；不是解析聊天广播；天空模块尚未制作 |
| 配置/翻译 | 4份新cfg及既有设施耗电字段；中英172词条、7词条源 | 合计22cfg、9建造配方 |
| 构建/部署 | 正式/测试构建分离，独立27139回归，正式27129部署 | 最终联合409/0，正式日夜/API检查通过 |
| 录制数值 | 低成本采集建造升级、较少活动丧尸、较低BossHP | 只改现场配置；另存可恢复的演示配置包 |
| 异常普通HP | M4A1 -1000造成负伤害加血 | 已纠正服务器配置并重启，非测试探针遗留 |

增强器已加入耗电字段、电网统计和停机状态显示，但本次交接代码复核发现：Progression.inc/AmplifierBonus没有过滤BuildingPowered，缺电增强器可能仍累计伤害加成。此项应列为明确待修，不能声称“缺电增强器停止加成”已通过验证。已有回归检查了供电分配，没有覆盖这个加成断电结果。详细见06。

## 用户已明确的规则与限制

三基地各自供电，电量表示持续容量，当前没有燃料、电池和能量累积。设施升级增加耗电；发电机升级增加产能。围栏、地雷不耗电。发电机每刀修50HP，普通建筑每刀10HP；当前维修不扣材料。升级保留原有缺血量，不能免费修满。

演示无需高等级或海量材料。默认等级上限保持：基地4、普通机枪2、重机枪/火焰/增强器/发电机/榴弹塔3、灯2、围栏/地雷1。Growth机制可以扩展，默认不要开100级。历史0.3曾验证100级只是扩展能力，不是现在正在用100级。

没有授权本轮制作动态天空、飞机AI或重新打包地图。旧建造脚本仅作灯具思路参考，旧飞机脚本仅作实体位置发射思路参考。用户暂不修复changelevel崩溃；不能据此改主体或改用map换图。

## 延续的0.3玩法

当前地图zsh_desertplant_cso_v1_d：三基地、32资源点、一个固定Boss；原生普通NPC池100，外部只控制放行/隐藏，不自造NPC和重写追敌。Boss休眠，真正受伤才唤醒，天亮不回收，胜利条件为击败驻守Boss。

默认发行日夜各180秒，第1天8/50，第2—4天10/80，第5—9天12/90，第10天起12/100；现场演示已改为300/120秒、所有天20/60，详见03。夜间在基地安全区外每秒扣1HP仍保留。

五材料木/铁/铜/银/金，各玩家个人背包独立，A/B/C仓库分别共享。采集资源耗尽后隐藏、成长、成熟重抽材料；同实体换模型、尺寸和碰撞。离线背包转共享仓库，重连空背包，设施变无主；不要恢复“离线个人背包”旧方案。

失败回退一天，保留材料、资源成长、存活设施；升级基地按50%恢复，普通基地恢复基础状态；被战斗摧毁和已爆地雷不复活。无磁盘战役存档，换图/重启是新战役。录制配置切换后的旧局被清除，当前从第1天重新开始。

## 还没有完成的验收

0.4未有真人新菜单/灯光/榴弹画面的逐图复核，不复用0.3的50张截图作0.4验收。多人长期负载、灯具客户端可见性、榴弹对移动目标命中率、最终经济平衡仍待实玩。模型暂用现有引擎零件/泛光灯/内嵌炮塔，尚无专用美术。录制配置是便于展示的临时平衡，不能拿来宣称正式难度已定稿。
''')
    write('02_开发接手/新AI接手指南.md',r'''
# 新AI接手指南

先读README及01/03/06，再看最新用户需求。不要从0.1/0.2历史方案重做项目。优先核对实际文件哈希、插件版本及配置；最后现场状态不是静态保证，用户可能已经玩过或改过配置。

本轮新增明确必读：[BOT与人质NPC引擎限制](BOT与人质NPC引擎限制.md)。录制服不加BOT，空服休眠不通过添加机器人解决，当前没有安装对应修复。现成修复源码和gamedata已存参考快照，不必再重复摸索或复现这个已知BUG。原仓库AGENTS.md也记录了这些约束。

## 工作区与入口

外部仓库：D:\github\git\css\CSS-HZS-ZombieShelter。主入口addons/sourcemod/scripting/HanZombieShelter.sp；模块路径addons/sourcemod/scripting/HanZombieShelter/；公开头文件addons/sourcemod/scripting/include/HanZombieShelterAPI.inc。

| 模块 | 职责 |
| --- | --- |
| Types | 版本、结构、固定容量、建筑/电网/投射物状态 |
| Levels、Progression | 通用等级公式、4伤害通道、基地等级/增强器；注意06所记增强器供电缺漏 |
| Config、DayCvars | 22cfg读取校验、阶段Cvar、原生完整池保护与退出还原 |
| Geometry | 安全区、射线/体积、出生点、占位和模型世界边界 |
| Inventory、Management | 个人/共享背包、离线转仓、归属、UID、拆除、投入与退款 |
| EntityBudget、Audio | 全服实体预算、关键余量、声音 |
| Power | 基地归属、产能/负载分配、灯光开关、供电变化通知 |
| API | native注册、原子环境快照、revision、全局forward |
| World | 建筑/采集创建、刀修、采集成长、重建、升级材料事务 |
| NPC、Combat | HZS实体登记/停放/原生目标、建筑攻击、机枪/火焰/地雷 |
| Grenades | 共享20Hz弹道、重力、整段体积射线、爆炸/清理 |
| HUD、Commands、Admin | 本地化HUD、E/建造/升级/仓库/管理员菜单与命令 |
| Lifecycle | 日夜/回合/快照/结果、出生、胜利ForceChangeLevel |

容量不是无限：建筑128、节点80、NPC256、基地8、Boss8、配方32；默认防御设施96，灯16，同时飞行榴弹24。实体预算1500，硬上限1800，关键余量248；动态预留按32槽、100普通池、1Boss、3基地、32节点为429。不能只算自己新建的实体。

## 保持主体边界和时序

HZS仓库D:\github\git\css\H-AN-CSS-ZombieScenario，本轮27个主体源/API文件只读基线未变。运行UnmodifiedHZS.smx版本2.3.2。外部使用已有Han_IsZombie/GetZombieName/GetZombieCount/GetZombieByIndex/SetZombieTarget/UnlockZombie/SetCustomAnimationEx/SafeDamageZombie以及GameStart/End、ZombieWin/HumanWin、DamagePre、Death等；具体声明读现有include。

主体先发Han_OnGameStart，然后SpawnRoundZombie。外部必须在该段保留hzs_maxzombiespawn=100，下一帧才应用阶段上限；否则池创建被截断，夜晚不能补足。现场演示阶段60也必须先创建100池，隐藏实体不是活动进攻人数。不要杀停放NPC来减负，不重写HZS追敌/补怪，不增加自定义生成API。

实体用EntRef，客户端用serial，建筑用UID；延迟任务核对epoch。UID只在本提供方/地图会话内稳定。新的回合/API延迟消费者任务要再次查询状态，避免旧任务覆盖新回合。

资源换模型保留SetModelScale实体输入和EnableCollision/DisableCollision流程；只写m_flModelScale曾导致包围盒不同步。不要仅凭模型名/截图断言碰撞。

升级扣料后失败要退材料，投入记录只计真正花费，拆除按50%实际投入整数退款；无主和离线交接不能复制材料。新动态灯子实体和榴弹也要计预算并清理。

## 构建、翻译、验证

本机Python用D:\python\python.exe。SourcePawn编译器默认D:\css\sp\大灾变1.12\addons\sourcemod\scripting\spcomp64.exe及同目录include。仓库内执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
D:\python\python.exe .\tools\build_translations.py --check --report .\tests\reports\next\translation.json
```

玩家文本来自tests/phrases_*.json，更新对应源后生成phrases再检查。不得只编辑生成phrases而忘源。标题已译文本用%s包住，避免再次格式化丢百分号。

有功能改动才跑相关验证，纯文档不重跑专服。0.4独立回归必须使用tests/runtime/v04_server和27139，不要覆盖用户录制服27129。检查隔离进程已停止后：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -IntegrationProbe
D:\python\python.exe .\tools\prepare_isolated_server.py
D:\python\python.exe .\tools\test_local_server.py --server-root .\tests\runtime\v04_server --port 27139 --integration --power --report-dir .\tests\reports\next\integration
```

测试产物tests/compiled/ShelterIntegrationProbe.smx，不进入正式交付。生产复核先prepare_isolated_server.py --production，然后runner省略--integration。独立API消费者tests/ShelterAPIConsumer.sp，不装正式服。原经济/成长独立测试关闭供电以验原功能，新power用真实供电；这种分组不能替代缺电增强器实际加成用例。

prepare_isolated_server会硬链接不可变资源、复制可变配置；不要去改硬链接资产。隔离目录不是开发源码。tools/release_shelter04.py仅打包可用，但--deploy有旧哈希保护且现在现场已定制；不要再以整套默认配置覆盖录制服。

## 现场操作

tools/local_rcon.py只连接本机27129并读服务器根.zsh-local-rcon.txt。不输出密码、不打印含RCON参数的进程CommandLine。按ExecutablePath识别本服进程，不按srcds名字杀其他端口。

配置/实体结构改动冷启动生效；sm_zsh_reload在战役已建快照时拒绝，不是无条件热加载。现场changelevel已知崩溃且用户要求不修，使用安全关服后启动：sm_zsh_fail →等约5秒→sm plugins unload UnmodifiedHZS→quit→等待退出。tools/Start-LocalShelter.ps1隐藏启动、初始+map；运行换图仍统一changelevel，不能用map。

部署前备份、保持现场演示配方；正式服只加载HanZombieShelter.smx和UnmodifiedHZS.smx。zsh_test summary必须Unknown。临时只读NPC审计插件在plugins/disabled中，不自动加载。不要把测试fixture/摄像操作脚本直接装进正式服。
''')
    write('02_开发接手/关键路径与配置索引.md',r'''
# 关键路径与配置索引

| 用途 | 路径 |
| --- | --- |
| 外部唯一开发仓库 | D:\github\git\css\CSS-HZS-ZombieShelter |
| HZS主体参考仓库 | D:\github\git\css\H-AN-CSS-ZombieScenario |
| 现场测试服/27129 | D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server |
| 正式服务配置 | 上述服务器\cstrike\addons\sourcemod\configs\HanZombieShelter |
| HZS武器修正规则 | 同configs\HanZombieScenario\HanZombieScenarioDamageConfig.cfg |
| 地图HZS桥接 | 同configs\HanZombieScenario\zsh_desertplant_cso_v1_d\HanZombieScenarioStage.cfg |
| 客户端 | D:\SteamLibrary\steamapps\common\Counter-Strike Source |
| 0.4正式交付目录 | D:\gmod昼夜求生资源\2026年10月15日\昼夜求生外部插件0.4 |
| 0.4正式ZIP | D:\gmod昼夜求生资源\2026年10月15日\CSS-HZS-ZombieShelter-0.4.0.zip |
| 本轮完整回归 | 外部仓库\tests\reports\v04\integration_final、production_final |
| 演示前后状态证据 | 外部仓库\tests\reports\v04\demo_profile；本交接05也有副本 |
| 动态天空计划 | D:\gmod昼夜求生资源\动态天空盒测试\文档 |
| 灯具旧参考 | D:\css\sp\sourcedaima\scripting\[华仔]新采集建造模式带数据库.sp |
| 实体炮弹旧参考 | D:\css\sp\sourcedaima\scripting\新暴动服自律飞机.sp |

22份cfg（21份通用、1份当前地图），均为schema_version=1。旧注释0.1/design_revision不是外部运行版本，不要据此退回旧源码。

| 配置 | 作用 |
| --- | --- |
| mode / lifecycle | 启停、日夜时长、暴露伤害、服务器策略、失败回退/恢复 |
| resources / drops | 五材料产出/成长/重抽权重、死亡掉落 |
| zombies / days / bosses | 原生池/建筑攻击、阶段人数/倍率/Cvar、Boss最大HP |
| bases | HP、维修、等级、升级材料 |
| turrets / flame_turrets | 机枪/火焰等级、伤害、耗电、配方 |
| fences / mines | 被动围栏/地雷 |
| amplifiers / progression | 增强器门槛/加成、四伤害通道与最大最终伤害 |
| entities / management | 全服实体预算、设施上限、拆除、断线转共享 |
| sounds | 交互音效 |
| power / generators | 供电开关和额外实体限制、发电机HP/产能/刀修 |
| lights / grenade_turrets | 灯光参数、弹道参数、对应等级/配方 |
| maps/zsh_desertplant_cso_v1_d | 3基地/32资源点/Boss/出生禁建点位 |

本图Stage固定count=99999、healthboost=0、普通putong_stage1、customzombies=boss_pangzi:1；不是靠推进主体普通关卡完成昼夜。HZS普通基础模板health=100，Boss实体由外部bosses.cfg覆盖到默认50000/现场3000。M4A1+0.0是服务器全局武器规则，不能在恢复旧配置时恢复成-1000.0。

用户要求只读检查索敌：Han_Think选最近可见玩家不设最大距离，但引擎CHostage跟随仍有直线2000/路径4000限制。配套hzs_zombieskill/HZSZombieSkill/detours.inc的PatchRangeJA、PatchPathLengthJA、PrepIdleDetour处理该限制，当前录制服未加载它；不能声称当前引擎全图跟随已解除。全局HanZombieScenarioConfig.cfg没有索敌距离字段，attack_range仅为近身攻击距离。此次只更正文档，没有改索敌或安装模块。

参考快照/仓库保存当前外部源码、默认配置、正式SMX、docs和工具；03/生效配置保存现场低成本配方。二者有意不同。初次0.4部署备份服务器\zsh_test_backup\v04_20261015_020814，当前演示前备份路径见03/配置应用记录.json。
''')
    write('03_录制演示配置/录制数值与演示操作.md',r'''
# 当前录制数值与演示操作

用途：一个人录开发进度/玩法视频，少量采集后就能造、升、修；不赠送巨量材料、不自动升到100级。现场正式插件仍为0.4.0，配置单独定制，发行包默认平衡未变。

| 项目 | 当前测试服 | 原默认/说明 |
| --- | --- | --- |
| 采集单次产出 | 3材料 | 原为1，个人背包 |
| 资源完整成长 | 10秒，耗尽后继续重抽 | 原45/60/90/120/180秒 |
| 节点可采次数 | 木5、铁4、铜3、银2、金1 | 未提高到巨大配额 |
| 重抽相对权重 | 木45/铁40/铜10/银4/金1 | 增加铁，仍展示稀有资源 |
| 建造/升级材料 | 少量木/铁，铜银金不作演示门槛 | 五种材料仍能采/入仓 |
| 白天/夜晚 | 300/120秒，30秒预警 | 原180/180 |
| 活动普通NPC | 所有天白天20、夜晚60（用户最新明确要求） | 原8—12 / 50—100 |
| 普通NPC血量 | 模板100，倍率1.0，不随天增血，放行上限150 | 150是保护上限，不是每只150 |
| 固定Boss | 3000HP，依旧休眠/受伤唤醒 | 原50000；基地1级仍锁伤，先升2级 |
| 原生隐藏池 | 100普通+1Boss | 不是101只同时进攻 |
| 普通攻击建筑 | 见生效zombies.cfg | 其他生命/武器属性大体保留 |
| 发电机产能 | 100/200/350 | 供电规则保持真实 |
| 维修 | 普通10HP/刀、发电机50HP/刀；不扣材料 | 全局维修冷却0.5秒 |

| 建造 | 木 | 铁 |
| --- | ---: | ---: |
| 普通机枪塔 | 3 | 2 |
| 重机枪塔 | 4 | 3 |
| 火焰塔 | 3 | 3 |
| 围栏 | 2 | 0 |
| 地雷 | 0 | 2 |
| 增强器 | 3 | 3 |
| 发电机 | 4 | 3 |
| 灯具 | 2 | 1 |
| 榴弹塔 | 4 | 4 |

所有可升级建筑统一为升2级2木3铁，升3级3木4铁，基地升4级4木5铁。未解锁的等级不开放；增强器仍要求基地达到对应2/3/4级。满采一个木点15木、一个铁点12铁，够造发电机+灯+普通塔，再做一次基础升级；榴弹塔多采一两刀铁即可。不是一刀获得所有材料，木铁点按模型/瞄准提示寻找。

## 建议录制顺序

1. 控制台connect 192.168.0.102:27129，进入游戏加入CT并等原生开始。机器IP变了以当前局域网地址为准。没有真人时处于Waiting/休眠并不等于插件故障。
2. 刀砍木/铁资源，展示采集反馈、sm_materials、耗尽和10秒再生。可以bind b sm_build打开建造菜单，也可聊天!build。
3. 同一基地安全区内先造发电机，再造灯、普通炮塔/榴弹炮塔。看HUD电网、设施E菜单的耗电/产能和等级。放在空旷平地，避开出生/基地禁建柱和其他模型；无效位置会拒绝且不扣材料。
4. 对基地按E可升2级；对自建设施按E选择升级，再确认。升级后留意耗电增加、剩余产能和HP，不要求开高等级。
5. 切夜展示丧尸和炮塔。管理员可通过RCON执行sm_zsh_phase night，之后sm_zsh_phase day恢复白天并重置本阶段倒计时；服务器端使用tools/local_rcon.py即可，无需把密码放视频中。
6. 让丧尸攻击发电机，观察HP降低/产能减少/设施停机；刀修恢复产能和开机。玩家枪/刀对建筑按修理处理，不是主动破坏发电机的方式。升级不会消除原有缺血量。
7. 要展示过载可在同一电网造两台1级榴弹塔+一台重机枪+一盏灯，总需求95，发电机略受伤就会有设备停机；随后修理或升级发电机。先用少数设备完成基础演示，避免不必要的大量实体。
8. 演示仓库存取、拆除退款、围栏/地雷；Boss要等升级基地后再主动攻击，当前胜利换图可能碰到已知本地崩溃，录基础玩法不必触发胜利换图。

无需通过sm_zsh_grant灌上万材料。当前没有新增无敌/瞬间建造逻辑。增强器缺电加成缺漏尚待修，不能把该行为拍成已完成的正确机制。

## 几万血普通丧尸的原因和处理

在HZS原示例配置及现场全局HanZombieScenarioDamageConfig.cfg均找到weapon_m4a1=-1000.0。普通枪伤若33，规则后为-967；Han_HurtZombie直接做health - damage，于是100变1067，持续射击不断加血。现场只读审计读到普通HP73503和30152；同池其他普通仍100/120，Boss49837独立，不是把Boss看成普通。

已把现场M4规则改为+0.0，保持引擎正常伤害；AK47 +40和自定义AK×2原规则保留。没有修改HZS源码/SMX。归因是原示例负伤害配置，不能说0.4自动回归遗留了高HP：0.4回归在隔离27139，27129只运行正式SMX，测试入口Unknown。

原阶段max_raider_hp=30000只在普通NPC放行时截HP，不能截之后的负伤害加血。单纯降低该上限不能修根因。通过安全关服、冷启动清掉已被加血的实体后，白天/夜晚分别检查普通池100只，全部100HP，Boss3000HP。实际M4真人射击画面仍由用户录制/复核；此轮没有假称做过真人截图验收。

## 保存、恢复与再次生效

03/生效配置是完整现场22cfg及修正过的HZS武器规则，附未改的HZS模板/地图Stage作参考。配置应用记录.json有修改前/后SHA、实际备份路径、配方和升级报价。参考快照/仓库中的cfg为发行默认，不能直接覆盖本服演示配置。

先安全关服再执行本目录恢复演示前平衡.ps1，可恢复演示前22份外部cfg。脚本保留已修正的M4规则，绝不把-1000恢复回来；检查当前配置哈希，若用户后来改过则拒绝覆盖。恢复后用仓库tools/Start-LocalShelter.ps1冷启动。本机changelevel崩溃暂不修，勿为恢复配置尝试运行map。
''')
    write('03_录制演示配置/恢复演示前平衡.ps1',r'''
param()
$ErrorActionPreference = 'Stop'
$record = Get-Content -LiteralPath (Join-Path $PSScriptRoot '配置应用记录.json') -Encoding UTF8 -Raw | ConvertFrom-Json
$demoServerRoot = [IO.Path]::GetFullPath($record.server_root)
$demoBackupRoot = [IO.Path]::GetFullPath($record.backup)
$demoExe = Join-Path $demoServerRoot 'srcds_win64.exe'
if (Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $demoExe }) { throw 'Stop this server safely before restoring configs.' }
$plan = @()
foreach ($item in $record.changed) {
    if ($item.server_relative -like '*/HanZombieScenarioDamageConfig.cfg') { continue }
    $relative = $item.server_relative.Replace('/', '\')
    $target = [IO.Path]::GetFullPath((Join-Path $demoServerRoot $relative))
    $preserved = [IO.Path]::GetFullPath((Join-Path $demoBackupRoot $relative))
    if (-not $target.StartsWith($demoServerRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Target escaped server root' }
    if (-not $preserved.StartsWith($demoBackupRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Backup escaped backup root' }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $item.profile_sha256) { throw "Config changed after recording profile; review manually: $target" }
    if ((Get-FileHash -LiteralPath $preserved -Algorithm SHA256).Hash -ne $item.before_sha256) { throw "Backup hash mismatch: $preserved" }
    $plan += [pscustomobject]@{ Target=$target; Backup=$preserved }
}
foreach ($entry in $plan) { Copy-Item -LiteralPath $entry.Backup -Destination $entry.Target -Force }
Write-Output "Restored $($plan.Count) shelter configs. M4A1 +0.0 fix kept. Start this server again to apply."
''')
    write('05_验收证据/验收与现场说明.md',r'''
# 验收与现场说明

0.4首次功能开发最终证据在外部仓库tests/reports/v04，本目录复制最终有效报告。不混加各分组：联合409/0已含新power和既有功能；power专项61/0中有19条旧World前置，不能再加到409上。API消费者独立计数61/0，生产最后35/0。

| 证据 | 结果/解释 |
| --- | --- |
| integration_final | 409通过/0失败；runtime0/updater0，真实隔离专服27139 |
| power_fifth | 61/0，已包含于联合轮；发电/灯/炮塔/弹体/刀修/付费升级/真实回合重建 |
| production_final | 正式8→50→8/Boss1、API晚加载/卸载/恢复、测试入口Unknown，runtime0/updater0 |
| core_baseline.json | 27主体源/API文件未变 |
| translation_final.json | 172词条、7源，引用/参数/硬编码检查通过 |
| deployment.json/live_ready.json | 初次正式部署和崩溃后冷启动记录，PID为历史值 |
| package.json | 原0.4ZIP SHA256/CRC/文件数/地图SHA |
| demo_profile/before.json | 当前配置调整前实体HP73503/30152及普通100/120；临时只读审计插件在场 |
| demo_profile/day.json、night.json | 初次录制配置普通100HP；4/16是较早临时值，已被用户20/60取代 |
| 配置应用记录.json、demo_profile/ready.json | 用户最新20/60已写入各阶段并加载；未完成新20/60实际人数专项，不虚构已通过 |
| demo_profile/ready.json | 最终现场进程、正式SMX/配置哈希、插件/测试入口/错误检查，以该快照时间为准 |

首次失败报告integration_first/second、power_third/verified仍保留在仓库，未覆盖。失败来自测试fixture假客户端管理员权限、RemoveEntity延迟、模型初始物理位置等，随后修了测试路径；不假称首次全通过。

联合409/0证明既定用例通过，不等于覆盖所有行为。交接代码复核新增发现缺电增强器加成过滤缺漏，列06待修；此项缺少用例，不能以总数否认实际缺陷。新功能真人视觉、多人长期压力、经济/移动命中率仍待验。

原0.4ZIP是默认平衡，不含本次演示调参/HZS现场武器规则修正。发行包未装HZS主体SMX，不含完整NPC原资源和密码。BSP未重编，地图哈希保持原值。03演示配置与参考快照帮助区分现场值，文件清单SHA256用于核对当时产物，不能替代以后新改动的验证。
''')
    write('06_下一步/下一步与已知问题.md',r'''
# 下一步与已知问题

当前用户要录视频和审阅进度。先协助录制/确认菜单灯光炮塔画面，不擅自继续制作天空插件或更大玩法。最新用户需求优先于历史待办。

## 明确发现、尚待修复

增强器缺电仍可能加成：addons/sourcemod/scripting/HanZombieShelter/Progression.inc/AmplifierBonus循环只过滤used/alive/kind/HP/ref，没有BuildingPowered；而ProgressionBaseLevel的基地过滤加了该检查，基地本身不耗电，因此不能代替增强器过滤。这是代码复核发现的具体缺漏，本轮只做进度归档和录制配置，未修改SMX。修时在增强器累计入口过滤供电，增加真实电网断电→伤害加成消失→修复恢复的验证，确认兼容enabled=0仍累加。现有PowerProbe虽检查增强器实体/分配，未断言伤害加成随断电变化。不要修改HZS主体绕过。

M4负伤害防御：现场配置已修正，症状已消除，但主体扣血路径仍接受负数。后续如要通用防御，需用户决定是在独立外部DamagePre夹到0还是改主体；本轮未修改主体，不自作主张扩大修复范围。恢复配置必须保留M4 +0.0，不把示例-1000重新带回。

## 用户明确暂不修

本机changelevel崩溃：用户在其他插件也复现，疑似本地服务器bug，暂只记录。初次0.4部署changelevel退出的Windows异常steamclient64.dll/c0000409，SM无错误；冷启动可恢复。运行换图必须changelevel，不能map。不要把“用户要求暂不修”误解为批准更改HZS主体/引擎。胜利ForceChangeLevel仍存在，因此录完基础玩法再考虑Boss通关画面。

旧主体机器人移除/清理时序问题也不在本轮修复范围；临时机器人测试结束先安全停局再关服，不在运行关卡中随意bot_kick。

用户明确纠正：录视频根本不需要机器人。CS:S引擎内人质实体与BOT存在共享指针冲突，主体源码已经说明NPC生成后加BOT会炸服，只有安装对应修复插件后才允许。本服没有据此安装修复插件；不要再为录制加BOT，不使用临时机器人启动方式。普通启动bot_quota=0，移除了本次临时加的WarmupBot选项。机器人异常不是继续扩展测试/修引擎的理由。

已按用户指定直接阅读hzs-supporting-plugin/addons/sourcemod/scripting/hzs_botaddfix.sp、配套gamedata和README。具体机制为CSGameState::Reset的人质循环初始化触发InitializeHostageInfo硬编码数组越界；补丁在循环回边改cmp reg, [g_Hostages.m_Size]为cmp reg, 0并填NOP，跳过NPC人质初始化。完整路径、32/64位偏移、成功日志、主体拦截与引擎补丁区别见02/BOT与人质NPC引擎限制.md。这个已知原因不再靠加BOT重现，当前只记录，不安装补丁。

索敌结论已更正：Han_Think不设选目标距离上限不能等同引擎全图跟随已解除；hzs_zombieskill另有2000/4000跟随限制与Idle补丁，当前服未加载。全局配置没有相应字段。用户原要求只读检查，未修改索敌。

用户报告窗口化全屏切换后一度紫黑材质，切回窗口化已恢复，并明确取消继续纹理检查。本轮不再检查/修复纹理，也未据此替换BSP或客户端资源。

用户指出简单调参耗时过长，要求收敛工作。本次停止额外运行人数专项，不再为20/60强行解决空服休眠/机器人问题。20/60的各阶段字段已配置并由正式插件正常加载；早期4/16普通HP=100实测仍是血量证据，但不冒充20/60实测。最终恢复默认无机器人启动，现场0人时休眠、活动数暂不到目标是已知现象，玩家进入后再观察。

此次人数复核还触发主体Cmd_BlockAddBot对控制台client=0调用CPrintToChat的异常，随后进程在sourcemod.2.css.dll/c0000005退出；另一次关服有steamclient64.dll/c0000409。记录在05/demo_profile/新增异常记录.json。此错误与先前changelevel事件分别记录，不能混同为同一个已定位原因。本轮没有改主体，避免在游戏已开始后执行bot_add；服务器已重新启动，不把本次新错误写成“当前日志0错误”。

## 待用户实玩审阅

灯具亮度/半径和客户端动态灯数量、E/升级菜单信息、场景亮暗、移动目标弹道、近墙爆炸与队友无伤画面、多基地供电归属、多人离线转仓和长时实体负载。现在是自动运行证据，通过真人逐图复核前，不写成0.4视觉全部通过。

录制平衡仅作演示，正式配方/普通NPC人口/BossHP/枪械倍率尚未重新定稿。录完用户决定恢复默认或继续演示值，注意默认普通HP倍率虽升，但原100基础不会自行到几万；必须先查武器负伤害。

## 后续独立天空模块

已有D:\gmod昼夜求生资源\动态天空盒测试\文档方案，0.4已经提供草案要求的GetEnvironmentAPIVersion/GetEnvironmentState/OnEnvironmentStateChanged。当前未编写独立天空/地图动态灯组模块、未改BSP。以后明确开工后按现有计划消费真实API，兼容晚加载、库移除、提供方重载、revision会话重置和延迟任务epoch。别从聊天文字猜昼夜；以后新增接口按新需要补充，保持只读职责优先。

专用发电机/灯具/榴弹炮塔美术、燃料/电池/电缆/跨基地输电、磁盘战役存档、NPC自定义生成API均未制作，也不是本次隐含承诺。
''')
    write('06_下一步/新窗口接手提示词.txt',r'''
请先阅读 D:\gmod昼夜求生资源\文档\2026年10月15日_v0.4_进度文档\README.md，再按其中顺序读01/02/03/04/05/06。

开发仓库 D:\github\git\css\CSS-HZS-ZombieShelter；CS:S SourceMod外部插件HanZombieShelter 0.4.0，HZS主体2.3.2保持现有源码/SMX。本轮发电机/三基地独立供电、缺电炮塔、灯具、榴弹炮塔及10native/5forward公共API已制作，409/0联合隔离回归。天空环境模块只有方案/API，尚未制作，不擅自开工。

27129正式测试服已切换录视频配置：每刀3材料、10秒再生、建造升级只少量木铁，白天20/夜晚60（用户最新要求）、普通100HP、Boss3000HP。仓库/0.4ZIP默认平衡未变，不能直接覆盖现场。03有完整生效cfg、修改记录和恢复脚本。普通几万血根因是原HZS示例M4A1 -1000规则使负伤害加血，已改现场+0.0并冷启动；不要恢复该错误规则，不是测试SMX遗留。

06记录一个明确待修：Progression.inc/AmplifierBonus没有过滤BuildingPowered，缺电增强器可能仍加成，409既有用例未覆盖此结果。先按用户最新安排处理，不能把它写成已正确完成。

用户明确：换图用changelevel，不能map；其他插件也复现换图崩溃，怀疑本地服务器问题，暂只记录、不修复。本轮不要因此修改HZS主体/引擎。

录制演示不需要机器人。本服没有安装人质NPC与BOT共享指针冲突修复插件；主体源码已明示NPC存在后加BOT会崩溃并禁止该操作。录制服保持bot_quota=0，不能再为了验证人数添加BOT。0人空服休眠不强求实际人数达到20/60，直接等用户真人进入演示。

已直接阅读用户指定hzs_botaddfix.sp和gamedata：Reset循环回边cmp改为0，跳过InitializeHostageInfo初始化以规避硬编码数组越界。详读02/BOT与人质NPC引擎限制.md和原仓库AGENTS.md，不能再为调参加BOT。另更正旧答复：Han_Think选目标不限制距离，不代表引擎2000/4000跟随限制已解除；配套hzs_zombieskill的对应补丁当前未安装。窗口化全屏材质问题用户切回窗口化已恢复，明确取消纹理检查，不再继续。

0.4新菜单/灯光/弹体未做真人逐图验收，多人长期压力/平衡仍待评估。0.3的50截图只作历史，不是0.4证据。默认100级未开启。无磁盘存档，重启新战役。

工具：D:\python\python.exe；build.ps1；27139隔离tests/runtime/v04_server测试，不在录制服27129部署探针或UI fixture。读现场正式SMX/配置哈希和plugins/status；不要打印RCON密码/进程完整命令行。新改动保留现场低成本配方，按EntRef/serial/UID/epoch处理异步与事务，并补有意义的断言。最新用户需求如下，先评估再按授权继续：
''')
    # Official documentation and public API are copied verbatim for consumers.
    for source, dest in [
        ('docs/昼夜求生公开API.md','04_公开API/昼夜求生公开API.md'),
        ('docs/BOT与人质NPC引擎限制.md','02_开发接手/BOT与人质NPC引擎限制.md'),
        ('addons/sourcemod/scripting/include/HanZombieShelterAPI.inc','04_公开API/HanZombieShelterAPI.inc'),
        ('docs/发电机供电与榴弹炮塔.md','01_项目进度/供电与榴弹实现说明.md'),
        ('docs/0.4开发与验收记录.md','05_验收证据/仓库0.4开发与验收记录.md')]:
        destination=OUT/dest;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(REPO/source,destination)
    sky=Path(r'D:\gmod昼夜求生资源\动态天空盒测试\文档')
    for source in sky.glob('*'):
        if source.is_file():
            dest=OUT/'04_公开API/天空模块计划参考'/source.name
            dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
    for name in ('core_baseline.json','translation_final.json','deployment.json','live_ready.json','package.json'):
        source=REPO/'tests/reports/v04'/name
        if source.exists():shutil.copy2(source,OUT/'05_验收证据'/name)
    for group in ('integration_final','production_final','power_fifth','demo_profile'):
        src=REPO/'tests/reports/v04'/group
        if src.exists():shutil.copytree(src,OUT/'05_验收证据'/group,dirs_exist_ok=True)
    # Small source/config/test snapshots, excluding runtime, secrets and large logs.
    snapshot=OUT/'参考快照/仓库'
    for prefix in ('addons/sourcemod/scripting','addons/sourcemod/configs','docs','tools','server-support'):
        for source in (REPO/prefix).rglob('*'):
            if source.is_file() and '__pycache__' not in source.parts:
                dest=snapshot/source.relative_to(REPO);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
    for source in (REPO/'tests').glob('*'):
        if source.is_file() and source.suffix in ('.sp','.inc','.json','.py'):
            dest=snapshot/source.relative_to(REPO);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
    for relative in ('README.md','AGENTS.md','build.ps1','.gitignore','addons/sourcemod/plugins/HanZombieShelter.smx','addons/sourcemod/translations/HanZombieShelter.phrases.txt'):
        dest=snapshot/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(REPO/relative,dest)
    supporting=Path(r'D:\github\git\css\hzs-supporting-plugin')
    for relative in ('README.md','addons/sourcemod/scripting/hzs_botaddfix.sp',
                     'addons/sourcemod/gamedata/hzs_botaddfix.gamedata.txt',
                     'addons/sourcemod/scripting/HZSZombieSkill/detours.inc'):
        dest=OUT/'参考快照/hzs-supporting-plugin'/relative
        dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(supporting/relative,dest)
    write('参考快照/说明.md','参考快照/仓库是交接时源码、正式二进制与发行默认配置。03/生效配置是录制服实际配置。它们不是同一套平衡。开发修改原仓库，不编辑快照充当当前源码；重新接手按SHA核对。这里不是完整CS:S/HZS/地图资源安装包。')
    write('交接索引.json',json.dumps({'created':datetime.datetime.now().isoformat(),
        'version':'0.4.0','api_version':1,'workspace':str(REPO),'server_root':str(SERVER),'server_port':27129,
        'active_profile':'recording-v04','release_profile':'default','demo_backup':profile['backup'],
        'integrated_assertions':{'pass':409,'fail':0},
        'known_unfixed':['amplifier bonus loop missing BuildingPowered filter','user-deferred local changelevel crash'],
        'map':'zsh_desertplant_cso_v1_d','human_visual_acceptance_v04':False,
        'bot_fix_source_read':True,'recording_bot_policy':'no bots; no additions while NPCs exist',
        'bot_fix_installed':False,'infinite_engine_following_fix_installed':False,
        'texture_investigation':'user cancelled after window-mode switch restored materials'},ensure_ascii=False,indent=2))
    rows=[]
    for path in sorted(OUT.rglob('*')):
        if path.is_file() and path.name!='交接文件清单.json':
            rows.append({'relative':path.relative_to(OUT).as_posix(),'bytes':path.stat().st_size,
                         'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    write('交接文件清单.json',json.dumps({'created':datetime.datetime.now().isoformat(),'count':len(rows),'files':rows},ensure_ascii=False,indent=2))
    print(f'Handoff written: {len(rows)} files; {OUT}')

if __name__=='__main__':main()
