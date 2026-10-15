// Isolated engine and mathematical checks. No human, bot, or hostage is created.
Building tBuildSaved[5];
Building tQuotaSaved[80];
void TestV05BuildLogic()
{
    if(!gEnabled || gCoreCount<1 || gCores[0].building<0)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP v05build requires loaded map configuration");return;}
    for(int i=0;i<5;i++)if(gBuildings[MAX_BUILDINGS-1-i].used && gBuildings[MAX_BUILDINGS-1-i].alive)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP v05build fixture slots occupied");return;}
    int base=gCores[0].building;Building savedBase;savedBase=gBuildings[base];CoreDef savedCore;savedCore=gCores[0];
    Phase savedPhase=gPhase;bool savedReady=gCoreReady,savedPower=gPowerEnabled;
    gPhase=Phase_Day;gCoreReady=true;gPowerEnabled=false;
    for(int i=0;i<5;i++){tBuildSaved[i]=gBuildings[MAX_BUILDINGS-1-i];Building empty;gBuildings[MAX_BUILDINGS-1-i]=empty;}
    gBuildings[base].alive=true;gBuildings[base].hp=100;gBuildings[base].baseBuildLimit=72;
    gBuildings[base].buildHalfExtents[0]=100.0;gBuildings[base].buildHalfExtents[1]=80.0;
    gCores[0].buildHardLimit=128;gCores[0].buildCenter=view_as<float>({1000.0,2000.0,50.0});
    gCores[0].buildAngles=view_as<float>({0.0,90.0,0.0});gCores[0].buildZMin=-100.0;gCores[0].buildZMax=500.0;
    gBuildAreas[0].cornersReady=false;MarkBuildAreasDirty();RefreshBuildAreas();
    int baselineCount=gBuildAreas[0].count;
    Check(gBuildAreas[0].limit==72,"level-three base quota is 72 without expanders");
    float local[3]={99.0,79.0,0.0},point[3];WorldPoint(local,gCores[0].buildCenter,gCores[0].buildAngles,point);
    Check(BuildAreaContains(0,point),"rotated fixed-center build rectangle accepts a local interior anchor");
    local[0]=101.0;WorldPoint(local,gCores[0].buildCenter,gCores[0].buildAngles,point);
    Check(!BuildAreaContains(0,point),"rectangle excludes anchors outside its horizontal edge");
    local[0]=0.0;local[2]=501.0;WorldPoint(local,gCores[0].buildCenter,gCores[0].buildAngles,point);
    Check(!BuildAreaContains(0,point) && BuildAreaContains(0,point,false),"height restriction is distinct from horizontal build range");
    local[2]=500.0;WorldPoint(local,gCores[0].buildCenter,gCores[0].buildAngles,point);
    Check(BuildAreaContains(0,point),"build height boundary uses the same inclusive geometry");
    float frame[3]={0.0,90.0,0.0},offset[3]={0.0,0.0,0.0},angles[3],position[3],zero[3];
    ComposeModelAngles(frame,offset,angles);offset=view_as<float>({10.0,0.0,0.0});WorldPoint(offset,zero,angles,position);
    Check(FloatAbs(position[0])<0.001 && FloatAbs(position[1]-10.0)<0.001,"model-local offsets rotate with final yaw without global lifting");
    int refs[3];for(int i=0;i<3;i++)refs[i]=INVALID_ENT_REFERENCE;
    int exp=MAX_BUILDINGS-1,gen=MAX_BUILDINGS-2,amp=MAX_BUILDINGS-3,extra=MAX_BUILDINGS-4,waiting=MAX_BUILDINGS-5;
    for(int i=0;i<3;i++)
    {
        int b=MAX_BUILDINGS-1-i;Building fixture;fixture.used=true;fixture.alive=true;fixture.hp=100;fixture.maxHP=100;
        fixture.uid=900000+i;fixture.buildCore=0;fixture.powerCore=0;fixture.gridAssigned=true;fixture.interval=0.5;
        fixture.origin=gCores[0].buildCenter;fixture.origin[2]+=100.0;
        int body=CreateBody("models/props_lab/reciever01b.mdl",fixture.origin,zero,"hzsh_v05_build_fixture",false,true);
        if(body>0)refs[i]=EntIndexToEntRef(body);fixture.ref=refs[i];gBuildings[b]=fixture;
    }
    Check(Resolve(refs[0])>0 && Resolve(refs[1])>0 && Resolve(refs[2])>0,"engine creates independent stock-model fixtures without clients");
    gBuildings[exp].kind=Kind_Expander;gBuildings[exp].buildLimitBonus=5;gBuildings[exp].buildRangeBonus=100.0;gBuildings[exp].powerDemand=5.0;
    gBuildings[gen].kind=Kind_Generator;gBuildings[gen].powerOutput=100.0;
    gBuildings[amp].kind=Kind_Amplifier;gBuildings[amp].level=1;gBuildings[amp].powerDemand=5.0;strcopy(gBuildings[amp].type,sizeof(gBuildings[].type),"damage_amplifier");
    MarkBuildAreasDirty();RecalculatePower();
    Check(gBuildAreas[0].limit==77 && gBuildAreas[0].halfX==200.0,"effective level-one expander adds current +5 quota and 100 to each side");
    gBuildings[exp].level=2;gBuildings[exp].buildLimitBonus=10;gBuildings[exp].buildRangeBonus=200.0;MarkBuildAreasDirty();RecalculatePower();
    Check(gBuildAreas[0].limit==82,"level-two expander replaces +5 with +10: 72+10=82, never 87");
    gBuildings[extra]=gBuildings[exp];gBuildings[extra].buildLimitBonus=8;gBuildings[extra].uid++;
    MarkBuildAreasDirty();RecalculatePower();Check(gBuildAreas[0].limit==90,"another expander variant independently adds its current +8 quota");
    gBuildings[extra].buildLimitBonus=256;MarkBuildAreasDirty();RecalculatePower();Check(gBuildAreas[0].limit==128,"per-base hard ceiling clamps combined expander quota");
    gCores[0].buildHardLimit=256;MarkBuildAreasDirty();RecalculatePower();int storageCeiling=MAX_BUILDINGS;
    Check(gBuildAreas[0].limit==256 && storageCeiling>3*256,"256 per-base configuration has a larger global storage ceiling");
    gBuildings[extra].alive=false;gCores[0].buildHardLimit=128;
    gBuildings[exp].origin[0]+=10000.0;MarkBuildAreasDirty();RecalculatePower();
    Check(gBuildAreas[0].limit==82 && EffectiveExpander(exp),"existing powered expander contributes even outside the shrunken territory");
    Building logical;logical.used=true;logical.alive=true;logical.kind=Kind_Fence;logical.buildCore=0;logical.ref=INVALID_ENT_REFERENCE;gBuildings[waiting]=logical;
    MarkBuildAreasDirty();RecalculatePower();Check(gBuildAreas[0].count==baselineCount+4,"logical building awaiting entity restoration still occupies one quota");
    int amplifierCount;float withPower=AmplifierBonus(0,amplifierCount);
    gPowerEnabled=true;gBuildings[gen].powerOutput=0.0;RecalculatePower();
    Check(gBuildAreas[0].limit==72 && !EffectiveExpander(exp),"real zero-supply allocation withdraws expander quota and range");
    float withoutPower=AmplifierBonus(0,amplifierCount);
    Check(withPower>withoutPower && amplifierCount==0,"amplifier damage contribution vanishes when actual power allocation fails");
    Check(gBuildings[waiting].alive && gBuildings[exp].alive,"shrinking territory and quota keep existing logical facilities alive");
    int added=80-gBuildAreas[0].count;bool slotsFree=added>=0 && added<=80;
    for(int i=0;i<added && slotsFree;i++)if(gBuildings[MAX_BUILDINGS-6-i].used && gBuildings[MAX_BUILDINGS-6-i].alive)slotsFree=false;
    if(slotsFree)
    {
        for(int i=0;i<added;i++){int b=MAX_BUILDINGS-6-i;tQuotaSaved[i]=gBuildings[b];gBuildings[b]=logical;}
        MarkBuildAreasDirty();RefreshBuildAreas();
        Check(gBuildAreas[0].count==80 && gBuildAreas[0].limit==72 && !CheckBuildCapacity(0,0),"80 existing buildings remain at quota 72 and new admission is rejected");
        bool retained=true;for(int i=0;i<added;i++)if(!gBuildings[MAX_BUILDINGS-6-i].alive)retained=false;
        Check(retained,"over-quota logic preserves every existing facility instead of deleting the excess eight");
        for(int i=0;i<added;i++)gBuildings[MAX_BUILDINGS-6-i]=tQuotaSaved[i];
        MarkBuildAreasDirty();RefreshBuildAreas();
    }
    else {tSkipped++;PrintToServer("[ShelterTest] SKIP 80/72 fixture slots unavailable");}
    gBuildings[gen].powerOutput=100.0;RecalculatePower();
    Check(gBuildAreas[0].limit==82 && BuildingPowered(exp),"restoring generator output restores expander quota");
    float restored=AmplifierBonus(0,amplifierCount);Check(FloatAbs(restored-withPower)<0.0001,"restoring power restores amplifier damage contribution");
    gBuildings[gen].powerOutput=0.0;gPowerEnabled=false;RecalculatePower();
    Check(gBuildAreas[0].limit==82 && FloatAbs(AmplifierBonus(0,amplifierCount)-withPower)<0.0001,"power.enabled=0 keeps expander and amplifier compatibility");
    gBuildings[exp].powerDemand=0.0;gPowerEnabled=true;RecalculatePower();Check(EffectiveExpander(exp),"zero-demand expander remains valid without generator output");
    int redBefore=gRedConstructed;gNextBuildDisplay=0.0;DrawBuildZones();
    Check(TestHuman()>0 || gRedConstructed==redBefore,"no living real client means no red temporary effect is constructed");
    // Restore an existing overlapping building outside the temporary test area.
    // This exercises the production restoration entry without adding a client.
    gBuildings[waiting].origin=savedBase.origin;gBuildings[waiting].placementAnchor=savedBase.origin;
    gBuildings[waiting].angles=view_as<float>({0.0,45.0,0.0});gBuildings[waiting].uid=900099;
    gBuildings[waiting].configRevision=gConfigRevision;gBuildings[waiting].hp=37;gBuildings[waiting].maxHP=100;
    strcopy(gBuildings[waiting].model,sizeof(gBuildings[].model),"models/props_lab/reciever01b.mdl");
    ReadTypeMetadata(C_Fences,"wood_fence",gBuildings[waiting].meta);
    gBuildings[waiting].meta.render[0]=12;gBuildings[waiting].meta.render[1]=34;gBuildings[waiting].meta.render[2]=56;gBuildings[waiting].meta.render[3]=200;
    int coreMaterial=FindMaterial("plague_core");if(coreMaterial>=0)gBuildings[waiting].invested[coreMaterial]=3;
    Check(RestoreSavedBuilding(waiting) && Resolve(gBuildings[waiting].ref)>0,
        "saved overlapping building outside the current rectangle restores without a strict ClearHull gate");
    int restoredBody=Resolve(gBuildings[waiting].ref),red,green,blue,alpha;
    if(restoredBody>0)GetEntityRenderColor(restoredBody,red,green,blue,alpha);
    Check(red==12 && green==34 && blue==56 && alpha==200,"restored body applies the saved variant RGBA to the actual engine entity");
    Check(gBuildings[waiting].hp==37 && gBuildings[waiting].uid==900099
        && gBuildings[waiting].origin[0]==savedBase.origin[0] && gBuildings[waiting].angles[1]==45.0
        && (coreMaterial<0 || gBuildings[waiting].invested[coreMaterial]==3),
        "restoration preserves final transform, HP, UID and special-material investment without reapplying offsets");
    int restoredRef=gBuildings[waiting].ref;gBuildings[waiting].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(restoredRef);
    for(int i=0;i<3;i++)RemoveOwnedRef(refs[i]);
    for(int i=0;i<5;i++)gBuildings[MAX_BUILDINGS-1-i]=tBuildSaved[i];
    gBuildings[base]=savedBase;gCores[0]=savedCore;gPhase=savedPhase;gCoreReady=savedReady;gPowerEnabled=savedPower;
    gBuildAreas[0].cornersReady=false;MarkBuildAreasDirty();RecalculatePower();
}
