// Per-map SteamID64 accounts deliberately outlive round epochs. No disk persistence.
#define SKILL_COUNT 13
#define SKILL_ACCOUNT_CELLS 17
#define SKILL_SPENT 13
#define SKILL_REVISION 14
#define SKILL_SESSION 15
#define SKILL_ADMIN 16
#define SKILL_CELL_LIMIT 2000000000

enum SkillIndex { Skill_Gather,Skill_Repair,Skill_Construct,Skill_Speed,Skill_Gravity,Skill_Immunity,
    Skill_Medic,Skill_Logistics,Skill_Artisan,Skill_SelfHeal,Skill_SelfArmor,Skill_MaxHP,Skill_MaxArmor }

char gSkillID[SKILL_COUNT][32]={"gather","repair","construct","speed","gravity","immunity",
    "medic","logistics","artisan","selfheal","selfarmor","maxhp","maxarmor"};
char gSkillName[SKILL_COUNT][32]={"采集强化","维修强化","施工强化","体能强化","跳跃强化","黑夜免疫",
    "军医","后勤","工匠","自我修复","护甲修理","生命上限","护甲上限"};
bool gSkillEnabled[SKILL_COUNT],gSkillsEnabled,gSkillsConfigReady;
int gSkillMaxLevel[SKILL_COUNT],gSkillCost[SKILL_COUNT],gSkillRespecCost[MATERIALS];
float gSkillAmount[SKILL_COUNT],gSkillRadius[SKILL_COUNT],gSkillInterval[SKILL_COUNT];
float gSkillsImmunityCap=0.10;
int gSkillsPointsPerDay=1,gSkillsHighestDay,gSkillsMapSession,gSkillsConfigRevision;
StringMap gSkillsAccounts;
ArrayList gSkillsRows;
int gSkillsBinding[MAXPLAYERS+1],gSkillsSerial[MAXPLAYERS+1]; // Stored index + 1.
char gSkillsSteam[MAXPLAYERS+1][32];
float gSkillsNextPulse[MAXPLAYERS+1][5];
int gSkillsPulseEpoch[MAXPLAYERS+1],gSkillsSpawnTicket[MAXPLAYERS+1];
bool gSkillsSpawnPending[MAXPLAYERS+1],gSkillsBusy[MAXPLAYERS+1];
bool gSkillsSpawnAwardPending[MAXPLAYERS+1];
int gSkillsSpawnAwardEpoch[MAXPLAYERS+1];
bool gSkillsAttributes[MAXPLAYERS+1],gSkillsGravityConflict[MAXPLAYERS+1];
float gSkillsSpeedBase[MAXPLAYERS+1],gSkillsSpeedApplied[MAXPLAYERS+1];
float gSkillsGravityBase[MAXPLAYERS+1],gSkillsGravityApplied[MAXPLAYERS+1];
int gSkillsHPBase[MAXPLAYERS+1],gSkillsHPApplied[MAXPLAYERS+1],gSkillsArmorBase[MAXPLAYERS+1];
int gSkillsPulseCount,gSkillsPulseTargetCount;

#include "SkillsConfig.inc"
#include "SkillsBudget.inc"

bool SkillsSteam2To64(const char[] steam2,char[] steam64,int size)
{
    steam64[0]=0;int length=strlen(steam2);
    if(size<18 || length<11 || length>20 || StrContains(steam2,"STEAM_")!=0 || (steam2[6]!='0' && steam2[6]!='1')
        || steam2[7]!=':' || (steam2[8]!='0' && steam2[8]!='1') || steam2[9]!=':')return false;
    int halfAccount;
    for(int i=10;i<length;i++)
    {
        int digit=steam2[i]-'0';if(digit<0 || digit>9 || halfAccount>(2147483647-digit)/10)return false;
        halfAccount=halfAccount*10+digit;
    }
    int parity=steam2[8]-'0';if(halfAccount==0 && parity==0)return false;
    // Public individual SteamID64 = 76561197960265728 + 2*Z + Y.
    // Decimal carry keeps the unsigned account ID and 64-bit result out of cells.
    strcopy(steam64,size,"76561197960265728");int carry=parity,source=length-1;
    for(int out=16;out>=0;out--)
    {
        int sum=steam64[out]-'0'+carry;if(source>=10)sum+=2*(steam2[source--]-'0');
        steam64[out]='0'+sum%10;carry=sum/10;
    }
    return carry==0 && source<10;
}

bool SkillsVerifiedSteam64(int client,char[] steam,int size)
{
    steam[0]=0;if(!IsClientAuthorized(client))return false;
    if(GetClientAuthId(client,AuthId_SteamID64,steam,size,true) && strlen(steam)==17)return true;
    char verified[32];if(!GetClientAuthId(client,AuthId_Steam2,verified,sizeof(verified),true))return false;
    return SkillsSteam2To64(verified,steam,size);
}

bool SkillsIdentity(int client)
{
    if(client<1 || client>MaxClients || !IsClientInGame(client) || !IsClientAuthorized(client)
        || IsFakeClient(client) || IsClientSourceTV(client) || IsClientReplay(client)
        || gSkillsSerial[client]!=GetClientSerial(client) || gSkillsBinding[client]<1 || gSkillsRows==null)return false;
    int index=gSkillsBinding[client]-1;if(index>=gSkillsRows.Length)return false;
    char steam[32];if(!SkillsVerifiedSteam64(client,steam,sizeof(steam))
        || !StrEqual(steam,gSkillsSteam[client]))return false;
    return gSkillsRows.Get(index,SKILL_SESSION)==gSkillsMapSession;
}

bool SkillsActor(int client)
{
    return gSkillsEnabled && gSkillsConfigReady && Running() && !gTerminal && LiveHuman(client)
        && !gSkillsSpawnPending[client] && SkillsIdentity(client);
}

int SkillsAccountIndex(int client){return SkillsIdentity(client) ? gSkillsBinding[client]-1 : -1;}
int SkillsAccountRevision(int client){int account=SkillsAccountIndex(client);return account<0 ? -1 : gSkillsRows.Get(account,SKILL_REVISION);}
int SkillsSpent(int client){int account=SkillsAccountIndex(client);return account<0 ? 0 : gSkillsRows.Get(account,SKILL_SPENT);}
int SkillsBalance(int client)
{
    int quota=SkillsQuota(),account=SkillsAccountIndex(client);if(account<0)return 0;
    int budget=SkillsAccountBudget(account,quota),spent=gSkillsRows.Get(account,SKILL_SPENT);
    return budget>=spent ? budget-spent : 0;
}
int SkillsLevel(int client,int skill)
{
    if(skill<0 || skill>=SKILL_COUNT || !gSkillsEnabled || !gSkillEnabled[skill])return 0;
    int account=SkillsAccountIndex(client);return account<0 ? 0 : gSkillsRows.Get(account,skill);
}

void SkillsClearPulses(int client)
{
    for(int p=0;p<5;p++)gSkillsNextPulse[client][p]=0.0;
    gSkillsPulseEpoch[client]=gEpoch;
}

void SkillsBindClient(int client)
{
    if(!gSkillsConfigReady || gSkillsRows==null || gSkillsAccounts==null || client<1 || client>MaxClients
        || !IsClientInGame(client) || !IsClientAuthorized(client) || IsFakeClient(client)
        || IsClientSourceTV(client) || IsClientReplay(client))return;
    char steam[32];if(!SkillsVerifiedSteam64(client,steam,sizeof(steam)))return;
    // Authorization and in-server callbacks can arrive in either order.
    if(SkillsIdentity(client)){SkillsObserveDay();SkillsTrySpawnAward(client);return;}
    SkillsRemoveAttributes(client);
    int account=SkillsFindAccount(steam);
    gSkillsBinding[client]=account+1;gSkillsSerial[client]=GetClientSerial(client);
    strcopy(gSkillsSteam[client],sizeof(gSkillsSteam[]),steam);SkillsClearPulses(client);SkillsObserveDay();
    // Binding is never a health/armor award, even on a reconnect.
    if(SkillsActor(client) && !gSkillsSpawnPending[client]){SkillsApplyAttributes(client);SkillsTrySpawnAward(client);}
}

int SkillsFindAccount(const char[] steam)
{
    int account;if(gSkillsAccounts.GetValue(steam,account))return account;
    int row[SKILL_ACCOUNT_CELLS];row[SKILL_REVISION]=1;row[SKILL_SESSION]=gSkillsMapSession;
    account=gSkillsRows.PushArray(row,sizeof(row));gSkillsAccounts.SetValue(steam,account);return account;
}

void SkillsMapStart()
{
    SkillsUnload();gSkillsMapSession++;if(gSkillsMapSession<1)gSkillsMapSession=1;
    gSkillsConfigReady=false;gSkillsConfigRevision=0;SkillsBudgetReset();
    gSkillsAccounts=new StringMap();gSkillsRows=new ArrayList(SKILL_ACCOUNT_CELLS);
}

void SkillsMapEnd(){SkillsUnload();gSkillsConfigReady=false;}

void SkillsDisconnectClient(int client)
{
    if(client<1 || client>MaxClients)return;
    SkillsRemoveAttributes(client);SkillsClearPulses(client);gSkillsBinding[client]=0;gSkillsSerial[client]=0;
    gSkillsSteam[client][0]=0;gSkillsBusy[client]=false;gSkillsSpawnPending[client]=false;gSkillsSpawnAwardPending[client]=false;gSkillsSpawnTicket[client]++;
}

void SkillsUnload()
{
    for(int client=1;client<=MaxClients;client++)SkillsDisconnectClient(client);
    delete gSkillsAccounts;gSkillsAccounts=null;delete gSkillsRows;gSkillsRows=null;
}

// Each purchase must still improve the actual effect. Configuration retains its
// declared ceiling; external gravity changes may reduce the current useful cap.
int SkillsEffectiveMaximum(int skill,float baseline)
{
    if(skill<0 || skill>=SKILL_COUNT || !gSkillEnabled[skill] || gSkillAmount[skill]<=0.0)return 0;
    int maximum=gSkillMaxLevel[skill];float limit;
    if(skill==view_as<int>(Skill_Immunity))limit=gSkillsImmunityCap;
    else if(skill==view_as<int>(Skill_Gravity))
    {
        if(baseline==0.0)baseline=1.0;
        limit=baseline-0.1;
    }
    else return maximum;
    if(limit<=0.0)return 0;
    // Ceil deliberately allows the final partial improvement to reach the cap.
    float ratio=limit/gSkillAmount[skill];
    int useful=RoundToCeil(ratio-0.00001);if(useful<0)useful=0;
    return useful<maximum ? useful : maximum;
}

int SkillsMaximumLevel(int client,int skill)
{
    float baseline=1.0;
    if(skill==view_as<int>(Skill_Gravity) && client>=1 && client<=MaxClients && IsClientInGame(client))
    {
        baseline=GetEntityGravity(client);
        if(gSkillsAttributes[client] && FloatAbs(baseline-gSkillsGravityApplied[client])<0.0001)
            baseline=gSkillsGravityBase[client];
    }
    return SkillsEffectiveMaximum(skill,baseline);
}

int SkillsReadMaxHealth(int client)
{
    int value;
    if(HasEntProp(client,Prop_Data,"m_iMaxHealth"))value=GetEntProp(client,Prop_Data,"m_iMaxHealth");
    else if(HasEntProp(client,Prop_Send,"m_iMaxHealth"))value=GetEntProp(client,Prop_Send,"m_iMaxHealth");
    return value>0 ? value : 100;
}

void SkillsWriteMaxHealth(int client,int maximum)
{
    if(HasEntProp(client,Prop_Data,"m_iMaxHealth"))SetEntProp(client,Prop_Data,"m_iMaxHealth",maximum);
    else if(HasEntProp(client,Prop_Send,"m_iMaxHealth"))SetEntProp(client,Prop_Send,"m_iMaxHealth",maximum);
}

int SkillsHealthLimit(int client){return SkillsReadMaxHealth(client);}
int SkillsArmorLimit(int client)
{
    int base=gSkillsAttributes[client] ? gSkillsArmorBase[client] : 100;
    return base+RoundToFloor(float(SkillsLevel(client,Skill_MaxArmor))*gSkillAmount[Skill_MaxArmor]);
}

float SkillsSpeedValue(float base,int level,float amount)
{
    return level>0 ? base*(1.0+float(level)*amount) : base;
}

float SkillsGravityValue(float base,int level,float amount)
{
    if(level<1)return base;
    if(base==0.0)base=1.0; // Source's unset personal-gravity sentinel means normal gravity.
    if(base<0.1)return base;
    float value=base-float(level)*amount;return value<0.1 ? 0.1 : value;
}

void SkillsApplyAttributes(int client)
{
    if(!SkillsActor(client) || gSkillsSpawnPending[client])return;
    float speed=GetEntPropFloat(client,Prop_Data,"m_flLaggedMovementValue"),gravity=GetEntityGravity(client);
    int maximum=SkillsReadMaxHealth(client);
    if(!gSkillsAttributes[client])
    {
        gSkillsSpeedBase[client]=speed;gSkillsGravityBase[client]=gravity;
        gSkillsHPBase[client]=maximum;gSkillsArmorBase[client]=100;gSkillsAttributes[client]=true;
    }
    else
    {
        // An external writer replaced the final property. Treat its new value as
        // the baseline; unchanged own output is never multiplied a second time.
        if(FloatAbs(speed-gSkillsSpeedApplied[client])>0.0001)gSkillsSpeedBase[client]=speed;
        if(FloatAbs(gravity-gSkillsGravityApplied[client])>0.0001)gSkillsGravityBase[client]=gravity;
        if(maximum!=gSkillsHPApplied[client])gSkillsHPBase[client]=maximum;
    }
    int speedLevel=SkillsLevel(client,Skill_Speed),gravityLevel=SkillsLevel(client,Skill_Gravity);
    gSkillsSpeedApplied[client]=SkillsSpeedValue(gSkillsSpeedBase[client],speedLevel,gSkillAmount[Skill_Speed]);
    gSkillsGravityApplied[client]=SkillsGravityValue(gSkillsGravityBase[client],gravityLevel,gSkillAmount[Skill_Gravity]);
    if(gravityLevel>0 && gSkillsGravityBase[client]>0.0 && gSkillsGravityBase[client]<0.1 && !gSkillsGravityConflict[client])
    {LogMessage("[ShelterSkills] gravity source conflict serial=%d baseline=%.6f; extra reduction suspended",GetClientSerial(client),gSkillsGravityBase[client]);gSkillsGravityConflict[client]=true;}
    if(gSkillsGravityBase[client]==0.0 || gSkillsGravityBase[client]>=0.1)gSkillsGravityConflict[client]=false;
    gSkillsHPApplied[client]=gSkillsHPBase[client]+RoundToFloor(float(SkillsLevel(client,Skill_MaxHP))*gSkillAmount[Skill_MaxHP]);
    SetEntPropFloat(client,Prop_Data,"m_flLaggedMovementValue",gSkillsSpeedApplied[client]);
    SetEntityGravity(client,gSkillsGravityApplied[client]);SkillsWriteMaxHealth(client,gSkillsHPApplied[client]);
}

void SkillsRemoveAttributes(int client)
{
    if(!gSkillsAttributes[client])return;
    if(IsClientInGame(client) && gSkillsSerial[client]==GetClientSerial(client))
    {
        // Undo only values still owned by us, preserving later external writes.
        if(FloatAbs(GetEntPropFloat(client,Prop_Data,"m_flLaggedMovementValue")-gSkillsSpeedApplied[client])<0.0001)
            SetEntPropFloat(client,Prop_Data,"m_flLaggedMovementValue",gSkillsSpeedBase[client]);
        if(FloatAbs(GetEntityGravity(client)-gSkillsGravityApplied[client])<0.0001)SetEntityGravity(client,gSkillsGravityBase[client]);
        if(SkillsReadMaxHealth(client)==gSkillsHPApplied[client])SkillsWriteMaxHealth(client,gSkillsHPBase[client]);
        if(IsPlayerAlive(client))
        {
            int health=GetClientHealth(client),maximum=SkillsReadMaxHealth(client);
            if(health>maximum)SetEntityHealth(client,maximum);
            int armor=GetEntProp(client,Prop_Send,"m_ArmorValue");
            if(SkillsLevel(client,Skill_MaxArmor)>0 && armor>gSkillsArmorBase[client])
                SetEntProp(client,Prop_Send,"m_ArmorValue",gSkillsArmorBase[client]);
        }
    }
    gSkillsAttributes[client]=false;gSkillsGravityConflict[client]=false;
}

void SkillsSpawnClient(int client)
{
    if(client<1 || client>MaxClients || !IsClientInGame(client) || IsFakeClient(client))return;
    // Remove stale previous-life ownership without overwriting freshly reset fields.
    SkillsRemoveAttributes(client);SkillsClearPulses(client);
    gSkillsSpawnAwardPending[client]=true;gSkillsSpawnAwardEpoch[client]=gEpoch;
    gSkillsSpawnPending[client]=true;int ticket=++gSkillsSpawnTicket[client];
    DataPack pack=new DataPack();pack.WriteCell(GetClientSerial(client));pack.WriteCell(ticket);pack.WriteCell(gEpoch);pack.WriteCell(gSkillsMapSession);
    RequestFrame(SkillsFinishSpawn,pack);
}

public void SkillsFinishSpawn(any value)
{
    DataPack pack=view_as<DataPack>(value);pack.Reset();int serial=pack.ReadCell(),ticket=pack.ReadCell(),epoch=pack.ReadCell(),session=pack.ReadCell();delete pack;
    int client=GetClientFromSerial(serial);if(client<1 || ticket!=gSkillsSpawnTicket[client] || session!=gSkillsMapSession)return;
    gSkillsSpawnPending[client]=false;
    if(epoch!=gEpoch || !LiveHuman(client)){gSkillsSpawnAwardPending[client]=false;return;}
    SkillsBindClient(client);if(!SkillsActor(client))return;
    SkillsApplyAttributes(client);SkillsTrySpawnAward(client);
}

void SkillsTrySpawnAward(int client)
{
    if(!gSkillsSpawnAwardPending[client])return;
    if(gSkillsSpawnAwardEpoch[client]!=gEpoch || !LiveHuman(client)){gSkillsSpawnAwardPending[client]=false;return;}
    if(!SkillsActor(client))return; // Late auth/running transition retains the actual-spawn ticket.
    gSkillsSpawnAwardPending[client]=false; // Consume before the actual delta, never on an ordinary rebind.
    SkillsApplyAttributes(client);
    int healthBonus=RoundToFloor(float(SkillsLevel(client,Skill_MaxHP))*gSkillAmount[Skill_MaxHP]);
    int armorBonus=RoundToFloor(float(SkillsLevel(client,Skill_MaxArmor))*gSkillAmount[Skill_MaxArmor]);
    RestorationRestoreHealth(client,healthBonus);RestorationRestoreArmor(client,armorBonus);
}

int SkillsGatherBonus(int client,int base)
{
    if(base<1 || !SkillsActor(client))return base;
    int bonus=RoundToNearest(float(SkillsLevel(client,Skill_Gather))*gSkillAmount[Skill_Gather]*float(GATHER_MULTIPLIER_SCALE));
    return bonus>SKILL_CELL_LIMIT-base ? SKILL_CELL_LIMIT : base+bonus;
}

int SkillsWorkHP(int client,int base,bool constructing)
{
    if(base<1 || !SkillsActor(client))return base;
    int skill=constructing ? view_as<int>(Skill_Construct) : view_as<int>(Skill_Repair);
    int bonus=RoundToFloor(float(SkillsLevel(client,skill))*gSkillAmount[skill]);
    return bonus>SKILL_CELL_LIMIT-base ? SKILL_CELL_LIMIT : base+bonus;
}

bool SkillsExposureImmune(int client)
{
    if(!SkillsActor(client))return false;
    float chance=float(SkillsLevel(client,Skill_Immunity))*gSkillAmount[Skill_Immunity];
    if(chance>gSkillsImmunityCap)chance=gSkillsImmunityCap;
    return chance>0.0 && SkillsChanceResult(chance,GetRandomFloat(0.0,1.0));
}

bool SkillsChanceResult(float chance,float sample){return chance>0.0 && sample>=0.0 && sample<=1.0 && (chance>=1.0 || sample<chance);}

bool SkillsAccountPurchase(int account,int skill,int expectedLevel,int expectedRevision,int quota)
{
    if(gSkillsRows==null || account<0 || account>=gSkillsRows.Length || skill<0 || skill>=SKILL_COUNT
        || !gSkillEnabled[skill] || expectedLevel<0 || expectedLevel>=gSkillMaxLevel[skill] || gSkillCost[skill]<1
        || gSkillAmount[skill]<=0.0
        || (skill==view_as<int>(Skill_Immunity) && expectedLevel>=SkillsEffectiveMaximum(skill,1.0))
        || gSkillsRows.Get(account,SKILL_SESSION)!=gSkillsMapSession
        || gSkillsRows.Get(account,SKILL_REVISION)!=expectedRevision || gSkillsRows.Get(account,skill)!=expectedLevel)return false;
    int spent=gSkillsRows.Get(account,SKILL_SPENT),budget=SkillsAccountBudget(account,quota);
    if(budget<spent || budget-spent<gSkillCost[skill] || spent>SKILL_CELL_LIMIT-gSkillCost[skill]
        || expectedRevision<1 || expectedRevision>=SKILL_CELL_LIMIT)return false;
    gSkillsRows.Set(account,expectedRevision+1,SKILL_REVISION); // Consume first.
    gSkillsRows.Set(account,expectedLevel+1,skill);gSkillsRows.Set(account,spent+gSkillCost[skill],SKILL_SPENT);return true;
}

bool SkillsPurchase(int client,int skill,int expectedLevel,int expectedRevision)
{
    SkillsObserveDay();
    if(!SkillsActor(client) || gSkillsBusy[client] || skill<0 || skill>=SKILL_COUNT || !gSkillEnabled[skill]
        || expectedRevision!=SkillsAccountRevision(client) || expectedLevel!=SkillsLevel(client,skill)
        || expectedLevel>=SkillsMaximumLevel(client,skill) || SkillsBalance(client)<gSkillCost[skill])return false;
    int account=SkillsAccountIndex(client);
    gSkillsBusy[client]=true;
    int beforeHP=RoundToFloor(float(SkillsLevel(client,Skill_MaxHP))*gSkillAmount[Skill_MaxHP]);
    int beforeArmor=RoundToFloor(float(SkillsLevel(client,Skill_MaxArmor))*gSkillAmount[Skill_MaxArmor]);
    if(!SkillsAccountPurchase(account,skill,expectedLevel,expectedRevision,SkillsQuota())){gSkillsBusy[client]=false;return false;}
    SkillsApplyAttributes(client);
    if(skill==view_as<int>(Skill_MaxHP))RestorationRestoreHealth(client,RoundToFloor(float(expectedLevel+1)*gSkillAmount[skill])-beforeHP);
    if(skill==view_as<int>(Skill_MaxArmor))RestorationRestoreArmor(client,RoundToFloor(float(expectedLevel+1)*gSkillAmount[skill])-beforeArmor);
    if(skill>=view_as<int>(Skill_Medic) && skill<=view_as<int>(Skill_SelfArmor) && expectedLevel==0)
        gSkillsNextPulse[client][skill-view_as<int>(Skill_Medic)]=GetGameTime()+gSkillInterval[skill];
    gSkillsBusy[client]=false;return true;
}

bool SkillsRespec(int client,int expectedRevision)
{
    SkillsObserveDay();if(!SkillsActor(client) || gSkillsBusy[client] || expectedRevision!=SkillsAccountRevision(client)
        || expectedRevision>=SKILL_CELL_LIMIT || SkillsSpent(client)<1)return false;
    // Validate the complete personal payment before changing either ledger.
    if(!SkillsCanPay(gSkillRespecCost,gPlayerInventory[client],gMaterialCount))return false;
    gSkillsBusy[client]=true;int account=SkillsAccountIndex(client);
    gSkillsRows.Set(account,expectedRevision+1,SKILL_REVISION);
    if(!Spend(gSkillRespecCost,client)){gSkillsRows.Set(account,expectedRevision,SKILL_REVISION);gSkillsBusy[client]=false;return false;}
    SkillsRemoveAttributes(client);
    for(int skill=0;skill<SKILL_COUNT;skill++)gSkillsRows.Set(account,0,skill);
    gSkillsRows.Set(account,0,SKILL_SPENT);SkillsClearPulses(client);SkillsApplyAttributes(client);
    gSkillsBusy[client]=false;return true;
}

bool SkillsCanPay(const int cost[MATERIALS],const int inventory[MATERIALS],int count)
{
    if(count<1 || count>MATERIALS)return false;
    for(int m=0;m<count;m++)if(cost[m]<0 || inventory[m]<cost[m])return false;return true;
}

bool SkillsPulseDue(float now,float interval,float &deadline)
{
    if(deadline<=0.0){deadline=now+interval;return false;}
    if(now<deadline)return false;
    deadline=now+interval;return true;
}

void SkillsEmitPulse(int client,int skill,int targets)
{
    if(targets<1)return;
    int color[4]={145,255,170,190};
    if(skill==view_as<int>(Skill_Logistics) || skill==view_as<int>(Skill_SelfArmor)){color[0]=150;color[1]=205;color[2]=255;}
    else if(skill==view_as<int>(Skill_Artisan)){color[0]=255;color[1]=235;color[2]=150;}
    float center[3];GetClientAbsOrigin(client,center);center[2]+=16.0;
    float radius=gSkillRadius[skill];if(radius<24.0)radius=24.0;
    if(gBeamSprite>0){TE_SetupBeamRingPoint(center,8.0,radius*2.0,gBeamSprite,0,0,0,0.7,8.0,0.0,color,0,0);TE_SendToAll();}
    gSkillsPulseCount++;gSkillsPulseTargetCount+=targets;
}

int SkillsPulse(int client,int skill)
{
    if(!SkillsActor(client) || skill<view_as<int>(Skill_Medic) || skill>view_as<int>(Skill_SelfArmor))return 0;
    int amount=RoundToFloor(float(SkillsLevel(client,skill))*gSkillAmount[skill]);if(amount<1)return 0;
    int targets,serial=GetClientSerial(client),epoch=gEpoch,session=gSkillsMapSession,account=SkillsAccountIndex(client),revision=SkillsAccountRevision(client);
    if(skill==view_as<int>(Skill_SelfHeal)){if(RestorationRestoreHealth(client,amount)>0)targets++;}
    else if(skill==view_as<int>(Skill_SelfArmor)){if(RestorationRestoreArmor(client,amount)>0)targets++;}
    else
    {
        float origin[3];GetClientAbsOrigin(client,origin);float squared=gSkillRadius[skill]*gSkillRadius[skill];
        if(skill==view_as<int>(Skill_Artisan))
        {
            bool powerChanged;
            for(int b=0;b<MAX_BUILDINGS;b++)
            {
                if(!BuildingOperational(b) || Resolve(gBuildings[b].ref)<0 || GetVectorDistance(origin,gBuildings[b].origin,true)>squared)continue;
                if(RestorationRestoreBuilding(b,amount)>0){targets++;if(gBuildings[b].kind==Kind_Generator)powerChanged=true;}
            }
            if(powerChanged)RecalculatePower();
        }
        else for(int target=1;target<=MaxClients;target++)
        {
            if(target==client || !SkillsActor(target))continue;
            float position[3];GetClientAbsOrigin(target,position);if(GetVectorDistance(origin,position,true)>squared)continue;
            int delta=skill==view_as<int>(Skill_Medic) ? RestorationRestoreHealth(target,amount) : RestorationRestoreArmor(target,amount);
            if(delta>0)targets++;
        }
    }
    if(SkillsPulseContext(client,serial,epoch,session,account,revision))SkillsEmitPulse(client,skill,targets);
    return targets;
}

bool SkillsPulseContext(int client,int serial,int epoch,int session,int account,int revision)
{
    return epoch==gEpoch && session==gSkillsMapSession && SkillsActor(client) && GetClientSerial(client)==serial
        && SkillsAccountIndex(client)==account && SkillsAccountRevision(client)==revision;
}

void SkillsSuspend()
{
    for(int client=1;client<=MaxClients;client++){SkillsClearPulses(client);SkillsRemoveAttributes(client);}
}

void SkillsTick(float now)
{
    if(!gSkillsConfigReady || !gSkillsEnabled || !Running() || gTerminal){SkillsSuspend();return;}
    SkillsObserveDay();
    for(int client=1;client<=MaxClients;client++)
    {
        if(IsClientInGame(client) && !SkillsIdentity(client))SkillsBindClient(client);
        if(!SkillsActor(client)){SkillsClearPulses(client);SkillsRemoveAttributes(client);continue;}
        SkillsApplyAttributes(client);SkillsTrySpawnAward(client);
        if(gSkillsPulseEpoch[client]!=gEpoch)SkillsClearPulses(client);
        int serial=GetClientSerial(client),epoch=gEpoch,session=gSkillsMapSession,account=SkillsAccountIndex(client),revision=SkillsAccountRevision(client);
        for(int p=0;p<5;p++)
        {
            if(!SkillsPulseContext(client,serial,epoch,session,account,revision))break;
            int skill=p+view_as<int>(Skill_Medic);
            if(SkillsLevel(client,skill)<1){gSkillsNextPulse[client][p]=0.0;continue;}
            if(SkillsPulseDue(now,gSkillInterval[skill],gSkillsNextPulse[client][p]))SkillsPulse(client,skill);
            if(epoch!=gEpoch || session!=gSkillsMapSession || !Running() || gTerminal)return;
        }
    }
}
