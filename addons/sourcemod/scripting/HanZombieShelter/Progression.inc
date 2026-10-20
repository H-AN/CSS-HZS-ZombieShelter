// Only the HZS pre-damage forward applies these multipliers. Callers keep raw
// damage, including burn ticks, so no facility can multiply the same hit twice.
#define PROGRESSION_CHANNELS 4
#define PROGRESSION_LEVEL_INTEGER_LIMIT 2000000000

static const char gProgressionChannels[][]={"Player/Ordinary","Player/Boss","Defense/Ordinary","Defense/Boss"};
static const char gAmplifierBonusKeys[][]={"player_ordinary_bonus","player_boss_bonus","defense_ordinary_bonus","defense_boss_bonus"};

enum struct ProgressionRule
{
    int unlockLevel;
    int lockedDamage;
    float maxMultiplier;
    float maxAmplifierBonus;
    bool grows;
    int anchorLevel;
    float anchorMultiplier;
    float multiplierPerLevel;
    float minimumBaseMultiplier;
    float maximumBaseMultiplier;
}

enum struct AmplifierProfile
{
    char type[64];
}

ProgressionRule gProgressionRules[PROGRESSION_CHANNELS];
AmplifierProfile gAmplifiers[MAX_RECIPES];
int gAmplifierCount,gProgressionLevels,gProgressionDamageCap;
bool gProgressionEnabled;
// Scoped by DamageNPC; the NPC EntRef must also match before this is consulted.
bool gDamageIsDefense;

bool LoadProgression()
{
    gProgressionEnabled=Number(C_Progression,"Rules","enabled",1)!=0;
    bool found;
    if(!StoredLevelInt(C_Progression,"Rules","base_level_cap",4,gProgressionLevels,found)
        || !StoredLevelInt(C_Progression,"Rules","maximum_final_damage",100000,gProgressionDamageCap,found))
        return ConfigFailure("progression cap must be an integer");
    if(gProgressionLevels<1 || gProgressionLevels>PROGRESSION_LEVEL_INTEGER_LIMIT
        || gProgressionDamageCap<1 || gProgressionDamageCap>100000)
        return ConfigFailure("progression base_level_cap/final damage cap");
    for(int channel=0;channel<PROGRESSION_CHANNELS;channel++)
    {
        ProgressionRule rule;
        if(!Node(C_Progression,gProgressionChannels[channel]))return ConfigFailure("progression channel missing");
        if(!StoredLevelInt(C_Progression,gProgressionChannels[channel],"unlock_base_level",1,rule.unlockLevel,found)
            || !StoredLevelInt(C_Progression,gProgressionChannels[channel],"locked_damage",1,rule.lockedDamage,found))
            return ConfigFailure("progression unlock/locked damage must be integers");
        if(!ProgressionFloatAtCursor("maximum_multiplier",8.0,rule.maxMultiplier)
            || !ProgressionFloatAtCursor("maximum_amplifier_bonus",6.0,rule.maxAmplifierBonus))
            return ConfigFailure("progression multiplier is not a finite number");
        if(rule.unlockLevel<1 || rule.unlockLevel>gProgressionLevels || rule.lockedDamage<0
            || rule.lockedDamage>gProgressionDamageCap || !(rule.maxMultiplier>0.0 && rule.maxMultiplier<=100.0)
            || !ValidAmplifierBonus(rule.maxAmplifierBonus))
            return ConfigFailure("progression unlock/locked damage/multiplier limits");
        char path[128];Format(path,sizeof(path),"%s/Growth",gProgressionChannels[channel]);
        rule.grows=Node(C_Progression,path);
        if(rule.grows)
        {
            if(!StoredLevelInt(C_Progression,path,"anchor_level",1,rule.anchorLevel,found))
                return ConfigFailure("progression Growth anchor must be an integer");
            if(!ProgressionFloatAtCursor("multiplier_per_level",0.0,rule.multiplierPerLevel)
                || !ProgressionFloatAtCursor("multiplier_min",0.0,rule.minimumBaseMultiplier)
                || !ProgressionFloatAtCursor("multiplier_max",100.0,rule.maximumBaseMultiplier))
                return ConfigFailure("progression Growth is not a finite number");
            if(rule.anchorLevel<1 || rule.anchorLevel>gProgressionLevels
                || !(rule.multiplierPerLevel>=-100.0 && rule.multiplierPerLevel<=100.0)
                || !ValidAmplifierBonus(rule.minimumBaseMultiplier) || !ValidAmplifierBonus(rule.maximumBaseMultiplier)
                || rule.minimumBaseMultiplier>rule.maximumBaseMultiplier)
                return ConfigFailure("progression Growth anchor/increment/clamp range");
        }
        // Visit only configured keys. A level cap of 100 or two billion must
        // never allocate or iterate that many entries during config loading.
        Format(path,sizeof(path),"%s/BaseMultipliers",gProgressionChannels[channel]);
        if(!Node(C_Progression,path) || !gCfg[C_Progression].GotoFirstSubKey(false))
            return ConfigFailure("progression BaseMultipliers missing");
        int prefixCount;bool valid=true,anchorFound;StringMap seen=new StringMap();
        do
        {
            char key[64];int level,unused;
            gCfg[C_Progression].GetSectionName(key,sizeof(key));
            float value;bool numeric=ProgressionFloatAtCursor(NULL_STRING,-1.0,value);
            if(!ProgressionLevelKey(key,level) || level>gProgressionLevels
                || gCfg[C_Progression].GetDataType(NULL_STRING)==KvData_None
                || !numeric || !ValidAmplifierBonus(value) || seen.GetValue(key,unused))
            {valid=false;break;}
            seen.SetValue(key,1);
            if(!rule.grows || level<=rule.anchorLevel)prefixCount++;
            if(rule.grows && level==rule.anchorLevel){rule.anchorMultiplier=value;anchorFound=true;}
        }while(gCfg[C_Progression].GotoNextKey(false));
        delete seen;
        int requiredPrefix=rule.grows ? rule.anchorLevel : gProgressionLevels;
        if(!valid || prefixCount!=requiredPrefix || (rule.grows && !anchorFound))
            return ConfigFailure("progression requires unique positive BaseMultipliers through Growth anchor (or cap without Growth)");
        gProgressionRules[channel]=rule;
    }

    gAmplifierCount=0;
    if(!Node(C_Amplifiers,"Types") || !gCfg[C_Amplifiers].GotoFirstSubKey())
        return ConfigFailure("amplifier Types missing");
    do
    {
        if(gAmplifierCount>=MAX_RECIPES)return ConfigFailure("too many amplifier types");
        AmplifierProfile profile;gCfg[C_Amplifiers].GetSectionName(profile.type,sizeof(profile.type));
        gAmplifiers[gAmplifierCount++]=profile;
    }while(gCfg[C_Amplifiers].GotoNextKey());
    for(int profile=0;profile<gAmplifierCount;profile++)
        if(!ValidateAmplifierProgression(gAmplifiers[profile].type))
            return ConfigFailure("amplifier level HP/base requirement/bonus/upgrade cost range");
    for(int recipe=0;recipe<gRecipeCount;recipe++)
        if(gRecipes[recipe].kind==Kind_Amplifier && FindAmplifierProfile(gRecipes[recipe].type)<0)
            return ConfigFailure("amplifier recipe references unknown type");
    return true;
}

bool ProgressionLevelKey(const char[] key,int &level)
{
    int length=strlen(key);level=0;
    if(length<1 || length>10 || key[0]=='0')return false;
    for(int c=0;c<length;c++)
    {
        int digit=key[c]-'0';
        if(digit<0 || digit>9 || level>(PROGRESSION_LEVEL_INTEGER_LIMIT-digit)/10)return false;
        level=level*10+digit;
    }
    return level>=1;
}

bool ProgressionFloatAtCursor(const char[] key,float fallback,float &value)
{
    value=fallback;
    if(gCfg[C_Progression].GetDataType(key)==KvData_None)return true;
    char raw[64];gCfg[C_Progression].GetString(key,raw,sizeof(raw));
    int length=strlen(raw);
    return length>0 && length<sizeof(raw)-1 && StringToFloatEx(raw,value)==length
        && value>=-3.4e38 && value<=3.4e38;
}

bool ReadProgressionBaseMultiplier(int channel,int level,float &value)
{
    value=0.0;
    if(channel<0 || channel>=PROGRESSION_CHANNELS || level<1 || level>gProgressionLevels)return false;
    char path[128],key[16];Format(path,sizeof(path),"%s/BaseMultipliers",gProgressionChannels[channel]);
    IntToString(level,key,sizeof(key));
    if(Node(C_Progression,path) && gCfg[C_Progression].GetDataType(key)!=KvData_None)
    {
        return ProgressionFloatAtCursor(key,-1.0,value) && ValidAmplifierBonus(value);
    }
    if(!gProgressionRules[channel].grows || level<=gProgressionRules[channel].anchorLevel)return false;
    value=gProgressionRules[channel].anchorMultiplier+gProgressionRules[channel].multiplierPerLevel
        *float(level-gProgressionRules[channel].anchorLevel);
    // Reject NaN/infinity before applying a configured clamp.
    if(!(value>=-3.4e38 && value<=3.4e38))return false;
    if(value<gProgressionRules[channel].minimumBaseMultiplier)value=gProgressionRules[channel].minimumBaseMultiplier;
    if(value>gProgressionRules[channel].maximumBaseMultiplier)value=gProgressionRules[channel].maximumBaseMultiplier;
    return ValidAmplifierBonus(value);
}

bool ValidateAmplifierProgressionLevel(const char[] type,int level)
{
    int required,hp,cost[MATERIALS];float bonuses[PROGRESSION_CHANNELS];char path[128];
    Format(path,sizeof(path),"Types/%s",type);
    return AmplifierLevelInfo(type,level,required,bonuses)
        && ReadLevelInt(C_Amplifiers,path,level,"max_hp",0,hp) && hp>0
        && (level==1 || ReadLevelUpgradeCost(C_Amplifiers,path,level,cost));
}

bool ValidateAmplifierProgression(const char[] type)
{
    char path[128];Format(path,sizeof(path),"Types/%s/Levels",type);
    if(!Node(C_Amplifiers,path) || !gCfg[C_Amplifiers].GotoFirstSubKey())return false;
    ArrayList levels=new ArrayList();bool valid=true;
    do
    {
        char key[64];int level;gCfg[C_Amplifiers].GetSectionName(key,sizeof(key));
        if(!ProgressionLevelKey(key,level)){valid=false;break;}
        levels.Push(level);
    }while(gCfg[C_Amplifiers].GotoNextKey());
    // Save the level list before any shared reader rewinds the KeyValues cursor.
    if(valid)for(int n=0;n<levels.Length;n++)
        if(!ValidateAmplifierProgressionLevel(type,levels.Get(n))){valid=false;break;}
    delete levels;
    Format(path,sizeof(path),"Types/%s",type);
    int maximum=ProfileMaximumLevel(C_Amplifiers,path);
    // Explicit rows include the required anchor; also resolve the formula's
    // endpoint so an unreachable base gate or overflowing cost fails at load.
    return valid && maximum>=1 && ValidateAmplifierProgressionLevel(type,maximum);
}

bool ValidAmplifierBonus(float bonus)
{
    return bonus>=0.0 && bonus<=100.0;
}

int FindAmplifierProfile(const char[] type)
{
    for(int profile=0;profile<gAmplifierCount;profile++)
        if(StrEqual(type,gAmplifiers[profile].type))return profile;
    return -1;
}

int ProgressionBaseLevel()
{
    int highest=0;
    for(int core=0;core<gCoreCount;core++)
    {
        int building=gCores[core].building;
        if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used || !gBuildings[building].alive
            || gBuildings[building].hp<=0 || !BuildingPowered(building))continue;
        if(gBuildings[building].level>highest)highest=gBuildings[building].level;
    }
    return highest;
}

bool AmplifierLevelInfo(const char[] type,int level,int &requiredLevel,float bonuses[PROGRESSION_CHANNELS])
{
    requiredLevel=0;for(int channel=0;channel<PROGRESSION_CHANNELS;channel++)bonuses[channel]=0.0;
    if(FindAmplifierProfile(type)<0 || level<1)return false;
    char path[128];Format(path,sizeof(path),"Types/%s",type);
    if(!LevelAvailable(C_Amplifiers,path,level)
        || !ReadLevelInt(C_Amplifiers,path,level,"minimum_base_level",1,requiredLevel)
        || requiredLevel<1 || requiredLevel>gProgressionLevels)return false;
    for(int channel=0;channel<PROGRESSION_CHANNELS;channel++)
        if(!ReadLevelFloat(C_Amplifiers,path,level,gAmplifierBonusKeys[channel],0.0,bonuses[channel])
            || !ValidAmplifierBonus(bonuses[channel]))return false;
    return true;
}

bool CanBuildAmplifier(const char[] type,int level,int &requiredLevel)
{
    float bonuses[PROGRESSION_CHANNELS];
    return AmplifierLevelInfo(type,level,requiredLevel,bonuses) && ProgressionBaseLevel()>=requiredLevel;
}

// The same campaign-wide level used by existing progression. Missing fields
// default to 1 so every pre-0.7.2 recipe retains its original availability.
bool ProfileBaseLevelAllowed(ConfigFile file,const char[] profile,int level,int &required)
{
    required=1;
    if(!ReadLevelInt(file,profile,level,"minimum_base_level",1,required))return false;
    return ProgressionBaseLevel()>=required;
}

bool RecipeBaseLevelAllowed(int recipe,int &required)
{
    required=1;if(recipe<0 || recipe>=gRecipeCount)return false;
    if(gRecipes[recipe].kind==Kind_Amplifier)return CanBuildAmplifier(gRecipes[recipe].type,1,required);
    char profile[128];Format(profile,sizeof(profile),"Types/%s",gRecipes[recipe].type);
    return ProfileBaseLevelAllowed(gRecipes[recipe].file,profile,1,required);
}

bool BuildingBaseLevelAllowed(int building,int level,int &required)
{
    required=1;if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used)return false;
    if(gBuildings[building].kind==Kind_Amplifier)return CanBuildAmplifier(gBuildings[building].type,level,required);
    char profile[128];BuildingProfilePath(building,profile,sizeof(profile));
    return ProfileBaseLevelAllowed(gBuildings[building].file,profile,level,required);
}

int ProgressionChannel(bool defense,bool boss)
{
    return (defense ? 2 : 0)+(boss ? 1 : 0);
}

float AmplifierBonus(int channel,int &count)
{
    count=0;float total=0.0;
    for(int building=0;building<MAX_BUILDINGS;building++)
    {
        if(!gBuildings[building].used || !gBuildings[building].alive || gBuildings[building].kind!=Kind_Amplifier
            || gBuildings[building].hp<=0 || !BuildingPowered(building))continue;
        int required;float bonuses[PROGRESSION_CHANNELS];
        if(!AmplifierLevelInfo(gBuildings[building].type,gBuildings[building].level,required,bonuses))continue;
        count++;total+=bonuses[channel];
    }
    return total;
}

// lockedDamage == -1 means the multiplier is active; otherwise the positive
// incoming hit becomes lockedDamage, independently of incoming weapon strength.
void GetProgressionStats(bool defense,bool boss,float &multiplier,int &lockedDamage,int &amplifierCount)
{
    multiplier=1.0;lockedDamage=-1;amplifierCount=0;
    if(!gProgressionEnabled)return;
    int channel=ProgressionChannel(defense,boss),level=ProgressionBaseLevel();
    float bonus=AmplifierBonus(channel,amplifierCount);
    if(level<gProgressionRules[channel].unlockLevel)
    {lockedDamage=gProgressionRules[channel].lockedDamage;multiplier=0.0;return;}
    if(level>gProgressionLevels)level=gProgressionLevels;
    if(bonus>gProgressionRules[channel].maxAmplifierBonus)bonus=gProgressionRules[channel].maxAmplifierBonus;
    float baseMultiplier;
    if(!ReadProgressionBaseMultiplier(channel,level,baseMultiplier))
    {lockedDamage=gProgressionRules[channel].lockedDamage;multiplier=0.0;return;}
    multiplier=baseMultiplier+bonus;
    if(multiplier>gProgressionRules[channel].maxMultiplier)multiplier=gProgressionRules[channel].maxMultiplier;
}

void ApplyProgressionDamage(int npc,int attacker,int &damage,bool defense)
{
    if(!gProgressionEnabled || damage<=0 || npc<0 || npc>=MAX_NPCS || !gNPCs[npc].used)return;
    // Core damage accepts player credit. Facility hits retain that credit but
    // use their scoped defense channel instead of masquerading as player shots.
    if(attacker<1 || attacker>MaxClients || !IsClientInGame(attacker))return;
    float multiplier;int lockedDamage,amplifierCount;
    GetProgressionStats(defense,gNPCs[npc].boss>=0,multiplier,lockedDamage,amplifierCount);
    if(lockedDamage>=0){damage=lockedDamage;return;}
    float scaled=float(damage)*multiplier;
    if(scaled>=float(gProgressionDamageCap))damage=gProgressionDamageCap;
    else if(scaled<=0.0)damage=0;
    else{damage=RoundToNearest(scaled);if(damage<1)damage=1;}
}
