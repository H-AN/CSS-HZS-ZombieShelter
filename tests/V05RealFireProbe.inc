// Isolated real native-NPC cadence fixture, compiled only into the test plugin.
bool tV05FireActive,tV05FireTraceBefore;
int tV05FireNPC=-1,tV05FireNPCHealth,tV05FireCount[10],tV05FireSlots[10],tV05FireUIDs[10];
float tV05FireTimes[10][256],tV05FireNPCPosition[3],tV05FireNPCAngles[3],tV05FireStarted;
NPC tV05FireSavedNPC;
Building tV05FireSavedBuildings[10];
KeyValues tV05FireOriginalCfg;

void V05CaptureRealShot(int building,float now,int target)
{
    if(!tV05FireActive || target!=tV05FireNPC)return;
    for(int i=0;i<10;i++)if(tV05FireSlots[i]==building && gBuildings[building].uid==tV05FireUIDs[i])
    {int count=tV05FireCount[i];if(count<256)tV05FireTimes[i][count]=now;tV05FireCount[i]++;return;}
}

void V05RealFireCleanup(int client)
{
    if(client<1)return;
    if(!tV05FireActive && tV05FireOriginalCfg==null)return;
    tV05FireActive=false;gDefenseTrace=tV05FireTraceBefore;
    for(int i=0;i<10;i++)if(tV05FireUIDs[i]>0)
    {
        int b=tV05FireSlots[i];
        if(gBuildings[b].uid==tV05FireUIDs[i])
        {RemoveBuildingEffect(b);int ref=gBuildings[b].ref;gBuildings[b].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);gBuildings[b]=tV05FireSavedBuildings[i];}
        tV05FireUIDs[i]=0;
    }
    if(tV05FireNPC>=0 && gNPCs[tV05FireNPC].ref==tV05FireSavedNPC.ref)
    {
        int entity=Resolve(gNPCs[tV05FireNPC].ref);gNPCs[tV05FireNPC]=tV05FireSavedNPC;
        if(entity>0)
        {
            SetEntProp(entity,Prop_Data,"m_iHealth",tV05FireNPCHealth);TeleportEntity(entity,tV05FireNPCPosition,tV05FireNPCAngles,view_as<float>({0.0,0.0,0.0}));
            if(gNPCs[tV05FireNPC].paused){FreezeNPC(tV05FireNPC);Colors(tV05FireNPC,false);SetEntProp(entity,Prop_Data,"m_takedamage",0,1);SetEntProp(entity,Prop_Send,"m_usSolidFlags",gNPCs[tV05FireNPC].solidFlags|4);}
            else RestoreNPCBody(tV05FireNPC);
        }
    }
    tV05FireNPC=-1;
    if(tV05FireOriginalCfg!=null){KeyValues fixture=gCfg[C_Turrets];gCfg[C_Turrets]=tV05FireOriginalCfg;tV05FireOriginalCfg=null;delete fixture;}
    MarkBuildAreasDirty();RecalculatePower();PrintToServer("[V05RealFire] CLEANUP restored original native target HP/position/state, configuration handle and ten prior slots");
}

void V05RealFireSetup(int client)
{
    if(tV05FireActive || tV05FireOriginalCfg!=null)V05RealFireCleanup(client);
    if(!V05BeginRealFixture(client))return;
    int npc=-1;for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].ready && !gNPCs[n].dead && gNPCs[n].boss<0 && Resolve(gNPCs[n].ref)>0){npc=n;break;}
    if(npc<0){tSkipped++;PrintToServer("[ShelterTest] SKIP real fire: no existing living ordinary native NPC; none created");return;}
    int source=-1;for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Turret){source=r;break;}if(source<0)return;
    int core=0,base=gCores[core].building,body=base>=0 ? Resolve(gBuildings[base].ref) : -1;if(body<0)return;
    float maxs[3],center[3];GetEntPropVector(body,Prop_Send,"m_vecMaxs",maxs);
    center=gCores[core].origin;center[0]-=160.0;center[2]+=maxs[2]+96.0;
    if(!BuildAreaContains(core,center) || TR_PointOutsideWorld(center)){tSkipped++;PrintToServer("[ShelterTest] SKIP real fire: above-roof target position outside current valid build area");return;}
    int entity=Resolve(gNPCs[npc].ref);tV05FireNPC=npc;tV05FireSavedNPC=gNPCs[npc];tV05FireNPCHealth=GetEntProp(entity,Prop_Data,"m_iHealth");
    GetEntPropVector(entity,Prop_Data,"m_vecOrigin",tV05FireNPCPosition);GetEntPropVector(entity,Prop_Data,"m_angRotation",tV05FireNPCAngles);
    tV05FireTraceBefore=gDefenseTrace;tV05FireOriginalCfg=gCfg[C_Turrets];gCfg[C_Turrets]=new KeyValues("ShelterTurrets");
    char path[128],model[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s",gRecipes[source].type);
    if(tV05FireOriginalCfg.JumpToKey("Types") && tV05FireOriginalCfg.JumpToKey(gRecipes[source].type))tV05FireOriginalCfg.GetString("model",model,sizeof(model));
    else strcopy(model,sizeof(model),"models/zshelter/shelter_b_turret01.mdl");tV05FireOriginalCfg.Rewind();
    for(int i=0;i<10;i++){tV05FireUIDs[i]=0;tV05FireCount[i]=0;}
    TeleportEntity(entity,center,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));SetEntProp(entity,Prop_Data,"m_iHealth",1000000);
    RestoreNPCBody(npc);gNPCs[npc].active=true;gNPCs[npc].paused=false;gNPCs[npc].burnUntil=0.0;gNPCs[npc].lastHP=1000000;FreezeNPC(npc);
    SetEntProp(entity,Prop_Data,"m_takedamage",2,1);
    bool complete=true;
    for(int i=0;i<10 && complete;i++)
    {
        float interval=float(i+1)*0.1;char type[64];Format(type,sizeof(type),"v05_fire_%02d",i+1);
        KeyValues cfg=gCfg[C_Turrets];cfg.Rewind();cfg.JumpToKey("Types",true);cfg.JumpToKey(type,true);cfg.SetString("model",model);
        cfg.SetString("display_name",type);cfg.JumpToKey("Levels",true);cfg.JumpToKey("1",true);cfg.SetNum("max_hp",100);
        cfg.SetFloat("damage",1.0);cfg.SetFloat("fire_interval",interval);cfg.SetFloat("range",4096.0);cfg.SetFloat("power_required",0.0);
        int slot=-1;for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(!gBuildings[b].used || !gBuildings[b].alive){slot=b;break;}
        if(slot<0){complete=false;break;}
        tV05FireSlots[i]=slot;tV05FireSavedBuildings[i]=gBuildings[slot];Building value;
        value.used=true;value.alive=true;value.kind=Kind_Turret;value.uid=++gUID;value.file=C_Turrets;value.level=1;value.core=-1;
        value.buildCore=core;value.powerCore=core;value.gridAssigned=true;value.configRevision=gConfigRevision;value.ref=INVALID_ENT_REFERENCE;value.effectRef=INVALID_ENT_REFERENCE;
        float radians=DegToRad(float(i)*36.0);value.origin=center;value.origin[0]+=Cosine(radians)*240.0;value.origin[1]+=Sine(radians)*240.0;
        value.placementAnchor=value.origin;value.angles[1]=float(i)*36.0+180.0;strcopy(value.type,sizeof(value.type),type);strcopy(value.model,sizeof(value.model),model);
        ReadTypeMetadata(C_Turrets,type,value.meta);gBuildings[slot]=value;tV05FireUIDs[i]=value.uid;AssignBuildingOwner(slot,client);
        if(TR_PointOutsideWorld(value.origin) || !BuildAreaContains(core,value.placementAnchor) || !CanAllocateEntities(1,true)
            || !ApplyBuildingLevel(slot,true) || !SpawnBuildingBody(slot)){complete=false;break;}
        InvalidateDefenseCache(slot);
    }
    float targetPoint[3];EntityCenter(entity,targetPoint);
    for(int i=0;i<10 && complete;i++)
    {int b=tV05FireSlots[i];float muzzle[3];BuildingAttachmentPosition(b,gBuildings[b].meta.muzzleOffset,muzzle);if(!LineClear(muzzle,targetPoint,Resolve(gBuildings[b].ref),entity))complete=false;}
    if(!complete){tSkipped++;PrintToServer("[ShelterTest] SKIP real fire: ten valid in-map static positions or all target lines of sight were unavailable");V05RealFireCleanup(client);return;}
    RecalculatePower();tV05FireStarted=GetGameTime();tV05FireActive=true;gDefenseTrace=true;
    PrintToServer("[V05RealFire] READY existing_native_ref=%d native_registry_count=%d groups=10 damage=1 intervals=0.1..1.0 target_hp=1000000; wait >=10 seconds",gNPCs[npc].ref,Han_GetZombieCount());
    PrintToServer("[V05RealFire] Cadence fixtures are static, in-map and inside the build rectangle above the roof; this does not assert normal ground placement.");
}

void V05RealFireVerify(int client)
{
    if(client<1)return;
    if(!tV05FireActive){tSkipped++;PrintToServer("[ShelterTest] SKIP real fire verify without successful setup");return;}
    float elapsed=GetGameTime()-tV05FireStarted;Check(elapsed>=9.5,"real shared timer fire fixture ran for at least 9.5 seconds");
    for(int i=0;i<10;i++)
    {
        int shots=tV05FireCount[i],limit=shots<256 ? shots : 256;float interval=float(i+1)*0.1,minimum=9999.0,maximum,mean;bool cadence=shots>=2;
        for(int j=1;j<limit;j++)
        {
            float delta=tV05FireTimes[i][j]-tV05FireTimes[i][j-1];mean+=delta;if(delta<minimum)minimum=delta;if(delta>maximum)maximum=delta;
            if(delta+0.035<interval || delta>interval+0.2)cadence=false;
        }
        if(limit>1)mean/=float(limit-1);
        if(i==0 && mean>=0.2)cadence=false;
        char assertion[192];Format(assertion,sizeof(assertion),"real native-NPC firing group %.1fs produces independent shots with bounded actual timer deltas",interval);Check(cadence,assertion);
        PrintToServer("[V05RealFire] group=%s uid=%d interval=%.3f shots=%d min_delta=%.6f max_delta=%.6f mean_delta=%.6f elapsed=%.3f",gBuildings[tV05FireSlots[i]].type,tV05FireUIDs[i],interval,shots,minimum,maximum,mean,elapsed);
    }
    int entity=Resolve(gNPCs[tV05FireNPC].ref);Check(entity>0 && GetEntProp(entity,Prop_Data,"m_iHealth")<1000000 && GetEntProp(entity,Prop_Data,"m_iHealth")>0,
        "real scheduled tower shots damage the existing native NPC while preserving its live entity");
}

bool V05RealFireCommand(const char[] which)
{
    if(!StrEqual(which,"v05_fire_setup") && !StrEqual(which,"v05_fire_verify") && !StrEqual(which,"v05_fire_cleanup"))return false;
    int client=V05RealClient();if(client<1)return true;
    if(StrEqual(which,"v05_fire_setup"))V05RealFireSetup(client);
    else if(StrEqual(which,"v05_fire_verify"))V05RealFireVerify(client);
    else V05RealFireCleanup(client);
    return true;
}
