// Configuration is parsed into a candidate and only published after every field
// validates. First version permits identical reloads; changes apply next map.
bool SkillsConfigInt(KeyValues kv,const char[] key,int minimum,int maximum,int &value)
{
    char text[64];kv.GetString(key,text,sizeof(text));
    return strlen(text)<sizeof(text)-1 && ParseLevelInteger(text,value) && value>=minimum && value<=maximum;
}

bool SkillsConfigFloat(KeyValues kv,const char[] key,float minimum,float maximum,float &value)
{
    char text[64];kv.GetString(key,text,sizeof(text));if(strlen(text)>=sizeof(text)-1)return false;TrimString(text);
    return text[0] && StringToFloatEx(text,value)==strlen(text) && value>=minimum && value<=maximum;
}

int SkillsFindID(const char[] id)
{
    for(int i=0;i<SKILL_COUNT;i++)if(StrEqual(id,gSkillID[i]))return i;
    return -1;
}

bool SkillsConfigKeys(KeyValues kv,const char[] allowed)
{
    if(!kv.GotoFirstSubKey(false))return true;
    bool valid=true;
    do
    {
        char name[64],token[68];kv.GetSectionName(name,sizeof(name));if(strlen(name)>=sizeof(name)-1){valid=false;break;}Format(token,sizeof(token),"|%s|",name);
        if(StrContains(allowed,token)<0){valid=false;break;}
    }while(kv.GotoNextKey(false));
    kv.GoBack();return valid;
}

bool SkillsParseConfig(KeyValues kv,bool &enabled,int &daily,bool enabledSkill[SKILL_COUNT],
    int maximum[SKILL_COUNT],int cost[SKILL_COUNT],float amount[SKILL_COUNT],float radius[SKILL_COUNT],
    float interval[SKILL_COUNT],float &immunityCap,int respec[MATERIALS])
{
    char root[64];kv.Rewind();kv.GetSectionName(root,sizeof(root));int schema,flag;
    if(!StrEqual(root,"ShelterSkills") || !SkillsConfigKeys(kv,"|schema_version||Rules||Skills||Respec|")
        || !SkillsConfigInt(kv,"schema_version",1,1,schema) || !kv.JumpToKey("Rules"))return false;
    if(!SkillsConfigKeys(kv,"|enabled||points_per_day|") || !SkillsConfigInt(kv,"enabled",0,1,flag)
        || !SkillsConfigInt(kv,"points_per_day",0,1000000,daily))return false;
    enabled=flag!=0;kv.Rewind();if(!kv.JumpToKey("Skills") || !kv.GotoFirstSubKey(false))return false;
    bool seen[SKILL_COUNT];int count;
    do
    {
        char id[64];kv.GetSectionName(id,sizeof(id));if(strlen(id)>=sizeof(id)-1)return false;int skill=SkillsFindID(id);
        if(skill<0 || seen[skill] || !SkillsConfigKeys(kv,"|enabled||max_level||point_cost||amount_per_level||radius_units||interval_seconds||chance_cap|"))return false;
        seen[skill]=true;count++;
        if(!SkillsConfigInt(kv,"enabled",0,1,flag) || !SkillsConfigInt(kv,"max_level",1,1000,maximum[skill])
            || !SkillsConfigInt(kv,"point_cost",1,1000000,cost[skill])
            || !SkillsConfigFloat(kv,"amount_per_level",0.0,1000000.0,amount[skill]))return false;
        enabledSkill[skill]=flag!=0;
        float total=amount[skill]*float(maximum[skill]);
        if(!(total>=0.0 && total<=1000000.0) || maximum[skill]>SKILL_CELL_LIMIT/cost[skill])return false;
        if(skill==view_as<int>(Skill_Speed) && total>2.0)return false;
        if(skill==view_as<int>(Skill_Gather) && total>1000.0)return false;
        if(skill==view_as<int>(Skill_Gravity) && total>10.0)return false;
        if(skill==view_as<int>(Skill_Immunity))
        {
            if(!SkillsConfigFloat(kv,"chance_cap",0.0,1.0,immunityCap) || total>1.0)return false;
        }
        else if(kv.GetDataType("chance_cap")!=KvData_None)return false;
        bool pulse=skill>=view_as<int>(Skill_Medic) && skill<=view_as<int>(Skill_SelfArmor);
        if(pulse)
        {
            if(!SkillsConfigFloat(kv,"interval_seconds",1.0,3600.0,interval[skill]))return false;
            if(skill<=view_as<int>(Skill_Artisan))
            {if(!SkillsConfigFloat(kv,"radius_units",1.0,8192.0,radius[skill]))return false;}
            else if(kv.GetDataType("radius_units")!=KvData_None)return false;
        }
        else if(kv.GetDataType("interval_seconds")!=KvData_None || kv.GetDataType("radius_units")!=KvData_None)return false;
        // Work, healing and max-stat contributions are whole HP/armor units.
        if(skill!=view_as<int>(Skill_Gather) && skill!=view_as<int>(Skill_Speed) && skill!=view_as<int>(Skill_Gravity)
            && skill!=view_as<int>(Skill_Immunity) && FloatAbs(amount[skill]-float(RoundToFloor(amount[skill])))>0.0001)return false;
    }while(kv.GotoNextKey(false));
    if(count!=SKILL_COUNT)return false;
    kv.Rewind();if(!kv.JumpToKey("Respec") || !kv.GotoFirstSubKey(false))return false;
    bool seenMaterial[MATERIALS];
    do
    {
        char id[64],text[64];kv.GetSectionName(id,sizeof(id));if(strlen(id)>=sizeof(id)-1)return false;int material=FindMaterial(id),value;
        if(material<0 || seenMaterial[material])return false;
        kv.GetString(NULL_STRING,text,sizeof(text));
        if(strlen(text)>=sizeof(text)-1 || !ParseLevelInteger(text,value))return false;
        seenMaterial[material]=true;respec[material]=value;
    }while(kv.GotoNextKey(false));
    for(int m=0;m<gMaterialCount;m++)if(!seenMaterial[m])return false;
    int spendCap;
    for(int skill=0;skill<SKILL_COUNT;skill++)
    {
        int spend=maximum[skill]*cost[skill];
        if(spend>SKILL_CELL_LIMIT-spendCap)return false;spendCap+=spend;
    }
    return true;
}

bool SkillsLoadConfig()
{
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieShelter/skills.cfg");
    KeyValues kv=new KeyValues("ShelterSkills");
    bool enabled,enabledSkill[SKILL_COUNT];int daily,maximum[SKILL_COUNT],cost[SKILL_COUNT],respec[MATERIALS];
    float amount[SKILL_COUNT],radius[SKILL_COUNT],interval[SKILL_COUNT],immunityCap;
    bool valid=kv.ImportFromFile(path) && SkillsParseConfig(kv,enabled,daily,enabledSkill,maximum,cost,amount,radius,interval,immunityCap,respec);delete kv;
    if(!valid){LogError("[ShelterSkills] skills.cfg rejected: invalid/missing field, unknown skill/material, or out-of-range value; previous effective configuration retained");return false;}
    if(gSkillsConfigReady)
    {
        bool same=enabled==gSkillsEnabled && daily==gSkillsPointsPerDay && immunityCap==gSkillsImmunityCap;
        for(int s=0;s<SKILL_COUNT;s++)if(enabledSkill[s]!=gSkillEnabled[s] || maximum[s]!=gSkillMaxLevel[s] || cost[s]!=gSkillCost[s]
            || amount[s]!=gSkillAmount[s] || radius[s]!=gSkillRadius[s] || interval[s]!=gSkillInterval[s])same=false;
        for(int m=0;m<MATERIALS;m++)if(respec[m]!=gSkillRespecCost[m])same=false;
        if(!same){LogMessage("[ShelterSkills] configuration change deferred until next map session; account levels, original purchase costs and active configuration retained");return false;}
        return true;
    }
    gSkillsEnabled=enabled;gSkillsPointsPerDay=daily;gSkillsImmunityCap=immunityCap;
    for(int s=0;s<SKILL_COUNT;s++)
    {
        gSkillEnabled[s]=enabledSkill[s];gSkillMaxLevel[s]=maximum[s];gSkillCost[s]=cost[s];
        gSkillAmount[s]=amount[s];gSkillRadius[s]=radius[s];gSkillInterval[s]=interval[s];
    }
    for(int m=0;m<MATERIALS;m++)gSkillRespecCost[m]=respec[m];
    gSkillsConfigReady=true;gSkillsConfigRevision++;SkillsBudgetConfigReady();
    if(gSkillsRows==null){gSkillsAccounts=new StringMap();gSkillsRows=new ArrayList(SKILL_ACCOUNT_CELLS);}
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client))SkillsBindClient(client);
    return true;
}
