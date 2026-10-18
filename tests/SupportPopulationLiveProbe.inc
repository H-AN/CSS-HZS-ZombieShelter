// Test-only observation of production admission from the existing native pool.
// No spawning, resurrection, BOT/fake-client commands, or config-file writes.
enum SupportLiveStage { SPL_Day, SPL_Night, SPL_Reached, SPL_Failed }
enum struct SupportLiveBody
{
    bool saved;
    NPC npc;
    int hp;
    int damage;
    int solid;
    int leader;
    int leaderSerial;
    int leaderRef;
    MoveType movement;
    RenderMode bodyMode;
    RenderMode visualMode;
    int bodyColor[4];
    int visualColor[4];
    float position[3];
    float angles[3];
    float velocity[3];
    float fallback;
}
SupportLiveBody tSupportLiveBodies[MAX_NPCS];
Building tSupportLiveBuildings[MAX_BUILDINGS];
bool tSupportLiveActive,tSupportLiveExposure,tSupportLiveWarning,tSupportLiveCvarsCaptured,tSupportLiveBootstrap;
int tSupportLiveSerial,tSupportLiveEpoch,tSupportLiveRevision,tSupportLiveDay,tSupportLiveNative,tSupportLiveInitialActive;
int tSupportLiveHP,tSupportLiveArmor,tSupportLiveDamage,tSupportLiveInventory[MATERIALS];
MoveType tSupportLiveMovement;
float tSupportLivePosition[3],tSupportLiveAngles[3],tSupportLiveVelocity[3];
float tSupportLiveStarted,tSupportLiveDeadlineRemaining,tSupportLiveAdmissionsRemaining,tSupportLiveExposureRemaining;
Phase tSupportLivePhase;
HZSHEnvironmentPhase tSupportLiveEnvironment;
SupportLiveStage tSupportLiveStage;
Handle tSupportLiveTimer;
static const char sSupportLiveCvars[][]={"hzs_maxzombiespawn","hzs_playerreborn","hzs_killmoney","hzs_maxmoney","hzs_showdamage","hzs_enablekillicon","hzs_allowbuy"};
ConVar tSupportLiveCvarHandles[7],tSupportLiveSavedCvarHandles[7];
char tSupportLiveActualCvars[7][64],tSupportLiveOriginalCvars[7][64];
bool tSupportLiveTouchedCvars[7];
int tSupportLiveBossRefs[MAX_BOSSES],tSupportLiveBossHP[MAX_BOSSES];
bool tSupportLiveBossAwake[MAX_BOSSES],tSupportLiveBossDefeated[MAX_BOSSES];
float tSupportLiveBossPosition[MAX_BOSSES][3],tSupportLiveBossAngles[MAX_BOSSES][3];

bool SupportLiveSameWorld()
{
    return tSupportLiveActive && gEpoch==tSupportLiveEpoch && gConfigRevision==tSupportLiveRevision;
}

bool SupportLiveBossesIntact()
{
    for(int b=0;b<gBossCount;b++)
    {
        if(gBosses[b].awakened!=tSupportLiveBossAwake[b] || gBosses[b].defeated!=tSupportLiveBossDefeated[b])return false;
        int body=Resolve(tSupportLiveBossRefs[b]);if(body<0 || GetEntProp(body,Prop_Data,"m_iHealth")!=tSupportLiveBossHP[b])return false;
        float position[3],angles[3];GetEntPropVector(body,Prop_Data,"m_vecOrigin",position);GetEntPropVector(body,Prop_Data,"m_angRotation",angles);
        if(GetVectorDistance(position,tSupportLiveBossPosition[b])>0.05 || GetVectorDistance(angles,tSupportLiveBossAngles[b])>0.05)return false;
    }
    return true;
}

void SupportLiveProtect()
{
    float hold=GetGameTime()+600.0;gDeadline=hold;
    if(tSupportLiveStage==SPL_Reached || tSupportLiveStage==SPL_Failed)gNextAdmissions=hold;
    for(int b=0;b<MAX_BUILDINGS;b++)if(tSupportLiveBuildings[b].used && tSupportLiveBuildings[b].alive
        && gBuildings[b].used && gBuildings[b].alive && gBuildings[b].uid==tSupportLiveBuildings[b].uid
        && gBuildings[b].ref==tSupportLiveBuildings[b].ref)
    {
        gBuildings[b].maxHP=10000000;gBuildings[b].hp=10000000;
        gBuildings[b].nextFire=hold;gBuildings[b].nextRestore=hold;
        if(gBuildings[b].kind==Kind_Mine || gBuildings[b].kind==Kind_Fence)
        {gBuildings[b].powerCore=-1;gBuildings[b].gridAssigned=true;}
    }
    for(int n=0;n<MAX_NPCS;n++)if(tSupportLiveBodies[n].saved && gNPCs[n].ref==tSupportLiveBodies[n].npc.ref && !gNPCs[n].dead)
    {
        // Starting a new wall contact can bypass nextAttack; the HP buffer above
        // also protects logical building HP and is renewed once per second.
        gNPCs[n].nextAttack=hold;gNPCs[n].takeDamage=0;
        int body=Resolve(gNPCs[n].ref);if(body>0)SetEntProp(body,Prop_Data,"m_takedamage",0,1);
    }
}

void SupportLiveFail(const char[] reason)
{
    if(tSupportLiveStage==SPL_Failed)return;
    tSupportLiveStage=SPL_Failed;Check(false,reason);
    PrintToServer("[SupportPopulationLive] FAILED elapsed=%.2f; fixture remains protected until explicit cleanup",GetGameTime()-tSupportLiveStarted);
    if(!SupportLiveSameWorld())return;
    for(int n=0;n<MAX_NPCS;n++)if(tSupportLiveBodies[n].saved && gNPCs[n].ref==tSupportLiveBodies[n].npc.ref && !gNPCs[n].dead)
    {gNPCs[n].attackingUID=0;gNPCs[n].paused=true;FreezeNPC(n);}
}

void ObserveSupportPopulationLive(bool verbose)
{
    if(!tSupportLiveActive){if(verbose)PrintToServer("[SupportPopulationLive] INACTIVE");return;}
    if(!SupportLiveSameWorld()){SupportLiveFail("live population fixture crossed its campaign epoch/config identity; old snapshots will not overwrite a new world");return;}
    SupportLiveProtect();int client=GetClientFromSerial(tSupportLiveSerial);
    if(!LiveHuman(client) || IsFakeClient(client) || !Running())SupportLiveFail("live population lost its original living real client or running campaign");
    if(tSupportLiveStage!=SPL_Failed && (!SupportLiveBossesIntact() || Han_GetZombieCount()!=tSupportLiveNative))
        SupportLiveFail("live population changed a fixed Boss or the original native registry size");
    int ordinary,bosses,parked;SupportPopulationBodyCounts(ordinary,bosses,parked);int active=ActiveOrdinary();
    if(tSupportLiveStage!=SPL_Failed && ordinary!=250)SupportLiveFail("live population lost an existing ordinary body; no replacement or resurrection is allowed");
    if(tSupportLiveStage!=SPL_Reached && tSupportLiveStage!=SPL_Failed && GetGameTime()-tSupportLiveStarted>=240.0)
        SupportLiveFail("live admission did not naturally reach both day 100 and night 250 within 240 seconds");
    if(tSupportLiveStage==SPL_Day && gPhase==Phase_Day && active==100)
    {
        Check(true,"the real production world timer naturally reached exactly 100 active ordinary NPCs in daytime");
        PrintToServer("[SupportPopulationLive] DAY_REACHED active=%d ordinary=%d bosses=%d native_registry=%d elapsed=%.2f",active,ordinary,bosses,Han_GetZombieCount(),GetGameTime()-tSupportLiveStarted);
        tSupportLiveStage=SPL_Night;StartNight();gDeadline=GetGameTime()+600.0;
    }
    else if(tSupportLiveStage==SPL_Night && gPhase==Phase_Night && active==250)
    {
        tSupportLiveStage=SPL_Reached;
        Check(SupportLiveBossesIntact() && ordinary==250 && bosses==gBossCount && Han_GetZombieCount()==tSupportLiveNative,
            "the existing native pool naturally reached 250 active ordinary NPCs at night while the independent dormant Boss stayed unchanged");
        PrintToServer("[SupportPopulationLive] NIGHT_REACHED active=%d ordinary=%d bosses=%d native_registry=%d elapsed=%.2f; held until cleanup",active,ordinary,bosses,Han_GetZombieCount(),GetGameTime()-tSupportLiveStarted);
        SupportLiveProtect();
    }
    if(verbose)PrintToServer("[SupportPopulationLive] %s stage=%d phase=%d day=%d active=%d target=%d ordinary=%d bosses=%d parked=%d native_registry=%d elapsed=%.2f protected_fixture=1",
        tSupportLiveStage==SPL_Reached ? "HOLD_REACHED" : (tSupportLiveStage==SPL_Failed ? "FAILED" : "PENDING"),
        view_as<int>(tSupportLiveStage),view_as<int>(gPhase),gDay,active,tSupportLiveStage==SPL_Day ? 100 : 250,
        ordinary,bosses,parked,Han_GetZombieCount(),GetGameTime()-tSupportLiveStarted);
}

public Action SupportPopulationLiveTimer(Handle timer,any serial)
{
    if(timer!=tSupportLiveTimer || !tSupportLiveActive)return Plugin_Stop;
    ObserveSupportPopulationLive(false);
    if(!SupportLiveSameWorld()){tSupportLiveTimer=null;return Plugin_Stop;}
    return Plugin_Continue;
}

void StartSupportPopulationLive(int client)
{
    if(tSupportLiveActive || tV05RealActive || !Running() || client<1 || !LiveHuman(client) || IsFakeClient(client)
        || gNativePoolBootstrap || gTerminal || gOutcomeQueued || !APIAvailable() || GrenadeCount()!=0)
    {tSkipped++;PrintToServer("[SupportPopulationLive] SKIP requires an existing living real client, no other fixture/outcome/bootstrap and no existing grenade");return;}
    int humans;for(int c=1;c<=MaxClients;c++)if(LiveHuman(c) && !IsFakeClient(c))humans++;
    if(humans!=1){tSkipped++;PrintToServer("[SupportPopulationLive] SKIP preserves other living real players");return;}
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && !gNPCs[n].dead && gNPCs[n].burnUntil>GetGameTime())
    {tSkipped++;PrintToServer("[SupportPopulationLive] SKIP existing delayed burn must expire naturally first");return;}
    for(int b=0;b<gBossCount;b++)if(gBosses[b].awakened || gBosses[b].defeated)
    {tSkipped++;PrintToServer("[SupportPopulationLive] SKIP preserves an awakened or defeated Boss");return;}
    int ordinary,bosses,parked;SupportPopulationBodyCounts(ordinary,bosses,parked);
    if(ordinary!=250 || bosses!=gBossCount || Han_GetZombieCount()!=250+gBossCount || DayTarget()!=100 || NightTarget()!=250)
    {tSkipped++;PrintToServer("[SupportPopulationLive] SKIP needs the already-created living 250 pool and loaded day100/night250 support profile");return;}
    tSupportLiveSerial=GetClientSerial(client);tSupportLiveEpoch=gEpoch;tSupportLiveRevision=gConfigRevision;
    tSupportLiveNative=Han_GetZombieCount();tSupportLiveInitialActive=ActiveOrdinary();tSupportLiveStarted=GetGameTime();
    tSupportLiveDay=gDay;tSupportLivePhase=gPhase;tSupportLiveEnvironment=gEnvironmentPhase;tSupportLiveWarning=gWarningSent;
    tSupportLiveExposure=gExposureEnabled;tSupportLiveDeadlineRemaining=gDeadline-tSupportLiveStarted;
    tSupportLiveAdmissionsRemaining=gNextAdmissions-tSupportLiveStarted;tSupportLiveExposureRemaining=gNextExposure-tSupportLiveStarted;
    tSupportLiveCvarsCaptured=gDayCvarsCaptured;tSupportLiveBootstrap=gNativePoolBootstrap;
    for(int i=0;i<sizeof(sSupportLiveCvars);i++)
    {
        tSupportLiveCvarHandles[i]=FindConVar(sSupportLiveCvars[i]);
        if(tSupportLiveCvarHandles[i]!=null)tSupportLiveCvarHandles[i].GetString(tSupportLiveActualCvars[i],sizeof(tSupportLiveActualCvars[]));
        tSupportLiveSavedCvarHandles[i]=gDayCvars[i];tSupportLiveTouchedCvars[i]=gDayCvarTouched[i];
        strcopy(tSupportLiveOriginalCvars[i],sizeof(tSupportLiveOriginalCvars[]),gDayCvarOriginal[i]);
    }
    tSupportLiveHP=GetClientHealth(client);tSupportLiveArmor=GetEntProp(client,Prop_Send,"m_ArmorValue");
    tSupportLiveDamage=GetEntProp(client,Prop_Data,"m_takedamage");tSupportLiveMovement=GetEntityMoveType(client);
    tSupportLiveInventory=gPlayerInventory[client];GetClientAbsOrigin(client,tSupportLivePosition);GetClientEyeAngles(client,tSupportLiveAngles);
    GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",tSupportLiveVelocity);
    for(int b=0;b<MAX_BUILDINGS;b++)tSupportLiveBuildings[b]=gBuildings[b];
    for(int n=0;n<MAX_NPCS;n++)
    {
        SupportLiveBody snapshot;
        int body=Resolve(gNPCs[n].ref);
        if(gNPCs[n].used && gNPCs[n].ready && !gNPCs[n].dead && gNPCs[n].boss<0 && body>0)
        {
            snapshot.saved=true;snapshot.npc=gNPCs[n];snapshot.hp=GetEntProp(body,Prop_Data,"m_iHealth");
            snapshot.damage=GetEntProp(body,Prop_Data,"m_takedamage");snapshot.solid=GetEntProp(body,Prop_Send,"m_usSolidFlags");
            snapshot.movement=GetEntityMoveType(body);snapshot.bodyMode=GetEntityRenderMode(body);snapshot.fallback=gNextTargetFallback[n];
            GetEntityRenderColor(body,snapshot.bodyColor[0],snapshot.bodyColor[1],snapshot.bodyColor[2],snapshot.bodyColor[3]);
            GetEntPropVector(body,Prop_Data,"m_vecOrigin",snapshot.position);GetEntPropVector(body,Prop_Data,"m_angRotation",snapshot.angles);
            if(HasEntProp(body,Prop_Data,"m_vecAbsVelocity"))GetEntPropVector(body,Prop_Data,"m_vecAbsVelocity",snapshot.velocity);
            else if(HasEntProp(body,Prop_Data,"m_vecVelocity"))GetEntPropVector(body,Prop_Data,"m_vecVelocity",snapshot.velocity);
            snapshot.leader=gLeaderOffset>=0 ? GetEntDataEnt2(body,gLeaderOffset) : -1;snapshot.leaderRef=INVALID_ENT_REFERENCE;
            if(snapshot.leader>0 && snapshot.leader<=MaxClients && IsClientInGame(snapshot.leader))snapshot.leaderSerial=GetClientSerial(snapshot.leader);
            else if(snapshot.leader>MaxClients && IsValidEntity(snapshot.leader))snapshot.leaderRef=EntIndexToEntRef(snapshot.leader);
            int visual=Resolve(gNPCs[n].visualRef);
            if(visual>0){snapshot.visualMode=GetEntityRenderMode(visual);GetEntityRenderColor(visual,snapshot.visualColor[0],snapshot.visualColor[1],snapshot.visualColor[2],snapshot.visualColor[3]);}
        }
        tSupportLiveBodies[n]=snapshot;
    }
    for(int b=0;b<gBossCount;b++)
    {
        tSupportLiveBossRefs[b]=INVALID_ENT_REFERENCE;tSupportLiveBossAwake[b]=gBosses[b].awakened;tSupportLiveBossDefeated[b]=gBosses[b].defeated;
        for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].boss==b && !gNPCs[n].dead)
        {
            int body=Resolve(gNPCs[n].ref);if(body<0)continue;tSupportLiveBossRefs[b]=gNPCs[n].ref;
            tSupportLiveBossHP[b]=GetEntProp(body,Prop_Data,"m_iHealth");GetEntPropVector(body,Prop_Data,"m_vecOrigin",tSupportLiveBossPosition[b]);
            GetEntPropVector(body,Prop_Data,"m_angRotation",tSupportLiveBossAngles[b]);break;
        }
    }
    tSupportLiveActive=true;tSupportLiveStage=SPL_Day;gExposureEnabled=false;
    SetEntProp(client,Prop_Data,"m_takedamage",0,1);SetEntityMoveType(client,MOVETYPE_NOCLIP);
    if(gCoreCount>0 && gCores[0].pointCount>0)
    {float camera[3];camera=gCorePoints[0][0];camera[2]+=96.0;TeleportEntity(client,camera,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));}
    Phase previous=gPhase;gPhase=Phase_Day;gEnvironmentPhase=HZSHEnvironment_Day;gWarningSent=false;
    SupportLiveProtect();gNextAdmissions=0.0;ApplyDayCvars();RecalculatePower();NotifyPhaseChanged(previous,600.0);
    tSupportLiveTimer=CreateTimer(1.0,SupportPopulationLiveTimer,tSupportLiveSerial,TIMER_REPEAT|TIMER_FLAG_NO_MAPCHANGE);
    Check(true,"live population fixture starts from existing native bodies and leaves production admission to the regular world timer");
    PrintToServer("[SupportPopulationLive] START native_registry=%d ordinary=%d bosses=%d initial_active=%d day_target=100 night_target=250 deadline_hold=600 timeout=240; protection: client_god/noclip ordinary_damage_disabled building_hp_max=10000000_refill_1s next_attack/fire_delayed mine_fence_grid_disconnected; population admission evidence only, not normal combat difficulty",
        tSupportLiveNative,ordinary,bosses,tSupportLiveInitialActive);
}

void PollSupportPopulationLive(){ObserveSupportPopulationLive(true);}

void CleanupSupportPopulationLive(int client)
{
    if(!tSupportLiveActive)return;
    if(tSupportLiveTimer!=null){delete tSupportLiveTimer;tSupportLiveTimer=null;}
    if(!SupportLiveSameWorld())
    {tSupportLiveActive=false;Check(false,"live population cleanup refuses to overwrite a different campaign/config world");return;}
    bool restored=true;
    for(int n=0;n<MAX_NPCS;n++)if(tSupportLiveBodies[n].saved)
    {
        SupportLiveBody snapshot;snapshot=tSupportLiveBodies[n];int body=Resolve(snapshot.npc.ref);
        if(body<0 || gNPCs[n].ref!=snapshot.npc.ref || gNPCs[n].dead || GetEntProp(body,Prop_Data,"m_iHealth")<1)
        {restored=false;continue;} // Never replace or revive a missing/dead body.
        Han_UnlockZombie(body);gNPCs[n]=snapshot.npc;gNextTargetFallback[n]=snapshot.fallback;
        if(snapshot.npc.paused)FreezeNPC(n);else Han_SetCustomAnimationEx(body,snapshot.npc.idle,0.01);
        SetEntProp(body,Prop_Data,"m_iHealth",snapshot.hp);SetEntProp(body,Prop_Data,"m_takedamage",snapshot.damage,1);
        SetEntProp(body,Prop_Send,"m_usSolidFlags",snapshot.solid);SetEntityMoveType(body,snapshot.movement);
        SetEntityRenderMode(body,snapshot.bodyMode);SetEntityRenderColor(body,snapshot.bodyColor[0],snapshot.bodyColor[1],snapshot.bodyColor[2],snapshot.bodyColor[3]);
        TeleportEntity(body,snapshot.position,snapshot.angles,snapshot.velocity);
        int leader=snapshot.leader;
        if(snapshot.leaderSerial>0 && GetClientFromSerial(snapshot.leaderSerial)!=leader)leader=body;
        else if(snapshot.leaderRef!=INVALID_ENT_REFERENCE && Resolve(snapshot.leaderRef)!=leader)leader=body;
        if(gLeaderOffset>=0 && leader>=0 && (leader==0 || IsValidEntity(leader)))SetEntDataEnt2(body,gLeaderOffset,leader);
        int visual=Resolve(snapshot.npc.visualRef);
        if(visual>0){SetEntityRenderMode(visual,snapshot.visualMode);SetEntityRenderColor(visual,snapshot.visualColor[0],snapshot.visualColor[1],snapshot.visualColor[2],snapshot.visualColor[3]);}
    }
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(gBuildings[b].uid==tSupportLiveBuildings[b].uid && gBuildings[b].ref==tSupportLiveBuildings[b].ref)gBuildings[b]=tSupportLiveBuildings[b];
        else restored=false;
    }
    for(int i=0;i<sizeof(sSupportLiveCvars);i++)
    {
        if(tSupportLiveCvarHandles[i]!=null)tSupportLiveCvarHandles[i].SetString(tSupportLiveActualCvars[i]);
        gDayCvars[i]=tSupportLiveSavedCvarHandles[i];gDayCvarTouched[i]=tSupportLiveTouchedCvars[i];
        strcopy(gDayCvarOriginal[i],sizeof(gDayCvarOriginal[]),tSupportLiveOriginalCvars[i]);
    }
    Phase previous=gPhase;gDay=tSupportLiveDay;gPhase=tSupportLivePhase;gEnvironmentPhase=tSupportLiveEnvironment;
    gDayCvarsCaptured=tSupportLiveCvarsCaptured;gNativePoolBootstrap=tSupportLiveBootstrap;gWarningSent=tSupportLiveWarning;
    gExposureEnabled=tSupportLiveExposure;float now=GetGameTime();
    gDeadline=now+(tSupportLiveDeadlineRemaining>0.0 ? tSupportLiveDeadlineRemaining : 0.0);
    gNextAdmissions=now+(tSupportLiveAdmissionsRemaining>0.0 ? tSupportLiveAdmissionsRemaining : 0.0);
    gNextExposure=now+(tSupportLiveExposureRemaining>0.0 ? tSupportLiveExposureRemaining : 0.0);
    client=GetClientFromSerial(tSupportLiveSerial);
    if(LiveHuman(client))
    {
        gPlayerInventory[client]=tSupportLiveInventory;SetEntProp(client,Prop_Data,"m_takedamage",tSupportLiveDamage,1);
        SetEntityHealth(client,tSupportLiveHP);SetEntProp(client,Prop_Send,"m_ArmorValue",tSupportLiveArmor);SetEntityMoveType(client,tSupportLiveMovement);
        TeleportEntity(client,tSupportLivePosition,tSupportLiveAngles,tSupportLiveVelocity);
    }
    else{restored=false;PrintToServer("[SupportPopulationLive] original client absent/dead; no client was restored, created or respawned");}
    tSupportLiveActive=false;MarkBuildAreasDirty();RecalculatePower();NotifyPhaseChanged(previous,gDeadline-now);
    Check(restored && SupportLiveBossesIntact() && Han_GetZombieCount()==tSupportLiveNative && ActiveOrdinary()==tSupportLiveInitialActive,
        "live population cleanup restores every existing ordinary body, original active count, buildings, player, phase timers and exact runtime cvar snapshots without spawning or reviving");
    PrintToServer("[SupportPopulationLive] CLEANUP restored=%d active=%d original_active=%d native_registry=%d boss_intact=%d entity_creation=0",restored,ActiveOrdinary(),tSupportLiveInitialActive,Han_GetZombieCount(),SupportLiveBossesIntact());
}
