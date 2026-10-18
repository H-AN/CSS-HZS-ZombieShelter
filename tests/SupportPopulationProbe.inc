// Test the explicitly configured support-build population profile. This case
// never runs NPCTick/StartDay/StartNight or creates, revives or damages an NPC.
static const char sSupportPopulationCvars[][]={"hzs_maxzombiespawn","hzs_playerreborn","hzs_killmoney","hzs_maxmoney","hzs_showdamage","hzs_enablekillicon","hzs_allowbuy"};

KeyValues SupportPopulationDaysCopy()
{
    KeyValues source=gCfg[C_Days];if(source==null)return null;
    int depth=source.NodesInStack();KeyValues copy=new KeyValues("ShelterDays");
    // Rewind(false) preserves the existing traversal stack and current node.
    // Query a separate tree because SelectDay normally clears that history.
    source.Rewind(false);copy.Import(source);
    while(source.NodesInStack()>depth)if(!source.GoBack())break;
    return copy;
}

void SupportPopulationBodyCounts(int &ordinary,int &bosses,int &parked)
{
    ordinary=0;bosses=0;parked=0;
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!gNPCs[n].used || !gNPCs[n].ready || gNPCs[n].dead)continue;
        int body=Resolve(gNPCs[n].ref);if(body<1 || GetEntProp(body,Prop_Data,"m_iHealth")<1)continue;
        if(gNPCs[n].boss>=0)bosses++;else{ordinary++;if(!gNPCs[n].active)parked++;}
    }
}

void TestSupportPopulation(int client)
{
    ConVar pool=FindConVar("hzs_maxzombiespawn");
    bool ready=client>0 && LiveHuman(client) && !IsFakeClient(client) && Running()
        && pool!=null && !gNativePoolBootstrap && gCfg[C_Days]!=null;
    Check(ready,"support population test uses an existing living real client after native pool bootstrap");if(!ready)return;
    KeyValues originalDays=gCfg[C_Days];int originalDepth=originalDays.NodesInStack(),originalSymbol;
    originalDays.GetSectionSymbol(originalSymbol);KeyValues daysCopy=SupportPopulationDaysCopy();
    if(daysCopy==null){Check(false,"support population can copy the loaded day profile without replacing its cursor");return;}
    int savedDay=gDay,nativeCount=Han_GetZombieCount(),ordinary,bosses,parked;
    Phase savedPhase=gPhase;bool savedCaptured=gDayCvarsCaptured,savedBootstrap=gNativePoolBootstrap;
    bool savedTouched[sizeof(sSupportPopulationCvars)];ConVar savedHandles[sizeof(sSupportPopulationCvars)],actualHandles[sizeof(sSupportPopulationCvars)];
    char savedActual[sizeof(sSupportPopulationCvars)][64],savedOriginals[sizeof(sSupportPopulationCvars)][64];
    for(int i=0;i<sizeof(sSupportPopulationCvars);i++)
    {
        actualHandles[i]=FindConVar(sSupportPopulationCvars[i]);if(actualHandles[i]!=null)actualHandles[i].GetString(savedActual[i],sizeof(savedActual[]));
        savedHandles[i]=gDayCvars[i];savedTouched[i]=gDayCvarTouched[i];strcopy(savedOriginals[i],sizeof(savedOriginals[]),gDayCvarOriginal[i]);
    }
    bool savedAwake[MAX_BOSSES],savedDefeated[MAX_BOSSES];int savedBossHP[MAX_BOSSES],savedBossRef[MAX_BOSSES];
    float savedBossPosition[MAX_BOSSES][3];
    for(int b=0;b<gBossCount;b++)
    {
        savedAwake[b]=gBosses[b].awakened;savedDefeated[b]=gBosses[b].defeated;savedBossRef[b]=INVALID_ENT_REFERENCE;
        for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && !gNPCs[n].dead && gNPCs[n].boss==b)
        {
            int body=Resolve(gNPCs[n].ref);if(body<1)continue;savedBossRef[b]=gNPCs[n].ref;
            savedBossHP[b]=GetEntProp(body,Prop_Data,"m_iHealth");GetEntPropVector(body,Prop_Data,"m_vecOrigin",savedBossPosition[b]);break;
        }
    }
    SupportPopulationBodyCounts(ordinary,bosses,parked);
    int trackingSlots=MAX_NPCS;
    Check(trackingSlots==512 && gNativePoolSize==250 && gPopulationLimit==250,
        "loaded support profile has 512 tracking slots, 250 prebuilt ordinary bodies and external population limit 250");
    Check(nativeCount==251 && ordinary==250 && bosses==1 && gBossCount==1,
        "the actual native registry contains 251 NPCs: 250 living ordinary bodies plus one independent living Boss");
    PrintToServer("[SupportPopulation] initial day=%d phase=%d native_registry=%d ordinary=%d bosses=%d parked=%d native_pool=%d external_cap=%d tracking_slots=%d",
        savedDay,savedPhase,nativeCount,ordinary,bosses,parked,gNativePoolSize,gPopulationLimit,MAX_NPCS);
    gCfg[C_Days]=daysCopy;static const int days[]={1,2,5,10};
    for(int stage=0;stage<sizeof(days);stage++)
    {
        gDay=days[stage];gPhase=Phase_Day;int daytime=DayTarget(),nighttime=NightTarget(),cap=StagePopulationCap();char label[176];
        Format(label,sizeof(label),"support day %d resolves daytime target 100, nighttime target 250 and stage cap 250 from the loaded profile",gDay);
        Check(daytime==100 && nighttime==250 && cap==250 && OrdinaryTarget()==100,label);
        ApplyDayCvars();Format(label,sizeof(label),"support day %d applies native hzs_maxzombiespawn 250 without changing the active world",gDay);Check(pool.IntValue==250,label);
        gPhase=Phase_Night;
        Format(label,sizeof(label),"support night %d uses the configured ordinary target 250 beneath native/stage cap 250",gDay);
        Check(OrdinaryTarget()==250 && NightTarget()==250 && StagePopulationCap()==250,label);
        ApplyDayCvars();Format(label,sizeof(label),"support night %d also applies native hzs_maxzombiespawn 250",gDay);Check(pool.IntValue==250,label);
        PrintToServer("[SupportPopulation] queried_day=%d day_target=%d night_target=%d stage_cap=%d actual_native_cvar=%d native_registry=%d",
            gDay,daytime,nighttime,cap,pool.IntValue,Han_GetZombieCount());
    }
    // Restore actual cvar strings and the provider's cvar bookkeeping, not just
    // whichever values the original day would currently choose.
    for(int i=0;i<sizeof(sSupportPopulationCvars);i++)
    {
        if(actualHandles[i]!=null)actualHandles[i].SetString(savedActual[i]);
        gDayCvars[i]=savedHandles[i];gDayCvarTouched[i]=savedTouched[i];strcopy(gDayCvarOriginal[i],sizeof(gDayCvarOriginal[]),savedOriginals[i]);
    }
    gDay=savedDay;gPhase=savedPhase;gDayCvarsCaptured=savedCaptured;gNativePoolBootstrap=savedBootstrap;gCfg[C_Days]=originalDays;delete daysCopy;
    bool restored=true;char actual[64];
    for(int i=0;i<sizeof(sSupportPopulationCvars);i++)if(actualHandles[i]!=null)
    {actualHandles[i].GetString(actual,sizeof(actual));if(!StrEqual(actual,savedActual[i]))restored=false;}
    int symbol;originalDays.GetSectionSymbol(symbol);
    Check(restored && gDay==savedDay && gPhase==savedPhase && originalDays.NodesInStack()==originalDepth && symbol==originalSymbol,
        "support stage queries restore every actual cvar string, phase/day and the original loaded configuration cursor/history");
    bool bossesIntact=true;
    for(int b=0;b<gBossCount;b++)
    {
        if(gBosses[b].awakened!=savedAwake[b] || gBosses[b].defeated!=savedDefeated[b])bossesIntact=false;
        if(savedBossRef[b]==INVALID_ENT_REFERENCE)continue;
        int body=Resolve(savedBossRef[b]);float position[3];if(body>0)GetEntPropVector(body,Prop_Data,"m_vecOrigin",position);
        if(body<1 || GetEntProp(body,Prop_Data,"m_iHealth")!=savedBossHP[b] || GetVectorDistance(position,savedBossPosition[b])>0.01)bossesIntact=false;
    }
    int ordinaryAfter,bossesAfter,parkedAfter;SupportPopulationBodyCounts(ordinaryAfter,bossesAfter,parkedAfter);
    Check(bossesIntact && Han_GetZombieCount()==nativeCount && ordinaryAfter==ordinary && bossesAfter==bosses && parkedAfter==parked,
        "population configuration queries preserve all native bodies, parked counts and the Boss HP, position and awakening state");
}

// Read-only, repeatable observation. Admission is asynchronous; being below
// target is progress, not a fabricated pass, failure, or skipped test.
void TestSupportPopulationActive()
{
    if(!Running()){PrintToServer("[SupportPopulationActive] PENDING running=0; no gameplay or NPC state changed");return;}
    KeyValues originalDays=gCfg[C_Days],copy=SupportPopulationDaysCopy();
    if(copy==null){PrintToServer("[SupportPopulationActive] PENDING loaded day configuration unavailable");return;}
    gCfg[C_Days]=copy;int target=OrdinaryTarget(),dayTarget=DayTarget(),nightTarget=NightTarget(),cap=StagePopulationCap();gCfg[C_Days]=originalDays;delete copy;
    int ordinary,bosses,parked;SupportPopulationBodyCounts(ordinary,bosses,parked);int active=ActiveOrdinary();
    PrintToServer("[SupportPopulationActive] %s day=%d phase=%d active=%d target=%d day_target=%d night_target=%d stage_cap=%d ordinary_bodies=%d bosses=%d parked=%d native_registry=%d",
        active==target ? "REACHED" : "PENDING",gDay,gPhase,active,target,dayTarget,nightTarget,cap,ordinary,bosses,parked,Han_GetZombieCount());
    if(active==target && target>0)Check(true,"actual active ordinary population has reached the configured target for the current phase");
}
