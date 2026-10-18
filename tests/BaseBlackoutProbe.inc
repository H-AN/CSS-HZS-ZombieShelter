// Test binary only: two existing real bases and existing entity references.
// Logical fixtures never spawn/remove entities or call the base-destruction path.
Building tBlackoutBuildings[MAX_BUILDINGS];
bool tBlackoutActive;

Action BlackoutMarkerPolicy(int beacon,int client)
{
    int entity=Resolve(gBuildings[beacon].ref);
    if(entity<0 || entity>=BEACON_MARKER_ENTITY_SLOTS)return Plugin_Handled;
    int saved[7];saved[0]=gBeaconMarkerRef[beacon];saved[1]=gBeaconMarkerUID[beacon];
    saved[2]=gBeaconMarkerGeneration[beacon];saved[3]=gBeaconMarkerEpoch[beacon];
    saved[4]=gBeaconMarkerRevision[beacon];saved[5]=gBeaconMarkerBodyRef[beacon];saved[6]=gBeaconMarkerBuilding[entity];
    // Bind a current, existing reference only for this synchronous policy call.
    // Never run SyncBeaconMarker/cleanup while this temporary alias exists.
    gBeaconMarkerRef[beacon]=gBuildings[beacon].ref;gBeaconMarkerUID[beacon]=gBuildings[beacon].uid;
    gBeaconMarkerGeneration[beacon]=gBuildings[beacon].generation;gBeaconMarkerEpoch[beacon]=gEpoch;
    gBeaconMarkerRevision[beacon]=gConfigRevision;gBeaconMarkerBodyRef[beacon]=gBuildings[beacon].ref;
    gBeaconMarkerBuilding[entity]=beacon+1;
    Action result=BeaconMarkerTransmit(entity,client);
    gBeaconMarkerRef[beacon]=saved[0];gBeaconMarkerUID[beacon]=saved[1];gBeaconMarkerGeneration[beacon]=saved[2];
    gBeaconMarkerEpoch[beacon]=saved[3];gBeaconMarkerRevision[beacon]=saved[4];gBeaconMarkerBodyRef[beacon]=saved[5];
    gBeaconMarkerBuilding[entity]=saved[6];return result;
}

void TestBaseBlackoutReal(int client)
{
    if(tBlackoutActive || tV05RealActive || client<1 || !LiveHuman(client) || IsFakeClient(client)
        || !Running() || gCoreCount<2 || !BuildCoreAlive(0) || !BuildCoreAlive(1))
    {tSkipped++;PrintToServer("[ShelterTest] SKIP blackout requires an existing living real client, two living bases and no active fixture");return;}
    int baseA=gCores[0].building,baseB=gCores[1].building;
    if(Resolve(gBuildings[baseA].ref)<0 || Resolve(gBuildings[baseB].ref)<0)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP blackout requires two existing real base bodies; none is created");return;}
    RecalculatePower();bool originalPower=gPowerEnabled;
    float originalProduction[MAX_CORES],originalDemand[MAX_CORES],originalAllocation[MAX_CORES];int originalOffline[MAX_CORES];
    for(int core=0;core<gCoreCount;core++)
    {
        originalProduction[core]=gGridProduction[core];originalDemand[core]=gGridDemand[core];
        originalAllocation[core]=gGridAllocated[core];originalOffline[core]=gGridOffline[core];
    }
    if(!V05BeginRealFixture(client)){tSkipped++;return;}
    tBlackoutActive=true;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        tBlackoutBuildings[b]=gBuildings[b];
        bool base;for(int core=0;core<gCoreCount;core++)if(gCores[core].building==b){base=true;break;}
        if(!base)gBuildings[b].used=false;
    }
    bool queued=gOutcomeQueued,terminal=gTerminal,failure=gFailureCandidate,victory=gVictoryCandidate;
    int baseRefs[2];baseRefs[0]=gBuildings[baseA].ref;baseRefs[1]=gBuildings[baseB].ref;
    BuildingKind kinds[]={Kind_Generator,Kind_Generator,Kind_Turret,Kind_Light,Kind_AutoRepair,Kind_Heal,Kind_Armor,
        Kind_Beacon,Kind_Flame,Kind_Grenade,Kind_Amplifier,Kind_Expander,Kind_Mine,Kind_Fence};
    int slots[2][14];
    for(int core=0;core<2;core++)
    {
        int base=gCores[core].building;gBuildings[base].powerDemand=0.0;
        gBuildings[base].powerCore=core;gBuildings[base].gridAssigned=true;
        for(int i=0;i<sizeof(kinds);i++)
        {
            int b=MAX_BUILDINGS-1-core*sizeof(kinds)-i;slots[core][i]=b;
            Building fixture;fixture.used=true;fixture.alive=true;fixture.kind=kinds[i];
            fixture.uid=19500000+core*sizeof(kinds)+i;fixture.generation=1;fixture.configRevision=gConfigRevision;
            fixture.core=-1;fixture.buildCore=core;fixture.powerCore=core;fixture.gridAssigned=true;
            fixture.hp=i==1 ? 500 : 1000;fixture.maxHP=1000;fixture.ref=baseRefs[core];fixture.effectRef=INVALID_ENT_REFERENCE;
            fixture.origin=gCores[core].origin;fixture.placementAnchor=gCores[core].buildCenter;
            fixture.powerPriority=10;fixture.powerDemand=i<2 || fixture.kind==Kind_Fence ? 0.0 : 5.0;
            fixture.powerOutput=i==0 ? (core==0 ? 200.0 : 300.0) : (i==1 ? 100.0 : 0.0);
            fixture.restoreAmount=5;fixture.restoreInterval=20.0;fixture.level=1;
            fixture.buildRangeBonus=32.0;fixture.buildLimitBonus=2;
            gBuildings[b]=fixture;
        }
    }
    gPowerEnabled=true;MarkBuildAreasDirty();RecalculatePower();
    bool powered=true;
    for(int core=0;core<2;core++)for(int i=0;i<sizeof(kinds);i++)if(!BuildingPowered(slots[core][i]))powered=false;
    Check(powered && FloatAbs(gGridProduction[0]-250.0)<0.01 && FloatAbs(gGridProduction[1]-350.0)<0.01,
        "two live bases independently power existing-reference fixtures, including two HP-scaled generators per grid");
    Check(RestorationReady(slots[0][4]) && RestorationReady(slots[0][5]) && RestorationReady(slots[0][6])
        && EffectiveExpander(slots[0][11]),"the three restoration classes and expander are effective before their base loses power");
    if(gBeaconConfigAvailable)
        Check(BeaconEffective(slots[0][7]) && BlackoutMarkerPolicy(slots[0][7],client)==Plugin_Continue,
            "a live-grid beacon accepts the real marker-transmission policy with a fully matching existing identity");
    else{tSkipped++;PrintToServer("[ShelterTest] SKIP blackout marker policy: optional beacon configuration is absent");}
    float bProduction=gGridProduction[1],bDemand=gGridDemand[1],bAllocated=gGridAllocated[1];int bOffline=gGridOffline[1];
    float aDemand=gGridDemand[0];
    gBuildings[baseA].alive=false;gBuildings[baseA].hp=0;MarkBuildAreasDirty();RecalculatePower();
    Check(GeneratorOutput(slots[0][0])==0.0 && GeneratorOutput(slots[0][1])==0.0
        && gGridProduction[0]==0.0 && gGridAllocated[0]==0.0 && FloatAbs(gGridDemand[0]-aDemand)<0.01,
        "base A logical destruction zeroes every generator and allocated output while retaining its living consumers' demand");
    bool offline=true,otherPowered=true;
    for(int i=0;i<sizeof(kinds);i++)
    {
        if(BuildingPowered(slots[0][i]) || gBuildings[slots[0][i]].powered)offline=false;
        if(!BuildingPowered(slots[1][i]))otherPowered=false;
    }
    Check(offline && !BuildingPowered(slots[0][13]),"all A facility classes, including zero-demand consumers, reject the shared power gate after base loss");
    Check(otherPowered && FloatAbs(gGridProduction[1]-bProduction)<0.01 && FloatAbs(gGridDemand[1]-bDemand)<0.01
        && FloatAbs(gGridAllocated[1]-bAllocated)<0.01 && gGridOffline[1]==bOffline,
        "destroying A leaves B generation, demand, allocation and every consumer unchanged");
    Check(!RestorationReady(slots[0][4]) && !RestorationReady(slots[0][5]) && !RestorationReady(slots[0][6])
        && !EffectiveExpander(slots[0][11]),"A restoration and expansion stop through their existing unified power predicates");
    int pulses=gRestorationPulseCount;
    Check(RestorationPulse(slots[0][4])==0 && RestorationPulse(slots[0][5])==0 && RestorationPulse(slots[0][6])==0
        && gRestorationPulseCount==pulses,"a dead-grid recovery call produces no actual restoration ring or ambient sound");
    if(gBeaconConfigAvailable)
        Check(!BeaconEffective(slots[0][7]) && BeaconForCore(0)<0
            && BlackoutMarkerPolicy(slots[0][7],client)==Plugin_Handled
            && BeaconEffective(slots[1][7]) && BlackoutMarkerPolicy(slots[1][7],client)==Plugin_Continue,
            "A beacon travel and matching marker policy stop immediately while B remains usable");
    gPowerEnabled=false;RecalculatePower();offline=true;otherPowered=true;
    for(int i=0;i<sizeof(kinds);i++)
    {
        if(BuildingPowered(slots[0][i]) || gBuildings[slots[0][i]].powered)offline=false;
        if(!BuildingPowered(slots[1][i]))otherPowered=false;
    }
    Check(offline && otherPowered && gGridProduction[0]==0.0 && gGridAllocated[0]==0.0
        && FloatAbs(gGridProduction[1]-bProduction)<0.01 && FloatAbs(gGridAllocated[1]-bAllocated)<0.01,
        "power.enabled=0 preserves every live B consumer but cannot bypass A's destroyed-base blackout");
    if(gBeaconConfigAvailable)
        Check(!BeaconEffective(slots[0][7]) && BlackoutMarkerPolicy(slots[0][7],client)==Plugin_Handled
            && BeaconEffective(slots[1][7]),"power-disabled compatibility never restores a dead-base beacon marker or travel");
    Check(gOutcomeQueued==queued && gTerminal==terminal && gFailureCandidate==failure && gVictoryCandidate==victory,
        "logical blackout regression never queues an actual campaign outcome or destructive base event");
    PrintToServer("[BaseBlackout] epoch=%d A_production=%.1f A_demand=%.1f A_allocated=%.1f B_production=%.1f B_allocated=%.1f fixtures_per_base=%d existing_refs=%d,%d entity_creation=0",
        gEpoch,gGridProduction[0],gGridDemand[0],gGridAllocated[0],gGridProduction[1],gGridAllocated[1],sizeof(kinds),Resolve(baseRefs[0]),Resolve(baseRefs[1]));

    // Restore every logical field before the shared cleanup can inspect ownership.
    for(int b=0;b<MAX_BUILDINGS;b++)gBuildings[b]=tBlackoutBuildings[b];
    tBlackoutActive=false;MarkBuildAreasDirty();V05RealCleanup(client);RecalculatePower();
    bool originalGrid=gPowerEnabled==originalPower;
    for(int core=0;core<gCoreCount;core++)if(FloatAbs(gGridProduction[core]-originalProduction[core])>0.01
        || FloatAbs(gGridDemand[core]-originalDemand[core])>0.01 || FloatAbs(gGridAllocated[core]-originalAllocation[core])>0.01
        || gGridOffline[core]!=originalOffline[core])originalGrid=false;
    Check(BuildCoreAlive(0) && BuildCoreAlive(1) && Resolve(baseRefs[0])>0 && Resolve(baseRefs[1])>0
        && originalGrid && gBuildings[baseA].hp==tBlackoutBuildings[baseA].hp && gBuildings[baseB].hp==tBlackoutBuildings[baseB].hp,
        "blackout cleanup restores both original living base identities and HP and recalculates the original power configuration");
}
