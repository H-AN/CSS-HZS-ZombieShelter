// Integration binary only. No client/NPC creation, respawn or synthetic props.
void TestRestorationLogic()
{
    int kinds[]={view_as<int>(Kind_AutoRepair),view_as<int>(Kind_Heal),view_as<int>(Kind_Armor),view_as<int>(Kind_Beacon),view_as<int>(Kind_Expander)};
    int expectedKinds[]={10,11,12,13,9};bool stable=true;
    for(int k=0;k<sizeof(kinds);k++)if(kinds[k]!=expectedKinds[k])stable=false;
    Check(stable,"new restoration kinds append after every legacy building enum value");
    Check(IsRestorationKind(Kind_AutoRepair) && IsRestorationKind(Kind_Heal) && IsRestorationKind(Kind_Armor)
        && !IsRestorationKind(Kind_Beacon) && !IsRestorationKind(Kind_Amplifier),"only the three automatic recovery classes participate in the restoration timer");
    Check(RestorationDelta(99,100,5)==1 && RestorationDelta(100,100,5)==0 && RestorationDelta(101,100,5)==0
        && RestorationDelta(20,100,0)==0 && RestorationDelta(1999999999,2000000000,1000000)==1,
        "recovery clamps to the actual remaining capacity without overflow or lowering existing excess health");

    KeyValues previous=gCfg[C_Repair],fixture=new KeyValues("ShelterRepairStations");gCfg[C_Repair]=fixture;
    fixture.JumpToKey("Limits",true);fixture.SetNum("max_per_base",2);
    fixture.Rewind();fixture.JumpToKey("Types",true);fixture.JumpToKey("probe_low",true);
    fixture.JumpToKey("Levels",true);fixture.JumpToKey("1",true);
    fixture.SetNum("max_hp",500);fixture.SetNum("restore_amount",1);fixture.SetFloat("restore_interval_seconds",20.0);fixture.SetNum("max_per_base",5);
    Node(C_Repair,"Types/probe_low");fixture.JumpToKey("Growth",true);fixture.SetNum("max_level",3);fixture.SetNum("anchor_level",1);
    fixture.JumpToKey("Stats",true);fixture.SetNum("restore_amount_per_level",1);fixture.SetFloat("restore_interval_seconds_per_level",-2.0);fixture.SetFloat("restore_interval_seconds_min",1.0);
    int amount;float interval;
    Check(ReadLevelInt(C_Repair,"Types/probe_low",3,"restore_amount",0,amount) && amount==3
        && ReadLevelFloat(C_Repair,"Types/probe_low",3,"restore_interval_seconds",20.0,interval) && FloatAbs(interval-16.0)<0.001,
        "station variants use the production Levels/Growth resolver for recovery amount and interval");
    Check(RestorationBuildLimit(C_Repair,"probe_low",1)==2,"the class-wide group cap constrains the per-type level cap");
    Node(C_Repair,"Types/probe_low/Levels/1");fixture.SetNum("max_per_base",1);
    Check(RestorationBuildLimit(C_Repair,"probe_low",1)==1,"a smaller current-level station cap still applies");
    Node(C_Repair,"Types/probe_low/Levels/1");fixture.SetFloat("restore_interval_seconds",0.2);
    Check(ReadLevelFloat(C_Repair,"Types/probe_low",1,"restore_interval_seconds",20.0,interval) && interval==1.0,
        "a sub-second station interval is normalized to one second without rejecting the configuration");
    Node(C_Repair,"Types/probe_low/Levels/1");fixture.SetFloat("restore_interval_seconds",-5.0);
    Check(ReadLevelFloat(C_Repair,"Types/probe_low",1,"restore_interval_seconds",20.0,interval) && interval==1.0,
        "a finite negative station interval is also normalized to one second");
    Node(C_Repair,"Types/probe_low/Levels/1");fixture.SetString("restore_interval_seconds","nan");
    Check(!ReadLevelFloat(C_Repair,"Types/probe_low",1,"restore_interval_seconds",20.0,interval),"non-finite interval text is still rejected");
    gCfg[C_Repair]=previous;delete fixture;

    if(gCoreCount>0)
    {
        int a=MAX_BUILDINGS-1,b=MAX_BUILDINGS-2;Building savedA,savedB,value;savedA=gBuildings[a];savedB=gBuildings[b];
        value.used=true;value.alive=true;value.kind=Kind_AutoRepair;value.buildCore=0;strcopy(value.type,sizeof(value.type),"repair_low");gBuildings[a]=value;
        strcopy(value.type,sizeof(value.type),"repair_high");gBuildings[b]=value;
        int count=RestorationCount(0,Kind_AutoRepair);
        Check(count>=2 && RestorationCount(0,Kind_AutoRepair,a)==count-1,"low and high station variants share one per-base class count including bodies awaiting restoration");
        gBuildings[b].kind=Kind_Heal;
        Check(RestorationCount(0,Kind_AutoRepair)==count-1,"a different restoration class never consumes the repair-station group cap");
        gBuildings[a]=savedA;gBuildings[b]=savedB;
        if(gCores[0].boxCount>0)
        {
            float local[3],inside[3],outside[3];for(int axis=0;axis<3;axis++)local[axis]=(gCoreMins[0][0][axis]+gCoreMaxs[0][0][axis])*0.5;
            WorldPoint(local,gCores[0].origin,gCores[0].angles,inside);outside=inside;outside[2]+=100000.0;
            Check(RestorationSafeContains(0,inside) && !RestorationSafeContains(0,outside),"player recovery uses the base's original three-dimensional green boxes");
        }
    }
}

void RestorationProbeMetadata(int station,BuildingKind kind)
{
    for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==kind){gBuildings[station].meta=gRecipes[r].meta;return;}
    gBuildings[station].meta.pulseSound[0]=0;
}

void TestRestorationReal(int client)
{
    int humans;for(int c=1;c<=MaxClients;c++)if(LiveHuman(c) && !IsFakeClient(c) && !IsClientSourceTV(c) && !IsClientReplay(c))humans++;
    if(client<1 || !LiveHuman(client) || IsFakeClient(client) || !Running() || tV05RealActive
        || humans!=1 || gCoreCount<2 || gCores[0].boxCount<1 || !V05BeginRealFixture(client))
    {tSkipped++;PrintToServer("[ShelterTest] SKIP restoration needs exactly one existing living real client, two live base bodies and an unused real fixture; none is created");return;}
    int station=gCores[0].building,target=gCores[1].building;
    if(station<0 || target<0 || Resolve(gBuildings[station].ref)<0 || Resolve(gBuildings[target].ref)<0
        || !gBuildings[station].alive || !gBuildings[target].alive)
    {tSkipped++;V05RealCleanup(client);return;}
    Building savedStation,savedTarget;savedStation=gBuildings[station];savedTarget=gBuildings[target];
    int savedHP[MAX_BUILDINGS],savedArmor=GetEntProp(client,Prop_Send,"m_ArmorValue");float savedNext[MAX_BUILDINGS];
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        savedHP[b]=gBuildings[b].hp;savedNext[b]=gBuildings[b].nextRestore;
        if(gBuildings[b].used && gBuildings[b].alive)gBuildings[b].hp=gBuildings[b].maxHP;
        if(IsRestorationKind(gBuildings[b].kind))gBuildings[b].nextRestore=GetGameTime()+3600.0;
    }
    bool maxProperty=HasEntProp(client,Prop_Data,"m_iMaxHealth");int savedMax=maxProperty ? GetEntProp(client,Prop_Data,"m_iMaxHealth") : 0;
    gBuildings[station].kind=Kind_AutoRepair;gBuildings[station].buildCore=0;gBuildings[station].powerCore=0;
    gBuildings[station].gridAssigned=true;gBuildings[station].powered=false;gBuildings[station].powerDemand=10.0;
    gBuildings[station].restoreAmount=5;gBuildings[station].restoreInterval=20.0;gBuildings[station].nextRestore=0.0;
    RestorationProbeMetadata(station,Kind_AutoRepair);
    gBuildings[station].placementAnchor=gCores[0].buildCenter;
    gBuildings[target].placementAnchor=gCores[0].buildCenter;gBuildings[target].hp=gBuildings[target].maxHP-2;
    MarkBuildAreasDirty();RefreshBuildAreas();
    int pulses=gRestorationPulseCount,before=gBuildings[target].hp;
    Check(RestorationPulse(station)==0 && gBuildings[target].hp==before && gRestorationPulseCount==pulses,
        "a real existing facility belonging to another base receives no automatic repair even inside this red rectangle");
    gBuildings[target].buildCore=0;gBuildings[target].placementAnchor[2]=gCores[0].buildCenter[2]+gCores[0].buildZMax+64.0;
    Check(RestorationPulse(station)==0 && gBuildings[target].hp==before,"a same-base existing facility outside the current red volume is retained but receives no repair");
    gBuildings[target].placementAnchor=gCores[0].buildCenter;gBuildings[target].alive=false;gBuildings[target].hp=0;
    Check(RestorationPulse(station)==0 && gBuildings[target].hp==0,"automatic repair never resurrects a destroyed facility");
    gBuildings[target].alive=true;gBuildings[target].hp=gBuildings[target].maxHP-2;
    gPowerEnabled=true;
    Check(!RestorationReady(station) && RestorationPulse(station)==0,"an actually unpowered station cannot repair even when the target is eligible");
    float now=GetGameTime();gBuildings[station].nextRestore=now;
    RestorationTick(now);
    Check(gBuildings[target].hp==gBuildings[target].maxHP-2 && gBuildings[station].nextRestore>=now+19.99,
        "a due offline station advances its own deadline without storing a catch-up pulse");
    gPowerEnabled=false;
    Check(RestorationReady(station),"power.enabled=0 restores station compatibility without anonymous power assignment");
    gBuildings[station].gridAssigned=false;
    Check(!RestorationReady(station),"disabling the power system does not bypass a station's fixed-base assignment requirement");
    gBuildings[station].gridAssigned=true;gBuildings[station].powerCore=1;
    Check(!RestorationReady(station),"a mismatched power-grid assignment cannot move restoration into another base");
    gBuildings[station].powerCore=0;gBuildings[station].nextRestore=0.0;
    Building targetBeforeBaseCheck;targetBeforeBaseCheck=gBuildings[target];
    gBuildings[target].kind=Kind_Heal;gBuildings[target].powerCore=0;gBuildings[target].gridAssigned=true;
    gBuildings[station].alive=false;
    Check(!RestorationReady(target),"a live assigned station with a real body remains inactive when its owning base is destroyed");
    gBuildings[station].alive=true;gBuildings[target]=targetBeforeBaseCheck;
    RestorationTick(now);RestorationTick(now+19.99);
    Check(gBuildings[target].hp==gBuildings[target].maxHP-2 && FloatAbs(gBuildings[station].nextRestore-(now+20.0))<0.01,
        "the production timer waits the full per-station interval before its first recovery");
    RestorationTick(now+20.0);int atCap=gBuildings[target].hp;
    Check(atCap==gBuildings[target].maxHP && gRestorationPulseCount==pulses+1,
        "a due real facility repair clamps a five-point grant to two missing HP and emits exactly one useful pulse");
    RestorationTick(now+20.0);
    Check(gRestorationPulseCount==pulses+1 && gBuildings[target].hp==atCap,"repeating the world tick at the same time cannot duplicate recovery");
    gBuildings[target].hp=atCap-12;RestorationTick(now+900.0);
    Check(gBuildings[target].hp==atCap-7 && gBuildings[station].nextRestore>=now+919.99,
        "a long elapsed gap grants one recovery and schedules from now rather than replaying missed intervals");
    gBuildings[target].hp=gBuildings[target].maxHP;gBuildings[station].hp=gBuildings[station].maxHP-1;
    Check(RestorationPulse(station)==1 && gBuildings[station].hp==gBuildings[station].maxHP,
        "a repair station can repair its own existing body in the assigned red rectangle");
    pulses=gRestorationPulseCount;
    Check(RestorationPulse(station)==0 && gRestorationPulseCount==pulses,"fully repaired real facilities cause no ring or sound pulse");

    float local[3],inside[3],outside[3];for(int axis=0;axis<3;axis++)local[axis]=(gCoreMins[0][0][axis]+gCoreMaxs[0][0][axis])*0.5;
    WorldPoint(local,gCores[0].origin,gCores[0].angles,inside);outside=inside;outside[2]+=4096.0;
    SetEntityMoveType(client,MOVETYPE_NOCLIP);TeleportEntity(client,inside,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    gBuildings[station].kind=Kind_Heal;RestorationProbeMetadata(station,Kind_Heal);
    if(maxProperty)
    {
        SetEntProp(client,Prop_Data,"m_iMaxHealth",250);SetEntityHealth(client,249);
        Check(RestorationHuman(client,0) && RestorationPulse(station)>0 && GetClientHealth(client)==250,
            "a real living player heals to the actual engine maximum of 250 HP without an artificial 100-HP cap");
        pulses=gRestorationPulseCount;
        Check(RestorationPulse(station)==0 && gRestorationPulseCount==pulses,"a player at their actual maximum causes no healing ring or sound");
        SetEntityHealth(client,240);TeleportEntity(client,outside,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));pulses=gRestorationPulseCount;
        Check(!RestorationHuman(client,0) && RestorationPulse(station)==0 && GetClientHealth(client)==240 && gRestorationPulseCount==pulses,
            "a living real player above the original green volume receives no healing or empty pulse");
    }
    else{tSkipped++;PrintToServer("[ShelterTest] SKIP engine player class has no writable m_iMaxHealth field");}
    TeleportEntity(client,inside,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));gBuildings[station].kind=Kind_Armor;RestorationProbeMetadata(station,Kind_Armor);
    SetEntProp(client,Prop_Send,"m_ArmorValue",99);
    Check(RestorationPulse(station)>0 && GetEntProp(client,Prop_Send,"m_ArmorValue")==100,"a real armor pulse clamps to the default 100-armor maximum");
    pulses=gRestorationPulseCount;
    Check(RestorationPulse(station)==0 && gRestorationPulseCount==pulses,"full real-player armor causes no ring or ambient sound");
    PrintToServer("[RestorationReal] serial=%d station_ref=%d target_ref=%d useful_pulses=%d client_npc_creation=0",
        GetClientSerial(client),savedStation.ref,savedTarget.ref,gRestorationPulseCount);

    gBuildings[station]=savedStation;gBuildings[target]=savedTarget;
    for(int b=0;b<MAX_BUILDINGS;b++){gBuildings[b].hp=savedHP[b];gBuildings[b].nextRestore=savedNext[b];}
    if(maxProperty)SetEntProp(client,Prop_Data,"m_iMaxHealth",savedMax);
    SetEntProp(client,Prop_Send,"m_ArmorValue",savedArmor);
    V05RealCleanup(client);MarkBuildAreasDirty();RefreshBuildAreas();
}
