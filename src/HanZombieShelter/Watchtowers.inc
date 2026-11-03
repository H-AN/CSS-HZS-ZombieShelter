// Tower travel is a living-player transaction, separate from spawning and bases.
enum struct WatchtowerProfile
{
    char type[64];
    float top[3];
    float bottom[3];
    float mins[3];
    float maxs[3];
    float probeHeight;
    float probeDepth;
    float maximumDrop;
    float floorNormal;
    float cooldown;
    float groundRadius;
    float groundZMin;
    float groundZMax;
    bool ownerOnly;
}
WatchtowerProfile gWatchtowerProfiles[MAX_RECIPES];
int gWatchtowerProfileCount;
float gWatchtowerNextUse[MAXPLAYERS+1],gWatchtowerReservedUntil[MAXPLAYERS+1];
float gWatchtowerReservedPoint[MAXPLAYERS+1][3];
int gWatchtowerReservationEpoch[MAXPLAYERS+1],gWatchtowerReservationSerial[MAXPLAYERS+1];
bool gWatchtowerTravelBusy;
enum WatchtowerTravelFailure
{
    WatchtowerFailure_None,
    WatchtowerFailure_Unavailable,
    WatchtowerFailure_Cooldown,
    WatchtowerFailure_Landing,
    WatchtowerFailure_TopOccupied,
    WatchtowerFailure_Changed
}
WatchtowerTravelFailure gWatchtowerTravelFailure[MAXPLAYERS+1];

void ResetWatchtowerClient(int client)
{
    gWatchtowerNextUse[client]=0.0;gWatchtowerReservedUntil[client]=0.0;
    gWatchtowerReservationEpoch[client]=0;gWatchtowerReservationSerial[client]=0;
    gWatchtowerTravelFailure[client]=WatchtowerFailure_None;
}

void ResetWatchtowerState()
{
    gWatchtowerTravelBusy=false;
    for(int client=1;client<=MaxClients;client++)ResetWatchtowerClient(client);
}

bool WatchtowerVector(const char[] path,const char[] key,float vector[3])
{
    if(!Node(C_Watchtowers,path) || gCfg[C_Watchtowers].GetDataType(key)==KvData_None)return false;
    char text[160],parts[4][40];gCfg[C_Watchtowers].GetString(key,text,sizeof(text));TrimString(text);
    ReplaceString(text,sizeof(text),"\t"," ");while(StrContains(text,"  ")>=0)ReplaceString(text,sizeof(text),"  "," ");
    if(ExplodeString(text," ",parts,4,40)!=3)return false;
    for(int axis=0;axis<3;axis++)
        if(!parts[axis][0] || StringToFloatEx(parts[axis],vector[axis])!=strlen(parts[axis])
            || !(vector[axis]>=-4096.0 && vector[axis]<=4096.0))return false;
    return true;
}

int WatchtowerProfileForType(const char[] type)
{
    for(int i=0;i<gWatchtowerProfileCount;i++)if(StrEqual(type,gWatchtowerProfiles[i].type))return i;
    return -1;
}

bool WatchtowerProfileGeometryValid(WatchtowerProfile profile)
{
    for(int axis=0;axis<3;axis++)
    {
        if(!(profile.top[axis]>=-4096.0 && profile.top[axis]<=4096.0)
            || !(profile.bottom[axis]>=-4096.0 && profile.bottom[axis]<=4096.0)
            || !(profile.mins[axis]>=-4096.0 && profile.mins[axis]<=4096.0)
            || !(profile.maxs[axis]>=-4096.0 && profile.maxs[axis]<=4096.0)
            || profile.mins[axis]>=profile.maxs[axis]
            || profile.top[axis]<profile.mins[axis] || profile.top[axis]>profile.maxs[axis])return false;
    }
    return profile.probeHeight>=2.0 && profile.probeHeight<=96.0
        && profile.probeDepth>=2.0 && profile.probeDepth<=128.0
        && profile.maximumDrop>=1.0 && profile.maximumDrop<=96.0
        && profile.floorNormal>=0.7 && profile.floorNormal<=1.0
        && profile.cooldown>=0.25 && profile.cooldown<=30.0
        && profile.groundRadius>=32.0 && profile.groundRadius<=1024.0
        && profile.groundZMin>=-256.0 && profile.groundZMin<=0.0
        && profile.groundZMax>=0.0 && profile.groundZMax<=256.0
        && profile.groundZMin<profile.groundZMax && profile.groundZMax<profile.mins[2]-72.0
        && profile.top[2]-profile.bottom[2]>=24.0;
}

bool LoadWatchtowerSettings()
{
    gWatchtowerProfileCount=0;
    if(gCfg[C_Watchtowers]==null)return true;
    for(int r=0;r<gRecipeCount;r++)
    {
        if(gRecipes[r].kind!=Kind_Watchtower || WatchtowerProfileForType(gRecipes[r].type)>=0)continue;
        if(gWatchtowerProfileCount>=MAX_RECIPES)return ConfigFailure("watchtower profile capacity");
        WatchtowerProfile profile;strcopy(profile.type,sizeof(profile.type),gRecipes[r].type);
        char path[192];Format(path,sizeof(path),"Types/%s/Travel",profile.type);
        bool valid=StrEqual(gRecipes[r].meta.placementMode,"upright")
            && FloatAbs(gRecipes[r].meta.angleOffset[0])<0.01 && FloatAbs(gRecipes[r].meta.angleOffset[2])<0.01
            && WatchtowerVector(path,"top_anchor",profile.top) && WatchtowerVector(path,"bottom_anchor",profile.bottom)
            && WatchtowerVector(path,"platform_mins",profile.mins) && WatchtowerVector(path,"platform_maxs",profile.maxs);
        bool found;
        valid=valid && StoredLevelFloat(C_Watchtowers,path,"support_probe_height",24.0,profile.probeHeight,found)
            && StoredLevelFloat(C_Watchtowers,path,"support_probe_depth",48.0,profile.probeDepth,found)
            && StoredLevelFloat(C_Watchtowers,path,"maximum_drop",48.0,profile.maximumDrop,found)
            && StoredLevelFloat(C_Watchtowers,path,"minimum_floor_normal",0.7,profile.floorNormal,found)
            && StoredLevelFloat(C_Watchtowers,path,"use_cooldown",1.0,profile.cooldown,found)
            && StoredLevelFloat(C_Watchtowers,path,"ground_use_radius",256.0,profile.groundRadius,found)
            && StoredLevelFloat(C_Watchtowers,path,"ground_use_z_min",-64.0,profile.groundZMin,found)
            && StoredLevelFloat(C_Watchtowers,path,"ground_use_z_max",96.0,profile.groundZMax,found);
        char policy[24];Text(C_Watchtowers,path,"use_policy",policy,sizeof(policy),"public");
        if(!StrEqual(policy,"public") && !StrEqual(policy,"owner"))valid=false;
        profile.ownerOnly=StrEqual(policy,"owner");
        if(!WatchtowerProfileGeometryValid(profile))valid=false;
        if(!valid){LogError("[Shelter] Watchtower travel disabled for %s: invalid upright platform/landing settings",profile.type);continue;}
        gWatchtowerProfiles[gWatchtowerProfileCount++]=profile;
    }
    return true;
}

bool WatchtowerBodyAlive(int b)
{
    if(!(Running() && b>=0 && b<MAX_BUILDINGS && gBuildings[b].used && gBuildings[b].alive
        && gBuildings[b].hp>0 && gBuildings[b].kind==Kind_Watchtower
        && gBuildings[b].configRevision==gConfigRevision && Resolve(gBuildings[b].ref)>0))return false;
    float angles[3];GetEntPropVector(Resolve(gBuildings[b].ref),Prop_Data,"m_angRotation",angles);
    return FloatAbs(angles[0])<0.01 && FloatAbs(angles[2])<0.01;
}

bool WatchtowerBodyFrame(int b,float origin[3],float angles[3])
{
    if(!WatchtowerBodyAlive(b))return false;
    int body=Resolve(gBuildings[b].ref);
    GetEntPropVector(body,Prop_Data,"m_vecAbsOrigin",origin);GetEntPropVector(body,Prop_Data,"m_angRotation",angles);
    for(int a=0;a<3;a++)
        if(!(origin[a]>=-15000.0 && origin[a]<=15000.0 && angles[a]>=-3600.0 && angles[a]<=3600.0))return false;
    return FloatAbs(angles[0])<0.01 && FloatAbs(angles[2])<0.01;
}

bool WatchtowerGroundPointAllowed(WatchtowerProfile profile,const float feet[3],const float origin[3])
{
    float dx=feet[0]-origin[0],dy=feet[1]-origin[1],z=feet[2]-origin[2];
    return dx*dx+dy*dy<=profile.groundRadius*profile.groundRadius
        && z>=profile.groundZMin && z<=profile.groundZMax;
}

bool WatchtowerActor(int client)
{
    return LiveHuman(client) && !IsFakeClient(client) && !IsClientSourceTV(client) && !IsClientReplay(client);
}

// Geometry half of support registration. The caller must separately retain and
// match the validated candidate.surfaceRef to this exact tower's current ref.
// Do not retrace after spawning a child, whose own collision can cover anchor.
bool WatchtowerAnchorOnPlatform(int b,const float anchor[3])
{
    if(!WatchtowerBodyAlive(b) || gBuildings[b].constructing || gConstructionSolidPending[b])return false;
    int p=WatchtowerProfileForType(gBuildings[b].type);if(p<0)return false;
    float origin[3],angles[3],local[3];if(!WatchtowerBodyFrame(b,origin,angles))return false;
    LocalPoint(anchor,origin,angles,local);
    return InBox(local,gWatchtowerProfiles[p].mins,gWatchtowerProfiles[p].maxs);
}

bool WatchtowerOnPlatform(int client,int b)
{
    if(!WatchtowerActor(client) || !WatchtowerBodyAlive(b) || gBuildings[b].constructing || gConstructionSolidPending[b])return false;
    int p=WatchtowerProfileForType(gBuildings[b].type),body=Resolve(gBuildings[b].ref);if(p<0 || body<0)return false;
    float feet[3],local[3],up[3],down[3],normal[3],supportPoint[3],origin[3],angles[3];GetClientAbsOrigin(client,feet);
    if(!WatchtowerBodyFrame(b,origin,angles))return false;
    LocalPoint(feet,origin,angles,local);
    if(!InBox(local,gWatchtowerProfiles[p].mins,gWatchtowerProfiles[p].maxs))return false;
    up=feet;down=feet;up[2]+=3.0;down[2]-=8.0;
    Handle trace=TR_TraceRayFilterEx(up,down,MASK_PLAYERSOLID,RayType_EndPoint,FilterExcept,client);
    bool supported=TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace) && TR_GetEntityIndex(trace)==body;
    if(supported){TR_GetPlaneNormal(trace,normal);TR_GetEndPosition(supportPoint,trace);}delete trace;
    return supported && normal[2]>=gWatchtowerProfiles[p].floorNormal
        && feet[2]-supportPoint[2]>=-1.0 && feet[2]-supportPoint[2]<=3.0;
}

// Completed tower selection uses a horizontal ground circle and its own bounded
// height band, independent of the configured downward destination. The long ray
// still has to hit this exact body first; ordinary facility use distances stay.
bool WatchtowerInUseRange(int client,int b,bool requireAim=true)
{
    if(!WatchtowerActor(client) || !WatchtowerBodyAlive(b))return false;
    int p=WatchtowerProfileForType(gBuildings[b].type);if(p<0)return false;
    if(WatchtowerOnPlatform(client,b))return !requireAim || AimedEntity(client,8192.0)==Resolve(gBuildings[b].ref);
    float feet[3],origin[3],angles[3];GetClientAbsOrigin(client,feet);
    if(!WatchtowerBodyFrame(b,origin,angles) || !WatchtowerGroundPointAllowed(gWatchtowerProfiles[p],feet,origin))return false;
    return AimedEntity(client,8192.0)==Resolve(gBuildings[b].ref);
}

int WatchtowerAimedBuilding(int client)
{
    if(!WatchtowerActor(client))return -1;
    int b=FindBuilding(AimedEntity(client,8192.0));
    return b>=0 && gBuildings[b].kind==Kind_Watchtower && !gBuildings[b].constructing
        && WatchtowerInUseRange(client,b,true) ? b : -1;
}

int WatchtowerUnderPlayer(int client)
{
    if(!WatchtowerActor(client))return -1;
    for(int b=MAX_CORES;b<MAX_BUILDINGS;b++)
        if(gBuildings[b].used && gBuildings[b].kind==Kind_Watchtower && WatchtowerOnPlatform(client,b))return b;
    return -1;
}

bool WatchtowerTravelAllowed(int client,int b,bool down)
{
    if(!WatchtowerActor(client) || !WatchtowerBodyAlive(b) || !BuildingPowered(b) || gConstructionSolidPending[b])return false;
    int p=WatchtowerProfileForType(gBuildings[b].type);if(p<0)return false;
    if(gWatchtowerProfiles[p].ownerOnly && !CanManageBuilding(client,b))return false;
    bool on=WatchtowerOnPlatform(client,b);
    return down ? on : (!on && WatchtowerInUseRange(client,b,true));
}

bool WatchtowerLandingOccupied(int client,const float point[3])
{
    float now=GetGameTime();
    for(int other=1;other<=MaxClients;other++)
    {
        if(other==client)continue;
        if(LiveHuman(other)){float p[3];GetClientAbsOrigin(other,p);if(GetVectorDistance(point,p)<40.0)return true;}
        if(IsClientInGame(other) && gWatchtowerReservedUntil[other]>now && gWatchtowerReservationEpoch[other]==gEpoch
            && gWatchtowerReservationSerial[other]==GetClientSerial(other)
            && GetVectorDistance(point,gWatchtowerReservedPoint[other])<40.0)return true;
    }
    return false;
}

bool WatchtowerLanding(int client,int b,bool down,const float candidate[3],float landing[3],bool &buildingBlocked=false)
{
    buildingBlocked=false;
    int p=WatchtowerProfileForType(gBuildings[b].type),body=Resolve(gBuildings[b].ref);if(p<0 || body<0)return false;
    float mins[3],maxs[3],feetMins[3],feetMaxs[3],up[3],below[3],normal[3];GetClientMins(client,mins);GetClientMaxs(client,maxs);
    // Sweep the footprint to find support, then validate the complete live hull
    // at the landing. Lifting a standing hull above a low canopy would begin
    // inside its roof even though the actual platform has enough headroom.
    feetMins=mins;feetMaxs=maxs;feetMins[2]=0.0;feetMaxs[2]=1.0;
    up=candidate;up[2]+=gWatchtowerProfiles[p].probeHeight;
    below=candidate;below[2]-=gWatchtowerProfiles[p].probeDepth;
    Handle trace=TR_TraceHullFilterEx(up,below,feetMins,feetMaxs,MASK_PLAYERSOLID,FilterExcept,client);
    bool valid=TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);int support=TR_GetEntityIndex(trace);
    if(support>0 && support!=body && FindBuilding(support)>=0)buildingBlocked=true;
    if(valid){TR_GetEndPosition(landing,trace);TR_GetPlaneNormal(trace,normal);support=TR_GetEntityIndex(trace);}delete trace;
    if(!valid || normal[2]<gWatchtowerProfiles[p].floorNormal || candidate[2]-landing[2]>gWatchtowerProfiles[p].maximumDrop)return false;
    if(down)
    {
        if(support==body || (support>0 && support<=MaxClients) || FindNPC(support)>=0 || FindBuilding(support)>=0)return false;
        if(support>MaxClients && IsValidEntity(support))
        {
            MoveType movement=GetEntityMoveType(support);
            if(movement!=MOVETYPE_NONE && movement!=MOVETYPE_PUSH)return false;
        }
    }
    else if(support!=body)return false;
    landing[2]+=1.0;
    if(TR_PointOutsideWorld(landing))return false;
    trace=TR_TraceHullFilterEx(landing,landing,mins,maxs,MASK_PLAYERSOLID,FilterExcept,client);
    bool clear=!TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);int obstacle=TR_GetEntityIndex(trace);delete trace;
    if(!clear && obstacle>0 && obstacle!=body && FindBuilding(obstacle)>=0)buildingBlocked=true;
    if(!clear || WatchtowerLandingOccupied(client,landing))return false;
    if(!down)
    {
        float local[3],origin[3],angles[3];if(!WatchtowerBodyFrame(b,origin,angles))return false;LocalPoint(landing,origin,angles,local);
        if(!InBox(local,gWatchtowerProfiles[p].mins,gWatchtowerProfiles[p].maxs))return false;
    }
    return true;
}

bool FindWatchtowerLanding(int client,int b,bool down,float result[3],bool &buildingBlocked=false)
{
    buildingBlocked=false;
    int p=WatchtowerProfileForType(gBuildings[b].type);if(p<0)return false;
    float origin[3],angles[3];if(!WatchtowerBodyFrame(b,origin,angles))return false;
    static const float offsets[9][2]={{0.0,0.0},{24.0,0.0},{-24.0,0.0},{0.0,24.0},{0.0,-24.0},{24.0,24.0},{24.0,-24.0},{-24.0,24.0},{-24.0,-24.0}};
    for(int i=0;i<sizeof(offsets);i++)
    {
        float local[3],point[3];if(down)local=gWatchtowerProfiles[p].bottom;else local=gWatchtowerProfiles[p].top;
        local[0]+=offsets[i][0];local[1]+=offsets[i][1];
        if(!down && !InBox(local,gWatchtowerProfiles[p].mins,gWatchtowerProfiles[p].maxs))continue;
        WorldPoint(local,origin,angles,point);bool blocked;
        if(WatchtowerLanding(client,b,down,point,result,blocked))return true;
        buildingBlocked=buildingBlocked || blocked;
    }
    if(down)return false;
    // Cover gaps between the original nine anchors as well. Current platforms
    // use at most 8-unit spacing; a configured huge platform remains bounded by
    // 13x13 samples. Every sample retains the same live hull/support checks.
    int xSteps=RoundToCeil((gWatchtowerProfiles[p].maxs[0]-gWatchtowerProfiles[p].mins[0])/8.0);
    int ySteps=RoundToCeil((gWatchtowerProfiles[p].maxs[1]-gWatchtowerProfiles[p].mins[1])/8.0);
    if(xSteps<1)xSteps=1;else if(xSteps>12)xSteps=12;
    if(ySteps<1)ySteps=1;else if(ySteps>12)ySteps=12;
    for(int x=0;x<=xSteps;x++)for(int y=0;y<=ySteps;y++)
    {
        float local[3],point[3];local=gWatchtowerProfiles[p].top;
        local[0]=gWatchtowerProfiles[p].mins[0]+float(x)*(gWatchtowerProfiles[p].maxs[0]-gWatchtowerProfiles[p].mins[0])/float(xSteps);
        local[1]=gWatchtowerProfiles[p].mins[1]+float(y)*(gWatchtowerProfiles[p].maxs[1]-gWatchtowerProfiles[p].mins[1])/float(ySteps);
        WorldPoint(local,origin,angles,point);bool blocked;
        if(WatchtowerLanding(client,b,false,point,result,blocked))return true;
        buildingBlocked=buildingBlocked || blocked;
    }
    return false;
}

bool TryWatchtowerTravel(int client,int b,bool down)
{
    if(client<1 || client>MaxClients)return false;
    gWatchtowerTravelFailure[client]=WatchtowerFailure_Unavailable;
    if(gWatchtowerTravelBusy || !WatchtowerTravelAllowed(client,b,down))return false;
    float now=GetGameTime();if(now<gWatchtowerNextUse[client])
    {gWatchtowerTravelFailure[client]=WatchtowerFailure_Cooldown;return false;}
    int serial=GetClientSerial(client),epoch=gEpoch,revision=gConfigRevision,uid=gBuildings[b].uid,ref=gBuildings[b].ref,generation=gBuildings[b].generation;
    int profile=WatchtowerProfileForType(gBuildings[b].type);float landing[3],bodyOrigin[3],bodyAngles[3];
    if(profile<0){gWatchtowerTravelFailure[client]=WatchtowerFailure_Changed;return false;}
    if(!WatchtowerBodyFrame(b,bodyOrigin,bodyAngles)){gWatchtowerTravelFailure[client]=WatchtowerFailure_Changed;return false;}
    bool buildingBlocked;
    if(!FindWatchtowerLanding(client,b,down,landing,buildingBlocked))
    {gWatchtowerTravelFailure[client]=!down && buildingBlocked ? WatchtowerFailure_TopOccupied : WatchtowerFailure_Landing;return false;}
    float currentOrigin[3],currentAngles[3];bool sameFrame=WatchtowerBodyFrame(b,currentOrigin,currentAngles)
        && GetVectorDistance(bodyOrigin,currentOrigin)<0.01 && GetVectorDistance(bodyAngles,currentAngles)<0.01;
    if(!WatchtowerTravelAllowed(client,b,down) || epoch!=gEpoch || revision!=gConfigRevision
        || !sameFrame || gBuildings[b].uid!=uid || gBuildings[b].ref!=ref || gBuildings[b].generation!=generation || GetClientSerial(client)!=serial)
    {gWatchtowerTravelFailure[client]=WatchtowerFailure_Changed;return false;}
    float before[3],beforeAngles[3],beforeVelocity[3],zero[3];GetClientAbsOrigin(client,before);GetClientEyeAngles(client,beforeAngles);
    GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",beforeVelocity);
    gWatchtowerTravelBusy=true;gWatchtowerReservedPoint[client]=landing;gWatchtowerReservedUntil[client]=now+0.25;
    gWatchtowerReservationEpoch[client]=epoch;gWatchtowerReservationSerial[client]=serial;
    TeleportEntity(client,landing,beforeAngles,zero);
    float actual[3],mins[3],maxs[3];bool valid=WatchtowerActor(client) && GetClientSerial(client)==serial && epoch==gEpoch && revision==gConfigRevision
        && WatchtowerBodyAlive(b) && gBuildings[b].uid==uid && gBuildings[b].ref==ref && gBuildings[b].generation==generation;
    if(valid)
    {
        GetClientAbsOrigin(client,actual);GetClientMins(client,mins);GetClientMaxs(client,maxs);
        valid=GetVectorDistance(actual,landing)<4.0 && ClearHull(actual,mins,maxs,client,MASK_PLAYERSOLID);
        if(valid && !down)valid=WatchtowerOnPlatform(client,b);
    }
    if(!valid)
    {
        gWatchtowerTravelFailure[client]=WatchtowerFailure_Changed;
        gWatchtowerReservedUntil[client]=0.0;
        if(epoch==gEpoch && revision==gConfigRevision && IsClientInGame(client)
            && GetClientSerial(client)==serial && IsPlayerAlive(client))TeleportEntity(client,before,beforeAngles,beforeVelocity);
    }
    else {gWatchtowerNextUse[client]=now+gWatchtowerProfiles[profile].cooldown;gWatchtowerTravelFailure[client]=WatchtowerFailure_None;}
    gWatchtowerTravelBusy=false;return valid;
}

void ReportWatchtowerTravelFailure(int client)
{
    if(!WatchtowerActor(client))return;
    switch(gWatchtowerTravelFailure[client])
    {
        case WatchtowerFailure_Cooldown:Tell(client,"%t","WatchtowerCooldown");
        case WatchtowerFailure_Landing:Tell(client,"%t","WatchtowerLandingBlocked");
        case WatchtowerFailure_TopOccupied:Tell(client,"%t","WatchtowerTopOccupied");
        case WatchtowerFailure_Changed:Tell(client,"%t","WatchtowerTravelChanged");
        default:Tell(client,"%t","WatchtowerTravelUnavailable");
    }
}

void QueueWatchtowerMenuRefresh(int client,int b,int nonce)
{
    if(!WatchtowerActor(client) || !WatchtowerBodyAlive(b) || gBuildings[b].constructing)return;
    DataPack pack=new DataPack();
    pack.WriteCell(GetClientSerial(client));pack.WriteCell(gEpoch);pack.WriteCell(gConfigRevision);
    pack.WriteCell(b);pack.WriteCell(gBuildings[b].uid);pack.WriteCell(gBuildings[b].ref);pack.WriteCell(gBuildings[b].generation);pack.WriteCell(nonce);
    RequestFrame(RefreshWatchtowerMenuFrame,pack);
}

public void RefreshWatchtowerMenuFrame(any data)
{
    DataPack pack=view_as<DataPack>(data);pack.Reset();
    int serial=pack.ReadCell(),epoch=pack.ReadCell(),revision=pack.ReadCell(),b=pack.ReadCell();
    int uid=pack.ReadCell(),ref=pack.ReadCell(),generation=pack.ReadCell(),nonce=pack.ReadCell();delete pack;
    int client=GetClientFromSerial(serial);
    // A successful teleport changes the side from which the next action is
    // offered. Build a fresh menu next frame; never reuse the consumed token or
    // replace a newer tower/base/other menu opened in the meantime.
    if(!WatchtowerActor(client) || epoch!=gEpoch || revision!=gConfigRevision
        || !WatchtowerBodyAlive(b) || gBuildings[b].constructing
        || gBuildings[b].uid!=uid || gBuildings[b].ref!=ref || gBuildings[b].generation!=generation
        || !WatchtowerMenuRefreshCurrent(client,nonce) || !WatchtowerInUseRange(client,b,false))return;
    ShowDefenseMenu(client,b);
}
