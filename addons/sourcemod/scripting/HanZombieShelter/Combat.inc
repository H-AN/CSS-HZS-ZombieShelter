int BlockingBuilding(int n,int client)
{
    int entity=Resolve(gNPCs[n].ref);if(entity<0)return -1;
    float origin[3],eye[3],target[3];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",origin);GetClientAbsOrigin(client,target);
    float maximum=Decimal(C_Zombies,"BuildingAttack","contact_distance",64.0);
    if(gNPCs[n].boss>=0)maximum*=1.5;
    float direction[3];MakeVectorFromPoints(origin,target,direction);
    if(GetVectorLength(direction)>maximum)
    {
        NormalizeVector(direction,direction);
        for(int a=0;a<3;a++)target[a]=origin[a]+direction[a]*maximum;
    }
    // Trace the actual walking hull: windows and low door lintels can block
    // movement even when the player is visible through them.
    float hullMin[3],hullMax[3];NPCBounds(entity,hullMin,hullMax);
    Handle route=TR_TraceHullFilterEx(origin,target,hullMin,hullMax,MASK_NPCSOLID,FilterNoClients,entity);
    int routeHit=TR_DidHit(route) ? TR_GetEntityIndex(route) : -1;delete route;
    int candidate=FindBuilding(routeHit);
    // Sloping ground may be the first hull hit. Lower-body rays retain wall contact.
    for(int height=0;height<3 && candidate<0;height++)
    {
        eye=origin;float destination[3];destination=target;
        eye[2]+=18.0+float(height)*18.0;destination[2]+=18.0+float(height)*18.0;
        Handle trace=TR_TraceRayFilterEx(eye,destination,MASK_NPCSOLID,RayType_EndPoint,FilterNoClients,entity);
        int hit=TR_DidHit(trace) ? TR_GetEntityIndex(trace) : -1;delete trace;candidate=FindBuilding(hit);
    }
    if(candidate<0 && gNPCs[n].touchedUntil>GetGameTime())
        for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].uid==gNPCs[n].touchedUID){candidate=b;break;}
    if(candidate<0 || !gBuildings[candidate].alive || gBuildings[candidate].kind==Kind_Mine)return -1;
    int body=Resolve(gBuildings[candidate].ref);if(body<0)return -1;
    float local[3],mins[3],maxs[3],closest[3];LocalPoint(origin,gBuildings[candidate].origin,gBuildings[candidate].angles,local);
    GetEntPropVector(body,Prop_Send,"m_vecMins",mins);GetEntPropVector(body,Prop_Send,"m_vecMaxs",maxs);
    for(int a=0;a<3;a++){if(local[a]<mins[a])local[a]=mins[a];if(local[a]>maxs[a])local[a]=maxs[a];}
    WorldPoint(local,gBuildings[candidate].origin,gBuildings[candidate].angles,closest);
    if(GetVectorDistance(origin,closest)>maximum)return -1;
    return candidate;
}

RewardContext gDamageRewardContext,gBurnRewardContext[MAX_NPCS];
int gBurnRewardNPCRef[MAX_NPCS];
bool gDefenseTrace;

public Action DefenseTraceCommand(int client,int args)
{
    if(args!=1){ReplyToCommand(client,"Usage: sm_zsh_defense_trace <0|1>");return Plugin_Handled;}
    char value[8];GetCmdArg(1,value,sizeof(value));
    if(!StrEqual(value,"0") && !StrEqual(value,"1")){ReplyToCommand(client,"Expected 0 or 1");return Plugin_Handled;}
    gDefenseTrace=StrEqual(value,"1");ReplyToCommand(client,"Defense shot logging: %d",gDefenseTrace);return Plugin_Handled;
}

void TraceDefenseShot(int b,float now,int target)
{
#if defined HZSH_TEST_REALFIRE
    V05CaptureRealShot(b,now,target);
#endif
    if(gDefenseTrace)LogMessage("[ShelterDefense] epoch=%d uid=%d group=%s interval=%.4f fired=%.6f target_ref=%d",
        gEpoch,gBuildings[b].uid,gBuildings[b].type,gBuildings[b].interval,now,gNPCs[target].ref);
}

void ClearNPCRewardContext(int n)
{
    RewardContext empty;gBurnRewardContext[n]=empty;gBurnRewardNPCRef[n]=INVALID_ENT_REFERENCE;
    gNPCs[n].burnUntil=0.0;
}

bool CaptureBuildingReward(int b,RewardContext context)
{
    RewardContext empty;context=empty;
    if(b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive)return false;
    context.valid=true;context.facility=true;context.epoch=gEpoch;
    context.sourceUID=gBuildings[b].uid;context.sourceRef=gBuildings[b].ref;
    context.buildCore=gBuildings[b].buildCore;context.ownerSerial=gBuildings[b].ownerSerial;
    strcopy(context.ownerKey,sizeof(context.ownerKey),gBuildings[b].ownerKey);return true;
}

bool GetDeathRewardContext(int n,int killer,RewardContext context)
{
    RewardContext empty;context=empty;
    if(n<0 || n>=MAX_NPCS)return false;
    if(gDamageNPCRef==gNPCs[n].ref)
    {
        // A HZS proxy is only the delivery client. Never fall back to it when
        // an external source deliberately has no eligible reward identity.
        if(!gDamageRewardContext.valid || gDamageRewardContext.epoch!=gEpoch)return false;
        context=gDamageRewardContext;return true;
    }
    if(killer<1 || killer>MaxClients || !IsClientInGame(killer) || IsFakeClient(killer))return false;
    context.valid=true;context.epoch=gEpoch;context.buildCore=-1;context.ownerSerial=GetClientSerial(killer);
    strcopy(context.ownerKey,sizeof(context.ownerKey),gInventoryOwner[killer]);return true;
}

int DamageCredit(int serial)
{
    int client=GetClientFromSerial(serial);if(LiveHuman(client))return client;
    for(int i=1;i<=MaxClients;i++)if(LiveHuman(i))return i;
    return 0;
}

bool FenceRetaliationClient(int client)
{
    return client>0 && client<=MaxClients && IsClientInGame(client) && !IsFakeClient(client)
        && !IsClientSourceTV(client) && !IsClientReplay(client);
}

int SelectFenceRetaliationClient(int b)
{
    int owner=BuildingOwnerOnline(b);if(owner>0)return owner;
    int clients[MAXPLAYERS+1],count;
    for(int client=1;client<=MaxClients;client++)if(FenceRetaliationClient(client))clients[count++]=client;
    return count>0 ? clients[GetRandomInt(0,count-1)] : 0;
}

int ExactRewardClient(RewardContext context)
{
    int client=GetClientFromSerial(context.ownerSerial);
    return context.valid && context.epoch==gEpoch && FenceRetaliationClient(client)
        && RewardOwnerKeyMatches(context.ownerKey,gInventoryOwner[client],context.ownerSerial) ? client : 0;
}

bool ApplyNPCDamageContext(int n,int damage,int serial,bool defense,RewardContext context,bool exactIdentity=false)
{
    if(!Running() || n<0 || n>=MAX_NPCS || !gNPCs[n].used || !gNPCs[n].active || gNPCs[n].dead || damage<1)return false;
    int entity=Resolve(gNPCs[n].ref),client=exactIdentity ? ExactRewardClient(context) : DamageCredit(serial);
    if(entity<0 || client==0)return false;
    int previousRef=gDamageNPCRef,previousValue=gDamageValue;bool previousDefense=gDamageIsDefense;
    RewardContext previousContext;previousContext=gDamageRewardContext;
    gDamageNPCRef=gNPCs[n].ref;gDamageValue=damage>100000 ? 100000 : damage;gDamageIsDefense=defense;
    gDamageRewardContext=context;
    bool applied=Han_SafeDamageZombie(client,entity,gDamageValue);
    gDamageNPCRef=previousRef;gDamageValue=previousValue;gDamageIsDefense=previousDefense;
    gDamageRewardContext=previousContext;
    return applied;
}

int FenceRetaliationDamage(int b)
{
    if(b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive || gBuildings[b].kind!=Kind_Fence
        || !(gBuildings[b].damage>0.0 && gBuildings[b].damage<=1000000.0))return 0;
    int amount=RoundToNearest(gBuildings[b].damage);if(amount<1)amount=1;
    return amount>100000 ? 100000 : amount; // Same public HZS damage cell limit.
}

bool CaptureFenceRetaliation(int b,int n,FenceRetaliationEvent hit)
{
    FenceRetaliationEvent empty;hit=empty;
    int damage=FenceRetaliationDamage(b);
    if(damage<1 || !Running() || n<0 || n>=MAX_NPCS || !gNPCs[n].used || !gNPCs[n].ready
        || !gNPCs[n].active || gNPCs[n].dead || gNPCs[n].registrationEpoch!=gEpoch
        || gNPCs[n].attackingUID!=gBuildings[b].uid || Resolve(gNPCs[n].ref)<0 || Resolve(gBuildings[b].ref)<0)return false;
    int client=SelectFenceRetaliationClient(b);if(client==0)return false;
    if(!CaptureBuildingReward(b,hit.reward))return false;
    // Unlike delivery proxies, this client really owns this strike's reward.
    hit.reward.ownerSerial=GetClientSerial(client);
    strcopy(hit.reward.ownerKey,sizeof(hit.reward.ownerKey),gInventoryOwner[client]);
    hit.pending=true;hit.epoch=gEpoch;hit.npc=n;hit.npcRef=gNPCs[n].ref;hit.damage=damage;return true;
}

bool ApplyFenceRetaliation(FenceRetaliationEvent hit)
{
    if(!hit.pending)return false;
    hit.pending=false; // Commit before re-entrant HZS damage/death callbacks.
    if(!Running() || hit.epoch!=gEpoch || hit.npc<0 || hit.npc>=MAX_NPCS
        || gNPCs[hit.npc].ref!=hit.npcRef || gNPCs[hit.npc].registrationEpoch!=hit.epoch)return false;
    // HZS 2.3.2 accepts an online player entity even when its owner is dead.
    // Always use that exact real recipient; never fall back to DamageCredit.
    return ApplyNPCDamageContext(hit.npc,hit.damage,hit.reward.ownerSerial,true,hit.reward,true);
}

bool DamageNPCWithContext(int n,int damage,RewardContext context)
{
    if(!context.valid || context.epoch!=gEpoch)return false;
    return ApplyNPCDamageContext(n,damage,context.ownerSerial,context.facility,context);
}

stock bool DamageNPC(int n,int damage,int serial=0,bool defense=false)
{
    RewardContext context;int client=GetClientFromSerial(serial);
    if(!defense && client>0 && IsClientInGame(client) && !IsFakeClient(client))
    {
        context.valid=true;context.epoch=gEpoch;context.buildCore=-1;context.ownerSerial=serial;
        strcopy(context.ownerKey,sizeof(context.ownerKey),gInventoryOwner[client]);
    }
    return ApplyNPCDamageContext(n,damage,serial,defense,context);
}

bool DamageBuildingNPC(int b,int n,int damage)
{
    RewardContext context;if(!CaptureBuildingReward(b,context))return false;
    return DamageNPCWithContext(n,damage,context);
}

void DamageBurningNPC(int n)
{
    if(gBurnRewardNPCRef[n]!=gNPCs[n].ref)return;
    DamageNPCWithContext(n,gNPCs[n].burnDamage,gBurnRewardContext[n]);
}

void InvalidateDefenseCache(int b)
{
    gBuildings[b].targetRef=INVALID_ENT_REFERENCE;gBuildings[b].targetEpoch=0;
    gBuildings[b].targetNPC=-1;
    gBuildings[b].targetBuildingUID=0;gBuildings[b].targetBuildingRef=INVALID_ENT_REFERENCE;
    gBuildings[b].nextSearch=0.0;gBuildings[b].nextFire=GetGameTime()+gBuildings[b].interval;
}

void ClearDefenseState()
{
    for(int b=0;b<MAX_BUILDINGS;b++)InvalidateDefenseCache(b);
    for(int n=0;n<MAX_NPCS;n++)ClearNPCRewardContext(n);
    RewardContext empty;gDamageRewardContext=empty;
}

// Preserve a normal deadline cadence, but discard overdue shots after a stall.
// Each caller emits at most one shot per shared callback.
float NextFireDeadline(float previous,float now,float interval)
{
    float deadline=previous+interval;
    return previous<=0.0 || deadline<=now+0.00001 ? now+interval : deadline;
}

bool DefenseTarget(int n,bool allowDormant=false)
{
    if(!gNPCs[n].used || !gNPCs[n].active || gNPCs[n].dead || Resolve(gNPCs[n].ref)<0)return false;
    int boss=gNPCs[n].boss;return allowDormant || boss<0 || gBosses[boss].awakened;
}

int TurretTarget(int b,const float muzzle[3])
{
    float best=gBuildings[b].range*gBuildings[b].range;int selected=-1,body=Resolve(gBuildings[b].ref);
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!DefenseTarget(n))continue;
        int entity=Resolve(gNPCs[n].ref);float point[3];EntityCenter(entity,point);
        float distance=GetVectorDistance(muzzle,point,true);
        if(distance<best && LineClear(muzzle,point,body,entity)){selected=n;best=distance;}
    }
    return selected;
}

int CachedTurretTarget(int b,const float muzzle[3],float now)
{
    int body=Resolve(gBuildings[b].ref),selected=-1;
    if(gBuildings[b].targetEpoch==gEpoch && gBuildings[b].targetBuildingUID==gBuildings[b].uid
        && gBuildings[b].targetBuildingRef==gBuildings[b].ref)
    {
        int entity=Resolve(gBuildings[b].targetRef);
        if(entity>0)
        {
            int n=gBuildings[b].targetNPC;
            if(n>=0 && n<MAX_NPCS && gNPCs[n].ref==gBuildings[b].targetRef && DefenseTarget(n))
            {
                float point[3];EntityCenter(entity,point);
                if(GetVectorDistance(muzzle,point,true)<=gBuildings[b].range*gBuildings[b].range
                    && LineClear(muzzle,point,body,entity))return n;
            }
        }
    }
    gBuildings[b].targetRef=INVALID_ENT_REFERENCE;gBuildings[b].targetNPC=-1;
    if(now+0.00001<gBuildings[b].nextSearch)return -1;
    gBuildings[b].nextSearch=now+0.3;selected=TurretTarget(b,muzzle);
    gBuildings[b].targetEpoch=gEpoch;gBuildings[b].targetBuildingUID=gBuildings[b].uid;
    gBuildings[b].targetBuildingRef=gBuildings[b].ref;
    if(selected>=0){gBuildings[b].targetRef=gNPCs[selected].ref;gBuildings[b].targetNPC=selected;}
    return selected;
}

void Beam(const float from[3],const float to[3],bool fire)
{
    int color[4]={255,220,100,220};if(fire){color[0]=255;color[1]=100;color[2]=25;}
    TE_SetupBeamPoints(from,to,fire ? gFireSprite : gBeamSprite,0,0,10,0.2,fire ? 12.0 : 1.5,fire ? 30.0 : 1.0,0,0.0,color,0);
    TE_SendToAll();
}

void DefenseAimAngles(BuildingMetadata meta,const float placedAngles[3],const float direction[3],float angles[3])
{
    // Recover the mounting frame's up axis from (frame * model correction).
    // Aiming rotates around that axis and reapplies the model correction once.
    float offsetForward[3],offsetRight[3],offsetUp[3],localUp[3],up[3],zero[3];
    GetAngleVectors(meta.angleOffset,offsetForward,offsetRight,offsetUp);
    localUp[0]=offsetForward[2];localUp[1]=-offsetRight[2];localUp[2]=offsetUp[2];
    WorldPoint(localUp,zero,placedAngles,up);NormalizeVector(up,up);
    float aimed[3],frame[3];float projection=GetVectorDotProduct(direction,up);
    for(int a=0;a<3;a++)aimed[a]=direction[a]-projection*up[a];
    if(GetVectorLength(aimed)<0.0001){angles=placedAngles;return;}
    NormalizeVector(aimed,aimed);AnglesFromForwardUp(aimed,up,frame);
    ComposeModelAngles(frame,meta.angleOffset,angles);
}

bool AimDefenseBody(int b,int target,float muzzle[3],float point[3])
{
    int body=Resolve(gBuildings[b].ref);if(body<0 || !DefenseTarget(target))return false;
    int entity=Resolve(gNPCs[target].ref);EntityCenter(entity,point);
    float direction[3],angles[3];SubtractVectors(point,gBuildings[b].origin,direction);
    DefenseAimAngles(gBuildings[b].meta,gBuildings[b].angles,direction,angles);
    TeleportEntity(body,NULL_VECTOR,angles,NULL_VECTOR);
    BuildingAttachmentPosition(b,gBuildings[b].meta.muzzleOffset,muzzle);
    // Rotating a nonzero local X/Y muzzle offset can change visibility/range.
    return GetVectorDistance(muzzle,point,true)<=gBuildings[b].range*gBuildings[b].range
        && LineClear(muzzle,point,body,entity);
}

void TurretFire(int b,float now)
{
    if(now+0.00001<gBuildings[b].nextFire)return;
    int body=Resolve(gBuildings[b].ref);if(body<0 || !BuildingPowered(b))return;
    float muzzle[3];BuildingAttachmentPosition(b,gBuildings[b].meta.muzzleOffset,muzzle);
    int target=CachedTurretTarget(b,muzzle,now);if(target<0)return;
    float position[3],direction[3];if(!AimDefenseBody(b,target,muzzle,position))return;
    MakeVectorFromPoints(muzzle,position,direction);
    gBuildings[b].nextFire=NextFireDeadline(gBuildings[b].nextFire,now,gBuildings[b].interval);
    TraceDefenseShot(b,now,target);
    PlayBuildingSound(b,"attack");
    if(gBuildings[b].kind==Kind_Turret)
    {
        Beam(muzzle,position,false);DamageBuildingNPC(b,target,RoundToNearest(gBuildings[b].damage));return;
    }
    NormalizeVector(direction,direction);float cutoff=Cosine(DegToRad(gBuildings[b].halfAngle));
    int epoch=gEpoch,uid=gBuildings[b].uid,sourceRef=gBuildings[b].ref;
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!Running() || gRebuilding || epoch!=gEpoch || !gBuildings[b].used || !gBuildings[b].alive
            || gBuildings[b].uid!=uid || gBuildings[b].ref!=sourceRef)return;
        if(!DefenseTarget(n))continue;
        int victim=Resolve(gNPCs[n].ref);float point[3],toward[3];EntityCenter(victim,point);MakeVectorFromPoints(muzzle,point,toward);
        if(GetVectorLength(toward)>gBuildings[b].range)continue;
        NormalizeVector(toward,toward);if(GetVectorDotProduct(direction,toward)<cutoff || !LineClear(muzzle,point,body,victim))continue;
        int victimRef=gNPCs[n].ref;RewardContext burnContext;CaptureBuildingReward(b,burnContext);
        Beam(muzzle,point,true);DamageNPCWithContext(n,RoundToNearest(gBuildings[b].damage),burnContext);
        if(epoch!=gEpoch || !Running() || !gBuildings[b].used || !gBuildings[b].alive
            || gBuildings[b].uid!=uid || gBuildings[b].ref!=sourceRef)return;
        if(gNPCs[n].ref==victimRef && !gNPCs[n].dead && gBuildings[b].burnDuration>0.0)
        {
            bool burning=gNPCs[n].burnUntil>now;
            float until=now+gBuildings[b].burnDuration;if(until>gNPCs[n].burnUntil)gNPCs[n].burnUntil=until;
            if(!burning || gBuildings[b].burnDamage>=gNPCs[n].burnDamage)
            {
                gNPCs[n].burnDamage=gBuildings[b].burnDamage;gNPCs[n].burnInterval=gBuildings[b].burnInterval;
                gNPCs[n].burnClientSerial=burnContext.ownerSerial;
                gBurnRewardContext[n]=burnContext;gBurnRewardNPCRef[n]=victimRef;
            }
            if(!burning)gNPCs[n].nextBurn=now+gBuildings[b].burnInterval;
        }
    }
}

void MineTick(int b)
{
    int body=Resolve(gBuildings[b].ref);if(body<0)return;
    float source[3];source=gBuildings[b].origin;source[2]+=20.0;
    bool triggered=false;
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!DefenseTarget(n,true))continue;
        int entity=Resolve(gNPCs[n].ref);float p[3];EntityCenter(entity,p);
        if(GetVectorDistance(source,p)<80.0 && LineClear(source,p,body,entity)){triggered=true;break;}
    }
    if(!triggered)return;
    // Consumption is committed before re-entrant native damage/death callbacks.
    RewardContext context;CaptureBuildingReward(b,context);
    gBuildings[b].alive=false;gBuildings[b].hp=0;InvalidateDefenseCache(b);
    MarkBuildAreasDirty();RecalculatePower(); // A consumed mine releases its logical quota exactly once.
    float radius=gBuildings[b].range;int damage=RoundToNearest(gBuildings[b].damage);
    int color[4]={255,170,40,255};TE_SetupBeamRingPoint(source,10.0,radius*2.0,gBeamSprite,0,0,0,0.35,12.0,0.0,color,0,0);TE_SendToAll();
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!Running() || context.epoch!=gEpoch)return;
        if(!DefenseTarget(n,true))continue;
        int entity=Resolve(gNPCs[n].ref);float p[3];EntityCenter(entity,p);
        if(GetVectorDistance(source,p)<=radius && LineClear(source,p,body,entity))DamageNPCWithContext(n,damage,context);
    }
    if(gBuildings[b].uid==context.sourceUID && gBuildings[b].ref==context.sourceRef)
        gBuildings[b].ref=INVALID_ENT_REFERENCE;
    RemoveOwnedRef(context.sourceRef);
}

void MineWorldTick()
{
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!gBuildings[b].used || !gBuildings[b].alive)continue;
        if(!Running() || gRebuilding)return;
        if(gBuildings[b].kind==Kind_Mine)MineTick(b);
    }
}

public Action DefenseTimer(Handle timer)
{
    if(!Running() || gRebuilding || !APIAvailable())return Plugin_Continue;
    float now=GetGameTime();int epoch=gEpoch;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!Running() || gRebuilding || epoch!=gEpoch)break;
        if(!gBuildings[b].used || !gBuildings[b].alive || now+0.00001<gBuildings[b].nextFire)continue;
        if(gBuildings[b].kind==Kind_Turret || gBuildings[b].kind==Kind_Flame)TurretFire(b,now);
        else if(gBuildings[b].kind==Kind_Grenade)GrenadeFire(b,now);
    }
    return Plugin_Continue;
}

public Action HumanDamageHook(int victim,int &attacker,int &inflictor,float &damage,int &damageType)
{
    if(!gEnabled)return Plugin_Continue;
    int n=FindNPC(attacker);
    if(n>=0 && (!Running() || !gNPCs[n].active || gNPCs[n].attackingUID
        || (gNPCs[n].boss>=0 && !gBosses[gNPCs[n].boss].awakened)))
    {damage=0.0;return Plugin_Handled;}
    if(n>=0 && LiveHuman(victim))
    {
        // Cover only the owned wall-contact transition before the next mode tick.
        // Other melee decisions remain the core's existing behavior.
        if(BlockingBuilding(n,victim)>=0){damage=0.0;return Plugin_Handled;}
    }
    if(LiveHuman(victim) && attacker>0 && attacker<=MaxClients && attacker!=victim && IsClientInGame(attacker))
    {damage=0.0;return Plugin_Handled;}
    return Plugin_Continue;
}
