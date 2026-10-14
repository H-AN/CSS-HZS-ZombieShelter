GlobalForward gEnvironmentForward,gPhaseForward,gRoundForward,gOutcomeForward,gPowerForward;
bool gAPIStateReady,gAPIPublished,gPublishedEnabled;
Phase gPublishedPhase;
HZSHEnvironmentPhase gEnvironmentPhase=HZSHEnvironment_Day;
int gAPIRevision,gPublishedDay;
HZSHEnvironmentPhase gPublishedEnvironment;

void RegisterShelterNatives()
{
    CreateNative("HZSH_GetAPIVersion",NativeShelterVersion);
    CreateNative("HZSH_GetEnvironmentAPIVersion",NativeShelterVersion);
    CreateNative("HZSH_GetEnvironmentState",NativeShelterEnvironment);
    CreateNative("HZSH_IsRunning",NativeShelterRunning);
    CreateNative("HZSH_GetPhaseTimeRemaining",NativeShelterTime);
    CreateNative("HZSH_GetBaseCount",NativeShelterBaseCount);
    CreateNative("HZSH_GetBaseInfo",NativeShelterBaseInfo);
    CreateNative("HZSH_GetBasePower",NativeShelterPower);
    CreateNative("HZSH_GetBuildingInfo",NativeShelterBuilding);
    CreateNative("HZSH_GetPlayerMaterials",NativeShelterMaterials);
}

void InitializeShelterForwards()
{
    gEnvironmentForward=new GlobalForward("HZSH_OnEnvironmentStateChanged",ET_Ignore,Param_Cell);
    gPhaseForward=new GlobalForward("HZSH_OnPhaseChanged",ET_Ignore,Param_Cell,Param_Cell,Param_Cell,Param_Float);
    gRoundForward=new GlobalForward("HZSH_OnRoundStarted",ET_Ignore,Param_Cell,Param_Cell);
    gOutcomeForward=new GlobalForward("HZSH_OnOutcome",ET_Ignore,Param_Cell,Param_Cell,Param_Cell,Param_Cell);
    gPowerForward=new GlobalForward("HZSH_OnBasePowerChanged",ET_Ignore,Param_Cell,Param_Float,Param_Float,Param_Float,Param_Cell);
}

void PublishShelterState(bool resetEnvironment=false)
{
    if(resetEnvironment)gEnvironmentPhase=HZSHEnvironment_Day;
    else if(gPhase==Phase_Day)gEnvironmentPhase=HZSHEnvironment_Day;
    else if(gPhase==Phase_Night)gEnvironmentPhase=HZSHEnvironment_Night;
    bool enabled=gEnabled && gCoreReady;
    if(!gAPIStateReady || (gAPIPublished && enabled==gPublishedEnabled && gPhase==gPublishedPhase
        && gDay==gPublishedDay && gEnvironmentPhase==gPublishedEnvironment))return;
    gAPIPublished=true;gPublishedEnabled=enabled;gPublishedPhase=gPhase;gPublishedDay=gDay;gPublishedEnvironment=gEnvironmentPhase;
    gAPIRevision=gAPIRevision==2147483647 ? 1 : gAPIRevision+1;
    if(gEnvironmentForward!=null){Call_StartForward(gEnvironmentForward);Call_PushCell(gAPIRevision);Call_Finish();}
}

void NotifyPhaseChanged(Phase previous,float duration)
{
    PublishShelterState();
    Call_StartForward(gPhaseForward);Call_PushCell(previous);Call_PushCell(gPhase);Call_PushCell(gDay);Call_PushFloat(duration);Call_Finish();
}

void NotifyPowerChanged(int core)
{
    if(gPowerForward==null)return;
    Call_StartForward(gPowerForward);Call_PushCell(core);Call_PushFloat(gGridProduction[core]);
    Call_PushFloat(gGridDemand[core]);Call_PushFloat(gGridAllocated[core]);Call_PushCell(gGridOffline[core]);Call_Finish();
}

public int NativeShelterVersion(Handle plugin,int params){return HZSH_API_VERSION;}
public int NativeShelterRunning(Handle plugin,int params){return Running();}
public int NativeShelterTime(Handle plugin,int params)
{
    float remaining=Running() ? gDeadline-GetGameTime() : 0.0;if(remaining<0.0)remaining=0.0;return view_as<int>(remaining);
}
public int NativeShelterBaseCount(Handle plugin,int params){return gEnabled ? gCoreCount : 0;}
public int NativeShelterEnvironment(Handle plugin,int params)
{
    SetNativeCellRef(1,gEnabled && gCoreReady);SetNativeCellRef(2,gPhase);SetNativeCellRef(3,gEnvironmentPhase);
    SetNativeCellRef(4,gDay);SetNativeCellRef(5,gAPIRevision);return gAPIStateReady;
}
bool APIValidCore(int core){return gEnabled && core>=0 && core<gCoreCount;}
public int NativeShelterBaseInfo(Handle plugin,int params)
{
    int core=GetNativeCell(1);if(!APIValidCore(core))return false;
    int b=gCores[core].building;if(b<0 || !gBuildings[b].used)return false;
    SetNativeCellRef(2,gBuildings[b].uid);SetNativeCellRef(3,gBuildings[b].level);SetNativeCellRef(4,gBuildings[b].hp);
    SetNativeCellRef(5,gBuildings[b].maxHP);SetNativeCellRef(6,gBuildings[b].alive);return true;
}
public int NativeShelterPower(Handle plugin,int params)
{
    int core=GetNativeCell(1);if(!APIValidCore(core))return false;
    SetNativeCellRef(2,view_as<int>(gGridProduction[core]));SetNativeCellRef(3,view_as<int>(gGridDemand[core]));
    SetNativeCellRef(4,view_as<int>(gGridAllocated[core]));SetNativeCellRef(5,gGridOffline[core]);return true;
}
public int NativeShelterBuilding(Handle plugin,int params)
{
    int uid=GetNativeCell(1),b=-1;if(!gEnabled || uid<1)return false;
    for(int i=0;i<MAX_BUILDINGS;i++)if(gBuildings[i].used && gBuildings[i].uid==uid){b=i;break;}
    if(b<0)return false;
    SetNativeCellRef(2,gBuildings[b].kind);SetNativeCellRef(3,gBuildings[b].kind==Kind_Base ? gBuildings[b].core : gBuildings[b].powerCore);
    SetNativeCellRef(4,gBuildings[b].level);SetNativeCellRef(5,gBuildings[b].hp);SetNativeCellRef(6,gBuildings[b].maxHP);
    SetNativeCellRef(7,gBuildings[b].alive);SetNativeCellRef(8,BuildingPowered(b));
    SetNativeCellRef(9,view_as<int>(gBuildings[b].powerDemand));SetNativeCellRef(10,view_as<int>(GeneratorOutput(b)));return true;
}
public int NativeShelterMaterials(Handle plugin,int params)
{
    int client=GetNativeCell(1);if(!gEnabled || client<1 || client>MaxClients || !IsClientInGame(client))return false;
    SetNativeArray(2,gPlayerInventory[client],MATERIALS);return true;
}
