static const char sFiles[][]={"mode.cfg","lifecycle.cfg","resources.cfg","zombies.cfg","days.cfg","bosses.cfg","bases.cfg","turrets.cfg","flame_turrets.cfg","fences.cfg","mines.cfg","drops.cfg","maps/{map}.cfg"};
static const char sRoots[][]={"ShelterMode","ShelterLifecycle","ShelterResources","ShelterZombies","ShelterDays","ShelterBosses","ShelterBases","ShelterTurrets","ShelterFlameTurrets","ShelterFences","ShelterMines","ShelterDrops","ShelterMap"};

bool Node(ConfigFile file, const char[] path)
{
    if(gCfg[file]==null)return false;
    gCfg[file].Rewind();
    if(!path[0])return true;
    char pieces[8][64];int count=ExplodeString(path,"/",pieces,sizeof(pieces),sizeof(pieces[]));
    for(int i=0;i<count;i++)if(!gCfg[file].JumpToKey(pieces[i]))return false;
    return true;
}

int Number(ConfigFile file,const char[] path,const char[] key,int fallback=0)
{
    return Node(file,path) ? gCfg[file].GetNum(key,fallback) : fallback;
}

float Decimal(ConfigFile file,const char[] path,const char[] key,float fallback=0.0)
{
    return Node(file,path) ? gCfg[file].GetFloat(key,fallback) : fallback;
}

void Text(ConfigFile file,const char[] path,const char[] key,char[] buffer,int size,const char[] fallback="")
{
    if(Node(file,path))gCfg[file].GetString(key,buffer,size,fallback);
    else strcopy(buffer,size,fallback);
}

void ReleaseConfigs()
{
    for(int i=0;i<view_as<int>(C_Count);i++){delete gCfg[i];gCfg[i]=null;}
}

bool ConfigFailure(const char[] message)
{
    LogError("[Shelter] 配置拒绝启动: %s",message);
    return false;
}

bool LoadConfigs()
{
    char path[PLATFORM_MAX_PATH],relative[PLATFORM_MAX_PATH];
    GetCurrentMap(gMap,sizeof(gMap));
    Format(relative,sizeof(relative),"configs/HanZombieShelter/maps/%s.cfg",gMap);
    BuildPath(Path_SM,path,sizeof(path),"%s",relative);
    if(!FileExists(path))return false; // Other maps remain under HZS control.
    ReleaseConfigs();
    for(int i=0;i<view_as<int>(C_Count);i++)
    {
        strcopy(relative,sizeof(relative),sFiles[i]);ReplaceString(relative,sizeof(relative),"{map}",gMap);
        BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieShelter/%s",relative);
        gCfg[i]=new KeyValues(sRoots[i]);
        if(!gCfg[i].ImportFromFile(path))return ConfigFailure(path);
        char root[64];gCfg[i].GetSectionName(root,sizeof(root));
        if(!StrEqual(root,sRoots[i]) || gCfg[i].GetNum("schema_version")!=1)return ConfigFailure("root/schema_version mismatch");
    }
    if(!Number(C_Mode,"","enabled_for_configured_maps",1))return false;
    gDaySeconds=Decimal(C_Mode,"Rules","day_seconds",180.0);
    gNightSeconds=Decimal(C_Mode,"Rules","night_seconds",180.0);
    gWarningSeconds=Decimal(C_Mode,"Rules","warning_seconds_before_night",30.0);
    gExposureEnabled=Number(C_Mode,"Rules","night_exposure_enabled",1)!=0;
    gExposureDamage=Number(C_Mode,"Rules","night_exposure_damage",10);
    gExposureInterval=Decimal(C_Mode,"Rules","night_exposure_interval_seconds",1.0);
    gUseDistance=Decimal(C_Mode,"Interactions","maximum_distance",128.0);
    gUseCooldown=Decimal(C_Mode,"Interactions","cooldown_seconds",0.3);
    gGatherCooldown=Decimal(C_Resources,"Gathering","cooldown_seconds",0.5);
    gRepairCooldown=Decimal(C_Bases,"Profiles/shelter_basic","repair_interval_seconds",0.5);
    gSummonLimit=Number(C_Mode,"ManagedDefensePolicy","max_summons_team_test_budget",20);
    gInventoryCap=Number(C_Resources,"Inventory","capacity",0);
    gPopulationLimit=Number(C_Zombies,"Population","raider_alive_limit",60);
    gNativePoolSize=Number(C_Zombies,"NativePool","initial_ordinary_count",60);
    if(!Node(C_Zombies,"NativePool"))return ConfigFailure("NativePool required; generation APIs are not used in this version");
    gCfg[C_Zombies].GetVector("parking_origin",gParking);
    if(gDaySeconds<1.0 || gNightSeconds<1.0 || gExposureInterval<0.1 || gExposureDamage<0
        || gUseDistance<16.0 || gUseDistance>512.0 || gUseCooldown<0.1 || gGatherCooldown<0.1
        || gPopulationLimit<1 || gPopulationLimit>(MAX_NPCS-MAX_BOSSES)/2 || gNativePoolSize<gPopulationLimit
        || gNativePoolSize>(MAX_NPCS-MAX_BOSSES)/2 || gSummonLimit<1 || gSummonLimit>MAX_BUILDINGS-MAX_CORES
        || gInventoryCap<0)return ConfigFailure("invalid time/range/entity limits");
    for(int axis=0;axis<3;axis++)if(FloatAbs(gParking[axis])>15000.0)return ConfigFailure("parking must remain inside engine +/-15000 bounds");
    if(FloatAbs(gParking[0])<8000.0 && FloatAbs(gParking[1])<8000.0)return ConfigFailure("parking point must be outside playable area");
    gDestroySummons=Number(C_Lifecycle,"Campaign","destroy_summons_on_failure",0)!=0;
    char policy[64];Text(C_Lifecycle,"Campaign","failure_policy",policy,sizeof(policy),"rollback_one_day");
    if(!StrEqual(policy,"rollback_one_day") && !StrEqual(policy,"lose_and_change_map"))return ConfigFailure("failure_policy");
    gDirectLoss=StrEqual(policy,"lose_and_change_map");
    gRollbackDays=Number(C_Lifecycle,"Campaign","rollback_days",1);
    gMinimumDay=Number(C_Lifecycle,"Campaign","minimum_day",1);
    gRecoveryFraction=Decimal(C_Lifecycle,"BaseRecovery","upgraded_hp_fraction",0.5);
    gOutcomeSeconds=Decimal(C_Lifecycle,"Outcome","display_seconds",8.0);
    if(gRecoveryFraction<=0.0 || gRecoveryFraction>=1.0 || gOutcomeSeconds<1.0 || gRollbackDays<0 || gMinimumDay<1)return ConfigFailure("failure/recovery ranges");
    Text(C_Lifecycle,"Outcome","victory_message",gVictoryText,sizeof(gVictoryText),"你战胜了Boss，本地图已经肃清");
    Text(C_Lifecycle,"Outcome","loss_message",gFailureText,sizeof(gFailureText),"你失败了，此地被丧尸统治");
    Text(C_Mode,"HumanTeamPolicy","t_client_spawn_policy",gSpawnPolicy,sizeof(gSpawnPolicy),"native_position");
    if(!StrEqual(gSpawnPolicy,"native_position") && !StrEqual(gSpawnPolicy,"alive_base") && !StrEqual(gSpawnPolicy,"random_ct_spawn"))return ConfigFailure("t_client_spawn_policy");
    return ReadMaterials() && ReadMapData() && ReadRecipes() && ValidateDayRanges() && ValidateNativeBridge();
}

bool ReadMaterials()
{
    static const char ids[][]={"wood","iron","copper","silver","gold"};
    for(int i=0;i<MATERIALS;i++)
    {
        char path[96];Format(path,sizeof(path),"Materials/%s",ids[i]);
        if(!Node(C_Resources,path))return ConfigFailure(path);
        strcopy(gMaterials[i].id,sizeof(gMaterials[].id),ids[i]);
        gCfg[C_Resources].GetString("display_name",gMaterials[i].label,sizeof(gMaterials[].label),ids[i]);
        gCfg[C_Resources].GetString("model",gMaterials[i].model,sizeof(gMaterials[].model));
        gMaterials[i].quota=gCfg[C_Resources].GetNum("hits_per_cycle");
        gMaterials[i].yield=gCfg[C_Resources].GetNum("yield_per_successful_hit");
        gMaterials[i].grow=gCfg[C_Resources].GetFloat("grow_seconds");
        gMaterials[i].hold=gCfg[C_Resources].GetFloat("hold_seconds");
        gMaterials[i].scale=gCfg[C_Resources].GetFloat("model_scale",1.0);
        char rgb[64],parts[3][16];gCfg[C_Resources].GetString("render_rgb",rgb,sizeof(rgb),"255 255 255");
        if(ExplodeString(rgb," ",parts,3,16)!=3)return ConfigFailure("material render_rgb");
        for(int axis=0;axis<3;axis++)gMaterials[i].rgb[axis]=StringToInt(parts[axis]);
        char curve[32];gCfg[C_Resources].GetString("render_alpha_curve",curve,sizeof(curve),"linear");
        gMaterials[i].ease=StrEqual(curve,"ease_in");
        if(gMaterials[i].quota<1 || gMaterials[i].yield<1 || gMaterials[i].grow<0.25 || gMaterials[i].hold<0.0
           || gMaterials[i].scale<=0.0 || gMaterials[i].scale>4.0 || PrecacheModel(gMaterials[i].model,true)<=0)return ConfigFailure("material model/quota/growth");
    }
    return true;
}

bool ReadMapData()
{
    gCoreCount=0;gNodeCount=0;gBossCount=0;
    if(!Node(C_Map,"Cores") || !gCfg[C_Map].GotoFirstSubKey())return ConfigFailure("Cores");
    do
    {
        if(!gCfg[C_Map].GetNum("enabled",1))continue;
        if(gCoreCount>=MAX_CORES)return ConfigFailure("too many cores");
        CoreDef core;
        gCfg[C_Map].GetSectionName(core.id,sizeof(core.id));
        gCfg[C_Map].GetString("base_profile",core.profile,sizeof(core.profile));
        gCfg[C_Map].GetVector("origin",core.origin);gCfg[C_Map].GetVector("angles",core.angles);
        core.building=-1;gCores[gCoreCount++]=core;
    }while(gCfg[C_Map].GotoNextKey());
    for(int i=0;i<gCoreCount;i++)
    {
        char section[128],safe[64],points[64],profile[128];Format(section,sizeof(section),"Cores/%s",gCores[i].id);
        Text(C_Map,section,"safe_zone",safe,sizeof(safe));Text(C_Map,section,"human_safe_spawns",points,sizeof(points));
        Format(profile,sizeof(profile),"Profiles/%s",gCores[i].profile);
        Text(C_Bases,profile,"reference_model",gCores[i].model,sizeof(gCores[].model));
        if(PrecacheModel(gCores[i].model,true)<=0)return ConfigFailure("base model");
        Format(section,sizeof(section),"SafeZones/%s/Boxes",safe);
        if(!Node(C_Map,section) || !gCfg[C_Map].GotoFirstSubKey())return ConfigFailure(section);
        do
        {
            int n=gCores[i].boxCount;
            if(n>=MAX_BOXES)return ConfigFailure("too many safe boxes");
            gCfg[C_Map].GetVector("mins",gCoreMins[i][n]);gCfg[C_Map].GetVector("maxs",gCoreMaxs[i][n]);
            for(int axis=0;axis<3;axis++)if(gCoreMins[i][n][axis]>=gCoreMaxs[i][n][axis])return ConfigFailure("safe bounds");
            gCores[i].boxCount++;
        }while(gCfg[C_Map].GotoNextKey());
        Format(section,sizeof(section),"HumanSafeSpawns/%s/Points",points);
        if(!Node(C_Map,section) || !gCfg[C_Map].GotoFirstSubKey())return ConfigFailure(section);
        do
        {
            int n=gCores[i].pointCount;
            if(n>=MAX_POINTS)return ConfigFailure("too many human points");
            gCfg[C_Map].GetVector("origin",gCorePoints[i][n]);gCfg[C_Map].GetVector("angles",gCorePointAngles[i][n]);
            gCores[i].pointCount++;
        }while(gCfg[C_Map].GotoNextKey());
    }
    if(!Node(C_Map,"ResourceZones") || !gCfg[C_Map].GotoFirstSubKey())return ConfigFailure("ResourceZones");
    do
    {
        char pool[64];gCfg[C_Map].GetString("pool_id",pool,sizeof(pool));
        if(!gCfg[C_Map].JumpToKey("Points") || !gCfg[C_Map].GotoFirstSubKey())return ConfigFailure("resource Points");
        do
        {
            if(gNodeCount>=MAX_NODES)return ConfigFailure("too many resource nodes");
            ResourceNode resource;resource.used=true;resource.ref=INVALID_ENT_REFERENCE;
            gCfg[C_Map].GetSectionName(resource.id,sizeof(resource.id));strcopy(resource.pool,sizeof(resource.pool),pool);
            gCfg[C_Map].GetVector("origin",resource.origin);gCfg[C_Map].GetVector("angles",resource.angles);
            gNodes[gNodeCount++]=resource;
        }while(gCfg[C_Map].GotoNextKey());
        gCfg[C_Map].GoBack();gCfg[C_Map].GoBack();
    }while(gCfg[C_Map].GotoNextKey());
    if(!Node(C_Map,"Bosses") || !gCfg[C_Map].GotoFirstSubKey())return ConfigFailure("Bosses");
    do
    {
        if(gBossCount>=MAX_BOSSES)return ConfigFailure("too many bosses");
        BossDef boss;char profile[64];
        gCfg[C_Map].GetSectionName(boss.id,sizeof(boss.id));gCfg[C_Map].GetString("boss_profile",profile,sizeof(profile));
        gCfg[C_Map].GetString("marker_targetname",boss.marker,sizeof(boss.marker));
        if(!boss.marker[0])gCfg[C_Map].GetString("marker",boss.marker,sizeof(boss.marker));
        boss.maxHP=0;strcopy(boss.key,sizeof(boss.key),profile);gBosses[gBossCount++]=boss;
    }while(gCfg[C_Map].GotoNextKey());
    char required[512];Text(C_Map,"","required_boss_ids",required,sizeof(required));
    for(int i=0;i<gBossCount;i++)
    {
        char section[128];Format(section,sizeof(section),"BossTypes/%s",gBosses[i].key);
        Text(C_Bosses,section,"han_config_key",gBosses[i].key,sizeof(gBosses[].key));
        Text(C_Bosses,section,"public_name",gBosses[i].publicName,sizeof(gBosses[].publicName));
        gBosses[i].maxHP=Number(C_Bosses,section,"max_hp",50000);
        gBosses[i].savedHP=gBosses[i].maxHP;
        gBosses[i].damage=Decimal(C_Bosses,section,"building_damage",120.0);
        gBosses[i].interval=Decimal(C_Bosses,section,"building_attack_interval",1.0);
        gBosses[i].required=TokenPresent(required,gBosses[i].id);
        strcopy(gBosses[i].idle,sizeof(gBosses[].idle),"idle");strcopy(gBosses[i].attack,sizeof(gBosses[].attack),"attack1");
        if(!gBosses[i].publicName[0] || StrEqual(gBosses[i].publicName,"TO_MATCH_ACTUAL_NAME") || gBosses[i].maxHP<1)return ConfigFailure("boss public name/HP");
        int marker=-1,matched=0;
        while((marker=FindEntityByClassname(marker,"info_target"))!=-1)
        {
            char name[64];GetEntPropString(marker,Prop_Data,"m_iName",name,sizeof(name));
            if(!StrEqual(name,gBosses[i].marker))continue;
            matched++;GetEntPropVector(marker,Prop_Data,"m_vecOrigin",gBosses[i].origin);GetEntPropVector(marker,Prop_Data,"m_angRotation",gBosses[i].angles);
        }
        if(matched!=1)return ConfigFailure("each boss needs one unique compiled marker");
    }
    if(!gCoreCount || !gBossCount)return ConfigFailure("cores/bosses empty");
    return true;
}

bool TokenPresent(const char[] tokens,const char[] word)
{
    char values[16][64];int count=ExplodeString(tokens," ",values,16,64);
    for(int i=0;i<count;i++)if(StrEqual(values[i],word))return true;
    return false;
}

bool ReadRecipes()
{
    gRecipeCount=0;
    ConfigFile files[]={C_Turrets,C_Flames,C_Fences,C_Mines};
    BuildingKind kinds[]={Kind_Turret,Kind_Flame,Kind_Fence,Kind_Mine};
    for(int file=0;file<4;file++)
    {
        KeyValues kv=gCfg[files[file]];kv.Rewind();
        if(!kv.JumpToKey("BuildRecipes") || !kv.GotoFirstSubKey())return ConfigFailure("BuildRecipes");
        do
        {
            if(gRecipeCount>=MAX_RECIPES)return ConfigFailure("too many recipes");
            Recipe recipe;recipe.file=files[file];recipe.kind=kinds[file];
            kv.GetSectionName(recipe.id,sizeof(recipe.id));kv.GetString("output_type",recipe.type,sizeof(recipe.type));
            kv.GetString("display_name",recipe.label,sizeof(recipe.label),recipe.type);
            if(!kv.JumpToKey("cost"))return ConfigFailure("recipe cost");
            for(int m=0;m<MATERIALS;m++)
            {
                recipe.cost[m]=kv.GetNum(gMaterials[m].id,0);
                if(recipe.cost[m]<0 || recipe.cost[m]>1000000)return ConfigFailure("recipe cost range");
            }
            kv.GoBack();gRecipes[gRecipeCount++]=recipe;
        }while(kv.GotoNextKey());
    }
    for(int i=0;i<gRecipeCount;i++)
    {
        char section[128],model[PLATFORM_MAX_PATH];Format(section,sizeof(section),"Types/%s",gRecipes[i].type);
        Text(gRecipes[i].file,section,"model",model,sizeof(model));
        if(!model[0] || PrecacheModel(model,true)<=0)return ConfigFailure("recipe output model");
        Format(section,sizeof(section),"Types/%s/Levels/1",gRecipes[i].type);
        if(Number(gRecipes[i].file,section,"max_hp",0)<1)return ConfigFailure("recipe level/HP");
    }
    return true;
}

bool ValidateDayRanges()
{
    if(!Node(C_Days,"Stages") || !gCfg[C_Days].GotoFirstSubKey())return ConfigFailure("day stages");
    int expected=1;bool unlimited=false;
    do
    {
        int low=gCfg[C_Days].GetNum("from_day"),high=gCfg[C_Days].GetNum("to_day"),target=gCfg[C_Days].GetNum("night_target_alive");
        if(unlimited || low!=expected || target<1 || target>gPopulationLimit || (high!=0 && high<low))return ConfigFailure("day range/target");
        if(gCfg[C_Days].GetFloat("raider_hp_multiplier",1.0)<=0.0 || gCfg[C_Days].GetFloat("spawn_interval_seconds",1.0)<0.25)return ConfigFailure("day growth/timing");
        unlimited=high==0;expected=high+1;
    }while(gCfg[C_Days].GotoNextKey());
    return unlimited || ConfigFailure("last day range must be unbounded");
}

bool ValidateNativeBridge()
{
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieScenario/%s/HanZombieScenarioStage.cfg",gMap);
    KeyValues kv=new KeyValues("Stage");
    if(!kv.ImportFromFile(path) || !kv.GotoFirstSubKey()){delete kv;return ConfigFailure("install the supplied per-map native Stage bridge");}
    if(kv.GetNum("count")!=99999 || kv.GetNum("healthboost")!=0){delete kv;return ConfigFailure("native bridge count99999/healthboost0 required");}
    char ordinary[512],custom[512];kv.GetString("zombie",ordinary,sizeof(ordinary));kv.GetString("customzombies",custom,sizeof(custom));
    if(!ordinary[0] || kv.GetNum("maxszombiecount")!=0){delete kv;return ConfigFailure("ordinary pool / special count0 required");}
    ReplaceString(custom,sizeof(custom),","," ");
    for(int i=0;i<gBossCount;i++)
    {
        if(StrContains(ordinary,gBosses[i].key)!=-1){delete kv;return ConfigFailure("fixed boss cannot appear in ordinary reborn pool");}
        char token[96];Format(token,sizeof(token),"%s:1",gBosses[i].key);
        if(!TokenPresent(custom,token) && !StrEqual(custom,token)){delete kv;return ConfigFailure("each fixed boss needs customzombies key:1");}
    }
    bool extra=kv.GotoNextKey();delete kv;
    if(extra)return ConfigFailure("native bridge must contain one Stage");
    return true;
}

bool SelectDay()
{
    if(!Node(C_Days,"Stages") || !gCfg[C_Days].GotoFirstSubKey())return false;
    do
    {
        int low=gCfg[C_Days].GetNum("from_day"),high=gCfg[C_Days].GetNum("to_day");
        if(gDay>=low && (high==0 || gDay<=high))return true;
    }while(gCfg[C_Days].GotoNextKey());
    return false;
}

int NightTarget()
{
    if(!SelectDay())return 0;
    int target=gCfg[C_Days].GetNum("night_target_alive");
    target+=(gDay-gCfg[C_Days].GetNum("from_day"))*gCfg[C_Days].GetNum("extra_alive_per_day_after_from",0);
    int cap=gCfg[C_Days].GetNum("night_target_cap",gPopulationLimit);
    if(target>cap)target=cap;if(target>gPopulationLimit)target=gPopulationLimit;
    return target;
}

float HPScale()
{
    if(!SelectDay())return 1.0;
    float scale=gCfg[C_Days].GetFloat("raider_hp_multiplier",1.0)
        +float(gDay-gCfg[C_Days].GetNum("from_day"))*gCfg[C_Days].GetFloat("extra_hp_multiplier_per_day",0.0);
    float cap=gCfg[C_Days].GetFloat("hp_multiplier_cap",10.0);return scale>cap ? cap : scale;
}
