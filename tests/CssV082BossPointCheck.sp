#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#include <HanZombieScenarioAPI>

// Temporary read-only geometry observations. No timers or entity/state writes.
// The filter excludes ALL current HZS bodies/decoded visuals and all players;
// this deliberately differs from the production Boss-only placement filter.
#define POINTCHECK_BOSSES 3
#define POINTCHECK_POINTS 5
#define POINTCHECK_NPCS 1024

public Plugin myinfo={name="CSS v0.8.2 Boss point geometry check",author="H-AN",description="Read current Boss hulls and captured points; no movement, spawn, damage or awakening",version="1.0"};

int pCheckNPCRefs[POINTCHECK_NPCS],pCheckVisualRefs[POINTCHECK_NPCS],pCheckNPCCount;
char pCheckIDs[POINTCHECK_BOSSES][64],pCheckProfiles[POINTCHECK_BOSSES][64],pCheckNames[POINTCHECK_BOSSES][128];
int pCheckBossRefs[POINTCHECK_BOSSES],pCheckBossVisualRefs[POINTCHECK_BOSSES];
float pCheckOrigins[POINTCHECK_POINTS][3];
char pCheckMap[PLATFORM_MAX_PATH];

void PointCheckLog(const char[] row)
{
    PrintToServer("[V082BossPointCheck] %s",row);
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"logs/v082_boss_point_check.jsonl");
    File file=OpenFile(path,"a");if(file!=null){file.WriteLine("%s",row);delete file;}
}

void PointCheckJSON(const char[] input,char[] output,int size)
{
    int cursor;
    for(int i=0;input[i] && cursor<size-1;i++)
    {
        if(input[i]=='"' || input[i]=='\\'){if(cursor>=size-2)break;output[cursor++]='\\';}
        if(input[i]<32)output[cursor++]=' ';else output[cursor++]=input[i];
    }
    output[cursor]='\0';
}

bool PointCheckFailure(const char[] reason)
{
    char escaped[768],row[1024];PointCheckJSON(reason,escaped,sizeof(escaped));
    Format(row,sizeof(row),"{\"kind\":\"rejected\",\"time\":%d,\"reason\":\"%s\",\"gameplay_state_changed\":false}",GetTime(),escaped);PointCheckLog(row);return false;
}

int PointCheckResolve(int ref)
{
    if(ref==INVALID_ENT_REFERENCE)return -1;
    int entity=EntRefToEntIndex(ref);return entity>MaxClients && IsValidEntity(entity) ? entity : -1;
}

bool PointCheckVector(const char[] raw,float vector[3])
{
    int cursor,length=strlen(raw);
    for(int axis=0;axis<3;axis++)
    {
        while(cursor<length && raw[cursor]<=32)cursor++;
        int start=cursor;while(cursor<length && raw[cursor]>32)cursor++;
        int count=cursor-start;if(count<1 || count>=64)return false;
        char token[64];for(int i=0;i<count;i++)token[i]=raw[start+i];token[count]='\0';
        if(StringToFloatEx(token,vector[axis])!=count || (view_as<int>(vector[axis])&0x7f800000)==0x7f800000
            || vector[axis]<-15000.0 || vector[axis]>15000.0)return false;
    }
    while(cursor<length && raw[cursor]<=32)cursor++;return cursor==length;
}

bool PointCheckReadPoints()
{
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"data/zsh_boss_points/%s.cfg",pCheckMap);
    KeyValues points=new KeyValues("BossSpawnPoints");
    if(!points.ImportFromFile(path)){delete points;return PointCheckFailure("current-map user-captured BossSpawnPoints file could not be read");}
    bool seen[POINTCHECK_POINTS],valid=points.GotoFirstSubKey(false);int count;
    if(valid)do
    {
        char name[64],wanted[32],origin[192];points.GetSectionName(name,sizeof(name));int slot=-1;
        for(int i=0;i<POINTCHECK_POINTS;i++){Format(wanted,sizeof(wanted),"boss%d",i+1);if(StrEqual(name,wanted)){slot=i;break;}}
        if(slot<0 || seen[slot] || points.GetDataType(NULL_STRING)!=KvData_None){valid=false;break;}
        points.GetString("origin",origin,sizeof(origin));
        if(!PointCheckVector(origin,pCheckOrigins[slot])){valid=false;break;}
        seen[slot]=true;count++;
    }while(points.GotoNextKey(false));
    delete points;
    if(!valid || count!=POINTCHECK_POINTS)return PointCheckFailure("capture file must contain each boss1..boss5 once with three finite origin numbers");
    char escaped[PLATFORM_MAX_PATH*2],row[1024];PointCheckJSON(path,escaped,sizeof(escaped));
    Format(row,sizeof(row),"{\"kind\":\"point_source\",\"time\":%d,\"path\":\"%s\",\"point_count\":5,\"origin_was_adjusted\":false}",GetTime(),escaped);PointCheckLog(row);return true;
}

bool PointCheckReadBossNames()
{
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieShelter/maps/%s.cfg",pCheckMap);
    KeyValues map=new KeyValues("ShelterMap");
    if(!map.ImportFromFile(path) || !map.JumpToKey("Bosses") || !map.GotoFirstSubKey())
    {delete map;return PointCheckFailure("current live map Bosses configuration could not be read");}
    int count;bool valid=true;
    do
    {
        if(map.GetNum("enabled",1)==0)continue;
        if(count>=POINTCHECK_BOSSES){valid=false;break;}
        map.GetSectionName(pCheckIDs[count],sizeof(pCheckIDs[]));map.GetString("boss_profile",pCheckProfiles[count],sizeof(pCheckProfiles[]));
        if(!pCheckProfiles[count][0]){valid=false;break;}count++;
    }while(map.GotoNextKey());delete map;
    if(!valid || count!=POINTCHECK_BOSSES)return PointCheckFailure("this helper requires exactly three enabled configured Boss instances");
    BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieShelter/bosses.cfg");KeyValues profiles=new KeyValues("ShelterBosses");
    if(!profiles.ImportFromFile(path)){delete profiles;return PointCheckFailure("live Boss profile file could not be read");}
    for(int i=0;i<count;i++)
    {
        profiles.Rewind();
        if(!profiles.JumpToKey("BossTypes") || !profiles.JumpToKey(pCheckProfiles[i])){valid=false;break;}
        profiles.GetString("public_name",pCheckNames[i],sizeof(pCheckNames[]));if(!pCheckNames[i][0]){valid=false;break;}
        for(int previous=0;previous<i;previous++)if(StrEqual(pCheckNames[i],pCheckNames[previous]))valid=false;
    }
    delete profiles;return valid || PointCheckFailure("three Boss profile public names must exist and be distinct for this one-map observation");
}

bool PointCheckSnapshotActors()
{
    pCheckNPCCount=Han_GetZombieCount();
    if(pCheckNPCCount<1 || pCheckNPCCount>POINTCHECK_NPCS)return PointCheckFailure("current native NPC count is outside the helper snapshot limit");
    int found[POINTCHECK_BOSSES],validBodies,validVisuals,invalidBodies,unresolvedMarkers;
    for(int b=0;b<POINTCHECK_BOSSES;b++){pCheckBossRefs[b]=INVALID_ENT_REFERENCE;pCheckBossVisualRefs[b]=INVALID_ENT_REFERENCE;}
    for(int i=0;i<pCheckNPCCount;i++)
    {
        pCheckNPCRefs[i]=INVALID_ENT_REFERENCE;pCheckVisualRefs[i]=INVALID_ENT_REFERENCE;
        int entity=Han_GetZombieByIndex(i);if(entity<=MaxClients || !IsValidEntity(entity)){invalidBodies++;continue;}
        pCheckNPCRefs[i]=EntIndexToEntRef(entity);validBodies++;
        char marker[128];int visualRef;
        if(HasEntProp(entity,Prop_Data,"m_iName"))GetEntPropString(entity,Prop_Data,"m_iName",marker,sizeof(marker));
        if(marker[0] && StringToIntEx(marker,visualRef)==strlen(marker) && PointCheckResolve(visualRef)>0)
        {pCheckVisualRefs[i]=visualRef;validVisuals++;}
        else if(marker[0])unresolvedMarkers++;
        char name[128];if(!Han_GetZombieName(entity,name,sizeof(name)))continue;
        if(!HasEntProp(entity,Prop_Data,"m_iHealth") || GetEntProp(entity,Prop_Data,"m_iHealth")<=0)continue;
        for(int b=0;b<POINTCHECK_BOSSES;b++)if(StrEqual(name,pCheckNames[b]))
        {found[b]++;pCheckBossRefs[b]=pCheckNPCRefs[i];pCheckBossVisualRefs[b]=pCheckVisualRefs[i];}
    }
    char row[1536];Format(row,sizeof(row),"{\"kind\":\"filter_scope\",\"time\":%d,\"native_npc_entries\":%d,\"valid_hzs_body_entries_ignored\":%d,\"valid_visual_entries_ignored\":%d,\"invalid_body_entries\":%d,\"unresolved_nonempty_visual_markers\":%d,\"all_players_ignored\":true,\"all_hzs_boss_and_ordinary_bodies_ignored\":true,\"all_decoded_hzs_visuals_ignored\":true,\"world_brushes_props_and_other_entities_retained\":true,\"production_initial_hull_filter_equivalent\":false,\"world_entity_may_include_bsp_static_props\":true,\"npc_or_player_state_writes\":false,\"timers\":false}",
        GetTime(),pCheckNPCCount,validBodies,validVisuals,invalidBodies,unresolvedMarkers);PointCheckLog(row);
    for(int b=0;b<POINTCHECK_BOSSES;b++)if(found[b]!=1)return PointCheckFailure("each configured public name must match exactly one already-existing living native Boss; none is created or revived");
    return true;
}

public bool PointCheckTraceFilter(int entity,int contentsMask,any data)
{
    #pragma unused contentsMask, data
    if(entity>0 && entity<=MaxClients)return false;
    if(entity==0)return true;
    for(int i=0;i<pCheckNPCCount;i++)if(PointCheckResolve(pCheckNPCRefs[i])==entity || PointCheckResolve(pCheckVisualRefs[i])==entity)return false;
    return true;
}

void PointCheckDescribeHit(int entity,char[] classname,int classSize,char[] model,int modelSize)
{
    classname[0]='\0';model[0]='\0';
    if(entity==0)
    {
        strcopy(classname,classSize,"worldspawn_or_bsp_staticprop");Format(model,modelSize,"maps/%s.bsp",pCheckMap);return;
    }
    if(entity<1 || !IsValidEntity(entity))return;
    char actualClass[128],actualModel[PLATFORM_MAX_PATH];GetEntityClassname(entity,actualClass,sizeof(actualClass));
    if(HasEntProp(entity,Prop_Data,"m_ModelName"))GetEntPropString(entity,Prop_Data,"m_ModelName",actualModel,sizeof(actualModel));
    PointCheckJSON(actualClass,classname,classSize);PointCheckJSON(actualModel,model,modelSize);
}

bool PointCheckCombination(int boss,int point,bool &supported)
{
    int entity=PointCheckResolve(pCheckBossRefs[boss]);
    if(entity<0 || !HasEntProp(entity,Prop_Send,"m_vecMins") || !HasEntProp(entity,Prop_Send,"m_vecMaxs"))return PointCheckFailure("exact captured Boss reference disappeared or lacks actual collision bounds");
    float mins[3],maxs[3],scale=1.0;GetEntPropVector(entity,Prop_Send,"m_vecMins",mins);GetEntPropVector(entity,Prop_Send,"m_vecMaxs",maxs);
    if(HasEntProp(entity,Prop_Send,"m_flModelScale"))scale=GetEntPropFloat(entity,Prop_Send,"m_flModelScale");
    char rawModel[PLATFORM_MAX_PATH],model[PLATFORM_MAX_PATH*2],id[128],profile[128],name[256];
    if(HasEntProp(entity,Prop_Data,"m_ModelName"))GetEntPropString(entity,Prop_Data,"m_ModelName",rawModel,sizeof(rawModel));
    PointCheckJSON(rawModel,model,sizeof(model));PointCheckJSON(pCheckIDs[boss],id,sizeof(id));PointCheckJSON(pCheckProfiles[boss],profile,sizeof(profile));PointCheckJSON(pCheckNames[boss],name,sizeof(name));
    Handle hull=TR_TraceHullFilterEx(pCheckOrigins[point],pCheckOrigins[point],mins,maxs,MASK_NPCSOLID,PointCheckTraceFilter);
    bool hit=TR_DidHit(hull),startSolid=TR_StartSolid(hull),allSolid=TR_AllSolid(hull),clear=!hit && !startSolid && !allSolid;
    int hullEntity=(hit || startSolid || allSolid) ? TR_GetEntityIndex(hull) : -1;
    char hullClass[256],hullModel[PLATFORM_MAX_PATH*2],hullSurfaceRaw[128],hullSurface[256];
    PointCheckDescribeHit(hullEntity,hullClass,sizeof(hullClass),hullModel,sizeof(hullModel));
    if(hit || startSolid || allSolid)TR_GetSurfaceName(hull,hullSurfaceRaw,sizeof(hullSurfaceRaw));PointCheckJSON(hullSurfaceRaw,hullSurface,sizeof(hullSurface));
    int hullHitbox=TR_GetHitBoxIndex(hull);float hullFraction=TR_GetFraction(hull);delete hull;
    float rayStart[3],rayEnd[3],floor[3],normal[3],gap;rayStart=pCheckOrigins[point];rayStart[2]+=mins[2]+2.0;rayEnd=rayStart;rayEnd[2]-=130.0;
    Handle ground=TR_TraceRayFilterEx(rayStart,rayEnd,MASK_NPCSOLID,RayType_EndPoint,PointCheckTraceFilter);
    bool floorHit=TR_DidHit(ground),floorStart=TR_StartSolid(ground),floorAll=TR_AllSolid(ground);int floorEntity=floorHit ? TR_GetEntityIndex(ground) : -1;
    char floorClass[256],floorModel[PLATFORM_MAX_PATH*2],floorSurfaceRaw[128],floorSurface[256];
    if(floorHit){TR_GetEndPosition(floor,ground);TR_GetPlaneNormal(ground,normal);gap=pCheckOrigins[point][2]+mins[2]-floor[2];TR_GetSurfaceName(ground,floorSurfaceRaw,sizeof(floorSurfaceRaw));}
    PointCheckDescribeHit(floorEntity,floorClass,sizeof(floorClass),floorModel,sizeof(floorModel));PointCheckJSON(floorSurfaceRaw,floorSurface,sizeof(floorSurface));
    int floorHitbox=TR_GetHitBoxIndex(ground);delete ground;
    supported=floorHit && !floorStart && !floorAll && normal[2]>=0.7 && gap>=-0.125 && gap<=4.0;
    char row[4096];Format(row,sizeof(row),"{\"kind\":\"point_actor_geometry\",\"time\":%d,\"game_time\":%.3f,\"point\":%d,\"boss_id\":\"%s\",\"profile\":\"%s\",\"public_name\":\"%s\",\"body_ref\":%d,\"visual_ref\":%d,\"body_model\":\"%s\",\"model_scale\":%.6f,\"mins\":[%.6f,%.6f,%.6f],\"maxs\":[%.6f,%.6f,%.6f],\"bounds_multiplied_by_scale\":false,\"origin\":[%.6f,%.6f,%.6f],\"hull_clear\":%s,\"hull_did_hit\":%d,\"hull_start_solid\":%d,\"hull_all_solid\":%d,\"hull_fraction\":%.6f,\"hull_hit_entity\":%d,\"hull_hit_classname\":\"%s\",\"hull_hit_model\":\"%s\",\"hull_hit_surface\":\"%s\",\"hull_hitbox\":%d,\"support_ray_hit\":%d,\"support_start_solid\":%d,\"support_all_solid\":%d,\"floor_z\":%.6f,\"gap_from_hull_bottom\":%.6f,\"floor_normal\":[%.6f,%.6f,%.6f],\"floor_entity\":%d,\"floor_classname\":\"%s\",\"floor_model\":\"%s\",\"floor_surface\":\"%s\",\"floor_hitbox\":%d,\"supported_within_4_units\":%s,\"coordinates_adjusted\":false,\"npc_or_player_state_changed\":false}",
        GetTime(),GetGameTime(),point+1,id,profile,name,pCheckBossRefs[boss],pCheckBossVisualRefs[boss],model,scale,mins[0],mins[1],mins[2],maxs[0],maxs[1],maxs[2],pCheckOrigins[point][0],pCheckOrigins[point][1],pCheckOrigins[point][2],clear ? "true" : "false",hit,startSolid,allSolid,hullFraction,hullEntity,hullClass,hullModel,hullSurface,hullHitbox,floorHit,floorStart,floorAll,floor[2],gap,normal[0],normal[1],normal[2],floorEntity,floorClass,floorModel,floorSurface,floorHitbox,supported ? "true" : "false");PointCheckLog(row);return clear;
}

public Action PointCheckCommand(int client,int args)
{
    #pragma unused client
    if(args!=0){PointCheckFailure("usage: sm_v082_boss_point_check (no arguments, current map and existing actors only)");return Plugin_Handled;}
    if(GetFeatureStatus(FeatureType_Native,"Han_GetZombieCount")!=FeatureStatus_Available
        || GetFeatureStatus(FeatureType_Native,"Han_GetZombieByIndex")!=FeatureStatus_Available
        || GetFeatureStatus(FeatureType_Native,"Han_GetZombieName")!=FeatureStatus_Available)
    {PointCheckFailure("the three read-only HZS native queries are unavailable");return Plugin_Handled;}
    GetCurrentMap(pCheckMap,sizeof(pCheckMap));
    if(!PointCheckReadPoints() || !PointCheckReadBossNames() || !PointCheckSnapshotActors())return Plugin_Handled;
    int checks,clearCount,supportCount,both;
    for(int b=0;b<POINTCHECK_BOSSES;b++)for(int p=0;p<POINTCHECK_POINTS;p++)
    {bool supported;bool clear=PointCheckCombination(b,p,supported);checks++;if(clear)clearCount++;if(supported)supportCount++;if(clear && supported)both++;}
    char row[1024];Format(row,sizeof(row),"{\"kind\":\"geometry_summary\",\"time\":%d,\"combinations\":%d,\"hulls_clear\":%d,\"supported_within_4_units\":%d,\"clear_and_supported\":%d,\"geometry_observations_only\":true,\"path_or_platform_exit_or_chase_tested\":false,\"production_filter_equivalent\":false,\"npc_or_player_state_changed\":false}",GetTime(),checks,clearCount,supportCount,both);PointCheckLog(row);return Plugin_Handled;
}

public APLRes AskPluginLoad2(Handle myself,bool late,char[] error,int maxlength)
{
    #pragma unused myself, late, error, maxlength
    MarkNativeAsOptional("Han_GetZombieCount");MarkNativeAsOptional("Han_GetZombieByIndex");MarkNativeAsOptional("Han_GetZombieName");return APLRes_Success;
}

public void OnPluginStart()
{
    RegAdminCmd("sm_v082_boss_point_check",PointCheckCommand,ADMFLAG_ROOT,"Read captured five points against three already-existing Boss collision hulls; never changes gameplay state.");
}
