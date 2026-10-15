void LocalPoint(const float world[3],const float origin[3],const float angles[3],float local[3])
{
    float delta[3],axisForward[3],axisRight[3],axisUp[3];SubtractVectors(world,origin,delta);GetAngleVectors(angles,axisForward,axisRight,axisUp);
    local[0]=GetVectorDotProduct(delta,axisForward);local[1]=-GetVectorDotProduct(delta,axisRight);local[2]=GetVectorDotProduct(delta,axisUp);
}

void WorldPoint(const float local[3],const float origin[3],const float angles[3],float world[3])
{
    float axisForward[3],axisRight[3],axisUp[3];GetAngleVectors(angles,axisForward,axisRight,axisUp);
    for(int a=0;a<3;a++)world[a]=origin[a]+local[0]*axisForward[a]-local[1]*axisRight[a]+local[2]*axisUp[a];
}

bool InBox(const float position[3],const float mins[3],const float maxs[3])
{
    for(int axis=0;axis<3;axis++)if(position[axis]<mins[axis] || position[axis]>maxs[axis])return false;
    return true;
}

int SafeCoreAt(const float position[3])
{
    // The zone belongs to the map/campaign, not the destructible prop.
    if(AliveCores()==0)return -1;
    int closest=-1;float best=999999999.0;
    for(int i=0;i<gCoreCount;i++)
    {
        float local[3];LocalPoint(position,gCores[i].origin,gCores[i].angles,local);
        for(int box=0;box<gCores[i].boxCount;box++)if(InBox(local,gCoreMins[i][box],gCoreMaxs[i][box]))
        {
            float distance=GetVectorDistance(position,gCores[i].origin,true);
            if(distance<best){best=distance;closest=i;}break;
        }
    }
    return closest;
}

bool InSafeBase(const float position[3])
{
    return SafeCoreAt(position)>=0;
}

void DrawSafeZones()
{
    if(!Running() || AliveCores()==0 || !Number(C_Mode,"SafeZoneDisplay","enabled",1))return;
    int color[4]={40,255,80,220};
    float width=Decimal(C_Mode,"SafeZoneDisplay","line_width",3.0);
    float height=Decimal(C_Mode,"SafeZoneDisplay","height_above_base",4.0);
    for(int c=0;c<gCoreCount;c++)for(int box=0;box<gCores[c].boxCount;box++)
    {
        float corners[4][3];
        for(int p=0;p<4;p++)
        {
            float local[3];local[0]=(p==0 || p==3) ? gCoreMins[c][box][0] : gCoreMaxs[c][box][0];
            local[1]=p<2 ? gCoreMins[c][box][1] : gCoreMaxs[c][box][1];local[2]=height;
            WorldPoint(local,gCores[c].origin,gCores[c].angles,corners[p]);
        }
        for(int p=0;p<4;p++)
        {
            TE_SetupBeamPoints(corners[p],corners[(p+1)%4],gBeamSprite,0,0,0,1.2,width,width,0,0.0,color,0);
            TE_SendToAll();
        }
    }
}

public bool FilterExcept(int entity,int mask,any ignored)
{
    return entity!=ignored;
}

public bool FilterNoClients(int entity,int mask,any ignored)
{
    return !(entity>0 && entity<=MaxClients) && entity!=ignored;
}

bool ClearHull(const float position[3],const float mins[3],const float maxs[3],int ignored,int mask=MASK_NPCSOLID)
{
    Handle trace=TR_TraceHullFilterEx(position,position,mins,maxs,mask,FilterExcept,ignored);
    bool clear=!TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);delete trace;return clear;
}

bool LineClear(const float from[3],const float to[3],int source,int target)
{
    Handle trace=TR_TraceRayFilterEx(from,to,MASK_SOLID,RayType_EndPoint,FilterNoClients,source);
    bool clear=!TR_DidHit(trace) || TR_GetEntityIndex(trace)==target;delete trace;return clear;
}

void EntityCenter(int entity,float position[3])
{
    GetEntPropVector(entity,Prop_Data,"m_vecOrigin",position);
    float mins[3],maxs[3];GetEntPropVector(entity,Prop_Send,"m_vecMins",mins);GetEntPropVector(entity,Prop_Send,"m_vecMaxs",maxs);
    position[2]+=(mins[2]+maxs[2])*0.5;
}

bool RandomTPoint(int entity,float position[3],float angles[3])
{
    int spawns[256],count=0,spawn=-1;
    while((spawn=FindEntityByClassname(spawn,"info_player_terrorist"))!=-1 && count<256)spawns[count++]=spawn;
    if(!count)return false;
    float mins[3],maxs[3];NPCBounds(entity,mins,maxs);
    int first=GetRandomInt(0,count-1);
    for(int i=0;i<count;i++)
    {
        spawn=spawns[(first+i)%count];GetEntPropVector(spawn,Prop_Data,"m_vecOrigin",position);GetEntPropVector(spawn,Prop_Data,"m_angRotation",angles);
        if(!ClearHull(position,mins,maxs,entity))continue;
        float landing[3];landing=position;landing[2]-=34.0;
        Handle fall=TR_TraceHullFilterEx(position,landing,mins,maxs,MASK_NPCSOLID,FilterExcept,entity);
        bool fallClear=!TR_DidHit(fall) && !TR_StartSolid(fall) && !TR_AllSolid(fall);delete fall;
        if(!fallClear)continue;
        bool occupied=false;
        for(int n=0;n<MAX_NPCS;n++)
        {
            if(!gNPCs[n].used || !gNPCs[n].active || gNPCs[n].dead)continue;
            int other=Resolve(gNPCs[n].ref);if(other<0 || other==entity)continue;
            float p[3];GetEntPropVector(other,Prop_Data,"m_vecOrigin",p);
            if(GetVectorDistance(position,p)<96.0){occupied=true;break;}
        }
        if(!occupied)return true;
    }
    return false;
}

void NPCBounds(int entity,float mins[3],float maxs[3])
{
    GetEntPropVector(entity,Prop_Send,"m_vecMins",mins);GetEntPropVector(entity,Prop_Send,"m_vecMaxs",maxs);
    // CCollisionProperty m_vecMins/m_vecMaxs are the current collision bounds.
    // Do not multiply them a second time by render/model scale.
}

bool SafeHumanPoint(int client,float position[3],float angles[3])
{
    float mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0};
    int first=GetRandomInt(0,gCoreCount-1);
    for(int index=0;index<gCoreCount;index++)
    {
        int core=(first+index)%gCoreCount,b=gCores[core].building;
        if(b<0 || !gBuildings[b].alive || Resolve(gBuildings[b].ref)<0)continue;
        int start=GetRandomInt(0,gCores[core].pointCount-1);
        for(int j=0;j<gCores[core].pointCount;j++)
        {
            int point=(start+j)%gCores[core].pointCount;
            position=gCorePoints[core][point];angles=gCorePointAngles[core][point];
            if(!ClearHull(position,mins,maxs,client,MASK_PLAYERSOLID) || !InSafeBase(position))continue;
            bool occupied=false;
            for(int player=1;player<=MaxClients;player++)
            {
                if(player==client || !LiveHuman(player))continue;
                float p[3];GetClientAbsOrigin(player,p);if(GetVectorDistance(position,p)<40.0){occupied=true;break;}
            }
            if(!occupied)return true;
        }
    }
    return false;
}

int AimedEntity(int client,float range)
{
    float start[3],angles[3],direction[3],end[3];GetClientEyePosition(client,start);GetClientEyeAngles(client,angles);GetAngleVectors(angles,direction,NULL_VECTOR,NULL_VECTOR);
    for(int a=0;a<3;a++)end[a]=start[a]+direction[a]*range;
    Handle trace=TR_TraceRayFilterEx(start,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
    int entity=TR_DidHit(trace) ? TR_GetEntityIndex(trace) : -1;delete trace;return entity;
}

bool BoxesOverlap(const float amin[3],const float amax[3],const float bmin[3],const float bmax[3])
{
    for(int a=0;a<3;a++)if(amax[a]<bmin[a] || amin[a]>bmax[a])return false;
    return true;
}

void ModelWorldBounds(int entity,const float origin[3],const float angles[3],float mins[3],float maxs[3])
{
    float low[3],high[3];GetEntPropVector(entity,Prop_Send,"m_vecMins",low);GetEntPropVector(entity,Prop_Send,"m_vecMaxs",high);
    for(int a=0;a<3;a++){mins[a]=99999.0;maxs[a]=-99999.0;}
    for(int corner=0;corner<8;corner++)
    {
        float local[3],world[3];for(int a=0;a<3;a++)local[a]=(corner&(1<<a)) ? high[a] : low[a];
        WorldPoint(local,origin,angles,world);
        for(int a=0;a<3;a++){if(world[a]<mins[a])mins[a]=world[a];if(world[a]>maxs[a])maxs[a]=world[a];}
    }
}

bool ReservedBounds(const float mins[3],const float maxs[3])
{
    if(Node(C_Map,"BuildRestrictions/NPCSpawnColumns") && gCfg[C_Map].GotoFirstSubKey())
    {
        do
        {
            float low[3],high[3];gCfg[C_Map].GetVector("mins",low);gCfg[C_Map].GetVector("maxs",high);
            if(BoxesOverlap(mins,maxs,low,high))return true;
        }while(gCfg[C_Map].GotoNextKey());
    }
    if(Node(C_Map,"BuildRestrictions/BossSpawnColumn"))
    {
        float low[3],high[3];gCfg[C_Map].GetVector("mins",low);gCfg[C_Map].GetVector("maxs",high);
        if(BoxesOverlap(mins,maxs,low,high))return true;
    }
    // Human respawns select an available hull in SafeHumanPoint. Their candidate
    // points are not permanent build exclusions inside the shelter.
    return false;
}
