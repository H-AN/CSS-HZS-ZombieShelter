// Test-only map identities, existing NPC hulls and the real fallback path.
static const char sFollowupSpawns[][]={"hzsh_t_003","hzsh_t_004","hzsh_t_005","hzsh_t_006","hzsh_t_007","hzsh_t_008"};
static const float sFollowupPoints[6][3]={{2176.0,-2528.0,36.031},{2048.0,-2528.0,36.031},{1920.0,-2528.0,36.031},
    {2176.0,-2400.0,36.031},{2048.0,-2400.0,36.031},{1792.0,-2528.0,36.031}};

int FollowupSpawn(const char[] name)
{
    int entity=-1,found=-1;
    while((entity=FindEntityByClassname(entity,"info_player_terrorist"))!=-1)
    {char actual[64];GetEntPropString(entity,Prop_Data,"m_iName",actual,sizeof(actual));if(StrEqual(name,actual)){if(found>=0)return -1;found=entity;}}
    return found;
}

bool FollowupBrush(const char[] model)
{
    int entity=-1;while((entity=FindEntityByClassname(entity,"func_brush"))!=-1)
    {char actual[64];GetEntPropString(entity,Prop_Data,"m_ModelName",actual,sizeof(actual));if(StrEqual(model,actual))return true;}
    return false;
}

void TestNPCFollowupLogic()
{
    Check(!FollowupBrush("*3") && !FollowupBrush("*7") && FollowupBrush("*4"),"only the two requested infection-vine brushes are absent while the mixed structural brush remains");
    for(int p=0;p<6;p++)
    {
        int entity=FollowupSpawn(sFollowupSpawns[p]);float actual[3];if(entity>0)GetEntPropVector(entity,Prop_Data,"m_vecOrigin",actual);
        char label[160];Format(label,sizeof(label),"existing spawn %s matches its explicit outside-room coordinates",sFollowupSpawns[p]);
        Check(entity>0 && GetVectorDistance(actual,sFollowupPoints[p])<0.1,label);
    }
    int real;for(int client=1;client<=MaxClients;client++)if(LiveHuman(client) && !IsFakeClient(client) && !IsClientSourceTV(client) && !IsClientReplay(client))real++;
    int base=gCoreCount>0 ? gCores[0].building : -1,entity=base>=0 ? Resolve(gBuildings[base].ref) : -1;
    if(real==0 && entity>0)Check(NearestFallbackPlayer(entity)==0,"no living real player means no fallback target or fabricated client");
    PrintToServer("[NPCFollowup] removed_this_round=%d moved_this_round=%d (brush identities checked independently of counters)",gMapRemovedVines,gMapMovedSpawns);
}

bool FollowupSpawnExit(int entity,const float position[3],float yaw,float &exitYaw,float &floorZ)
{
    float mins[3],maxs[3];NPCBounds(entity,mins,maxs);
    if(!ClearHull(position,mins,maxs,entity,MASK_NPCSOLID))return false;
    float down[3],ground[3];down=position;down[2]-=128.0;
    Handle fall=TR_TraceHullFilterEx(position,down,mins,maxs,MASK_NPCSOLID,FilterExcept,entity);
    bool landed=TR_DidHit(fall) && !TR_StartSolid(fall) && !TR_AllSolid(fall);
    if(landed){float normal[3];TR_GetPlaneNormal(fall,normal);TR_GetEndPosition(ground,fall);landed=normal[2]>=0.5;}
    delete fall;if(!landed)return false;floorZ=ground[2];ground[2]+=1.0;
    // Prefer the configured view direction, then inspect the other open sectors.
    for(int turn=0;turn<8;turn++)
    {
        float direction=yaw+float(turn)*45.0,end[3];end=ground;
        end[0]+=Cosine(DegToRad(direction))*128.0;end[1]+=Sine(DegToRad(direction))*128.0;
        Handle walk=TR_TraceHullFilterEx(ground,end,mins,maxs,MASK_NPCSOLID,FilterExcept,entity);
        bool clear=!TR_DidHit(walk) && !TR_StartSolid(walk) && !TR_AllSolid(walk);delete walk;
        if(clear){exitYaw=direction;return true;}
    }
    return false;
}

void TestNPCFollowupReal(int client)
{
    if(client<1 || !LiveHuman(client) || IsFakeClient(client) || !Running() || tV05RealActive)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP NPC followup requires a running campaign and existing real client without another fixture");return;}
    int n=-1;for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && gNPCs[i].ready && !gNPCs[i].dead && gNPCs[i].boss<0 && Resolve(gNPCs[i].ref)>0){n=i;break;}
    if(n<0 || !V05BeginRealFixture(client))
    {tSkipped++;PrintToServer("[ShelterTest] SKIP NPC followup: no existing ordinary native NPC; none created or respawned");return;}
    int entity=Resolve(gNPCs[n].ref),leader=GetEntDataEnt2(entity,gLeaderOffset),assignments=gFallbackAssignments;
    NPC saved;saved=gNPCs[n];float savedFallback=gNextTargetFallback[n];
    for(int p=0;p<6;p++)
    {
        int spawn=FollowupSpawn(sFollowupSpawns[p]);float position[3],angles[3],yaw,floorZ;
        if(spawn>0){GetEntPropVector(spawn,Prop_Data,"m_vecOrigin",position);GetEntPropVector(spawn,Prop_Data,"m_angRotation",angles);}
        bool usable=spawn>0 && FollowupSpawnExit(entity,position,angles[1],yaw,floorZ);
        char label[180];Format(label,sizeof(label),"native NPC hull fits, lands and has a clear 128-unit exit at outside spawn %s",sFollowupSpawns[p]);Check(usable,label);
        PrintToServer("[NPCFollowupSpawn] name=%s hull_ok=%d origin=%.3f %.3f %.3f floor_z=%.3f exit_yaw=%.1f",sFollowupSpawns[p],usable,position[0],position[1],position[2],floorZ,yaw);
    }
    RestoreNPCBody(n);gNPCs[n].active=true;gNPCs[n].attackingUID=0;gNPCs[n].targetClientSerial=0;gNPCs[n].burnUntil=0.0;
    SetEntDataEnt2(entity,gLeaderOffset,entity);gNextTargetFallback[n]=0.0;
    int nearest=NearestFallbackPlayer(entity);float now=GetGameTime();NPCTick(now);
    Check(nearest>0 && GetEntDataEnt2(entity,gLeaderOffset)==nearest && gNPCs[n].targetClientSerial==GetClientSerial(nearest)
        && gFallbackAssignments==assignments+1 && FloatAbs(gNextTargetFallback[n]-(now+0.5))<0.001,
        "an active native NPC with a self leader gets the nearest real player and a 0.5-second retry deadline");
    float npcPoint[3],clientPoint[3];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",npcPoint);if(nearest>0)GetClientAbsOrigin(nearest,clientPoint);
    Check(nearest>0 && GetVectorDistance(npcPoint,clientPoint)>4000.0,"fallback selection reaches a real player beyond 4000 units without claiming an engine follow-distance patch");
    int after=gFallbackAssignments;SetEntDataEnt2(entity,gLeaderOffset,entity);NPCTick(now+0.1);
    Check(gFallbackAssignments==after && GetEntDataEnt2(entity,gLeaderOffset)==entity,"a missing target inside the 0.5-second throttle does not receive repeated short locks");
    SetEntDataEnt2(entity,gLeaderOffset,client);gNextTargetFallback[n]=0.0;NPCTick(now+0.2);
    Check(gFallbackAssignments==after && GetEntDataEnt2(entity,gLeaderOffset)==client,"an existing valid native player leader remains unchanged by fallback");
    gNPCs[n].targetClientSerial=GetClientSerial(client);gNextTargetFallback[n]=now+10.0;
    ClearDisconnectedNPCTarget(n,client,GetClientSerial(client));
    Check(gNPCs[n].targetClientSerial==0 && GetEntDataEnt2(entity,gLeaderOffset)==entity && gNextTargetFallback[n]==0.0,
        "the production disconnect-target helper clears the serial, native lock and retry deadline without disconnecting or clearing inventory");
    ParkNPC(n);gNextTargetFallback[n]=0.0;NPCTick(now+0.3);
    Check(gFallbackAssignments==after && GetEntDataEnt2(entity,gLeaderOffset)==entity,"a parked native NPC keeps its self lock and receives no real-player fallback");
    bool dormantChecked;int bossAssignments;
    for(int boss=0;boss<MAX_NPCS;boss++)if(gNPCs[boss].used && gNPCs[boss].ready && !gNPCs[boss].dead && gNPCs[boss].boss>=0
        && !gBosses[gNPCs[boss].boss].awakened && Resolve(gNPCs[boss].ref)>0)
    {
        int body=Resolve(gNPCs[boss].ref),bossIndex=gNPCs[boss].boss;
        if(GetEntProp(body,Prop_Data,"m_iHealth")<=0)continue;
        NPC savedBossNPC;savedBossNPC=gNPCs[boss];BossDef savedBoss;savedBoss=gBosses[bossIndex];
        int bossLeader=GetEntDataEnt2(body,gLeaderOffset),bossHP=GetEntProp(body,Prop_Data,"m_iHealth");
        int bossDamage=GetEntProp(body,Prop_Data,"m_takedamage"),bossSolid=GetEntProp(body,Prop_Send,"m_usSolidFlags"),bossCounter=gFallbackAssignments;
        float bossFallback=gNextTargetFallback[boss],bossPosition[3],bossAngles[3],bossVelocity[3];
        int bossColor[4],visualColor[4],visual=Resolve(gNPCs[boss].visualRef);MoveType bossMove=GetEntityMoveType(body);
        GetEntPropVector(body,Prop_Data,"m_vecOrigin",bossPosition);GetEntPropVector(body,Prop_Data,"m_angRotation",bossAngles);
        GetEntPropVector(body,Prop_Data,"m_vecAbsVelocity",bossVelocity);GetEntityRenderColor(body,bossColor[0],bossColor[1],bossColor[2],bossColor[3]);
        if(visual>0)GetEntityRenderColor(visual,visualColor[0],visualColor[1],visualColor[2],visualColor[3]);
        NPCTick(now+0.4);
        bool stayedDormant=gFallbackAssignments==after && GetEntDataEnt2(body,gLeaderOffset)==body;
        Check(stayedDormant,"an existing dormant Boss stays self-locked instead of acquiring the fallback player");dormantChecked=true;
        if(stayedDormant)
        {
            // Reuse this same living native body. Explicitly switch only the
            // fixture's awakening state; no damage, creation or revival occurs.
            gBosses[bossIndex].awakened=true;RestoreNPCBody(boss);gNPCs[boss].active=true;
            // This is a target-selection assertion. A nearby wall must not
            // receive a fixture attack or retaliate against the existing Boss.
            gBosses[bossIndex].damage=0.0;SetEntProp(body,Prop_Data,"m_takedamage",0,1);
            gNPCs[boss].attackingUID=0;gNPCs[boss].targetClientSerial=0;gNPCs[boss].burnUntil=0.0;
            SetEntDataEnt2(body,gLeaderOffset,body);gNextTargetFallback[boss]=0.0;
            int bossNearest=NearestFallbackPlayer(body),beforeAwake=gFallbackAssignments;float awakeNow=now+0.5;
            NPCTick(awakeNow);bossAssignments=gFallbackAssignments-beforeAwake;
            Check(bossNearest>0 && GetEntDataEnt2(body,gLeaderOffset)==bossNearest
                && gNPCs[boss].targetClientSerial==GetClientSerial(bossNearest) && bossAssignments==1
                && FloatAbs(gNextTargetFallback[boss]-(awakeNow+0.5))<0.001,
                "the same existing living Boss, once awakened and active without a target, acquires the nearest real player through the production fallback path");
            PrintToServer("[NPCFollowupBoss] native_ref=%d awakened_fixture=1 nearest=%d leader=%d fallback_delta=%d hp=%d",
                savedBossNPC.ref,bossNearest,GetEntDataEnt2(body,gLeaderOffset),bossAssignments,GetEntProp(body,Prop_Data,"m_iHealth"));
        }
        // Undo the native short lock before reinstating the original dormant
        // freeze/paused state and every saved body property and plugin field.
        Han_UnlockZombie(body);gBosses[bossIndex]=savedBoss;gNPCs[boss]=savedBossNPC;
        if(savedBossNPC.paused || !savedBoss.awakened)FreezeNPC(boss);else RestoreNPCBody(boss);
        TeleportEntity(body,bossPosition,bossAngles,bossVelocity);SetEntityMoveType(body,bossMove);
        SetEntProp(body,Prop_Data,"m_iHealth",bossHP);SetEntProp(body,Prop_Data,"m_takedamage",bossDamage,1);
        SetEntProp(body,Prop_Send,"m_usSolidFlags",bossSolid);SetEntityRenderColor(body,bossColor[0],bossColor[1],bossColor[2],bossColor[3]);
        if(visual>0)SetEntityRenderColor(visual,visualColor[0],visualColor[1],visualColor[2],visualColor[3]);
        SetEntDataEnt2(body,gLeaderOffset,bossLeader);gNPCs[boss]=savedBossNPC;gNextTargetFallback[boss]=bossFallback;gFallbackAssignments=bossCounter;
        float restoredPosition[3];GetEntPropVector(body,Prop_Data,"m_vecOrigin",restoredPosition);
        Check(gBosses[bossIndex].awakened==savedBoss.awakened && gBosses[bossIndex].savedHP==savedBoss.savedHP
            && gNPCs[boss].active==savedBossNPC.active && gNPCs[boss].paused==savedBossNPC.paused && gNPCs[boss].lastHP==savedBossNPC.lastHP
            && gNPCs[boss].targetClientSerial==savedBossNPC.targetClientSerial && GetEntDataEnt2(body,gLeaderOffset)==bossLeader
            && GetEntProp(body,Prop_Data,"m_iHealth")==bossHP && GetVectorDistance(restoredPosition,bossPosition)<0.01
            && GetEntityMoveType(body)==bossMove && gNextTargetFallback[boss]==bossFallback && gFallbackAssignments==bossCounter,
            "the Boss fallback fixture restores awakening, native leader, NPC state, HP, position, paused movement, retry deadline and assignment counter");
        break;
    }
    if(!dormantChecked){tSkipped++;PrintToServer("[ShelterTest] SKIP dormant Boss fallback check: no existing dormant Boss");}
    gNPCs[n]=saved;gNextTargetFallback[n]=savedFallback;
    if(saved.paused){FreezeNPC(n);Colors(n,false);SetEntProp(entity,Prop_Data,"m_takedamage",0,1);SetEntProp(entity,Prop_Send,"m_usSolidFlags",saved.solidFlags|4);}
    else RestoreNPCBody(n);SetEntDataEnt2(entity,gLeaderOffset,leader);
    PrintToServer("[NPCFollowup] actual_fallback_assignments=%d awakened_boss_assignments=%d short_lock_seconds=0.75 retry_seconds=0.5 native_ref=%d",gFallbackAssignments-assignments+bossAssignments,bossAssignments,gNPCs[n].ref);
    gFallbackAssignments=assignments;
    V05RealCleanup(client);
}
