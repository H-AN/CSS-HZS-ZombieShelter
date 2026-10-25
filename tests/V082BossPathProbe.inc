// DIAGNOSTIC MAIN ONLY. Never include this file in a release build.
// Include after all production includes. RegisterV082BossPathProbe(true) is
// allowed only when the diagnostic source also injects these three guards:
//   DamageBuilding entry: if(V082BossPathBlockBuildingDamage(b))return;
//   Han_OnZombieDamagePre end: V082BossPathFilterDamage(zombie,attacker,damage);
//   NPCTick entry: V082BossPathBeforeNPCTick(now);
// Call V082BossPathShutdown() FIRST in the diagnostic OnPluginEnd.
// This fixture never creates, kills, respawns, or revives any NPC or player.
// Only prepare/cleanup move Bosses. During a measured chase the Boss is read
// only: no velocity, move type, target, path, or animation writes are made.

enum V082BPStage { V082BP_None, V082BP_AwaitHit, V082BP_AwaitReturn, V082BP_Chasing, V082BP_Finished }

enum struct V082BPEntitySnapshot
{
    bool saved;
    int ref;
    int visualRef;
    int health;
    int takeDamage;
    int solidFlags;
    int flags;
    MoveType movement;
    RenderMode render;
    RenderMode visualRender;
    int color[4];
    int visualColor[4];
    float origin[3];
    float angles[3];
    float velocity[3];
    int leaderRef;
    int leaderSerial;
}

NPC tV082BPNPC[MAX_NPCS];
BossDef tV082BPBoss[MAX_BOSSES];
V082BPEntitySnapshot tV082BPEntities[MAX_NPCS];
bool tV082BPActive,tV082BPGuards,tV082BPExposure,tV082BPHitObserved;
int tV082BPSerial,tV082BPEpoch,tV082BPRevision,tV082BPBossCount,tV082BPScene;
int tV082BPDay;
Phase tV082BPPhase;
int tV082BPNpc=-1,tV082BPBossIndex=-1,tV082BPPoint=-1,tV082BPRef=INVALID_ENT_REFERENCE,tV082BPVisualRef=INVALID_ENT_REFERENCE;
int tV082BPPreparedHP,tV082BPHitCount,tV082BPSavedHP,tV082BPSavedArmor,tV082BPSavedDamage,tV082BPSavedGod;
int tV082BPReturnedCore=-1,tV082BPPassed,tV082BPFailed,tV082BPBuildingBlocks,tV082BPDamageBlocks;
int tV082BPResult[MAX_BOSSES][BOSS_SPAWN_POINT_COUNT]; // 0 untested, 1 pass, -1 fail.
int tV082BPBuildingUID[MAX_BUILDINGS],tV082BPBuildingRef[MAX_BUILDINGS],tV082BPBuildingGeneration[MAX_BUILDINGS],tV082BPBuildingHP[MAX_BUILDINGS];
MoveType tV082BPPlayerMove;
float tV082BPPlayerOrigin[3],tV082BPPlayerAngles[3],tV082BPPlayerVelocity[3];
float tV082BPStarted,tV082BPPreparedAt,tV082BPChaseAt,tV082BPTimeout,tV082BPNextSample,tV082BPLastProgressAt;
float tV082BPDeadlineRemaining,tV082BPAdmissionsRemaining,tV082BPExposureRemaining;
float tV082BPNextFallback[MAX_NPCS];
float tV082BPStartOrigin[3],tV082BPLastOrigin[3],tV082BPProgressOrigin[3],tV082BPTravel;
V082BPStage tV082BPStage;
Handle tV082BPTimer;
int tV082BPObservedEpoch=-1,tV082BPObservedPhase=-1,tV082BPObservedDeaths[MAX_BOSSES],tV082BPObservedAwake[MAX_BOSSES],tV082BPObservedBound[MAX_BOSSES];

void V082BPLog(const char[] row)
{
    PrintToServer("[V082BossPath] %s",row);
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"logs/v082_boss_path_probe.jsonl");
    File file=OpenFile(path,"a");if(file!=null){file.WriteLine("%s",row);delete file;}
}

void V082BPJSON(const char[] source,char[] result,int size)
{
    int out;
    for(int i=0;source[i] && out<size-1;i++)
    {
        if(source[i]=='"' || source[i]=='\\'){if(out>=size-2)break;result[out++]='\\';}
        if(source[i]<32)result[out++]=' ';else result[out++]=source[i];
    }
    result[out]='\0';
}

void V082BPEvent(const char[] kind,const char[] reason)
{
    char row[768],escaped[384];V082BPJSON(reason,escaped,sizeof(escaped));
    Format(row,sizeof(row),"{\"kind\":\"%s\",\"time\":%d,\"game_time\":%.3f,\"epoch\":%d,\"scene\":%d,\"diagnostic\":true,\"reason\":\"%s\"}",kind,GetTime(),GetGameTime(),gEpoch,tV082BPScene,escaped);V082BPLog(row);
}

int V082BPHuman()
{
    int actor,humans,fakes;
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client))
    {
        if(IsFakeClient(client)){fakes++;continue;}
        if(IsClientSourceTV(client) || IsClientReplay(client))continue;
        humans++;if(LiveHuman(client))actor=client;
    }
    return humans==1 && fakes==0 ? actor : 0;
}

bool V082BPSession(int client)
{
    return tV082BPActive && client>0 && V082BPHuman()==client && GetClientSerial(client)==tV082BPSerial
        && gEpoch==tV082BPEpoch && gConfigRevision==tV082BPRevision && gBossCount==tV082BPBossCount
        && gDay==tV082BPDay && gPhase==tV082BPPhase && Running() && !gTerminal;
}

bool V082BPNPCIdentity(int n)
{
    return n>=0 && n<MAX_NPCS && tV082BPEntities[n].saved && gNPCs[n].used && gNPCs[n].ready
        && gNPCs[n].registrationEpoch==tV082BPEpoch && gNPCs[n].ref==tV082BPEntities[n].ref
        && gNPCs[n].visualRef==tV082BPEntities[n].visualRef && gNPCs[n].boss==tV082BPNPC[n].boss
        && (tV082BPEntities[n].visualRef==INVALID_ENT_REFERENCE || tV082BPEntities[n].visualRef==0 || Resolve(tV082BPEntities[n].visualRef)>0)
        && Resolve(gNPCs[n].ref)>0;
}

bool V082BPCurrentBoss()
{
    int n=tV082BPNpc,b=tV082BPBossIndex;
    return V082BPNPCIdentity(n) && b>=0 && b<gBossCount && gNPCs[n].boss==b && !gNPCs[n].dead
        && !gBosses[b].defeated && gBosses[b].boundThisRound && gNPCs[n].ref==tV082BPRef
        && gNPCs[n].visualRef==tV082BPVisualRef && StrEqual(gBosses[b].id,tV082BPBoss[b].id)
        && GetEntProp(Resolve(tV082BPRef),Prop_Data,"m_iHealth")>0;
}

// These guards are called only by a temporary, explicitly injected diagnostic
// main. Building immunity must guard logical DamageBuilding, not just props.
bool V082BossPathBlockBuildingDamage(int b)
{
    if(!tV082BPActive || gEpoch!=tV082BPEpoch || b<0 || b>=MAX_BUILDINGS)return false;
    if(tV082BPBuildingUID[b]!=0 && gBuildings[b].used && gBuildings[b].alive
        && gBuildings[b].uid==tV082BPBuildingUID[b] && gBuildings[b].ref==tV082BPBuildingRef[b]
        && gBuildings[b].generation==tV082BPBuildingGeneration[b])
    {tV082BPBuildingBlocks++;return true;}
    return false;
}

void V082BossPathBeforeNPCTick(float now)
{
    if(tV082BPActive && gEpoch==tV082BPEpoch && gConfigRevision==tV082BPRevision)
        gNextAdmissions=now+86400.0;
}

void V082BossPathFilterDamage(int zombie,int attacker,int &damage)
{
    if(!tV082BPActive || gEpoch!=tV082BPEpoch || damage<=0)return;
    int n=FindNPC(zombie),actor=GetClientFromSerial(tV082BPSerial);
    if(n<0)return;
    if(tV082BPStage!=V082BP_AwaitHit || !V082BPSession(actor) || !V082BPCurrentBoss()
        || n!=tV082BPNpc || attacker!=actor || gDamageNPCRef==gNPCs[n].ref)
    {damage=0;tV082BPDamageBlocks++;return;}
    int hp=GetEntProp(zombie,Prop_Data,"m_iHealth"),requested=damage;
    if(damage>=hp)damage=hp-1;
    if(damage<1){damage=0;tV082BPDamageBlocks++;return;}
    tV082BPHitCount++;
    char row[768],weaponName[96];int weapon=GetEntPropEnt(actor,Prop_Send,"m_hActiveWeapon");
    if(weapon>MaxClients && IsValidEntity(weapon))GetEntityClassname(weapon,weaponName,sizeof(weaponName));
    Format(row,sizeof(row),"{\"kind\":\"actor_damage_observed\",\"time\":%d,\"game_time\":%.3f,\"scene\":%d,\"boss_ref\":%d,\"actor_serial\":%d,\"actor_userid\":%d,\"buttons\":%d,\"weapon\":\"%s\",\"requested_damage\":%d,\"nonlethal_damage\":%d,\"hp_before\":%d,\"diagnostic_nonlethal_guard\":true}",
        GetTime(),GetGameTime(),tV082BPScene,tV082BPRef,tV082BPSerial,GetClientUserId(actor),GetClientButtons(actor),weaponName,requested,damage,hp);V082BPLog(row);
}

void V082BPFinish(bool passed,const char[] reason)
{
    if(tV082BPStage==V082BP_None || tV082BPStage==V082BP_Finished)return;
    tV082BPStage=V082BP_Finished;if(passed)tV082BPPassed++;else tV082BPFailed++;
    if(tV082BPBossIndex>=0 && tV082BPPoint>=0)tV082BPResult[tV082BPBossIndex][tV082BPPoint]=passed ? 1 : -1;
    char row[1024],escaped[384],id[128];V082BPJSON(reason,escaped,sizeof(escaped));
    if(tV082BPBossIndex>=0 && tV082BPBossIndex<gBossCount)V082BPJSON(gBosses[tV082BPBossIndex].id,id,sizeof(id));
    Format(row,sizeof(row),"{\"kind\":\"scene_result\",\"time\":%d,\"game_time\":%.3f,\"scene\":%d,\"boss_id\":\"%s\",\"boss_ref\":%d,\"point\":%d,\"passed\":%s,\"reason\":\"%s\",\"travel\":%.3f,\"chase_seconds\":%.3f,\"hit_count\":%d,\"diagnostic\":true}",
        GetTime(),GetGameTime(),tV082BPScene,id,tV082BPRef,tV082BPPoint+1,passed ? "true" : "false",escaped,tV082BPTravel,tV082BPChaseAt>0.0 ? GetGameTime()-tV082BPChaseAt : 0.0,tV082BPHitCount);V082BPLog(row);
}

void V082BPSample(const char[] kind)
{
    if(!V082BPCurrentBoss())return;
    int actor=GetClientFromSerial(tV082BPSerial),entity=Resolve(tV082BPRef),b=tV082BPBossIndex,n=tV082BPNpc;
    float origin[3],velocity[3],player[3];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",origin);GetEntPropVector(entity,Prop_Data,"m_vecAbsVelocity",velocity);
    if(actor>0 && IsClientInGame(actor))GetClientAbsOrigin(actor,player);
    int target=gLeaderOffset>=0 ? GetEntDataEnt2(entity,gLeaderOffset) : -1;
    char row[2048],id[128];V082BPJSON(gBosses[b].id,id,sizeof(id));
    Format(row,sizeof(row),"{\"kind\":\"%s\",\"time\":%d,\"game_time\":%.3f,\"epoch\":%d,\"scene\":%d,\"stage\":%d,\"boss_id\":\"%s\",\"point\":%d,\"npc_slot\":%d,\"body_ref\":%d,\"visual_ref\":%d,\"origin\":[%.4f,%.4f,%.4f],\"velocity\":[%.3f,%.3f,%.3f],\"health\":%d,\"saved_hp\":%d,\"awakened\":%d,\"defeated\":%d,\"active\":%d,\"paused\":%d,\"move_type\":%d,\"target_entity\":%d,\"target_client_serial\":%d,\"attacking_uid\":%d,\"safe_core\":%d,\"returned_core\":%d,\"player_safe_core\":%d,\"player_origin\":[%.3f,%.3f,%.3f],\"player_distance\":%.3f,\"distance_from_start\":%.3f,\"travel\":%.3f,\"chase_seconds\":%.3f,\"seconds_since_progress\":%.3f,\"diagnostic\":true}",
        kind,GetTime(),GetGameTime(),gEpoch,tV082BPScene,tV082BPStage,id,tV082BPPoint+1,n,tV082BPRef,tV082BPVisualRef,
        origin[0],origin[1],origin[2],velocity[0],velocity[1],velocity[2],GetEntProp(entity,Prop_Data,"m_iHealth"),gBosses[b].savedHP,gBosses[b].awakened,gBosses[b].defeated,gNPCs[n].active,gNPCs[n].paused,GetEntityMoveType(entity),target,gNPCs[n].targetClientSerial,gNPCs[n].attackingUID,SafeCoreAt(origin),tV082BPReturnedCore,actor>0 ? SafeCoreAt(player) : -1,
        player[0],player[1],player[2],GetVectorDistance(origin,player),GetVectorDistance(origin,tV082BPStartOrigin),tV082BPTravel,tV082BPChaseAt>0.0 ? GetGameTime()-tV082BPChaseAt : 0.0,GetGameTime()-tV082BPLastProgressAt);V082BPLog(row);
    for(int c=0;c<gCoreCount;c++)
    {
        Format(row,sizeof(row),"{\"kind\":\"base_distance\",\"game_time\":%.3f,\"scene\":%d,\"core\":%d,\"alive\":%d,\"distance_to_core_origin\":%.3f}",GetGameTime(),tV082BPScene,c,BuildCoreAlive(c),GetVectorDistance(origin,gCores[c].origin));V082BPLog(row);
    }
}

bool V082BPTraceFilter(int entity,int mask,any ignored)
{
    if(entity==ignored)return false;
    return BossPlacementTraceFilter(entity,mask,ignored);
}

bool V082BPFloor(const float origin[3],float bottomOffset,int ignored,float floor[3],float &gap,int &hit,float &normalZ)
{
    float start[3],end[3],normal[3];start=origin;start[2]+=bottomOffset+2.0;end=start;end[2]-=74.0;
    Handle trace=TR_TraceRayFilterEx(start,end,MASK_NPCSOLID,RayType_EndPoint,V082BPTraceFilter,ignored);
    bool good=TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);hit=TR_GetEntityIndex(trace);
    if(good){TR_GetEndPosition(floor,trace);TR_GetPlaneNormal(trace,normal);gap=origin[2]+bottomOffset-floor[2];normalZ=normal[2];}
    delete trace;return good && (hit==0 || hit>MaxClients) && normalZ>=0.7 && gap>=-0.125 && gap<=4.0;
}

bool V082BPCheckBossPoint(int entity,int point)
{
    float mins[3],maxs[3],floor[3],gap,normalZ;NPCBounds(entity,mins,maxs);int hit;
    bool hull=BossSpawnHullClear(gBossSpawnOrigins[point],mins,maxs,entity);
    bool supported=V082BPFloor(gBossSpawnOrigins[point],mins[2],entity,floor,gap,hit,normalZ);
    char row[1024];Format(row,sizeof(row),"{\"kind\":\"point_geometry\",\"time\":%d,\"scene\":%d,\"point\":%d,\"body_ref\":%d,\"origin\":[%.6f,%.6f,%.6f],\"mins\":[%.3f,%.3f,%.3f],\"maxs\":[%.3f,%.3f,%.3f],\"real_hull_clear\":%d,\"real_supported_floor\":%d,\"floor\":[%.6f,%.6f,%.6f],\"support_gap\":%.6f,\"support_normal_z\":%.6f,\"support_entity\":%d,\"point_was_adjusted\":false}",
        GetTime(),tV082BPScene,point+1,EntIndexToEntRef(entity),gBossSpawnOrigins[point][0],gBossSpawnOrigins[point][1],gBossSpawnOrigins[point][2],mins[0],mins[1],mins[2],maxs[0],maxs[1],maxs[2],hull,supported,floor[0],floor[1],floor[2],gap,normalZ,hit);V082BPLog(row);
    return hull && supported;
}

bool V082BPAttackPoint(int client,int entity)
{
    float mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0},boss[3],low[3],high[3],zero[3];
    GetEntPropVector(entity,Prop_Data,"m_vecOrigin",boss);NPCBounds(entity,low,high);
    for(int ring=0;ring<6;ring++)for(int direction=0;direction<16;direction++)
    {
        float feet[3],start[3],end[3],angle=DegToRad(float(direction)*22.5);start=boss;
        start[0]+=Cosine(angle)*(80.0+float(ring)*16.0);start[1]+=Sine(angle)*(80.0+float(ring)*16.0);start[2]+=96.0;
        end=start;end[2]-=224.0;Handle trace=TR_TraceRayFilterEx(start,end,MASK_PLAYERSOLID,RayType_EndPoint,V082BPTraceFilter,client);
        bool floor=TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);float normal[3];
        if(floor){TR_GetEndPosition(feet,trace);TR_GetPlaneNormal(trace,normal);floor=normal[2]>=0.8;}delete trace;
        if(!floor)continue;feet[2]+=1.0;if(!ClearHull(feet,mins,maxs,client,MASK_PLAYERSOLID))continue;
        float eye[3],target[3],vector[3],angles[3];TeleportEntity(client,feet,NULL_VECTOR,zero);GetClientEyePosition(client,eye);
        target=boss;target[2]+=(low[2]+high[2])*0.5;MakeVectorFromPoints(eye,target,vector);GetVectorAngles(vector,angles);TeleportEntity(client,NULL_VECTOR,angles,zero);
        int aimed=AimedEntity(client,256.0);
        if(aimed==entity || (Resolve(tV082BPVisualRef)>0 && aimed==Resolve(tV082BPVisualRef)))
        {SetEntityMoveType(client,MOVETYPE_NONE);return true;}
    }
    return false;
}

void V082BPReadState()
{
    int actor=V082BPHuman();char row[1536];
    Format(row,sizeof(row),"{\"kind\":\"state\",\"time\":%d,\"game_time\":%.3f,\"active\":%d,\"guards_injected\":%d,\"stage\":%d,\"epoch\":%d,\"config_revision\":%d,\"phase\":%d,\"day\":%d,\"running\":%d,\"terminal\":%d,\"boss_count\":%d,\"actor_userid\":%d,\"actor_serial\":%d,\"exposure_enabled\":%d,\"deadline\":%.3f,\"passed_attempts\":%d,\"failed_attempts\":%d,\"building_damage_blocks\":%d,\"npc_damage_blocks\":%d}",
        GetTime(),GetGameTime(),tV082BPActive,tV082BPGuards,tV082BPStage,gEpoch,gConfigRevision,gPhase,gDay,Running(),gTerminal,gBossCount,actor>0 ? GetClientUserId(actor) : 0,actor>0 ? GetClientSerial(actor) : 0,gExposureEnabled,gDeadline,tV082BPPassed,tV082BPFailed,tV082BPBuildingBlocks,tV082BPDamageBlocks);V082BPLog(row);
    for(int point=0;point<BOSS_SPAWN_POINT_COUNT;point++)
    {
        Format(row,sizeof(row),"{\"kind\":\"configured_point\",\"point\":%d,\"origin\":[%.6f,%.6f,%.6f],\"angles\":[%.6f,%.6f,%.6f]}",point+1,gBossSpawnOrigins[point][0],gBossSpawnOrigins[point][1],gBossSpawnOrigins[point][2],gBossSpawnAngles[point][0],gBossSpawnAngles[point][1],gBossSpawnAngles[point][2]);V082BPLog(row);
    }
    for(int b=0;b<gBossCount;b++)
    {
        int n=-1;for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && gNPCs[i].boss==b){n=i;break;}
        int entity=n>=0 ? Resolve(gNPCs[n].ref) : -1;float origin[3];if(entity>0)GetEntPropVector(entity,Prop_Data,"m_vecOrigin",origin);
        char id[128],name[128];V082BPJSON(gBosses[b].id,id,sizeof(id));V082BPJSON(gBosses[b].publicName,name,sizeof(name));
        Format(row,sizeof(row),"{\"kind\":\"boss_state\",\"index\":%d,\"id\":\"%s\",\"public_name\":\"%s\",\"npc_slot\":%d,\"body_ref\":%d,\"health\":%d,\"saved_hp\":%d,\"awakened\":%d,\"defeated\":%d,\"bound\":%d,\"active\":%d,\"dead\":%d,\"origin\":[%.6f,%.6f,%.6f],\"point_results\":[%d,%d,%d,%d,%d]}",
            b,id,name,n,n>=0 ? gNPCs[n].ref : INVALID_ENT_REFERENCE,entity>0 ? GetEntProp(entity,Prop_Data,"m_iHealth") : 0,gBosses[b].savedHP,gBosses[b].awakened,gBosses[b].defeated,gBosses[b].boundThisRound,n>=0 && gNPCs[n].active,n>=0 && gNPCs[n].dead,origin[0],origin[1],origin[2],tV082BPResult[b][0],tV082BPResult[b][1],tV082BPResult[b][2],tV082BPResult[b][3],tV082BPResult[b][4]);V082BPLog(row);
    }
    for(int core=0;core<gCoreCount;core++)
    {
        int b=gCores[core].building;Format(row,sizeof(row),"{\"kind\":\"base_state\",\"core\":%d,\"alive\":%d,\"building\":%d,\"uid\":%d,\"body_ref\":%d,\"health\":%d,\"safe_point_count\":%d}",core,BuildCoreAlive(core),b,b>=0 ? gBuildings[b].uid : 0,b>=0 ? gBuildings[b].ref : INVALID_ENT_REFERENCE,b>=0 ? gBuildings[b].hp : 0,gCores[core].pointCount);V082BPLog(row);
        for(int point=0;point<gCores[core].pointCount;point++)
        {Format(row,sizeof(row),"{\"kind\":\"base_safe_point\",\"core\":%d,\"point\":%d,\"origin\":[%.6f,%.6f,%.6f]}",core,point,gCorePoints[core][point][0],gCorePoints[core][point][1],gCorePoints[core][point][2]);V082BPLog(row);}
    }
    if(actor>0)
    {
        Format(row,sizeof(row),"{\"kind\":\"actor_attributes\",\"userid\":%d,\"serial\":%d,\"health\":%d,\"armor\":%d,\"gravity\":%.6f,\"speed_multiplier\":%.6f,\"move_type\":%d,\"god\":%d,\"takedamage\":%d,\"skills_revision\":%d}",GetClientUserId(actor),GetClientSerial(actor),GetClientHealth(actor),GetEntProp(actor,Prop_Send,"m_ArmorValue"),GetEntityGravity(actor),GetEntPropFloat(actor,Prop_Send,"m_flLaggedMovementValue"),GetEntityMoveType(actor),!!(GetEntityFlags(actor)&FL_GODMODE),GetEntProp(actor,Prop_Data,"m_takedamage"),SkillsAccountRevision(actor));V082BPLog(row);
        for(int skill=0;skill<SKILL_COUNT;skill++)
        {Format(row,sizeof(row),"{\"kind\":\"actor_skill\",\"id\":\"%s\",\"level\":%d,\"amount\":%.6f}",gSkillID[skill],SkillsLevel(actor,skill),gSkillAmount[skill]);V082BPLog(row);}
    }
}

bool V082BPBegin(int client)
{
    if(tV082BPActive){V082BPEvent("rejected","fixture_already_active");return false;}
    if(!tV082BPGuards || client<1 || !Running() || gTerminal || gBossCount<1 || gLeaderOffset<0)
    {V082BPEvent("rejected","begin_requires_three_injected_guards_one_existing_living_real_human_running_and_native_target_offset");return false;}
    if(!(GetUserFlagBits(client)&ADMFLAG_ROOT)){V082BPEvent("rejected","unique_existing_real_actor_requires_root_admin");return false;}
    if(gNextBossAudit>GetGameTime()){V082BPEvent("rejected","wait_until_native_initial_boss_placement_and_three_second_audit_have_settled");return false;}
    if(GetEntityMoveType(client)==MOVETYPE_NOCLIP){V082BPEvent("rejected","actor_must_disable_noclip_before_fixture_snapshot");return false;}
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].ready && !gNPCs[n].dead
        && (gNPCs[n].burnUntil>GetGameTime() || ExpansionNPCFrozen(n,GetGameTime())))
    {V082BPEvent("rejected","wait_for_existing_burn_or_freeze_to_expire_before_parking_no_effect_state_is_erased");return false;}
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].ready && !gNPCs[n].dead)
    {
        int entity=Resolve(gNPCs[n].ref),visual=Resolve(gNPCs[n].visualRef);
        if((entity>0 && BurnVisualNativeFire(entity)) || (visual>0 && BurnVisualNativeFire(visual)))
        {V082BPEvent("rejected","wait_for_existing_native_fire_effect_to_expire_before_snapshot");return false;}
    }
    int livingBosses;
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].boss>=0 && gNPCs[n].ready && !gNPCs[n].dead)
    {int entity=Resolve(gNPCs[n].ref);if(entity>0 && GetEntProp(entity,Prop_Data,"m_iHealth")>1 && !gBosses[gNPCs[n].boss].defeated)livingBosses++;}
    if(livingBosses!=gBossCount){V082BPEvent("rejected","all_configured_boss_instances_must_already_exist_alive_and_unsettled_no_respawn_allowed");return false;}
    V082BPReadState();tV082BPSerial=GetClientSerial(client);tV082BPEpoch=gEpoch;tV082BPRevision=gConfigRevision;tV082BPBossCount=gBossCount;tV082BPDay=gDay;tV082BPPhase=gPhase;
    tV082BPStarted=GetGameTime();tV082BPDeadlineRemaining=gDeadline-tV082BPStarted;tV082BPAdmissionsRemaining=gNextAdmissions-tV082BPStarted;tV082BPExposureRemaining=gNextExposure-tV082BPStarted;
    tV082BPExposure=gExposureEnabled;tV082BPSavedHP=GetClientHealth(client);tV082BPSavedArmor=GetEntProp(client,Prop_Send,"m_ArmorValue");
    tV082BPSavedDamage=GetEntProp(client,Prop_Data,"m_takedamage");tV082BPSavedGod=GetEntityFlags(client)&FL_GODMODE;tV082BPPlayerMove=GetEntityMoveType(client);
    GetClientAbsOrigin(client,tV082BPPlayerOrigin);GetClientEyeAngles(client,tV082BPPlayerAngles);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",tV082BPPlayerVelocity);
    char playerRow[1024];Format(playerRow,sizeof(playerRow),"{\"kind\":\"player_snapshot\",\"time\":%d,\"serial\":%d,\"userid\":%d,\"epoch\":%d,\"origin\":[%.6f,%.6f,%.6f],\"angles\":[%.6f,%.6f,%.6f],\"velocity\":[%.6f,%.6f,%.6f],\"health\":%d,\"armor\":%d,\"takedamage\":%d,\"god_flag\":%d,\"move_type\":%d,\"deadline_remaining\":%.6f,\"exposure_enabled\":%d}",
        GetTime(),tV082BPSerial,GetClientUserId(client),gEpoch,tV082BPPlayerOrigin[0],tV082BPPlayerOrigin[1],tV082BPPlayerOrigin[2],tV082BPPlayerAngles[0],tV082BPPlayerAngles[1],tV082BPPlayerAngles[2],tV082BPPlayerVelocity[0],tV082BPPlayerVelocity[1],tV082BPPlayerVelocity[2],tV082BPSavedHP,tV082BPSavedArmor,tV082BPSavedDamage,tV082BPSavedGod,tV082BPPlayerMove,tV082BPDeadlineRemaining,tV082BPExposure);V082BPLog(playerRow);
    tV082BPScene=0;tV082BPPassed=0;tV082BPFailed=0;tV082BPBuildingBlocks=0;tV082BPDamageBlocks=0;
    for(int b=0;b<MAX_BOSSES;b++){if(b<gBossCount)tV082BPBoss[b]=gBosses[b];for(int p=0;p<BOSS_SPAWN_POINT_COUNT;p++)tV082BPResult[b][p]=0;}
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        tV082BPBuildingUID[b]=0;
        if(!gBuildings[b].used || !gBuildings[b].alive)continue;
        tV082BPBuildingUID[b]=gBuildings[b].uid;tV082BPBuildingRef[b]=gBuildings[b].ref;tV082BPBuildingGeneration[b]=gBuildings[b].generation;tV082BPBuildingHP[b]=gBuildings[b].hp;
    }
    for(int n=0;n<MAX_NPCS;n++)
    {
        V082BPEntitySnapshot snapshot;tV082BPEntities[n]=snapshot;
        if(!gNPCs[n].used || !gNPCs[n].ready || gNPCs[n].dead)continue;
        int entity=Resolve(gNPCs[n].ref),visual=Resolve(gNPCs[n].visualRef);if(entity<0 || GetEntProp(entity,Prop_Data,"m_iHealth")<=0)continue;
        tV082BPNPC[n]=gNPCs[n];tV082BPNextFallback[n]=gNextTargetFallback[n];snapshot.saved=true;snapshot.ref=gNPCs[n].ref;snapshot.visualRef=gNPCs[n].visualRef;snapshot.health=GetEntProp(entity,Prop_Data,"m_iHealth");
        snapshot.takeDamage=GetEntProp(entity,Prop_Data,"m_takedamage");snapshot.solidFlags=GetEntProp(entity,Prop_Send,"m_usSolidFlags");snapshot.flags=GetEntityFlags(entity);snapshot.movement=GetEntityMoveType(entity);snapshot.render=GetEntityRenderMode(entity);
        GetEntityRenderColor(entity,snapshot.color[0],snapshot.color[1],snapshot.color[2],snapshot.color[3]);GetEntPropVector(entity,Prop_Data,"m_vecOrigin",snapshot.origin);GetEntPropVector(entity,Prop_Data,"m_angRotation",snapshot.angles);GetEntPropVector(entity,Prop_Data,"m_vecAbsVelocity",snapshot.velocity);
        if(visual>0){snapshot.visualRender=GetEntityRenderMode(visual);GetEntityRenderColor(visual,snapshot.visualColor[0],snapshot.visualColor[1],snapshot.visualColor[2],snapshot.visualColor[3]);}
        int leader=GetEntDataEnt2(entity,gLeaderOffset);snapshot.leaderRef=INVALID_ENT_REFERENCE;
        if(leader>0 && leader<=MaxClients && IsClientInGame(leader))snapshot.leaderSerial=GetClientSerial(leader);
        else if(leader>MaxClients && IsValidEntity(leader))snapshot.leaderRef=EntIndexToEntRef(leader);
        tV082BPEntities[n]=snapshot;
        char row[1536];Format(row,sizeof(row),"{\"kind\":\"existing_npc_snapshot\",\"time\":%d,\"epoch\":%d,\"npc_slot\":%d,\"body_ref\":%d,\"visual_ref\":%d,\"boss_index\":%d,\"health\":%d,\"origin\":[%.6f,%.6f,%.6f],\"angles\":[%.6f,%.6f,%.6f],\"velocity\":[%.6f,%.6f,%.6f],\"move_type\":%d,\"active\":%d,\"paused\":%d,\"takedamage\":%d,\"solid_flags\":%d,\"entity_flags\":%d,\"leader_ref\":%d,\"leader_serial\":%d}",
            GetTime(),gEpoch,n,snapshot.ref,snapshot.visualRef,gNPCs[n].boss,snapshot.health,snapshot.origin[0],snapshot.origin[1],snapshot.origin[2],snapshot.angles[0],snapshot.angles[1],snapshot.angles[2],snapshot.velocity[0],snapshot.velocity[1],snapshot.velocity[2],snapshot.movement,gNPCs[n].active,gNPCs[n].paused,snapshot.takeDamage,snapshot.solidFlags,snapshot.flags,snapshot.leaderRef,snapshot.leaderSerial);V082BPLog(row);
    }
    tV082BPActive=true;tV082BPStage=V082BP_None;tV082BPNpc=-1;tV082BPBossIndex=-1;tV082BPPoint=-1;
    SetEntityFlags(client,GetEntityFlags(client)|FL_GODMODE);SetEntProp(client,Prop_Data,"m_takedamage",0,1);gExposureEnabled=false;
    gDeadline=GetGameTime()+86400.0;gNextAdmissions=GetGameTime()+86400.0;
    for(int n=0;n<MAX_NPCS;n++)if(tV082BPEntities[n].saved)ParkNPC(n);
    V082BPEvent("begin","diagnostic_only_all_existing_npcs_snapshotted_and_parked_player_buildings_protected_exposure_off_campaign_clock_paused_no_inventory_or_skill_changes");return true;
}

void V082BPPrepare(int client,const char[] id,int point,float timeout)
{
    if(!V082BPSession(client)){V082BPEvent("rejected","prepare_requires_original_unique_living_actor_epoch_revision");return;}
    if(tV082BPStage==V082BP_AwaitHit || tV082BPStage==V082BP_AwaitReturn || tV082BPStage==V082BP_Chasing)
    {V082BPEvent("rejected","finish_or_stop_current_scene_before_prepare");return;}
    int b=-1,n=-1;
    for(int i=0;i<gBossCount;i++)if(StrEqual(gBosses[i].id,id)){b=i;break;}
    if(b>=0)for(int i=0;i<MAX_NPCS;i++)if(V082BPNPCIdentity(i) && gNPCs[i].boss==b && !gNPCs[i].dead){n=i;break;}
    if(n<0 || point<0 || point>=BOSS_SPAWN_POINT_COUNT || timeout<10.0 || timeout>300.0)
    {V082BPEvent("rejected","prepare_requires_exact_existing_boss_id_point_1_to_5_timeout_10_to_300");return;}
    int entity=Resolve(gNPCs[n].ref);
    if(gBosses[b].defeated || GetEntProp(entity,Prop_Data,"m_iHealth")<=1)
    {V082BPEvent("rejected","boss_not_alive_or_safe_for_nonlethal_hit_never_revived");return;}
    if(tV082BPNpc>=0 && V082BPNPCIdentity(tV082BPNpc) && !gNPCs[tV082BPNpc].dead)
    {ParkNPC(tV082BPNpc);V082BPEvent("scene_boundary_park","previous_measured_scene_already_finished_existing_boss_parked_before_next_scene");}
    tV082BPScene++;tV082BPNpc=n;tV082BPBossIndex=b;tV082BPPoint=point;tV082BPRef=gNPCs[n].ref;tV082BPVisualRef=gNPCs[n].visualRef;
    tV082BPStage=V082BP_AwaitHit;tV082BPTimeout=timeout;tV082BPPreparedAt=GetGameTime();tV082BPChaseAt=0.0;tV082BPHitCount=0;tV082BPHitObserved=false;tV082BPTravel=0.0;tV082BPReturnedCore=-1;
    if(!V082BPCheckBossPoint(entity,point)){V082BPFinish(false,"actual_boss_hull_or_support_floor_rejected_exact_user_point_no_adjustment");return;}
    if(SafeCoreAt(gBossSpawnOrigins[point])>=0){V082BPFinish(false,"spawn_point_already_inside_base_no_meaningful_route");return;}
    gBosses[b].awakened=false;gNPCs[n].active=true;gNPCs[n].paused=false;gNPCs[n].attackingUID=0;gNPCs[n].targetClientSerial=0;
    gNPCs[n].burnUntil=0.0;gNPCs[n].nextBurn=0.0;gNextTargetFallback[n]=0.0;
    float zero[3];TeleportEntity(entity,gBossSpawnOrigins[point],gBossSpawnAngles[point],zero);
    RestoreNPCBody(n);FreezeNPC(n);tV082BPPreparedHP=GetEntProp(entity,Prop_Data,"m_iHealth");gNPCs[n].lastHP=tV082BPPreparedHP;gBosses[b].savedHP=tV082BPPreparedHP;
    tV082BPStartOrigin=gBossSpawnOrigins[point];tV082BPLastOrigin=tV082BPStartOrigin;tV082BPProgressOrigin=tV082BPStartOrigin;tV082BPLastProgressAt=GetGameTime();tV082BPNextSample=0.0;
    if(!V082BPAttackPoint(client,entity)){V082BPFinish(false,"no_clear_nearby_player_floor_hull_and_actual_attack_aim");return;}
    V082BPEvent("prepare","diagnostic_scene_start_exact_captured_point_dormant_production_freeze_player_positioned_nearby_operator_must_supply_real_client_attack");V082BPSample("prepared");
}

void V082BPReturn(int client,int core)
{
    if(!V082BPSession(client) || !V082BPCurrentBoss() || tV082BPStage!=V082BP_AwaitReturn || !tV082BPHitObserved || !gBosses[tV082BPBossIndex].awakened)
    {V082BPEvent("rejected","return_requires_confirmed_actor_damage_real_hp_drop_and_production_awakened");return;}
    float bossOrigin[3];GetEntPropVector(Resolve(tV082BPRef),Prop_Data,"m_vecOrigin",bossOrigin);
    if(SafeCoreAt(bossOrigin)>=0){V082BPFinish(false,"boss_already_entered_base_before_player_return_repeat_scene_for_requested_sequence");return;}
    if(!TeleportPlayerToBase(client,core)){V082BPEvent("rejected","production_base_teleport_rejected_selected_base_support_hull_or_occupancy");return;}
    SetEntityMoveType(client,MOVETYPE_NONE);
    tV082BPReturnedCore=core;tV082BPStage=V082BP_Chasing;tV082BPChaseAt=GetGameTime();tV082BPLastProgressAt=tV082BPChaseAt;tV082BPNextSample=0.0;
    GetEntPropVector(Resolve(tV082BPRef),Prop_Data,"m_vecOrigin",tV082BPLastOrigin);tV082BPProgressOrigin=tV082BPLastOrigin;
    V082BPEvent("return_to_base","only_real_player_teleported_to_selected_configured_safe_point_and_held_still_boss_motion_target_path_and_animation_not_modified");V082BPSample("chase_started");
}

void V082BPCleanup()
{
    if(!tV082BPActive){V082BPEvent("cleanup","no_active_fixture_no_state_changed");return;}
    V082BPFinish(false,"operator_cleanup_before_scene_finished");
    int actor=GetClientFromSerial(tV082BPSerial),restored,missing;bool sameEpoch=gEpoch==tV082BPEpoch && gConfigRevision==tV082BPRevision && gDay==tV082BPDay && gPhase==tV082BPPhase && Running() && !gTerminal;
    if(sameEpoch)
    {
        // Never replace a dead/missing/reused entity with an old live snapshot.
        for(int n=0;n<MAX_NPCS;n++)if(tV082BPEntities[n].saved)
        {
            if(!V082BPNPCIdentity(n) || gNPCs[n].dead){missing++;continue;}
            int entity=Resolve(gNPCs[n].ref);if(GetEntProp(entity,Prop_Data,"m_iHealth")<=0){missing++;continue;}
            int b=gNPCs[n].boss;if(b>=0 && (!StrEqual(gBosses[b].id,tV082BPBoss[b].id) || gBosses[b].defeated)){missing++;continue;}
            Han_UnlockZombie(entity);Han_SetCustomAnimationEx(entity,tV082BPNPC[n].idle,0.01);
            if(b>=0)gBosses[b]=tV082BPBoss[b];gNPCs[n]=tV082BPNPC[n];gNextTargetFallback[n]=tV082BPNextFallback[n];
            SetEntProp(entity,Prop_Data,"m_iHealth",tV082BPEntities[n].health);SetEntProp(entity,Prop_Data,"m_takedamage",tV082BPEntities[n].takeDamage,1);
            SetEntProp(entity,Prop_Send,"m_usSolidFlags",tV082BPEntities[n].solidFlags);SetEntityFlags(entity,tV082BPEntities[n].flags);SetEntityMoveType(entity,tV082BPEntities[n].movement);
            TeleportEntity(entity,tV082BPEntities[n].origin,tV082BPEntities[n].angles,tV082BPEntities[n].velocity);
            SetEntityRenderMode(entity,tV082BPEntities[n].render);SetEntityRenderColor(entity,tV082BPEntities[n].color[0],tV082BPEntities[n].color[1],tV082BPEntities[n].color[2],tV082BPEntities[n].color[3]);
            int visual=Resolve(tV082BPEntities[n].visualRef);if(visual>0){SetEntityRenderMode(visual,tV082BPEntities[n].visualRender);SetEntityRenderColor(visual,tV082BPEntities[n].visualColor[0],tV082BPEntities[n].visualColor[1],tV082BPEntities[n].visualColor[2],tV082BPEntities[n].visualColor[3]);}
            int leader=tV082BPEntities[n].leaderSerial>0 ? GetClientFromSerial(tV082BPEntities[n].leaderSerial) : Resolve(tV082BPEntities[n].leaderRef);
            SetEntDataEnt2(entity,gLeaderOffset,leader>0 ? leader : entity);
            if(gNPCs[n].paused || !gNPCs[n].active || (b>=0 && !gBosses[b].awakened))FreezeNPC(n);
            restored++;
        }
        float now=GetGameTime();gDeadline=now+tV082BPDeadlineRemaining;gNextAdmissions=now+tV082BPAdmissionsRemaining;gNextExposure=now+tV082BPExposureRemaining;gExposureEnabled=tV082BPExposure;
        for(int b=0;b<MAX_BUILDINGS;b++)if(tV082BPBuildingUID[b]!=0 && gBuildings[b].used && gBuildings[b].uid==tV082BPBuildingUID[b]
            && gBuildings[b].ref==tV082BPBuildingRef[b] && gBuildings[b].generation==tV082BPBuildingGeneration[b] && gBuildings[b].hp!=tV082BPBuildingHP[b])
            V082BPEvent("cleanup_building_hp_changed","protected_building_hp_changed_outside_damage_guard_preserved_for_operator_audit_not_blanket_reset");
    }
    // Exposure belongs to the same loaded configuration, even if an unexpected
    // round invalidated the NPC snapshots. Never apply old deadlines/HP then.
    if(gConfigRevision==tV082BPRevision)gExposureEnabled=tV082BPExposure;
    if(actor>0 && IsClientInGame(actor))
    {
        SetEntProp(actor,Prop_Data,"m_takedamage",tV082BPSavedDamage,1);SetEntityFlags(actor,(GetEntityFlags(actor)&~FL_GODMODE)|tV082BPSavedGod);
        if(GetEntityMoveType(actor)==MOVETYPE_NONE)SetEntityMoveType(actor,tV082BPPlayerMove);
        if(sameEpoch && LiveHuman(actor))
        {SetEntityHealth(actor,tV082BPSavedHP);SetEntProp(actor,Prop_Send,"m_ArmorValue",tV082BPSavedArmor);SetEntityMoveType(actor,tV082BPPlayerMove);TeleportEntity(actor,tV082BPPlayerOrigin,tV082BPPlayerAngles,tV082BPPlayerVelocity);}
    }
    char row[1024];Format(row,sizeof(row),"{\"kind\":\"cleanup\",\"time\":%d,\"game_time\":%.3f,\"same_epoch_revision\":%d,\"restored_existing_npcs\":%d,\"missing_dead_or_changed_instances_not_revived\":%d,\"actor_present\":%d,\"passed_attempts\":%d,\"failed_attempts\":%d,\"native_internal_path_and_lock_duration_restored\":false,\"inventory_or_skills_written\":false,\"diagnostic\":true}",GetTime(),GetGameTime(),sameEpoch,restored,missing,actor>0,tV082BPPassed,tV082BPFailed);V082BPLog(row);
    tV082BPActive=false;tV082BPStage=V082BP_None;tV082BPNpc=-1;tV082BPBossIndex=-1;tV082BPPoint=-1;
    V082BPReadState();
}

void V082BPObserveLifecycle()
{
    if(tV082BPActive)return; // Synthetic dormancy is never natural-round evidence.
    bool changed=gEpoch!=tV082BPObservedEpoch || view_as<int>(gPhase)!=tV082BPObservedPhase;
    if(changed)
    {
        tV082BPObservedEpoch=gEpoch;tV082BPObservedPhase=view_as<int>(gPhase);
        char row[512];Format(row,sizeof(row),"{\"kind\":\"natural_lifecycle_observed\",\"time\":%d,\"game_time\":%.3f,\"epoch\":%d,\"phase\":%d,\"day\":%d,\"running\":%d,\"terminal\":%d,\"boss_count\":%d,\"diagnostic_fixture_active\":false}",GetTime(),GetGameTime(),gEpoch,gPhase,gDay,Running(),gTerminal,gBossCount);V082BPLog(row);
    }
    for(int b=0;b<gBossCount;b++)if(changed || tV082BPObservedDeaths[b]!=view_as<int>(gBosses[b].defeated) || tV082BPObservedAwake[b]!=view_as<int>(gBosses[b].awakened) || tV082BPObservedBound[b]!=view_as<int>(gBosses[b].boundThisRound))
    {
        char row[768],id[128];V082BPJSON(gBosses[b].id,id,sizeof(id));
        Format(row,sizeof(row),"{\"kind\":\"natural_boss_progress_observed\",\"time\":%d,\"game_time\":%.3f,\"epoch\":%d,\"boss_id\":\"%s\",\"awakened\":%d,\"defeated\":%d,\"bound\":%d,\"saved_hp\":%d,\"positioned_hp\":%d,\"fixture_active\":false}",GetTime(),GetGameTime(),gEpoch,id,gBosses[b].awakened,gBosses[b].defeated,gBosses[b].boundThisRound,gBosses[b].savedHP,gBosses[b].positionedHP);V082BPLog(row);
        tV082BPObservedDeaths[b]=view_as<int>(gBosses[b].defeated);tV082BPObservedAwake[b]=view_as<int>(gBosses[b].awakened);tV082BPObservedBound[b]=view_as<int>(gBosses[b].boundThisRound);
    }
}

public Action V082BossPathTimer(Handle timer)
{
    V082BPObserveLifecycle();if(!tV082BPActive)return Plugin_Continue;
    int actor=GetClientFromSerial(tV082BPSerial);float now=GetGameTime();
    if(!V082BPSession(actor)){V082BPFinish(false,"actor_serial_human_count_epoch_revision_running_or_boss_count_changed");V082BPCleanup();return Plugin_Continue;}
    if(tV082BPStage==V082BP_None || tV082BPStage==V082BP_Finished)return Plugin_Continue;
    if(!V082BPCurrentBoss()){V082BPFinish(false,"exact_boss_instance_disappeared_changed_or_died_no_respawn");return Plugin_Continue;}
    int entity=Resolve(tV082BPRef),b=tV082BPBossIndex;float origin[3];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",origin);
    if(tV082BPStage==V082BP_AwaitHit)
    {
        if(tV082BPHitCount==0 && GetVectorDistance(origin,tV082BPStartOrigin)>2.0)
        {V082BPSample("dormant_drift");V082BPFinish(false,"dormant_boss_left_exact_captured_point_before_actor_hit");return Plugin_Continue;}
        if(gBosses[b].awakened)
        {
            if(tV082BPHitCount<1 || GetEntProp(entity,Prop_Data,"m_iHealth")>=tV082BPPreparedHP)
            {V082BPFinish(false,"production_awakened_without_confirmed_actor_hit_and_hp_drop");return Plugin_Continue;}
            tV082BPHitObserved=true;tV082BPStage=V082BP_AwaitReturn;V082BPEvent("awakened","actual_actor_damage_hp_drop_and_production_awakened_observed_no_direct_awaken_write");V082BPSample("awakened");
        }
        else if(now-tV082BPPreparedAt>60.0)V082BPFinish(false,"no_real_hit_awaken_within_60_seconds");
    }
    else if(tV082BPStage==V082BP_AwaitReturn && now-tV082BPPreparedAt>90.0)V082BPFinish(false,"operator_did_not_return_player_to_base_within_90_seconds");
    else if(tV082BPStage==V082BP_Chasing)
    {
        tV082BPTravel+=GetVectorDistance(origin,tV082BPLastOrigin);tV082BPLastOrigin=origin;
        if(GetVectorDistance(origin,tV082BPProgressOrigin)>=16.0){tV082BPProgressOrigin=origin;tV082BPLastProgressAt=now;}
        if(SafeCoreAt(origin)>=0){V082BPSample("arrived_at_any_base");V082BPFinish(true,"existing_boss_physically_entered_any_production_green_safe_zone_after_real_hit_and_player_return");}
        else if(now-tV082BPChaseAt>=tV082BPTimeout){V082BPSample("timed_out");V082BPFinish(false,"timeout_boss_did_not_enter_any_base_actual_motion_is_not_a_path_pass");}
        else if(now-tV082BPLastProgressAt>=30.0){V082BPSample("stuck");V082BPFinish(false,"stuck_no_16_unit_displacement_for_30_seconds");}
    }
    if(now>=tV082BPNextSample){tV082BPNextSample=now+1.0;V082BPSample("sample");}
    return Plugin_Continue;
}

public Action V082BossPathCommand(int client,int args)
{
    char action[64];if(args>0)GetCmdArg(1,action,sizeof(action));else strcopy(action,sizeof(action),"state");
    int actor=V082BPHuman();
    if(StrEqual(action,"state")){V082BPReadState();return Plugin_Handled;}
    if(StrEqual(action,"status")){V082BPReadState();V082BPSample("status");return Plugin_Handled;}
    if(StrEqual(action,"begin")){V082BPBegin(actor);return Plugin_Handled;}
    if(StrEqual(action,"cleanup")){V082BPCleanup();return Plugin_Handled;}
    if(!V082BPSession(actor)){V082BPEvent("rejected","action_requires_original_active_fixture_session");return Plugin_Handled;}
    if(StrEqual(action,"stop")){V082BPSample("operator_stopped");V082BPFinish(false,"operator_stopped_scene");return Plugin_Handled;}
    if(StrEqual(action,"prepare") && (args==3 || args==4))
    {
        char id[64],number[32];GetCmdArg(2,id,sizeof(id));GetCmdArg(3,number,sizeof(number));int point;float timeout=120.0;
        bool valid=StringToIntEx(number,point)==strlen(number) && point>=1 && point<=BOSS_SPAWN_POINT_COUNT;
        if(args==4){GetCmdArg(4,number,sizeof(number));valid=valid && StringToFloatEx(number,timeout)==strlen(number) && (view_as<int>(timeout)&0x7f800000)!=0x7f800000;}
        if(valid)V082BPPrepare(actor,id,point-1,timeout);else V082BPEvent("rejected","invalid_point_or_timeout");return Plugin_Handled;
    }
    if(StrEqual(action,"return_to_base") && args==2)
    {
        char text[16];GetCmdArg(2,text,sizeof(text));int core=-1;
        if(strlen(text)==1 && text[0]>='A' && text[0]<='H')core=text[0]-'A';
        else if(strlen(text)==1 && text[0]>='a' && text[0]<='h')core=text[0]-'a';
        V082BPReturn(actor,core);return Plugin_Handled;
    }
    ReplyToCommand(client,"DIAGNOSTIC: sm_v082_bosspath state|begin|prepare <exact_boss_id> <1..5> [10..300_seconds]|return_to_base <A..H>|status|stop|cleanup");return Plugin_Handled;
}

void RegisterV082BossPathProbe(bool guardsInjected=false)
{
    tV082BPGuards=guardsInjected;
    if(tV082BPTimer!=null)return;
    RegAdminCmd("sm_v082_bosspath",V082BossPathCommand,ADMFLAG_ROOT,"Temporary existing-Boss path observations, one real human, no NPC creation or revival.");
    tV082BPTimer=CreateTimer(0.1,V082BossPathTimer,_,TIMER_REPEAT);
}

void V082BossPathShutdown()
{
    V082BPCleanup();delete tV082BPTimer;tV082BPTimer=null;
}
