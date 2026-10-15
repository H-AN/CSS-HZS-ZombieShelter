// Test binary only: actual production readers, ownership and NPC attack path.
bool tFenceObserve;
int tFenceObserveNPCRef=INVALID_ENT_REFERENCE,tFenceHurtCount,tFenceHurtClientSerial;
RewardContext tFenceHurtReward;

public Action FenceRetaliationRemovedPoll(Handle timer,DataPack pack)
{
    pack.Reset();int ref=pack.ReadCell(),epoch=pack.ReadCell(),uid=pack.ReadCell();
    float started=pack.ReadFloat();int poll=pack.ReadCell()+1;
    if(epoch!=gEpoch)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP deferred fence removal check crossed a campaign epoch (old=%d current=%d)",epoch,gEpoch);return Plugin_Stop;}
    int entity=Resolve(ref);
    PrintToServer("[FenceRetaliationRemoved] old_ref=%d old_resolve=%d epoch=%d source_uid=%d poll=%d elapsed=%.3f",ref,entity,epoch,uid,poll,GetGameTime()-started);
    if(entity<0 || poll>=5)
    {
        Check(entity<0,"the consumed lethal-strike fence's original EntRef is physically invalid within five 0.1-second cleanup polls");
        return Plugin_Stop;
    }
    // The timer owns this pack. Only the captured identity is polled; fixture slots may already be reused.
    pack.Reset(true);pack.WriteCell(ref);pack.WriteCell(epoch);pack.WriteCell(uid);pack.WriteFloat(started);pack.WriteCell(poll);
    return Plugin_Continue;
}

public void Han_OnZombieHurt(int zombie,int attacker)
{
    if(!tFenceObserve || EntIndexToEntRef(zombie)!=tFenceObserveNPCRef || gDamageNPCRef!=tFenceObserveNPCRef)return;
    tFenceHurtCount++;tFenceHurtClientSerial=GetClientSerial(attacker);tFenceHurtReward=gDamageRewardContext;
}

void TestFenceRetaliationLogic()
{
    KeyValues previous=gCfg[C_Fences],fixture=new KeyValues("ShelterFences");
    fixture.JumpToKey("Types",true);fixture.JumpToKey("probe_fence",true);fixture.JumpToKey("Levels",true);fixture.JumpToKey("1",true);fixture.SetNum("max_hp",300);
    gCfg[C_Fences]=fixture;float value;
    Check(ReadLevelFloat(C_Fences,"Types/probe_fence",1,"damage",0.0,value) && value==0.0,"fence damage omitted resolves to zero without an implicit retaliation default");
    Node(C_Fences,"Types/probe_fence/Levels/1");fixture.SetFloat("damage",10.0);
    Node(C_Fences,"Types/probe_fence");fixture.JumpToKey("Growth",true);fixture.SetNum("anchor_level",1);fixture.SetNum("max_level",3);
    fixture.JumpToKey("Stats",true);fixture.SetFloat("damage_per_level",5.0);
    Check(ReadLevelFloat(C_Fences,"Types/probe_fence",3,"damage",0.0,value) && FloatAbs(value-20.0)<0.001,"fence variants inherit the existing damage Growth resolver");
    Node(C_Fences,"Types/probe_fence/Levels");fixture.JumpToKey("3",true);fixture.SetFloat("damage",7.0);
    Check(ReadLevelFloat(C_Fences,"Types/probe_fence",3,"damage",0.0,value) && FloatAbs(value-7.0)<0.001,"explicit fence level damage overrides the shared Growth formula");
    gCfg[C_Fences]=previous;delete fixture;

    int b=MAX_BUILDINGS-1;Building saved;saved=gBuildings[b];Building fence;
    fence.used=true;fence.alive=true;fence.kind=Kind_Fence;fence.uid=27831;gBuildings[b]=fence;
    Check(FenceRetaliationDamage(b)==0,"a zero-damage fence cannot reflect any attack");
    gBuildings[b].damage=10.0;Check(FenceRetaliationDamage(b)==10,"a configured fence uses its resolved current-level damage");
    gBuildings[b].kind=Kind_Turret;Check(FenceRetaliationDamage(b)==0,"an armed turret never inherits fence contact retaliation");
    gBuildings[b].kind=Kind_Generator;Check(FenceRetaliationDamage(b)==0,"non-fence facilities ignore the same damage scalar for retaliation");
    gBuildings[b].kind=Kind_Fence;gBuildings[b].alive=false;Check(FenceRetaliationDamage(b)==0,"an already destroyed fence cannot start a fresh retaliation event");
    gBuildings[b].alive=true;
    int real;for(int c=1;c<=MaxClients;c++)if(FenceRetaliationClient(c))real++;
    if(real==0)Check(SelectFenceRetaliationClient(b)==0,"without any online real client an unowned fence obtains no damage recipient");
    else{int recipient=SelectFenceRetaliationClient(b);Check(FenceRetaliationClient(recipient),"an unowned fence randomly selects an actual online real client");}
    FenceRetaliationEvent empty;
    Check(!ApplyFenceRetaliation(empty),"an absent attack snapshot cannot generate HZS damage");
    gBuildings[b]=saved;
}

void TestFenceRetaliationReal(int client)
{
    if(client<1 || !LiveHuman(client) || IsFakeClient(client) || !Running() || tV05RealActive)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP fence retaliation requires an existing living real client and no active real-client fixture");return;}
    int recipe=-1;for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Fence){recipe=r;break;}
    int target=-1;for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].ready && !gNPCs[n].dead && gNPCs[n].boss<0 && Resolve(gNPCs[n].ref)>0){target=n;break;}
    if(recipe<0 || target<0 || gLeaderOffset<0 || !V05BeginRealFixture(client))
    {tSkipped++;PrintToServer("[ShelterTest] SKIP fence retaliation needs a fence recipe and existing ordinary native NPC; none is created");return;}
    int priorSlot=-1;for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(!gBuildings[b].used || !gBuildings[b].alive){priorSlot=b;break;}
    if(priorSlot<0){tSkipped++;V05RealCleanup(client);return;}
    Building prior;prior=gBuildings[priorSlot];
    float point[3];point=gCores[0].origin;point[2]+=350.0;
    int b=TestBuild(recipe,point);
    if(b<0){Check(false,"retaliation fixture creates one existing-model fence body");gBuildings[priorSlot]=prior;V05RealCleanup(client);return;}
    AssignBuildingOwner(b,client);gBuildings[b].buildCore=0;gBuildings[b].damage=0.0;gBuildings[b].hp=300;gBuildings[b].maxHP=300;
    int entity=Resolve(gNPCs[target].ref),body=Resolve(gBuildings[b].ref),health=GetEntProp(entity,Prop_Data,"m_iHealth"),leader=GetEntDataEnt2(entity,gLeaderOffset);
    NPC savedNPC;savedNPC=gNPCs[target];float npcPosition[3],npcAngles[3];
    GetEntPropVector(entity,Prop_Data,"m_vecOrigin",npcPosition);GetEntPropVector(entity,Prop_Data,"m_angRotation",npcAngles);
    float mins[3],maxs[3],local[3],npcPoint[3],humanPoint[3];GetEntPropVector(body,Prop_Send,"m_vecMins",mins);GetEntPropVector(body,Prop_Send,"m_vecMaxs",maxs);
    local[0]=(mins[0]+maxs[0])*0.5;local[1]=mins[1]-24.0;local[2]=mins[2];WorldPoint(local,gBuildings[b].origin,gBuildings[b].angles,npcPoint);
    local[1]=maxs[1]+100.0;WorldPoint(local,gBuildings[b].origin,gBuildings[b].angles,humanPoint);
    TeleportEntity(client,humanPoint,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    TeleportEntity(entity,npcPoint,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));SetEntProp(entity,Prop_Data,"m_iHealth",1000000);
    RestoreNPCBody(target);gNPCs[target].active=true;gNPCs[target].attackingUID=0;gNPCs[target].touchedUID=gBuildings[b].uid;
    gNPCs[target].touchedUntil=GetGameTime()+30.0;gNPCs[target].nextAttack=0.0;SetEntDataEnt2(entity,gLeaderOffset,client);
    tFenceObserve=true;tFenceObserveNPCRef=gNPCs[target].ref;tFenceHurtCount=0;
    bool route=BlockingBuilding(target,client)==b;
    Check(route,"an existing native NPC's actual contact route selects the fence collider");
    if(route)
    {
        int fenceHP=gBuildings[b].hp;NPCTick(GetGameTime());
        Check(gBuildings[b].hp<fenceHP && tFenceHurtCount==0 && GetEntProp(entity,Prop_Data,"m_iHealth")==1000000,"a real zero-damage fence strike reduces fence HP without hurting the NPC");
        gBuildings[b].damage=10.0;gNPCs[target].nextAttack=0.0;SetEntDataEnt2(entity,gLeaderOffset,client);
        int expected=10;ApplyProgressionDamage(target,client,expected,true);int beforeHP=GetEntProp(entity,Prop_Data,"m_iHealth");
        fenceHP=gBuildings[b].hp;NPCTick(GetGameTime());
        Check(gBuildings[b].hp<fenceHP && tFenceHurtCount==1 && beforeHP-GetEntProp(entity,Prop_Data,"m_iHealth")==expected,"one actual NPC fence strike performs one configured retaliation through the defense damage channel");
        Check(tFenceHurtClientSerial==GetClientSerial(client) && tFenceHurtReward.facility
            && tFenceHurtReward.ownerSerial==GetClientSerial(client) && tFenceHurtReward.buildCore==0
            && tFenceHurtReward.sourceUID==gBuildings[b].uid,"retaliation calls the real fence owner and preserves its fixed-base reward context");
        beforeHP=GetEntProp(entity,Prop_Data,"m_iHealth");fenceHP=gBuildings[b].hp;NPCTick(GetGameTime());
        Check(tFenceHurtCount==1 && gBuildings[b].hp==fenceHP && GetEntProp(entity,Prop_Data,"m_iHealth")==beforeHP,"the existing NPC nextAttack gate prevents duplicate contact damage and reflection in the same attack interval");
        Check(SelectFenceRetaliationClient(b)==client,"online fence owner is preferred over random fallback");
        gBuildings[b].ownerSerial=0;gBuildings[b].ownerKey[0]=0;
        FenceRetaliationEvent fallback;
        Check(CaptureFenceRetaliation(b,target,fallback) && fallback.reward.ownerSerial==GetClientSerial(client)
            && StrEqual(fallback.reward.ownerKey,gInventoryOwner[client]),"an unowned fence captures the only online real client as its actual recipient, not an anonymous proxy");
        AssignBuildingOwner(b,client);
        int oldRef=gBuildings[b].ref,uid=gBuildings[b].uid;gBuildings[b].hp=1;gNPCs[target].nextAttack=0.0;SetEntDataEnt2(entity,gLeaderOffset,client);
        int beforeCount=tFenceHurtCount;beforeHP=GetEntProp(entity,Prop_Data,"m_iHealth");NPCTick(GetGameTime());
        PrintToServer("[FenceRetaliationLethal] alive=%d hp=%d logical_ref=%d old_ref=%d old_resolve=%d hurt_delta=%d hp_delta=%d expected=%d epoch=%d uid=%d",
            gBuildings[b].alive,gBuildings[b].hp,gBuildings[b].ref,oldRef,Resolve(oldRef),tFenceHurtCount-beforeCount,
            beforeHP-GetEntProp(entity,Prop_Data,"m_iHealth"),expected,gEpoch,uid);
        Check(!gBuildings[b].alive && gBuildings[b].hp==0 && gBuildings[b].ref==INVALID_ENT_REFERENCE && tFenceHurtCount==beforeCount+1
            && beforeHP-GetEntProp(entity,Prop_Data,"m_iHealth")==expected,"a lethal NPC strike consumes its logical fence and still reflects exactly once from the captured attack");
        Check(tFenceHurtReward.sourceUID==uid && tFenceHurtReward.sourceRef==oldRef && tFenceHurtReward.buildCore==0,"lethal retaliation retains the original destroyed fence identity and base after logical removal");
        // SourceMod's next-frame callback can precede the engine deletion queue.
        // Poll only the captured reference, independently of restored/reused fixture slots.
        DataPack removal;CreateDataTimer(0.1,FenceRetaliationRemovedPoll,removal,TIMER_REPEAT|TIMER_FLAG_NO_MAPCHANGE);
        removal.WriteCell(oldRef);removal.WriteCell(gEpoch);removal.WriteCell(uid);removal.WriteFloat(GetGameTime());removal.WriteCell(0);
        beforeCount=tFenceHurtCount;DamageBuilding(b,1,true,target);
        Check(tFenceHurtCount==beforeCount,"an already consumed destroyed fence cannot reflect a repeated building-damage call");
        PrintToServer("[FenceRetaliation] native_ref=%d owner_serial=%d real_hurt_callbacks=%d resolved_damage=%d lethal_reflection=1",
            gNPCs[target].ref,GetClientSerial(client),tFenceHurtCount,expected);
    }
    tFenceObserve=false;tFenceObserveNPCRef=INVALID_ENT_REFERENCE;
    // The target remains alive: restore its native state/HP without spawning or reviving.
    gNPCs[target]=savedNPC;SetEntProp(entity,Prop_Data,"m_iHealth",health);TeleportEntity(entity,npcPosition,npcAngles,view_as<float>({0.0,0.0,0.0}));
    if(savedNPC.paused){FreezeNPC(target);Colors(target,false);SetEntProp(entity,Prop_Data,"m_takedamage",0,1);SetEntProp(entity,Prop_Send,"m_usSolidFlags",savedNPC.solidFlags|4);}
    else RestoreNPCBody(target);
    SetEntDataEnt2(entity,gLeaderOffset,leader);
    RemoveBuildingEffect(b);int ref=gBuildings[b].ref;gBuildings[b].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);gBuildings[b]=prior;
    V05RealCleanup(client);PrintToServer("[FenceRetaliation] restored native NPC HP/state, temporary fence slot and real client fixture; no NPC/client created or revived");
}
