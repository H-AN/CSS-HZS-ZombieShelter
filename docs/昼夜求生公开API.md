# HanZombieShelter API v2（环境 API v1）

提供方0.6.0，库名 `HanZombieShelter`；头文件 `addons/sourcemod/scripting/include/HanZombieShelterAPI.inc`。提供只读状态、全局forward和明确授权的基地选择/安全回城，不提供切阶段、刷怪或扣料后门。HZS主体API职责保持原有边界。

v0.6 保持总体 API v2、环境 API v1 和所有旧枚举值，末尾追加 `HZSHBuilding_Campfire=14`。基地数量包含本战役已分配的自建基地，已毁编号在新战役前不回收。`HZSH_GetBaseInfo` / `HZSH_GetBuildingInfo` 的 `alive` 表示已完成并有效存活：施工蓝图仍可查询，返回 `alive=false`，不能将“查询成功”当成基地已启用。自建基地完工后可使用现有电网、仓库和回城 API；回城在动态区域内检查落地和真人 hull，无可用位置时返回 false。篝火仅提供夜间暴露保护，不是基地或电网；现有接口不单独返回篝火燃烧阈值或施工标志，应读取相关配置，不依赖 HP=1 猜测状态。

| Native | 用途 |
| --- | --- |
| HZSH_GetAPIVersion / HZSH_GetEnvironmentAPIVersion | 分别返回2/1 |
| HZSH_GetEnvironmentState | 一次读启用状态、游戏阶段、有效环境、天数、revision |
| HZSH_IsRunning | 是否处于有效Day/Night玩法 |
| HZSH_GetPhaseTimeRemaining | 本阶段剩余秒数，非运行/到期为0 |
| HZSH_GetBaseCount / HZSH_GetBaseInfo | 基地数量、UID、等级、HP、存活 |
| HZSH_TeleportPlayerToBase | 按存活基地 UID 将存活真人传至该基地安全空闲出生点，返回实际成功/失败 |
| HZSH_ShowBaseTeleportMenu | 仅显示基地选择菜单，通过调用插件的 callback 返回基地 UID，不自动传送 |
| HZSH_GetBasePower | 指定基地发电、总需求、已分配、停机数量 |
| HZSH_GetBuildingInfo | 按UID查种类、电网、等级、HP、存活/供电、耗电和有效发电 |
| HZSH_GetPlayerMaterials | 在线玩家木、铁、铜、银、金五个材料 |
| HZSH_GetMaterialCount / HZSH_GetMaterialId | 登记材料数量及按当前配置修订枚举稳定 ID |
| HZSH_GetPlayerMaterialAmount / HZSH_GetBaseMaterialAmount | 按材料 ID 查询在线玩家（不要求存活）或基地余额 |

共16个native。旧 `HZSH_GetPlayerMaterials(client, int materials[5])` 永远恰好写五格，固定顺序 wood/iron/copper/silver/gold；新增材料通过 ID 接口查询，不得向旧数组写全部材料。新数量查询遇未知 ID、非法对象或未初始化返回 false 并写0，合法零余额返回 true/0。严格要求总体版本等于1的消费者需要调整版本检查，环境接口版本仍为1。

## 基地选择和安全回城

两个回城接口作为 API v2 的增量 optional native 提供，不改旧枚举值。旧版提供方可能没有这些 native，调用前应检查 `GetFeatureStatus`；仅判断版本号等于 2 不足以确认新增能力。

```sourcepawn
native bool HZSH_TeleportPlayerToBase(int client, int coreUID);
native bool HZSH_ShowBaseTeleportMenu(int client, Function onSelected, any context=0);
// Callback: void onSelected(int client, int coreUID, any context)
```

`coreUID` 使用 `HZSH_GetBaseInfo` 返回的正数 UID，**不是基地数组索引，也不是实体索引**。UID 只在当前提供方/地图会话内有效。直接传送接口不要求玩家管理员权限，供普通商店等插件调用；本插件没有因此新增普通玩家命令或收费系统。

直接传送要求模式 Running、玩家为存活真人 T/CT、指定基地实体存活。只搜索该基地配置的 HumanSafeSpawns，检查实际玩家 hull、该基地绿色安全区、附近真实落地支撑和其他真人占位；全部被阻挡则 false，不随机换去另一基地。传送速度归零，禁止复活、FakeClient、观众和死亡玩家。成功返回 true，失败返回 false，不改变积分、金钱或材料；目标位于现有绿区，所以夜间扣血继续使用原安全区判定。

管理员菜单的“回城”额外要求管理员权限，动态列出当前存活基地 A/B/C。菜单执行再次核对管理员、客户端 serial、战役 epoch、配置修订、一次性 nonce 和基地 UID，调用同一安全传送实现。

`HZSH_ShowBaseTeleportMenu` **只做选择**。返回 true 表示接受并显示一个最长 30 秒的选择事务；用户点击某基地后回调该正数 UID，取消、超时或事务失效时回调 0。选择或取消本身都不移动玩家。调用插件卸载或已不处于 Running 状态时不再调用其函数。原玩家断线后 callback 的 client 可能为 0，调用方必须检查 UID 和玩家有效性。

每个客户端最多保留一个选择事务，新请求替换旧请求时先以 UID=0 完成旧事务。事务绑定调用插件、callback、客户端 serial、战役 epoch、配置修订及 nonce；回调前消费并清除旧事务，避免重复点击或 callback 重入重复提交。地图/回合清理、提供方卸载和玩家离线会取消未完成事务。不存在的 callback 应传 `INVALID_FUNCTION`，该参数被拒绝；有效 callback 必须遵守上述三个参数签名。

商店示例：callback 先确认 UID>0、玩家仍有效，再检查余额；调用直接传送接口，只有返回 true 才确认这次购买并扣分，返回 false 不扣分。若商店在异步流程中预留了积分，预留、提交、取消和退款全部由商店自己管理。

```sourcepawn
public void OpenShopBaseSelector(int client, any purchaseContext)
{
    if(GetFeatureStatus(FeatureType_Native,"HZSH_ShowBaseTeleportMenu")!=FeatureStatus_Available
        || GetFeatureStatus(FeatureType_Native,"HZSH_TeleportPlayerToBase")!=FeatureStatus_Available)return;
    // A false return means no selector was accepted; keep the shop transaction uncharged.
    HZSH_ShowBaseTeleportMenu(client,ShopBaseSelected,purchaseContext);
}

public void ShopBaseSelected(int client,int coreUID,any purchaseContext)
{
    if(coreUID<=0 || client<1 || !IsClientInGame(client) || !IsPlayerAlive(client))return;
    // Recheck this purchase's balance/authorization in the shop here.
    // If that check fails, return without calling the provider.
    if(!HZSH_TeleportPlayerToBase(client,coreUID))return;
    // The teleport succeeded. Commit this purchase and deduct points in the shop.
}
```

独立消费者 `tests/ShelterAPIConsumer.sp` 的普通 snapshot 只验证新 native 存在及非法参数拒绝，不移动玩家。显式 `hzsh_api_test travel` 逐一实际传送到每个存活基地 UID，核对速度归零、五种旧材料与 `m_iAccount` 均不变，最后还原原位置/朝向/速度；`hzsh_api_test selector` 打开只选择菜单，真人点击后执行 `hzsh_api_test selector_verify`，按本次请求的计数基线检查恰好一次 callback、返回有效存活基地 UID 或取消 0，且选择本身没有传送。

参数和enum以正式include为准，无效索引、UID或玩家查询返回false。扩大器只追加为原枚举末尾，不改变旧值。旧建筑查询的 core 继续表示原电网/基地语义，私有 buildCore 不冒充旧接口输出。基地索引从0开始；UID只在当前提供方/地图会话内稳定，不是实体索引，不应跨图保存。实例记录还在时可查已毁设施，须检查alive。

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
