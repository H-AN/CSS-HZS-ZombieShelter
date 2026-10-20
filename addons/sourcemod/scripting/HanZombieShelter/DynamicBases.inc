// Player-created cores share the same stable core IDs, grids, inventories and
// defeat accounting as map cores. Slots 0..MAX_CORES-1 are reserved for cores.
int gFixedCoreCount,gDynamicBaseLimit;
// Only explicitly canceled blueprints release a slot. Battle-destroyed bases
// retain their identity/reservation; live core indices are never compacted.
bool gDynamicBaseSlotReleased[MAX_CORES],gDynamicBaseSlotRetiring[MAX_CORES];
int gDynamicLayoutUID[MAX_CORES],gDynamicLayoutRevision[MAX_CORES];
int gDynamicLayoutBoxes[MAX_CORES],gDynamicLayoutPoints[MAX_CORES];
float gDynamicLayoutMins[MAX_CORES][MAX_BOXES][3],gDynamicLayoutMaxs[MAX_CORES][MAX_BOXES][3];
float gDynamicLayoutOrigins[MAX_CORES][MAX_POINTS][3],gDynamicLayoutAngles[MAX_CORES][MAX_POINTS][3];

// Optional template data is read once per base identity/config revision. These
// are coordinates only: no spawn entity, trigger or permanent trace is created.
bool ReadDynamicLayout(const char[] profile,int core=-1)
{
    int counts[2];
    for(int section=0;section<2;section++)
    {
        char path[256];Format(path,sizeof(path),"%s/DynamicLayout/%s",profile,section==0 ? "SafeBoxes" : "HumanPoints");
        ArrayList keys=new ArrayList(ByteCountToCells(64));
        if(Node(C_Shelters,path) && gCfg[C_Shelters].GotoFirstSubKey())
        {
            do{char key[64];gCfg[C_Shelters].GetSectionName(key,sizeof(key));keys.PushString(key);}while(gCfg[C_Shelters].GotoNextKey());
        }
        int limit=section==0 ? MAX_BOXES : MAX_POINTS;
        if(keys.Length>limit){delete keys;return false;}
        counts[section]=keys.Length;
        for(int i=0;i<keys.Length;i++)
        {
            char key[64],entry[320];keys.GetString(i,key,sizeof(key));Format(entry,sizeof(entry),"%s/%s",path,key);
            float first[3],second[3];
            char firstKey[16],secondKey[16];strcopy(firstKey,sizeof(firstKey),section==0 ? "mins" : "origin");
            strcopy(secondKey,sizeof(secondKey),section==0 ? "maxs" : "angles");
            if(!Node(C_Shelters,entry) || gCfg[C_Shelters].GetDataType(firstKey)==KvData_None
                || gCfg[C_Shelters].GetDataType(secondKey)==KvData_None
                || !ReadMetadataVector(C_Shelters,entry,firstKey,first)
                || !ReadMetadataVector(C_Shelters,entry,secondKey,second)){delete keys;return false;}
            for(int a=0;a<3;a++)if((section==0 && first[a]>=second[a]) || (section==1 && FloatAbs(second[a])>360.0))
            {delete keys;return false;}
            if(core>=0)
            {
                if(section==0){gDynamicLayoutMins[core][i]=first;gDynamicLayoutMaxs[core][i]=second;}
                else{gDynamicLayoutOrigins[core][i]=first;gDynamicLayoutAngles[core][i]=second;}
            }
        }
        delete keys;
    }
    if(core>=0){gDynamicLayoutBoxes[core]=counts[0];gDynamicLayoutPoints[core]=counts[1];}
    return true;
}

bool IsReleasedDynamicBaseSlot(int core)
{
    return core>=0 && core<MAX_CORES && gDynamicBaseSlotReleased[core];
}

int ExistingCoreCount()
{
    int count;
    for(int c=0;c<gCoreCount;c++)
    {
        int b=gCores[c].building;
        if(!gDynamicBaseSlotReleased[c] && b>=0 && b<MAX_BUILDINGS && gBuildings[b].used)count++;
    }
    return count;
}

int ReservedDynamicBaseCount()
{
    int count;for(int c=gFixedCoreCount;c<gCoreCount;c++)if(!gDynamicBaseSlotReleased[c])count++;return count;
}

int NextDynamicBaseSlot()
{
    for(int c=gFixedCoreCount;c<gCoreCount;c++)
        if(gDynamicBaseSlotReleased[c] && !gDynamicBaseSlotRetiring[c]
            && !gBuildings[c].used && !gBuildings[c].alive && !gBuildings[c].constructing)return c;
    return gCoreCount<MAX_CORES ? gCoreCount : -1;
}

bool CanReleaseUnfinishedDynamicBase(int b)
{
    if(b<gFixedCoreCount || b>=gCoreCount || b>=MAX_CORES || gDynamicBaseSlotReleased[b]
        || !gCores[b].dynamic || gCores[b].building!=b || gBuildings[b].kind!=Kind_Base
        || gBuildings[b].file!=C_Shelters || gBuildings[b].core!=b || !gBuildings[b].used
        || !gBuildings[b].alive || !gBuildings[b].constructing)return false;
    // A normal blueprint grants no territory. Reject any administrative or
    // exceptional live dependents instead of orphaning their stable core IDs.
    for(int other=0;other<MAX_BUILDINGS;other++)
        if(other!=b && gBuildings[other].used && gBuildings[other].alive
            && (gBuildings[other].buildCore==b || (gBuildings[other].gridAssigned && gBuildings[other].powerCore==b)))return false;
    return true;
}

void RetireCanceledDynamicBase(int core)
{
    gDynamicBaseSlotReleased[core]=true;gDynamicBaseSlotRetiring[core]=true;
    CoreDef empty;empty.dynamic=true;empty.building=core;gCores[core]=empty;
    BuildAreaCache area;gBuildAreas[core]=area;
    // Shared stock is drained after removal callbacks, while the released slot
    // is still unavailable for a replacement base with a different identity.
    MarkBuildAreasDirty();
}

void FinishCanceledDynamicBase(int core)
{
    for(int m=0;m<gMaterialCount;m++)
    {
        int amount=gBaseInventory[core][m];gBaseInventory[core][m]=0;
        if(amount>0)DepositSharedAutomatic(core,m,amount);
    }
    gDynamicBaseSlotRetiring[core]=false;
}

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
            || safeLow< -8192.0 || safeHigh>8192.0 || safeLow>=safeHigh || !ReadDynamicLayout(profile))
            return ConfigFailure("invalid shelter territory height or building ceiling");
        if(!StrEqual(gRecipes[r].meta.placementMode,"upright"))
            return ConfigFailure("shelter placement must be upright");
    }
    return true;
}

void ResetDynamicBases()
{
    for(int c=0;c<MAX_CORES;c++)
    {gDynamicBaseSlotReleased[c]=false;gDynamicBaseSlotRetiring[c]=false;gDynamicLayoutUID[c]=0;gDynamicLayoutRevision[c]=0;}
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
    for(int a=0;a<3;a++)if(!(anchor[a]>=-15000.0 && anchor[a]<=15000.0))return false;
    if(NextDynamicBaseSlot()<0 || ReservedDynamicBaseCount()>=gDynamicBaseLimit)
    {if(report)Tell(client,"%t","ShelterBaseLimit",gDynamicBaseLimit);return false;}
    // User chooses clipping/appearance. Spatial rejection is exclusively the
    // current NPC envelope check in ValidatePlacementCandidate, including Bosses.
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
        if(gDynamicLayoutUID[c]!=gBuildings[b].uid || gDynamicLayoutRevision[c]!=gConfigRevision)
        {
            if(!ReadDynamicLayout(profile,c)){gDynamicLayoutBoxes[c]=0;gDynamicLayoutPoints[c]=0;continue;}
            gDynamicLayoutUID[c]=gBuildings[b].uid;gDynamicLayoutRevision[c]=gConfigRevision;
        }
        if(gDynamicLayoutBoxes[c]>0)
        {
            gCores[c].boxCount=gDynamicLayoutBoxes[c];
            for(int box=0;box<gCores[c].boxCount;box++)
            {gCoreMins[c][box]=gDynamicLayoutMins[c][box];gCoreMaxs[c][box]=gDynamicLayoutMaxs[c][box];}
        }
        gCores[c].pointCount=gDynamicLayoutPoints[c];
        for(int p=0;p<gCores[c].pointCount;p++)
        {
            WorldPoint(gDynamicLayoutOrigins[c][p],gCores[c].origin,gCores[c].angles,gCorePoints[c][p]);
            gCorePointAngles[c][p]=gDynamicLayoutAngles[c][p];gCorePointAngles[c][p][1]+=gCores[c].angles[1];
        }
    }
}

stock bool DynamicBaseHasSupport(int client,const float anchor[3],bool report)
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
    if(gConstructionCancelBusy || !Running() || !LiveHuman(client) || recipe<0 || recipe>=gRecipeCount || gRecipes[recipe].file!=C_Shelters)return false;
    int required;if(!RecipeBaseLevelAllowed(recipe,required)){Tell(client,"%t","FacilityBaseLevelRequired",required);return false;}
    int c=NextDynamicBaseSlot();
    if(c<0 || (expectedCore>=0 && expectedCore!=c)){Tell(client,"%t","BuildMenuTargetChanged");return false;}
    if(!CheckConstructionBudget(client))return false;
    char candidateProfile[128],candidateModel[PLATFORM_MAX_PATH];Format(candidateProfile,sizeof(candidateProfile),"Types/%s",gRecipes[recipe].type);
    Text(C_Shelters,candidateProfile,"model",candidateModel,sizeof(candidateModel));
    if(!ValidatePlacementCandidate(client,candidateModel,gRecipes[recipe].meta,candidate,true,true,true)
        || !DynamicBasePositionAllowed(recipe,candidate.anchor,true,client))return false;
    float anchor[3],origin[3],angles[3];anchor=candidate.anchor;origin=candidate.origin;angles=candidate.angles;
    BuildingSupportIdentity support;if(!CaptureBuildingSupport(candidate,support))return false;
    if(!Spend(gRecipes[recipe].cost,client)){Tell(client,"%t","BuildPersonalMaterialsMissing");return false;}
    bool appended=c==gCoreCount;CoreDef previousCore;previousCore=gCores[c];Building previousBuilding;previousBuilding=gBuildings[c];
    gDynamicBaseSlotReleased[c]=false;gDynamicBaseSlotRetiring[c]=false;
    CoreDef core;core.dynamic=true;core.building=c;core.origin=anchor;core.buildCenter=anchor;
    core.angles[1]=angles[1];core.buildAngles=core.angles;
    Format(core.id,sizeof(core.id),"player_shelter_%d",gUID+1);
    strcopy(core.profile,sizeof(core.profile),gRecipes[recipe].type);
    char profile[128];Format(profile,sizeof(profile),"Types/%s",gRecipes[recipe].type);
    Text(C_Shelters,profile,"model",core.model,sizeof(core.model));
    core.buildHardLimit=Number(C_Shelters,profile,"build_limit_max",128);
    core.buildZMin=Decimal(C_Shelters,profile,"build_z_min",-128.0);core.buildZMax=Decimal(C_Shelters,profile,"build_z_max",512.0);
    gCores[c]=core;if(appended)gCoreCount++;
    Building value;value.used=true;value.alive=true;value.kind=Kind_Base;value.uid=++gUID;value.core=c;value.buildCore=c;
    value.file=C_Shelters;value.level=1;value.ref=INVALID_ENT_REFERENCE;value.effectRef=INVALID_ENT_REFERENCE;
    value.origin=origin;value.angles=angles;value.placementAnchor=anchor;value.configRevision=gConfigRevision;value.meta=gRecipes[recipe].meta;
    value.powerCore=c;value.gridAssigned=true;
    strcopy(value.type,sizeof(value.type),gRecipes[recipe].type);strcopy(value.model,sizeof(value.model),core.model);gBuildings[c]=value;
    AssignBuildingOwner(c,client);
    for(int m=0;m<gMaterialCount;m++)gBuildings[c].invested[m]=gRecipes[recipe].cost[m];
    bool valid=ApplyBuildingLevel(c,true);
    if(valid){BeginBuildingConstruction(c);valid=SpawnBuildingBody(c) && CommitBuildingSupport(c,support);}
    if(!valid)
    {
        int ref=gBuildings[c].ref;gBuildings[c].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
        if(appended)
        {
            Building empty;empty.ref=INVALID_ENT_REFERENCE;gBuildings[c]=empty;
            CoreDef emptyCore;emptyCore.building=-1;gCores[c]=emptyCore;gCoreCount--;
        }
        else
        {
            gBuildings[c]=previousBuilding;gCores[c]=previousCore;
            gDynamicBaseSlotReleased[c]=true;gDynamicBaseSlotRetiring[c]=false;
        }
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
    int count=gCores[core].pointCount>0 ? gCores[core].pointCount : 16;
    int first=GetRandomInt(0,count-1),b=gCores[core].building;
    for(int offset=0;offset<count;offset++)
    {
        int point=(first+offset)%count;float radians=DegToRad(float(point%8)*45.0);
        float smaller=gBuildings[b].buildHalfExtents[0];if(gBuildings[b].buildHalfExtents[1]<smaller)smaller=gBuildings[b].buildHalfExtents[1];
        float radius=smaller*(point<8 ? 0.4 : 0.75);if(radius<48.0)continue;
        float local[3],probe[3],down[3],landing[3],normal[3];local[0]=Cosine(radians)*radius;local[1]=Sine(radians)*radius;local[2]=48.0;
        if(gCores[core].pointCount>0){probe=gCorePoints[core][point];probe[2]+=48.0;}
        else WorldPoint(local,gCores[core].origin,gCores[core].angles,probe);
        down=probe;down[2]-=160.0;
        Handle floor=TR_TraceHullFilterEx(probe,down,mins,maxs,MASK_PLAYERSOLID,FilterExcept,client);
        bool valid=TR_DidHit(floor) && !TR_StartSolid(floor) && !TR_AllSolid(floor);
        if(valid){TR_GetEndPosition(landing,floor);TR_GetPlaneNormal(floor,normal);}delete floor;
        if(!valid || normal[2]<0.7)continue;landing[2]+=1.0;
        LocalPoint(landing,gCores[core].origin,gCores[core].angles,local);
        if(TR_PointOutsideWorld(landing) || !TravelPointInsideBase(core,landing)
            || !ClearHull(landing,mins,maxs,client,MASK_PLAYERSOLID))continue;
        bool occupied;
        for(int other=1;other<=MaxClients;other++)if(other!=client && LiveHuman(other))
        {float p[3];GetClientAbsOrigin(other,p);if(GetVectorDistance(landing,p)<40.0){occupied=true;break;}}
        if(occupied)continue;position=landing;angles=gCores[core].pointCount>0 ? gCorePointAngles[core][point] : gCores[core].angles;return true;
    }
    return false;
}
