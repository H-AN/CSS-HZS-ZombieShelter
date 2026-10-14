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

int DamageCredit(int serial)
{
    int client=GetClientFromSerial(serial);if(LiveHuman(client))return client;
    for(int i=1;i<=MaxClients;i++)if(LiveHuman(i))return i;
    return 0;
}

bool DamageNPC(int n,int damage,int serial=0)
{
    if(!Running() || n<0 || n>=MAX_NPCS || !gNPCs[n].used || !gNPCs[n].active || gNPCs[n].dead || damage<1)return false;
    int entity=Resolve(gNPCs[n].ref),client=DamageCredit(serial);if(entity<0 || client==0)return false;
    int previousRef=gDamageNPCRef,previousValue=gDamageValue;
    gDamageNPCRef=gNPCs[n].ref;gDamageValue=damage>100000 ? 100000 : damage;
    bool applied=Han_SafeDamageZombie(client,entity,gDamageValue);
    gDamageNPCRef=previousRef;gDamageValue=previousValue;
    return applied;
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

void Beam(const float from[3],const float to[3],bool fire)
{
    int color[4]={255,220,100,220};if(fire){color[0]=255;color[1]=100;color[2]=25;}
    TE_SetupBeamPoints(from,to,fire ? gFireSprite : gBeamSprite,0,0,10,0.2,fire ? 12.0 : 1.5,fire ? 30.0 : 1.0,0,0.0,color,0);
    TE_SendToAll();
}

void TurretFire(int b,float now)
{
    int body=Resolve(gBuildings[b].ref);if(body<0 || now<gBuildings[b].nextFire)return;
    float muzzle[3];muzzle=gBuildings[b].origin;muzzle[2]+=48.0;
    int target=TurretTarget(b,muzzle);if(target<0)return;
    int entity=Resolve(gNPCs[target].ref);float position[3],direction[3],angles[3];EntityCenter(entity,position);MakeVectorFromPoints(muzzle,position,direction);GetVectorAngles(direction,angles);
    angles[0]=0.0;angles[2]=0.0;TeleportEntity(body,NULL_VECTOR,angles,NULL_VECTOR);
    gBuildings[b].nextFire=now+gBuildings[b].interval;
    if(gBuildings[b].kind==Kind_Turret)
    {
        Beam(muzzle,position,false);DamageNPC(target,RoundToNearest(gBuildings[b].damage));return;
    }
    NormalizeVector(direction,direction);float cutoff=Cosine(DegToRad(gBuildings[b].halfAngle));
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!DefenseTarget(n))continue;
        int victim=Resolve(gNPCs[n].ref);float point[3],toward[3];EntityCenter(victim,point);MakeVectorFromPoints(muzzle,point,toward);
        if(GetVectorLength(toward)>gBuildings[b].range)continue;
        NormalizeVector(toward,toward);if(GetVectorDotProduct(direction,toward)<cutoff || !LineClear(muzzle,point,body,victim))continue;
        Beam(muzzle,point,true);DamageNPC(n,RoundToNearest(gBuildings[b].damage));
        if(!gNPCs[n].dead && gBuildings[b].burnDuration>0.0)
        {
            bool burning=gNPCs[n].burnUntil>now;
            float until=now+gBuildings[b].burnDuration;if(until>gNPCs[n].burnUntil)gNPCs[n].burnUntil=until;
            if(!burning || gBuildings[b].burnDamage>=gNPCs[n].burnDamage)
            {gNPCs[n].burnDamage=gBuildings[b].burnDamage;gNPCs[n].burnInterval=gBuildings[b].burnInterval;}
            if(!burning)gNPCs[n].nextBurn=now+gBuildings[b].burnInterval;
            int credit=DamageCredit(0);gNPCs[n].burnClientSerial=credit>0 ? GetClientSerial(credit) : 0;
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
    gBuildings[b].alive=false;gBuildings[b].hp=0;
    float radius=gBuildings[b].range;int damage=RoundToNearest(gBuildings[b].damage);
    int color[4]={255,170,40,255};TE_SetupBeamRingPoint(source,10.0,radius*2.0,gBeamSprite,0,0,0,0.35,12.0,0.0,color,0,0);TE_SendToAll();
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!DefenseTarget(n,true))continue;
        int entity=Resolve(gNPCs[n].ref);float p[3];EntityCenter(entity,p);
        if(GetVectorDistance(source,p)<=radius && LineClear(source,p,body,entity))DamageNPC(n,damage);
    }
    int ref=gBuildings[b].ref;gBuildings[b].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
}

void DefenseTick(float now)
{
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!gBuildings[b].used || !gBuildings[b].alive)continue;
        if(gBuildings[b].kind==Kind_Turret || gBuildings[b].kind==Kind_Flame)TurretFire(b,now);
        else if(gBuildings[b].kind==Kind_Mine)MineTick(b);
    }
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
