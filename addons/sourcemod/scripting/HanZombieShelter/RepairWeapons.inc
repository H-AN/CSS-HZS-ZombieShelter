// Weapon systems own entity creation and attack timing. This module only maps
// an existing entity's literal classname to building work and gathering yield.
#define REPAIR_WEAPON_AMOUNT_MAX 1000000
#define REPAIR_WEAPON_CLASS_BUFFER 1024
#define GATHER_MULTIPLIER_SCALE 1000
#define GATHER_MULTIPLIER_MAX 1000000 // 1000.000 times the configured resource yield.

StringMap gRepairWeaponRules;
bool gRepairWeaponBuiltinKnife=true,gRepairWeaponMissingFile;
bool gRepairWeaponCommandsRegistered;
KeyValues gReadingWeaponConfig;
int gReadingWeaponDepth;
bool gReadingWeaponRootSeen;

// ImportFromFile coerces 1.1 to a float and GetString formats it as 1.100000.
// Preserve numeric source text so strict fixed-point validation is exact.
public SMCResult WeaponConfigSection(SMCParser parser,const char[] name,bool quoted)
{
    if(gReadingWeaponDepth==0)
    {
        if(gReadingWeaponRootSeen)return SMCParse_HaltFail;
        gReadingWeaponConfig.SetSectionName(name);gReadingWeaponRootSeen=true;
    }
    else if(!gReadingWeaponConfig.JumpToKey(name,true))return SMCParse_HaltFail;
    gReadingWeaponDepth++;return SMCParse_Continue;
}

public SMCResult WeaponConfigValue(SMCParser parser,const char[] key,const char[] value,bool keyQuoted,bool valueQuoted)
{
    if(gReadingWeaponDepth<1)return SMCParse_HaltFail;
    gReadingWeaponConfig.SetString(key,value);return SMCParse_Continue;
}

public SMCResult WeaponConfigSectionEnd(SMCParser parser)
{
    if(gReadingWeaponDepth<1)return SMCParse_HaltFail;
    gReadingWeaponDepth--;
    if(gReadingWeaponDepth>0)gReadingWeaponConfig.GoBack();
    return SMCParse_Continue;
}

bool ReadWeaponConfigStrings(KeyValues config,const char[] path)
{
    if(gReadingWeaponConfig!=null)return false;
    gReadingWeaponConfig=config;gReadingWeaponDepth=0;gReadingWeaponRootSeen=false;
    SMCParser parser=new SMCParser();
    parser.OnEnterSection=WeaponConfigSection;parser.OnKeyValue=WeaponConfigValue;
    parser.OnLeaveSection=WeaponConfigSectionEnd;
    bool valid=parser.ParseFile(path)==SMCError_Okay && gReadingWeaponRootSeen && gReadingWeaponDepth==0;
    delete parser;config.Rewind();gReadingWeaponConfig=null;return valid;
}

bool RepairWeaponConfigError(char[] error,int maxlen,const char[] message)
{
    if(maxlen>0)strcopy(error,maxlen,message);
    return false;
}

bool ReadRepairWeaponInteger(KeyValues config,const char[] key,int &amount)
{
    if(config.GetDataType(key)==KvData_None)return false;
    char raw[64];config.GetString(key,raw,sizeof(raw));
    // Reject a saturated buffer instead of accepting a truncated numeric value.
    if(strlen(raw)==sizeof(raw)-1)return false;
    return ParseLevelInteger(raw,amount) && amount<=REPAIR_WEAPON_AMOUNT_MAX;
}

bool ReadGatherMultiplier(KeyValues config,int &multiplier)
{
    multiplier=GATHER_MULTIPLIER_SCALE;
    if(config.GetDataType("gather_multiplier")==KvData_None)
    {
        if(config.JumpToKey("gather_multiplier")){config.GoBack();return false;}
        return true;
    }
    char raw[64];config.GetString("gather_multiplier",raw,sizeof(raw));
    if(strlen(raw)==sizeof(raw)-1)return false;
    TrimString(raw);int length=strlen(raw),whole,fraction,digits;bool decimal,hasWhole;
    if(length<1)return false;
    for(int i=0;i<length;i++)
    {
        if(raw[i]=='.')
        {
            if(decimal || !hasWhole)return false;
            decimal=true;continue;
        }
        if(raw[i]<'0' || raw[i]>'9')return false;
        int digit=raw[i]-'0';
        if(decimal)
        {
            if(++digits>3)return false;
            fraction=fraction*10+digit;
        }
        else
        {
            if(whole>(GATHER_MULTIPLIER_MAX/GATHER_MULTIPLIER_SCALE-digit)/10)return false;
            whole=whole*10+digit;hasWhole=true;
        }
    }
    if(decimal && digits==0)return false;
    while(digits<3){fraction*=10;digits++;}
    multiplier=whole*GATHER_MULTIPLIER_SCALE+fraction;
    return multiplier<=GATHER_MULTIPLIER_MAX;
}

bool LoadRepairWeaponsFromPath(const char[] path,char[] error,int maxlen)
{
    if(maxlen>0)error[0]=0;
    StringMap pending=new StringMap();
    bool missing=!FileExists(path),useBuiltinKnife=true;
    if(!missing)
    {
        KeyValues config=new KeyValues("ShelterWeapons");
        if(!ReadWeaponConfigStrings(config,path))
        {
            delete config;delete pending;
            return RepairWeaponConfigError(error,maxlen,"weapons.cfg cannot be parsed");
        }
        char root[64];config.GetSectionName(root,sizeof(root));int schema;
        if(!StrEqual(root,"ShelterWeapons") || !ReadRepairWeaponInteger(config,"schema_version",schema) || schema!=1)
        {
            delete config;delete pending;
            return RepairWeaponConfigError(error,maxlen,"expected ShelterWeapons/schema_version 1");
        }
        if(config.GetDataType("Weapons")!=KvData_None)
        {
            delete config;delete pending;
            return RepairWeaponConfigError(error,maxlen,"Weapons must be a section");
        }
        if(!config.JumpToKey("Weapons"))
        {
            delete config;delete pending;
            return RepairWeaponConfigError(error,maxlen,"Weapons section is required");
        }
        if(config.GotoFirstSubKey(false))
        {
            do
            {
                char group[128],classname[REPAIR_WEAPON_CLASS_BUFFER],message[256];
                config.GetSectionName(group,sizeof(group));int amounts[3];
                if(config.GetDataType("classname")==KvData_None)
                {
                    Format(message,sizeof(message),"Weapons/%s must contain a classname value",group);
                    delete config;delete pending;return RepairWeaponConfigError(error,maxlen,message);
                }
                config.GetString("classname",classname,sizeof(classname));
                // Preserve case, whitespace and spelling. This is a storage
                // bound only; no prefix/engine registry/entity probe is used.
                if(!classname[0] || strlen(classname)==sizeof(classname)-1)
                {
                    Format(message,sizeof(message),"Weapons/%s classname is empty or exceeds the parser buffer",group);
                    delete config;delete pending;return RepairWeaponConfigError(error,maxlen,message);
                }
                if(!ReadRepairWeaponInteger(config,"repair_hp_per_hit",amounts[0])
                    || !ReadRepairWeaponInteger(config,"construction_hp_per_hit",amounts[1]))
                {
                    Format(message,sizeof(message),"Weapons/%s requires integer repair_hp_per_hit and construction_hp_per_hit in 0..1000000",group);
                    delete config;delete pending;return RepairWeaponConfigError(error,maxlen,message);
                }
                if(!ReadGatherMultiplier(config,amounts[2]))
                {
                    Format(message,sizeof(message),"Weapons/%s gather_multiplier requires 0..1000 with at most 3 decimal places",group);
                    delete config;delete pending;return RepairWeaponConfigError(error,maxlen,message);
                }
                if(!pending.SetArray(classname,amounts,sizeof(amounts),false))
                {
                    Format(message,sizeof(message),"Weapons/%s duplicates an earlier classname",group);
                    delete config;delete pending;return RepairWeaponConfigError(error,maxlen,message);
                }
            }while(config.GotoNextKey(false));
        }
        delete config;
    }
    if(!pending.ContainsKey("weapon_knife"))
    {
        int fallback[3]={10,10,GATHER_MULTIPLIER_SCALE};pending.SetArray("weapon_knife",fallback,sizeof(fallback));
    }
    else useBuiltinKnife=false;

    // Commit only after every row is valid. Reloading never mutates building
    // state, campaign revision, weapon entities, or their attack clocks.
    StringMap previous=gRepairWeaponRules;gRepairWeaponRules=pending;
    gRepairWeaponBuiltinKnife=useBuiltinKnife;gRepairWeaponMissingFile=missing;delete previous;
    return true;
}

int RepairWeaponRuleCount()
{
    return gRepairWeaponRules==null ? 1 : gRepairWeaponRules.Size;
}

bool WeaponWorkAmountsForClass(const char[] classname,int &repair,int &construction,int &gather)
{
    int amounts[3]={10,10,GATHER_MULTIPLIER_SCALE};
    if(gRepairWeaponRules!=null && !gRepairWeaponRules.GetArray(classname,amounts,sizeof(amounts)))
        gRepairWeaponRules.GetArray("weapon_knife",amounts,sizeof(amounts));
    repair=amounts[0];construction=amounts[1];gather=amounts[2];return true;
}

stock bool RepairWeaponAmountsForClass(const char[] classname,int &repair,int &construction)
{
    int gather;return WeaponWorkAmountsForClass(classname,repair,construction,gather);
}

bool HeldWeaponWorkAmounts(int client,int &repair,int &construction,int &gather,bool requireWeapon=false)
{
    repair=10;construction=10;gather=GATHER_MULTIPLIER_SCALE;
    if(!LiveHuman(client) || IsFakeClient(client) || IsClientSourceTV(client) || IsClientReplay(client))return false;
    char classname[REPAIR_WEAPON_CLASS_BUFFER];classname[0]=0;
    int weapon=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon");
    if(requireWeapon && (weapon<=MaxClients || !IsValidEntity(weapon)))return false;
    if(weapon>MaxClients && IsValidEntity(weapon))
    {
        if(!GetEntityClassname(weapon,classname,sizeof(classname)) || strlen(classname)==sizeof(classname)-1)
            classname[0]=0;
    }
    // Missing/unreadable active weapon also uses the knife fallback. Only an
    // invalid/non-human actor is rejected; unknown classnames are never denied.
    return WeaponWorkAmountsForClass(classname,repair,construction,gather);
}

bool HeldRepairWeaponAmounts(int client,int &repair,int &construction)
{
    int gather;return HeldWeaponWorkAmounts(client,repair,construction,gather);
}

bool LoadRepairWeapons()
{
    char path[PLATFORM_MAX_PATH],error[256];
    BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieShelter/weapons.cfg");
    if(!LoadRepairWeaponsFromPath(path,error,sizeof(error)))
    {
        LogError("[Shelter] weapons.cfg rejected; previous weapon rules retained: %s",error);return false;
    }
    int repair,construction,gather;WeaponWorkAmountsForClass("weapon_knife",repair,construction,gather);
    LogMessage("[Shelter] Weapon rules=%d; unknown/unreadable classname -> weapon_knife %d/%d/%d (repair/construction/gather_per_1000); builtin_knife=%d missing_file=%d",
        RepairWeaponRuleCount(),repair,construction,gather,gRepairWeaponBuiltinKnife,gRepairWeaponMissingFile);
    return true;
}

public Action ReloadRepairWeaponsCommand(int client,int args)
{
    if(args!=0)
    {ReplyToCommand(client,"Usage: sm_zsh_reload_weapons (reloads only weapons.cfg)");return Plugin_Handled;}
    if(!LoadRepairWeapons())
    {ReplyToCommand(client,"[Shelter] weapons.cfg rejected; previous weapon rules retained.");return Plugin_Handled;}
    int repair,construction,gather;WeaponWorkAmountsForClass("weapon_knife",repair,construction,gather);
    ReplyToCommand(client,"[Shelter] Reloaded %d weapon rules; unknown/unreadable -> weapon_knife: repair=%d construction=%d gather=%.3f; builtin_knife=%d missing_file=%d.",
        RepairWeaponRuleCount(),repair,construction,float(gather)/GATHER_MULTIPLIER_SCALE,gRepairWeaponBuiltinKnife,gRepairWeaponMissingFile);
    return Plugin_Handled;
}

void RegisterRepairWeaponCommands()
{
    if(gRepairWeaponCommandsRegistered)return;
    RegAdminCmd("sm_zsh_reload_weapons",ReloadRepairWeaponsCommand,ADMFLAG_ROOT,"Reload only weapon repair, construction and gathering rules.");
    gRepairWeaponCommandsRegistered=true;
}
