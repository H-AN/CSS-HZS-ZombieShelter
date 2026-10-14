// Tests configuration, native cvars and the real startup forwarding contract.
// This case does not create/kill NPCs, change their movement or finish a round.
void TestDayPopulation(int client)
{
    static const char names[][]={"hzs_maxzombiespawn","hzs_playerreborn","hzs_killmoney","hzs_maxmoney","hzs_showdamage","hzs_enablekillicon","hzs_allowbuy"};
    ConVar pool=FindConVar("hzs_maxzombiespawn");
    Check(LiveHuman(client) && Running() && pool!=null && !gNativePoolBootstrap,"day cvar fixture is running after full native pool bootstrap");
    if(!LiveHuman(client) || !Running() || pool==null || gNativePoolBootstrap)return;

    int savedDay=gDay,savedOriginalPool=gOriginalPool,nativeCount=Han_GetZombieCount();
    Phase savedPhase=gPhase;bool savedPoolOverridden=gPoolOverridden,savedCaptured=gDayCvarsCaptured;
    bool savedBootstrap=gNativePoolBootstrap,savedTouched[sizeof(names)];
    ConVar savedPoolHandle=gCorePoolCvar,savedHandles[sizeof(names)];
    char savedOriginals[sizeof(names)][64],savedActual[sizeof(names)][64];
    for(int i=0;i<sizeof(names);i++)
    {
        savedHandles[i]=gDayCvars[i];savedTouched[i]=gDayCvarTouched[i];
        strcopy(savedOriginals[i],sizeof(savedOriginals[]),gDayCvarOriginal[i]);
        ConVar cvar=FindConVar(names[i]);if(cvar!=null)cvar.GetString(savedActual[i],sizeof(savedActual[]));
    }
    int bossRefs[MAX_BOSSES],bossHP[MAX_BOSSES];bool bossActive[MAX_BOSSES];
    for(int b=0;b<gBossCount;b++)
    {
        bossRefs[b]=INVALID_ENT_REFERENCE;
        for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].boss==b && !gNPCs[n].dead)
        {
            int entity=Resolve(gNPCs[n].ref);if(entity<0)continue;
            bossRefs[b]=gNPCs[n].ref;bossHP[b]=GetEntProp(entity,Prop_Data,"m_iHealth");bossActive[b]=gNPCs[n].active;break;
        }
    }

    Check(gNativePoolSize==100 && gPopulationLimit==100,"default ordinary pool and external population ceiling both support day-ten 100");
    Check(nativeCount==gNativePoolSize+gBossCount,"real core has the complete future ordinary pool plus independent fixed bosses");
    static const int days[]={1,2,5,10};
    static const int night[]={50,80,90,100};
    static const int day[]={8,10,12,12};
    for(int stage=0;stage<sizeof(days);stage++)
    {
        gDay=days[stage];gPhase=Phase_Day;ApplyDayCvars();
        char message[160];Format(message,sizeof(message),"day %d applies native hzs_maxzombiespawn %d",gDay,night[stage]);
        Check(pool.IntValue==night[stage],message);
        Format(message,sizeof(message),"day %d retains daytime target %d beneath its stage cap",gDay,day[stage]);
        Check(OrdinaryTarget()==day[stage],message);
        gPhase=Phase_Night;ApplyDayCvars();
        Format(message,sizeof(message),"night %d external admission target and native cvar both equal %d",gDay,night[stage]);
        Check(OrdinaryTarget()==night[stage] && pool.IntValue==night[stage],message);
    }
    Check(Han_GetZombieCount()==nativeCount,"changing all four stage cvars neither spawns extra native NPCs nor deletes the parked pool");
    gDay=9;ApplyDayCvars();
    Check(pool.IntValue==90 && OrdinaryTarget()==90,"rolling back across day-ten boundary reapplies the prior 90 cap");
    gDay=1;ApplyDayCvars();ApplyDayCvars();
    Check(pool.IntValue==50 && OrdinaryTarget()==50,"resetting or reapplying the same day restores its 50 cap without sticky later values");

    OverrideCorePool();gPhase=Phase_Day;ApplyDayCvars();
    Check(gNativePoolBootstrap && pool.IntValue==gNativePoolSize && DayTarget()==8,"synchronous Han_OnGameStart keeps startup cvar 100 despite day-one admission target eight");
    Check(gOriginalPool==savedOriginalPool,"round bootstrap never replaces the pre-mode native cvar snapshot");
    FinishNativePoolBootstrap(gEpoch-1);
    Check(gNativePoolBootstrap && pool.IntValue==gNativePoolSize,"previous-round bootstrap callback cannot lower the new round native pool");
    FinishNativePoolBootstrap(gEpoch);
    Check(!gNativePoolBootstrap && pool.IntValue==50,"post-native-spawn bootstrap callback applies the actual day-one cvar 50");

    Check(DayCvarIndex("hzs_enableplugins")==-1 && DayCvarIndex("hzs_winendround")==-1 && DayCvarIndex("quit")==-1,"stage cvar whitelist excludes plugin lifecycle, round outcome and server commands");
    Check(!ValidDayCvarValue(0,"50; quit") && !ValidDayCvarValue(0,"50abc") && !ValidDayCvarValue(0,"2147483648"),"stage cvar parsing rejects injection, partial numbers and signed-cell overflow");
    Check(!ValidDayCvarValue(0,"101") && !ValidDayCvarValue(0,"0") && ValidDayCvarValue(0,"100"),"stage ordinary cap is positive and cannot exceed the prebuilt native pool");
    Check(ValidDayCvarValue(1,"-1") && ValidDayCvarValue(1,"0.0") && ValidDayCvarValue(1,"7.5")
        && !ValidDayCvarValue(1,"0.5") && !ValidDayCvarValue(1,"nan") && !ValidDayCvarValue(1,"1e99"),"respawn values follow native sentinel and countdown rules and reject non-finite input");

    KeyValues fixture=new KeyValues("ShelterDays");
    fixture.JumpToKey("Stages",true);fixture.JumpToKey("first",true);
    fixture.SetNum("from_day",1);fixture.SetNum("to_day",1);fixture.SetNum("day_target_alive",8);fixture.SetNum("night_target_alive",50);
    fixture.JumpToKey("Cvars",true);fixture.SetString("hzs_maxzombiespawn","50");fixture.SetString("hzs_killmoney","1234");fixture.SetString("hzs_playerreborn","7.5");
    fixture.GoBack();
    Check(ValidateStageCvars(fixture),"range validator accepts a complete supported integer and float cvar section");
    fixture.JumpToKey("Cvars");fixture.SetString("hzs_enableplugins","0");fixture.GoBack();
    Check(!ValidateStageCvars(fixture),"range validator rejects an unknown or lifecycle cvar before any side effect");
    fixture.JumpToKey("Cvars");fixture.DeleteKey("hzs_enableplugins");fixture.GoBack();fixture.GoBack();
    fixture.JumpToKey("second",true);fixture.SetNum("from_day",2);fixture.SetNum("to_day",0);
    fixture.SetNum("day_target_alive",8);fixture.SetNum("night_target_alive",50);
    fixture.JumpToKey("Cvars",true);fixture.SetString("hzs_maxzombiespawn","50");fixture.Rewind();
    KeyValues savedDays=gCfg[C_Days];gCfg[C_Days]=fixture;
    ConVar money=FindConVar("hzs_killmoney"),respawn=FindConVar("hzs_playerreborn");
    gDay=1;ApplyDayCvars();
    Check(money!=null && money.IntValue==1234 && respawn!=null && FloatAbs(respawn.FloatValue-7.5)<0.01,"optional stage cvars use native integer and float setters");
    gDay=2;ApplyDayCvars();
    char restored[64];if(money!=null)money.GetString(restored,sizeof(restored));
    bool optionalRestored=money!=null && StrEqual(restored,gDayCvarOriginal[2]);
    if(respawn!=null)respawn.GetString(restored,sizeof(restored));
    Check(optionalRestored && respawn!=null && StrEqual(restored,gDayCvarOriginal[1]),"omitting a previously overridden option restores its pre-mode value instead of inheriting the previous stage");
    Node(C_Days,"Stages/second/Cvars");fixture.DeleteKey("hzs_maxzombiespawn");ApplyDayCvars();
    Check(pool.IntValue==gPopulationLimit && StagePopulationCap()==gPopulationLimit,"a legacy stage without a max cvar retains its configured external global ceiling");
    Node(C_Days,"Stages/second/Cvars");fixture.SetString("hzs_maxzombiespawn","3");ApplyDayCvars();
    Check(pool.IntValue==3 && DayTarget()==3 && NightTarget()==3,"a stage native cap also bounds both external day and night ordinary admission targets");
    Node(C_Days,"Stages/second/Cvars");fixture.SetString("hzs_maxzombiespawn","100");
    Node(C_Days,"Stages/second");fixture.SetNum("extra_alive_per_day_after_from",100);fixture.SetNum("night_target_cap",100);
    gDay=2147483647;
    Check(NightTarget()==100,"an INT_MAX campaign day saturates growing ordinary target before integer multiplication can overflow");
    gCfg[C_Days]=savedDays;delete fixture;

    RestoreCorePool();
    Check(!gPoolOverridden && !gDayCvarsCaptured && !gNativePoolBootstrap && pool.IntValue==savedOriginalPool,"mode exit restores the true pre-mode native maximum and clears bootstrap ownership");
    if(money!=null)money.GetString(restored,sizeof(restored));
    optionalRestored=money!=null && StrEqual(restored,savedOriginals[2]);
    if(respawn!=null)respawn.GetString(restored,sizeof(restored));
    Check(optionalRestored && respawn!=null && StrEqual(restored,savedOriginals[1]),"mode exit restores all stage-modified gameplay cvars");
    RestoreCorePool();
    Check(pool.IntValue==savedOriginalPool && !gPoolOverridden,"duplicate exit restoration is idempotent and cannot write a stale stage value");

    // Restore the exact running mode, including snapshots and already-managed flags.
    for(int i=0;i<sizeof(names);i++)
    {
        ConVar cvar=FindConVar(names[i]);if(cvar!=null)cvar.SetString(savedActual[i]);
        gDayCvars[i]=savedHandles[i];gDayCvarTouched[i]=savedTouched[i];
        strcopy(gDayCvarOriginal[i],sizeof(gDayCvarOriginal[]),savedOriginals[i]);
    }
    gDay=savedDay;gPhase=savedPhase;gOriginalPool=savedOriginalPool;gCorePoolCvar=savedPoolHandle;
    gPoolOverridden=savedPoolOverridden;gDayCvarsCaptured=savedCaptured;gNativePoolBootstrap=savedBootstrap;
    bool bossesIntact=true;
    for(int b=0;b<gBossCount;b++)if(bossRefs[b]!=INVALID_ENT_REFERENCE)
    {
        int entity=Resolve(bossRefs[b]);int npc=entity>0 ? FindNPC(entity) : -1;
        if(npc<0 || gNPCs[npc].active!=bossActive[b] || GetEntProp(entity,Prop_Data,"m_iHealth")!=bossHP[b])bossesIntact=false;
    }
    Check(bossesIntact && Han_GetZombieCount()==nativeCount,"all stage and exit tests preserve fixed Boss bodies, active state, HP and the complete native registry");
}
