#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#undef REQUIRE_PLUGIN
#include <HanZombieShelterAPI>

public Plugin myinfo={name="Shelter API consumer test",author="Codex",description="Independent optional API client",version="1.0",url=""};
int aPassed,aFailed,aSkipped,aPhases,aStates,aRounds,aOutcomes,aPowerChanges;
bool aTravelSelectionPending,aTravelSelectionAccepted;
int aTravelSelectionCalls,aTravelSelectionBaselineCalls,aTravelSelectionContext,aTravelSelectionSerial,aTravelSelectionUID;
float aTravelSelectionPosition[3];
enum struct LegacyInventoryGuard
{
    int before;
    int materials[5];
    int after;
}

void CheckMaterialAPI()
{
    bool available=GetFeatureStatus(FeatureType_Native,"HZSH_GetMaterialCount")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"HZSH_GetMaterialId")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"HZSH_GetPlayerMaterialAmount")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"HZSH_GetBaseMaterialAmount")==FeatureStatus_Available;
    APICheck(available,"optional consumer resolves all four v2 material natives");if(!available)return;
    int count=HZSH_GetMaterialCount();APICheck(count>=7 && count<=64,"v2 material count includes legacy IDs, core and internal currency");
    static const char legacy[][]={"wood","iron","copper","silver","gold"};
    for(int index=0;index<count;index++)
    {
        char id[64];int amount=-1;
        bool valid=HZSH_GetMaterialId(index,id,sizeof(id));
        APICheck(valid && id[0] && HZSH_GetBaseMaterialAmount(0,id,amount) && amount>=0,"v2 enumerated material ID resolves to a nonnegative base amount");
        if(index<5)APICheck(StrEqual(id,legacy[index]),"v1 material enumeration positions remain stable");
    }
    char invalid[64]="sentinel";int amount=123;
    APICheck(!HZSH_GetMaterialId(-1,invalid,sizeof(invalid)) && !invalid[0],"invalid material index clears string output");
    APICheck(!HZSH_GetBaseMaterialAmount(-1,"wood",amount) && amount==0,"invalid core zeroes material amount");
    amount=123;APICheck(!HZSH_GetBaseMaterialAmount(0,"unknown_material",amount) && amount==0,"unknown material ID returns false and zero");
    amount=123;APICheck(!HZSH_GetPlayerMaterialAmount(0,"wood",amount) && amount==0,"invalid client zeroes material amount");
    LegacyInventoryGuard guard;guard.before=0x13579;guard.after=0x24680;
    APICheck(!HZSH_GetPlayerMaterials(0,guard.materials) && guard.before==0x13579 && guard.after==0x24680,"invalid legacy five-cell call preserves adjacent guard cells");
    int client;for(int c=1;c<=MaxClients;c++)if(IsClientInGame(c) && !IsFakeClient(c)){client=c;break;}
    if(client>0)
    {
        APICheck(HZSH_GetPlayerMaterials(client,guard.materials) && guard.before==0x13579 && guard.after==0x24680,"valid v1 five-cell array preserves adjacent guard cells");
        for(int m=0;m<5;m++){amount=-1;APICheck(HZSH_GetPlayerMaterialAmount(client,legacy[m],amount) && amount==guard.materials[m],"v2 per-ID amounts agree with v1 legacy array");}
        amount=-1;APICheck(HZSH_GetPlayerMaterialAmount(client,"plague_core",amount) && amount>=0,"v2 queries added core balance without requiring the player alive");
    }
    else{aSkipped++;PrintToServer("[ShelterAPI] SKIP valid-client five-cell guard requires an online real client; no BOT/fake client created");}
}
void APICheck(bool ok,const char[] text)
{
    if(ok)aPassed++;else aFailed++;
    PrintToServer("[ShelterAPI] %s %s",ok ? "PASS" : "FAIL",text);
}
bool HasShelterAPI(){return LibraryExists("HanZombieShelter") && GetFeatureStatus(FeatureType_Native,"HZSH_GetEnvironmentState")==FeatureStatus_Available;}
void CheckTravelNatives()
{
    bool available=GetFeatureStatus(FeatureType_Native,"HZSH_TeleportPlayerToBase")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"HZSH_ShowBaseTeleportMenu")==FeatureStatus_Available;
    APICheck(available,"optional consumer resolves direct travel and selection-only menu natives");if(!available)return;
    APICheck(!HZSH_TeleportPlayerToBase(0,0) && !HZSH_TeleportPlayerToBase(-1,-1),"travel API rejects invalid clients and invalid base UIDs");
    APICheck(!HZSH_ShowBaseTeleportMenu(0,APITravelSelection,-1),"selection API rejects an invalid client without creating a transaction");
    int client;for(int i=1;i<=MaxClients;i++)if(IsClientInGame(i) && IsPlayerAlive(i) && !IsFakeClient(i)){client=i;break;}
    if(client>0)
    {
        APICheck(!HZSH_ShowBaseTeleportMenu(client,INVALID_FUNCTION,0),"selection API rejects an invalid callback sentinel");
        APICheck(!HZSH_TeleportPlayerToBase(client,-1),"a real client cannot travel to an invalid base UID");
    }
}

void CheckDirectTravel()
{
    int client;for(int i=1;i<=MaxClients;i++)if(IsClientInGame(i) && IsPlayerAlive(i) && !IsFakeClient(i)){client=i;break;}
    if(client<1 || !HZSH_IsRunning()){aSkipped++;PrintToServer("[ShelterAPI] SKIP direct travel requires an existing living real client and running mode");return;}
    float before[3],angles[3],velocity[3],after[3],afterVelocity[3];GetClientAbsOrigin(client,before);GetClientEyeAngles(client,angles);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",velocity);
    int materialsBefore[5],materialsAfter[5],accountBefore=GetEntProp(client,Prop_Send,"m_iAccount"),checked;
    APICheck(HZSH_GetPlayerMaterials(client,materialsBefore),"independent travel consumer captures all five legacy material balances");
    for(int core=0;core<HZSH_GetBaseCount();core++)
    {
        int uid,level,hp,maxHP;bool alive;
        if(!HZSH_GetBaseInfo(core,uid,level,hp,maxHP,alive) || !alive || uid<1)continue;
        bool moved=HZSH_TeleportPlayerToBase(client,uid);GetClientAbsOrigin(client,after);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",afterVelocity);
        APICheck(moved && IsPlayerAlive(client) && !TR_PointOutsideWorld(after) && GetVectorLength(afterVelocity)<0.1,
            "independent consumer actually teleports to this enumerated living base UID with zero velocity");
        bool unchanged=HZSH_GetPlayerMaterials(client,materialsAfter);
        for(int m=0;m<5;m++)if(materialsBefore[m]!=materialsAfter[m])unchanged=false;
        int accountAfter=GetEntProp(client,Prop_Send,"m_iAccount");
        APICheck(unchanged && accountAfter==accountBefore,"this free base transport preserves all five legacy materials and m_iAccount");
        PrintToServer("[ShelterAPITravel] direct core=%d uid=%d success=%d dest=%.3f %.3f %.3f zero_velocity=%d speed=%.6f account=%d materials_unchanged=%d",
            core,uid,moved,after[0],after[1],after[2],GetVectorLength(afterVelocity)<0.1,GetVectorLength(afterVelocity),accountAfter,unchanged);checked++;
    }
    if(checked<1){aSkipped++;PrintToServer("[ShelterAPI] SKIP no living base UID");}
    TeleportEntity(client,before,angles,velocity);
    PrintToServer("[ShelterAPITravel] direct_checked_bases=%d original_transform_restored=1",checked);
}

public void APITravelSelection(int client,int coreUID,any context)
{
    aTravelSelectionCalls++;aTravelSelectionPending=false;
    aTravelSelectionUID=coreUID;
    APICheck(context==aTravelSelectionContext,"selector returns the caller's original transaction context");
    APICheck(aTravelSelectionCalls==aTravelSelectionBaselineCalls+1,"this selector request invokes its callback exactly once");
    bool validUID=coreUID==0;
    for(int core=0;coreUID>0 && core<HZSH_GetBaseCount();core++)
    {
        int uid,level,hp,maxHP;bool alive;
        if(HZSH_GetBaseInfo(core,uid,level,hp,maxHP,alive) && alive && uid==coreUID){validUID=true;break;}
    }
    APICheck(validUID,"selector callback returns zero cancellation or the UID of an enumerated living base");
    APICheck((client==0 && coreUID==0) || (client>0 && IsClientInGame(client) && GetClientSerial(client)==aTravelSelectionSerial),
        "selector callback retains its original real actor or reports a disconnected cancellation");
    if(client>0 && IsClientInGame(client) && GetClientSerial(client)==aTravelSelectionSerial)
    {
        float actual[3];GetClientAbsOrigin(client,actual);
        APICheck(GetVectorDistance(actual,aTravelSelectionPosition)<4.0,"selecting or cancelling a base does not itself teleport the player");
    }
    PrintToServer("[ShelterAPITravel] callback client=%d core_uid=%d context=%d calls=%d; selection_only=1",client,coreUID,context,aTravelSelectionCalls);
}

void StartTravelSelector()
{
    int client;for(int i=1;i<=MaxClients;i++)if(IsClientInGame(i) && IsPlayerAlive(i) && !IsFakeClient(i)){client=i;break;}
    if(client<1){aSkipped++;PrintToServer("[ShelterAPI] SKIP selector requires an existing living real client");return;}
    if(aTravelSelectionPending){APICheck(false,"finish the previous selector request before starting another independent request");return;}
    aTravelSelectionBaselineCalls=aTravelSelectionCalls;aTravelSelectionAccepted=false;aTravelSelectionUID=-1;
    aTravelSelectionContext=73491+aTravelSelectionCalls;aTravelSelectionSerial=GetClientSerial(client);GetClientAbsOrigin(client,aTravelSelectionPosition);
    aTravelSelectionPending=HZSH_ShowBaseTeleportMenu(client,APITravelSelection,aTravelSelectionContext);
    aTravelSelectionAccepted=aTravelSelectionPending;
    APICheck(aTravelSelectionPending,"independent consumer opens a selection-only menu for the real client");
}
public void OnPluginStart(){RegServerCmd("hzsh_api_test",APICommand);}
public Action APICommand(int args)
{
    char which[32];GetCmdArg(1,which,sizeof(which));
    if(StrEqual(which,"selector_verify"))
    {
        APICheck(aTravelSelectionAccepted && !aTravelSelectionPending && aTravelSelectionCalls==aTravelSelectionBaselineCalls+1,
            "this accepted selector request finishes with exactly one callback before verification");
        PrintToServer("[ShelterAPITravel] selector_verify baseline=%d calls=%d pending=%d uid=%d",aTravelSelectionBaselineCalls,aTravelSelectionCalls,aTravelSelectionPending,aTravelSelectionUID);
        aTravelSelectionAccepted=false;return Plugin_Handled;
    }
    if(StrEqual(which,"absent"))
    {APICheck(!HasShelterAPI(),"optional consumer survives provider absence");return Plugin_Handled;}
    APICheck(HasShelterAPI(),"independent consumer resolves provider library and natives");if(!HasShelterAPI())return Plugin_Handled;
    bool enabled;HZSHPhase phase;HZSHEnvironmentPhase environment;int day,revision;
    APICheck(HZSH_GetEnvironmentState(enabled,phase,environment,day,revision),"atomic environment snapshot is ready");
    APICheck(HZSH_GetAPIVersion()==2 && HZSH_GetEnvironmentAPIVersion()==1,"overall API is v2 while environment ABI remains v1");
    APICheck(enabled && revision>0 && day>=1,"snapshot exposes an enabled map, revision and day");
    if(enabled)CheckMaterialAPI();
    if(enabled)CheckTravelNatives();
    if(StrEqual(which,"travel"))CheckDirectTravel();
    if(StrEqual(which,"selector"))StartTravelSelector();
    if(StrEqual(which,"night"))APICheck(phase==HZSHPhase_Night && environment==HZSHEnvironment_Night,"night getter agrees with notification");
    if(StrEqual(which,"day"))APICheck(phase==HZSHPhase_Day && environment==HZSHEnvironment_Day,"day getter agrees with notification");
    if(StrEqual(which,"ending"))APICheck(phase==HZSHPhase_Ending && !HZSH_IsRunning(),"ending snapshot stops active gameplay");
    if(StrEqual(which,"night_ending"))APICheck(phase==HZSHPhase_Ending && environment==HZSHEnvironment_Night,"night settlement preserves the last effective night environment");
    if(HZSH_IsRunning())
    {
        float remaining=HZSH_GetPhaseTimeRemaining();
        APICheck(remaining>=0.0 && (view_as<int>(remaining)&0x7f800000)!=0x7f800000,"phase remaining time is finite and nonnegative for the active server profile");
        APICheck(HZSH_GetBaseCount()==3,"consumer sees the three configured bases");
        int uid,level,hp,maxHP;bool alive;float output,demand,allocated;int offline;
        APICheck(HZSH_GetBaseInfo(0,uid,level,hp,maxHP,alive) && uid>0 && maxHP>=hp,"base info getter resolves a stable UID");
        APICheck(HZSH_GetBasePower(0,output,demand,allocated,offline) && output>=0.0 && demand>=allocated,"power snapshot is coherent");
        HZSHBuildingKind kind;int core;bool powered;float required,generation;
        APICheck(HZSH_GetBuildingInfo(uid,kind,core,level,hp,maxHP,alive,powered,required,generation) && kind==HZSHBuilding_Base && core==0,"building query uses the stable UID");
        APICheck(!HZSH_GetBasePower(-1,output,demand,allocated,offline),"invalid core is rejected without runtime error");
        APICheck(!HZSH_GetBuildingInfo(-1,kind,core,level,hp,maxHP,alive,powered,required,generation),"invalid building UID is rejected");
        int materials[5];APICheck(!HZSH_GetPlayerMaterials(0,materials),"invalid player inventory query is rejected");
    }
    PrintToServer("[ShelterAPISummary] Passed=%d Failed=%d Skipped=%d phase=%d environment=%d day=%d revision=%d phases=%d states=%d rounds=%d outcomes=%d power_changes=%d",
        aPassed,aFailed,aSkipped,phase,environment,day,revision,aPhases,aStates,aRounds,aOutcomes,aPowerChanges);return Plugin_Handled;
}
public void HZSH_OnEnvironmentStateChanged(int revision){aStates++;}
public void HZSH_OnPhaseChanged(HZSHPhase previous,HZSHPhase current,int day,float duration)
{
    aPhases++;bool enabled;HZSHPhase phase;HZSHEnvironmentPhase environment;int currentDay,revision;
    if(HasShelterAPI() && HZSH_GetEnvironmentState(enabled,phase,environment,currentDay,revision))
        APICheck(phase==current && currentDay==day,"phase forward runs after committed snapshot");
}
public void HZSH_OnRoundStarted(int day,bool restored){aRounds++;}
public void HZSH_OnOutcome(bool victory,int previousDay,int nextDay,bool terminal){aOutcomes++;}
public void HZSH_OnBasePowerChanged(int core,float production,float demand,float allocated,int offline){aPowerChanges++;}
