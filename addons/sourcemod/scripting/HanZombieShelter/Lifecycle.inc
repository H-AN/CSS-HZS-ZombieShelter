void OverrideCorePool()
{
    if(!gEnabled)return;
    gCorePoolCvar=FindConVar("hzs_maxzombiespawn");
    if(gCorePoolCvar==null)return;
    CaptureDayCvars();gNativePoolBootstrap=true;
    if(!gPoolOverridden){gOriginalPool=gCorePoolCvar.IntValue;gPoolOverridden=true;}
    gCorePoolCvar.IntValue=gNativePoolSize;
}

void OverrideServerPolicy()
{
    if(gPolicyOverridden || !gEnabled)return;
    static const char names[][]={"mp_timelimit","mp_maxrounds","mp_winlimit","mp_limitteams","mp_autoteambalance","mp_friendlyfire","mp_ignore_round_win_conditions"};
    for(int i=0;i<sizeof(names);i++)
    {
        gPolicyCVars[i]=FindConVar(names[i]);if(gPolicyCVars[i]==null)continue;
        gPolicyCVars[i].GetString(gPolicyOriginal[i],sizeof(gPolicyOriginal[]));
        char value[64];Text(C_Mode,"ServerOverrides",names[i],value,sizeof(value),i==6 ? "1" : "0");
        gPolicyCVars[i].SetString(value);
    }
    gPolicyOverridden=true;
}

void RestoreServerPolicy()
{
    if(!gPolicyOverridden)return;
    for(int i=0;i<sizeof(gPolicyCVars);i++)if(gPolicyCVars[i]!=null)gPolicyCVars[i].SetString(gPolicyOriginal[i]);
    gPolicyOverridden=false;
}

void RestoreCorePool()
{
    RestoreDayCvars();
    if(gPoolOverridden && gCorePoolCvar!=null)gCorePoolCvar.IntValue=gOriginalPool;
    gPoolOverridden=false;
}

// A new engine round recreates native Boss bodies. Resume campaign progress,
// but let every surviving Boss begin dormant at its configured position.
void ResetBossRoundState()
{
    for(int i=0;i<gBossCount;i++)
    {
        gBosses[i].boundThisRound=false;
        gBosses[i].awakened=false;
        gBosses[i].positionedHP=0;
    }
}

void PrepareRound()
{
    if(!gEnabled || gTerminal)return;
    Phase previous=gPhase;
    CancelAllBaseTeleportSelections();
    gEpoch++;gRoundPrepared=false;gPhase=Phase_Waiting;gEnvironmentPhase=HZSHEnvironment_Day;gWarningSent=false;
    gFailureCandidate=false;gVictoryCandidate=false;gOutcomeQueued=false;gTerminationExpected=false;
    gVictoryReason=Victory_None;
    CleanWorld();ApplyMapAdjustments();
    for(int i=0;i<MAX_NPCS;i++){gNPCs[i].used=false;gNPCs[i].ref=INVALID_ENT_REFERENCE;}
    ResetBossRoundState();
    gRoundRestored=gSnapshot;
    if(!CreateWorld(gSnapshot))
    {
        CleanWorld();gEnabled=false;RestoreCorePool();RestoreServerPolicy();PublishShelterState(true);LogError("[Shelter] World creation failed; mode left disabled, no partial session");return;
    }
    gSnapshot=true;gRoundPrepared=true;gRollback=false;OverrideCorePool();
    PublishShelterState(true);NotifyPhaseChanged(previous,0.0);
}

void StartDay(bool advance)
{
    if(!gEnabled || gTerminal || !gRoundPrepared)return;
    Phase previous=gPhase;
    if(advance && gDay<2147483647)gDay++;
    SkillsObserveDay();
    gPhase=Phase_Day;gEnvironmentPhase=HZSHEnvironment_Day;gDeadline=GetGameTime()+gDaySeconds;gWarningSent=false;gNextAdmissions=0.0;
    ApplyDayCvars();
    TrimOrdinary(DayTarget());
    AnnouncePhrase("DayStarted",gDay,RoundToNearest(gDaySeconds));
    PowerTick();NotifyPhaseChanged(previous,gDaySeconds);
}

void StartNight()
{
    if(!Running() || gTerminal)return;
    Phase previous=gPhase;
    gPhase=Phase_Night;gEnvironmentPhase=HZSHEnvironment_Night;gDeadline=GetGameTime()+gNightSeconds;gNextAdmissions=0.0;
    ApplyDayCvars();TrimOrdinary(NightTarget());
    gNextExposure=GetGameTime()+gExposureInterval;
    AnnouncePhrase("NightStarted",gDay,gExposureInterval,gExposureDamage);
    if(previous!=Phase_Night)PlayGlobalInteractionSound("night_start");
    PowerTick();NotifyPhaseChanged(previous,gNightSeconds);
}

public void Han_OnGameStart()
{
    if(!gEnabled || gTerminal)return;
    gCoreReady=APIAvailable();if(!gCoreReady)return;
    if(!gRoundPrepared)PrepareRound();
    if(!gRoundPrepared)return;
    bool restored=gRoundRestored;
    OverrideCorePool();StartDay(false);gNextBossAudit=GetGameTime()+3.0;
    Call_StartForward(gRoundForward);Call_PushCell(gDay);Call_PushCell(restored);Call_Finish();
    RequestFrame(FinishNativePoolBootstrap,gEpoch);
}

public void Han_OnGameEnd()
{
    if(!gEnabled || gTerminal || gFailureCandidate || gVictoryCandidate || gPhase==Phase_Ending)return;
    // A lifecycle notification without a result is not a loss.
    Phase previous=gPhase;gPhase=Phase_Waiting;ClearGrenades();StopPowerEffects();NotifyPhaseChanged(previous,0.0);
}

public void Han_OnZombieWin()
{
    if(!gEnabled || gTerminal || gPhase==Phase_Ending)return;
    gTerminationExpected=true;QueueOutcome(false);
}

public void Han_OnHumanWin()
{
    if(!gEnabled || gTerminal)return;
    if(AllBossesDefeated())QueueOutcome(true);
    else LogError("[Shelter] Core Stage completed before all configured Bosses were defeated; 99999 bridge/configuration needs review");
}

void QueueOutcome(bool victory,VictoryReason reason=Victory_Bosses)
{
    if(!gEnabled || gTerminal || gPhase==Phase_Ending)return;
    if(victory)
    {
        gVictoryCandidate=true;
        if(reason>gVictoryReason)gVictoryReason=reason;
    }
    else gFailureCandidate=true;
    if(!gOutcomeQueued){gOutcomeQueued=true;RequestFrame(ResolveOutcome,gEpoch);}
}

public void ResolveOutcome(any epoch)
{
    if(epoch!=gEpoch || !gEnabled || gTerminal || gPhase==Phase_Ending || (!gVictoryCandidate && !gFailureCandidate))return;
    gOutcomeQueued=false;bool victory=gVictoryCandidate;
    LogMessage("[Shelter] Campaign outcome victory=%d reason=%s day=%d target=%d",victory,
        victory ? (gVictoryReason==Victory_Rescue ? "army_rescue" : "all_bosses") : "failure",gDay,gSurvivalDayLimit);
    Phase previous=gPhase;int previousDay=gDay;
    gPhase=Phase_Ending;gSnapshot=true;
    PlayGlobalInteractionSound(victory ? "victory" : "failure");
    ClearGrenades();StopPowerEffects();
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!gNPCs[n].used || !gNPCs[n].ready || gNPCs[n].dead)continue;
        int entity=Resolve(gNPCs[n].ref);if(entity<0)continue;
        if(gNPCs[n].boss>=0)
        {
            int boss=gNPCs[n].boss;if(!gBosses[boss].defeated)gBosses[boss].savedHP=GetEntProp(entity,Prop_Data,"m_iHealth");
            FreezeNPC(n);
        }
        else ParkNPC(n);
    }
    if(victory || gDirectLoss)
    {
        gTerminal=true;gDeadline=GetGameTime()+gOutcomeSeconds;
        AnnouncePhrase(victory ? (gVictoryReason==Victory_Rescue ? "CampaignRescueVictory" : gVictoryText) : gFailureText);
    }
    else
    {
        gDay-=gRollbackDays;if(gDay<gMinimumDay)gDay=gMinimumDay;
        ApplyDayCvars();
        RecoverBases();
        if(gDestroySummons)
        {
            SuspendBuildingSupports();
            for(int b=MAX_CORES;b<MAX_BUILDINGS;b++){gBuildings[b].alive=false;gBuildings[b].used=false;}
        }
        gRollback=true;AnnouncePhrase(gDestroySummons ? "CampaignRollbackDestroy" : "CampaignRollbackRetain",gDay);
        if(!gTerminationExpected)CS_TerminateRound(3.0,CSRoundEnd_Draw,true);
    }
    NotifyPhaseChanged(previous,0.0);
    Call_StartForward(gOutcomeForward);Call_PushCell(victory);Call_PushCell(previousDay);Call_PushCell(gDay);Call_PushCell(gTerminal);Call_Finish();
}

void RecoverBases()
{
    for(int c=0;c<gCoreCount;c++)
    {
        int b=gCores[c].building;if(b<0)continue;
        // Player bases retain their exact health/blueprint state on rollback.
        // In particular a placed blueprint cannot become a free living base.
        if(gCores[c].dynamic)continue;
        if(!gBuildings[b].upgraded)
        {
            gBuildings[b].level=1;ApplyBuildingLevel(b,true);
        }
        else
        {
            int fraction=RoundToFloor(float(gBuildings[b].maxHP)*gRecoveryFraction);
            if(fraction<1)fraction=1;
            if(gBuildings[b].alive && gBuildings[b].hp>0 && gBuildings[b].hp<fraction)fraction=gBuildings[b].hp;
            gBuildings[b].hp=fraction;
        }
        gBuildings[b].alive=true;
    }
}

public void RoundStart(Event event,const char[] name,bool dontBroadcast)
{
    if(gEnabled && !gTerminal)PrepareRound();
}

public void RoundEnd(Event event,const char[] name,bool dontBroadcast)
{
    if(!gEnabled)return;
    CleanWorld();gRoundPrepared=false;
    if(!gTerminal && gPhase!=Phase_Ending){Phase previous=gPhase;gPhase=Phase_Waiting;NotifyPhaseChanged(previous,0.0);}
    else PublishShelterState();
}

public Action CS_OnTerminateRound(float &delay,CSRoundEndReason &reason)
{
    if(!Running() || gFailureCandidate || gVictoryCandidate)return Plugin_Continue;
    if(reason==CSRoundEnd_GameStart || reason==CSRoundEnd_Draw)return Plugin_Continue;
    // The HZS all-dead forward has priority; native PVP objectives/time limits do not.
    return Plugin_Handled;
}

public void PlayerSpawn(Event event,const char[] name,bool dontBroadcast)
{
    int client=GetClientOfUserId(event.GetInt("userid"));if(!gEnabled || !LiveHuman(client))return;
    SkillsSpawnClient(client);
    DataPack pack=new DataPack();pack.WriteCell(GetClientSerial(client));pack.WriteCell(gEpoch);pack.WriteCell(0);
    RequestFrame(RedirectHuman,pack);
}

public void RedirectHuman(any value)
{
    DataPack pack=view_as<DataPack>(value);pack.Reset();int serial=pack.ReadCell(),epoch=pack.ReadCell(),attempt=pack.ReadCell();delete pack;
    int client=GetClientFromSerial(serial);if(!gEnabled || !gRoundPrepared || gTerminal || epoch!=gEpoch || !LiveHuman(client))return;
    bool redirect=GetClientTeam(client)==CS_TEAM_CT || !StrEqual(gSpawnPolicy,"native_position");
    if(!redirect)return;
    float position[3],angles[3];
    if(SafeHumanPoint(client,position,angles)){TeleportEntity(client,position,angles,view_as<float>({0.0,0.0,0.0}));return;}
    if(attempt>=4){Tell(client,"%t","SafeSpawnOccupied");return;}
    DataPack retry;CreateDataTimer(0.25,RetryHuman,retry,TIMER_FLAG_NO_MAPCHANGE);retry.WriteCell(serial);retry.WriteCell(epoch);retry.WriteCell(attempt+1);
}

public Action RetryHuman(Handle timer,DataPack value)
{
    value.Reset();DataPack next=new DataPack();next.WriteCell(value.ReadCell());next.WriteCell(value.ReadCell());next.WriteCell(value.ReadCell());RedirectHuman(next);return Plugin_Stop;
}

void ExposureTick(float now)
{
    if(gPhase!=Phase_Night || !gExposureEnabled || gExposureDamage<=0 || now<gNextExposure)return;
    gNextExposure=now+gExposureInterval;
    for(int client=1;client<=MaxClients;client++)
    {
        if(!LiveHuman(client))continue;
        float position[3];GetClientAbsOrigin(client,position);
        bool valid=true;for(int axis=0;axis<3;axis++)if((view_as<int>(position[axis])&0x7f800000)==0x7f800000)valid=false;
        if(!valid || InSafeBase(position) || CampfireSafeAt(position))continue;
        if(SkillsExposureImmune(client)){PlaySkillsImmunitySound(client);continue;}
        int health=GetClientHealth(client)-gExposureDamage;
        if(health<=0)ForcePlayerSuicide(client);else SetEntityHealth(client,health);
    }
}

void CampaignHUD()
{
    RenderCampaignHUD();
}

void ChangeCampaignMap()
{
    char next[PLATFORM_MAX_PATH];
    if(!GetNextMap(next,sizeof(next)) || !IsMapValid(next))
    {
        Handle list=new ArrayList(ByteCountToCells(PLATFORM_MAX_PATH));int serial;
        if(ReadMapList(list,serial,"mapcyclefile",MAPLIST_FLAG_CLEARARRAY|MAPLIST_FLAG_MAPSFOLDER)!=null)
        {
            ArrayList maps=view_as<ArrayList>(list);
            for(int i=0;i<maps.Length;i++){maps.GetString(i,next,sizeof(next));if(IsMapValid(next) && !StrEqual(next,gMap))break;}
        }
        delete list;
        if(!IsMapValid(next))strcopy(next,sizeof(next),gMap);
    }
    char reason[192];Format(reason,sizeof(reason),"%T","CampaignMapChange",LANG_SERVER);
    gDeadline=999999999.0;ForceChangeLevel(next,reason);
}

public Action WorldTick(Handle timer)
{
    float now=GetGameTime(),elapsed=now-gPreviousTick;gPreviousTick=now;
    BurnVisualTick(now);
    if(!gEnabled){SkillsTick(now);return Plugin_Continue;}
    bool previousReady=gCoreReady;gCoreReady=APIAvailable();
    if(previousReady!=gCoreReady){if(!gCoreReady){StopPowerEffects();ClearGrenades();}PublishShelterState();}
    if(gTerminal)
    {
        SkillsTick(now);
        CampaignHUD();if(now>=gDeadline)ChangeCampaignMap();return Plugin_Continue;
    }
    if(!Running()){SkillsTick(now);SuspendCampfires();return Plugin_Continue;}
    if(now>=gDeadline)
    {
        if(gPhase==Phase_Day)StartNight();
        else if(gSurvivalDayLimit>0 && gDay>=gSurvivalDayLimit)
        {
            QueueOutcome(true,Victory_Rescue);
            return Plugin_Continue; // No extra day/point, NPC attack, exposure or recovery.
        }
        else StartDay(true);
    }
    else if(gPhase==Phase_Day && !gWarningSent && gDeadline-now<=gWarningSeconds)
    {
        gWarningSent=true;AnnouncePhrase("NightApproaching");
    }
    int skillEpoch=gEpoch;SkillsTick(now);
    if(!Running() || gTerminal || skillEpoch!=gEpoch)return Plugin_Continue;
    EntityBudgetTick(now);FlushSharedOverflow();RestoreDeferredBuildings();
    SyncDynamicBasesGeometry();ResourceTick(elapsed);CampfireTick(elapsed);NPCTick(now);PowerTick();RestorationTick(now);BeaconTick(now);MineWorldTick();ExpansionWorldTick(now);ExposureTick(now);CampaignHUD();
    if(gNextBossAudit>0.0 && now>=gNextBossAudit)
    {
        gNextBossAudit=0.0;
        for(int b=0;b<gBossCount;b++)if(!gBosses[b].defeated && !gBosses[b].boundThisRound)
            LogError("[Shelter] Boss %s missing: check native Stage customzombies and public name",gBosses[b].id);
    }
    return Plugin_Continue;
}
