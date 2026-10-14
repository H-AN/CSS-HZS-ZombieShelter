// Real logical upgrades against an existing collider; config handles, inventory,
// instance state and client transform are restored before returning.
Building tGrowthSavedBuilding;
int tGrowthSavedInventory[MATERIALS];

void GrowthSetInt(ConfigFile file,const char[] path,const char[] key,int value)
{
    gCfg[file].Rewind();char parts[8][64];int count=ExplodeString(path,"/",parts,sizeof(parts),sizeof(parts[]));
    for(int i=0;i<count;i++)gCfg[file].JumpToKey(parts[i],true);
    gCfg[file].SetNum(key,value);
}

void GrowthSetFloat(ConfigFile file,const char[] path,const char[] key,float value)
{
    gCfg[file].Rewind();char parts[8][64];int count=ExplodeString(path,"/",parts,sizeof(parts),sizeof(parts[]));
    for(int i=0;i<count;i++)gCfg[file].JumpToKey(parts[i],true);
    gCfg[file].SetFloat(key,value);
}

void GrowthSetText(ConfigFile file,const char[] path,const char[] key,const char[] value)
{
    gCfg[file].Rewind();char parts[8][64];int count=ExplodeString(path,"/",parts,sizeof(parts),sizeof(parts[]));
    for(int i=0;i<count;i++)gCfg[file].JumpToKey(parts[i],true);
    gCfg[file].SetString(key,value);
}

bool GrowthBalancesEqual(int client,const int expected[MATERIALS])
{
    for(int m=0;m<MATERIALS;m++)if(gPlayerInventory[client][m]!=expected[m])return false;
    return true;
}

void GrowthCheckPaidUpgrade(int client,int building,int destination,int expectedHP,const char[] name)
{
    gBuildings[building].level=destination-1;
    for(int m=0;m<MATERIALS;m++){gBuildings[building].invested[m]=0;gPlayerInventory[client][m]=1000000;}
    Check(ApplyBuildingLevel(building,true),"formula prepares the preceding real building level");
    gBuildings[building].hp=gBuildings[building].maxHP-100;
    int quote[MATERIALS],nextHP,before[MATERIALS];before=gPlayerInventory[client];
    bool read=ReadBuildingLevelQuote(building,destination,quote,nextHP);
    bool upgraded=read && TryUpgrade(client,building);
    bool exact=upgraded;
    for(int m=0;m<MATERIALS;m++)
        if(gPlayerInventory[client][m]!=before[m]-quote[m] || gBuildings[building].invested[m]!=quote[m])exact=false;
    Check(exact && gBuildings[building].level==destination && gBuildings[building].maxHP==expectedHP
        && nextHP==expectedHP && gBuildings[building].hp==expectedHP-100,name);
}

void TestGrowth(int client)
{
    Check(Running() && LiveHuman(client) && gCoreCount>0,"growth fixture has a running campaign and living player");
    if(!Running() || !LiveHuman(client) || gCoreCount<1)return;
    int building=gCores[0].building;
    if(building<0 || !gBuildings[building].alive || Resolve(gBuildings[building].ref)<0)
    {Check(false,"growth fixture base collider exists");return;}
    ConfigFile files[]={C_Bases,C_Turrets,C_Amplifiers};KeyValues original[3];
    for(int i=0;i<sizeof(files);i++)
    {
        original[i]=gCfg[files[i]];original[i].Rewind();char root[64];original[i].GetSectionName(root,sizeof(root));
        gCfg[files[i]]=new KeyValues(root);KvCopySubkeys(original[i],gCfg[files[i]]);
    }
    tGrowthSavedBuilding=gBuildings[building];tGrowthSavedInventory=gPlayerInventory[client];
    float origin[3],angles[3],velocity[3],repairCooldown=gNextRepair[client];
    GetClientAbsOrigin(client,origin);GetClientEyeAngles(client,angles);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",velocity);
    int activeWeapon=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon"),inventoryCap=gInventoryCap;
    gInventoryCap=0;
    char base[128],baseGrowth[192],baseStats[192],baseCosts[192];
    BuildingProfilePath(building,base,sizeof(base));Format(baseGrowth,sizeof(baseGrowth),"%s/Growth",base);
    Format(baseStats,sizeof(baseStats),"%s/Stats",baseGrowth);Format(baseCosts,sizeof(baseCosts),"%s/UpgradeCost",baseGrowth);
    Check(ValidateLevelProfiles(),"all shipped explicit levels and inactive extension formulas validate");
    GrowthSetInt(C_Bases,baseGrowth,"max_level",100);
    Check(ValidateLevelProfile(C_Bases,base) && LevelAvailable(C_Bases,base,100)
        && !LevelAvailable(C_Bases,base,101),"changing only max_level enables a hundred-level base and enforces its configured cap");
    TeleportEntity(client,gCorePoints[0][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    GrowthCheckPaidUpgrade(client,building,100,149000,"base 99 to 100 consumes the formula cost once and preserves missing HP");
    int before[MATERIALS];before=gPlayerInventory[client];
    Check(!TryUpgrade(client,building) && GrowthBalancesEqual(client,before) && gBuildings[building].level==100,
        "upgrading beyond configured maximum never spends personal materials");
    GrowthSetInt(C_Bases,baseGrowth,"max_level",1000);
    GrowthCheckPaidUpgrade(client,building,1000,1499000,"base 999 to 1000 proves the implementation has no hardcoded hundred-level ceiling");

    // A valid quote still cannot wrap refundable investment.
    gBuildings[building].level=99;ApplyBuildingLevel(building,true);
    for(int m=0;m<MATERIALS;m++)gBuildings[building].invested[m]=0;
    gBuildings[building].invested[0]=LEVEL_CELL_LIMIT;before=gPlayerInventory[client];
    Check(!TryUpgrade(client,building) && gBuildings[building].level==99 && GrowthBalancesEqual(client,before)
        && gBuildings[building].invested[0]==LEVEL_CELL_LIMIT,"cumulative investment overflow rejects before debiting any material");

    int value,cost[MATERIALS];
    GrowthSetInt(C_Bases,baseCosts,"wood_per_level",-1);
    Check(!ReadLevelUpgradeCost(C_Bases,base,100,cost) && !ValidateLevelProfile(C_Bases,base),
        "negative growth material costs are rejected by both runtime quote and profile validation");
    GrowthSetInt(C_Bases,baseCosts,"wood_per_level",LEVEL_CELL_LIMIT);
    Check(!ReadLevelUpgradeCost(C_Bases,base,100,cost),"multiplication overflow cannot wrap a large material quote into a cheap upgrade");
    GrowthSetInt(C_Bases,baseCosts,"wood_per_level",5);
    GrowthSetText(C_Bases,baseGrowth,"max_level","4294967296");
    Check(!LevelAvailable(C_Bases,base,100),"configuration integer overflow is rejected before conversion");
    GrowthSetInt(C_Bases,baseGrowth,"max_level",LEVEL_CELL_LIMIT);
    for(int m=0;m<MATERIALS;m++){char key[64];Format(key,sizeof(key),"%s_per_level",gMaterials[m].id);GrowthSetInt(C_Bases,baseCosts,key,0);}
    GrowthSetInt(C_Bases,baseStats,"max_hp_per_level",LEVEL_CELL_LIMIT);
    Check(ReadLevelInt(C_Bases,base,LEVEL_CELL_LIMIT,"max_hp",0,value) && value==LEVEL_CELL_LIMIT,
        "two-billionth level resolves in one arithmetic step and safely saturates HP");
    Check(ProfileMaximumLevel(C_Bases,base)==LEVEL_CELL_LIMIT,"maximum-level lookup does not allocate or iterate a per-level array");

    gBuildings[building].level=LEVEL_CELL_LIMIT;ApplyBuildingLevel(building,true);gBuildings[building].hp=LEVEL_CELL_LIMIT-1;
    GrowthSetInt(C_Bases,base,"repair_hp_per_hit",LEVEL_CELL_LIMIT);
    int knife=GetPlayerWeaponSlot(client,2);
    if(knife>MaxClients)
    {
        SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",knife);gNextRepair[client]=0.0;
        int entity=Resolve(gBuildings[building].ref);
        SDKHooks_TakeDamage(entity,client,client,1.0,DMG_BULLET|DMG_NEVERGIB,_,_,_,false);
        Check(gBuildings[building].hp==LEVEL_CELL_LIMIT,"repair near the HP integer boundary clamps before addition");
    }
    else Check(false,"growth repair fixture requires the player's existing knife");
    GrowthSetInt(C_Bases,baseGrowth,"max_level",100);
    GrowthSetInt(C_Bases,baseStats,"max_hp_per_level",1500);
    GrowthSetInt(C_Bases,baseStats,"max_hp_min",100000);
    Check(ReadLevelInt(C_Bases,base,5,"max_hp",0,value) && value==100000,
        "integer formula clamps after calculating rather than adding growth to an already-clamped anchor");

    // Reuse the existing body's real collider to test ordinary defense ownership,
    // distance checks and high-level charging without leaving test entities behind.
    gBuildings[building]=tGrowthSavedBuilding;
    gBuildings[building].kind=Kind_Turret;gBuildings[building].core=-1;gBuildings[building].file=C_Turrets;
    strcopy(gBuildings[building].type,sizeof(gBuildings[].type),"machinegun_basic");AssignBuildingOwner(building,client);
    char turret[]="Types/machinegun_basic";
    GrowthSetInt(C_Turrets,"Types/machinegun_basic/Growth","max_level",100);
    float wall[3],stand[3];wall=gCores[0].origin;wall[0]-=160.0;wall[1]-=128.0;wall[2]+=45.0;
    stand=wall;stand[1]-=70.0;stand[2]=gCores[0].origin[2]+2.0;Aim(client,stand,wall);
    Check(BuildingInUseRange(client,building),"formula defense fixture reaches its real collider within use range");
    GrowthCheckPaidUpgrade(client,building,100,25250,"owned turret 99 to 100 uses formula attributes and exact personal upgrade costs");
    Check(FloatAbs(gBuildings[building].damage-505.0)<0.01 && FloatAbs(gBuildings[building].range-1580.0)<0.01
        && FloatAbs(gBuildings[building].interval-0.1)<0.001,"high-level turret damage and range grow while fire interval stops at its safe floor");
    GrowthSetInt(C_Turrets,"Types/machinegun_basic/Levels/50","max_hp",123456);
    GrowthSetFloat(C_Turrets,"Types/machinegun_basic/Levels/50","damage",700.0);
    GrowthSetInt(C_Turrets,"Types/machinegun_basic/Levels/50/upgrade_cost","gold",7);
    float decimal;
    Check(ReadLevelInt(C_Turrets,turret,50,"max_hp",0,value) && value==123456
        && ReadLevelFloat(C_Turrets,turret,50,"damage",0.0,decimal) && decimal==700.0
        && ReadLevelUpgradeCost(C_Turrets,turret,50,cost) && cost[0]==0 && cost[1]==0 && cost[4]==7,
        "sparse explicit stat overrides and complete explicit cost recipes take precedence over formulas");
    GrowthSetText(C_Turrets,"Types/machinegun_basic/Levels/50","damage","NaN");
    Check(!ReadLevelFloat(C_Turrets,turret,50,"damage",0.0,decimal),"NaN attributes never reach entity or combat arithmetic");
    GrowthSetFloat(C_Turrets,"Types/machinegun_basic/Levels/50","damage",700.0);
    GrowthSetFloat(C_Turrets,"Types/machinegun_basic/Levels/2","fire_interval",0.0);
    Check(!ValidateLevelProfile(C_Turrets,turret),"an explicitly zero firing interval rejects the profile");
    GrowthSetFloat(C_Turrets,"Types/machinegun_basic/Levels/2","fire_interval",0.4);
    GrowthSetFloat(C_Turrets,"Types/machinegun_basic/Growth/Stats","fire_interval_min",1.0);
    GrowthSetFloat(C_Turrets,"Types/machinegun_basic/Growth/Stats","fire_interval_max",0.5);
    Check(!ValidateLevelProfile(C_Turrets,turret),"reversed formula clamps are rejected");

    GrowthSetInt(C_Amplifiers,"Types/damage_amplifier/Growth","max_level",100);
    Check(ReadLevelInt(C_Amplifiers,"Types/damage_amplifier",100,"minimum_base_level",1,value) && value==4
        && ReadLevelFloat(C_Amplifiers,"Types/damage_amplifier",100,"player_boss_bonus",0.0,decimal)
        && FloatAbs(decimal-16.05)<0.001,"amplifier requirements and bonuses also resolve beyond sixteen explicit levels");

    gBuildings[building]=tGrowthSavedBuilding;gPlayerInventory[client]=tGrowthSavedInventory;
    gInventoryCap=inventoryCap;gNextRepair[client]=repairCooldown;
    if(activeWeapon>MaxClients && IsValidEntity(activeWeapon))SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",activeWeapon);
    for(int i=0;i<sizeof(files);i++){delete gCfg[files[i]];gCfg[files[i]]=original[i];gCfg[files[i]].Rewind();}
    TeleportEntity(client,origin,angles,velocity);
}

int tGrowthUIBuilding=-1,tGrowthUIWoodBefore,tGrowthUIWoodCost,tGrowthUIMaxHP;

// Deliberately persistent test fixture: reload the map to discard its in-memory
// configuration and balances. The final confirmation must use real E/number input.
void TestPrepareGrowthUI(int client,bool turret)
{
    if(!LiveHuman(client) || !Running()){Check(false,"growth UI requires a live real campaign");return;}
    int building=gCores[0].building;
    if(turret)
    {
        LiveManagementCommand(client,"defense");building=tLiveDefense;if(building<0)return;
    }
    else
    {
        TeleportEntity(client,gCorePoints[0][0],gCorePointAngles[0][0],view_as<float>({0.0,0.0,0.0}));
        SetEntityHealth(client,10000);
    }
    char profile[128],path[192];BuildingProfilePath(building,profile,sizeof(profile));
    Format(path,sizeof(path),"%s/Growth",profile);GrowthSetInt(gBuildings[building].file,path,"max_level",100);
    gBuildings[building].level=99;
    Check(ApplyBuildingLevel(building,true),"live growth fixture resolves level 99 without adding explicit level rows");
    for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=100000;
    int cost[MATERIALS];Check(ReadBuildingLevelQuote(building,100,cost,tGrowthUIMaxHP),"live level-100 menu quote exists");
    tGrowthUIBuilding=building;tGrowthUIWoodBefore=gPlayerInventory[client][0];tGrowthUIWoodCost=cost[0];gNextUse[client]=0.0;gNextHUD=0.0;
    if(turret)LiveAimDefense(client);
    PrintToServer("[GrowthUI] building=%d kind=%d level=99 nextHP=%d wood_cost=%d",building,gBuildings[building].kind,tGrowthUIMaxHP,tGrowthUIWoodCost);
}

void TestVerifyGrowthUI(int client)
{
    int building=tGrowthUIBuilding;
    Check(building>=0 && gBuildings[building].alive && gBuildings[building].level==100
        && gBuildings[building].maxHP==tGrowthUIMaxHP && gPlayerInventory[client][0]==tGrowthUIWoodBefore-tGrowthUIWoodCost
        && !BuildingLevelAvailable(building,101),"physical menu confirmation upgrades 99 to 100, charges the quoted wood and reaches the configured cap");
}
