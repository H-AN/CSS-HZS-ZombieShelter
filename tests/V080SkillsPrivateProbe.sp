// Private assertions on isolated account/configuration data, using production
// functions. Never creates entities/clients, respawns, changes live day/phase,
// calls engine damage, or modifies a real player's account/materials.
// Rename every engine/HZS forward before including production. This plugin has
// no production startup/map/client/entity callbacks and registers no API library.
#define myinfo SkillsPrivateUnusedProductionInfo
#define AskPluginLoad2 SkillsPrivateDisabledAsk
#define OnPluginStart SkillsPrivateDisabledStart
#define OnMapStart SkillsPrivateDisabledMapStart
#define OnConfigsExecuted SkillsPrivateDisabledConfigs
#define OnClientPutInServer SkillsPrivateDisabledPut
#define OnClientDisconnect SkillsPrivateDisabledDisconnect
#define OnClientAuthorized SkillsPrivateDisabledAuthorized
#define OnEntityDestroyed SkillsPrivateDisabledEntityDestroyed
#define OnMapEnd SkillsPrivateDisabledMapEnd
#define OnPluginEnd SkillsPrivateDisabledEnd
#define OnLibraryRemoved SkillsPrivateDisabledLibraryRemoved
#define OnPlayerRunCmd SkillsPrivateDisabledRunCmd
#define Han_OnGameStart SkillsPrivateDisabledGameStart
#define Han_OnGameEnd SkillsPrivateDisabledGameEnd
#define Han_OnZombieWin SkillsPrivateDisabledZombieWin
#define Han_OnHumanWin SkillsPrivateDisabledHumanWin
#define Han_OnZombieCreated SkillsPrivateDisabledZombieCreated
#define Han_OnZombieDamagePre SkillsPrivateDisabledZombieDamagePre
#define Han_OnZombieDeath SkillsPrivateDisabledZombieDeath
#define CS_OnTerminateRound SkillsPrivateDisabledTerminateRound
#include "../addons/sourcemod/scripting/HanZombieShelter.sp"
#undef myinfo
#undef AskPluginLoad2
#undef OnPluginStart
#undef OnMapStart
#undef OnConfigsExecuted
#undef OnClientPutInServer
#undef OnClientDisconnect
#undef OnClientAuthorized
#undef OnEntityDestroyed
#undef OnMapEnd
#undef OnPluginEnd
#undef OnLibraryRemoved
#undef OnPlayerRunCmd
#undef Han_OnGameStart
#undef Han_OnGameEnd
#undef Han_OnZombieWin
#undef Han_OnHumanWin
#undef Han_OnZombieCreated
#undef Han_OnZombieDamagePre
#undef Han_OnZombieDeath
#undef CS_OnTerminateRound

public Plugin myinfo={name="V080 Skills Private Assertions",author="H-AN",description="Isolated account/config/math tests; no game hooks",version="0.8.0-test",url=""};
int tSkillsPassed,tSkillsFailed,tSkillsSkipped;
char gPrivateSkillsPath[PLATFORM_MAX_PATH]="data/v080/skills.cfg";

public APLRes AskPluginLoad2(Handle myself,bool late,char[] error,int maxlength)
{
    MarkNativeAsOptional("Han_IsZombie");MarkNativeAsOptional("Han_GetZombieName");MarkNativeAsOptional("Han_GetZombieCount");MarkNativeAsOptional("Han_GetZombieByIndex");
    MarkNativeAsOptional("Han_SetZombieTarget");MarkNativeAsOptional("Han_UnlockZombie");MarkNativeAsOptional("Han_SetCustomAnimationEx");MarkNativeAsOptional("Han_SafeDamageZombie");
    return APLRes_Success;
}

public void OnPluginStart()
{
    RegServerCmd("zsh_v080_skills_private",V080SkillsPrivateCommand);
    static const char materialIDs[][]={"wood","iron","copper","silver","gold","plague_core","credits"};
    gMaterialCount=sizeof(materialIDs);for(int m=0;m<gMaterialCount;m++)strcopy(gMaterials[m].id,sizeof(gMaterials[].id),materialIDs[m]);
    gSkillsMapSession=1;
}

void SkillsCheck(bool result,const char[] label)
{
    if(result)tSkillsPassed++;else tSkillsFailed++;
    PrintToServer("[V080SkillsPrivate] %s %s",result ? "PASS" : "FAIL",label);
}

bool SkillsProbeParse(KeyValues kv)
{
    bool enabled,skillEnabled[SKILL_COUNT];int daily,maximum[SKILL_COUNT],cost[SKILL_COUNT],respec[MATERIALS];
    float amount[SKILL_COUNT],radius[SKILL_COUNT],interval[SKILL_COUNT],cap;
    return SkillsParseConfig(kv,enabled,daily,skillEnabled,maximum,cost,amount,radius,interval,cap,respec);
}

KeyValues SkillsProbeConfig()
{
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"%s",gPrivateSkillsPath);
    KeyValues kv=new KeyValues("ShelterSkills");if(!kv.ImportFromFile(path)){delete kv;return null;}return kv;
}

void SkillsPrivateConfigTests()
{
    KeyValues kv=SkillsProbeConfig();if(kv==null){SkillsCheck(false,"shipped skills.cfg available");return;}
    SkillsCheck(SkillsProbeParse(kv),"shipped 13-skill config and seven personal respec costs accepted");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.JumpToKey("gather");kv.SetString("amount_per_level","nan");
    SkillsCheck(!SkillsProbeParse(kv),"NaN multiplier rejected");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.JumpToKey("gravity");kv.SetString("amount_per_level","inf");
    SkillsCheck(!SkillsProbeParse(kv),"infinite gravity rejected");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Respec");kv.SetString("wood","-1");
    SkillsCheck(!SkillsProbeParse(kv),"negative material cost rejected");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.JumpToKey("selfarmor");kv.SetString("interval_seconds","0");
    SkillsCheck(!SkillsProbeParse(kv),"zero recovery interval rejected");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.JumpToKey("unknown_skill",true);kv.SetNum("enabled",1);
    SkillsCheck(!SkillsProbeParse(kv),"unknown skill section rejected");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.SetString("unknown_scalar","1");
    SkillsCheck(!SkillsProbeParse(kv),"unknown scalar skill rejected rather than ignored");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Respec");kv.SetNum("unknown_material",1);
    SkillsCheck(!SkillsProbeParse(kv),"unknown payment material rejected");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.JumpToKey("maxhp");kv.SetString("max_level","2147483648");
    SkillsCheck(!SkillsProbeParse(kv),"overflow level text rejected");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.JumpToKey("immunity");kv.SetString("chance_cap","1.01");
    SkillsCheck(!SkillsProbeParse(kv),"probability over one rejected");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.JumpToKey("medic");kv.SetString("self_included","1");
    SkillsCheck(!SkillsProbeParse(kv),"config cannot silently change caster-exclusion rule");delete kv;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.JumpToKey("repair");kv.SetFloat("amount_per_level",0.5);
    SkillsCheck(!SkillsProbeParse(kv),"fractional repair HP rejected without rounding silent loss");delete kv;
    char longNumber[160];strcopy(longNumber,sizeof(longNumber),"0.");for(int i=2;i<120;i++)longNumber[i]='0';longNumber[120]='x';longNumber[121]=0;
    kv=SkillsProbeConfig();kv.JumpToKey("Skills");kv.JumpToKey("gather");kv.SetString("amount_per_level",longNumber);
    SkillsCheck(!SkillsProbeParse(kv),"long float suffix cannot disappear through truncation");delete kv;
    for(int i=0;i<120;i++)longNumber[i]='0';longNumber[120]='x';longNumber[121]=0;
    kv=SkillsProbeConfig();kv.JumpToKey("Respec");kv.SetString("wood",longNumber);
    SkillsCheck(!SkillsProbeParse(kv),"long integer suffix cannot disappear through truncation");delete kv;
}

void SkillsPrivateWorkTests()
{
    int client;
    for(int c=1;c<=MaxClients;c++)if(LiveHuman(c) && IsClientAuthorized(c) && !IsFakeClient(c) && !IsClientSourceTV(c) && !IsClientReplay(c)){client=c;break;}
    if(client<1){tSkillsSkipped++;PrintToServer("[V080SkillsPrivate] SKIP work formula/auth-order read-only fixture requires an existing verified real human; none created");return;}
    char steam[32];if(!SkillsVerifiedSteam64(client,steam,sizeof(steam))){tSkillsSkipped++;return;}
    // This standalone plugin's state is private. Only engine auth/serial/alive
    // reads below refer to the existing client; no attributes, health or entity
    // properties are ever written, and no real production account is accessed.
    bool savedEnabled=gEnabled,savedCore=gCoreReady,savedTerminal=gTerminal,savedSkills=gSkillsEnabled,savedReady=gSkillsConfigReady;
    Phase savedPhase=gPhase;gEnabled=true;gCoreReady=true;gTerminal=false;gSkillsEnabled=true;gSkillsConfigReady=true;gPhase=Phase_Day;
    int savedBinding=gSkillsBinding[client],savedSerial=gSkillsSerial[client];char savedSteam[32];strcopy(savedSteam,sizeof(savedSteam),gSkillsSteam[client]);
    bool savedAward=gSkillsSpawnAwardPending[client],savedPending=gSkillsSpawnPending[client];int savedAwardEpoch=gSkillsSpawnAwardEpoch[client];
    ArrayList savedRows=gSkillsRows;StringMap savedMap=gSkillsAccounts;gSkillsRows=new ArrayList(SKILL_ACCOUNT_CELLS);gSkillsAccounts=new StringMap();
    bool savedSkillEnabled[3];float savedAmount[3];
    for(int s=0;s<3;s++){savedSkillEnabled[s]=gSkillEnabled[s];savedAmount[s]=gSkillAmount[s];gSkillEnabled[s]=true;}
    gSkillAmount[0]=0.02;gSkillAmount[1]=1.0;gSkillAmount[2]=1.0;
    int hp=GetClientHealth(client),armor=GetEntProp(client,Prop_Send,"m_ArmorValue");
    gSkillsBinding[client]=0;gSkillsSpawnPending[client]=false;gSkillsSpawnAwardPending[client]=true;gSkillsSpawnAwardEpoch[client]=gEpoch;
    SkillsTrySpawnAward(client);
    SkillsCheck(gSkillsSpawnAwardPending[client] && GetClientHealth(client)==hp && GetEntProp(client,Prop_Send,"m_ArmorValue")==armor,
        "actual-spawn ticket survives unavailable verified binding without awarding health/armor");
    gSkillsSpawnAwardEpoch[client]=gEpoch-1;SkillsTrySpawnAward(client);
    SkillsCheck(!gSkillsSpawnAwardPending[client],"old round epoch invalidates delayed actual-spawn award");
    int account=SkillsFindAccount(steam);gSkillsBinding[client]=account+1;gSkillsSerial[client]=GetClientSerial(client);strcopy(gSkillsSteam[client],sizeof(gSkillsSteam[]),steam);
    SkillsCheck(SkillsActor(client),"private read-only identity fixture uses actual verified existing human");
    SkillsCheck(SkillsGatherBonus(client,1000)==1000 && SkillsGatherBonus(client,2000)==2000
        && SkillsWorkHP(client,5,false)==5 && SkillsWorkHP(client,7,true)==7,"zero skill levels preserve knife/axe and separate tool work values exactly");
    for(int s=0;s<3;s++)gSkillsRows.Set(account,1,s);
    SkillsCheck(SkillsGatherBonus(client,1000)==1020 && SkillsGatherBonus(client,2000)==2020,"level-one knife/axe adds fixed20 precision units rather than multiplying tool value");
    SkillsCheck(SkillsWorkHP(client,5,false)==6 && SkillsWorkHP(client,7,true)==8,"repair and construction each add fixed1 HP to their own positive tool value");
    int total,remainder;
    for(int strike=0;strike<5;strike++){int next;total+=CalculateGatherYield(10,SkillsGatherBonus(client,1000),remainder,1000,next);remainder=next;}
    SkillsCheck(total==51 && remainder==0,"five10.2 knife yields preserve production fractional accumulator to51");
    for(int s=0;s<3;s++)gSkillsRows.Set(account,5,s);
    SkillsCheck(SkillsGatherBonus(client,1000)==1100 && SkillsGatherBonus(client,2000)==2100,"level-five knife/axe adds configured0.10 to original multiplier");
    SkillsCheck(SkillsGatherBonus(client,0)==0 && SkillsWorkHP(client,0,false)==0 && SkillsWorkHP(client,0,true)==0,
        "maxed gather repair construction cannot enable any original zero tool ability");
    gSkillsRows.Set(account,3,1);gSkillsRows.Set(account,5,2);
    SkillsCheck(SkillsWorkHP(client,10,false)==13 && SkillsWorkHP(client,10,true)==15,"separate repair3 and construction5 do not cross-apply");
    SkillsCheck(SkillsWorkHP(client,SKILL_CELL_LIMIT-1,true)==SKILL_CELL_LIMIT,"positive work bonus saturates safely at numeric cell bound");
    SkillsTrySpawnAward(client);
    SkillsCheck(!gSkillsSpawnAwardPending[client] && GetClientHealth(client)==hp && GetEntProp(client,Prop_Send,"m_ArmorValue")==armor,
        "verified identity binding without a new birth ticket never grants HP or armor");
    for(int s=0;s<3;s++){gSkillEnabled[s]=savedSkillEnabled[s];gSkillAmount[s]=savedAmount[s];}
    delete gSkillsRows;delete gSkillsAccounts;gSkillsRows=savedRows;gSkillsAccounts=savedMap;
    gSkillsBinding[client]=savedBinding;gSkillsSerial[client]=savedSerial;strcopy(gSkillsSteam[client],sizeof(gSkillsSteam[]),savedSteam);
    gSkillsSpawnAwardPending[client]=savedAward;gSkillsSpawnPending[client]=savedPending;gSkillsSpawnAwardEpoch[client]=savedAwardEpoch;
    gEnabled=savedEnabled;gCoreReady=savedCore;gTerminal=savedTerminal;gSkillsEnabled=savedSkills;gSkillsConfigReady=savedReady;gPhase=savedPhase;
}

public Action V080SkillsPrivateCommand(int args)
{
    if(args>0)GetCmdArg(1,gPrivateSkillsPath,sizeof(gPrivateSkillsPath));
    tSkillsPassed=0;tSkillsFailed=0;tSkillsSkipped=0;
    char steam64[32];
    SkillsCheck(SkillsSteam2To64("STEAM_0:0:87636802",steam64,sizeof(steam64)) && StrEqual(steam64,"76561198135539332"),"verified Steam2 converts to exact observed SteamID64");
    SkillsCheck(SkillsSteam2To64("STEAM_1:1:87636802",steam64,sizeof(steam64)) && StrEqual(steam64,"76561198135539333"),"public Steam universe 0/1 normalize and parity retained");
    SkillsCheck(SkillsSteam2To64("STEAM_0:1:2147483647",steam64,sizeof(steam64)) && StrEqual(steam64,"76561202255233023"),"maximum unsigned account ID converts without signed overflow");
    SkillsCheck(!SkillsSteam2To64("STEAM_0:1:2147483648",steam64,sizeof(steam64)) && !SkillsSteam2To64("STEAM_0:0:0",steam64,sizeof(steam64)),"overflow account and invalid zero identity rejected");
    SkillsCheck(!SkillsSteam2To64("STEAM_ID_PENDING",steam64,sizeof(steam64)) && !SkillsSteam2To64("STEAM_ID_LAN",steam64,sizeof(steam64))
        && !SkillsSteam2To64("STEAM_0:2:1",steam64,sizeof(steam64)) && !SkillsSteam2To64("STEAM_2:1:1",steam64,sizeof(steam64))
        && !SkillsSteam2To64("STEAM_0:1:-1",steam64,sizeof(steam64)),"pending LAN malformed parity nonpublic and negative IDs rejected");
    SkillsCheck(SkillsQuotaFor(1,30,1)==1 && SkillsQuotaFor(5,30,1)==5,"first day and late join use cumulative quota");
    SkillsCheck(SkillsQuotaFor(50,30,1)==30 && SkillsQuotaFor(50,50,1)==50 && SkillsQuotaFor(65,0,1)==65,"finite 30/50 and infinite quotas follow session target");
    SkillsCheck(SkillsQuotaFor(2000000000,0,1000000)==SKILL_CELL_LIMIT,"quota multiplication never wraps signed cell");
    SkillsCheck(SkillsQuotaFor(-1,30,1)==0 && SkillsQuotaFor(1,-1,1)==0,"invalid quota inputs rejected");
    int savedHighest=gSkillsHighestDay;gSkillsHighestDay=1;
    SkillsRecordDay(20);SkillsRecordDay(5);bool steady=gSkillsHighestDay==20;
    for(int day=6;day<=20;day++){SkillsRecordDay(day);if(gSkillsHighestDay!=20)steady=false;}
    SkillsCheck(steady,"20 to 5 then 6..20 never re-awards prior days");SkillsRecordDay(21);
    SkillsCheck(gSkillsHighestDay==21,"first new highest day 21 raises quota once");gSkillsHighestDay=savedHighest;

    // Temporarily replace handles, not their real contents; restores before return.
    ArrayList savedRows=gSkillsRows;StringMap savedMap=gSkillsAccounts;
    gSkillsRows=new ArrayList(SKILL_ACCOUNT_CELLS);gSkillsAccounts=new StringMap();
    int a=SkillsFindAccount("76561198000000001"),b=SkillsFindAccount("76561198000000002");
    SkillsCheck(a==0 && b==1 && SkillsFindAccount("76561198000000001")==a,"same SteamID rejoins original row; different identity never inherits slot");
    bool enabled=gSkillEnabled[0];int maxLevel=gSkillMaxLevel[0],cost=gSkillCost[0];
    gSkillEnabled[0]=true;gSkillMaxLevel[0]=5;gSkillCost[0]=1;
    SkillsCheck(SkillsAccountPurchase(a,0,0,1,5),"valid purchase consumes actual cost and one revision");
    SkillsCheck(gSkillsRows.Get(a,SKILL_SPENT)==1 && gSkillsRows.Get(a,0)==1 && gSkillsRows.Get(a,SKILL_REVISION)==2,"level/spent/revision commit atomically");
    SkillsCheck(!SkillsAccountPurchase(a,0,0,1,5) && gSkillsRows.Get(a,SKILL_SPENT)==1,"duplicate old-menu purchase rejected without second debit");
    SkillsCheck(!SkillsAccountPurchase(a,0,1,2,1) && gSkillsRows.Get(a,0)==1,"insufficient quota preserves level and spent");
    int originalEpoch=gEpoch;gEpoch++;
    SkillsCheck(SkillsFindAccount("76561198000000001")==a && gSkillsRows.Get(a,0)==1,"round epoch changes leave map account intact");gEpoch=originalEpoch;
    int originalSession=gSkillsMapSession;gSkillsMapSession++;
    SkillsCheck(!SkillsAccountPurchase(a,0,1,2,5),"old map account cannot spend in new map session");gSkillsMapSession=originalSession;
    gSkillCost[0]=2;
    SkillsCheck(SkillsAccountPurchase(a,0,1,2,5) && gSkillsRows.Get(a,SKILL_SPENT)==3,"spent ledger preserves prior actual cost rather than recomputing old level");
    gSkillEnabled[0]=false;
    SkillsCheck(!SkillsAccountPurchase(a,0,2,3,30),"disabled skill cannot be purchased");gSkillEnabled[0]=true;
    gSkillCost[0]=1;SkillsAccountPurchase(a,0,2,3,30);SkillsAccountPurchase(a,0,3,4,30);SkillsAccountPurchase(a,0,4,5,30);
    SkillsCheck(gSkillsRows.Get(a,0)==5 && !SkillsAccountPurchase(a,0,5,6,30),"configured maximum prevents sixth level");
    SkillsCheck(gSkillsRows.Get(b,SKILL_SPENT)==0 && gSkillsRows.Get(b,0)==0,"other identity remains untouched by purchases");
    gSkillEnabled[0]=enabled;gSkillMaxLevel[0]=maxLevel;gSkillCost[0]=cost;
    delete gSkillsRows;delete gSkillsAccounts;gSkillsRows=savedRows;gSkillsAccounts=savedMap;

    int payment[MATERIALS],bag[MATERIALS];payment[0]=50;payment[1]=20;payment[2]=5;bag=payment;
    SkillsCheck(SkillsCanPay(payment,bag,7),"exact seven-material personal balance can pay");bag[2]=4;
    SkillsCheck(!SkillsCanPay(payment,bag,7) && bag[0]==50 && bag[1]==20 && bag[2]==4,"one missing copper rejects entire payment without partial debit");
    payment[6]=-1;bag[2]=5;SkillsCheck(!SkillsCanPay(payment,bag,7),"negative payment is never credit");

    SkillsCheck(FloatAbs(SkillsSpeedValue(1.0,5,0.01)-1.05)<0.0001 && SkillsSpeedValue(0.7,0,0.01)==0.7,"speed contribution is percent of baseline; zero level exact");
    SkillsCheck(FloatAbs(SkillsGravityValue(1.0,5,0.02)-0.9)<0.0001 && FloatAbs(SkillsGravityValue(0.8,5,0.02)-0.7)<0.0001,"gravity reduces scalar by 0.10 from either baseline");
    SkillsCheck(SkillsGravityValue(0.15,5,0.02)==0.1 && SkillsGravityValue(0.05,5,0.02)==0.05 && SkillsGravityValue(0.01,0,0.02)==0.01,"gravity floor respects below-floor external source and zero level");
    SkillsCheck(SkillsGravityValue(0.0,0,0.02)==0.0 && FloatAbs(SkillsGravityValue(0.0,5,0.02)-0.9)<0.0001,"unset gravity sentinel stays raw zero at level zero and uses effective one with skill");
    SkillsCheck(SkillsChanceResult(0.02,0.019) && !SkillsChanceResult(0.02,0.02) && !SkillsChanceResult(0.0,0.0),"deterministic immunity success/failure/zero branch");
    SkillsCheck(SkillsChanceResult(1.0,1.0) && !SkillsChanceResult(0.10,1.0) && !SkillsChanceResult(1.0,-0.1),"full probability succeeds even at inclusive RNG endpoint");
    float clockA,clockB;
    SkillsCheck(!SkillsPulseDue(10.0,60.0,clockA) && clockA==70.0 && !SkillsPulseDue(20.0,60.0,clockB) && clockB==80.0,"each source starts own full interval");
    SkillsCheck(SkillsPulseDue(70.0,60.0,clockA) && !SkillsPulseDue(70.0,60.0,clockB) && !SkillsPulseDue(70.0,60.0,clockA),"asynchronous sources and repeated same tick grant once");
    SkillsCheck(SkillsPulseDue(1000.0,60.0,clockA) && clockA==1060.0 && !SkillsPulseDue(1000.0,60.0,clockA),"long pause schedules one pulse from now without catch-up");
    clockA=0.0;SkillsCheck(!SkillsPulseDue(1000.0,60.0,clockA) && clockA==1060.0,"rebind/death/respec clock reset waits full interval");
    for(int sources=4;sources<=32;sources*=2)
    {
        int hp=50;for(int source=0;source<sources;source++)hp+=RestorationDelta(hp,110,5);
        int expected=50+sources*5;if(expected>110)expected=110;
        char label[100];Format(label,sizeof(label),"%d independent recovery sources add in order and clamp to actual 110 cap",sources);SkillsCheck(hp==expected,label);
    }
    SkillsCheck(RestorationDelta(50,102,2)==2 && RestorationDelta(0,102,2)==2 && RestorationDelta(101,102,5)==1,"new max-level grants only +2 current HP/armor and all recovery uses actual cap");
    SkillsPrivateConfigTests();
    SkillsPrivateWorkTests();
    PrintToServer("[V080SkillsPrivate] RESULT passed=%d failed=%d skipped=%d live_entities_changed=0 live_accounts_changed=0",tSkillsPassed,tSkillsFailed,tSkillsSkipped);
    return Plugin_Handled;
}
