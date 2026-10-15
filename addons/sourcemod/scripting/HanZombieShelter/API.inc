GlobalForward gEnvironmentForward,gPhaseForward,gRoundForward,gOutcomeForward,gPowerForward;
bool gAPIStateReady,gAPIPublished,gPublishedEnabled;
Phase gPublishedPhase;
HZSHEnvironmentPhase gEnvironmentPhase=HZSHEnvironment_Day;
int gAPIRevision,gPublishedDay;
HZSHEnvironmentPhase gPublishedEnvironment;
Handle gBaseTeleportCaller[MAXPLAYERS+1];
Function gBaseTeleportCallback[MAXPLAYERS+1];
Menu gBaseTeleportMenu[MAXPLAYERS+1];
int gBaseTeleportSerial[MAXPLAYERS+1],gBaseTeleportEpoch[MAXPLAYERS+1],gBaseTeleportRevision[MAXPLAYERS+1];
int gBaseTeleportNonce[MAXPLAYERS+1],gBaseTeleportContext[MAXPLAYERS+1];
bool gBaseTeleportOpening[MAXPLAYERS+1];
bool gBaseTeleportClosing;

void RegisterShelterNatives()
{
    CreateNative("HZSH_GetAPIVersion",NativeShelterVersion);
    CreateNative("HZSH_GetEnvironmentAPIVersion",NativeShelterEnvironmentVersion);
    CreateNative("HZSH_GetEnvironmentState",NativeShelterEnvironment);
    CreateNative("HZSH_IsRunning",NativeShelterRunning);
    CreateNative("HZSH_GetPhaseTimeRemaining",NativeShelterTime);
    CreateNative("HZSH_GetBaseCount",NativeShelterBaseCount);
    CreateNative("HZSH_GetBaseInfo",NativeShelterBaseInfo);
    CreateNative("HZSH_TeleportPlayerToBase",NativeShelterTeleportPlayer);
    CreateNative("HZSH_ShowBaseTeleportMenu",NativeShelterShowBaseTeleportMenu);
    CreateNative("HZSH_GetBasePower",NativeShelterPower);
    CreateNative("HZSH_GetBuildingInfo",NativeShelterBuilding);
    CreateNative("HZSH_GetPlayerMaterials",NativeShelterMaterials);
    CreateNative("HZSH_GetMaterialCount",NativeShelterMaterialCount);
    CreateNative("HZSH_GetMaterialId",NativeShelterMaterialId);
    CreateNative("HZSH_GetPlayerMaterialAmount",NativeShelterPlayerMaterial);
    CreateNative("HZSH_GetBaseMaterialAmount",NativeShelterBaseMaterial);
}

bool BaseTeleportSelectorActor(int client)
{
    return LiveHuman(client) && !IsFakeClient(client) && !IsClientSourceTV(client) && !IsClientReplay(client) && Running();
}

void FinishBaseTeleportSelection(int client,int uid,bool notify=true,bool closeMenu=false)
{
    if(client<1 || client>MaxClients || gBaseTeleportCaller[client]==null)return;
    Handle caller=gBaseTeleportCaller[client];Function callback=gBaseTeleportCallback[client];
    Menu previous=gBaseTeleportMenu[client];
    int serial=gBaseTeleportSerial[client],context=gBaseTeleportContext[client];
    // Consume all identity before user code can reenter this provider.
    gBaseTeleportCaller[client]=null;gBaseTeleportCallback[client]=INVALID_FUNCTION;gBaseTeleportMenu[client]=null;
    gBaseTeleportSerial[client]=0;gBaseTeleportEpoch[client]=0;gBaseTeleportRevision[client]=0;gBaseTeleportContext[client]=0;
    gBaseTeleportNonce[client]++;
    if(closeMenu && previous!=null && IsValidHandle(previous) && IsClientInGame(client))CancelClientMenu(client);
    if(!notify || caller==null || !IsValidHandle(caller) || GetPluginStatus(caller)!=Plugin_Running || callback==INVALID_FUNCTION)return;
    Call_StartFunction(caller,callback);Call_PushCell(GetClientFromSerial(serial));Call_PushCell(uid);Call_PushCell(context);
    int error=Call_Finish();if(error!=0)LogError("[Shelter] base teleport selection callback failed with error %d",error);
}

void CancelBaseTeleportSelection(int client)
{
    if(client<1 || client>MaxClients || gBaseTeleportCaller[client]==null)return;
    FinishBaseTeleportSelection(client,0,true,true);
}

void CancelAllBaseTeleportSelections()
{
    bool previous=gBaseTeleportClosing;gBaseTeleportClosing=true;
    for(int client=1;client<=MaxClients;client++)CancelBaseTeleportSelection(client);
    gBaseTeleportClosing=previous;
}

void InvalidateBaseTeleportSelections()
{
    for(int client=1;client<=MaxClients;client++)if(gBaseTeleportCaller[client]!=null)
    {
        Handle caller=gBaseTeleportCaller[client];
        if(!IsValidHandle(caller) || GetPluginStatus(caller)!=Plugin_Running || !BaseTeleportSelectorActor(client)
            || GetClientSerial(client)!=gBaseTeleportSerial[client] || gBaseTeleportEpoch[client]!=gEpoch
            || gBaseTeleportRevision[client]!=gConfigRevision)CancelBaseTeleportSelection(client);
    }
}

public int NativeShelterShowBaseTeleportMenu(Handle plugin,int params)
{
    if(params<2 || params>3 || plugin==null || !IsValidHandle(plugin) || GetPluginStatus(plugin)!=Plugin_Running)return false;
    int client=GetNativeCell(1);Function callback=GetNativeFunction(2);
    if(!BaseTeleportSelectorActor(client) || callback==INVALID_FUNCTION || gBaseTeleportOpening[client] || gBaseTeleportClosing)return false;
    int count;for(int core=0;core<gCoreCount;core++)if(TravelBaseAlive(core))count++;
    if(count==0)return false;
    gBaseTeleportOpening[client]=true;CancelBaseTeleportSelection(client);
    if(!BaseTeleportSelectorActor(client)){gBaseTeleportOpening[client]=false;return false;}
    gBaseTeleportNonce[client]++;gBaseTeleportCaller[client]=plugin;gBaseTeleportCallback[client]=callback;
    gBaseTeleportContext[client]=params==3 ? GetNativeCell(3) : 0;gBaseTeleportSerial[client]=GetClientSerial(client);
    gBaseTeleportEpoch[client]=gEpoch;gBaseTeleportRevision[client]=gConfigRevision;
    Menu menu=new Menu(BaseTeleportSelectionHandler);gBaseTeleportMenu[client]=menu;
    char title[192];TranslateLabel(client,"BaseTeleportChoiceTitle",title,sizeof(title));menu.SetTitle("%s",title);
    for(int core=0;core<gCoreCount;core++)if(TravelBaseAlive(core))
    {
        char token[64],name[64];CoreDisplayName(core,name,sizeof(name),client);
        Format(token,sizeof(token),"%d:%d",gBaseTeleportNonce[client],gBuildings[gCores[core].building].uid);menu.AddItem(token,name);
    }
    bool shown=menu.Display(client,30);
    if(!shown)
    {
        FinishBaseTeleportSelection(client,0,false);
        // A failed display may never reach MenuAction_End. Release any menu
        // still owned here without deleting one its handler already ended.
        if(IsValidHandle(menu))delete menu;
    }
    gBaseTeleportOpening[client]=false;return shown;
}

public int BaseTeleportSelectionHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End){delete menu;return 0;}
    if(client<1 || client>MaxClients || gBaseTeleportMenu[client]!=menu)return 0;
    if(action==MenuAction_Cancel){FinishBaseTeleportSelection(client,0);return 0;}
    if(action!=MenuAction_Select)return 0;
    char token[64],parts[3][24];int nonce,uid;menu.GetItem(item,token,sizeof(token));
    bool valid=ExplodeString(token,":",parts,3,24)==2 && ParseLevelInteger(parts[0],nonce) && ParseLevelInteger(parts[1],uid)
        && nonce==gBaseTeleportNonce[client] && BaseTeleportSelectorActor(client) && GetClientSerial(client)==gBaseTeleportSerial[client]
        && gEpoch==gBaseTeleportEpoch[client] && gConfigRevision==gBaseTeleportRevision[client];
    bool alive;
    if(valid)for(int core=0;core<gCoreCount;core++)if(TravelBaseAlive(core) && gBuildings[gCores[core].building].uid==uid){alive=true;break;}
    FinishBaseTeleportSelection(client,valid && alive ? uid : 0);return 0;
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
    InvalidateBaseTeleportSelections();
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
public int NativeShelterEnvironmentVersion(Handle plugin,int params){return HZSH_ENVIRONMENT_API_VERSION;}
public int NativeShelterRunning(Handle plugin,int params){return Running();}
public int NativeShelterTime(Handle plugin,int params)
{
    float remaining=Running() ? gDeadline-GetGameTime() : 0.0;if(remaining<0.0)remaining=0.0;return view_as<int>(remaining);
}
public int NativeShelterBaseCount(Handle plugin,int params){return gEnabled ? gCoreCount : 0;}
public int NativeShelterTeleportPlayer(Handle plugin,int params)
{
    if(params!=2)return false;int client=GetNativeCell(1),uid=GetNativeCell(2);
    if(uid<=0 || !gEnabled || !gCoreReady)return false;
    for(int core=0;core<gCoreCount;core++)
    {
        int b=gCores[core].building;
        if(b>=0 && b<MAX_BUILDINGS && gBuildings[b].used && gBuildings[b].uid==uid)return TeleportPlayerToBase(client,core);
    }
    return false;
}
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
    // ABI v1 callers allocate exactly five cells, forever.
    SetNativeArray(2,gPlayerInventory[client],LEGACY_MATERIALS);return true;
}

public int NativeShelterMaterialCount(Handle plugin,int params){return gEnabled && gCoreReady ? gMaterialCount : 0;}
public int NativeShelterMaterialId(Handle plugin,int params)
{
    int index=GetNativeCell(1),length=GetNativeCell(3);
    if(length<1)return false;
    SetNativeString(2,"",length);
    if(!gEnabled || !gCoreReady || index<0 || index>=gMaterialCount || length<=strlen(gMaterials[index].id))return false;
    SetNativeString(2,gMaterials[index].id,length);return true;
}
public int NativeShelterPlayerMaterial(Handle plugin,int params)
{
    SetNativeCellRef(3,0);int client=GetNativeCell(1);char id[64];GetNativeString(2,id,sizeof(id));int material=FindMaterial(id);
    if(!gEnabled || !gCoreReady || client<1 || client>MaxClients || !IsClientInGame(client) || material<0)return false;
    SetNativeCellRef(3,gPlayerInventory[client][material]);return true;
}
public int NativeShelterBaseMaterial(Handle plugin,int params)
{
    SetNativeCellRef(3,0);int core=GetNativeCell(1);char id[64];GetNativeString(2,id,sizeof(id));int material=FindMaterial(id);
    if(!gCoreReady || !APIValidCore(core) || material<0)return false;
    SetNativeCellRef(3,gBaseInventory[core][material]);return true;
}
