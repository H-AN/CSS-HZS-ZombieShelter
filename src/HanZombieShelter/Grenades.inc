// One shared 20 Hz simulation; no timers/stock grenade explosions per shot.
// Swept hulls cover the whole travelled segment, including thin walls.
#define MAX_GRENADES 32
enum struct ShelterGrenade
{
    bool used;
    int ref;
    int epoch;
    int sourceRef;
    int sourceUID;
    int shotToken;
    bool mortar;
    int serial;
    int damage;
    float radius;
    float gravity;
    float position[3];
    float velocity[3];
    float lastTick;
    float expires;
}
ShelterGrenade gGrenades[MAX_GRENADES];
RewardContext gGrenadeRewardContext[MAX_GRENADES];
int gGrenadeShotToken;
static const char sGrenadeExplosionDefaults[][]={"hzsh/han/explode3.wav","hzsh/han/explode4.wav","hzsh/han/explode5.wav"};
char gGrenadeExplosionSounds[sizeof(sGrenadeExplosionDefaults)][PLATFORM_MAX_PATH];
float gGrenadeExplosionVolume=1.0;

bool LoadGrenadeExplosionSounds()
{
    bool found;
    if(!StoredLevelFloat(C_Grenades,"Projectile","explosion_volume",1.0,gGrenadeExplosionVolume,found)
        || !(gGrenadeExplosionVolume>=0.0 && gGrenadeExplosionVolume<=1.0))return ConfigFailure("grenade explosion volume must be 0..1");
    for(int i=0;i<sizeof(gGrenadeExplosionSounds);i++)
    {
        char key[32],path[PLATFORM_MAX_PATH];Format(key,sizeof(key),"explosion_sound_%d",i+1);
        Text(C_Grenades,"Projectile",key,gGrenadeExplosionSounds[i],sizeof(gGrenadeExplosionSounds[]),sGrenadeExplosionDefaults[i]);
        TrimString(gGrenadeExplosionSounds[i]);
        if(StrContains(gGrenadeExplosionSounds[i],"sound/")==0)
            strcopy(gGrenadeExplosionSounds[i],sizeof(gGrenadeExplosionSounds[]),gGrenadeExplosionSounds[i][6]);
        if(!gGrenadeExplosionSounds[i][0] || gGrenadeExplosionSounds[i][0]=='/'
            || StrContains(gGrenadeExplosionSounds[i],"\\")>=0 || StrContains(gGrenadeExplosionSounds[i],":")>=0
            || StrContains(gGrenadeExplosionSounds[i],"..")>=0 || strlen(gGrenadeExplosionSounds[i])>sizeof(path)-7)
            return ConfigFailure("grenade explosion sound must be a relative sound path");
        Format(path,sizeof(path),"sound/%s",gGrenadeExplosionSounds[i]);
        if(!FileExists(path,true) || !PrecacheSound(gGrenadeExplosionSounds[i],true))
        {LogError("[Shelter] Missing or invalid grenade explosion sound %s",path);return ConfigFailure("grenade explosion sound");}
        AddFileToDownloadsTable(path);
    }
    return true;
}

void PlayGrenadeExplosionSound(const float position[3])
{
    if(gGrenadeExplosionVolume<=0.0)return;
    int sound=GetRandomInt(0,sizeof(gGrenadeExplosionSounds)-1);
    EmitAmbientSound(gGrenadeExplosionSounds[sound],position,SOUND_FROM_WORLD,SNDLEVEL_NORMAL,SND_NOFLAGS,gGrenadeExplosionVolume);
}

void RemoveGrenade(int p)
{
    if(p<0 || p>=MAX_GRENADES || !gGrenades[p].used)return;
    int ref=gGrenades[p].ref;gGrenades[p].used=false;gGrenades[p].ref=INVALID_ENT_REFERENCE;
    RewardContext empty;gGrenadeRewardContext[p]=empty;RemoveOwnedRef(ref);
}
void ClearGrenades(){for(int p=0;p<MAX_GRENADES;p++)if(gGrenades[p].used)RemoveGrenade(p);}
int GrenadeCount(){int count;for(int p=0;p<MAX_GRENADES;p++)if(gGrenades[p].used)count++;return count;}

bool GrenadeSourceValid(int b,int epoch,int uid,int ref,int generation,int revision)
{
    return b>=0 && b<MAX_BUILDINGS && Running() && !gRebuilding && epoch==gEpoch && revision==gConfigRevision
        && gBuildings[b].used && gBuildings[b].alive && gBuildings[b].kind==Kind_Grenade
        && gBuildings[b].uid==uid && gBuildings[b].ref==ref && gBuildings[b].generation==generation
        && gBuildings[b].configRevision==revision && Resolve(ref)>0 && BuildingPowered(b);
}

bool GrenadeShotValid(int p,int token,int epoch,int ref)
{
    return p>=0 && p<MAX_GRENADES && gGrenades[p].used && gGrenades[p].shotToken==token
        && gGrenades[p].epoch==epoch && gGrenades[p].ref==ref;
}

bool LaunchGrenade(int b,const float muzzle[3],const float target[3])
{
    if(b<0 || b>=MAX_BUILDINGS || !Running() || !BuildingPowered(b) || GrenadeCount()>=gGrenadeLimit
        || GrenadeCount()+ExpansionProjectileCount()>=MAX_GRENADES || !CanAllocateEntities(1,true))return false;
    int slot=-1;for(int p=0;p<MAX_GRENADES;p++)if(!gGrenades[p].used){slot=p;break;}if(slot<0)return false;
    RewardContext context;if(!CaptureBuildingReward(b,context))return false;
    int epoch=gEpoch,uid=gBuildings[b].uid,sourceRef=gBuildings[b].ref,generation=gBuildings[b].generation,revision=gConfigRevision;
    if(!GrenadeSourceValid(b,epoch,uid,sourceRef,generation,revision))return false;
    bool mortar=MortarTrajectory(gBuildings[b].file,gBuildings[b].type);
    float delta[3];SubtractVectors(target,muzzle,delta);
    for(int a=0;a<3;a++)if(!(muzzle[a]>=-15000.0 && muzzle[a]<=15000.0 && target[a]>=-15000.0 && target[a]<=15000.0))return false;
    float speed=gBuildings[b].projectileSpeed,gravity=gGrenadeGravity;
    if(!(speed>=1.0 && speed<=10000.0 && gravity>=0.0 && gravity<=1000.0 && gGrenadeLifetime>=0.5 && gGrenadeLifetime<=15.0))return false;
    float flight,velocity[3];
    if(mortar)
    {if(!MortarLaunchVelocity(muzzle,target,speed,gravity,gGrenadeLifetime,velocity,flight))return false;}
    else
    {
        flight=GetVectorLength(delta)/speed;if(flight<0.05)flight=0.05;if(flight>=gGrenadeLifetime)return false;
        for(int a=0;a<3;a++)velocity[a]=delta[a]/flight;
        velocity[2]+=0.5*gravity*flight;
    }
    ShelterGrenade next;next.used=true;next.epoch=epoch;next.sourceRef=sourceRef;next.sourceUID=uid;next.mortar=mortar;next.gravity=gravity;
    next.shotToken=++gGrenadeShotToken;if(next.shotToken<=0){gGrenadeShotToken=1;next.shotToken=1;}
    next.ref=INVALID_ENT_REFERENCE;
    next.serial=gBuildings[b].ownerSerial;next.damage=RoundToNearest(gBuildings[b].damage);next.radius=gBuildings[b].blastRadius;
    next.position=muzzle;next.lastTick=GetGameTime();next.expires=next.lastTick+gGrenadeLifetime;
    next.velocity=velocity;
    // Reserve before spawn/teleport callbacks can reenter and reuse this slot.
    gGrenades[slot]=next;gGrenadeRewardContext[slot]=context;
    int entity=CreateEntityByName("prop_dynamic_override");
    if(entity<=MaxClients){if(GrenadeShotValid(slot,next.shotToken,epoch,INVALID_ENT_REFERENCE))RemoveGrenade(slot);return false;}
    int projectileRef=EntIndexToEntRef(entity);
    DispatchKeyValue(entity,"model",gGrenadeModel);DispatchKeyValue(entity,"solid","0");
    char name[64];Format(name,sizeof(name),"hzsh_grenade_%d_%d",gEpoch,slot);DispatchKeyValue(entity,"targetname",name);
    bool spawned=DispatchSpawn(entity);
    if(!spawned || Resolve(projectileRef)!=entity || !CanAllocateEntities(1,true)
        || !GrenadeShotValid(slot,next.shotToken,epoch,INVALID_ENT_REFERENCE)
        || !GrenadeSourceValid(b,epoch,uid,sourceRef,generation,revision))
    {
        if(GrenadeShotValid(slot,next.shotToken,epoch,INVALID_ENT_REFERENCE))RemoveGrenade(slot);
        RemoveOwnedRef(projectileRef);return false;
    }
    SetEntProp(entity,Prop_Data,"m_takedamage",0,1);SetEntityMoveType(entity,MOVETYPE_NONE);
    TeleportEntity(entity,muzzle,NULL_VECTOR,NULL_VECTOR);
    if(Resolve(projectileRef)!=entity || !GrenadeShotValid(slot,next.shotToken,epoch,INVALID_ENT_REFERENCE)
        || !GrenadeSourceValid(b,epoch,uid,sourceRef,generation,revision))
    {
        if(GrenadeShotValid(slot,next.shotToken,epoch,INVALID_ENT_REFERENCE))RemoveGrenade(slot);
        RemoveOwnedRef(projectileRef);return false;
    }
    gGrenades[slot].ref=projectileRef;GrenadeProjectileTrail(entity,mortar);
    return true;
}

public bool GrenadeTraceFilter(int entity,int mask,any slot)
{
    if(slot<0 || slot>=MAX_GRENADES)return true;
    if(entity==Resolve(gGrenades[slot].ref) || entity==Resolve(gGrenades[slot].sourceRef))return false;
    if(IsBuildPreviewEntity(entity))return false;
    char classname[64];if(entity>MaxClients && IsValidEntity(entity))
    {GetEntityClassname(entity,classname,sizeof(classname));if(StrEqual(classname,"light_dynamic"))return false;}
    return true;
}

void ExplodeGrenade(int p,const float position[3])
{
    if(p<0 || p>=MAX_GRENADES || !gGrenades[p].used)return;
    int damage=gGrenades[p].damage,epoch=gGrenades[p].epoch;float radius=gGrenades[p].radius,expires=gGrenades[p].expires;
    bool mortar=gGrenades[p].mortar;
    RewardContext context;context=gGrenadeRewardContext[p];
    RemoveGrenade(p); // Commit before re-entrant HZS death/award callbacks.
    if(!Running() || epoch!=gEpoch || GetGameTime()>=expires)return;
    TE_SetupExplosion(position,gExplosionSprite,1.5,15,TE_EXPLFLAG_NOSOUND,RoundToNearest(radius),damage);TE_SendToAll();
    if(mortar)MortarImpactRing(position,radius);
    PlayGrenadeExplosionSound(position);
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
    if(p<0 || p>=MAX_GRENADES || !gGrenades[p].used)return;
    int body=Resolve(gGrenades[p].ref);
    if(!Running() || gGrenades[p].epoch!=gEpoch || body<0 || now>=gGrenades[p].expires){RemoveGrenade(p);return;}
    float elapsed=now-gGrenades[p].lastTick;if(elapsed<=0.0)return;
    // After a server stall, drop the flight instead of advancing an unbounded
    // arc through geometry or detonating in a newly started round.
    if(!(elapsed>0.0 && elapsed<=0.25)){RemoveGrenade(p);return;}
    int token=gGrenades[p].shotToken,epoch=gGrenades[p].epoch,ref=gGrenades[p].ref;
    int steps=RoundToCeil(elapsed/0.05);if(steps>5)steps=5;float step=elapsed/float(steps);
    for(int sample=0;sample<steps;sample++)
    {
        float end[3];
        for(int a=0;a<3;a++)end[a]=gGrenades[p].position[a]+gGrenades[p].velocity[a]*step;
        end[2]-=0.5*gGrenades[p].gravity*step*step;
        for(int a=0;a<3;a++)if(!(end[a]>=-15000.0 && end[a]<=15000.0)){RemoveGrenade(p);return;}
        float mins[3]={-2.0,-2.0,-2.0},maxs[3]={2.0,2.0,2.0};
        Handle trace=TR_TraceHullFilterEx(gGrenades[p].position,end,mins,maxs,MASK_SOLID,GrenadeTraceFilter,p);
        bool hit=TR_DidHit(trace) || TR_StartSolid(trace) || TR_AllSolid(trace);
        if(!GrenadeShotValid(p,token,epoch,ref) || !Running() || epoch!=gEpoch || Resolve(ref)!=body)
        {delete trace;if(GrenadeShotValid(p,token,epoch,ref))RemoveGrenade(p);return;}
        if(hit)
        {
            float point[3],normal[3];TR_GetEndPosition(point,trace);TR_GetPlaneNormal(trace,normal);delete trace;
            for(int a=0;a<3;a++)point[a]+=normal[a]*3.0;
            ExplodeGrenade(p,point);return;
        }
        delete trace;gGrenades[p].position=end;gGrenades[p].velocity[2]-=gGrenades[p].gravity*step;
    }
    gGrenades[p].lastTick=now;
    float angles[3];GetVectorAngles(gGrenades[p].velocity,angles);TeleportEntity(body,gGrenades[p].position,angles,NULL_VECTOR);
}

public Action GrenadeTimer(Handle timer)
{
    ExpansionProjectileTick(GetGameTime());
    if(!Running()){ClearGrenades();return Plugin_Continue;}
    float now=GetGameTime();for(int p=0;p<MAX_GRENADES;p++)StepGrenade(p,now);return Plugin_Continue;
}

void GrenadeFire(int b,float now)
{
    if(now+0.00001<gBuildings[b].nextFire || !BuildingPowered(b))return;
    float muzzle[3];BuildingAttachmentPosition(b,gBuildings[b].meta.muzzleOffset,muzzle);
    int target=CachedTurretTarget(b,muzzle,now);if(target<0)return;
    float point[3];if(!AimDefenseBody(b,target,muzzle,point))return;
    int epoch=gEpoch,uid=gBuildings[b].uid,sourceRef=gBuildings[b].ref,generation=gBuildings[b].generation,revision=gConfigRevision;
    if(!LaunchGrenade(b,muzzle,point) || !GrenadeSourceValid(b,epoch,uid,sourceRef,generation,revision))return;
    gBuildings[b].nextFire=NextFireDeadline(gBuildings[b].nextFire,now,gBuildings[b].interval);
    TraceDefenseShot(b,now,target);
    PlayBuildingSound(b,"attack");
}
