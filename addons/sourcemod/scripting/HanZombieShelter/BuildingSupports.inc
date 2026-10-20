// Explicit placement support, rooted in a watchtower platform. This is a
// logical destruction relation, not SetParent or a proximity/territory guess.
bool gBuildingSupportsSuspended,gBuildingSupportDraining;
int gBuildingSupportRuntime,gBuildingSupportQueueHead,gBuildingSupportQueueTail;
BuildingSupportIdentity gBuildingSupportQueue[MAX_BUILDINGS];

int BuildingSupportFindUID(int uid)
{
    if(uid<=0)return -1;
    for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].uid==uid)return b;
    return -1;
}

bool BuildingSupportLiving(int b)
{
    return b>=0 && b<MAX_BUILDINGS && gBuildings[b].used && gBuildings[b].alive && gBuildings[b].hp>0;
}

// Used by cleanup tools too: missing/deferred bodies do not hide logical users
// built above an owned fixture. Never traverses via a reused slot or a ref.
bool BuildingSupportDescendsFrom(int child,int ancestor)
{
    if(child<0 || child>=MAX_BUILDINGS || ancestor<0 || ancestor>=MAX_BUILDINGS
        || child==ancestor || !gBuildings[child].used || !gBuildings[ancestor].used)return false;
    int wanted=gBuildings[ancestor].uid,seen[MAX_BUILDINGS],count;
    for(int at=child,depth=0;depth<MAX_BUILDINGS;depth++)
    {
        int uid=gBuildings[at].supportUID;if(uid<=0)return false;
        if(uid==wanted)return true;
        for(int i=0;i<count;i++)if(seen[i]==uid)return false;
        seen[count++]=uid;at=BuildingSupportFindUID(uid);if(at<0)return false;
    }
    return false;
}

bool BuildingSupportChainValid(int b,bool requireBodies=true)
{
    int seen[MAX_BUILDINGS],count;
    for(int depth=0;depth<MAX_BUILDINGS;depth++)
    {
        if(!BuildingSupportLiving(b))return false;
        int uid=gBuildings[b].uid;
        for(int i=0;i<count;i++)if(seen[i]==uid)return false;
        seen[count++]=uid;
        if(requireBodies && Resolve(gBuildings[b].ref)<0)return false;
        if(gBuildings[b].supportUID<=0)return gBuildings[b].kind==Kind_Watchtower;
        int parent=BuildingSupportFindUID(gBuildings[b].supportUID);
        if(!BuildingSupportLiving(parent))return false;
        if(requireBodies && (gBuildings[b].supportRef!=gBuildings[parent].ref
            || gBuildings[b].supportGeneration!=gBuildings[parent].generation))return false;
        b=parent;
    }
    return false;
}

void ClearBuildingSupportLink(int b,bool preserveUID=false)
{
    if(b<0 || b>=MAX_BUILDINGS)return;
    if(!preserveUID)gBuildings[b].supportUID=0;
    gBuildings[b].supportRef=INVALID_ENT_REFERENCE;gBuildings[b].supportGeneration=0;
}

bool BuildingSupportTowerAnchorAllowed(int tower,const float anchor[3])
{
    if(WatchtowerAnchorOnPlatform(tower,anchor))return true;
    // The same tower's roof is also real support. Keep the deck's horizontal
    // rectangle and lower height, extending only up to this model's own top.
    // Thus a ray on a low leg or on nearby ground cannot create a relation.
    if(!WatchtowerBodyAlive(tower) || gBuildings[tower].constructing || gConstructionSolidPending[tower]
        || !gBuildings[tower].boundsKnown)return false;
    int profile=WatchtowerProfileForType(gBuildings[tower].type);if(profile<0)return false;
    float origin[3],angles[3],local[3];if(!WatchtowerBodyFrame(tower,origin,angles))return false;
    LocalPoint(anchor,origin,angles,local);
    float highest=gBuildings[tower].restoreMaxs[2];
    return local[0]>=gWatchtowerProfiles[profile].mins[0] && local[0]<=gWatchtowerProfiles[profile].maxs[0]
        && local[1]>=gWatchtowerProfiles[profile].mins[1] && local[1]<=gWatchtowerProfiles[profile].maxs[1]
        && local[2]>=gWatchtowerProfiles[profile].mins[2] && local[2]<=highest+0.5;
}

// Capture before spending/spawning from the final validated placement trace.
// A tower leg/nearby floor creates no relation; the upper platform and roof do.
bool CaptureBuildingSupport(PlacementCandidate candidate,BuildingSupportIdentity identity)
{
    BuildingSupportIdentity empty;empty.ref=INVALID_ENT_REFERENCE;identity=empty;
    if(gBuildingSupportsSuspended || gRebuilding || candidate.epoch!=gEpoch || candidate.revision!=gConfigRevision)return false;
    identity.epoch=gEpoch;
    int entity=candidate.surfaceRef==0 ? -1 : Resolve(candidate.surfaceRef);
    if(entity<0)return candidate.surfaceRef==0;
    int parent=FindBuilding(entity);if(parent<0)return true;
    if(!BuildingSupportLiving(parent) || gBuildings[parent].ref!=candidate.surfaceRef)return false;
    if(gBuildings[parent].kind==Kind_Watchtower)
    {
        if(!BuildingSupportTowerAnchorAllowed(parent,candidate.anchor))return true;
    }
    else if(gBuildings[parent].supportUID<=0)return true;
    if(!BuildingSupportChainValid(parent))return false;
    identity.uid=gBuildings[parent].uid;identity.ref=gBuildings[parent].ref;identity.generation=gBuildings[parent].generation;
    return true;
}

bool CommitBuildingSupport(int child,BuildingSupportIdentity identity)
{
    if(gBuildingSupportsSuspended || gRebuilding || identity.epoch!=gEpoch || !BuildingSupportLiving(child))return false;
    ClearBuildingSupportLink(child);
    if(identity.uid<=0)return true;
    int parent=BuildingSupportFindUID(identity.uid);
    if(!BuildingSupportLiving(parent) || parent==child || gBuildings[parent].ref!=identity.ref
        || gBuildings[parent].generation!=identity.generation || Resolve(identity.ref)<0
        || !BuildingSupportChainValid(parent) || BuildingSupportDescendsFrom(parent,child))return false;
    gBuildings[child].supportUID=identity.uid;gBuildings[child].supportRef=identity.ref;
    gBuildings[child].supportGeneration=identity.generation;return true;
}

// Rebuilding changes refs/generations, not UIDs. A missing parent model/edict
// remains pending; a deferred parent is never treated as a destruction event.
bool RebindBuildingSupport(int child)
{
    if(!BuildingSupportLiving(child))return false;
    ClearBuildingSupportLink(child,true);
    if(gBuildings[child].supportUID<=0)return true;
    int parent=BuildingSupportFindUID(gBuildings[child].supportUID);
    if(parent==child || !BuildingSupportChainValid(parent) || BuildingSupportDescendsFrom(parent,child))return false;
    gBuildings[child].supportRef=gBuildings[parent].ref;gBuildings[child].supportGeneration=gBuildings[parent].generation;
    return true;
}

bool BuildingSupportCanRestore(int child)
{
    return gBuildings[child].supportUID<=0 || RebindBuildingSupport(child);
}

void BuildingSupportBodyReady(int parent)
{
    if(!BuildingSupportLiving(parent) || Resolve(gBuildings[parent].ref)<0)return;
    RebindBuildingSupport(parent);
    int uid=gBuildings[parent].uid;
    for(int child=0;child<MAX_BUILDINGS;child++)
        if(BuildingSupportLiving(child) && gBuildings[child].supportUID==uid)RebindBuildingSupport(child);
}

void SuspendBuildingSupports()
{
    gBuildingSupportsSuspended=true;gBuildingSupportRuntime++;
    gBuildingSupportQueueHead=0;gBuildingSupportQueueTail=0;gBuildingSupportDraining=false;
    for(int b=0;b<MAX_BUILDINGS;b++)ClearBuildingSupportLink(b,true);
}

void ResumeBuildingSupports()
{
    // RestoreSavedBuilding waits for live parent bodies before child bodies;
    // this final pass also covers children retained while their parent deferred.
    for(int b=0;b<MAX_BUILDINGS;b++)if(BuildingSupportLiving(b))RebindBuildingSupport(b);
    gBuildingSupportsSuspended=false;
}

void PruneOrphanedBuildingSupports()
{
    if(gBuildingSupportsSuspended || gRebuilding || !Running())return;
    int epoch=gEpoch,runtime=gBuildingSupportRuntime;
    for(int child=0;child<MAX_BUILDINGS;child++)
    {
        if(epoch!=gEpoch || runtime!=gBuildingSupportRuntime || gBuildingSupportsSuspended || !Running())return;
        if(!BuildingSupportLiving(child) || gBuildings[child].supportUID<=0)continue;
        int parent=BuildingSupportFindUID(gBuildings[child].supportUID);
        // No model/edict is not proof of death. Alive logical parents retain
        // their children through capacity-delayed restore or a clean rebuild.
        if(!BuildingSupportLiving(parent))DamageBuilding(child,gBuildings[child].hp);
    }
}

void BuildingSupportParentLost(int uid,int ref,int generation)
{
    if(gBuildingSupportsSuspended || gRebuilding || !Running() || uid<=0)return;
    if(BuildingSupportLiving(BuildingSupportFindUID(uid)))return;
    for(int i=0;i<gBuildingSupportQueueTail;i++)
        if(gBuildingSupportQueue[i].uid==uid && gBuildingSupportQueue[i].ref==ref
            && gBuildingSupportQueue[i].generation==generation && gBuildingSupportQueue[i].epoch==gEpoch)return;
    if(gBuildingSupportQueueTail>=MAX_BUILDINGS)
    {LogError("[Shelter] support cascade reached bounded building limit");return;}
    int row=gBuildingSupportQueueTail++;
    gBuildingSupportQueue[row].uid=uid;gBuildingSupportQueue[row].ref=ref;
    gBuildingSupportQueue[row].generation=generation;gBuildingSupportQueue[row].epoch=gEpoch;
    if(gBuildingSupportDraining)return;
    gBuildingSupportDraining=true;int runtime=gBuildingSupportRuntime,epoch=gEpoch;
    while(gBuildingSupportQueueHead<gBuildingSupportQueueTail && runtime==gBuildingSupportRuntime
        && epoch==gEpoch && !gBuildingSupportsSuspended && !gRebuilding && Running())
    {
        BuildingSupportIdentity parent;parent=gBuildingSupportQueue[gBuildingSupportQueueHead++];
        if(parent.epoch!=gEpoch)continue;
        for(int child=0;child<MAX_BUILDINGS;child++)
        {
            if(runtime!=gBuildingSupportRuntime || epoch!=gEpoch || gBuildingSupportsSuspended || !Running())break;
            if(!BuildingSupportLiving(child) || gBuildings[child].supportUID!=parent.uid)continue;
            bool exact=gBuildings[child].supportRef==parent.ref && gBuildings[child].supportGeneration==parent.generation;
            bool saved=gBuildings[child].supportRef==INVALID_ENT_REFERENCE;
            if(!exact && !saved)continue;
            // DamageBuilding consumes this child's own identity first. Its loss
            // appends another queue entry instead of recursing through descendants.
            DamageBuilding(child,gBuildings[child].hp);
        }
    }
    if(runtime==gBuildingSupportRuntime)
    {gBuildingSupportQueueHead=0;gBuildingSupportQueueTail=0;gBuildingSupportDraining=false;}
}
