// Construction reuses logical durability, with a separate state for activation.
#define CONSTRUCTION_ALPHA 125
ConVar gConstructionCvar;
bool gConstructionConfiguring;
bool gConstructionCompleting;
bool gConstructionSolidPending[MAX_BUILDINGS];
int gConstructionSolidRef[MAX_BUILDINGS],gConstructionSolidUID[MAX_BUILDINGS],gConstructionSolidEpoch[MAX_BUILDINGS];
int gConstructionSolidCount;
float gNextConstructionSafety;

void RegisterConstructionCvar()
{
    gConstructionCvar=CreateConVar("sm_zsh_real_construction","0","New player buildings start at 1 HP; weapon hits add the configured construction HP until complete.",FCVAR_NOTIFY,true,0.0,true,1.0);
    gConstructionCvar.AddChangeHook(ConstructionModeChanged);
}

void ConfigureRealConstruction(bool enabled)
{
    if(gConstructionCvar==null)return;
    gConstructionConfiguring=true;gConstructionCvar.BoolValue=enabled;gConstructionConfiguring=false;
}

bool BuildingOperational(int b)
{
    return b>=0 && b<MAX_BUILDINGS && gBuildings[b].used && gBuildings[b].alive
        && gBuildings[b].hp>0 && !gBuildings[b].constructing;
}

void BeginBuildingConstruction(int b)
{
    if(gConstructionCvar==null || !gConstructionCvar.BoolValue)return;
    gBuildings[b].constructionTarget=gBuildings[b].maxHP;
    gBuildings[b].constructing=gBuildings[b].maxHP>1;
    if(gBuildings[b].constructing)gBuildings[b].hp=1;
}

void ForgetConstructionSolid(int b)
{
    if(gConstructionSolidPending[b] && gConstructionSolidCount>0)gConstructionSolidCount--;
    gConstructionSolidPending[b]=false;
    gConstructionSolidRef[b]=INVALID_ENT_REFERENCE;gConstructionSolidUID[b]=0;gConstructionSolidEpoch[b]=0;
}

// Direct clipping tests this model's solid collision, including PHY cavities.
// A world-space model AABB would falsely block hardening an occupied open base.
bool ConstructionPlayerOverlaps(int entity,int client)
{
    float origin[3],mins[3],maxs[3];GetClientAbsOrigin(client,origin);GetClientMins(client,mins);GetClientMaxs(client,maxs);
    // Exclude ordinary resting/touching contact from interpenetration. These are
    // the current client hull bounds, so crouching does not use standing height.
    for(int axis=0;axis<3;axis++)
        if(maxs[axis]-mins[axis]>0.25){mins[axis]+=0.125;maxs[axis]-=0.125;}
    Handle trace=TR_ClipRayHullToEntityEx(origin,origin,mins,maxs,MASK_PLAYERSOLID,entity);
    bool overlapping=TR_StartSolid(trace) || TR_AllSolid(trace) || TR_DidHit(trace);delete trace;return overlapping;
}

bool ConstructionHasPlayerOverlap(int entity)
{
    for(int client=1;client<=MaxClients;client++)
        if(LiveHuman(client) && !IsFakeClient(client) && ConstructionPlayerOverlaps(entity,client))return true;
    return false;
}

void SyncConstructionCollision(int b,int entity)
{
    ForgetConstructionSolid(b);
    // Group 2 leaves physical weapon/knife traces enabled throughout the check.
    SetEntityCollisionGroup(entity,2);
    if(gBuildings[b].constructing)return;
    if(!ConstructionHasPlayerOverlap(entity)){SetEntityCollisionGroup(entity,0);return;}
    gConstructionSolidPending[b]=true;gConstructionSolidCount++;
    gConstructionSolidRef[b]=gBuildings[b].ref;gConstructionSolidUID[b]=gBuildings[b].uid;gConstructionSolidEpoch[b]=gEpoch;
}

// Invoke from the shared safety timer even while gameplay is paused. No
// per-building timer, player movement, resource transaction or NPC mutation.
void ConstructionSafetyTick(float now)
{
    if(gConstructionSolidCount<1 || now<gNextConstructionSafety)return;
    gNextConstructionSafety=now+0.1;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!gConstructionSolidPending[b])continue;
        int entity=Resolve(gConstructionSolidRef[b]);
        if(entity<0 || gConstructionSolidEpoch[b]!=gEpoch || gConstructionSolidUID[b]!=gBuildings[b].uid
            || gConstructionSolidRef[b]!=gBuildings[b].ref || !BuildingOperational(b))
        {ForgetConstructionSolid(b);continue;}
        if(ConstructionHasPlayerOverlap(entity))continue;
        // Consume the claim first: collision updates cannot replay completion.
        ForgetConstructionSolid(b);SetEntityCollisionGroup(entity,0);
    }
}

void ClearConstructionSafety()
{
    for(int b=0;b<MAX_BUILDINGS;b++)ForgetConstructionSolid(b);
    gConstructionSolidCount=0;gNextConstructionSafety=0.0;
}

stock void RefreshConstructionVisuals()
{
    for(int b=0;b<MAX_BUILDINGS;b++)
        if(gBuildings[b].used && gBuildings[b].alive && Resolve(gBuildings[b].ref)>0)SyncConstructionVisual(b);
}

void SyncConstructionVisual(int b)
{
    int entity=Resolve(gBuildings[b].ref);if(entity<0)return;
    SetEntityRenderMode(entity,RENDER_TRANSCOLOR);
    int alpha=gBuildings[b].constructing ? CONSTRUCTION_ALPHA : gBuildings[b].meta.render[3];
    // Map bases have no type metadata; completed dynamic bases do.
    if(gBuildings[b].kind==Kind_Base && !gBuildings[b].meta.label[0])
        SetEntityRenderColor(entity,255,255,255,gBuildings[b].constructing ? CONSTRUCTION_ALPHA : 255);
    else SetEntityRenderColor(entity,gBuildings[b].meta.render[0],gBuildings[b].meta.render[1],gBuildings[b].meta.render[2],alpha);
    SyncConstructionCollision(b,entity);
}

void CompleteBuildingConstruction(int b,int client=0)
{
    if(!gBuildings[b].constructing)return;
    gBuildings[b].hp=gBuildings[b].constructionTarget;
    gBuildings[b].constructing=false;
    if(gBuildings[b].kind==Kind_Campfire)CampfireConstructionCompleted(b);
    gBuildings[b].nextFire=GetGameTime()+gBuildings[b].interval;
    gBuildings[b].nextRestore=GetGameTime()+gBuildings[b].restoreInterval;
    SyncConstructionVisual(b);InvalidateDefenseCache(b);MarkBuildAreasDirty();RecalculatePower();
    PlayBuildingSound(b,"complete");
    if(client>0)Tell(client,"%t","ConstructionComplete");
}

public void ConstructionModeChanged(ConVar variable,const char[] oldValue,const char[] newValue)
{
    if(gConstructionConfiguring || gConstructionCompleting || variable.BoolValue)return;
    // Disabling does not strand existing ghosts; re-enabling never resets buildings.
    gConstructionCompleting=true;
    for(int b=0;b<MAX_BUILDINGS;b++)
        if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].constructing)CompleteBuildingConstruction(b);
    gConstructionCompleting=false;
}
