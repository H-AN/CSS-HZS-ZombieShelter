# HanZombieShelter API v1

提供方0.4.0，库名 `HanZombieShelter`；头文件 `addons/sourcemod/scripting/include/HanZombieShelterAPI.inc`。只读状态和全局forward，不提供切阶段、刷怪或扣料后门。HZS主体API职责保持原有边界。

| Native | 用途 |
| --- | --- |
| HZSH_GetAPIVersion / HZSH_GetEnvironmentAPIVersion | 当前均返回1 |
| HZSH_GetEnvironmentState | 一次读启用状态、游戏阶段、有效环境、天数、revision |
| HZSH_IsRunning | 是否处于有效Day/Night玩法 |
| HZSH_GetPhaseTimeRemaining | 本阶段剩余秒数，非运行/到期为0 |
| HZSH_GetBaseCount / HZSH_GetBaseInfo | 基地数量、UID、等级、HP、存活 |
| HZSH_GetBasePower | 指定基地发电、总需求、已分配、停机数量 |
| HZSH_GetBuildingInfo | 按UID查种类、电网、等级、HP、存活/供电、耗电和有效发电 |
| HZSH_GetPlayerMaterials | 在线玩家木、铁、铜、银、金五个材料 |

共10个native。参数和enum以正式include为准，无效索引、UID或玩家查询返回false。基地索引从0开始；UID只在当前提供方/地图会话内稳定，不是实体索引，不应跨图保存。实例记录还在时可查已毁设施，须检查alive。

完整环境Getter返回false表示没有地图状态，不能接管；返回true且modeEnabled=false是有效停用快照，消费者停止接管。新地图/新回合初始化Day；StartDay/Night同步环境；夜间Ending保留Night，直到明确重开或停用。revision只用于同一提供方会话的通知去重，不保证跨重载递增。

| Forward | 已提交状态之后的通知 |
| --- | --- |
| HZSH_OnEnvironmentStateChanged(revision) | 初始化、启停、阶段/天数、结算、准备失败、依赖失效/恢复 |
| HZSH_OnPhaseChanged(previous,current,day,duration) | 阶段切换；同阶段倒计时重置也通知，Waiting/Ending时长0 |
| HZSH_OnRoundStarted(day,restored) | 主体开始、外部进入Day后；restored表示准备本轮时是否使用战役快照 |
| HZSH_OnOutcome(victory,previousDay,nextDay,terminal) | 失败/通关和天数回退已经落实 |
| HZSH_OnBasePowerChanged(core,production,demand,allocated,offline) | 电网变化，相同结果不重复广播 |

这是真正的SourceMod全局forward，不需要解析本地化聊天广播。消费者收到事件后查询完整状态，延后执行时再次查询，并用自己的地图epoch、配置代次和库连接代次拒绝旧任务。

## 天空模块

保留动态天空文档草案的三个名字和语义：`HZSH_GetEnvironmentAPIVersion`、`HZSH_GetEnvironmentState`、`HZSH_OnEnvironmentStateChanged`。只制作本轮授权的API；天空覆盖和地图可切换灯组仍由独立环境模块负责。

可选依赖示例：

```sourcepawn
#include <sourcemod>
#undef REQUIRE_PLUGIN
#include <HanZombieShelterAPI>

void SyncLatestState()
{
    if(!LibraryExists("HanZombieShelter")
        || GetFeatureStatus(FeatureType_Native,"HZSH_GetEnvironmentState")!=FeatureStatus_Available)return;
    if(HZSH_GetEnvironmentAPIVersion()!=1)return;
    bool enabled;HZSHPhase phase;HZSHEnvironmentPhase environment;int day,revision;
    if(!HZSH_GetEnvironmentState(enabled,phase,environment,day,revision))return;
    if(!enabled)return; // Release only this consumer's own environment ownership.
    // Coalesce delayed work and query again when that work executes.
}
public void HZSH_OnEnvironmentStateChanged(int revision){SyncLatestState();}
public void OnAllPluginsLoaded(){SyncLatestState();}
public void OnLibraryAdded(const char[] name)
{
    if(StrEqual(name,"HanZombieShelter"))SyncLatestState();
}
```

还需在OnLibraryRemoved取消自己的待执行任务；晚加载主动同步，依赖恢复重新查询。天空延后覆盖、读回、限频和地图灯组继续依照原环境计划实施。独立消费者验收源码为 `tests/ShelterAPIConsumer.sp`，不装进正式发行包。
