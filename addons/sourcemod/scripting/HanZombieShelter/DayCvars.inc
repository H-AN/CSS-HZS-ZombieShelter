// Only options whose native read sites have been audited belong in this list.
// No commands, plugin enable/disable, round outcomes or arbitrary server cvars.
static const char sDayCvarNames[][]={"hzs_maxzombiespawn","hzs_playerreborn","hzs_killmoney","hzs_maxmoney","hzs_showdamage","hzs_enablekillicon","hzs_allowbuy"};
ConVar gDayCvars[sizeof(sDayCvarNames)];
char gDayCvarOriginal[sizeof(sDayCvarNames)][64];
bool gDayCvarTouched[sizeof(sDayCvarNames)],gDayCvarsCaptured;
bool gNativePoolBootstrap;

int DayCvarIndex(const char[] name)
{
    for(int i=0;i<sizeof(sDayCvarNames);i++)if(StrEqual(name,sDayCvarNames[i]))return i;
    return -1;
}

bool ValidDayCvarValue(int index,const char[] value)
{
    if(index<0 || index>=sizeof(sDayCvarNames) || !value[0] || strlen(value)>16)return false;
    // Require the whole value to be a simple number; partial parses such as
    // "50; quit", infinities, exponent overflow and NaN are never accepted.
    for(int c=0;value[c];c++)
        if(!(value[c]>='0' && value[c]<='9') && !(c==0 && value[c]=='-') && !(index==1 && value[c]=='.'))return false;
    if(index==1)
    {
        float seconds;if(StringToFloatEx(value,seconds)!=strlen(value))return false;
        return seconds==-1.0 || seconds==0.0 || (seconds>=1.0 && seconds<=600.0);
    }
    if(strlen(value)>8)return false; // also prevents signed cell overflow before validation
    int number;if(StringToIntEx(value,number)!=strlen(value))return false;
    if(index==0)return number>=1 && number<=gNativePoolSize;
    if(index==2)return number>=0 && number<=1000000;
    if(index==3)return number>=1 && number<=1000000;
    return number==0 || number==1;
}

// Called with the days.cfg cursor on one range. Restores that cursor even on
// rejection, so the surrounding range validator can continue safely.
bool ValidateStageCvars(KeyValues stage)
{
    if(!stage.JumpToKey("Cvars"))return true;
    bool valid=true;
    if(stage.GotoFirstSubKey(false))
    {
        do
        {
            char name[64],value[128];stage.GetSectionName(name,sizeof(name));stage.GetString(NULL_STRING,value,sizeof(value));
            int index=DayCvarIndex(name);
            if(!ValidDayCvarValue(index,value)){valid=false;break;}
        }while(stage.GotoNextKey(false));
        stage.GoBack();
    }
    stage.GoBack();return valid;
}

void CaptureDayCvars()
{
    if(gDayCvarsCaptured)return;
    for(int i=0;i<sizeof(sDayCvarNames);i++)
    {
        gDayCvars[i]=FindConVar(sDayCvarNames[i]);gDayCvarTouched[i]=false;
        if(gDayCvars[i]!=null)gDayCvars[i].GetString(gDayCvarOriginal[i],sizeof(gDayCvarOriginal[]));
    }
    gDayCvarsCaptured=true;
}

int StagePopulationCap()
{
    int cap=gPopulationLimit;
    if(SelectDay() && gCfg[C_Days].JumpToKey("Cvars"))
    {
        cap=gCfg[C_Days].GetNum("hzs_maxzombiespawn",gPopulationLimit);gCfg[C_Days].GoBack();
    }
    if(cap>gPopulationLimit)cap=gPopulationLimit;
    if(cap>gNativePoolSize)cap=gNativePoolSize;
    return cap<1 ? 1 : cap;
}

void ApplyDayCvars()
{
    if(!gEnabled || !SelectDay())return;
    CaptureDayCvars();bool section=gCfg[C_Days].JumpToKey("Cvars");
    for(int i=0;i<sizeof(sDayCvarNames);i++)
    {
        ConVar cvar=gDayCvars[i];if(cvar==null)continue;
        char value[64];value[0]='\0';if(section)gCfg[C_Days].GetString(sDayCvarNames[i],value,sizeof(value));
        if(i==0)
        {
            // Han_OnGameStart runs BEFORE the core's SpawnRoundZombie. Keep the
            // full pool size until that synchronous creation has completed.
            int cap=value[0] ? StringToInt(value) : gPopulationLimit;
            cvar.IntValue=gNativePoolBootstrap ? gNativePoolSize : cap;
            continue;
        }
        if(value[0])
        {
            gDayCvarTouched[i]=true;
            if(i==1)cvar.FloatValue=StringToFloat(value);else cvar.IntValue=StringToInt(value);
        }
        else if(gDayCvarTouched[i])cvar.SetString(gDayCvarOriginal[i]);
    }
    if(section)gCfg[C_Days].GoBack();
}

public void FinishNativePoolBootstrap(any epoch)
{
    if(epoch!=gEpoch || !gEnabled || !gRoundPrepared || gTerminal || !gNativePoolBootstrap)return;
    gNativePoolBootstrap=false;ApplyDayCvars();
}

void RestoreDayCvars()
{
    // Pool restoration is owned by RestoreCorePool and uses its original
    // pre-mode snapshot, never a later stage value or the temporary bootstrap.
    if(gDayCvarsCaptured)
        for(int i=1;i<sizeof(sDayCvarNames);i++)
            if(gDayCvarTouched[i] && gDayCvars[i]!=null)gDayCvars[i].SetString(gDayCvarOriginal[i]);
    gDayCvarsCaptured=false;gNativePoolBootstrap=false;
    for(int i=0;i<sizeof(sDayCvarNames);i++)gDayCvarTouched[i]=false;
}
