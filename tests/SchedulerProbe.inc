// Pure deadline and reward-identity checks. No client or NPC is created.
void TestSchedulerLogic()
{
    Check(FloatAbs(NextFireDeadline(1.0,1.000001,0.1)-1.1)<0.0001,"deadline comparison tolerates tiny floating point delay");
    Check(FloatAbs(NextFireDeadline(1.0,1.035,0.2)-1.2)<0.0001,"small callback delay preserves the configured cadence");
    Check(FloatAbs(NextFireDeadline(1.0,8.0,0.1)-8.1)<0.0001,"stalled callback discards expired shots instead of burst catch-up");
    Check(FloatAbs(NextFireDeadline(0.0,8.0,1.0)-9.0)<0.0001,"a new instance starts with its own configured interval");
    bool independent=true;
    for(int group=1;group<=10;group++)
    {
        float interval=float(group)*0.1;
        if(FloatAbs(NextFireDeadline(5.0,5.0,interval)-(5.0+interval))>0.0001)independent=false;
    }
    Check(independent,"ten independent deadline groups retain 0.1 through 1.0 intervals");
    BuildingMetadata aimMeta;aimMeta.angleOffset[1]=90.0;
    float placed[3]={0.0,90.0,0.0},direction[3]={0.0,1.0,0.0},aimed[3];
    DefenseAimAngles(aimMeta,placed,direction,aimed);
    Check(FloatAbs(aimed[0])<0.001 && FloatAbs(FloatAbs(aimed[1])-180.0)<0.001 && FloatAbs(aimed[2])<0.001,
        "target aiming preserves the group's model angle correction");
    int slot=MAX_BUILDINGS-1;Building saved;saved=gBuildings[slot];
    Building fixture;fixture.used=true;fixture.alive=true;fixture.uid=731;
    fixture.ref=INVALID_ENT_REFERENCE;fixture.buildCore=2;fixture.ownerSerial=981;
    strcopy(fixture.ownerKey,sizeof(fixture.ownerKey),"offline-owner");gBuildings[slot]=fixture;
    float muzzle[3];
    Check(CachedTurretTarget(slot,muzzle,1.0)==-1 && FloatAbs(gBuildings[slot].nextSearch-1.3)<0.0001,
        "an empty turret advances its independent search deadline by 0.3 seconds");
    Check(CachedTurretTarget(slot,muzzle,1.1)==-1 && FloatAbs(gBuildings[slot].nextSearch-1.3)<0.0001,
        "a 0.1 callback before the search deadline does not rescan idle targets");
    Check(CachedTurretTarget(slot,muzzle,1.3)==-1 && FloatAbs(gBuildings[slot].nextSearch-1.6)<0.0001,
        "idle searches resume at the next 0.3 deadline");
    InvalidateDefenseCache(slot);
    Check(gBuildings[slot].targetRef==INVALID_ENT_REFERENCE && gBuildings[slot].targetNPC==-1
        && gBuildings[slot].targetEpoch==0 && gBuildings[slot].nextSearch==0.0,
        "facility lifecycle invalidation clears cached entity and epoch identity");
    RewardContext context;Check(CaptureBuildingReward(slot,context),"facility captures an independent reward identity");
    gBuildings[slot].alive=false;gBuildings[slot].ownerSerial=0;gBuildings[slot].buildCore=0;
    Check(context.valid && context.facility && context.epoch==gEpoch && context.sourceUID==731
        && context.buildCore==2 && context.ownerSerial==981 && StrEqual(context.ownerKey,"offline-owner"),
        "delayed reward snapshot survives source dismantle and owner disconnection");
    gBuildings[slot]=saved;
    int npc=MAX_NPCS-1;NPC savedNPC;savedNPC=gNPCs[npc];
    RewardContext savedContext;savedContext=gDamageRewardContext;int savedRef=gDamageNPCRef;
    gNPCs[npc].ref=19426;gDamageNPCRef=19426;gDamageRewardContext=context;
    RewardContext received;
    Check(GetDeathRewardContext(npc,0,received) && received.ownerSerial==981 && received.buildCore==2,
        "synchronous death reads the original facility identity without a proxy recipient");
    gDamageRewardContext.epoch=gEpoch-1;
    Check(!GetDeathRewardContext(npc,0,received),"old epoch cannot claim a current NPC reward");
    RewardContext empty;gDamageRewardContext=empty;
    Check(!GetDeathRewardContext(npc,0,received),"unattributed external damage cannot fall back to a HZS proxy");
    gNPCs[npc]=savedNPC;gDamageNPCRef=savedRef;gDamageRewardContext=savedContext;
}
