// Visuals only. Existing ApplyNPCBurn/DamageBurningNPC retain all damage,
// timing and captured reward ownership. Never Ignite/Extinguish the NPC: its
// m_hEffectEntity and FL_ONFIRE may belong to the unchanged HZS grenade system.
int gBurnVisualRef[MAX_NPCS],gBurnVisualNPCRef[MAX_NPCS],gBurnVisualTargetRef[MAX_NPCS],gBurnVisualEpoch[MAX_NPCS];
int gBurnVisualBlockedRef[MAX_NPCS];
int gBurnVisualTargetIndex[MAX_NPCS];

bool BurnVisualEntityFlame(int entity)
{
    if(entity<=MaxClients || !IsValidEntity(entity))return false;
    char classname[32];GetEntityClassname(entity,classname,sizeof(classname));
    return StrEqual(classname,"entityflame");
}

bool BurnVisualAttached(int flame,int target)
{
    return BurnVisualEntityFlame(flame) && target>MaxClients && IsValidEntity(target)
        && HasEntProp(flame,Prop_Send,"m_hEntAttached") && GetEntPropEnt(flame,Prop_Send,"m_hEntAttached")==target;
}

bool BurnVisualNativeFire(int entity)
{
    if(entity<=MaxClients || !IsValidEntity(entity))return false;
    if(GetEntityFlags(entity)&FL_ONFIRE)return true;
    if(!HasEntProp(entity,Prop_Data,"m_hEffectEntity"))return false;
    return BurnVisualEntityFlame(GetEntPropEnt(entity,Prop_Data,"m_hEffectEntity"));
}

void BurnVisualTag(int epoch,int npcRef,char[] tag,int length)
{
    Format(tag,length,"hzsh_burn_%d_%d",epoch,npcRef);
}

bool BurnVisualOwned(int n,int flame)
{
    if(n<0 || n>=MAX_NPCS || !BurnVisualEntityFlame(flame) || EntIndexToEntRef(flame)!=gBurnVisualRef[n]
        || !HasEntProp(flame,Prop_Data,"m_iName"))return false;
    char actual[96],expected[96];GetEntPropString(flame,Prop_Data,"m_iName",actual,sizeof(actual));
    BurnVisualTag(gBurnVisualEpoch[n],gBurnVisualNPCRef[n],expected,sizeof(expected));
    return StrEqual(actual,expected);
}

void ClearNPCBurnVisual(int n,bool preserveBlock=false)
{
    if(n<0 || n>=MAX_NPCS)return;
    int flame=Resolve(gBurnVisualRef[n]);bool owned=BurnVisualOwned(n,flame);
    // Consume before RemoveEntity can reenter NPC/entity destruction callbacks.
    gBurnVisualRef[n]=INVALID_ENT_REFERENCE;gBurnVisualNPCRef[n]=INVALID_ENT_REFERENCE;
    gBurnVisualTargetRef[n]=INVALID_ENT_REFERENCE;gBurnVisualEpoch[n]=0;
    gBurnVisualTargetIndex[n]=0;
    if(!preserveBlock)gBurnVisualBlockedRef[n]=INVALID_ENT_REFERENCE;
    if(!owned)return;
    SDKUnhook(flame,SDKHook_Think,BurnVisualThink);
    RemoveEntity(flame); // Exact owned EntRef/tag only. No ExtinguishEntity.
}

void OnBurnVisualEntityDestroyed(int entity)
{
    if(entity<=MaxClients)return;
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(gBurnVisualTargetIndex[n]!=entity || gBurnVisualTargetRef[n]==0
            || gBurnVisualTargetRef[n]==INVALID_ENT_REFERENCE)continue;
        // This may be a delayed callback for an old object whose index has
        // already been reused. A valid current target ref is never consumed.
        // If the engine calls before ref invalidation, the existing tick is
        // the safe fallback; do not treat a raw index as destruction authority.
        if(Resolve(gBurnVisualTargetRef[n])==entity)continue;
        ClearNPCBurnVisual(n);
    }
}

bool BurnVisualEligible(int n,float now)
{
    return n>=0 && n<MAX_NPCS && Running() && !gRebuilding && gNPCs[n].used && gNPCs[n].ready
        && gNPCs[n].active && !gNPCs[n].dead && !gNPCs[n].paused && gNPCs[n].registrationEpoch==gEpoch
        && gBurnRewardNPCRef[n]==gNPCs[n].ref && gNPCs[n].burnUntil>now && gNPCs[n].burnUntil<=now+3600.1
        && DefenseTarget(n);
}

int BurnVisualTarget(int n)
{
    int target=Resolve(gNPCs[n].visualRef);
    return target>MaxClients ? target : Resolve(gNPCs[n].ref);
}

// freshApplication is true only after a validated ApplyNPCBurn hit. An effect
// removed externally is treated as extinguished and is not recreated by ticks;
// a later real flame hit may ignite it again.
void SyncNPCBurnVisual(int n,float now,bool freshApplication=false)
{
    if(n<0 || n>=MAX_NPCS)return;
    if(!BurnVisualEligible(n,now)){ClearNPCBurnVisual(n);return;}
    int npcRef=gNPCs[n].ref,epoch=gEpoch,body=Resolve(npcRef),target=BurnVisualTarget(n);
    if(body<=MaxClients || target<=MaxClients){ClearNPCBurnVisual(n);return;}
    int targetRef=EntIndexToEntRef(target);
    if(freshApplication)gBurnVisualBlockedRef[n]=INVALID_ENT_REFERENCE;
    if(gBurnVisualNPCRef[n]!=npcRef || gBurnVisualEpoch[n]!=epoch || gBurnVisualTargetRef[n]!=targetRef)
        ClearNPCBurnVisual(n,true);
    // Yield to native HZS/map fire. No write, lifetime extension or cleanup of
    // its effect pointer/flags; our visual never becomes its shared fire source.
    if(BurnVisualNativeFire(body) || BurnVisualNativeFire(target))
    {ClearNPCBurnVisual(n,true);return;}
    int flame=Resolve(gBurnVisualRef[n]);
    if(gBurnVisualRef[n]!=0 && gBurnVisualRef[n]!=INVALID_ENT_REFERENCE && !BurnVisualOwned(n,flame))
    {
        // Removal/ref reuse/tag change is not permission to adopt another fire.
        gBurnVisualBlockedRef[n]=npcRef;ClearNPCBurnVisual(n,true);
        if(!freshApplication)return;
        gBurnVisualBlockedRef[n]=INVALID_ENT_REFERENCE;
    }
    if(gBurnVisualBlockedRef[n]==npcRef)return;
    if(BurnVisualOwned(n,flame))
    {
        if(!BurnVisualAttached(flame,target))
        {gBurnVisualBlockedRef[n]=npcRef;ClearNPCBurnVisual(n,true);return;}
        SetEntPropFloat(flame,Prop_Data,"m_flLifetime",gNPCs[n].burnUntil);
        return;
    }
    if(!CanAllocateEntities(1,true))return; // Damage still works when optional visuals cannot fit.
    flame=CreateEntityByName("entityflame");if(flame<=MaxClients)return;
    int flameRef=EntIndexToEntRef(flame);
    if(!HasEntProp(flame,Prop_Send,"m_hEntAttached") || !HasEntProp(flame,Prop_Data,"m_flLifetime")
        || !HasEntProp(flame,Prop_Data,"m_iName") || !SDKHookEx(flame,SDKHook_Think,BurnVisualThink))
    {RemoveEntity(flame);return;}
    char tag[96];BurnVisualTag(epoch,npcRef,tag,sizeof(tag));DispatchKeyValue(flame,"targetname",tag);
    if(!DispatchSpawn(flame) || Resolve(flameRef)!=flame)
    {if(Resolve(flameRef)>0){SDKUnhook(flame,SDKHook_Think,BurnVisualThink);RemoveEntity(flame);}return;}
    // Spawn may run external callbacks. Never attach to a replacement NPC/model,
    // a new epoch, a newly native-burning target, or an ended custom burn.
    if(!BurnVisualEligible(n,now) || gEpoch!=epoch || gNPCs[n].ref!=npcRef || BurnVisualTarget(n)!=target
        || Resolve(targetRef)!=target || BurnVisualNativeFire(body) || BurnVisualNativeFire(target))
    {SDKUnhook(flame,SDKHook_Think,BurnVisualThink);RemoveEntity(flame);return;}
    gBurnVisualRef[n]=flameRef;gBurnVisualNPCRef[n]=npcRef;gBurnVisualTargetRef[n]=targetRef;gBurnVisualEpoch[n]=epoch;gBurnVisualTargetIndex[n]=target;
    SetEntPropEnt(flame,Prop_Send,"m_hEntAttached",target);
    SetEntPropFloat(flame,Prop_Data,"m_flLifetime",gNPCs[n].burnUntil);
    // Match Valve CEntityFlame::Create: UTIL_SetOrigin(target.GetAbsOrigin())
    // precedes AttachToEntity/SetParent. A raw local m_vecOrigin write after
    // parenting does not run the engine's absolute-origin/PVS invalidation.
    float targetOrigin[3];GetEntPropVector(target,Prop_Data,"m_vecAbsOrigin",targetOrigin);
    for(int a=0;a<3;a++)if(!(targetOrigin[a]>=-15000.0 && targetOrigin[a]<=15000.0))
    {ClearNPCBurnVisual(n);return;}
    TeleportEntity(flame,targetOrigin,NULL_VECTOR,NULL_VECTOR);
    if(Resolve(flameRef)!=flame || !BurnVisualOwned(n,flame) || gEpoch!=epoch || gNPCs[n].ref!=npcRef
        || Resolve(targetRef)!=target || !BurnVisualAttached(flame,target))
    {ClearNPCBurnVisual(n);return;}
    SetVariantString("!activator");AcceptEntityInput(flame,"SetParent",target);
    if(Resolve(flameRef)!=flame || !BurnVisualOwned(n,flame) || gEpoch!=epoch || gNPCs[n].ref!=npcRef
        || Resolve(targetRef)!=target || !BurnVisualAttached(flame,target))
    {ClearNPCBurnVisual(n);return;}
    // Valve Create calls AddEFlags, which also recalculates transmit state.
    // A raw m_iEFlags write alone leaves CS:S's model-less flame cached as
    // FL_EDICT_DONTSEND. Real same-entity comparison confirmed that explicitly
    // clearing DONTSEND makes its existing size-zero effect visibly burn.
    // These flags belong only to our bounded optional flame, never the NPC.
    if(HasEntProp(flame,Prop_Data,"m_iEFlags"))
        SetEntProp(flame,Prop_Data,"m_iEFlags",GetEntProp(flame,Prop_Data,"m_iEFlags")|(1<<7));
    SetEdictFlags(flame,(GetEdictFlags(flame)&~FL_EDICT_DONTSEND)|FL_EDICT_ALWAYS);
    if(!BurnVisualAttached(flame,target))ClearNPCBurnVisual(n);
}

// A manually spawned visual normally has no native FlameThink installed. This
// guard also blocks a branch that does schedule it: vanilla FlameThink causes
// radius/direct damage and can Extinguish a shared character on expiry. Neither
// belongs to this purely visual effect. Lifetime is owned by BurnVisualTick.
public Action BurnVisualThink(int flame)
{
    for(int n=0;n<MAX_NPCS;n++)if(gBurnVisualRef[n]!=0 && gBurnVisualRef[n]!=INVALID_ENT_REFERENCE
        && Resolve(gBurnVisualRef[n])==flame && BurnVisualOwned(n,flame))return Plugin_Handled;
    return Plugin_Continue;
}

void BurnVisualTick(float now)
{
    for(int n=0;n<MAX_NPCS;n++)
        if((gNPCs[n].used && gNPCs[n].burnUntil>now) || (gBurnVisualRef[n]!=0 && gBurnVisualRef[n]!=INVALID_ENT_REFERENCE))
            SyncNPCBurnVisual(n,now);
}

void ClearBurnVisuals()
{
    for(int n=0;n<MAX_NPCS;n++)ClearNPCBurnVisual(n);
}
