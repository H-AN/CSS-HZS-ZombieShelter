// Kill rewards are independent of gathering pools and day-stage legacy profiles.
#define MAX_DROP_ENTRIES 4096
#define MAX_DROP_TOKENS 256
#define MAX_DROP_AMOUNT 1000000

enum struct DropEntry
{
    char zombieName[128];
    int material;
    float chance;
    int amount;
}

ArrayList gDropEntries;
ArrayList gNPCDropEntries[MAX_NPCS];
bool gRewardDefenseKills=true;

int CSVTokenCount(const char[] value)
{
    int count=1;for(int i=0;value[i];i++)if(value[i]==',')count++;
    return count;
}

bool CSVTokenAt(const char[] value,int wanted,char[] token,int length)
{
    token[0]=0;int current,start;
    for(int end=0;;end++)
    {
        if(value[end]!=',' && value[end]!=0)continue;
        if(current==wanted)
        {
            int count=end-start;if(count>=length)return false;
            for(int i=0;i<count;i++)token[i]=value[start+i];token[count]=0;TrimString(token);return true;
        }
        if(value[end]==0)return false;
        current++;start=end+1;
    }
}

bool PositiveDropAmount(const char[] token,int &amount)
{
    amount=0;int length=strlen(token);if(length<1)return false;
    // Accumulate safely; do not accept decimal fractions, signs or cell overflow.
    for(int i=0;i<length;i++)
    {
        if(token[i]<'0' || token[i]>'9')return false;
        int digit=token[i]-'0';if(amount>(2147483647-digit)/10)return false;
        amount=amount*10+digit;
    }
    return amount>0;
}

bool PositiveDropChance(const char[] token,float &chance)
{
    chance=0.0;if(!token[0] || StringToFloatEx(token,chance)!=strlen(token)
        || (view_as<int>(chance)&0x7f800000)==0x7f800000 || chance<=0.0)return false;
    return true;
}

bool DropRollSucceeds(float chance,float roll)
{
    if(chance<=0.0)return false;
    if(chance>=1.0)return true; // The RNG may return its inclusive upper endpoint.
    return roll<chance;
}

void ReleaseNPCDropCache(int n)
{
    if(n<0 || n>=MAX_NPCS)return;
    delete gNPCDropEntries[n];gNPCDropEntries[n]=null;
}

void ClearNPCDropCaches()
{
    for(int n=0;n<MAX_NPCS;n++)ReleaseNPCDropCache(n);
}

void CacheNPCDrops(int n,const char[] publicName)
{
    ReleaseNPCDropCache(n);strcopy(gNPCs[n].publicName,sizeof(gNPCs[].publicName),publicName);
    if(gDropEntries==null || !publicName[0])return;
    for(int i=0;i<gDropEntries.Length;i++)
    {
        DropEntry entry;gDropEntries.GetArray(i,entry,sizeof(entry));
        if(!StrEqual(entry.zombieName,publicName))continue;
        if(gNPCDropEntries[n]==null)gNPCDropEntries[n]=new ArrayList();
        gNPCDropEntries[n].Push(i);
    }
}

void ReadZombieDrops()
{
    ClearNPCDropCaches();delete gDropEntries;gDropEntries=new ArrayList(sizeof(DropEntry));
    gRewardDefenseKills=Number(C_ZombieDrops,"Settings","reward_defense_kills",1)!=0;
    if(!Node(C_ZombieDrops,"Rules") || !gCfg[C_ZombieDrops].GotoFirstSubKey())return;
    do
    {
        if(!gCfg[C_ZombieDrops].GetNum("enabled",1))continue;
        char rule[128],name[128],materials[8192],chances[8192],amounts[8192];
        gCfg[C_ZombieDrops].GetSectionName(rule,sizeof(rule));
        gCfg[C_ZombieDrops].GetString("zombie_name",name,sizeof(name));
        gCfg[C_ZombieDrops].GetString("materials",materials,sizeof(materials));
        gCfg[C_ZombieDrops].GetString("chances",chances,sizeof(chances));
        gCfg[C_ZombieDrops].GetString("amounts",amounts,sizeof(amounts));
        if(!name[0]){LogMessage("[Shelter] WARNING drop rule %s ignored: empty zombie_name",rule);continue;}
        char materialText[8192];strcopy(materialText,sizeof(materialText),materials);TrimString(materialText);
        if(!materialText[0])continue;
        int count=CSVTokenCount(materials),chanceCount=CSVTokenCount(chances),amountCount=CSVTokenCount(amounts);
        if(count>MAX_DROP_TOKENS || strlen(materials)>=sizeof(materials)-1 || strlen(chances)>=sizeof(chances)-1 || strlen(amounts)>=sizeof(amounts)-1)
        {LogMessage("[Shelter] WARNING drop rule %s ignored: CSV exceeds bounded parser capacity",rule);continue;}
        for(int position=0;position<count;position++)
        {
            char id[128],chanceText[128],amountText[128];
            if(!CSVTokenAt(materials,position,id,sizeof(id)) || !id[0])continue;
            int material=FindMaterial(id);
            if(material<0){LogMessage("[Shelter] WARNING drop rule %s position %d: unknown material %s",rule,position+1,id);continue;}
            if(!CSVTokenAt(chances,chanceCount==1 ? 0 : position,chanceText,sizeof(chanceText))
                || !CSVTokenAt(amounts,amountCount==1 ? 0 : position,amountText,sizeof(amountText)))continue;
            // Empty and non-positive slots deliberately mean no reward.
            if(!chanceText[0] || !amountText[0])continue;
            DropEntry entry;entry.material=material;
            if(!PositiveDropChance(chanceText,entry.chance) || !PositiveDropAmount(amountText,entry.amount))
            {LogMessage("[Shelter] WARNING drop rule %s position %d: invalid/non-positive chance or integer amount; skipped",rule,position+1);continue;}
            if(entry.chance>1.0){entry.chance=1.0;LogMessage("[Shelter] WARNING drop rule %s position %d: chance clamped to 1",rule,position+1);}
            if(entry.amount>MAX_DROP_AMOUNT){entry.amount=MAX_DROP_AMOUNT;LogMessage("[Shelter] WARNING drop rule %s position %d: amount clamped to %d",rule,position+1,MAX_DROP_AMOUNT);}
            if(gDropEntries.Length>=MAX_DROP_ENTRIES){LogMessage("[Shelter] WARNING drop entry capacity %d reached; remaining entry skipped",MAX_DROP_ENTRIES);continue;}
            strcopy(entry.zombieName,sizeof(entry.zombieName),name);gDropEntries.PushArray(entry,sizeof(entry));
        }
    }while(gCfg[C_ZombieDrops].GotoNextKey());
    PrintToServer("[Shelter] independent kill rewards loaded entries=%d defense_kills=%d (legacy drops/day profiles inactive)",gDropEntries.Length,gRewardDefenseKills);
}

bool RewardOwnerKeyMatches(const char[] captured,const char[] current,int serial)
{
    if(!captured[0] || StrEqual(captured,current))return true;
    // A projectile may have launched before Steam authentication completed.
    // Its still-valid client serial is enough to bind this provisional identity.
    char provisional[64];Format(provisional,sizeof(provisional),"serial:%d",serial);
    return StrEqual(captured,provisional);
}

void SettleZombieDrops(int n,int killer)
{
    RewardContext context;
    if(!GetDeathRewardContext(n,killer,context) || !context.valid || context.epoch!=gEpoch
        || (context.facility && !gRewardDefenseKills) || gNPCDropEntries[n]==null)return;
    int recipient=GetClientFromSerial(context.ownerSerial);
    if(recipient<1 || !IsClientInGame(recipient) || IsFakeClient(recipient)
        || !RewardOwnerKeyMatches(context.ownerKey,gInventoryOwner[recipient],context.ownerSerial))recipient=0;
    if(!context.facility && recipient==0)return;
    int core=context.buildCore;
    if(!context.facility && recipient>0){float position[3];GetClientAbsOrigin(recipient,position);core=NearestStorageCore(position);}
    int received[MATERIALS];
    for(int i=0;i<gNPCDropEntries[n].Length;i++)
    {
        int index=gNPCDropEntries[n].Get(i);if(gDropEntries==null || index<0 || index>=gDropEntries.Length)continue;
        DropEntry entry;gDropEntries.GetArray(index,entry,sizeof(entry));
        if(!DropRollSucceeds(entry.chance,GetRandomFloat(0.0,1.0)))continue;
        int actual=recipient>0 ? Grant(entry.material,entry.amount,recipient) : 0;
        if(actual>0)received[entry.material]=received[entry.material]>2000000000-actual ? 2000000000 : received[entry.material]+actual;
        if(entry.amount>actual)DepositSharedAutomatic(core,entry.material,entry.amount-actual);
    }
    if(recipient>0)
    {
        char summary[384];
        for(int m=0;m<gMaterialCount;m++)if(received[m]>0)
        {
            char label[64],part[96];TranslateLabel(recipient,gMaterials[m].label,label,sizeof(label));
            Format(part,sizeof(part),"%s%s +%d",summary[0] ? ", " : "",label,received[m]);StrCat(summary,sizeof(summary),part);
        }
        if(summary[0])Tell(recipient,"%s",summary);
    }
}
