#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#include <sdkhooks>
#include <cstrike>
#include <nextmap>
#include <HanZombieScenarioAPI>
#define HZSH_API_PROVIDER
#include <HanZombieShelterAPI>
#include "HanZombieShelter/Types.inc"
#include "HanZombieShelter/Levels.inc"
#include "HanZombieShelter/Progression.inc"
#include "HanZombieShelter/Config.inc"
#include "HanZombieShelter/DayCvars.inc"
#include "HanZombieShelter/Geometry.inc"
#include "HanZombieShelter/MapAdjustments.inc"
#include "HanZombieShelter/BuildAreas.inc"
#include "HanZombieShelter/Inventory.inc"
#include "HanZombieShelter/Drops.inc"
#include "HanZombieShelter/Management.inc"
#include "HanZombieShelter/EntityBudget.inc"
#include "HanZombieShelter/Audio.inc"
#include "HanZombieShelter/Power.inc"
#include "HanZombieShelter/Restoration.inc"
#include "HanZombieShelter/Beacon.inc"
#include "HanZombieShelter/API.inc"
#include "HanZombieShelter/World.inc"
#include "HanZombieShelter/NPC.inc"
#include "HanZombieShelter/Combat.inc"
#include "HanZombieShelter/Grenades.inc"
#include "HanZombieShelter/HUD.inc"
#include "HanZombieShelter/Lifecycle.inc"
#include "HanZombieShelter/Commands.inc"
#include "HanZombieShelter/Admin.inc"

public Plugin myinfo={name="HZS Zombie Shelter",author="H-AN",description="External cooperative day/night shelter mode; no core source changes",version=SHELTER_VERSION,url=""};

public APLRes AskPluginLoad2(Handle myself,bool late,char[] error,int maxlength)
{
    MarkNativeAsOptional("Han_IsZombie");MarkNativeAsOptional("Han_GetZombieName");MarkNativeAsOptional("Han_GetZombieCount");MarkNativeAsOptional("Han_GetZombieByIndex");
    MarkNativeAsOptional("Han_SetZombieTarget");MarkNativeAsOptional("Han_UnlockZombie");MarkNativeAsOptional("Han_SetCustomAnimationEx");MarkNativeAsOptional("Han_SafeDamageZombie");
    RegisterShelterNatives();RegPluginLibrary("HanZombieShelter");return APLRes_Success;
}

public void OnPluginStart()
{
    LoadTranslations("HanZombieShelter.phrases");
    LoadTranslations("common.phrases");
    InitializeShelterForwards();
    ResetInventories();
    HookEvent("round_start",RoundStart);HookEvent("round_end",RoundEnd);HookEvent("player_spawn",PlayerSpawn);
    RegConsoleCmd("sm_build",BuildMenuCommand);RegConsoleCmd("sm_materials",MaterialsCommand);
    RegConsoleCmd("sm_zsh_status",StatusCommand);
    RegAdminCmd("sm_zsh_phase",PhaseCommand,ADMFLAG_ROOT);RegAdminCmd("sm_zsh_day",DayCommand,ADMFLAG_ROOT);
    RegAdminCmd("sm_zsh_grant",GrantCommand,ADMFLAG_ROOT);RegAdminCmd("sm_zsh_fail",FailCommand,ADMFLAG_ROOT);
    RegAdminCmd("sm_zsh_reload",ReloadCommand,ADMFLAG_ROOT);
    RegAdminCmd("sm_zsh_defense_trace",DefenseTraceCommand,ADMFLAG_ROOT);
    RegisterShelterAdminCommands();
    for(int i=1;i<=MaxClients;i++)if(IsClientInGame(i))OnClientPutInServer(i);
    gTick=CreateTimer(0.25,WorldTick,_,TIMER_REPEAT);
    gDefenseTimer=CreateTimer(0.1,DefenseTimer,_,TIMER_REPEAT);
    gProjectileTimer=CreateTimer(0.05,GrenadeTimer,_,TIMER_REPEAT);
}

public void OnMapStart()
{
    ClearNPCDropCaches();ClearDefenseState();MarkBuildAreasDirty();
    gEpoch++;gEnabled=false;gCoreReady=false;gRoundPrepared=false;gSnapshot=false;gRollback=false;gTerminal=false;
    gPhase=Phase_Waiting;gDay=1;gNativeKills=0;gUID=0;gOutcomeQueued=false;gTerminationExpected=false;
    ResetInventories();
    for(int i=0;i<MAX_BUILDINGS;i++){gBuildings[i].used=false;gBuildings[i].alive=false;gBuildings[i].ref=INVALID_ENT_REFERENCE;}
    for(int i=0;i<MAX_NPCS;i++){gNPCs[i].used=false;gNPCs[i].ref=INVALID_ENT_REFERENCE;}
    for(int i=0;i<sizeof(gNPCBinding);i++){gNPCBinding[i]=0;gWorldBinding[i]=0;}
    gLeaderOffset=FindSendPropInfo("CHostage","m_leader");
    gBeamSprite=PrecacheModel("materials/sprites/laserbeam.vmt",true);gFireSprite=PrecacheModel("materials/sprites/fire1.vmt",true);
    gExplosionSprite=PrecacheModel("sprites/zerogxplode.spr",true);
    gPreviousTick=GetGameTime();gNextHUD=0.0;gNextAdmissions=0.0;
    ResetPowerState();gAPIStateReady=true;PublishShelterState(true);
}

public void OnConfigsExecuted()
{
    BootMode();
    PublishShelterState();
}

void BootMode()
{
    if(gSnapshot)return; // One immutable configuration revision per campaign.
    gEnabled=LoadConfigs();gCoreReady=APIAvailable();
    if(!gEnabled)return;
    if(!gCoreReady){LogError("[Shelter] HZS natives unavailable; load HZS and reload this map before play");gEnabled=false;return;}
    if(gLeaderOffset<0){LogError("[Shelter] CHostage leader unavailable; mode cannot consume the core target");gEnabled=false;return;}
    LoadHanData();LoadInteractionSounds();OverrideCorePool();OverrideServerPolicy();PrepareRound();
    PrintToServer("[Shelter] configuration loaded map=%s cores=%d resources=%d bosses=%d native_pool=%d",gMap,gCoreCount,gNodeCount,gBossCount,gNativePoolSize);
}

public void OnClientPutInServer(int client)
{
    ResetBeaconClient(client);
    StartClientInventory(client);
    gLastButtons[client]=0;gNextGather[client]=0.0;gNextRepair[client]=0.0;gNextUse[client]=0.0;
    SDKHook(client,SDKHook_OnTakeDamage,HumanDamageHook);
}

public void OnClientDisconnect(int client)
{
    ResetBeaconClient(client);
    CancelBaseTeleportSelection(client);
    SaveClientInventory(client);
    if(!gEnabled || !APIAvailable())return;
    int serial=GetClientSerial(client);
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!gNPCs[n].used || !gNPCs[n].ready || gNPCs[n].dead || !gNPCs[n].active)continue;
        ClearDisconnectedNPCTarget(n,client,serial);
    }
}

public void OnClientAuthorized(int client,const char[] auth)
{
    if(IsClientInGame(client))BindClientInventory(client);
}

public void OnEntityDestroyed(int entity)
{
    if(entity<=MaxClients || entity>=sizeof(gNPCBinding))return;
    int npc=gNPCBinding[entity]-1;
    if(npc>=0 && gNPCs[npc].used && gNPCs[npc].ref==gNPCBindingRef[entity])
    {
        ReleaseNPCDropCache(npc);ClearNPCRewardContext(npc);
        gNPCs[npc].used=false;gNPCs[npc].ref=INVALID_ENT_REFERENCE;
        // Body lifetime owns its associated ornament. Defer cleanup until HZS returns.
        if(gNPCs[npc].visualRef!=INVALID_ENT_REFERENCE)RequestFrame(CleanOrphan,gNPCs[npc].visualRef);
    }
    gNPCBinding[entity]=0;
    int binding=gWorldBinding[entity];
    if(binding>0)
    {
        int b=binding-1;
        if(gBuildings[b].ref==gWorldBindingRef[entity])
        {
            gBuildings[b].ref=INVALID_ENT_REFERENCE;
            if(!gRebuilding && Running())DamageBuilding(b,gBuildings[b].hp);
        }
    }
    else if(binding<0)
    {
        int n=-binding-1;if(gNodes[n].ref==gWorldBindingRef[entity])gNodes[n].ref=INVALID_ENT_REFERENCE;
    }
    gWorldBinding[entity]=0;
}

public void CleanOrphan(any ref)
{
    int entity=Resolve(ref);if(entity<0)return;
    char classname[64];GetEntityClassname(entity,classname,sizeof(classname));
    if(!StrEqual(classname,"prop_dynamic_ornament"))return;
    // Another module may have adopted the ornament. Never remove a still-parented visual.
    if(HasEntProp(entity,Prop_Send,"m_hMoveParent"))
    {
        int parent=GetEntPropEnt(entity,Prop_Send,"m_hMoveParent");if(parent>0 && IsValidEntity(parent))return;
    }
    RemoveEntity(entity);
}

public void OnMapEnd()
{
    CancelAllBaseTeleportSelections();
    ClearNPCDropCaches();
    gEnabled=false;gEpoch++;CleanWorld();RestoreCorePool();RestoreServerPolicy();gRoundPrepared=false;PublishShelterState();
}

public void OnPluginEnd()
{
    CancelAllBaseTeleportSelections();
    ClearNPCDropCaches();
    gEpoch++;
    if(gCoreReady && APIAvailable())for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used)RestoreNPCOnUnload(i);
    gEnabled=false;CleanWorld();PublishShelterState();RestoreCorePool();RestoreServerPolicy();ReleaseConfigs();delete gHanData;delete gTick;delete gProjectileTimer;delete gDefenseTimer;delete gSharedOverflow;
}

public void OnLibraryRemoved(const char[] name)
{
    if(!APIAvailable()){gCoreReady=false;StopPowerEffects();ClearGrenades();PublishShelterState();}
}
