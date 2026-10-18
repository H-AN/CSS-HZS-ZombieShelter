// LAST case of a disposable isolated campaign. A is genuinely destroyed and is
// never restored here. One real Steam client; no BOT/NPC/client creation/respawn.
Building tBaseExplosionSaved[6];
int tBaseExplosionSlots[6],tBaseExplosionUIDs[6],tBaseExplosionCount;
bool tBaseExplosionRan;

int BaseExplosionFacility(int client,BuildingKind kind,int core)
{
    int recipe=-1,slot=-1;
    for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==kind){recipe=r;break;}
    for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(!gBuildings[b].used){slot=b;break;}
    if(recipe<0 || slot<0 || tBaseExplosionCount>=6)return -1;
    int n=tBaseExplosionCount++;tBaseExplosionSlots[n]=slot;tBaseExplosionSaved[n]=gBuildings[slot];
    Building fixture;fixture.used=true;fixture.alive=true;fixture.kind=kind;fixture.uid=++gUID;fixture.core=-1;
    fixture.buildCore=core;fixture.powerCore=core;fixture.gridAssigned=true;fixture.configRevision=gConfigRevision;
    fixture.file=gRecipes[recipe].file;fixture.level=1;fixture.ref=INVALID_ENT_REFERENCE;fixture.effectRef=INVALID_ENT_REFERENCE;
    fixture.meta=gRecipes[recipe].meta;strcopy(fixture.type,sizeof(fixture.type),gRecipes[recipe].type);
    // Typed fixtures use separate real stock bodies, never the base entity.
    // Stats are production Levels/Growth values; ray placement/payment is not
    // claimed by this final destruction-only case (covered by prior suites).
    strcopy(fixture.model,sizeof(fixture.model),"models/props_lab/reciever01b.mdl");
    fixture.origin=gCores[core].origin;fixture.origin[0]+=64.0+float(n)*32.0;fixture.origin[2]+=48.0;
    fixture.placementAnchor=fixture.origin;gBuildings[slot]=fixture;tBaseExplosionUIDs[n]=fixture.uid;AssignBuildingOwner(slot,client);
    if(!ApplyBuildingLevel(slot,true) || !SpawnBuildingBody(slot))return -1;
    if(kind==Kind_Generator)
    {
        // Use an available configured level, rather than inventing extra watts.
        for(int level=0;level<8 && BuildingLevelAvailable(slot,gBuildings[slot].level+1);level++)
        {gBuildings[slot].level++;if(!ApplyBuildingLevel(slot,false))return -1;}
    }
    return slot;
}

void BaseExplosionCleanup(int client)
{
    V05RealCleanup(client);
    for(int i=0;i<tBaseExplosionCount;i++)
    {
        int b=tBaseExplosionSlots[i];
        if(b>=0 && b<MAX_BUILDINGS && !gBuildings[b].alive && gBuildings[b].uid==tBaseExplosionUIDs[i])gBuildings[b]=tBaseExplosionSaved[i];
    }
    tBaseExplosionCount=0;MarkBuildAreasDirty();RecalculatePower();
}

public Action BaseExplosionRemovedPoll(Handle timer,DataPack data)
{
    data.Reset();int baseRef=data.ReadCell(),markerRef=data.ReadCell(),epoch=data.ReadCell(),attempt=data.ReadCell()+1;
    if(epoch!=gEpoch){tSkipped++;PrintToServer("[ShelterTest] SKIP real base explosion physical removal crossed epoch");return Plugin_Stop;}
    bool gone=Resolve(baseRef)<0 && Resolve(markerRef)<0;
    if(gone || attempt>=5)
    {
        Check(gone,"genuinely destroyed base A body and its withdrawn beacon sprite are physically removed within five 0.1-second polls");
        PrintToServer("[BaseExplosionLive] physical_base_ref=%d base_resolve=%d marker_ref=%d marker_resolve=%d polls=%d",
            baseRef,Resolve(baseRef),markerRef,Resolve(markerRef),attempt);return Plugin_Stop;
    }
    data.Reset(true);data.WriteCell(baseRef);data.WriteCell(markerRef);data.WriteCell(epoch);data.WriteCell(attempt);return Plugin_Continue;
}

void TestBaseExplosionLive(int client)
{
    if(tBaseExplosionRan || !BeaconActor(client) || tV05RealActive || gCoreCount!=3 || AliveCores()!=3 || gOutcomeQueued || gTerminal)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP last-case real base explosion needs three living bases, an existing real client, no V05 fixture and no terminal outcome");return;}
    if(BeaconCount(0)>0)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP last-case explosion preserves a pre-existing base A beacon");return;}
    if(!V05BeginRealFixture(client))return;
    tBaseExplosionCount=0;gPowerEnabled=true;
    int aGen=BaseExplosionFacility(client,Kind_Generator,0),aRepair=BaseExplosionFacility(client,Kind_AutoRepair,0);
    int aLight=BaseExplosionFacility(client,Kind_Light,0),aBeacon=BaseExplosionFacility(client,Kind_Beacon,0);
    int bGen=BaseExplosionFacility(client,Kind_Generator,1),bRepair=BaseExplosionFacility(client,Kind_AutoRepair,1);
    if(aGen<0 || aRepair<0 || aLight<0 || aBeacon<0 || bGen<0 || bRepair<0)
    {Check(false,"real base explosion creates six separate typed stock facility bodies with actual configured profiles");BaseExplosionCleanup(client);return;}
    RecalculatePower();SyncBeaconMarker(aBeacon);
    int base=gCores[0].building,oldRef=gBuildings[base].ref,oldMarker=gBeaconMarkerRef[aBeacon],markers=gBeaconMarkerCount;
    float beforeA=gGridProduction[0],allocatedA=gGridAllocated[0],beforeB=gGridProduction[1],allocatedB=gGridAllocated[1];
    bool ready=beforeA>0.0 && allocatedA>0.0 && beforeB>0.0 && allocatedB>0.0
        && BuildingPowered(aRepair) && BuildingPowered(aLight) && BeaconEffective(aBeacon) && BuildingPowered(bRepair)
        && BeaconMarkerEntity(aBeacon)>0;
    Check(ready,"before real explosion A and B have real generators, allocated consumers and a visible effective A beacon");
    PrintToServer("[BaseExplosionLive] BEFORE a_output=%.3f a_allocated=%.3f b_output=%.3f b_allocated=%.3f agen_body=%d bgen_body=%d marker_ref=%d fixture=typed_stock_actual_profiles ray_payment_not_retested=1",
        beforeA,allocatedA,beforeB,allocatedB,Resolve(gBuildings[aGen].ref),Resolve(gBuildings[bGen].ref),oldMarker);
    if(!ready){BaseExplosionCleanup(client);return;}

    tBaseExplosionRan=true;int originalHP=gBuildings[base].hp,uid=gBuildings[base].uid;
    DamageBuilding(base,originalHP); // Real logical destruction, RemoveOwnedRef, announcement and power events.
    Check(!gBuildings[base].alive && gBuildings[base].hp==0 && gBuildings[base].ref==INVALID_ENT_REFERENCE,
        "production DamageBuilding genuinely consumes base A's HP, live state and entity reference");
    Check(Resolve(gBuildings[aGen].ref)>0 && gBuildings[aGen].alive && gBuildings[aGen].powerOutput>0.0
        && GeneratorOutput(aGen)==0.0 && gGridProduction[0]==0.0 && gGridAllocated[0]==0.0,
        "A generator body survives destruction while its actual output and allocated grid power become zero");
    Check(!BuildingPowered(aRepair) && !BuildingPowered(aLight) && !BuildingPowered(aBeacon)
        && !RestorationReady(aRepair) && !BeaconEffective(aBeacon),"every surviving A consumer and support function loses effective power immediately");
    int former=Resolve(oldMarker);
    Check(BeaconMarkerEntity(aBeacon)<0 && gBeaconMarkerCount==markers-1
        && (former<0 || gBeaconMarkerBuilding[former]==0),"real A destruction immediately withdraws its beacon marker ownership and transmit mapping");
    Check(FloatAbs(gGridProduction[1]-beforeB)<0.001 && FloatAbs(gGridAllocated[1]-allocatedB)<0.001
        && GeneratorOutput(bGen)>0.0 && BuildingPowered(bRepair),"base B's independent real output and allocations do not change when A explodes");
    Check(AliveCores()==2 && Running() && !gOutcomeQueued && !gTerminal,
        "one genuine base explosion leaves the other two bases alive and never queues the global failure");

    gPowerEnabled=false;RecalculatePower();
    Check(!BuildingPowered(aGen) && !BuildingPowered(aRepair) && !BuildingPowered(aLight) && !BuildingPowered(aBeacon)
        && GeneratorOutput(aGen)==0.0 && gGridProduction[0]==0.0 && gGridAllocated[0]==0.0,
        "power.enabled=0 compatibility still cannot reconnect any destroyed A grid facility");
    Check(BuildingPowered(bRepair) && GeneratorOutput(bGen)>0.0 && FloatAbs(gGridProduction[1]-beforeB)<0.001,
        "power-disabled compatibility keeps the surviving B grid independent and usable");
    Check(!RestorationRepairTarget(aRepair,base) && RestorationPulse(aRepair)==0 && gBuildings[base].hp==0 && !gBuildings[base].alive,
        "surviving A repair station cannot repair or resurrect its genuinely destroyed base");
    Check(!TryUpgrade(client,base) && !TravelBaseAlive(0),"destroyed base A cannot be upgraded or used as a travel destination");

    // Use an actual eye ray at A after destruction. RequestedCore prevents a
    // surprising ray outside A from ever falling through to another live base.
    int recipe=-1;for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_AutoRepair){recipe=r;break;}
    bool refused=false;int inventory[MATERIALS],count=SummonCount();
    if(recipe>=0)
    {
        V05EnsureRecipeMaterials(client,recipe);inventory=gPlayerInventory[client];
        float feet[3],point[3];feet=gCorePoints[0][0];feet[2]+=2.0;point=gCores[0].origin;point[0]+=96.0;
        V05AimAt(client,feet,point);refused=!BuildRecipe(client,recipe,0);
        for(int m=0;m<gMaterialCount;m++)if(inventory[m]!=gPlayerInventory[client][m])refused=false;
    }
    Check(refused && SummonCount()==count,"actual production build action cannot add a new structure to destroyed A or spend its materials");
    DataPack removal;CreateDataTimer(0.1,BaseExplosionRemovedPoll,removal,TIMER_REPEAT|TIMER_FLAG_NO_MAPCHANGE);
    removal.WriteCell(oldRef);removal.WriteCell(oldMarker);removal.WriteCell(gEpoch);removal.WriteCell(0);
    PrintToServer("[BaseExplosionLive] AFTER uid=%d original_hp=%d a_alive=%d a_output=%.3f a_allocated=%.3f b_output=%.3f remaining_bases=%d failure_queued=%d",
        uid,originalHP,gBuildings[base].alive,gGridProduction[0],gGridAllocated[0],gGridProduction[1],AliveCores(),gOutcomeQueued);
    BaseExplosionCleanup(client);
    Check(!gBuildings[base].alive && gBuildings[base].hp==0 && gBuildings[base].ref==INVALID_ENT_REFERENCE && AliveCores()==2,
        "cleanup restores the real actor and facility fixtures but deliberately never recreates destroyed base A");
    PrintToServer("[BaseExplosionLive] DISCARD_CAMPAIGN_REQUIRED=1 base_A_not_restored=1; restart the formal isolated server before user review; original Steam client remains running");
}
