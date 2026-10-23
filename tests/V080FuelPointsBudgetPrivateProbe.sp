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

public Plugin myinfo={name="V080 Fuel Points Budget Assertions",author="H-AN",description="Private day rate, caps and unused point ledger tests only",version="0.8.0-test",url=""};
int tPassed,tFailed;
public APLRes AskPluginLoad2(Handle myself,bool late,char[] error,int maxlength)
{
    MarkNativeAsOptional("Han_IsZombie");MarkNativeAsOptional("Han_GetZombieName");MarkNativeAsOptional("Han_GetZombieCount");MarkNativeAsOptional("Han_GetZombieByIndex");
    MarkNativeAsOptional("Han_SetZombieTarget");MarkNativeAsOptional("Han_UnlockZombie");MarkNativeAsOptional("Han_SetCustomAnimationEx");MarkNativeAsOptional("Han_SafeDamageZombie");
    return APLRes_Success;
}
public void OnPluginStart(){RegServerCmd("zsh_v080_fuel_points_budget_private",BudgetPrivateCommand);}
void BP(bool result,const char[] text)
{
    if(result)tPassed++;else tFailed++;
    PrintToServer("[FuelPointsBudgetPrivate] %s %s",result ? "PASS" : "FAIL",text);
}
void BudgetTests()
{
    gSkillsConfigReady=false;gSkillsPointsPerDay=1;SkillsBudgetReset();gSurvivalDayLimit=30;
    gSkillsConfigReady=true;SkillsBudgetConfigReady();gEnabled=true;gCoreReady=true;gPhase=Phase_Day;gDay=1;
    BP(SkillsQuota()==1 && gSkillsHighestDay==1,"day one books configured one point once");
    BP(SkillsQuota()==1,"repeated observation cannot duplicate day one");
    gDay=5;BP(SkillsQuota()==5,"first five days at one point book exactly five");
    ConVar rate=CreateConVar("sm_zsh_private_test_skill_rate","3","Isolated assertion input; never production cvar",0,true,-1.0,true,1000000.0);rate.IntValue=3;
    SkillsDailyRateChanged(rate,"1","3");
    BP(gSkillsEarned==5 && SkillsDailyRate()==3,"switch after day five preserves the five already earned");
    gDay=6;BP(SkillsQuota()==8,"sixth new day grants three rather than recomputing eighteen");
    BP(gSkillsRateSchedule.Length==2 && gSkillsRateSchedule.Get(1,0)==6 && gSkillsRateSchedule.Get(1,1)==3,"sparse schedule records rate three from day six");
    gDay=2;BP(SkillsQuota()==8 && gSkillsHighestDay==6,"lowering day cannot remove points or highest-day identity");
    for(int day=3;day<=6;day++){gDay=day;BP(SkillsQuota()==8,"replayed old day never grants another award");}
    gDay=7;BP(SkillsQuota()==11,"first new seventh day earns latest rate only once");
    gDay=9;rate.IntValue=2;SkillsDailyRateChanged(rate,"3","2");
    BP(gSkillsEarned==17 && gSkillsHighestDay==9,"rate change settles already reached unobserved days at old rate");
    gDay=10;BP(SkillsQuota()==19,"new rate two begins at day ten");
    rate.IntValue=0;SkillsDailyRateChanged(rate,"2","0");gDay=12;
    BP(SkillsQuota()==19 && gSkillsHighestDay==12,"zero rate consumes new day eligibility without awarding points");
    rate.IntValue=3;SkillsDailyRateChanged(rate,"0","3");gDay=13;
    BP(SkillsQuota()==22,"resume cannot retroactively recover zero-rate days");
    rate.IntValue=-1;SkillsDailyRateChanged(rate,"3","-1");gDay=14;
    BP(SkillsQuota()==23 && SkillsDailyRate()==1,"minus one resumes configured rate only for next new day");
    gDay=40;BP(SkillsQuota()==39,"finite day thirty cap books only remaining eligible days");
    BP(SkillsAccrueDays(5,5,6,0,3)==8,"unlimited campaign uses the same historical accumulation");
    BP(SkillsAccrueDays(5,5,2000000000,0,1000000)==SKILL_CELL_LIMIT,"huge day jump saturates without loops or overflow");
    BP(SkillsAccrueDays(SKILL_CELL_LIMIT-1,5,6,0,3)==SKILL_CELL_LIMIT,"last available currency unit saturates safely");
    BP(SkillsAccrueDays(5,6,5,0,3)==5 && SkillsAccrueDays(5,5,5,0,3)==5,"old/equal days cannot alter the ledger");
    BP(SkillsAccrueDays(5,5,6,0,0)==5 && SkillsAccrueDays(5,5,6,-1,3)==5,"zero award and invalid campaign cannot produce negative currency");
    BP(SkillsAccrueDays(0,0,3,30,4)==12,"override selected before first day applies from first day");
    gSkillsConfigReady=false;gEnabled=false;delete rate;
}
void AccountTests()
{
    gSkillsMapSession=101;gSkillsAccounts=new StringMap();gSkillsRows=new ArrayList(SKILL_ACCOUNT_CELLS);
    int a=SkillsFindAccount("76561198000000001"),b=SkillsFindAccount("76561198000000002"),actual;
    gSkillEnabled[0]=true;gSkillMaxLevel[0]=10;gSkillCost[0]=1;gSkillAmount[0]=0.02;
    BP(SkillsAccountBudget(a,5)==5 && gSkillsRows.Get(a,SKILL_ADMIN)==0,"new player inherits exact historical quota with no private adjustment");
    BP(SkillsAccountPurchase(a,0,0,1,5) && gSkillsRows.Get(a,SKILL_SPENT)==1,"purchase spends original historical quota");
    BP(SkillsAccountAdjust(a,10,2,5,actual) && actual==10 && SkillsAccountBudget(a,5)==15,"admin addition creates separate personal budget");
    BP(gSkillsRows.Get(a,0)==1 && gSkillsRows.Get(a,SKILL_SPENT)==1,"admin addition never changes purchased skill or spent cost");
    BP(!SkillsAccountAdjust(a,10,2,5,actual) && actual==0,"stale revision cannot replay admin adjustment");
    BP(SkillsAccountAdjust(a,-100,3,5,actual) && actual==-14 && SkillsAccountBudget(a,5)==1,"excess removal clamps to all fourteen unused points");
    BP(gSkillsRows.Get(a,0)==1 && gSkillsRows.Get(a,SKILL_SPENT)==1 && gSkillsRows.Get(a,SKILL_ADMIN)==-4,"removal preserves spent skill with signed historical adjustment");
    BP(!SkillsAccountAdjust(a,-1,4,5,actual) && actual==0 && gSkillsRows.Get(a,SKILL_REVISION)==4,"zero unused balance refuses debit without revision mutation");
    BP(!SkillsAccountPurchase(a,0,1,4,5),"admin removal actually prevents spending removed unused points");
    BP(SkillsAccountBudget(a,8)-gSkillsRows.Get(a,SKILL_SPENT)==3,"next earned day grants three after all prior unused points removed");
    gSkillsRows.Set(a,0,0);gSkillsRows.Set(a,0,SKILL_SPENT);
    BP(SkillsAccountBudget(a,8)==4 && gSkillsRows.Get(a,SKILL_ADMIN)==-4,"respec account reset refunds original spent point while retaining admin removal");
    BP(SkillsFindAccount("76561198000000001")==a && gSkillsRows.Get(a,SKILL_ADMIN)==-4,"same authenticated Steam row retains admin adjustment on reconnect");
    BP(SkillsAccountBudget(b,8)==8 && gSkillsRows.Get(b,SKILL_ADMIN)==0,"different identity never inherits another player's admin adjustment");
    int originalEpoch=gEpoch;gEpoch++;BP(SkillsFindAccount("76561198000000001")==a,"round changes retain personal points ledger");gEpoch=originalEpoch;
    gSkillsMapSession++;BP(!SkillsAccountAdjust(a,1,4,8,actual),"old map account cannot receive points in new session");gSkillsMapSession--;
    BP(!SkillsAccountAdjust(-1,1,1,8,actual) && !SkillsAccountAdjust(99,1,1,8,actual),"invalid account indices are rejected");
    BP(!SkillsAccountAdjust(a,0,4,8,actual) && !SkillsAccountAdjust(a,-2147483647,4,8,actual),"zero and out of bounded signed adjustment are rejected");
    BP(SkillsAccountAdjust(b,SKILL_CELL_LIMIT,1,5,actual) && actual==SKILL_CELL_LIMIT-5 && SkillsAccountBudget(b,5)==SKILL_CELL_LIMIT,"admin credit saturates at safe personal currency ceiling");
    BP(SkillsAccountBudget(b,SKILL_CELL_LIMIT)==SKILL_CELL_LIMIT,"future earned day cannot overflow a large private credit");
    BP(SkillsAccountAdjust(b,-1,2,SKILL_CELL_LIMIT,actual) && actual==-1 && SkillsAccountBudget(b,SKILL_CELL_LIMIT)==SKILL_CELL_LIMIT-1,"debit after hidden saturated credit removes actual visible point");
    int before=gSkillsRows.Get(a,SKILL_REVISION);
    BP(!SkillsAdminAdjust(0,0,1,a,before,actual) && actual==0,"public admin mutation rejects invalid actor and target without any live writes");
    delete gSkillsRows;gSkillsRows=null;delete gSkillsAccounts;gSkillsAccounts=null;
}
void CapTests()
{
    for(int s=0;s<SKILL_COUNT;s++){gSkillEnabled[s]=true;gSkillMaxLevel[s]=10;gSkillAmount[s]=0.02;}
    gSkillsImmunityCap=0.10;
    BP(SkillsEffectiveMaximum(Skill_Gravity,0.8)==10,"existing gravity 0.02 amount keeps ten useful levels at baseline 0.8");
    gSkillAmount[Skill_Gravity]=0.1;
    BP(SkillsEffectiveMaximum(Skill_Gravity,0.8)==7,"configured gravity 0.1 amount caps baseline 0.8 at seven useful levels");
    BP(SkillsEffectiveMaximum(Skill_Gravity,0.0)==9,"Source raw zero sentinel means baseline one when calculating gravity cap");
    BP(SkillsEffectiveMaximum(Skill_Gravity,0.1)==0 && SkillsEffectiveMaximum(Skill_Gravity,0.05)==0,"at/below external gravity floor forbids useless purchases");
    BP(SkillsEffectiveMaximum(Skill_Gravity,0.15)==1,"last partial gravity improvement may reach the floor");
    BP(SkillsEffectiveMaximum(Skill_Immunity,1.0)==5,"existing ten percent immunity cap has five useful 0.02 levels");
    gSkillAmount[Skill_Immunity]=0.03;BP(SkillsEffectiveMaximum(Skill_Immunity,1.0)==4,"last partial immunity improvement can reach its probability cap");
    gSkillsImmunityCap=0.0;BP(SkillsEffectiveMaximum(Skill_Immunity,1.0)==0,"zero immunity probability cap forbids no-effect upgrades");
    gSkillAmount[Skill_Gather]=0.0;BP(SkillsEffectiveMaximum(Skill_Gather,1.0)==0,"configured zero-contribution skill has no purchasable levels");
    gSkillEnabled[Skill_Repair]=false;BP(SkillsEffectiveMaximum(Skill_Repair,1.0)==0,"disabled skill cannot advertise useful levels");
    for(int s=0;s<SKILL_COUNT;s++)if(s!=view_as<int>(Skill_Gravity) && s!=view_as<int>(Skill_Immunity) && s!=view_as<int>(Skill_Gather) && s!=view_as<int>(Skill_Repair))
        BP(SkillsEffectiveMaximum(s,1.0)==10,"ordinary enabled skill retains all ten configured useful levels");
}
KeyValues BudgetCandidate()
{
    KeyValues kv=new KeyValues("ShelterSkills");kv.SetNum("schema_version",1);
    kv.JumpToKey("Rules",true);kv.SetNum("enabled",1);kv.SetNum("points_per_day",1);kv.GoBack();
    kv.JumpToKey("Skills",true);
    for(int s=0;s<SKILL_COUNT;s++)
    {
        kv.JumpToKey(gSkillID[s],true);kv.SetNum("enabled",1);kv.SetNum("max_level",10);kv.SetNum("point_cost",1);
        kv.SetFloat("amount_per_level",s==view_as<int>(Skill_Speed) ? 0.01 : (s==view_as<int>(Skill_Gather) || s==view_as<int>(Skill_Gravity) || s==view_as<int>(Skill_Immunity) ? 0.02 : 1.0));
        if(s==view_as<int>(Skill_Immunity))kv.SetFloat("chance_cap",0.10);
        if(s>=view_as<int>(Skill_Medic) && s<=view_as<int>(Skill_SelfArmor))
        {kv.SetFloat("interval_seconds",60.0);if(s<=view_as<int>(Skill_Artisan))kv.SetFloat("radius_units",256.0);}
        kv.GoBack();
    }
    kv.GoBack();kv.JumpToKey("Respec",true);
    for(int m=0;m<gMaterialCount;m++)kv.SetNum(gMaterials[m].id,0);
    kv.Rewind();return kv;
}
bool BudgetParse(KeyValues kv)
{
    bool enabled,enabledSkill[SKILL_COUNT];int daily,maximum[SKILL_COUNT],cost[SKILL_COUNT],respec[MATERIALS];
    float amount[SKILL_COUNT],radius[SKILL_COUNT],interval[SKILL_COUNT],cap;
    return SkillsParseConfig(kv,enabled,daily,enabledSkill,maximum,cost,amount,radius,interval,cap,respec);
}
void BudgetConfigTests()
{
    static const char ids[][]={"wood","iron","copper","silver","gold","plague_core","credits"};
    gMaterialCount=sizeof(ids);for(int m=0;m<gMaterialCount;m++)strcopy(gMaterials[m].id,sizeof(gMaterials[].id),ids[m]);
    KeyValues kv=BudgetCandidate();BP(BudgetParse(kv),"thirteen enabled level ten skill config validates unchanged per-level effects");delete kv;
    kv=BudgetCandidate();kv.JumpToKey("Rules");kv.SetNum("points_per_day",0);BP(BudgetParse(kv),"daily config zero is valid deliberate future-award pause");delete kv;
    kv=BudgetCandidate();kv.JumpToKey("Rules");kv.SetNum("points_per_day",1000000);BP(BudgetParse(kv),"daily config maximum million validates");delete kv;
    kv=BudgetCandidate();kv.JumpToKey("Rules");kv.SetNum("points_per_day",-1);BP(!BudgetParse(kv),"negative daily config rejected; minus one is only a cvar sentinel");delete kv;
    kv=BudgetCandidate();kv.JumpToKey("Rules");kv.SetNum("points_per_day",1000001);BP(!BudgetParse(kv),"over-limit daily config rejected");delete kv;
    kv=BudgetCandidate();kv.JumpToKey("Rules");kv.SetString("points_per_day","2.5");BP(!BudgetParse(kv),"fractional daily currency config rejected");delete kv;
    kv=BudgetCandidate();kv.JumpToKey("Skills");kv.JumpToKey("gather");kv.SetNum("enabled",2);BP(!BudgetParse(kv),"malformed enabled flag cannot enter active config");delete kv;
    kv=BudgetCandidate();kv.JumpToKey("Skills");kv.JumpToKey("gravity");kv.SetString("amount_per_level","nan");BP(!BudgetParse(kv),"non-finite gravity amount rejected");delete kv;
    kv=BudgetCandidate();kv.JumpToKey("Skills");kv.JumpToKey("immunity");kv.SetFloat("chance_cap",1.01);BP(!BudgetParse(kv),"probability above one rejected");delete kv;
}
public Action BudgetPrivateCommand(int args)
{
    tPassed=0;tFailed=0;BudgetTests();AccountTests();CapTests();BudgetConfigTests();
    PrintToServer("[FuelPointsBudgetPrivate] RESULT passed=%d failed=%d skipped=0 live_entities_changed=0 live_accounts_changed=0",tPassed,tFailed);
    return Plugin_Handled;
}
