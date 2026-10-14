// Include after ShelterIntegrationProbe's production include. The parent probe
// provides Check(), TestHuman(), TestBuild() and the zsh_test command dispatcher.
// This scenario restores the live world's logical state and removes its bodies.
void TestProgression(int client)
{
    int boss=-1,ordinary=-1,recipe=-1;
    for(int npc=0;npc<MAX_NPCS;npc++)
    {
        if(!gNPCs[npc].used || !gNPCs[npc].ready || !gNPCs[npc].active || gNPCs[npc].dead)continue;
        if(gNPCs[npc].boss==0)boss=npc;
        else if(gNPCs[npc].boss<0 && ordinary<0)ordinary=npc;
    }
    for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Amplifier){recipe=r;break;}
    Check(Running() && boss>=0 && ordinary>=0 && recipe>=0,"progression fixture has a native boss, ordinary NPC and amplifier recipe");
    if(!Running() || boss<0 || ordinary<0 || recipe<0)return;

    bool enabled=gProgressionEnabled;gProgressionEnabled=true;
    int savedLevels[MAX_CORES];bool savedCoreAlive[MAX_CORES];
    for(int core=0;core<gCoreCount;core++)
    {
        int building=gCores[core].building;
        savedLevels[core]=gBuildings[building].level;savedCoreAlive[core]=gBuildings[building].alive;
        gBuildings[building].level=1;
    }
    // Isolate existing amplifiers without destroying or recreating them.
    bool savedAmplifierAlive[MAX_BUILDINGS];
    for(int building=0;building<MAX_BUILDINGS;building++)
    {
        savedAmplifierAlive[building]=gBuildings[building].alive;
        if(gBuildings[building].kind==Kind_Amplifier)gBuildings[building].alive=false;
    }
    int bossEntity=Resolve(gNPCs[boss].ref),ordinaryEntity=Resolve(gNPCs[ordinary].ref);
    int savedBossHP=GetEntProp(bossEntity,Prop_Data,"m_iHealth"),savedOrdinaryHP=GetEntProp(ordinaryEntity,Prop_Data,"m_iHealth");
    NPC savedBossNPC,savedOrdinaryNPC;BossDef savedBoss;
    savedBossNPC=gNPCs[boss];savedOrdinaryNPC=gNPCs[ordinary];savedBoss=gBosses[0];
    SetEntProp(bossEntity,Prop_Data,"m_iHealth",50000);SetEntProp(ordinaryEntity,Prop_Data,"m_iHealth",50000);
    gNPCs[boss].lastHP=50000;gNPCs[ordinary].lastHP=50000;gBosses[0].savedHP=50000;

    int amount=5000;ApplyProgressionDamage(boss,client,amount,false);
    Check(amount==1,"level-one player Boss damage is fixed at one even for a large raw hit");
    amount=5000;ApplyProgressionDamage(boss,client,amount,true);
    Check(amount==1,"level-one defense Boss damage uses its separate default one-damage lock");
    amount=40;ApplyProgressionDamage(ordinary,client,amount,false);
    Check(amount==40,"initial player damage against ordinary zombies remains unchanged");
    amount=0;ApplyProgressionDamage(boss,client,amount,false);
    Check(amount==0,"zero input damage never becomes one free Boss damage");
    int required;
    Check(!CanBuildAmplifier(gRecipes[recipe].type,1,required) && required==2,"amplifier construction is gated by the configured living base level");

    // Real engine damage crosses the actual core forward before HP is changed.
    int hp=GetEntProp(bossEntity,Prop_Data,"m_iHealth");
    SDKHooks_TakeDamage(bossEntity,client,client,5000.0,DMG_BULLET|DMG_NEVERGIB,_,_,_,false);
    Check(GetEntProp(bossEntity,Prop_Data,"m_iHealth")==hp-1,"actual SDK weapon damage reaches the core pre-forward and deducts exactly one Boss HP");
    hp=GetEntProp(bossEntity,Prop_Data,"m_iHealth");DamageNPC(boss,5000,GetClientSerial(client),true);
    Check(GetEntProp(bossEntity,Prop_Data,"m_iHealth")==hp-1,"actual safe-damage defense hit also honors the Boss lock exactly once");

    int primary=gCores[0].building;gBuildings[primary].level=2;
    Check(CanBuildAmplifier(gRecipes[recipe].type,1,required),"level-two living base unlocks amplifier construction for the whole team");
    Check(!CanBuildAmplifier(gRecipes[recipe].type,2,required) && required==3,"amplifier upgrade has an independent next-level base requirement");
    amount=100;ApplyProgressionDamage(boss,client,amount,false);
    Check(amount==25,"second base level begins Boss progression at the configured quarter damage");
    gBuildings[primary].level=4;
    amount=100;ApplyProgressionDamage(boss,client,amount,false);
    Check(amount==200,"fourth base level grants the configured two-times Boss damage before amplifiers");
    gBuildings[primary].alive=false;
    amount=100;ApplyProgressionDamage(boss,client,amount,false);
    Check(amount==1,"a destroyed highest-level base stops supplying its progression level");
    gBuildings[primary].alive=true;
    TestProgressionFormula(client,boss,primary);

    // Seven real entities exercise summed bonuses and both configured caps.
    int slots[7],created;Building previous[7];
    for(int number=0;number<sizeof(slots);number++)
    {
        int slot=-1;
        // Keep every pre-existing instance, including destroyed ones, intact.
        for(int building=gCoreCount;building<MAX_BUILDINGS;building++)
            if(!gBuildings[building].used){slot=building;break;}
        if(slot<0)break;
        slots[created]=slot;previous[created]=gBuildings[slot];
        Building amplifier;amplifier.used=true;amplifier.alive=true;amplifier.kind=Kind_Amplifier;
        amplifier.uid=++gUID;amplifier.core=-1;amplifier.file=C_Amplifiers;amplifier.level=number==0 ? 1 : 3;
        amplifier.ref=INVALID_ENT_REFERENCE;amplifier.origin=gBosses[0].origin;
        amplifier.origin[0]+=600.0+float(number)*80.0;amplifier.origin[1]+=500.0;
        strcopy(amplifier.type,sizeof(amplifier.type),gRecipes[recipe].type);
        char path[128];Format(path,sizeof(path),"Types/%s",amplifier.type);Text(C_Amplifiers,path,"model",amplifier.model,sizeof(amplifier.model));
        gBuildings[slot]=amplifier;created++;
        if(!ApplyBuildingLevel(slot,true) || !SpawnBuildingBody(slot))break;
        if(number==0)
        {
            Check(Resolve(gBuildings[slot].ref)>0,"amplifier stock model produces a valid destructible world entity");
            amount=100;ApplyProgressionDamage(boss,client,amount,false);
            Check(amount==225,"a distant first-tier amplifier adds 0.25 to the team Boss multiplier");
            gBuildings[slot].level=3;ApplyBuildingLevel(slot,true);
            amount=100;ApplyProgressionDamage(boss,client,amount,false);
            Check(amount==350,"upgrading replaces the prior amplifier tier instead of stacking old tiers");
            gBuildings[slot].alive=false;amount=100;ApplyProgressionDamage(boss,client,amount,false);
            Check(amount==200,"destroyed amplifiers immediately stop contributing");
            gBuildings[slot].alive=true;
            int ref=gBuildings[slot].ref;gBuildings[slot].ref=INVALID_ENT_REFERENCE;
            amount=100;ApplyProgressionDamage(boss,client,amount,false);
            Check(amount==200,"an invalid amplifier EntRef cannot grant phantom damage bonuses");
            gBuildings[slot].ref=ref;
        }
    }
    Check(created==7,"progression cap scenario created seven amplifier instances");
    float multiplier;int locked,count;
    GetProgressionStats(false,true,multiplier,locked,count);
    Check(count==7 && locked==-1 && FloatAbs(multiplier-8.0)<0.001,"Boss amplifier bonus sum and final multiplier are capped at the configured eight-times limit");
    GetProgressionStats(false,false,multiplier,locked,count);
    Check(FloatAbs(multiplier-3.5)<0.001,"ordinary channel has its own smaller amplifier and total caps");
    amount=100000;ApplyProgressionDamage(boss,client,amount,false);
    Check(amount==gProgressionDamageCap,"large progressed hits clamp before integer conversion to the configured final damage cap");

    // Deliberately unequal channels make source mistakes or double application
    // visible through actual core HP, rather than merely inspecting a flag.
    ProgressionRule oldPlayerBoss,oldDefenseBoss,oldDefenseOrdinary;
    oldPlayerBoss=gProgressionRules[1];oldDefenseBoss=gProgressionRules[3];oldDefenseOrdinary=gProgressionRules[2];
    float oldDefenseBossBase=Decimal(C_Progression,"Defense/Boss/BaseMultipliers","4");
    float oldDefenseOrdinaryBase=Decimal(C_Progression,"Defense/Ordinary/BaseMultipliers","4");
    gProgressionRules[1].maxAmplifierBonus=0.0;
    gProgressionRules[3].maxAmplifierBonus=0.0;Node(C_Progression,"Defense/Boss/BaseMultipliers");gCfg[C_Progression].SetFloat("4",3.0);
    gProgressionRules[2].maxAmplifierBonus=0.0;Node(C_Progression,"Defense/Ordinary/BaseMultipliers");gCfg[C_Progression].SetFloat("4",2.0);
    hp=GetEntProp(bossEntity,Prop_Data,"m_iHealth");DamageNPC(boss,20,GetClientSerial(client),true);
    Check(GetEntProp(bossEntity,Prop_Data,"m_iHealth")==hp-60,"scoped defense API applies the defense multiplier once, not the player channel or a second multiplier");
    hp=GetEntProp(bossEntity,Prop_Data,"m_iHealth");DamageNPC(boss,20,GetClientSerial(client));
    Check(GetEntProp(bossEntity,Prop_Data,"m_iHealth")==hp-40 && !gDamageIsDefense && gDamageNPCRef==INVALID_ENT_REFERENCE,"following player API hit restores its own channel and clears the nested damage context");
    float now=GetGameTime();gNPCs[ordinary].burnUntil=now+2.0;gNPCs[ordinary].nextBurn=0.0;
    gNPCs[ordinary].burnDamage=10;gNPCs[ordinary].burnInterval=0.5;gNPCs[ordinary].burnClientSerial=GetClientSerial(client);
    hp=GetEntProp(ordinaryEntity,Prop_Data,"m_iHealth");NPCTick(now);
    Check(GetEntProp(ordinaryEntity,Prop_Data,"m_iHealth")==hp-20,"scheduled flame burn retains defense attribution and applies progression only once");
    gProgressionRules[1]=oldPlayerBoss;gProgressionRules[3]=oldDefenseBoss;gProgressionRules[2]=oldDefenseOrdinary;
    Node(C_Progression,"Defense/Boss/BaseMultipliers");gCfg[C_Progression].SetFloat("4",oldDefenseBossBase);
    Node(C_Progression,"Defense/Ordinary/BaseMultipliers");gCfg[C_Progression].SetFloat("4",oldDefenseOrdinaryBase);

    for(int number=0;number<created;number++)
    {
        int slot=slots[number],ref=gBuildings[slot].ref;gBuildings[slot].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
        gBuildings[slot]=previous[number];
    }
    for(int building=0;building<MAX_BUILDINGS;building++)
        if(gBuildings[building].kind==Kind_Amplifier)gBuildings[building].alive=savedAmplifierAlive[building];
    for(int core=0;core<gCoreCount;core++)
    {
        int building=gCores[core].building;
        gBuildings[building].level=savedLevels[core];gBuildings[building].alive=savedCoreAlive[core];
    }
    gNPCs[boss]=savedBossNPC;gNPCs[ordinary]=savedOrdinaryNPC;gBosses[0]=savedBoss;
    SetEntProp(bossEntity,Prop_Data,"m_iHealth",savedBossHP);SetEntProp(ordinaryEntity,Prop_Data,"m_iHealth",savedOrdinaryHP);
    if(!gBosses[0].awakened)FreezeNPC(boss);
    gProgressionEnabled=enabled;
    Check(Resolve(gNPCs[boss].ref)>0 && !gNPCs[boss].dead,"progression scenario restores its living boss and cleans all temporary amplifiers");
}

KeyValues ProgressionProbeClone(KeyValues original,const char[] root)
{
    original.Rewind();int size=original.ExportLength+1;char[] text=new char[size];
    original.ExportToString(text,size);KeyValues clone=new KeyValues(root);
    if(!clone.ImportFromString(text)){delete clone;return null;}
    return clone;
}

// Real production readers, loader and damage path operate on disposable config
// handles. No live config files, building bodies, inventories or HP are changed.
void TestProgressionFormula(int client,int boss,int primary)
{
    KeyValues originalProgression=gCfg[C_Progression],originalAmplifiers=gCfg[C_Amplifiers];
    KeyValues formula=ProgressionProbeClone(originalProgression,"ShelterProgression");
    KeyValues amplifiers=ProgressionProbeClone(originalAmplifiers,"ShelterAmplifiers");
    Check(formula!=null && amplifiers!=null,"formula progression clones configuration without editing server files");
    if(formula==null || amplifiers==null){delete formula;delete amplifiers;return;}
    int originalLevel=gBuildings[primary].level;
    gCfg[C_Progression]=formula;gCfg[C_Amplifiers]=amplifiers;
    Node(C_Progression,"Rules");formula.SetNum("base_level_cap",100);
    float increments[]={0.05,0.10,0.02,0.03};
    static const char channels[][]={"Player/Ordinary","Player/Boss","Defense/Ordinary","Defense/Boss"};
    for(int channel=0;channel<PROGRESSION_CHANNELS;channel++)
    {
        Node(C_Progression,channels[channel]);formula.SetFloat("maximum_multiplier",100.0);
        formula.SetFloat("maximum_amplifier_bonus",0.0);formula.JumpToKey("Growth",true);
        formula.SetNum("anchor_level",4);formula.SetFloat("multiplier_per_level",increments[channel]);
        formula.SetFloat("multiplier_min",0.0);formula.SetFloat("multiplier_max",100.0);
    }
    Node(C_Progression,"Player/Boss/BaseMultipliers");formula.SetFloat("75",9.25);
    char type[64];strcopy(type,sizeof(type),gAmplifiers[0].type);
    char profile[128];Format(profile,sizeof(profile),"Types/%s",type);
    Node(C_Amplifiers,profile);amplifiers.JumpToKey("Growth",true);
    amplifiers.SetNum("max_level",100);amplifiers.SetNum("anchor_level",3);
    amplifiers.JumpToKey("Stats",true);amplifiers.SetNum("max_hp_per_level",100);
    amplifiers.SetNum("minimum_base_level_per_level",0);
    amplifiers.SetFloat("player_ordinary_bonus_per_level",0.01);
    amplifiers.SetFloat("player_boss_bonus_per_level",0.05);
    amplifiers.SetFloat("defense_ordinary_bonus_per_level",0.02);
    amplifiers.SetFloat("defense_boss_bonus_per_level",0.10);
    amplifiers.GoBack();amplifiers.JumpToKey("UpgradeCost",true);amplifiers.SetNum("iron_per_level",5);
    bool loaded=LoadProgression();Check(loaded,"real progression loader accepts level 100 from sparse anchor rows and Growth formulas");
    if(loaded)
    {
        float value;
        Check(ReadProgressionBaseMultiplier(0,100,value) && FloatAbs(value-6.3)<0.001,"level 100 player ordinary uses anchor 1.5 plus 96 times 0.05");
        Check(ReadProgressionBaseMultiplier(1,100,value) && FloatAbs(value-11.6)<0.001,"level 100 player Boss uses independent 0.10 growth");
        Check(ReadProgressionBaseMultiplier(2,100,value) && FloatAbs(value-3.42)<0.001,"level 100 defense ordinary uses independent 0.02 growth");
        Check(ReadProgressionBaseMultiplier(3,100,value) && FloatAbs(value-4.88)<0.001,"level 100 defense Boss uses independent 0.03 growth");
        Check(ReadProgressionBaseMultiplier(1,75,value) && FloatAbs(value-9.25)<0.001,"a sparse explicit level 75 multiplier overrides the Growth formula");
        Check(!ReadProgressionBaseMultiplier(1,0,value) && !ReadProgressionBaseMultiplier(1,101,value),"progression reader rejects zero and levels above its configured cap");
        gBuildings[primary].level=100;int amount=100;ApplyProgressionDamage(boss,client,amount,false);
        Check(amount==1160,"actual player damage path applies configured level 100 multiplier");
        amount=100;ApplyProgressionDamage(boss,client,amount,true);
        Check(amount==488,"actual defense damage path keeps its separate level 100 formula");
        gBuildings[primary].level=101;amount=100;ApplyProgressionDamage(boss,client,amount,false);
        Check(amount==1160,"a living base beyond the damage cap uses the last configured progression level");
        int required,hp,cost[MATERIALS];float bonuses[PROGRESSION_CHANNELS];
        Check(AmplifierLevelInfo(type,100,required,bonuses) && required==4
            && FloatAbs(bonuses[0]-1.32)<0.001 && FloatAbs(bonuses[1]-6.35)<0.001
            && FloatAbs(bonuses[2]-2.29)<0.001 && FloatAbs(bonuses[3]-11.2)<0.001,
            "level 100 amplifier evaluates all four bonuses without fixed arrays");
        Check(ReadLevelInt(C_Amplifiers,profile,100,"max_hp",0,hp) && hp==11300
            && ReadLevelUpgradeCost(C_Amplifiers,profile,100,cost) && cost[1]==545,
            "level 100 amplifier HP and upgrade cost share the generic level formula");
        Check(!AmplifierLevelInfo(type,0,required,bonuses) && !AmplifierLevelInfo(type,101,required,bonuses),
            "amplifier rejects zero and a level above its own Growth max");
        gBuildings[primary].level=3;
        Check(!CanBuildAmplifier(type,100,required) && required==4,"formula amplifier construction still requires a sufficient living base");
        gBuildings[primary].level=100;
        Check(CanBuildAmplifier(type,100,required),"a sufficient living base permits a level 100 amplifier quote");
        Node(C_Amplifiers,profile);amplifiers.JumpToKey("Levels");amplifiers.JumpToKey("100",true);
        amplifiers.SetFloat("player_boss_bonus",7.25);
        Check(AmplifierLevelInfo(type,100,required,bonuses) && FloatAbs(bonuses[1]-7.25)<0.001,
            "explicit amplifier level 100 bonus overrides just that field of the formula");
        amplifiers.Rewind();Node(C_Amplifiers,profile);amplifiers.JumpToKey("Levels");amplifiers.JumpToKey("100");
        amplifiers.SetString("player_boss_bonus","NaN");
        Check(!AmplifierLevelInfo(type,100,required,bonuses),"amplifier reader refuses nonfinite explicit bonus without returning unsafe damage");
        Node(C_Progression,"Player/Boss/BaseMultipliers");formula.SetString("75","1.0bad");
        Check(!ReadProgressionBaseMultiplier(1,75,value),"progression explicit multiplier refuses a numeric prefix with invalid suffix");
        int parsed;
        Check(ProgressionLevelKey("2000000000",parsed) && parsed==2000000000
            && !ProgressionLevelKey("2000000001",parsed) && !ProgressionLevelKey("4294967297",parsed)
            && !ProgressionLevelKey("01",parsed),"level key parsing rejects integer overflow and ambiguous duplicate numbering");
        Node(C_Progression,"Player/Boss/BaseMultipliers");formula.SetFloat("75",9.25);
        Node(C_Amplifiers,profile);amplifiers.JumpToKey("Levels");amplifiers.JumpToKey("100");
        amplifiers.SetFloat("player_boss_bonus",7.25);
        Node(C_Progression,"Rules");formula.SetNum("base_level_cap",2000000000);
        Check(LoadProgression() && ReadProgressionBaseMultiplier(1,2000000000,value) && FloatAbs(value-100.0)<0.001,
            "two-billion level cap loads from sparse physical rows and clamps formula without per-level allocation or loops");
    }
    gBuildings[primary].level=originalLevel;
    gCfg[C_Progression]=originalProgression;gCfg[C_Amplifiers]=originalAmplifiers;
    delete formula;delete amplifiers;
    Check(LoadProgression(),"formula probe restores original progression handles and default balance");
}
