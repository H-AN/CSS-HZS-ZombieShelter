// Diagnostics only: death/round observation controls and interception of the
// known unsupported local changelevel path. Never include in a formal build.
bool tV082RouteLethalTest,tV082RouteMapIntercepted;

bool V082RouteAllowsLethal(){return tV082RouteLethalTest;}

void V082RouteRecordBossDeath(int zombie,int killer)
{
    int n=FindNPC(zombie);if(n<0 || gNPCs[n].boss<0)return;
    int b=gNPCs[n].boss;
    char row[768];Format(row,sizeof(row),"{\"kind\":\"native_boss_death_forward\",\"time\":%d,\"epoch\":%d,\"boss_id\":\"%s\",\"body_ref\":%d,\"killer_client\":%d,\"killer_userid\":%d,\"real_human_killer\":%d,\"previously_settled\":%d,\"active_before_death\":%d,\"phase\":%d,\"lethal_test\":%d}",
        GetTime(),gEpoch,gBosses[b].id,gNPCs[n].ref,killer,killer>0 && killer<=MaxClients && IsClientInGame(killer) ? GetClientUserId(killer) : 0,
        killer>0 && killer<=MaxClients && IsClientInGame(killer) && !IsFakeClient(killer),gNPCs[n].settled,gNPCs[n].active,gPhase,tV082RouteLethalTest);V082BPLog(row);
}

bool V082RouteInterceptMapChange()
{
    if(!tV082RouteMapIntercepted)
    {
        tV082RouteMapIntercepted=true;
        char row[512];Format(row,sizeof(row),"{\"kind\":\"map_change_intercepted\",\"time\":%d,\"epoch\":%d,\"terminal\":%d,\"victory_reason\":%d,\"all_bosses_defeated\":%d,\"diagnostic\":true,\"changelevel_sent\":false}",GetTime(),gEpoch,gTerminal,gVictoryReason,AllBossesDefeated());V082BPLog(row);
    }
    gDeadline=GetGameTime()+30.0;
    return true;
}

void V082RouteGoalState()
{
    int defeated,awake,bound;
    for(int i=0;i<gBossCount;i++){if(gBosses[i].defeated)defeated++;if(gBosses[i].awakened)awake++;if(gBosses[i].boundThisRound)bound++;}
    char row[768];Format(row,sizeof(row),"{\"kind\":\"goal_state\",\"time\":%d,\"epoch\":%d,\"phase\":%d,\"day\":%d,\"bosses\":%d,\"defeated\":%d,\"awake\":%d,\"bound\":%d,\"all_bosses_defeated\":%d,\"victory_queued\":%d,\"terminal\":%d,\"lethal_test\":%d,\"map_change_intercepted\":%d}",
        GetTime(),gEpoch,gPhase,gDay,gBossCount,defeated,awake,bound,AllBossesDefeated(),gVictoryCandidate,gTerminal,tV082RouteLethalTest,tV082RouteMapIntercepted);V082BPLog(row);
}

public Action V082RouteControlCommand(int args)
{
    char action[48];if(args>0)GetCmdArg(1,action,sizeof(action));
    if(args==0 || StrEqual(action,"state")){V082RouteGoalState();return Plugin_Handled;}
    int actor=GetClientFromSerial(tV082BPSerial);
    if(!V082BPSession(actor)){V082BPEvent("rejected","route_control_requires_active_exact_fixture");return Plugin_Handled;}
    if(StrEqual(action,"release_for_round") && args==1)
    {
        if(tV082BPStage!=V082BP_AwaitReturn || !tV082BPHitObserved || !V082BPCurrentBoss()
            || !gBosses[tV082BPBossIndex].awakened)
        {V082BPEvent("rejected","round_test_requires_actual_hit_and_awakened_observed_first");return Plugin_Handled;}
        // Intentionally retain this round's actually awakened Boss. The player
        // will die through real client input; new-round production resets it.
        float now=GetGameTime();gDeadline=now+tV082BPDeadlineRemaining;gNextAdmissions=now+tV082BPAdmissionsRemaining;
        gNextExposure=now+tV082BPExposureRemaining;gExposureEnabled=tV082BPExposure;
        SetEntProp(actor,Prop_Data,"m_takedamage",tV082BPSavedDamage,1);
        SetEntityFlags(actor,(GetEntityFlags(actor)&~FL_GODMODE)|tV082BPSavedGod);SetEntityMoveType(actor,tV082BPPlayerMove);
        V082BPEvent("round_sleep_test_released","protection_and_timers_restored_without_reverting_actual_awaken_state_operator_must_use_real_client_death_no_NPC_respawn_command");
        tV082BPActive=false;tV082BPStage=V082BP_None;tV082RouteLethalTest=false;V082RouteGoalState();
        return Plugin_Handled;
    }
    if(StrEqual(action,"lethal") && args==2)
    {
        char id[64];GetCmdArg(2,id,sizeof(id));
        if(tV082BPStage!=V082BP_AwaitHit || !V082BPCurrentBoss() || !StrEqual(id,gBosses[tV082BPBossIndex].id))
        {V082BPEvent("rejected","lethal_gate_requires_prepared_exact_existing_living_boss");return Plugin_Handled;}
        int entity=Resolve(tV082BPRef);
        if(GetEntProp(entity,Prop_Data,"m_iHealth")<=1 || gBosses[tV082BPBossIndex].defeated)
        {V082BPEvent("rejected","lethal_gate_refuses_dead_or_missing_boss");return Plugin_Handled;}
        // Lower HP only; the real player's subsequent shot must cause the death.
        SetEntProp(entity,Prop_Data,"m_iHealth",1);gNPCs[tV082BPNpc].lastHP=1;gBosses[tV082BPBossIndex].savedHP=1;
        tV082BPStage=V082BP_None;tV082RouteLethalTest=true;
        V082BPEvent("victory_test_hp_prepared","existing_living_boss_HP_lowered_to_1_for_real_client_death_hook_and_all_Boss_gate_test_no_damage_or_death_command_sent");
        V082RouteGoalState();return Plugin_Handled;
    }
    PrintToServer("DIAGNOSTIC: sm_v082_routes state|release_for_round|lethal <exact_prepared_boss_id>");
    return Plugin_Handled;
}

void RegisterV082BossRoutesControl()
{
    RegServerCmd("sm_v082_routes",V082RouteControlCommand,"Diagnostic round/death observation, no NPC creation or respawn");
}
