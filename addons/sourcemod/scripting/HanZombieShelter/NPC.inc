float gNextTargetFallback[MAX_NPCS];
int gFallbackAssignments;

void ClearDisconnectedNPCTarget(int n,int client,int serial)
{
    int entity=Resolve(gNPCs[n].ref);if(entity<0)return;
    if(gNPCs[n].targetClientSerial!=serial && (gLeaderOffset<0 || GetEntDataEnt2(entity,gLeaderOffset)!=client))return;
    Han_UnlockZombie(entity);gNPCs[n].targetClientSerial=0;StopBuildingAttack(n);
    if(gLeaderOffset>=0)SetEntDataEnt2(entity,gLeaderOffset,entity);
    gNextTargetFallback[n]=0.0;
}

bool APIAvailable()
{
    return GetFeatureStatus(FeatureType_Native,"Han_IsZombie")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"Han_GetZombieName")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"Han_SetZombieTarget")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"Han_UnlockZombie")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"Han_SetCustomAnimationEx")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"Han_SafeDamageZombie")==FeatureStatus_Available;
}

void LoadHanData()
{
    delete gHanData;gHanData=new KeyValues("zombies");
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieScenario/%s/HanZombieScenarioZombieData.cfg",gMap);
    if(!FileExists(path))BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieScenario/HanZombieScenarioZombieData.cfg");
    if(!gHanData.ImportFromFile(path)){delete gHanData;gHanData=null;}
}

void AnimationName(const char[] list,char[] result,int size,const char[] fallback)
{
    strcopy(result,size,list);int comma=FindCharInString(result,',');if(comma!=-1)result[comma]='\0';
    int colon=FindCharInString(result,':');if(colon!=-1)result[colon]='\0';if(!result[0])strcopy(result,size,fallback);
}

void ReadNPCAnimations(int index,const char[] publicName)
{
    strcopy(gNPCs[index].idle,sizeof(gNPCs[].idle),"idle");strcopy(gNPCs[index].attack,sizeof(gNPCs[].attack),"attack1");
    if(gHanData==null)return;
    gHanData.Rewind();if(!gHanData.GotoFirstSubKey())return;
    do
    {
        char name[128];gHanData.GetString("name",name,sizeof(name));if(!StrEqual(name,publicName))continue;
        char list[256];gHanData.GetString("idle_anim",list,sizeof(list));AnimationName(list,gNPCs[index].idle,sizeof(gNPCs[].idle),"idle");
        gHanData.GetString("attack_anim",list,sizeof(list));AnimationName(list,gNPCs[index].attack,sizeof(gNPCs[].attack),"attack1");return;
    }while(gHanData.GotoNextKey());
}

void BindNPC(int entity)
{
    if(!gEnabled || !gCoreReady || entity<=MaxClients || !IsValidEntity(entity) || !Han_IsZombie(entity) || FindNPC(entity)>=0)return;
    int slot=-1;
    for(int i=0;i<MAX_NPCS;i++)if(!gNPCs[i].used){slot=i;break;}
    if(slot<0){LogError("[Shelter] NPC tracking budget exceeded; native spawn count must fit configured pool");return;}
    char name[128];if(!Han_GetZombieName(entity,name,sizeof(name)))return;
    NPC npc;npc.used=true;npc.registrationEpoch=gEpoch;npc.ref=EntIndexToEntRef(entity);npc.visualRef=INVALID_ENT_REFERENCE;npc.boss=-1;
    for(int i=0;i<gBossCount;i++)if(StrEqual(name,gBosses[i].publicName))
    {
        if(!gBosses[i].boundThisRound){npc.boss=i;gBosses[i].boundThisRound=true;}
        else LogError("[Shelter] Duplicate fixed boss %s: kept in parked ordinary overflow pool",gBosses[i].id);
        break;
    }
    gNPCs[slot]=npc;gNextTargetFallback[slot]=0.0;
    ExpansionClearNPCFreeze(slot);
    ClearNPCRewardContext(slot);CacheNPCDrops(slot,name);
    if(entity<sizeof(gNPCBinding)){gNPCBinding[entity]=slot+1;gNPCBindingRef[entity]=npc.ref;}
    ReadNPCAnimations(slot,name);
    // This notification runs inside HZS CreateZombie. Only visibility is changed now;
    // movement/state operations wait until HZS has completed its initialization.
    GetEntityRenderColor(entity,gNPCs[slot].bodyColor[0],gNPCs[slot].bodyColor[1],gNPCs[slot].bodyColor[2],gNPCs[slot].bodyColor[3]);
    char attached[32];GetEntPropString(entity,Prop_Data,"m_iName",attached,sizeof(attached));int visual=Resolve(StringToInt(attached));
    if(visual>0)
    {
        gNPCs[slot].visualRef=EntIndexToEntRef(visual);
        GetEntityRenderColor(visual,gNPCs[slot].visualColor[0],gNPCs[slot].visualColor[1],gNPCs[slot].visualColor[2],gNPCs[slot].visualColor[3]);
        SetEntityRenderMode(visual,RENDER_TRANSCOLOR);SetEntityRenderColor(visual,255,255,255,0);
    }
    SetEntityRenderMode(entity,RENDER_TRANSCOLOR);SetEntityRenderColor(entity,255,255,255,0);
    DataPack pack=new DataPack();pack.WriteCell(npc.ref);pack.WriteCell(gEpoch);
    RequestFrame(NPCReady,pack);
}

public void Han_OnZombieCreated(int zombie)
{
    BindNPC(zombie);
}

public void NPCReady(any value)
{
    DataPack pack=view_as<DataPack>(value);pack.Reset();int ref=pack.ReadCell(),epoch=pack.ReadCell();delete pack;
    int entity=Resolve(ref);if(!gEnabled || !gCoreReady || epoch!=gEpoch || entity<0)return;
    int n=FindNPC(entity);if(n<0)return;
    char publicName[128];if(Han_GetZombieName(entity,publicName,sizeof(publicName)))CacheNPCDrops(n,publicName);
    gNPCs[n].ready=true;gNPCs[n].baseHP=GetEntProp(entity,Prop_Data,"m_iHealth");gNPCs[n].lastHP=gNPCs[n].baseHP;
    gNPCs[n].takeDamage=GetEntProp(entity,Prop_Data,"m_takedamage");gNPCs[n].solidFlags=GetEntProp(entity,Prop_Send,"m_usSolidFlags");
    gNPCs[n].moveType=GetEntityMoveType(entity);
    GetEntPropVector(entity,Prop_Data,"m_vecOrigin",gNPCs[n].originalOrigin);GetEntPropVector(entity,Prop_Data,"m_angRotation",gNPCs[n].originalAngles);
    SDKHook(entity,SDKHook_ThinkPost,NPCThinkPost);
    SDKHook(entity,SDKHook_Think,ExpansionNPCThinkPre);
    int boss=gNPCs[n].boss;
    ParkNPC(n);
    if(boss>=0)
    {
        DataPack timerPack;CreateDataTimer(0.1,BossPosition,timerPack,TIMER_FLAG_NO_MAPCHANGE);
        timerPack.WriteCell(ref);timerPack.WriteCell(epoch);
    }
}

void Colors(int n,bool visible)
{
    int entity=Resolve(gNPCs[n].ref),visual=Resolve(gNPCs[n].visualRef);
    if(entity>0)SetEntityRenderColor(entity,gNPCs[n].bodyColor[0],gNPCs[n].bodyColor[1],gNPCs[n].bodyColor[2],visible ? gNPCs[n].bodyColor[3] : 0);
    if(visual>0)SetEntityRenderColor(visual,gNPCs[n].visualColor[0],gNPCs[n].visualColor[1],gNPCs[n].visualColor[2],visible ? gNPCs[n].visualColor[3] : 0);
}

void FreezeNPC(int n)
{
    int entity=Resolve(gNPCs[n].ref);if(entity<0 || gNPCs[n].dead)return;
    Han_SetZombieTarget(entity,entity,-1.0);
    Han_SetCustomAnimationEx(entity,gNPCs[n].idle,3600.0);
    SetEntityMoveType(entity,MOVETYPE_NONE);TeleportEntity(entity,NULL_VECTOR,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    if(gLeaderOffset>=0)SetEntDataEnt2(entity,gLeaderOffset,entity);
}

void ParkNPC(int n)
{
    int entity=Resolve(gNPCs[n].ref);if(entity<0 || !gNPCs[n].ready || gNPCs[n].dead)return;
    ExpansionClearNPCFreeze(n,true);
    ClearNPCBurnVisual(n);
    gNPCs[n].active=false;gNPCs[n].paused=true;gNPCs[n].burnUntil=0.0;gNPCs[n].attackingUID=0;
    FreezeNPC(n);Colors(n,false);SetEntProp(entity,Prop_Data,"m_takedamage",0,1);
    SetEntProp(entity,Prop_Send,"m_usSolidFlags",gNPCs[n].solidFlags|4);
    float park[3];park=gParking;park[0]+=float(n%8)*16.0;park[1]+=float(n/8)*16.0;
    TeleportEntity(entity,park,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
}

void RestoreNPCBody(int n)
{
    int entity=Resolve(gNPCs[n].ref);if(entity<0)return;
    SetEntProp(entity,Prop_Send,"m_usSolidFlags",gNPCs[n].solidFlags);
    SetEntProp(entity,Prop_Data,"m_takedamage",gNPCs[n].takeDamage,1);Colors(n,true);
    Han_UnlockZombie(entity);Han_SetCustomAnimationEx(entity,gNPCs[n].idle,0.01);
    SetEntityMoveType(entity,MOVETYPE_STEP);gNPCs[n].paused=false;
    MaintainExpansionFreeze(n,GetGameTime());
}

public Action BossPosition(Handle timer,DataPack pack)
{
    pack.Reset();int entity=Resolve(pack.ReadCell()),epoch=pack.ReadCell();
    if(entity<0 || epoch!=gEpoch || !gEnabled)return Plugin_Stop;
    int n=FindNPC(entity);if(n<0 || gNPCs[n].boss<0)return Plugin_Stop;
    int b=gNPCs[n].boss;
    if(gBosses[b].defeated){ParkNPC(n);return Plugin_Stop;}
    float mins[3],maxs[3];NPCBounds(entity,mins,maxs);
    if(!ClearHull(gBosses[b].origin,mins,maxs,entity))
    {
        LogError("[Shelter] Boss %s actual hull does not fit target; mins=%.1f %.1f %.1f maxs=%.1f %.1f %.1f; remains parked",gBosses[b].id,mins[0],mins[1],mins[2],maxs[0],maxs[1],maxs[2]);return Plugin_Stop;
    }
    TeleportEntity(entity,gBosses[b].origin,gBosses[b].angles,view_as<float>({0.0,0.0,0.0}));
    SetEntProp(entity,Prop_Data,"m_iHealth",gBosses[b].savedHP);
    gBosses[b].positionedHP=GetEntProp(entity,Prop_Data,"m_iHealth");
    gNPCs[n].lastHP=gBosses[b].savedHP;RestoreNPCBody(n);
    gNPCs[n].active=true;
    if(!gBosses[b].awakened)FreezeNPC(n);
    PrintToServer("[Shelter] Boss positioned %s hp=%d dormant=%d",gBosses[b].id,gBosses[b].savedHP,!gBosses[b].awakened);
    return Plugin_Stop;
}

bool ActivateOrdinary(int n)
{
    int entity=Resolve(gNPCs[n].ref);if(entity<0 || gNPCs[n].dead || !gNPCs[n].ready || gNPCs[n].boss>=0)return false;
    float position[3],angles[3];if(!RandomTPoint(entity,position,angles))return false;
    int hp=RoundToNearest(float(gNPCs[n].baseHP)*HPScale());
    SelectDay();int cap=gCfg[C_Days].GetNum("max_raider_hp",30000);if(hp>cap)hp=cap;if(hp<1)hp=1;
    SetEntProp(entity,Prop_Data,"m_iHealth",hp);gNPCs[n].lastHP=hp;
    TeleportEntity(entity,position,angles,view_as<float>({0.0,0.0,0.0}));RestoreNPCBody(n);
    gNPCs[n].active=true;gNPCs[n].targetClientSerial=0;return true;
}

int ActiveOrdinary()
{
    int count;for(int i=0;i<MAX_NPCS;i++)
    {
        if(!gNPCs[i].used || !gNPCs[i].active || gNPCs[i].dead || gNPCs[i].boss>=0)continue;
        int entity=Resolve(gNPCs[i].ref);if(entity>0 && GetEntProp(entity,Prop_Data,"m_iHealth")>0)count++;
    }
    return count;
}

void TrimOrdinary(int target)
{
    // Keep the daytime population in place. Only excess ordinary NPCs are
    // parked; fixed Bosses never enter this population or the dawn cleanup.
    int retained;
    for(int i=0;i<MAX_NPCS;i++)
    {
        if(!gNPCs[i].used || !gNPCs[i].ready || !gNPCs[i].active || gNPCs[i].dead || gNPCs[i].boss>=0)continue;
        int entity=Resolve(gNPCs[i].ref);if(entity<0 || GetEntProp(entity,Prop_Data,"m_iHealth")<=0)continue;
        if(retained<target)retained++;
        else ParkNPC(i);
    }
}

void StopBuildingAttack(int n)
{
    if(!gNPCs[n].attackingUID)return;
    int entity=Resolve(gNPCs[n].ref);gNPCs[n].attackingUID=0;
    if(entity>0)
    {
        if(ExpansionNPCFrozen(n,GetGameTime()) || ExpansionNPCProtected(n))
        {if(ExpansionNPCFrozen(n,GetGameTime()))MaintainExpansionFreeze(n,GetGameTime());else FreezeNPC(n);}
        else {Han_SetCustomAnimationEx(entity,gNPCs[n].idle,0.01);SetEntityMoveType(entity,MOVETYPE_STEP);}
    }
}

void NPCTick(float now)
{
    if(Running() && now>=gNextAdmissions)
    {
        int target=OrdinaryTarget();TrimOrdinary(target);
        int active=ActiveOrdinary();SelectDay();
        bool daytime=gPhase==Phase_Day;
        int batch=gCfg[C_Days].GetNum(daytime ? "day_spawn_batch" : "spawn_batch",2);
        gNextAdmissions=now+gCfg[C_Days].GetFloat(daytime ? "day_spawn_interval_seconds" : "spawn_interval_seconds",daytime ? 2.0 : 1.0);
        for(int i=0;i<MAX_NPCS && active<target && batch>0;i++)
            if(gNPCs[i].used && !gNPCs[i].active && !gNPCs[i].dead && gNPCs[i].boss<0 && ActivateOrdinary(i)){active++;batch--;}
    }
    for(int i=0;i<MAX_NPCS;i++)
    {
        if(!gNPCs[i].used || !gNPCs[i].ready || gNPCs[i].dead)continue;
        int entity=Resolve(gNPCs[i].ref);if(entity<0)continue;
        int boss=gNPCs[i].boss,health=GetEntProp(entity,Prop_Data,"m_iHealth");
        if(boss>=0 && gNPCs[i].active)
        {
            if(health>0 && health<gNPCs[i].lastHP && !gBosses[boss].awakened)
            {
                gBosses[boss].awakened=true;RestoreNPCBody(i);AnnouncePhrase("BossAwakened");
            }
            if(health>0)gBosses[boss].savedHP=health;gNPCs[i].lastHP=health;
        }
        if(!Running() || !gNPCs[i].active || (boss>=0 && !gBosses[boss].awakened))
        {
            if(!gNPCs[i].paused)FreezeNPC(i);continue;
        }
        if(gNPCs[i].burnUntil>now && now>=gNPCs[i].nextBurn)
        {
            gNPCs[i].nextBurn=now+gNPCs[i].burnInterval;
            DamageBurningNPC(i);
            if(gNPCs[i].dead || Resolve(gNPCs[i].ref)<0)continue;
        }
        MaintainExpansionFreeze(i,now);
        if(ExpansionNPCFrozen(i,now))continue;
        // Keep valid native targets. Only an active NPC without one receives a
        // short fallback lock; parking, dormant Bosses and wall attacks are excluded.
        int target=gLeaderOffset>=0 ? GetEntDataEnt2(entity,gLeaderOffset) : 0;
        if((target<=0 || target==entity || (target<=MaxClients && !LiveHuman(target))
            || (target>MaxClients && !IsValidEntity(target)))
            && !gNPCs[i].attackingUID && !gNPCs[i].paused && now>=gNextTargetFallback[i])
        {
            gNextTargetFallback[i]=now+0.5;
            int fallback=NearestFallbackPlayer(entity);
            if(fallback>0 && Han_SetZombieTarget(entity,fallback,0.75))
            {
                target=fallback;gNPCs[i].targetClientSerial=GetClientSerial(fallback);
                if(gLeaderOffset>=0)SetEntDataEnt2(entity,gLeaderOffset,fallback);
                gFallbackAssignments++;
            }
        }
        if(LiveHuman(target))gNPCs[i].targetClientSerial=GetClientSerial(target);
        else if(gNPCs[i].attackingUID)target=GetClientFromSerial(gNPCs[i].targetClientSerial);
        if(!LiveHuman(target))target=0;
        if(target==0){StopBuildingAttack(i);continue;}
        int building=BlockingBuilding(i,target);
        if(building>=0)
        {
            bool starting=gNPCs[i].attackingUID!=gBuildings[building].uid;
            gNPCs[i].attackingUID=gBuildings[building].uid;
            SetEntityMoveType(entity,MOVETYPE_NONE);TeleportEntity(entity,NULL_VECTOR,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
            if(starting || now>=gNPCs[i].nextAttack)
            {
                float interval=boss>=0 ? gBosses[boss].interval : Decimal(C_Zombies,"BuildingAttack","interval_seconds",1.0);
                float damage=boss>=0 ? gBosses[boss].damage : Decimal(C_Zombies,"BuildingAttack","damage",20.0)*HPScale();
                Han_SetCustomAnimationEx(entity,gNPCs[i].attack,interval+0.1);
                gNPCs[i].nextAttack=now+interval;WallImpactSound(building);DamageBuilding(building,RoundToNearest(damage),true,i);
            }
        }
        else
        {
            StopBuildingAttack(i);
        }
    }
}

int NearestFallbackPlayer(int entity)
{
    float origin[3];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",origin);
    float nearest;int chosen,ties;
    for(int client=1;client<=MaxClients;client++)
    {
        if(!LiveHuman(client) || IsFakeClient(client) || IsClientSourceTV(client) || IsClientReplay(client))continue;
        float point[3];GetClientAbsOrigin(client,point);float distance=GetVectorDistance(origin,point,true);
        if(chosen==0 || distance<nearest-0.01){nearest=distance;chosen=client;ties=1;}
        else if(FloatAbs(distance-nearest)<=0.01 && GetRandomInt(1,++ties)==1)chosen=client;
    }
    return chosen;
}

public void NPCThinkPost(int entity)
{
    int n=FindNPC(entity);if(n<0 || gNPCs[n].dead || !gNPCs[n].ready)return;
    int boss=gNPCs[n].boss;
    MaintainExpansionFreeze(n,GetGameTime());
    if(!gNPCs[n].active || !Running() || (boss>=0 && !gBosses[boss].awakened) || gNPCs[n].attackingUID || ExpansionNPCFrozen(n,GetGameTime()))
    {
        SetEntityMoveType(entity,MOVETYPE_NONE);TeleportEntity(entity,NULL_VECTOR,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
        if(gLeaderOffset>=0 && (!gNPCs[n].active || !Running() || (boss>=0 && !gBosses[boss].awakened)))SetEntDataEnt2(entity,gLeaderOffset,entity);
    }
}

public void Han_OnZombieDamagePre(int zombie,int attacker,int &damage)
{
    int n=FindNPC(zombie);if(n<0)return;
    if(!Running() || !gNPCs[n].active){damage=0;return;}
    bool external=gDamageNPCRef==gNPCs[n].ref;
    if(external)damage=gDamageValue;
    ApplyProgressionDamage(n,attacker,damage,external && gDamageIsDefense);
}

public void Han_OnZombieDeath(int zombie,int killer)
{
    int n=FindNPC(zombie);if(n<0 || gNPCs[n].dead || gNPCs[n].settled || gNPCs[n].registrationEpoch!=gEpoch)return;
    bool actualDeath=gNPCs[n].active && Running();
    // Consume once before rewards/forward re-entry and before the final Boss ends play.
    gNPCs[n].dead=true;gNPCs[n].settled=true;gNPCs[n].active=false;gNPCs[n].burnUntil=0.0;
    ClearNPCBurnVisual(n);
    ExpansionClearNPCFreeze(n);
    if(!actualDeath)return;
    if(!gNPCs[n].publicName[0])
    {
        char publicName[128];if(Han_GetZombieName(zombie,publicName,sizeof(publicName)))CacheNPCDrops(n,publicName);
    }
    SettleZombieDrops(n,killer);
    int boss=gNPCs[n].boss;
    if(boss>=0)
    {
        gBosses[boss].defeated=true;gBosses[boss].savedHP=0;
        if(AllBossesDefeated())QueueOutcome(true);
    }
    else
    {
        gNativeKills++;
        if(gNativeKills==90000)LogError("[Shelter] Native count is nearing the finite 99999 bridge threshold");
    }
}

bool AllBossesDefeated()
{
    int required;for(int i=0;i<gBossCount;i++)if(gBosses[i].required){required++;if(!gBosses[i].defeated)return false;}
    return required>0;
}

void RestoreNPCOnUnload(int n)
{
    int entity=Resolve(gNPCs[n].ref);if(entity<0 || !gNPCs[n].ready || gNPCs[n].dead)return;
    SDKUnhook(entity,SDKHook_ThinkPost,NPCThinkPost);
    SDKUnhook(entity,SDKHook_Think,ExpansionNPCThinkPre);ExpansionClearNPCFreeze(n);
    if(gNPCs[n].paused)
    {
        float position[3],angles[3];
        if(RandomTPoint(entity,position,angles))TeleportEntity(entity,position,angles,NULL_VECTOR);
        else TeleportEntity(entity,gNPCs[n].originalOrigin,gNPCs[n].originalAngles,NULL_VECTOR);
    }
    RestoreNPCBody(n);
}
