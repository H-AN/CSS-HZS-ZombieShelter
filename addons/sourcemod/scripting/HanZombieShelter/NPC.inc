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
        char name[64];gHanData.GetString("name",name,sizeof(name));if(!StrEqual(name,publicName))continue;
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
    char name[64];if(!Han_GetZombieName(entity,name,sizeof(name)))return;
    NPC npc;npc.used=true;npc.ref=EntIndexToEntRef(entity);npc.visualRef=INVALID_ENT_REFERENCE;npc.boss=-1;
    for(int i=0;i<gBossCount;i++)if(StrEqual(name,gBosses[i].publicName))
    {
        if(!gBosses[i].boundThisRound){npc.boss=i;gBosses[i].boundThisRound=true;}
        else LogError("[Shelter] Duplicate fixed boss %s: kept in parked ordinary overflow pool",gBosses[i].id);
        break;
    }
    gNPCs[slot]=npc;
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
    gNPCs[n].ready=true;gNPCs[n].baseHP=GetEntProp(entity,Prop_Data,"m_iHealth");gNPCs[n].lastHP=gNPCs[n].baseHP;
    gNPCs[n].takeDamage=GetEntProp(entity,Prop_Data,"m_takedamage");gNPCs[n].solidFlags=GetEntProp(entity,Prop_Send,"m_usSolidFlags");
    gNPCs[n].moveType=GetEntityMoveType(entity);
    GetEntPropVector(entity,Prop_Data,"m_vecOrigin",gNPCs[n].originalOrigin);GetEntPropVector(entity,Prop_Data,"m_angRotation",gNPCs[n].originalAngles);
    SDKHook(entity,SDKHook_ThinkPost,NPCThinkPost);
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

void StopBuildingAttack(int n)
{
    if(!gNPCs[n].attackingUID)return;
    int entity=Resolve(gNPCs[n].ref);gNPCs[n].attackingUID=0;
    if(entity>0){Han_SetCustomAnimationEx(entity,gNPCs[n].idle,0.01);SetEntityMoveType(entity,MOVETYPE_STEP);}
}

void NPCTick(float now)
{
    if(gPhase==Phase_Night && now>=gNextAdmissions)
    {
        int target=NightTarget(),active=ActiveOrdinary();SelectDay();
        int batch=gCfg[C_Days].GetNum("spawn_batch",2);gNextAdmissions=now+gCfg[C_Days].GetFloat("spawn_interval_seconds",1.0);
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
                gBosses[boss].awakened=true;RestoreNPCBody(i);Announce("Boss已经苏醒！");
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
            DamageNPC(i,gNPCs[i].burnDamage,gNPCs[i].burnClientSerial);
            if(gNPCs[i].dead || Resolve(gNPCs[i].ref)<0)continue;
        }
        // HZS owns acquisition. Consume its current leader/target; do not choose
        // or periodically lock a nearest player again in the external mode.
        int target=gLeaderOffset>=0 ? GetEntDataEnt2(entity,gLeaderOffset) : 0;
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
                gNPCs[i].nextAttack=now+interval;DamageBuilding(building,RoundToNearest(damage));
            }
        }
        else
        {
            StopBuildingAttack(i);
        }
    }
}

public void NPCThinkPost(int entity)
{
    int n=FindNPC(entity);if(n<0 || gNPCs[n].dead || !gNPCs[n].ready)return;
    int boss=gNPCs[n].boss;
    if(!gNPCs[n].active || !Running() || (boss>=0 && !gBosses[boss].awakened) || gNPCs[n].attackingUID)
    {
        SetEntityMoveType(entity,MOVETYPE_NONE);TeleportEntity(entity,NULL_VECTOR,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
        if(gLeaderOffset>=0 && (!gNPCs[n].active || !Running() || (boss>=0 && !gBosses[boss].awakened)))SetEntDataEnt2(entity,gLeaderOffset,entity);
    }
}

public void Han_OnZombieDamagePre(int zombie,int attacker,int &damage)
{
    int n=FindNPC(zombie);if(n<0)return;
    if(!Running() || !gNPCs[n].active){damage=0;return;}
    if(gDamageNPCRef==gNPCs[n].ref)damage=gDamageValue;
}

public void Han_OnZombieDeath(int zombie,int killer)
{
    int n=FindNPC(zombie);if(n<0 || gNPCs[n].dead)return;
    bool active=gNPCs[n].active;gNPCs[n].dead=true;gNPCs[n].active=false;gNPCs[n].burnUntil=0.0;
    int boss=gNPCs[n].boss;
    if(boss>=0 && Running() && active)
    {
        gBosses[boss].defeated=true;gBosses[boss].savedHP=0;
        if(AllBossesDefeated())QueueOutcome(true);
    }
    else if(active && Running())
    {
        gNativeKills++;GrantDeathDrop();
        if(gNativeKills==90000)LogError("[Shelter] Native count is nearing the finite 99999 bridge threshold");
    }
}

void GrantDeathDrop()
{
    if(!SelectDay())return;
    char profile[64];gCfg[C_Days].GetString("drop_profile",profile,sizeof(profile),"night_raider");
    float multiplier=gCfg[C_Days].GetFloat("drop_multiplier",1.0);char path[128];Format(path,sizeof(path),"Profiles/%s",profile);
    if(!Node(C_Drops,path))return;
    float chance=gCfg[C_Drops].GetFloat("drop_chance",0.25);int low=gCfg[C_Drops].GetNum("amount_min",1),high=gCfg[C_Drops].GetNum("amount_max",2);
    if(GetRandomFloat(0.0,1.0)>chance || low<1 || high<low)return;
    Format(path,sizeof(path),"Profiles/%s/Weights",profile);int material=WeightedMaterial(C_Drops,path);
    if(material>=0)Grant(material,RoundToNearest(float(GetRandomInt(low,high))*multiplier));
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
    if(gNPCs[n].paused)
    {
        float position[3],angles[3];
        if(RandomTPoint(entity,position,angles))TeleportEntity(entity,position,angles,NULL_VECTOR);
        else TeleportEntity(entity,gNPCs[n].originalOrigin,gNPCs[n].originalAngles,NULL_VECTOR);
    }
    RestoreNPCBody(n);
}
