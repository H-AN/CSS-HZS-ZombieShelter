// Beacon regression fixtures create only ordinary stock props/sprites.
// No client, BOT, NPC, NPC resurrection or game-start fixture is created.
Building tBeaconSaved[3],tBeaconBaseSaved[2];
int tBeaconRefs[3];
Phase tBeaconPhase;
bool tBeaconReady,tBeaconPower,tBeaconFixture;
Building tBeaconPoseSaved;
int tBeaconPoseSlot=-1,tBeaconPoseBodyRef=INVALID_ENT_REFERENCE,tBeaconPoseMarkerRef=INVALID_ENT_REFERENCE;
int tBeaconPoseUID,tBeaconPoseEpoch,tBeaconPoseStage,tBeaconPoseAttempt;

void CheckBeaconRemovedEventually(int ref,int uid)
{
    DataPack removal;CreateDataTimer(0.1,BeaconRemovedPoll,removal,TIMER_REPEAT|TIMER_FLAG_NO_MAPCHANGE);
    removal.WriteCell(ref);removal.WriteCell(gEpoch);removal.WriteCell(uid);removal.WriteCell(0);
}

public Action BeaconRemovedPoll(Handle timer,DataPack removal)
{
    removal.Reset();int ref=removal.ReadCell(),epoch=removal.ReadCell(),uid=removal.ReadCell(),attempt=removal.ReadCell()+1;
    if(gEpoch!=epoch){tSkipped++;PrintToServer("[ShelterTest] SKIP beacon physical-removal check crossed a round epoch");return Plugin_Stop;}
    if(Resolve(ref)<0)
    {
        Check(true,"former beacon sprite EntRef is physically removed within the bounded engine deletion window");
        PrintToServer("[BeaconRemoval] uid=%d ref=%d polls=%d removed=1",uid,ref,attempt);return Plugin_Stop;
    }
    if(attempt>=5)
    {
        Check(false,"former beacon sprite EntRef is physically removed within the bounded engine deletion window");
        PrintToServer("[BeaconRemoval] uid=%d ref=%d polls=%d removed=0",uid,ref,attempt);return Plugin_Stop;
    }
    removal.Reset(true);removal.WriteCell(ref);removal.WriteCell(epoch);removal.WriteCell(uid);removal.WriteCell(attempt);
    return Plugin_Continue;
}

int BeaconMarkerActualParent(int marker)
{
    if(HasEntProp(marker,Prop_Data,"m_hMoveParent"))return GetEntPropEnt(marker,Prop_Data,"m_hMoveParent");
    if(HasEntProp(marker,Prop_Send,"moveparent"))return GetEntPropEnt(marker,Prop_Send,"moveparent");
    return -1;
}

void CheckBeaconMarkerParentLocal(int b)
{
    int marker=BeaconMarkerEntity(b),body=Resolve(gBuildings[b].ref);float local[3],angles[3],offset[3];
    char path[128];Format(path,sizeof(path),"Types/%s/Marker",gBuildings[b].type);
    bool configured=BeaconMarkerVector(path,"offset","0 0 72",offset,-512.0,512.0);
    if(marker>0){GetEntPropVector(marker,Prop_Data,"m_vecOrigin",local);GetEntPropVector(marker,Prop_Data,"m_angRotation",angles);}
    Check(configured && marker>0 && body>0 && BeaconMarkerActualParent(marker)==body
        && GetVectorDistance(local,offset)<0.01 && GetVectorLength(angles)<0.01,
        "actual beacon sprite has the real body parent, configured LOCAL offset and zero LOCAL angles");
    PrintToServer("[BeaconPose] immediate uid=%d body=%d sprite=%d parent=%d local=%.3f %.3f %.3f angles=%.3f %.3f %.3f",
        gBuildings[b].uid,body,marker,marker>0 ? BeaconMarkerActualParent(marker) : -1,local[0],local[1],local[2],angles[0],angles[1],angles[2]);
}

void FinishBeaconMarkerPoseProbe()
{
    int slot=tBeaconPoseSlot,body=tBeaconPoseBodyRef,marker=tBeaconPoseMarkerRef,uid=tBeaconPoseUID;
    tBeaconPoseSlot=-1;tBeaconPoseBodyRef=INVALID_ENT_REFERENCE;tBeaconPoseMarkerRef=INVALID_ENT_REFERENCE;
    if(slot>=0 && slot<MAX_BUILDINGS && gBuildings[slot].uid==uid && gBuildings[slot].ref==body)
    {RemoveBeaconMarker(slot);gBuildings[slot]=tBeaconPoseSaved;MarkBuildAreasDirty();RecalculatePower();}
    else RemoveOwnedRef(marker);
    RemoveOwnedRef(body);
    if(marker!=INVALID_ENT_REFERENCE)CheckBeaconRemovedEventually(marker,uid);
}

public Action BeaconMarkerWorldPosePoll(Handle timer,any uid)
{
    int b=tBeaconPoseSlot;
    if(b<0 || b>=MAX_BUILDINGS || uid!=tBeaconPoseUID)return Plugin_Stop;
    int body=Resolve(tBeaconPoseBodyRef),marker=Resolve(tBeaconPoseMarkerRef);
    if(gEpoch!=tBeaconPoseEpoch || gBuildings[b].uid!=uid || gBuildings[b].ref!=tBeaconPoseBodyRef)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP beacon pose fixture changed identity/epoch");FinishBeaconMarkerPoseProbe();return Plugin_Stop;}
    float origin[3],angles[3],offset[3],expected[3],actual[3],local[3];char path[128];
    Format(path,sizeof(path),"Types/%s/Marker",gBuildings[b].type);
    bool ready=body>0 && marker>0 && HasEntProp(marker,Prop_Data,"m_vecAbsOrigin")
        && BeaconMarkerVector(path,"offset","0 0 72",offset,-512.0,512.0);
    if(ready)
    {
        GetEntPropVector(body,Prop_Data,"m_vecOrigin",origin);GetEntPropVector(body,Prop_Data,"m_angRotation",angles);
        GetEntPropVector(marker,Prop_Data,"m_vecOrigin",local);GetEntPropVector(marker,Prop_Data,"m_vecAbsOrigin",actual);
        WorldPoint(offset,origin,angles,expected);
        ready=BeaconMarkerActualParent(marker)==body && GetVectorDistance(local,offset)<0.01 && GetVectorDistance(actual,expected)<0.1;
    }
    tBeaconPoseAttempt++;
    PrintToServer("[BeaconPose] stage=%d poll=%d uid=%d parent=%d local=%.3f %.3f %.3f expected_world=%.3f %.3f %.3f actual_abs=%.3f %.3f %.3f ready=%d",
        tBeaconPoseStage,tBeaconPoseAttempt,uid,marker>0 ? BeaconMarkerActualParent(marker) : -1,
        local[0],local[1],local[2],expected[0],expected[1],expected[2],actual[0],actual[1],actual[2],ready);
    if(!ready && tBeaconPoseAttempt<5)
    {CreateTimer(0.1,BeaconMarkerWorldPosePoll,uid,TIMER_FLAG_NO_MAPCHANGE);return Plugin_Stop;}
    Check(ready,tBeaconPoseStage==0
        ? "delayed actual sprite m_vecAbsOrigin matches its nonzero, rotated real parent and LOCAL configured offset"
        : "delayed actual sprite world position follows a moved and rotated real parent without double-transforming its local offset");
    if(!ready || tBeaconPoseStage==1){FinishBeaconMarkerPoseProbe();return Plugin_Stop;}
    origin[0]+=64.0;origin[1]-=32.0;origin[2]+=24.0;angles[1]+=90.0;
    TeleportEntity(body,origin,angles,NULL_VECTOR);gBuildings[b].origin=origin;gBuildings[b].angles=angles;
    // No child world-position write: the actual parent relationship must carry it.
    tBeaconPoseStage=1;tBeaconPoseAttempt=0;CreateTimer(0.1,BeaconMarkerWorldPosePoll,uid,TIMER_FLAG_NO_MAPCHANGE);
    return Plugin_Stop;
}

// Run separately from synchronous count/power checks; allow up to 1.2 seconds
// before summary/another destructive fixture, then 0.5 seconds for deletion polls.
void TestBeaconMarkerPose()
{
    if(!Running() || tBeaconPoseSlot>=0 || !gBeaconConfigAvailable)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP beacon delayed pose requires a running isolated campaign and no active pose fixture");return;}
    int core=-1,slot=-1,enabled;
    for(int c=0;c<gCoreCount;c++)if(TravelBaseAlive(c) && BeaconCount(c)==0){core=c;break;}
    for(int b=MAX_BUILDINGS-1;b>=gCoreCount;b--)if(!gBuildings[b].used || !gBuildings[b].alive){slot=b;break;}
    if(core<0 || slot<0 || gBeaconMarkerTypes==null || !gBeaconMarkerTypes.GetValue("beacon_basic",enabled) || enabled==0)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP beacon delayed pose needs a free base/place and original marker assets");return;}
    tBeaconPoseSaved=gBuildings[slot];Building fixture;fixture.used=true;fixture.alive=true;fixture.kind=Kind_Beacon;
    fixture.uid=29000001;fixture.generation=1;fixture.configRevision=gConfigRevision;fixture.level=1;
    fixture.hp=500;fixture.maxHP=500;fixture.buildCore=core;fixture.core=core;fixture.powerCore=core;
    fixture.gridAssigned=true;fixture.powerDemand=0.0;fixture.powered=true;fixture.file=C_Beacons;fixture.maxPerBase=1;
    strcopy(fixture.type,sizeof(fixture.type),"beacon_basic");fixture.origin=gCores[core].origin;
    fixture.origin[0]+=128.0;fixture.origin[1]-=48.0;fixture.origin[2]+=256.0;
    fixture.angles=view_as<float>({12.0,37.0,9.0});fixture.ref=INVALID_ENT_REFERENCE;fixture.effectRef=INVALID_ENT_REFERENCE;
    int body=CreateBody("models/props_lab/reciever01b.mdl",fixture.origin,fixture.angles,"hzsh_beacon_pose_probe",false,true);
    if(body<0){Check(false,"beacon pose creates a real rotated stock parent prop within entity budget");return;}
    fixture.ref=EntIndexToEntRef(body);gBuildings[slot]=fixture;
    tBeaconPoseSlot=slot;tBeaconPoseUID=fixture.uid;tBeaconPoseBodyRef=fixture.ref;tBeaconPoseEpoch=gEpoch;
    tBeaconPoseStage=0;tBeaconPoseAttempt=0;SyncBeaconMarker(slot);tBeaconPoseMarkerRef=gBeaconMarkerRef[slot];
    CheckBeaconMarkerParentLocal(slot);CreateTimer(0.1,BeaconMarkerWorldPosePoll,tBeaconPoseUID,TIMER_FLAG_NO_MAPCHANGE);
}

bool BeginBeaconProbe()
{
    if(tBeaconFixture || !gEnabled || !gBeaconConfigAvailable || gCoreCount<2 || gBeaconMarkerCount!=0)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP beacon requires two configured bases and no pre-existing markers");return false;}
    for(int core=0;core<gCoreCount;core++)if(BeaconCount(core)>0)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP beacon preserves existing player-built beacons");return false;}
    for(int i=0;i<3;i++)if(gBuildings[MAX_BUILDINGS-1-i].used && gBuildings[MAX_BUILDINGS-1-i].alive)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP beacon fixture slots are occupied");return false;}
    for(int core=0;core<2;core++)
    {
        int base=gCores[core].building;
        if(base<0 || base>=MAX_BUILDINGS || Resolve(gBuildings[base].ref)<0)
        {tSkipped++;PrintToServer("[ShelterTest] SKIP beacon requires existing real base entities");return false;}
    }
    tBeaconPhase=gPhase;tBeaconReady=gCoreReady;tBeaconPower=gPowerEnabled;
    tBeaconFixture=true;gPhase=Phase_Day;gCoreReady=true;gPowerEnabled=true;
    for(int core=0;core<2;core++)
    {
        int base=gCores[core].building;tBeaconBaseSaved[core]=gBuildings[base];
        gBuildings[base].alive=true;gBuildings[base].hp=1000;
    }
    for(int i=0;i<3;i++)
    {
        int slot=MAX_BUILDINGS-1-i;tBeaconSaved[i]=gBuildings[slot];tBeaconRefs[i]=INVALID_ENT_REFERENCE;
        Building fixture;fixture.used=true;fixture.alive=true;fixture.kind=Kind_Beacon;
        fixture.uid=19000001+i;fixture.generation=1;fixture.configRevision=gConfigRevision;
        fixture.buildCore=i==1 ? 1 : 0;fixture.core=fixture.buildCore;
        fixture.powerCore=fixture.buildCore;fixture.gridAssigned=true;fixture.powered=true;fixture.powerDemand=10.0;
        fixture.level=1;fixture.hp=500;fixture.maxHP=500;fixture.file=C_Beacons;fixture.maxPerBase=1;
        strcopy(fixture.type,sizeof(fixture.type),i==2 ? "another_beacon_variant" : "beacon_basic");
        fixture.origin=gCores[fixture.buildCore].origin;fixture.origin[2]+=96.0;fixture.origin[0]+=float(i)*48.0;
        fixture.ref=INVALID_ENT_REFERENCE;fixture.effectRef=INVALID_ENT_REFERENCE;
        if(i<2)
        {
            int body=CreateBody("models/props_lab/reciever01b.mdl",fixture.origin,fixture.angles,"hzsh_beacon_probe",false,true);
            if(body>0)tBeaconRefs[i]=EntIndexToEntRef(body);fixture.ref=tBeaconRefs[i];
        }
        gBuildings[slot]=fixture;
    }
    return true;
}

void EndBeaconProbe()
{
    if(!tBeaconFixture)return;
    for(int i=0;i<3;i++)
    {
        int slot=MAX_BUILDINGS-1-i;RemoveBeaconMarker(slot);RemoveOwnedRef(tBeaconRefs[i]);
        gBuildings[slot]=tBeaconSaved[i];
    }
    for(int core=0;core<2;core++)gBuildings[gCores[core].building]=tBeaconBaseSaved[core];
    gPhase=tBeaconPhase;gCoreReady=tBeaconReady;gPowerEnabled=tBeaconPower;tBeaconFixture=false;
}

void TestBeaconLogic()
{
    if(!BeginBeaconProbe())return;
    int source=MAX_BUILDINGS-1,destination=MAX_BUILDINGS-2,pending=MAX_BUILDINGS-3;
    Check(Resolve(gBuildings[source].ref)>0 && Resolve(gBuildings[destination].ref)>0,"beacon fixtures use real stock props with no fake client or NPC");
    Check(BeaconCount(0)==2 && BeaconCount(0,pending)==1 && BeaconCount(1)==1,
        "all beacon variants and pending restoration share one per-base group count");
    Check(BeaconCount(-1)==0 && BeaconCount(gCoreCount)==0,"invalid bases have no beacon allowance count");
    Check(BeaconEffective(source) && BeaconEffective(destination),"living powered beacons with fixed matching base grid are effective");
    Check(BeaconForCore(0,0)<0 && BeaconForCore(1,0)==destination,"travel lists another base and never its own source base");
    gBuildings[pending].alive=false;
    Check(BeaconCount(0)==1 && BeaconCount(0,source)==0,"destroyed beacon releases its group place; excluding upgrade target does not add itself");
    gBuildings[source].powered=false;
    Check(!BeaconEffective(source),"source power loss removes beacon availability");
    gPowerEnabled=false;
    Check(BeaconEffective(source),"power.enabled=0 keeps existing no-grid compatibility");
    gBuildings[source].powerCore=1;
    Check(!BeaconEffective(source),"power-disabled mode does not transfer a beacon to another base grid");
    gBuildings[source].powerCore=0;gBuildings[source].powered=true;gPowerEnabled=true;
    int base=gCores[0].building;gBuildings[base].alive=false;
    Check(!BeaconEffective(source),"source base loss disables its surviving beacon");gBuildings[base].alive=true;
    gBuildings[source].ref=INVALID_ENT_REFERENCE;
    Check(!BeaconEffective(source) && BeaconCount(0)==1,"missing restored body is unusable while retaining its one logical place");
    gBuildings[source].ref=tBeaconRefs[0];

    int fields[BT_Count],foundSource,foundTarget,core;BeaconTokenFields(source,1,destination,0,fields);
    Check(BeaconWorldTokenValid(fields,foundSource,foundTarget,core) && foundSource==source && foundTarget==destination && core==1,
        "beacon transaction resolves its original fixed destination identities");
    fields[BT_SourceUID]++;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"source slot reuse with a new UID rejects an old token");fields[BT_SourceUID]--;
    fields[BT_SourceGeneration]++;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"source rebuilt generation rejects an old token");fields[BT_SourceGeneration]--;
    fields[BT_TargetUID]++;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"destination beacon replacement cannot inherit an old token");fields[BT_TargetUID]--;
    fields[BT_TargetGeneration]++;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"destination beacon rebuilt generation rejects an old token");fields[BT_TargetGeneration]--;
    fields[BT_SourceBaseGeneration]++;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"source base rebuild rejects its earlier beacon menu");fields[BT_SourceBaseGeneration]--;
    fields[BT_TargetBaseUID]++;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"destination base identity replacement rejects an old token");fields[BT_TargetBaseUID]--;
    fields[BT_TargetBaseGeneration]++;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"destination base rebuild rejects an old token");fields[BT_TargetBaseGeneration]--;
    fields[BT_Epoch]++;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"round epoch changes reject beacon menus");fields[BT_Epoch]--;
    fields[BT_Revision]++;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"config revision changes reject beacon menus");fields[BT_Revision]--;
    fields[BT_TargetCore]=0;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"forged same-base destination cannot teleport back to itself");fields[BT_TargetCore]=1;
    gBuildings[destination].powered=false;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"destination power loss after menu creation rejects the click");
    gBuildings[destination].powered=true;gBuildings[gCores[1].building].alive=false;
    Check(!BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"destination base destruction after menu creation rejects the click");
    gBuildings[gCores[1].building].alive=true;
    int operation;
    Check(!ValidateBeaconMenuToken(0,"invalid",foundSource,foundTarget,core,operation),"server console cannot act as a beacon traveller");
    BeaconTokenFields(source,-1,-1,1,fields);
    Check(BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"management action retains its source transaction without inventing a destination");
    char managementToken[256];int decoded[BT_Count];BeaconEncodeFields(fields,managementToken,sizeof(managementToken));
    Check(ParseBeaconToken(managementToken,decoded) && decoded[BT_TargetCore]==-1,
        "management sentinel targetCore -1 round-trips through the production signed-field parser");
    fields[BT_SourceUID]=-1;BeaconEncodeFields(fields,managementToken,sizeof(managementToken));
    Check(!ParseBeaconToken(managementToken,decoded),"only targetCore may be signed; negative source UID is rejected");fields[BT_SourceUID]=gBuildings[source].uid;
    gBuildings[source].powered=false;
    Check(BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"unpowered existing beacon keeps its real management transaction");
    gBuildings[base].alive=false;
    Check(BeaconWorldTokenValid(fields,foundSource,foundTarget,core),"destroyed source base keeps management available for the surviving beacon");
    gBuildings[base].alive=true;gBuildings[source].powered=true;

    int markerSupported;
    if(gBeaconMarkerTypes!=null && gBeaconMarkerTypes.GetValue("beacon_basic",markerSupported) && markerSupported!=0)
    {
        SyncBeaconMarker(source);SyncBeaconMarker(destination);
        int first=gBeaconMarkerRef[source];
        Check(BeaconMarkerEntity(source)>0 && BeaconMarkerEntity(destination)>0 && gBeaconMarkerCount==2,
            "one real sprite per effective beacon is owned independently of building light effectRef");
        CheckBeaconMarkerParentLocal(source);CheckBeaconMarkerParentLocal(destination);
        Check(gBuildings[source].effectRef==INVALID_ENT_REFERENCE && gBuildings[destination].effectRef==INVALID_ENT_REFERENCE,
            "beacon markers never occupy the lighting effectRef slot");
        SyncBeaconMarker(source);SyncBeaconMarker(destination);
        Check(gBeaconMarkerRef[source]==first && gBeaconMarkerCount==2,"repeated world updates reuse markers rather than leak sprites");
        gBuildings[source].generation++;SyncBeaconMarker(source);
        int former=Resolve(first);
        Check(gBeaconMarkerRef[source]!=first && BeaconMarkerEntity(source)>0 && gBeaconMarkerCount==2
            && (former<0 || gBeaconMarkerBuilding[former]!=source+1),
            "restored beacon generation immediately releases old logical mapping and owns exactly one replacement sprite");
        CheckBeaconRemovedEventually(first,gBuildings[source].uid);
        gBuildings[source].powered=false;SyncBeaconMarker(source);
        Check(BeaconMarkerEntity(source)<0 && gBeaconMarkerCount==1,"power loss removes only that beacon marker");
        gBuildings[source].powered=true;SyncBeaconMarker(source);
        Check(BeaconMarkerEntity(source)>0 && gBeaconMarkerCount==2,"power restoration recreates exactly one marker");
        RemoveBeaconMarker(source);RemoveBeaconMarker(source);RemoveBeaconMarker(destination);
        Check(gBeaconMarkerCount==0 && BeaconMarkerEntity(source)<0 && BeaconMarkerEntity(destination)<0,"repeated removal cleans all fixture sprites without negative counts");
        int ceiling=gEntityConstructionCeiling;gEntityConstructionCeiling=GetEntityCount();SyncBeaconMarker(source);
        Check(BeaconMarkerEntity(source)<0 && gBeaconMarkerCount==0 && BeaconEffective(source),
            "entity budget denial suppresses optional marker without disabling beacon travel");gEntityConstructionCeiling=ceiling;
    }
    else{tSkipped++;PrintToServer("[ShelterTest] SKIP beacon sprite engine checks: original wh_frame assets unavailable");}
    EndBeaconProbe();
    Check(gBeaconMarkerCount==0,"beacon regression cleanup leaves no marker fixture behind");
}
