// Map positions are data. Definitions retain independent lifecycle state even
// when their ordinal wraps around onto a previously assigned spawn point.
float gBossSpawnOrigins[BOSS_SPAWN_POINT_COUNT][3];
float gBossSpawnAngles[BOSS_SPAWN_POINT_COUNT][3];

int BossSpawnPointForOrdinal(int ordinal)
{
    return ordinal<0 ? -1 : ordinal%BOSS_SPAWN_POINT_COUNT;
}

bool BossReadPointVector(KeyValues kv,const char[] key,float vector[3],bool optional=false,bool angles=false)
{
    for(int axis=0;axis<3;axis++)vector[axis]=0.0;
    if(kv.GetDataType(key)==KvData_None)return optional;
    char text[192];kv.GetString(key,text,sizeof(text));
    if(strlen(text)>=sizeof(text)-1)return false;
    int cursor,length=strlen(text);
    for(int axis=0;axis<3;axis++)
    {
        while(cursor<length && text[cursor]<=32)cursor++;
        int start=cursor;
        while(cursor<length && text[cursor]>32)cursor++;
        int count=cursor-start;
        if(count<1 || count>=64)return false;
        char token[64];
        for(int i=0;i<count;i++)token[i]=text[start+i];
        token[count]='\0';
        float value;
        if(StringToFloatEx(token,value)!=count || (view_as<int>(value)&0x7f800000)==0x7f800000)return false;
        float bound=angles ? 360.0 : 15000.0;
        if(value < -bound || value > bound)return false;
        vector[axis]=value;
    }
    while(cursor<length && text[cursor]<=32)cursor++;
    return cursor==length;
}

bool BossPointFieldsValid(KeyValues kv)
{
    if(!kv.GotoFirstSubKey(false))return false;
    bool origin,angles,valid=true;
    do
    {
        char key[64];kv.GetSectionName(key,sizeof(key));
        if(kv.GetDataType(NULL_STRING)==KvData_None){valid=false;break;}
        if(StrEqual(key,"origin")){if(origin){valid=false;break;}origin=true;}
        else if(StrEqual(key,"angles")){if(angles){valid=false;break;}angles=true;}
        else {valid=false;break;}
    }while(kv.GotoNextKey(false));
    kv.GoBack();
    return valid && origin;
}

bool ReadBossSpawnPoints()
{
    if(!Node(C_Map,"BossSpawnPoints") || !gCfg[C_Map].GotoFirstSubKey(false))
        return ConfigFailure("map BossSpawnPoints requires boss1..boss5 with origin coordinates; migrate the map configuration");
    bool seen[BOSS_SPAWN_POINT_COUNT];int count;
    do
    {
        char name[64];gCfg[C_Map].GetSectionName(name,sizeof(name));
        int point=-1;
        for(int i=0;i<BOSS_SPAWN_POINT_COUNT;i++)
        {
            char expected[16];Format(expected,sizeof(expected),"boss%d",i+1);
            if(StrEqual(name,expected)){point=i;break;}
        }
        if(point<0 || seen[point] || gCfg[C_Map].GetDataType(NULL_STRING)!=KvData_None)
            return ConfigFailure("BossSpawnPoints must contain exactly one section each named boss1..boss5");
        if(!BossPointFieldsValid(gCfg[C_Map])
            || !BossReadPointVector(gCfg[C_Map],"origin",gBossSpawnOrigins[point])
            || !BossReadPointVector(gCfg[C_Map],"angles",gBossSpawnAngles[point],true,true))
        {
            char error[192];Format(error,sizeof(error),"BossSpawnPoints/%s: origin needs three finite coordinates within +/-15000; angles optional, three finite values within +/-360",name);
            return ConfigFailure(error);
        }
        seen[point]=true;count++;
    }while(gCfg[C_Map].GotoNextKey(false));
    gCfg[C_Map].GoBack();
    return count==BOSS_SPAWN_POINT_COUNT || ConfigFailure("BossSpawnPoints is missing one or more of boss1..boss5");
}

bool BossConfigIdentifierValid(const char[] value)
{
    int length=strlen(value);
    if(length<1 || length>=64)return false;
    for(int i=0;i<length;i++)
        if(value[i]<=32 || value[i]=='/' || value[i]=='\\' || value[i]==',' || value[i]==':')return false;
    return true;
}

bool ReadBossDefinitions()
{
    gBossCount=0;
    // Every enabled definition belongs to the Boss victory objective. Legacy
    // required_boss_ids is intentionally ignored, including old subset lists.
    if(!Node(C_Map,"Bosses") || !gCfg[C_Map].GotoFirstSubKey(false))return ConfigFailure("Bosses");
    do
    {
        if(gCfg[C_Map].GetDataType(NULL_STRING)!=KvData_None)return ConfigFailure("Bosses entries must be sections");
        char enabled[8];gCfg[C_Map].GetString("enabled",enabled,sizeof(enabled),"1");
        if(!StrEqual(enabled,"0") && !StrEqual(enabled,"1"))return ConfigFailure("Boss enabled must be 0 or 1");
        if(StrEqual(enabled,"0"))continue;
        if(gBossCount>=MAX_BOSSES)return ConfigFailure("boss definitions exceed the NPC tracking capacity");
        BossDef boss;char id[128],profile[128];
        gCfg[C_Map].GetSectionName(id,sizeof(id));gCfg[C_Map].GetString("boss_profile",profile,sizeof(profile));
        if(!BossConfigIdentifierValid(id) || !BossConfigIdentifierValid(profile))
            return ConfigFailure("Boss ID and boss_profile must be nonempty IDs shorter than 64 bytes without whitespace or path separators");
        for(int i=0;i<gBossCount;i++)if(StrEqual(id,gBosses[i].id))return ConfigFailure("duplicate Boss ID");
        strcopy(boss.id,sizeof(boss.id),id);
        char section[128],key[128],publicName[128];Format(section,sizeof(section),"BossTypes/%s",profile);
        Text(C_Bosses,section,"han_config_key",key,sizeof(key));
        Text(C_Bosses,section,"public_name",publicName,sizeof(publicName));
        if(!BossConfigIdentifierValid(key) || !publicName[0] || strlen(publicName)>=sizeof(boss.publicName)
            || StrEqual(publicName,"TO_MATCH_ACTUAL_NAME"))return ConfigFailure("boss profile is missing or has invalid native key/public name");
        strcopy(boss.key,sizeof(boss.key),key);strcopy(boss.publicName,sizeof(boss.publicName),publicName);
        boss.maxHP=Number(C_Bosses,section,"max_hp",50000);
        if(boss.maxHP<1)return ConfigFailure("boss max_hp must be positive");
        boss.savedHP=boss.maxHP;boss.damage=Decimal(C_Bosses,section,"building_damage",120.0);
        boss.interval=Decimal(C_Bosses,section,"building_attack_interval",1.0);
        boss.required=true;
        strcopy(boss.idle,sizeof(boss.idle),"idle");strcopy(boss.attack,sizeof(boss.attack),"attack1");
        boss.spawnPoint=BossSpawnPointForOrdinal(gBossCount);
        boss.origin=gBossSpawnOrigins[boss.spawnPoint];boss.angles=gBossSpawnAngles[boss.spawnPoint];
        gBosses[gBossCount++]=boss;
    }while(gCfg[C_Map].GotoNextKey(false));
    gCfg[C_Map].GoBack();
    return gBossCount>0 || ConfigFailure("Bosses has no enabled entries");
}

int ClaimBossDefinition(const char[] publicName)
{
    for(int i=0;i<gBossCount;i++)if(!gBosses[i].boundThisRound && StrEqual(publicName,gBosses[i].publicName))
    {
        gBosses[i].boundThisRound=true;
        return i;
    }
    return -1;
}

// Sharing one of the five points is intentional. Only other configured Boss
// bodies/visuals are excluded; world brushes, props and other blockers remain.
public bool BossPlacementTraceFilter(int entity,int mask,any ignored)
{
    if(entity==ignored)return false;
    for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && gNPCs[i].boss>=0
        && (Resolve(gNPCs[i].ref)==entity || Resolve(gNPCs[i].visualRef)==entity))return false;
    return true;
}

bool BossSpawnHullClear(const float position[3],const float mins[3],const float maxs[3],int entity)
{
    Handle trace=TR_TraceHullFilterEx(position,position,mins,maxs,MASK_NPCSOLID,BossPlacementTraceFilter,entity);
    bool clear=!TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);delete trace;
    return clear;
}
