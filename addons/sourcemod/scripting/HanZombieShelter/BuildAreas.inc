// Build territory is independent of the green night-exposure safety boxes.
// Geometry is cached; power allocation never depends on the resulting territory.
#define BUILD_RANGE_MAX 8192.0
#define BUILD_EPSILON 0.01
enum struct BuildAreaCache
{
    float halfX;
    float halfY;
    int count;
    int limit;
    bool active;
    bool cornersReady;
}
BuildAreaCache gBuildAreas[MAX_CORES];
float gBuildCorners[MAX_CORES][4][3];
bool gBuildAreasDirty=true;
float gNextBuildDisplay;
int gRedConstructed,gRedDeliveries;

bool ReadBuildAreaVector(const char[] path,const char[] key,float vector[3],float bound)
{
    if(!Node(C_Map,path) || gCfg[C_Map].GetDataType(key)==KvData_None)return false;
    char text[160],parts[4][40];gCfg[C_Map].GetString(key,text,sizeof(text));TrimString(text);ReplaceString(text,sizeof(text),"\t"," ");
    while(StrContains(text,"  ")>=0)ReplaceString(text,sizeof(text),"  "," ");
    if(ExplodeString(text," ",parts,4,40)!=3)return false;
    for(int a=0;a<3;a++)if(!parts[a][0] || StringToFloatEx(parts[a],vector[a])!=strlen(parts[a])
        || !(vector[a]>=-bound && vector[a]<=bound))return false;
    return true;
}

bool LoadBuildAreaSettings()
{
    for(int c=0;c<gCoreCount;c++)
    {
        char path[192],profile[128];Format(path,sizeof(path),"Cores/%s/BuildArea",gCores[c].id);
        gCores[c].buildCenter=gCores[c].origin;gCores[c].buildAngles=gCores[c].angles;
        gCores[c].buildZMin=-128.0;gCores[c].buildZMax=512.0;
        if(Node(C_Map,path))
        {
            bool found;
            if(!ReadBuildAreaVector(path,"center",gCores[c].buildCenter,15000.0)
                || !ReadBuildAreaVector(path,"angles",gCores[c].buildAngles,3600.0)
                || !StoredLevelFloat(C_Map,path,"z_min",-128.0,gCores[c].buildZMin,found)
                || !StoredLevelFloat(C_Map,path,"z_max",512.0,gCores[c].buildZMax,found))return ConfigFailure("invalid BuildArea center/angles/heights");
        }
        else LogMessage("[Shelter] core %s missing BuildArea: explicit fallback to fixed base origin/yaw, z=-128..512",gCores[c].id);
        if(gCores[c].buildAngles[0]!=0.0 || gCores[c].buildAngles[2]!=0.0
            || !(gCores[c].buildAngles[1]>=-3600.0 && gCores[c].buildAngles[1]<=3600.0)
            || !(gCores[c].buildZMin>=-8192.0 && gCores[c].buildZMax<=8192.0 && gCores[c].buildZMin<gCores[c].buildZMax))return ConfigFailure("BuildArea requires finite horizontal yaw and valid z_min/z_max");
        for(int a=0;a<3;a++)if(!(gCores[c].buildCenter[a]>=-15000.0 && gCores[c].buildCenter[a]<=15000.0))return ConfigFailure("BuildArea center outside engine bounds");
        Format(profile,sizeof(profile),"Profiles/%s",gCores[c].profile);bool found;
        if(!StoredLevelInt(C_Bases,profile,"build_limit_max",128,gCores[c].buildHardLimit,found)
            || gCores[c].buildHardLimit<1 || gCores[c].buildHardLimit>MAX_BUILDINGS-MAX_CORES)return ConfigFailure("base build_limit_max outside storage ceiling");
        BuildAreaCache empty;gBuildAreas[c]=empty;
    }
    gBuildAreasDirty=true;gNextBuildDisplay=0.0;return true;
}

void MarkBuildAreasDirty(bool structure=true){gBuildAreasDirty=true;if(structure)InvalidateRuntimeBuildings();}

bool BuildCoreAlive(int core)
{
    if(core<0 || core>=gCoreCount)return false;
    int b=gCores[core].building;
    return BuildingOperational(b);
}

bool EffectiveExpander(int b)
{
    return BuildingOperational(b) && gBuildings[b].kind==Kind_Expander
        && gBuildings[b].hp>0 && BuildCoreAlive(gBuildings[b].buildCore) && BuildingPowered(b);
}

void RefreshBuildAreas()
{
    if(!gBuildAreasDirty)return;gBuildAreasDirty=false;
    float bonus[MAX_CORES];int extra[MAX_CORES],counts[MAX_CORES];
    for(int b=RuntimeNextBuilding(-1);b>=0;b=RuntimeNextBuilding(b))
    {
        if(!gBuildings[b].used || !gBuildings[b].alive || gBuildings[b].kind==Kind_Base)continue;
        int c=gBuildings[b].buildCore;if(c<0 || c>=gCoreCount)continue;
        counts[c]++; // Logical buildings waiting for their entity still occupy a place.
        if(!EffectiveExpander(b))continue;
        bonus[c]+=gBuildings[b].buildRangeBonus;if(bonus[c]>BUILD_RANGE_MAX)bonus[c]=BUILD_RANGE_MAX;
        extra[c]+=gBuildings[b].buildLimitBonus;if(extra[c]>MAX_BUILDINGS)extra[c]=MAX_BUILDINGS;
    }
    float height=Decimal(C_Mode,"BuildZoneDisplay","height_offset",4.0);
    for(int c=0;c<gCoreCount;c++)
    {
        int base=gCores[c].building;bool active=BuildCoreAlive(c);
        float halfX,halfY;int limit;
        if(active)
        {
            halfX=gBuildings[base].buildHalfExtents[0]+bonus[c];halfY=gBuildings[base].buildHalfExtents[1]+bonus[c];
            if(halfX>BUILD_RANGE_MAX)halfX=BUILD_RANGE_MAX;if(halfY>BUILD_RANGE_MAX)halfY=BUILD_RANGE_MAX;
            limit=gBuildings[base].baseBuildLimit+extra[c];if(limit>gCores[c].buildHardLimit)limit=gCores[c].buildHardLimit;
        }
        bool geometryChanged=!gBuildAreas[c].cornersReady || halfX!=gBuildAreas[c].halfX || halfY!=gBuildAreas[c].halfY;
        gBuildAreas[c].active=active;gBuildAreas[c].count=counts[c];gBuildAreas[c].limit=limit;
        gBuildAreas[c].halfX=halfX;gBuildAreas[c].halfY=halfY;
        if(geometryChanged)
        {
            for(int p=0;p<4;p++)
            {
                float local[3];local[0]=(p==0 || p==3) ? -halfX : halfX;
                local[1]=p<2 ? -halfY : halfY;local[2]=height;
                WorldPoint(local,gCores[c].buildCenter,gCores[c].buildAngles,gBuildCorners[c][p]);
            }
            gBuildAreas[c].cornersReady=true;
        }
    }
}

bool BuildAreaContains(int core,const float anchor[3],bool height=true)
{
    if(core<0 || core>=gCoreCount || !gBuildAreas[core].active)return false;
    float local[3];LocalPoint(anchor,gCores[core].buildCenter,gCores[core].buildAngles,local);
    return FloatAbs(local[0])<=gBuildAreas[core].halfX+BUILD_EPSILON
        && FloatAbs(local[1])<=gBuildAreas[core].halfY+BUILD_EPSILON
        && (!height || (local[2]>=gCores[core].buildZMin-BUILD_EPSILON && local[2]<=gCores[core].buildZMax+BUILD_EPSILON));
}

int BuildCoreAt(const float anchor[3])
{
    RefreshBuildAreas();int selected=-1;float nearest=999999999.0;
    for(int c=0;c<gCoreCount;c++)if(BuildAreaContains(c,anchor))
    {
        float distance=GetVectorDistance(anchor,gCores[c].buildCenter,true);
        if(selected<0 || distance<nearest-BUILD_EPSILON
            || (FloatAbs(distance-nearest)<=BUILD_EPSILON && strcmp(gCores[c].id,gCores[selected].id)<0))
        {selected=c;nearest=distance;}
    }
    return selected;
}

bool CheckBuildCapacity(int client,int core)
{
    RefreshBuildAreas();if(core<0 || core>=gCoreCount || !gBuildAreas[core].active)return false;
    if(gBuildAreas[core].count<gBuildAreas[core].limit)return true;
    Tell(client,"%t","BuildCoreFull",gBuildAreas[core].count,gBuildAreas[core].limit);return false;
}

void AppendBuildCapacityInfo(int client,int core,char[] text,int length)
{
    RefreshBuildAreas();if(core<0 || core>=gCoreCount)return;
    char name[64],line[192];CoreDisplayName(core,name,sizeof(name),client);
    int excess=gBuildAreas[core].count-gBuildAreas[core].limit;if(excess<0)excess=0;
    Format(line,sizeof(line),"%T","BuildCapacityInfo",client,name,gBuildAreas[core].count,gBuildAreas[core].limit,excess);StrCat(text,length,line);
}

void DrawBuildZones()
{
    if(!Running() || !Number(C_Mode,"BuildZoneDisplay","enabled",1) || GetGameTime()<gNextBuildDisplay)return;
    float interval=Decimal(C_Mode,"BuildZoneDisplay","refresh_interval",1.0);if(!(interval>=0.9 && interval<=5.0))interval=1.0;
    gNextBuildDisplay=GetGameTime()+interval;RefreshBuildAreas();
    int clients[MAXPLAYERS],count;float positions[MAXPLAYERS+1][3];bool eligible[MAXPLAYERS+1];
    for(int client=1;client<=MaxClients;client++)
        if(LiveHuman(client) && !IsFakeClient(client) && !IsClientSourceTV(client) && !IsClientReplay(client))
        {eligible[client]=true;GetClientAbsOrigin(client,positions[client]);}
    int color[4]={255,40,40,220};char rgba[64],parts[4][16];Text(C_Mode,"BuildZoneDisplay","color",rgba,sizeof(rgba),"255 40 40 220");
    if(ExplodeString(rgba," ",parts,4,16)==4)for(int a=0;a<4;a++){int value=StringToInt(parts[a]);color[a]=value<0 ? 0 : (value>255 ? 255 : value);}
    float life=Decimal(C_Mode,"BuildZoneDisplay","effect_lifetime",1.2);if(!(life>interval && life<=10.0))life=interval+0.2;
    float width=Decimal(C_Mode,"BuildZoneDisplay","line_width",3.0);if(!(width>0.0 && width<=32.0))width=3.0;
    for(int c=0;c<gCoreCount;c++)
    {
        count=0;for(int client=1;client<=MaxClients;client++)if(eligible[client] && BuildAreaContains(c,positions[client]))clients[count++]=client;
        if(count==0)continue; // No temporary effect is constructed without a living human recipient.
        for(int p=0;p<4;p++)
        {
            TE_SetupBeamPoints(gBuildCorners[c][p],gBuildCorners[c][(p+1)%4],gBeamSprite,0,0,0,life,width,width,0,0.0,color,0);
            TE_Send(clients,count);gRedConstructed++;gRedDeliveries+=count;
        }
    }
}
