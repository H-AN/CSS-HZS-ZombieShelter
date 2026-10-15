// Production parser/config/inventory tests: no clients, BOTs, NPC creation or damage.
void DropProbeRule(KeyValues kv,const char[] id,const char[] materials,const char[] chances,const char[] amounts,const char[] name="v05_fixture")
{
    kv.Rewind();kv.JumpToKey("Rules",true);kv.JumpToKey(id,true);
    kv.SetString("zombie_name",name);kv.SetString("materials",materials);kv.SetString("chances",chances);kv.SetString("amounts",amounts);
}

int DropProbeCount(const char[] name)
{
    int count;for(int i=0;i<gDropEntries.Length;i++){DropEntry entry;gDropEntries.GetArray(i,entry,sizeof(entry));if(StrEqual(entry.zombieName,name))count++;}
    return count;
}

bool DropProbeEntry(const char[] name,int ordinal,const char[] id,float chance,int amount)
{
    int found;for(int i=0;i<gDropEntries.Length;i++)
    {
        DropEntry entry;gDropEntries.GetArray(i,entry,sizeof(entry));if(!StrEqual(entry.zombieName,name))continue;
        if(found++!=ordinal)continue;
        return entry.material==FindMaterial(id) && FloatAbs(entry.chance-chance)<0.00001 && entry.amount==amount;
    }
    return false;
}

void RunMaterialDropTests()
{
    Check(gMaterialCount>=7 && gMaterialCount<=MATERIALS,"v0.5 material registry uses a checked runtime count");
    static const char ids[][]={"wood","iron","copper","silver","gold"};
    bool legacy=true;for(int m=0;m<5;m++)if(!StrEqual(gMaterials[m].id,ids[m]))legacy=false;
    Check(legacy,"legacy five material IDs retain their published array order");
    int coreMaterial=FindMaterial("plague_core"),credits=FindMaterial("credits");
    Check(coreMaterial>=0 && !gMaterials[coreMaterial].gatherable && !gMaterials[coreMaterial].model[0],"plague core is registered without a fake harvesting model");
    Check(credits>=0 && gMaterials[credits].currency && !gMaterials[credits].gatherable,"internal currency stays separate from harvesting and CS cash");
    Check(gCfg[C_Drops]==null,"retired drops.cfg is absent from runtime config handles");
    char token[64];
    Check(CSVTokenCount(",wood,,iron,")==5 && CSVTokenAt(",wood,,iron,",0,token,sizeof(token)) && !token[0],"CSV preserves a leading empty token");
    Check(CSVTokenAt(",wood,,iron,",2,token,sizeof(token)) && !token[0] && CSVTokenAt(",wood,,iron,",3,token,sizeof(token)) && StrEqual(token,"iron"),"CSV preserves consecutive empty slots and original indices");
    Check(CSVTokenAt(",wood,,iron,",4,token,sizeof(token)) && !token[0] && !CSVTokenAt("wood",1,token,sizeof(token)),"CSV preserves trailing empty and distinguishes missing positions");
    Check(CSVTokenCount("wood，iron;gold")==1,"CSV only recognizes the English comma delimiter");
    int amount;float chance;
    Check(!PositiveDropAmount("1.5",amount) && !PositiveDropAmount("abc",amount) && !PositiveDropAmount("2147483648",amount),"quantity parser rejects fractions, text and signed-cell overflow");
    Check(PositiveDropAmount("2147483647",amount) && amount==2147483647,"quantity parser accepts the maximum signed cell before policy clamping");
    Check(!PositiveDropChance("nan",chance) && !PositiveDropChance("inf",chance) && !PositiveDropChance("1e999",chance),"chance parser rejects NaN and infinity");
    Check(!DropRollSucceeds(0.0,0.0) && DropRollSucceeds(1.0,1.0) && DropRollSucceeds(1.0,0.0)
        && !DropRollSucceeds(0.5,0.5) && DropRollSucceeds(0.5,0.499),"drop probability boundaries include guaranteed 100 percent even at RNG upper endpoint");
    Check(RewardOwnerKeyMatches("serial:123","STEAM_0:1:456",123)
        && !RewardOwnerKeyMatches("STEAM_0:1:777","STEAM_0:1:456",123)
        && !RewardOwnerKeyMatches("serial:124","STEAM_0:1:456",123),"delayed reward identity tolerates authentication completion while rejecting a different owner");

    KeyValues previous=gCfg[C_ZombieDrops],fixture=new KeyValues("ShelterZombieDrops");
    fixture.SetNum("schema_version",2);
    DropProbeRule(fixture,"broadcast","wood,iron","0.1","1","broadcast");
    DropProbeRule(fixture,"tails","wood","0.1,0.9","1,99","tails");
    DropProbeRule(fixture,"slots","wood,,iron","0.1,0.9,0.3","1,9,3","slots");
    DropProbeRule(fixture,"unknown","wood,does_not_exist,iron","0.1,0.9,0.4","1,9,4","unknown");
    DropProbeRule(fixture,"short","wood,iron,gold","0.1,0.2","1,2","short");
    DropProbeRule(fixture,"trailing","wood,iron","0.1,","1","trailing");
    DropProbeRule(fixture,"empty","","0.1","1","empty");
    DropProbeRule(fixture,"no_chances","wood","","1","no_chances");
    DropProbeRule(fixture,"zeros","wood,iron","0,-1","1","zeros");
    DropProbeRule(fixture,"bad_amount","wood,iron","1","1.5,2147483648","bad_amount");
    DropProbeRule(fixture,"clamp","wood","10","1000001","clamp");
    DropProbeRule(fixture,"repeated","wood,wood","1","1","repeated");
    DropProbeRule(fixture,"extra_group","iron","1","2","repeated");
    DropProbeRule(fixture,"other_delimiter","wood，iron","1","1","other_delimiter");
    DropProbeRule(fixture,"disabled","wood","1","1","disabled");fixture.SetNum("enabled",0);
    DropProbeRule(fixture,"empty_name","wood","1","1","");
    DropProbeRule(fixture,"offline","plague_core","1","1","offline_facility");
    gCfg[C_ZombieDrops]=fixture;ReadZombieDrops();
    Check(DropProbeCount("broadcast")==2 && DropProbeEntry("broadcast",1,"iron",0.1,1),"single nonempty chance/amount broadcast across the material list");
    Check(DropProbeCount("tails")==1 && DropProbeEntry("tails",0,"wood",0.1,1),"excess numerical values are silently discarded");
    Check(DropProbeCount("slots")==2 && DropProbeEntry("slots",1,"iron",0.3,3),"empty material slots do not shift subsequent chance or amount");
    Check(DropProbeCount("unknown")==2 && DropProbeEntry("unknown",1,"iron",0.4,4),"unknown materials skip only their original position");
    Check(DropProbeCount("short")==2 && DropProbeCount("trailing")==1,"short multivalue lists and trailing empty values never broadcast/fill");
    Check(DropProbeCount("empty")==0 && DropProbeCount("no_chances")==0 && DropProbeCount("zeros")==0,"empty groups and non-positive or missing chances yield nothing");
    Check(DropProbeCount("bad_amount")==0 && DropProbeCount("other_delimiter")==0 && DropProbeCount("disabled")==0,"invalid local entries and disabled groups do not stop valid drop rules");
    Check(DropProbeEntry("clamp",0,"wood",1.0,1000000),"finite chance and quantity policy bounds are clamped independently");
    Check(DropProbeCount("repeated")==3,"duplicate materials and same-name rule groups remain independent entries");
    int freeNPC=-1;for(int n=MAX_NPCS-1;n>=0;n--)if(!gNPCs[n].used){freeNPC=n;break;}
    if(freeNPC>=0 && coreMaterial>=0 && gCoreCount>0)
    {
        NPC savedNPC;savedNPC=gNPCs[freeNPC];int savedRef=gDamageNPCRef;
        RewardContext savedReward;savedReward=gDamageRewardContext;
        bool savedDefensePolicy=gRewardDefenseKills;
        int destination=gCoreCount>1 ? 1 : 0,balance=gBaseInventory[destination][coreMaterial];
        gBaseInventory[destination][coreMaterial]=0;
        gNPCs[freeNPC].ref=-2000001234;CacheNPCDrops(freeNPC,"offline_facility");gDamageNPCRef=gNPCs[freeNPC].ref;
        RewardContext reward;reward.valid=true;reward.facility=true;reward.epoch=gEpoch;
        reward.sourceUID=1234;reward.sourceRef=INVALID_ENT_REFERENCE;reward.buildCore=destination;reward.ownerSerial=0;
        gDamageRewardContext=reward;gRewardDefenseKills=false;SettleZombieDrops(freeNPC,0);
        Check(gBaseInventory[destination][coreMaterial]==0,"disabled facility rewards cannot be claimed through an attack-client proxy");
        gRewardDefenseKills=true;gDamageRewardContext.epoch=gEpoch-1;SettleZombieDrops(freeNPC,0);
        Check(gBaseInventory[destination][coreMaterial]==0,"a delayed hit from an expired campaign epoch cannot reward");
        gDamageRewardContext=reward;SettleZombieDrops(freeNPC,0);
        Check(gBaseInventory[destination][coreMaterial]==1,"offline/unowned delayed facility reward reaches fixed buildCore even after source entity is gone");
        gDamageNPCRef=INVALID_ENT_REFERENCE;SettleZombieDrops(freeNPC,0);
        Check(gBaseInventory[destination][coreMaterial]==1,"world damage with no verified source produces no material reward");
        gBaseInventory[destination][coreMaterial]=balance;gDamageNPCRef=savedRef;gDamageRewardContext=savedReward;
        gRewardDefenseKills=savedDefensePolicy;ReleaseNPCDropCache(freeNPC);gNPCs[freeNPC]=savedNPC;
    }
    gCfg[C_ZombieDrops]=previous;delete fixture;ReadZombieDrops();
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used)CacheNPCDrops(n,gNPCs[n].publicName);

    KeyValues resourcePrevious=gCfg[C_Resources],resourceFixture=new KeyValues("ShelterResources");
    resourceFixture.JumpToKey("Weights",true);resourceFixture.SetNum("plague_core",1);
    gCfg[C_Resources]=resourceFixture;float weights[MATERIALS],total;
    Check(!ReadResourceWeights(C_Resources,"Weights",weights,total),"a harvesting pool cannot select a registered non-gatherable core");
    int savedMaterialCount=gMaterialCount;MaterialDef savedHigh;savedHigh=gMaterials[32];
    MaterialDef high;strcopy(high.id,sizeof(high.id),"v05_high_index");high.gatherable=true;gMaterials[32]=high;if(gMaterialCount<33)gMaterialCount=33;
    resourceFixture.Rewind();resourceFixture.JumpToKey("HighWeights",true);resourceFixture.SetNum("v05_high_index",1);
    Check(ReadResourceWeights(C_Resources,"HighWeights",weights,total) && FloatAbs(weights[32]-1.0)<0.0001,"harvesting weights above index 31 use a safe per-material seen table");
    gMaterials[32]=savedHigh;gMaterialCount=savedMaterialCount;
    gCfg[C_Resources]=resourcePrevious;delete resourceFixture;
    if(gCoreCount>0 && coreMaterial>=0)
    {
        int base=gCores[0].building;char profile[128],upgrade[192];BuildingProfilePath(base,profile,sizeof(profile));
        Format(upgrade,sizeof(upgrade),"%s/Levels/2/upgrade_cost",profile);Node(C_Bases,upgrade);
        bool hadCoreCost=gCfg[C_Bases].GetDataType("plague_core")!=KvData_None;
        char oldCoreCost[32];gCfg[C_Bases].GetString("plague_core",oldCoreCost,sizeof(oldCoreCost),"0");gCfg[C_Bases].SetNum("plague_core",1);
        int quote[MATERIALS],nextHP;
        Check(ReadBuildingLevelQuote(base,2,quote,nextHP) && quote[coreMaterial]==1,"real base upgrade resolver includes newly registered plague core costs");
        Node(C_Bases,upgrade);if(hadCoreCost)gCfg[C_Bases].SetString("plague_core",oldCoreCost);else gCfg[C_Bases].DeleteKey("plague_core");
        int saved[MAX_CORES];for(int c=0;c<gCoreCount;c++){saved[c]=gBaseInventory[c][coreMaterial];gBaseInventory[c][coreMaterial]=2000000000;}
        char oldCapacity[32];Text(C_Resources,"BaseStorage","capacity",oldCapacity,sizeof(oldCapacity),"0");
        Node(C_Resources,"BaseStorage");gCfg[C_Resources].SetNum("capacity",2000000000);
        ArrayList previousOverflow=gSharedOverflow;gSharedOverflow=new ArrayList(3);
        gBaseInventory[0][coreMaterial]=1999999999;DepositSharedAutomatic(0,coreMaterial,1000000);
        Check(gBaseInventory[0][coreMaterial]==2000000000 && SharedPendingAmount(0,coreMaterial)==999999,"new materials safely fill warehouse and preserve exact overflow remainder");
        gBaseInventory[0][coreMaterial]-=999999;FlushSharedOverflow();
        Check(gBaseInventory[0][coreMaterial]==2000000000 && SharedPendingAmount(0,coreMaterial)==0,"queued new material overflow flushes exactly once after room is available");
        delete gSharedOverflow;gSharedOverflow=previousOverflow;
        for(int c=0;c<gCoreCount;c++)gBaseInventory[c][coreMaterial]=saved[c];
        Node(C_Resources,"BaseStorage");gCfg[C_Resources].SetString("capacity",oldCapacity);
    }
}
