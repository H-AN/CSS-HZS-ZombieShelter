// One shared 20 Hz simulation; no timers/stock grenade explosions per shot.
// Swept hulls cover the whole travelled segment, including thin walls.
#define MAX_GRENADES 32
enum struct ShelterGrenade
{
    bool used;
    int ref;
    int epoch;
    int sourceRef;
    int serial;
    int damage;
    float radius;
    float position[3];
    float velocity[3];
    float lastTick;
    float expires;
}
ShelterGrenade gGrenades[MAX_GRENADES];
RewardContext gGrenadeRewardContext[MAX_GRENADES];

void RemoveGrenade(int p)
{
    int ref=gGrenades[p].ref;gGrenades[p].used=false;gGrenades[p].ref=INVALID_ENT_REFERENCE;
    RewardContext empty;gGrenadeRewardContext[p]=empty;RemoveOwnedRef(ref);
}
void ClearGrenades(){for(int p=0;p<MAX_GRENADES;p++)if(gGrenades[p].used)RemoveGrenade(p);}
int GrenadeCount(){int count;for(int p=0;p<MAX_GRENADES;p++)if(gGrenades[p].used)count++;return count;}

bool LaunchGrenade(int b,const float muzzle[3],const float target[3])
{
    if(!Running() || !BuildingPowered(b) || GrenadeCount()>=gGrenadeLimit || !CanAllocateEntities(1,true))return false;
    int slot=-1;for(int p=0;p<MAX_GRENADES;p++)if(!gGrenades[p].used){slot=p;break;}if(slot<0)return false;
    RewardContext context;if(!CaptureBuildingReward(b,context))return false;
    float delta[3];SubtractVectors(target,muzzle,delta);
    float flight=GetVectorLength(delta)/gBuildings[b].projectileSpeed;
    if(flight<0.05)flight=0.05;if(flight>=gGrenadeLifetime)return false;
    ShelterGrenade next;next.used=true;next.epoch=gEpoch;next.sourceRef=gBuildings[b].ref;
    next.serial=gBuildings[b].ownerSerial;next.damage=RoundToNearest(gBuildings[b].damage);next.radius=gBuildings[b].blastRadius;
    next.position=muzzle;next.lastTick=GetGameTime();next.expires=next.lastTick+gGrenadeLifetime;
    for(int a=0;a<3;a++)next.velocity[a]=delta[a]/flight;
    next.velocity[2]+=0.5*gGrenadeGravity*flight;
    int entity=CreateEntityByName("prop_dynamic_override");if(entity<0)return false;
    DispatchKeyValue(entity,"model",gGrenadeModel);DispatchKeyValue(entity,"solid","0");
    char name[64];Format(name,sizeof(name),"hzsh_grenade_%d_%d",gEpoch,slot);DispatchKeyValue(entity,"targetname",name);
    if(!DispatchSpawn(entity) || !CanAllocateEntities(1,true)){RemoveEntity(entity);return false;}
    SetEntProp(entity,Prop_Data,"m_takedamage",0,1);SetEntityMoveType(entity,MOVETYPE_NONE);
    TeleportEntity(entity,muzzle,NULL_VECTOR,NULL_VECTOR);next.ref=EntIndexToEntRef(entity);gGrenades[slot]=next;
    gGrenadeRewardContext[slot]=context;
    return true;
}

public bool GrenadeTraceFilter(int entity,int mask,any slot)
{
    if(slot<0 || slot>=MAX_GRENADES)return true;
    if(entity==Resolve(gGrenades[slot].ref) || entity==Resolve(gGrenades[slot].sourceRef))return false;
    char classname[64];if(entity>MaxClients && IsValidEntity(entity))
    {GetEntityClassname(entity,classname,sizeof(classname));if(StrEqual(classname,"light_dynamic"))return false;}
    return true;
}

void ExplodeGrenade(int p,const float position[3])
{
    if(!gGrenades[p].used)return;
    int damage=gGrenades[p].damage,epoch=gGrenades[p].epoch;float radius=gGrenades[p].radius;
    RewardContext context;context=gGrenadeRewardContext[p];
    RemoveGrenade(p); // Commit before re-entrant HZS death/award callbacks.
    if(!Running() || epoch!=gEpoch)return;
    TE_SetupExplosion(position,gExplosionSprite,1.5,15,0,RoundToNearest(radius),damage);TE_SendToAll();
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!Running() || epoch!=gEpoch)return;
        if(!DefenseTarget(n))continue;
        int entity=Resolve(gNPCs[n].ref);float center[3];EntityCenter(entity,center);
        if(GetVectorDistance(position,center)<=radius && LineClear(position,center,-1,entity))DamageNPCWithContext(n,damage,context);
    }
}

void StepGrenade(int p,float now)
{
    if(!gGrenades[p].used)return;
    int body=Resolve(gGrenades[p].ref);
    if(!Running() || gGrenades[p].epoch!=gEpoch || body<0 || now>=gGrenades[p].expires){RemoveGrenade(p);return;}
    float elapsed=now-gGrenades[p].lastTick;if(elapsed<=0.0)return;
    // After a server stall, drop the flight instead of advancing an unbounded
    // arc through geometry or detonating in a newly started round.
    if(elapsed>0.25){RemoveGrenade(p);return;}
    float end[3];
    for(int a=0;a<3;a++)end[a]=gGrenades[p].position[a]+gGrenades[p].velocity[a]*elapsed;
    end[2]-=0.5*gGrenadeGravity*elapsed*elapsed;
    float mins[3]={-2.0,-2.0,-2.0},maxs[3]={2.0,2.0,2.0};
    Handle trace=TR_TraceHullFilterEx(gGrenades[p].position,end,mins,maxs,MASK_SOLID,GrenadeTraceFilter,p);
    bool hit=TR_DidHit(trace) || TR_StartSolid(trace);
    if(hit)
    {
        float point[3],normal[3];TR_GetEndPosition(point,trace);TR_GetPlaneNormal(trace,normal);delete trace;
        for(int a=0;a<3;a++)point[a]+=normal[a]*3.0;
        ExplodeGrenade(p,point);return;
    }
    delete trace;
    for(int a=0;a<3;a++)if(FloatAbs(end[a])>15000.0){RemoveGrenade(p);return;}
    gGrenades[p].position=end;gGrenades[p].velocity[2]-=gGrenadeGravity*elapsed;gGrenades[p].lastTick=now;
    float angles[3];GetVectorAngles(gGrenades[p].velocity,angles);TeleportEntity(body,end,angles,NULL_VECTOR);
}

public Action GrenadeTimer(Handle timer)
{
    if(!Running()){ClearGrenades();return Plugin_Continue;}
    float now=GetGameTime();for(int p=0;p<MAX_GRENADES;p++)StepGrenade(p,now);return Plugin_Continue;
}

void GrenadeFire(int b,float now)
{
    if(now+0.00001<gBuildings[b].nextFire || !BuildingPowered(b))return;
    float muzzle[3];BuildingAttachmentPosition(b,gBuildings[b].meta.muzzleOffset,muzzle);
    int target=CachedTurretTarget(b,muzzle,now);if(target<0)return;
    float point[3];if(!AimDefenseBody(b,target,muzzle,point))return;
    if(!LaunchGrenade(b,muzzle,point))return;
    gBuildings[b].nextFire=NextFireDeadline(gBuildings[b].nextFire,now,gBuildings[b].interval);
    TraceDefenseShot(b,now,target);
    PlayBuildingSound(b,"attack");
}
