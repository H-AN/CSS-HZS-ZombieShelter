// Levels are resolved on demand from configuration. There is no per-level array
// and no gameplay cap such as 16 or 100; limits below protect SourcePawn cells.
#define LEVEL_CELL_LIMIT 2000000000

static const char sLevelIntegerFields[][]={"max_hp","initial_hp","burn_damage","minimum_base_level","power_priority","light_brightness","build_limit","build_limit_bonus","restore_amount","max_per_base","decay_amount","inactive_hp","no_decay_at_max_level","fuel_capacity","fuel_per_wood","fuel_wood_cost"};
static const char sLevelFloatFields[][]={"damage","range","fire_interval","cone_half_angle_degrees","burn_interval",
    "burn_duration","blast_radius","player_ordinary_bonus","player_boss_bonus","defense_ordinary_bonus","defense_boss_bonus",
    "power_output","power_required","light_radius","projectile_speed","build_range_bonus","build_half_extents_x","build_half_extents_y","restore_interval_seconds","safe_radius","safe_height","decay_interval_seconds","charge_seconds",
    "field_damage","field_duration","field_radius","field_interval"};
static const float sLevelFloatDefaults[]={0.0,0.0,0.5,30.0,0.5,0.0,180.0,0.0,0.0,0.0,0.0,0.0,0.0,500.0,900.0,0.0,512.0,512.0,20.0,160.0,160.0,1.0,1.5,0.0,0.0,96.0,0.5};

bool ParseLevelInteger(const char[] input,int &value,bool signedValue=false)
{
    char text[64];strcopy(text,sizeof(text),input);TrimString(text);
    int length=strlen(text),start;bool negative;
    if(length<1)return false;
    if(text[0]=='-'){if(!signedValue)return false;negative=true;start=1;}
    if(length==start || length-start>10)return false;
    int magnitude;
    for(int i=start;i<length;i++)
    {
        if(text[i]<'0' || text[i]>'9')return false;
        int digit=text[i]-'0';if(magnitude>(LEVEL_CELL_LIMIT-digit)/10)return false;
        magnitude=magnitude*10+digit;
    }
    value=negative ? -magnitude : magnitude;return true;
}

bool StoredLevelInt(ConfigFile file,const char[] path,const char[] key,int fallback,int &value,bool &found,bool signedValue=false)
{
    value=fallback;found=false;
    if(!Node(file,path) || gCfg[file].GetDataType(key)==KvData_None)return true;
    char text[64];gCfg[file].GetString(key,text,sizeof(text));found=true;
    return ParseLevelInteger(text,value,signedValue);
}

bool StoredLevelFloat(ConfigFile file,const char[] path,const char[] key,float fallback,float &value,bool &found)
{
    value=fallback;found=false;
    if(!Node(file,path) || gCfg[file].GetDataType(key)==KvData_None)return true;
    char text[64];gCfg[file].GetString(key,text,sizeof(text));TrimString(text);found=true;
    // All supported scalar coefficients fit this bound; it also rejects NaN/Inf.
    if(!text[0] || StringToFloatEx(text,value)!=strlen(text) || !(value>=-1000000.0 && value<=1000000.0))return false;
    if((StrEqual(key,"fire_interval_min") || StrEqual(key,"fire_interval_max")) && value>0.0 && value<0.1)value=0.1;
    if((StrEqual(key,"restore_interval_seconds") || StrEqual(key,"restore_interval_seconds_min")
        || StrEqual(key,"restore_interval_seconds_max")) && value<1.0)value=1.0;
    return true;
}

bool ReadLevelGrowth(ConfigFile file,const char[] profile,bool &growth,int &anchor,int &maximum)
{
    char path[192];Format(path,sizeof(path),"%s/Growth",profile);
    growth=Node(file,path);anchor=0;maximum=0;if(!growth)return true;
    bool found;
    if(!StoredLevelInt(file,path,"max_level",0,maximum,found) || !found || maximum<1)return false;
    if(!StoredLevelInt(file,path,"anchor_level",0,anchor,found) || !found || anchor<1 || anchor>maximum)return false;
    Format(path,sizeof(path),"%s/Levels/%d",profile,anchor);return Node(file,path);
}

bool LevelAvailable(ConfigFile file,const char[] profile,int level)
{
    if(level<1 || level>LEVEL_CELL_LIMIT)return false;
    bool growth;int anchor,maximum;if(!ReadLevelGrowth(file,profile,growth,anchor,maximum))return false;
    if(growth && level>maximum)return false;
    char path[192];Format(path,sizeof(path),"%s/Levels/%d",profile,level);
    return Node(file,path) || (growth && level>anchor);
}

stock int ProfileMaximumLevel(ConfigFile file,const char[] profile)
{
    bool growth;int anchor,maximum;if(!ReadLevelGrowth(file,profile,growth,anchor,maximum))return 0;
    if(growth)return maximum;
    char path[192];Format(path,sizeof(path),"%s/Levels",profile);
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey())return 0;
    do
    {
        char key[32];int level;gCfg[file].GetSectionName(key,sizeof(key));
        if(!ParseLevelInteger(key,level) || level<1)return 0;
        if(level>maximum)maximum=level;
    }while(gCfg[file].GotoNextKey());
    return maximum;
}

bool IntegerLevelBounds(const char[] key,int &minimum,int &maximum)
{
    minimum=0;maximum=LEVEL_CELL_LIMIT;
    if(StrEqual(key,"max_hp") || StrEqual(key,"initial_hp") || StrEqual(key,"minimum_base_level")){minimum=1;return true;}
    if(StrEqual(key,"burn_damage")){maximum=1000000;return true;}
    if(StrEqual(key,"power_priority")){maximum=1000;return true;}
    if(StrEqual(key,"light_brightness")){maximum=8;return true;}
    if(StrEqual(key,"build_limit") || StrEqual(key,"build_limit_bonus")){maximum=MAX_BUILDINGS;return true;}
    if(StrEqual(key,"restore_amount")){maximum=1000000;return true;}
    if(StrEqual(key,"max_per_base")){minimum=1;maximum=MAX_BUILDINGS;return true;}
    if(StrEqual(key,"decay_amount") || StrEqual(key,"inactive_hp")){maximum=1000000;return true;}
    if(StrEqual(key,"fuel_capacity") || StrEqual(key,"fuel_per_wood") || StrEqual(key,"fuel_wood_cost")){minimum=1;return true;}
    if(StrEqual(key,"no_decay_at_max_level")){maximum=1;return true;}
    return false;
}

bool FloatLevelBounds(const char[] key,float &minimum,float &maximum)
{
    minimum=0.0;maximum=1000000.0;
    if(StrEqual(key,"damage"))return true;
    if(StrEqual(key,"range") || StrEqual(key,"blast_radius")){maximum=8192.0;return true;}
    if(StrEqual(key,"fire_interval") || StrEqual(key,"burn_interval")){minimum=0.1;maximum=3600.0;return true;}
    if(StrEqual(key,"cone_half_angle_degrees")){maximum=180.0;return true;}
    if(StrEqual(key,"burn_duration")){maximum=3600.0;return true;}
    for(int i=7;i<11;i++)if(StrEqual(key,sLevelFloatFields[i])){maximum=100.0;return true;}
    if(StrEqual(key,"power_output") || StrEqual(key,"power_required")){maximum=100000.0;return true;}
    if(StrEqual(key,"light_radius")){minimum=32.0;maximum=2048.0;return true;}
    if(StrEqual(key,"projectile_speed")){minimum=100.0;maximum=3000.0;return true;}
    if(StrEqual(key,"build_range_bonus")){maximum=8192.0;return true;}
    if(StrEqual(key,"build_half_extents_x") || StrEqual(key,"build_half_extents_y")){minimum=1.0;maximum=8192.0;return true;}
    if(StrEqual(key,"restore_interval_seconds")){minimum=1.0;maximum=3600.0;return true;}
    if(StrEqual(key,"safe_radius")){minimum=16.0;maximum=4096.0;return true;}
    if(StrEqual(key,"safe_height")){minimum=16.0;maximum=1024.0;return true;}
    if(StrEqual(key,"decay_interval_seconds")){minimum=0.25;maximum=3600.0;return true;}
    if(StrEqual(key,"charge_seconds")){minimum=0.1;maximum=10.0;return true;}
    if(StrEqual(key,"field_damage")){maximum=100000.0;return true;}
    if(StrEqual(key,"field_duration")){maximum=30.0;return true;}
    if(StrEqual(key,"field_radius")){maximum=512.0;return true;}
    if(StrEqual(key,"field_interval")){minimum=0.1;maximum=5.0;return true;}
    return false;
}

bool ReadLevelInt(ConfigFile file,const char[] profile,int level,const char[] key,int fallback,int &value)
{
    value=fallback;int safeMin,safeMax;
    if(!IntegerLevelBounds(key,safeMin,safeMax) || !LevelAvailable(file,profile,level))return false;
    char path[192],field[96];bool found;
    Format(path,sizeof(path),"%s/Levels/%d",profile,level);
    if(!StoredLevelInt(file,path,key,fallback,value,found))return false;
    if(found)return value>=safeMin && value<=safeMax;
    bool growth;int anchor,maximum;if(!ReadLevelGrowth(file,profile,growth,anchor,maximum))return false;
    if(!growth || level<=anchor)return value>=safeMin && value<=safeMax;
    Format(path,sizeof(path),"%s/Levels/%d",profile,anchor);
    if(!StoredLevelInt(file,path,key,fallback,value,found) || value<safeMin || value>safeMax)return false;
    int increment,lower,upper;
    Format(path,sizeof(path),"%s/Growth/Stats",profile);Format(field,sizeof(field),"%s_per_level",key);
    if(!StoredLevelInt(file,path,field,0,increment,found,true))return false;
    Format(field,sizeof(field),"%s_min",key);if(!StoredLevelInt(file,path,field,safeMin,lower,found))return false;
    Format(field,sizeof(field),"%s_max",key);if(!StoredLevelInt(file,path,field,safeMax,upper,found))return false;
    if(lower<safeMin || upper>safeMax || lower>upper)return false;
    // Clamp before multiplying: even level 2,000,000,000 cannot wrap an integer.
    int steps=level-anchor;
    if(increment>0)value=value>=upper || steps>(upper-value)/increment ? upper : value+steps*increment;
    else if(increment<0){int magnitude=-increment;value=value<=lower || steps>(value-lower)/magnitude ? lower : value-steps*magnitude;}
    if(value<lower)value=lower;if(value>upper)value=upper;
    return true;
}

bool ReadLevelFloat(ConfigFile file,const char[] profile,int level,const char[] key,float fallback,float &value)
{
    value=fallback;float safeMin,safeMax;
    if(!FloatLevelBounds(key,safeMin,safeMax) || !LevelAvailable(file,profile,level))return false;
    char path[192],field[96];bool found;
    Format(path,sizeof(path),"%s/Levels/%d",profile,level);
    if(!StoredLevelFloat(file,path,key,fallback,value,found))return false;
    if(found)
    {
        if(StrEqual(key,"fire_interval") && value>0.0 && value<0.1)value=0.1;
        return value>=safeMin && value<=safeMax;
    }
    bool growth;int anchor,maximum;if(!ReadLevelGrowth(file,profile,growth,anchor,maximum))return false;
    if(!growth || level<=anchor)return value>=safeMin && value<=safeMax;
    Format(path,sizeof(path),"%s/Levels/%d",profile,anchor);
    if(!StoredLevelFloat(file,path,key,fallback,value,found))return false;
    if(StrEqual(key,"fire_interval") && value>0.0 && value<0.1)value=0.1;
    if(!(value>=safeMin && value<=safeMax))return false;
    float increment,lower,upper;
    Format(path,sizeof(path),"%s/Growth/Stats",profile);Format(field,sizeof(field),"%s_per_level",key);
    if(!StoredLevelFloat(file,path,field,0.0,increment,found))return false;
    Format(field,sizeof(field),"%s_min",key);if(!StoredLevelFloat(file,path,field,safeMin,lower,found))return false;
    Format(field,sizeof(field),"%s_max",key);if(!StoredLevelFloat(file,path,field,safeMax,upper,found))return false;
    if(!(lower>=safeMin && upper<=safeMax && lower<=upper))return false;
    // Bounded finite coefficients * a bounded positive cell stay finite as floats.
    value+=increment*float(level-anchor);
    if(value<lower)value=lower;if(value>upper)value=upper;
    return value>=lower && value<=upper;
}

bool ReadLevelUpgradeCost(ConfigFile file,const char[] profile,int level,int cost[MATERIALS])
{
    for(int m=0;m<MATERIALS;m++)cost[m]=0;
    if(!LevelAvailable(file,profile,level))return false;
    char path[192];Format(path,sizeof(path),"%s/Levels/%d/upgrade_cost",profile,level);
    bool explicitCost=Node(file,path),growth,found;int anchor,maximum;
    if(!ReadLevelGrowth(file,profile,growth,anchor,maximum))return false;
    if(!explicitCost && (!growth || level<=anchor))
    {
        if(level==1)return true;
        return false;
    }
    int steps;
    if(!explicitCost)
    {
        steps=level-anchor;
        Format(path,sizeof(path),"%s/Levels/%d/upgrade_cost",profile,anchor);
        if(anchor>1 && !Node(file,path))return false;
    }
    if(!ValidateMaterialCostKeys(file,path))return false;
    char growthPath[192];Format(growthPath,sizeof(growthPath),"%s/Growth/UpgradeCost",profile);
    for(int m=0;m<gMaterialCount;m++)
    {
        int amount,increment;char field[64];
        if(!StoredLevelInt(file,path,gMaterials[m].id,0,amount,found))return false;
        if(!explicitCost)
        {
            Format(field,sizeof(field),"%s_per_level",gMaterials[m].id);
            if(!StoredLevelInt(file,growthPath,field,0,increment,found))return false;
            if(increment>0 && steps>(LEVEL_CELL_LIMIT-amount)/increment)return false;
            amount+=steps*increment;
        }
        cost[m]=amount;
    }
    return true;
}

// Both the vector alias and scalar fields resolve through the same anchor.
// Scalar x/y keys (and their Growth fields) are the canonical extensible form.
bool ReadBuildHalfExtents(ConfigFile file,const char[] profile,int level,float extents[3])
{
    if(!LevelAvailable(file,profile,level))return false;
    bool growth;int anchor,maximum;if(!ReadLevelGrowth(file,profile,growth,anchor,maximum))return false;
    float defaults[3]={500.0,400.0,0.0};char path[192];
    Format(path,sizeof(path),"%s/Levels/%d",profile,level);
    bool explicitVector=Node(file,path) && gCfg[file].GetDataType("build_half_extents")!=KvData_None;
    if(!explicitVector && growth && level>anchor)Format(path,sizeof(path),"%s/Levels/%d",profile,anchor);
    if(Node(file,path) && gCfg[file].GetDataType("build_half_extents")!=KvData_None)
    {
        char text[160],values[4][40];gCfg[file].GetString("build_half_extents",text,sizeof(text));TrimString(text);
        ReplaceString(text,sizeof(text),"\t"," ");while(StrContains(text,"  ")>=0)ReplaceString(text,sizeof(text),"  "," ");
        if(ExplodeString(text," ",values,4,40)!=3)return false;
        for(int axis=0;axis<3;axis++)
            if(!values[axis][0] || StringToFloatEx(values[axis],defaults[axis])!=strlen(values[axis])
                || !(defaults[axis]>=0.0 && defaults[axis]<=8192.0))return false;
        if(defaults[2]!=0.0)return false;
    }
    for(int axis=0;axis<2;axis++)
    {
        char key[32];strcopy(key,sizeof(key),axis==0 ? "build_half_extents_x" : "build_half_extents_y");
        if(explicitVector)
        {
            bool found;Format(path,sizeof(path),"%s/Levels/%d",profile,level);
            if(!StoredLevelFloat(file,path,key,defaults[axis],extents[axis],found))return false;
        }
        else if(!ReadLevelFloat(file,profile,level,key,defaults[axis],extents[axis]))return false;
        if(!(extents[axis]>0.0 && extents[axis]<=8192.0))return false;
    }
    extents[2]=0.0;return true;
}

bool ValidateMaterialCostKeys(ConfigFile file,const char[] path)
{
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey(false))return true;
    do
    {
        char key[64],text[64];int amount;gCfg[file].GetSectionName(key,sizeof(key));gCfg[file].GetString(NULL_STRING,text,sizeof(text));
        if(FindMaterial(key)<0 || !ParseLevelInteger(text,amount))return false;
    }while(gCfg[file].GotoNextKey(false));
    return true;
}

void BuildingProfilePath(int building,char[] path,int length)
{
    if(gBuildings[building].kind==Kind_Base && gBuildings[building].file==C_Bases)
        Format(path,length,"Profiles/%s",gCores[gBuildings[building].core].profile);
    else Format(path,length,"Types/%s",gBuildings[building].type);
}

bool BuildingLevelAvailable(int building,int level)
{
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used)return false;
    if(gBuildings[building].configRevision!=gConfigRevision)return false;
    char path[128];BuildingProfilePath(building,path,sizeof(path));
    return LevelAvailable(gBuildings[building].file,path,level);
}

bool ReadBuildingLevelQuote(int building,int level,int cost[MATERIALS],int &nextMaxHP)
{
    if(!BuildingLevelAvailable(building,level))return false;
    char path[128];BuildingProfilePath(building,path,sizeof(path));
    return ReadLevelInt(gBuildings[building].file,path,level,"max_hp",0,nextMaxHP)
        && ReadLevelUpgradeCost(gBuildings[building].file,path,level,cost);
}

bool ValidateResolvedLevel(ConfigFile file,const char[] profile,int level)
{
    int maximum,value,cost[MATERIALS];float decimal,extents[3];
    if(!ReadLevelInt(file,profile,level,"max_hp",0,maximum)
        || !ReadLevelInt(file,profile,level,"initial_hp",maximum,value) || value>maximum)return false;
    if(!ReadLevelInt(file,profile,level,"burn_damage",0,value)
        || !ReadLevelInt(file,profile,level,"minimum_base_level",1,value)
        || !ReadLevelInt(file,profile,level,"power_priority",10,value)
        || !ReadLevelInt(file,profile,level,"light_brightness",2,value)
        || !ReadLevelInt(file,profile,level,"build_limit",24,value)
        || !ReadLevelInt(file,profile,level,"build_limit_bonus",0,value)
        || !ReadLevelInt(file,profile,level,"restore_amount",0,value)
        || !ReadLevelInt(file,profile,level,"max_per_base",2,value))return false;
    for(int f=0;f<sizeof(sLevelFloatFields);f++)
        if(!ReadLevelFloat(file,profile,level,sLevelFloatFields[f],sLevelFloatDefaults[f],decimal))return false;
    if(file==C_Campfires)
    {
        float radius,height,interval;int amount,threshold;bool noDecay;
        if(!ReadCampfireProfileLevel(profile,level,radius,height,amount,interval,threshold,noDecay) || threshold>=maximum)return false;
    }
    return ReadBuildHalfExtents(file,profile,level,extents) && ReadLevelUpgradeCost(file,profile,level,cost);
}

bool ValidateGrowthFieldNames(ConfigFile file,const char[] profile)
{
    char path[192];Format(path,sizeof(path),"%s/Growth/Stats",profile);
    ArrayList keys=new ArrayList(ByteCountToCells(96));
    if(Node(file,path) && gCfg[file].GotoFirstSubKey(false))
    {
        do{char field[96];gCfg[file].GetSectionName(field,sizeof(field));keys.PushString(field);}while(gCfg[file].GotoNextKey(false));
    }
    for(int i=0;i<keys.Length;i++)
    {
        char field[96],base[96];keys.GetString(i,field,sizeof(field));strcopy(base,sizeof(base),field);
        int cut=StrContains(base,"_per_level"),mode;
        if(cut<0){cut=StrContains(base,"_min");mode=1;}
        if(cut<0){cut=StrContains(base,"_max");mode=2;}
        if(cut<=0){delete keys;return false;}
        char suffix[16];strcopy(suffix,sizeof(suffix),base[cut]);base[cut]=0;
        if(!StrEqual(suffix,mode==0 ? "_per_level" : (mode==1 ? "_min" : "_max"))){delete keys;return false;}
        int lower,upper,integer;float low,high,decimal;bool found;
        if(IntegerLevelBounds(base,lower,upper))
        {
            if(!StoredLevelInt(file,path,field,0,integer,found,mode==0) || !found
                || (mode!=0 && (integer<lower || integer>upper))){delete keys;return false;}
        }
        else if(FloatLevelBounds(base,low,high))
        {
            if(!StoredLevelFloat(file,path,field,0.0,decimal,found) || !found
                || (mode!=0 && !(decimal>=low && decimal<=high))){delete keys;return false;}
        }
        else{delete keys;return false;}
    }
    delete keys;
    // Validate paired formula limits even when Growth/max_level equals its anchor.
    for(int i=0;i<sizeof(sLevelIntegerFields);i++)
    {
        int safeMin,safeMax,lower,upper;bool found;char field[96];
        IntegerLevelBounds(sLevelIntegerFields[i],safeMin,safeMax);
        Format(field,sizeof(field),"%s_min",sLevelIntegerFields[i]);
        if(!StoredLevelInt(file,path,field,safeMin,lower,found))return false;
        Format(field,sizeof(field),"%s_max",sLevelIntegerFields[i]);
        if(!StoredLevelInt(file,path,field,safeMax,upper,found) || lower>upper)return false;
    }
    for(int i=0;i<sizeof(sLevelFloatFields);i++)
    {
        float safeMin,safeMax,lower,upper;bool found;char field[96];
        FloatLevelBounds(sLevelFloatFields[i],safeMin,safeMax);
        Format(field,sizeof(field),"%s_min",sLevelFloatFields[i]);
        if(!StoredLevelFloat(file,path,field,safeMin,lower,found))return false;
        Format(field,sizeof(field),"%s_max",sLevelFloatFields[i]);
        if(!StoredLevelFloat(file,path,field,safeMax,upper,found) || lower>upper)return false;
    }
    keys=new ArrayList(ByteCountToCells(96));
    Format(path,sizeof(path),"%s/Growth/UpgradeCost",profile);
    if(Node(file,path) && gCfg[file].GotoFirstSubKey(false))
    {
        do{char field[96];gCfg[file].GetSectionName(field,sizeof(field));keys.PushString(field);}while(gCfg[file].GotoNextKey(false));
    }
    for(int i=0;i<keys.Length;i++)
    {
        char field[96];keys.GetString(i,field,sizeof(field));bool known;
        for(int m=0;m<gMaterialCount;m++){char expected[64];Format(expected,sizeof(expected),"%s_per_level",gMaterials[m].id);if(StrEqual(field,expected))known=true;}
        int value;bool found;
        if(!known || !StoredLevelInt(file,path,field,0,value,found) || !found){delete keys;return false;}
    }
    delete keys;return true;
}

bool ValidateLevelProfile(ConfigFile file,const char[] profile)
{
    bool growth;int anchor,maximum;
    if(!ReadLevelGrowth(file,profile,growth,anchor,maximum) || !ValidateGrowthFieldNames(file,profile))return false;
    char path[192];Format(path,sizeof(path),"%s/Levels",profile);
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey())return false;
    // The list contains only explicitly written rows, never 1..Growth/max_level.
    ArrayList levels=new ArrayList();
    do
    {
        char key[32],canonical[32];int level;gCfg[file].GetSectionName(key,sizeof(key));
        if(!ParseLevelInteger(key,level) || level<1){delete levels;return false;}
        IntToString(level,canonical,sizeof(canonical));if(!StrEqual(key,canonical)){delete levels;return false;}
        if(growth && level>maximum){delete levels;return false;}
        levels.Push(level);
    }while(gCfg[file].GotoNextKey());
    SortADTArray(levels,Sort_Ascending,Sort_Integer);
    int expected=1,previous;
    for(int i=0;i<levels.Length;i++)
    {
        int level=levels.Get(i);
        if(level==previous || (growth && level<=anchor && level!=expected)){delete levels;return false;}
        previous=level;if(growth && level<=anchor)expected++;
        if(!ValidateResolvedLevel(file,profile,level)){delete levels;return false;}
    }
    delete levels;
    if(!LevelAvailable(file,profile,1) || (growth && expected-1!=anchor))return false;
    return !growth || ValidateResolvedLevel(file,profile,maximum);
}

bool ValidateLevelProfiles()
{
    ConfigFile files[]={C_Bases,C_Turrets,C_Flames,C_Fences,C_Mines,C_Amplifiers,C_Generators,C_Lights,C_Grenades,C_Expanders,C_Repair,C_Heal,C_Armor,C_Beacons,C_Campfires,C_Shelters,C_Tesla,C_Plasma,C_Watchtowers,C_Workbenches};
    for(int f=0;f<sizeof(files);f++)
    {
        char section[16];strcopy(section,sizeof(section),f==0 ? "Profiles" : "Types");
        if(files[f]>=C_Repair && gCfg[files[f]]==null)continue;
        if(!Node(files[f],section) || !gCfg[files[f]].GotoFirstSubKey())return ConfigFailure("building level profiles missing");
        ArrayList profiles=new ArrayList(ByteCountToCells(128));
        do
        {
            char id[64],path[128];gCfg[files[f]].GetSectionName(id,sizeof(id));Format(path,sizeof(path),"%s/%s",section,id);profiles.PushString(path);
        }while(gCfg[files[f]].GotoNextKey());
        for(int p=0;p<profiles.Length;p++)
        {
            char path[128];profiles.GetString(p,path,sizeof(path));
            if(!ValidateLevelProfile(files[f],path))
            {
                if(files[f]==C_Bases){delete profiles;return ConfigFailure(path);}
                LogError("[Shelter] Disabled building type %s in config %d: invalid level/growth/material cost",path,files[f]);
            }
            else WarnNormalizedFireIntervals(files[f],path);
        }
        delete profiles;
    }
    return true;
}

void WarnNormalizedFireIntervals(ConfigFile file,const char[] profile)
{
    char path[192];Format(path,sizeof(path),"%s/Levels",profile);
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey())return;
    do
    {
        float interval=gCfg[file].GetFloat("fire_interval",0.5);
        if(interval>0.0 && interval<0.1)
        {
            char level[32];gCfg[file].GetSectionName(level,sizeof(level));
            LogMessage("[Shelter] %s/Levels/%s/fire_interval %.4f normalized to 0.1 seconds",profile,level,interval);
        }
    }while(gCfg[file].GotoNextKey());
}
