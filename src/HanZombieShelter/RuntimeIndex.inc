// Ordered views of logical buildings. Rebuild only after structural changes;
// never swap-remove: callbacks may mutate slots during an ascending traversal.
#define RUNTIME_BUILDING_KINDS 20
bool gRuntimeBuildingsDirty=true;
int gRuntimeBuildingRevision=1;
int gRuntimeUsedBuildings[MAX_BUILDINGS],gRuntimeUsedCount;
int gRuntimeAliveBuildings[MAX_BUILDINGS],gRuntimeAliveCount;
int gRuntimeKindBuildings[RUNTIME_BUILDING_KINDS][MAX_BUILDINGS],gRuntimeKindCount[RUNTIME_BUILDING_KINDS];
int gRuntimeCoreBuildings[MAX_CORES][MAX_BUILDINGS],gRuntimeCoreCount[MAX_CORES];
#define RUNTIME_UID_BUCKETS 2048 // At most 1024 logical buildings, <= 50% occupancy.
int gRuntimeUIDKeys[RUNTIME_UID_BUCKETS],gRuntimeUIDSlots[RUNTIME_UID_BUCKETS];

void InvalidateRuntimeBuildings()
{
    gRuntimeBuildingsDirty=true;
    gRuntimeBuildingRevision=gRuntimeBuildingRevision>=2000000000 ? 1 : gRuntimeBuildingRevision+1;
}

void EnsureRuntimeBuildings()
{
    if(!gRuntimeBuildingsDirty)return;
    gRuntimeBuildingsDirty=false;gRuntimeUsedCount=0;gRuntimeAliveCount=0;
    for(int k=0;k<RUNTIME_BUILDING_KINDS;k++)gRuntimeKindCount[k]=0;
    for(int c=0;c<MAX_CORES;c++)gRuntimeCoreCount[c]=0;
    for(int key=0;key<RUNTIME_UID_BUCKETS;key++)gRuntimeUIDKeys[key]=0;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!gBuildings[b].used)continue;
        gRuntimeUsedBuildings[gRuntimeUsedCount++]=b;
        int uid=gBuildings[b].uid;
        if(uid>0)
        {
            int key=uid & (RUNTIME_UID_BUCKETS-1);
            while(gRuntimeUIDKeys[key]!=0 && gRuntimeUIDKeys[key]!=uid)key=(key+1) & (RUNTIME_UID_BUCKETS-1);
            if(gRuntimeUIDKeys[key]==0){gRuntimeUIDKeys[key]=uid;gRuntimeUIDSlots[key]=b;}
        }
        int core=gBuildings[b].powerCore;
        if(core>=0 && core<MAX_CORES)gRuntimeCoreBuildings[core][gRuntimeCoreCount[core]++]=b;
        if(!gBuildings[b].alive)continue;
        gRuntimeAliveBuildings[gRuntimeAliveCount++]=b;
        int kind=view_as<int>(gBuildings[b].kind);
        if(kind>=0 && kind<RUNTIME_BUILDING_KINDS)gRuntimeKindBuildings[kind][gRuntimeKindCount[kind]++]=b;
    }
}

int RuntimeFindBuildingUID(int uid)
{
    if(uid<=0)return -1;
    EnsureRuntimeBuildings();int key=uid & (RUNTIME_UID_BUCKETS-1);
    for(int attempt=0;attempt<RUNTIME_UID_BUCKETS;attempt++)
    {
        if(gRuntimeUIDKeys[key]==0)return -1;
        if(gRuntimeUIDKeys[key]==uid)
        {
            int b=gRuntimeUIDSlots[key];
            return gBuildings[b].used && gBuildings[b].uid==uid ? b:-1;
        }
        key=(key+1) & (RUNTIME_UID_BUCKETS-1);
    }
    return -1;
}

int RuntimeAfter(const int[] slots,int count,int after)
{
    int low=0,high=count;
    while(low<high)
    {
        int middle=(low+high)/2;
        if(slots[middle]<=after)low=middle+1;else high=middle;
    }
    return low<count ? slots[low] : -1;
}

int RuntimeNextBuilding(int after,int kind=-1,bool includeDead=false)
{
    EnsureRuntimeBuildings();
    if(includeDead)return RuntimeAfter(gRuntimeUsedBuildings,gRuntimeUsedCount,after);
    if(kind>=0 && kind<RUNTIME_BUILDING_KINDS)return RuntimeAfter(gRuntimeKindBuildings[kind],gRuntimeKindCount[kind],after);
    return RuntimeAfter(gRuntimeAliveBuildings,gRuntimeAliveCount,after);
}

int RuntimeNextPowerBuilding(int after,int core=-1)
{
    EnsureRuntimeBuildings();
    if(core>=0 && core<MAX_CORES)return RuntimeAfter(gRuntimeCoreBuildings[core],gRuntimeCoreCount[core],after);
    return RuntimeAfter(gRuntimeUsedBuildings,gRuntimeUsedCount,after);
}
