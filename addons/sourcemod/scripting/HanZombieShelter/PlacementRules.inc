// Geometry-only overlap: never trace the map when the policy asks only about
// NPC occupancy or the part of a defensive structure touching its territory.
bool PlacementBoundsValid(const float low[3],const float high[3])
{
    for(int a=0;a<3;a++)if(!(low[a]>=-15000.0 && high[a]<=15000.0 && high[a]>low[a]))return false;
    return true;
}

void PlacementBoxFrame(const float low[3],const float high[3],const float origin[3],const float angles[3],float center[3],float half[3],float basis[3][3])
{
    float local[3],zero[3];
    for(int a=0;a<3;a++){local[a]=(low[a]+high[a])*0.5;half[a]=(high[a]-low[a])*0.5;}
    WorldPoint(local,origin,angles,center);
    for(int a=0;a<3;a++){float unit[3];unit[a]=1.0;WorldPoint(unit,zero,angles,basis[a]);}
}

bool PlacementBoxesIntersect(const float lowA[3],const float highA[3],const float originA[3],const float anglesA[3],
    const float lowB[3],const float highB[3],const float originB[3],const float anglesB[3])
{
    if(!PlacementBoundsValid(lowA,highA) || !PlacementBoundsValid(lowB,highB))return false;
    float centerA[3],centerB[3],halfA[3],halfB[3],basisA[3][3],basisB[3][3],delta[3],axes[15][3];
    PlacementBoxFrame(lowA,highA,originA,anglesA,centerA,halfA,basisA);
    PlacementBoxFrame(lowB,highB,originB,anglesB,centerB,halfB,basisB);MakeVectorFromPoints(centerA,centerB,delta);
    for(int a=0;a<3;a++){axes[a]=basisA[a];axes[a+3]=basisB[a];}
    for(int a=0;a<3;a++)for(int b=0;b<3;b++)GetVectorCrossProduct(basisA[a],basisB[b],axes[6+a*3+b]);
    for(int a=0;a<15;a++)
    {
        if(GetVectorLength(axes[a],true)<0.000001)continue;
        float radiusA,radiusB;
        for(int b=0;b<3;b++)
        {radiusA+=halfA[b]*FloatAbs(GetVectorDotProduct(axes[a],basisA[b]));radiusB+=halfB[b]*FloatAbs(GetVectorDotProduct(axes[a],basisB[b]));}
        if(FloatAbs(GetVectorDotProduct(axes[a],delta))>radiusA+radiusB+0.01)return false;
    }
    return true;
}

bool ShelterOverlapsNPC(const char[] model,PlacementCandidate candidate,float modelScale=1.0)
{
    float low[3],high[3];
    // An unknown envelope cannot safely establish that the dormant Boss is free.
    if(!PlacementModelBounds(model,low,high,modelScale) || !PlacementBoundsValid(low,high))return true;
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!gNPCs[n].used || gNPCs[n].dead || gNPCs[n].registrationEpoch!=gEpoch)continue;
        int entity=Resolve(gNPCs[n].ref);if(entity<=MaxClients)continue;
        float npcLow[3],npcHigh[3],origin[3],zero[3];NPCBounds(entity,npcLow,npcHigh);
        GetEntPropVector(entity,Prop_Data,"m_vecAbsOrigin",origin);
        if(PlacementBoxesIntersect(low,high,candidate.origin,candidate.angles,npcLow,npcHigh,origin,zero))return true;
    }
    // Include native actors not yet admitted/bound to the external pool.
    if(GetFeatureStatus(FeatureType_Native,"Han_GetZombieCount")==FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native,"Han_GetZombieByIndex")==FeatureStatus_Available)
    {
        int count=Han_GetZombieCount();
        for(int i=0;i<count;i++)
        {
            int entity=Han_GetZombieByIndex(i);if(entity<=MaxClients || !IsValidEntity(entity) || FindNPC(entity)>=0)continue;
            float npcLow[3],npcHigh[3],origin[3],zero[3];NPCBounds(entity,npcLow,npcHigh);
            GetEntPropVector(entity,Prop_Data,"m_vecAbsOrigin",origin);
            if(PlacementBoxesIntersect(low,high,candidate.origin,candidate.angles,npcLow,npcHigh,origin,zero))return true;
        }
    }
    return false;
}

int RecipeBuildCoreAt(int recipe,PlacementCandidate candidate)
{
    int selected=BuildCoreAt(candidate.anchor);
    if(selected>=0 || recipe<0 || recipe>=gRecipeCount || gRecipes[recipe].kind!=Kind_Fence)return selected;
    char profile[128],model[PLATFORM_MAX_PATH];Format(profile,sizeof(profile),"Types/%s",gRecipes[recipe].type);
    Text(gRecipes[recipe].file,profile,"model",model,sizeof(model));
    float low[3],high[3];if(!PlacementModelBounds(model,low,high,gRecipes[recipe].meta.modelScale) || !PlacementBoundsValid(low,high))return -1;
    RefreshBuildAreas();float nearest=999999999.0;
    for(int c=0;c<gCoreCount;c++)
    {
        if(!gBuildAreas[c].active)continue;
        float local[3];LocalPoint(candidate.anchor,gCores[c].buildCenter,gCores[c].buildAngles,local);
        if(local[2]<gCores[c].buildZMin-BUILD_EPSILON || local[2]>gCores[c].buildZMax+BUILD_EPSILON)continue;
        float baseLow[3],baseHigh[3];baseLow[0]=-gBuildAreas[c].halfX;baseLow[1]=-gBuildAreas[c].halfY;baseLow[2]=gCores[c].buildZMin;
        baseHigh[0]=gBuildAreas[c].halfX;baseHigh[1]=gBuildAreas[c].halfY;baseHigh[2]=gCores[c].buildZMax;
        if(!PlacementBoxesIntersect(low,high,candidate.origin,candidate.angles,baseLow,baseHigh,gCores[c].buildCenter,gCores[c].buildAngles))continue;
        float distance=GetVectorDistance(candidate.anchor,gCores[c].buildCenter,true);
        if(selected<0 || distance<nearest-BUILD_EPSILON || (FloatAbs(distance-nearest)<=BUILD_EPSILON && strcmp(gCores[c].id,gCores[selected].id)<0))
        {selected=c;nearest=distance;}
    }
    return selected;
}
