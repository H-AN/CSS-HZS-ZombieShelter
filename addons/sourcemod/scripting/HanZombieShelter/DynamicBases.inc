// Player-created cores share the same stable core IDs, grids, inventories and
// defeat accounting as map cores. Slots 0..MAX_CORES-1 are reserved for cores.
int gFixedCoreCount,gDynamicBaseLimit;

bool LoadShelterSettings()
{
    gFixedCoreCount=gCoreCount;
    bool found;
    if(!StoredLevelInt(C_Shelters,"Limits","max_dynamic_bases",MAX_CORES-gFixedCoreCount,gDynamicBaseLimit,found)
        || gDynamicBaseLimit<0 || gDynamicBaseLimit>MAX_CORES)
        return ConfigFailure("shelters max_dynamic_bases must be 0..8");
    if(gDynamicBaseLimit>MAX_CORES-gFixedCoreCount)gDynamicBaseLimit=MAX_CORES-gFixedCoreCount;
    for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].file==C_Shelters)
    {
        char profile[128];Format(profile,sizeof(profile),"Types/%s",gRecipes[r].type);
        int ceiling;float buildLow,buildHigh,safeLow,safeHigh;
        if(!StoredLevelInt(C_Shelters,profile,"build_limit_max",128,ceiling,found)
            || ceiling<1 || ceiling>MAX_BUILDINGS-MAX_CORES
            || !StoredLevelFloat(C_Shelters,profile,"build_z_min",-128.0,buildLow,found)
            || !StoredLevelFloat(C_Shelters,profile,"build_z_max",512.0,buildHigh,found)
            || !StoredLevelFloat(C_Shelters,profile,"safe_z_min",-96.0,safeLow,found)
            || !StoredLevelFloat(C_Shelters,profile,"safe_z_max",384.0,safeHigh,found)
            || buildLow< -8192.0 || buildHigh>8192.0 || buildLow>=buildHigh
            || safeLow< -8192.0 || safeHigh>8192.0 || safeLow>=safeHigh)
            return ConfigFailure("invalid shelter territory height or building ceiling");
        if(!StrEqual(gRecipes[r].meta.placementMode,"upright"))
            return ConfigFailure("shelter placement must be upright");
    }
    return true;
}

void ResetDynamicBases()
{
    for(int c=gFixedCoreCount;c<MAX_CORES;c++)
    {
        CoreDef core;core.building=-1;gCores[c]=core;
        Building empty;empty.ref=INVALID_ENT_REFERENCE;empty.effectRef=INVALID_ENT_REFERENCE;gBuildings[c]=empty;
        for(int m=0;m<MATERIALS;m++)gBaseInventory[c][m]=0;
        BuildAreaCache area;gBuildAreas[c]=area;
    }
    gCoreCount=gFixedCoreCount;MarkBuildAreasDirty();
}

bool DynamicBasePositionAllowed(int recipe,const float anchor[3],bool report=false,int client=0)
{
    if(recipe<0 || recipe>=gRecipeCount || gRecipes[recipe].file!=C_Shelters)return false;
    if(gCoreCount>=MAX_CORES || gCoreCount-gFixedCoreCount>=gDynamicBaseLimit)
    {if(report)Tell(client,"%t","ShelterBaseLimit",gDynamicBaseLimit);return false;}
    // Reserve the old core areas even after destruction. Dynamic blueprints
    // reserve their own square, but grant no usable territory before completion.
    RefreshBuildAreas();
    for(int c=0;c<gCoreCount;c++)
    {
        float local[3];LocalPoint(anchor,gCores[c].origin,gCores[c].angles,local);
        for(int box=0;box<gCores[c].boxCount;box++)if(InBox(local,gCoreMins[c][box],gCoreMaxs[c][box]))
        {if(report)Tell(client,"%t","ShelterOutsideExistingBase");return false;}
        int b=gCores[c].building;if(b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used)continue;
        LocalPoint(anchor,gCores[c].buildCenter,gCores[c].buildAngles,local);
        float halfX=gBuildings[b].buildHalfExtents[0],halfY=gBuildings[b].buildHalfExtents[1];
        if(gBuildAreas[c].halfX>halfX)halfX=gBuildAreas[c].halfX;
        if(gBuildAreas[c].halfY>halfY)halfY=gBuildAreas[c].halfY;
        if(FloatAbs(local[0])<=halfX && FloatAbs(local[1])<=halfY
            && local[2]>=gCores[c].buildZMin && local[2]<=gCores[c].buildZMax)
        {if(report)Tell(client,"%t","ShelterOutsideExistingBase");return false;}
    }
    return true;
}

void SyncDynamicBasesGeometry()
{
    for(int c=gFixedCoreCount;c<gCoreCount;c++)
    {
        int b=gCores[c].building;
        if(!gCores[c].dynamic || b<0 || b>=MAX_CORES || !gBuildings[b].used)continue;
        char profile[128];Format(profile,sizeof(profile),"Types/%s",gBuildings[b].type);
        gCores[c].boxCount=1;
        gCoreMins[c][0][0]=-gBuildings[b].buildHalfExtents[0];gCoreMaxs[c][0][0]=gBuildings[b].buildHalfExtents[0];
        gCoreMins[c][0][1]=-gBuildings[b].buildHalfExtents[1];gCoreMaxs[c][0][1]=gBuildings[b].buildHalfExtents[1];
        gCoreMins[c][0][2]=Decimal(C_Shelters,profile,"safe_z_min",-96.0);
        gCoreMaxs[c][0][2]=Decimal(C_Shelters,profile,"safe_z_max",384.0);
    }
}

bool DynamicBaseHasSupport(int client,const float anchor[3],bool report)
{
    float down[3],up[3],normal[3];up=anchor;up[2]+=4.0;down=anchor;down[2]-=16.0;
    Handle ground=TR_TraceRayFilterEx(up,down,MASK_SOLID,RayType_EndPoint,FilterPlacement,client);
    bool supported=TR_DidHit(ground) && !TR_StartSolid(ground) && !TR_AllSolid(ground);
    if(supported)TR_GetPlaneNormal(ground,normal);delete ground;
    if(!supported || normal[2]<0.7)return PlacementError(client,"BuildInvalidSurface",report);
    return true;
}

stock bool BuildDynamicBase(int client,int recipe,int expectedCore=-1)
{
    if(!Running() || !LiveHuman(client) || recipe<0 || recipe>=gRecipeCount || gRecipes[recipe].file!=C_Shelters)return false;
    PlacementCandidate candidate;if(!ComputeRecipePlacementCandidate(client,recipe,candidate,true))return false;
    return BuildDynamicBaseAtCandidate(client,recipe,expectedCore,candidate);
}

bool BuildDynamicBaseAtCandidate(int client,int recipe,int expectedCore,PlacementCandidate candidate)
{
    if(!Running() || !LiveHuman(client) || recipe<0 || recipe>=gRecipeCount || gRecipes[recipe].file!=C_Shelters)return false;
    if(expectedCore>=0 && expectedCore!=gCoreCount){Tell(client,"%t","BuildMenuTargetChanged");return false;}
    if(!CheckConstructionBudget(client))return false;
    char candidateProfile[128],candidateModel[PLATFORM_MAX_PATH];Format(candidateProfile,sizeof(candidateProfile),"Types/%s",gRecipes[recipe].type);
    Text(C_Shelters,candidateProfile,"model",candidateModel,sizeof(candidateModel));
    if(!ValidatePlacementCandidate(client,candidateModel,gRecipes[recipe].meta,candidate,true)
        || !DynamicBasePositionAllowed(recipe,candidate.anchor,true,client) || !DynamicBaseHasSupport(client,candidate.anchor,true))return false;
    float anchor[3],origin[3],angles[3];anchor=candidate.anchor;origin=candidate.origin;angles=candidate.angles;
    if(!Spend(gRecipes[recipe].cost,client)){Tell(client,"%t","BuildPersonalMaterialsMissing");return false;}
    int c=gCoreCount;
    CoreDef core;core.dynamic=true;core.building=c;core.origin=anchor;core.buildCenter=anchor;
    core.angles[1]=angles[1];core.buildAngles=core.angles;
    Format(core.id,sizeof(core.id),"player_shelter_%d",gUID+1);
    strcopy(core.profile,sizeof(core.profile),gRecipes[recipe].type);
    char profile[128];Format(profile,sizeof(profile),"Types/%s",gRecipes[recipe].type);
    Text(C_Shelters,profile,"model",core.model,sizeof(core.model));
    core.buildHardLimit=Number(C_Shelters,profile,"build_limit_max",128);
    core.buildZMin=Decimal(C_Shelters,profile,"build_z_min",-128.0);core.buildZMax=Decimal(C_Shelters,profile,"build_z_max",512.0);
    gCores[c]=core;gCoreCount++;
    Building value;value.used=true;value.alive=true;value.kind=Kind_Base;value.uid=++gUID;value.core=c;value.buildCore=c;
    value.file=C_Shelters;value.level=1;value.ref=INVALID_ENT_REFERENCE;value.effectRef=INVALID_ENT_REFERENCE;
    value.origin=origin;value.angles=angles;value.placementAnchor=anchor;value.configRevision=gConfigRevision;value.meta=gRecipes[recipe].meta;
    value.powerCore=c;value.gridAssigned=true;
    strcopy(value.type,sizeof(value.type),gRecipes[recipe].type);strcopy(value.model,sizeof(value.model),core.model);gBuildings[c]=value;
    AssignBuildingOwner(c,client);
    for(int m=0;m<gMaterialCount;m++)gBuildings[c].invested[m]=gRecipes[recipe].cost[m];
    bool valid=ApplyBuildingLevel(c,true);
    if(valid){BeginBuildingConstruction(c);valid=SpawnBuildingBody(c);}
    if(!valid)
    {
        int ref=gBuildings[c].ref;gBuildings[c].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
        Building empty;empty.ref=INVALID_ENT_REFERENCE;gBuildings[c]=empty;
        CoreDef emptyCore;emptyCore.building=-1;gCores[c]=emptyCore;gCoreCount--;
        Refund(gRecipes[recipe].cost,client);MarkBuildAreasDirty();RecalculatePower();Tell(client,"%t","BuildEntityCreationFailed");return false;
    }
    SyncDynamicBasesGeometry();MarkBuildAreasDirty();RecalculatePower();PlayBuildingSound(c,"build");
    char name[128];TranslateLabel(client,gRecipes[recipe].label,name,sizeof(name));
    if(gBuildings[c].constructing)Tell(client,"%t","ConstructionIncomplete");
    else Tell(client,"%t","DefenseBuilt",name);
    return true;
}

// Keep blueprints' saved health/state. Destroyed player cores never reappear
// during rollback; map cores remain the recovery anchor for the campaign.
void RestoreDynamicBaseBodies()
{
    SyncDynamicBasesGeometry();
    for(int c=gFixedCoreCount;c<gCoreCount;c++)
    {
        int b=gCores[c].building;
        if(gCores[c].dynamic && b>=0 && gBuildings[b].used && gBuildings[b].alive
            && Resolve(gBuildings[b].ref)<0 && GetGameTime()>=gBuildings[b].restoreRetryAt)
            RestoreSavedBuilding(b);
    }
}

bool SafeHumanPointForDynamicBase(int client,int core,float position[3],float angles[3])
{
    if(core<0 || core>=gCoreCount || !gCores[core].dynamic || !BuildCoreAlive(core))return false;
    float mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0};
    int first=GetRandomInt(0,15),b=gCores[core].building;
    for(int offset=0;offset<16;offset++)
    {
        int point=(first+offset)%16;float radians=DegToRad(float(point%8)*45.0);
        float smaller=gBuildings[b].buildHalfExtents[0];if(gBuildings[b].buildHalfExtents[1]<smaller)smaller=gBuildings[b].buildHalfExtents[1];
        float radius=smaller*(point<8 ? 0.4 : 0.75);if(radius<48.0)continue;
        float local[3],probe[3],down[3],landing[3],normal[3];local[0]=Cosine(radians)*radius;local[1]=Sine(radians)*radius;local[2]=48.0;
        WorldPoint(local,gCores[core].origin,gCores[core].angles,probe);down=probe;down[2]-=160.0;
        Handle floor=TR_TraceHullFilterEx(probe,down,mins,maxs,MASK_PLAYERSOLID,FilterExcept,client);
        bool valid=TR_DidHit(floor) && !TR_StartSolid(floor) && !TR_AllSolid(floor);
        if(valid){TR_GetEndPosition(landing,floor);TR_GetPlaneNormal(floor,normal);}delete floor;
        if(!valid || normal[2]<0.7)continue;landing[2]+=1.0;
        LocalPoint(landing,gCores[core].origin,gCores[core].angles,local);
        if(TR_PointOutsideWorld(landing) || !InBox(local,gCoreMins[core][0],gCoreMaxs[core][0])
            || !ClearHull(landing,mins,maxs,client,MASK_PLAYERSOLID))continue;
        bool occupied;
        for(int other=1;other<=MaxClients;other++)if(other!=client && LiveHuman(other))
        {float p[3];GetClientAbsOrigin(other,p);if(GetVectorDistance(landing,p)<40.0){occupied=true;break;}}
        if(occupied)continue;position=landing;angles=gCores[core].angles;return true;
    }
    return false;
}
