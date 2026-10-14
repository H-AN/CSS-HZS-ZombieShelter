void OverrideCorePool()
{
    if(!gEnabled)return;
    gCorePoolCvar=FindConVar("hzs_maxzombiespawn");
    if(gCorePoolCvar==null)return;
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
    if(gPoolOverridden && gCorePoolCvar!=null)gCorePoolCvar.IntValue=gOriginalPool;
    gPoolOverridden=false;
}

void PrepareRound()
{
    if(!gEnabled || gTerminal)return;
    gEpoch++;gRoundPrepared=false;gPhase=Phase_Waiting;gWarningSent=false;
    gFailureCandidate=false;gVictoryCandidate=false;gOutcomeQueued=false;gTerminationExpected=false;
    CleanWorld();
    for(int i=0;i<MAX_NPCS;i++){gNPCs[i].used=false;gNPCs[i].ref=INVALID_ENT_REFERENCE;}
    for(int i=0;i<gBossCount;i++)gBosses[i].boundThisRound=false;
    if(!CreateWorld(gSnapshot))
    {
        CleanWorld();gEnabled=false;RestoreCorePool();LogError("[Shelter] World creation failed; mode left disabled, no partial session");return;
    }
    gSnapshot=true;gRoundPrepared=true;gRollback=false;OverrideCorePool();
}

void StartDay(bool advance)
{
    if(!gEnabled || gTerminal || !gRoundPrepared)return;
    if(advance)gDay++;
    gPhase=Phase_Day;gDeadline=GetGameTime()+gDaySeconds;gWarningSent=false;
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].ready && !gNPCs[n].dead && gNPCs[n].boss<0)ParkNPC(n);
    char text[192];Format(text,sizeof(text),"第%d天白天：出门采集，修建基地。距离黑夜%.0f秒。",gDay,gDaySeconds);Announce(text);
}

void StartNight()
{
    if(!Running() || gTerminal)return;
    gPhase=Phase_Night;gDeadline=GetGameTime()+gNightSeconds;gNextAdmissions=0.0;
    gNextExposure=GetGameTime()+gExposureInterval;
    char text[192];Format(text,sizeof(text),"第%d天黑夜：请进入存活基地！屋外每%.1f秒损失%d点生命。",gDay,gExposureInterval,gExposureDamage);Announce(text);
}

public void Han_OnGameStart()
{
    if(!gEnabled || gTerminal)return;
    gCoreReady=APIAvailable();if(!gCoreReady)return;
    if(!gRoundPrepared)PrepareRound();
    if(!gRoundPrepared)return;
    OverrideCorePool();StartDay(false);gNextBossAudit=GetGameTime()+3.0;
}

public void Han_OnGameEnd()
{
    if(!gEnabled || gTerminal || gFailureCandidate || gVictoryCandidate || gPhase==Phase_Ending)return;
    // A lifecycle notification without a result is not a loss.
    gPhase=Phase_Waiting;
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
    else LogError("[Shelter] Core Stage completed before map Boss; 99999 bridge/configuration needs review");
}

void QueueOutcome(bool victory)
{
    if(!gEnabled || gTerminal || gPhase==Phase_Ending)return;
    if(victory)gVictoryCandidate=true;else gFailureCandidate=true;
    if(!gOutcomeQueued){gOutcomeQueued=true;RequestFrame(ResolveOutcome,gEpoch);}
}

public void ResolveOutcome(any epoch)
{
    if(epoch!=gEpoch || !gEnabled || gTerminal || (!gVictoryCandidate && !gFailureCandidate))return;
    gOutcomeQueued=false;bool victory=gVictoryCandidate;
    gPhase=Phase_Ending;gSnapshot=true;
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
        gTerminal=true;gDeadline=GetGameTime()+gOutcomeSeconds;Announce(victory ? gVictoryText : gFailureText);
    }
    else
    {
        gDay-=gRollbackDays;if(gDay<gMinimumDay)gDay=gMinimumDay;
        RecoverBases();
        if(gDestroySummons)
            for(int b=gCoreCount;b<MAX_BUILDINGS;b++){gBuildings[b].alive=false;gBuildings[b].used=false;}
        gRollback=true;char text[192];Format(text,sizeof(text),"防守失败，回到第%d天。基地成长保留；%s。",gDay,gDestroySummons ? "召唤物清空" : "存活设施将在下一回合重建");Announce(text);
        if(!gTerminationExpected)CS_TerminateRound(3.0,CSRoundEnd_Draw,true);
    }
}

void RecoverBases()
{
    for(int c=0;c<gCoreCount;c++)
    {
        int b=gCores[c].building;if(b<0)continue;
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
    if(!gTerminal && gPhase!=Phase_Ending)gPhase=Phase_Waiting;
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
    if(attempt>=4){Tell(client,"安全出生点暂被占用，请尽快前往存活基地。");return;}
    DataPack retry;CreateDataTimer(0.25,RetryHuman,retry,TIMER_FLAG_NO_MAPCHANGE);retry.WriteCell(serial);retry.WriteCell(epoch);retry.WriteCell(attempt+1);
}

public Action RetryHuman(Handle timer,DataPack value)
{
    value.Reset();DataPack next=new DataPack();next.WriteCell(value.ReadCell());next.WriteCell(value.ReadCell());next.WriteCell(value.ReadCell());RedirectHuman(next);return Plugin_Stop;
}

void ExposureTick(float now)
{
    if(gPhase!=Phase_Night || !gExposureEnabled || now<gNextExposure)return;
    gNextExposure=now+gExposureInterval;
    for(int client=1;client<=MaxClients;client++)
    {
        if(!LiveHuman(client))continue;
        float position[3];GetClientAbsOrigin(client,position);if(InSafeBase(position))continue;
        int health=GetClientHealth(client)-gExposureDamage;
        if(health<=0)ForcePlayerSuicide(client);else SetEntityHealth(client,health);
    }
}

void CampaignHUD(float now)
{
    if(now<gNextHUD)return;gNextHUD=now+1.0;
    int bosses;for(int i=0;i<gBossCount;i++)if(gBosses[i].defeated)bosses++;
    for(int client=1;client<=MaxClients;client++)
    {
        if(!IsClientInGame(client) || IsFakeClient(client))continue;
        if(gTerminal){PrintCenterText(client,"%s",gVictoryCandidate ? gVictoryText : gFailureText);continue;}
        if(!Running())continue;
        int b=LiveHuman(client) ? FindBuilding(AimedEntity(client,gUseDistance)) : -1;
        char target[128];if(b>=0)Format(target,sizeof(target),"\n瞄准设施 Lv%d HP %d/%d · E升级 · 刀修",gBuildings[b].level,gBuildings[b].hp,gBuildings[b].maxHP);
        float seconds=gDeadline-now;if(seconds<0.0)seconds=0.0;
        PrintHintText(client,"第%d天 %s %.0f秒 · 基地%d/%d · 夜袭%d/%d\n木%d 铁%d 铜%d 银%d 金%d · Boss%d/%d%s",
            gDay,gPhase==Phase_Day ? "白天" : "黑夜",seconds,AliveCores(),gCoreCount,ActiveOrdinary(),gPhase==Phase_Night ? NightTarget() : 0,
            gInventory[0],gInventory[1],gInventory[2],gInventory[3],gInventory[4],bosses,gBossCount,target);
    }
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
    gDeadline=999999999.0;ForceChangeLevel(next,"昼夜求生战役结束");
}

public Action WorldTick(Handle timer)
{
    float now=GetGameTime(),elapsed=now-gPreviousTick;gPreviousTick=now;
    if(!gEnabled)return Plugin_Continue;
    gCoreReady=APIAvailable();
    if(gTerminal)
    {
        CampaignHUD(now);if(now>=gDeadline)ChangeCampaignMap();return Plugin_Continue;
    }
    if(!Running())return Plugin_Continue;
    if(now>=gDeadline){if(gPhase==Phase_Day)StartNight();else StartDay(true);}
    else if(gPhase==Phase_Day && !gWarningSent && gDeadline-now<=gWarningSeconds)
    {
        gWarningSent=true;Announce("黑夜即将到来，请赶回基地！");
    }
    ResourceTick(elapsed);NPCTick(now);DefenseTick(now);ExposureTick(now);CampaignHUD(now);
    if(gNextBossAudit>0.0 && now>=gNextBossAudit)
    {
        gNextBossAudit=0.0;
        for(int b=0;b<gBossCount;b++)if(!gBosses[b].defeated && !gBosses[b].boundThisRound)
            LogError("[Shelter] Boss %s missing: check native Stage customzombies and public name",gBosses[b].id);
    }
    return Plugin_Continue;
}
