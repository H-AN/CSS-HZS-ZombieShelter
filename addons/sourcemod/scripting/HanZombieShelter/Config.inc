static const char sFiles[][]={"mode.cfg","lifecycle.cfg","resources.cfg","zombies.cfg","days.cfg","bosses.cfg","bases.cfg","turrets.cfg","flame_turrets.cfg","fences.cfg","mines.cfg","drops.cfg","maps/{map}.cfg","sounds.cfg","amplifiers.cfg","progression.cfg","entities.cfg","management.cfg","power.cfg","generators.cfg","lights.cfg","grenade_turrets.cfg","expanders.cfg","materials.cfg","zombie_drops.cfg","repair_stations.cfg","heal_stations.cfg","armor_stations.cfg","beacons.cfg","campfires.cfg","shelters.cfg","tesla_coils.cfg","plasma_turrets.cfg","watchtowers.cfg"};
static const char sRoots[][]={"ShelterMode","ShelterLifecycle","ShelterResources","ShelterZombies","ShelterDays","ShelterBosses","ShelterBases","ShelterTurrets","ShelterFlameTurrets","ShelterFences","ShelterMines","ShelterDrops","ShelterMap","ShelterSounds","ShelterAmplifiers","ShelterProgression","ShelterEntities","ShelterManagement","ShelterPower","ShelterGenerators","ShelterLights","ShelterGrenadeTurrets","ShelterExpanders","ShelterMaterials","ShelterZombieDrops","ShelterRepairStations","ShelterHealStations","ShelterArmorStations","ShelterBeacons","ShelterCampfires","ShelterShelters","ShelterTeslaCoils","ShelterPlasmaTurrets","ShelterWatchtowers"};

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
    LogError("[Shelter] %T","ConfigurationRejected",LANG_SERVER,message);
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
        if(i==view_as<int>(C_Drops))continue; // Retired migration reference; never load or fall back.
        strcopy(relative,sizeof(relative),sFiles[i]);ReplaceString(relative,sizeof(relative),"{map}",gMap);
        BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieShelter/%s",relative);
        gCfg[i]=new KeyValues(sRoots[i]);
        if(!gCfg[i].ImportFromFile(path))
        {
            if(i>=view_as<int>(C_Repair) && !FileExists(path))
            {delete gCfg[i];gCfg[i]=null;continue;} // New optional categories do not disable an older installation.
            if(i==view_as<int>(C_ZombieDrops)){LogMessage("[Shelter] WARNING zombie_drops.cfg missing/invalid: kill rewards disabled");delete gCfg[i];gCfg[i]=null;continue;}
            return ConfigFailure(path);
        }
        char root[64];gCfg[i].GetSectionName(root,sizeof(root));
        int schema=(i==view_as<int>(C_Materials) || i==view_as<int>(C_Resources) || i==view_as<int>(C_ZombieDrops)) ? 2 : 1;
        int actualSchema=gCfg[i].GetNum("schema_version");
        bool buildingSchema=i==view_as<int>(C_Bases) || i==view_as<int>(C_Turrets) || i==view_as<int>(C_Flames)
            || i==view_as<int>(C_Fences) || i==view_as<int>(C_Mines) || i==view_as<int>(C_Amplifiers)
            || i==view_as<int>(C_Generators) || i==view_as<int>(C_Lights) || i==view_as<int>(C_Grenades) || i==view_as<int>(C_Expanders)
            || i>=view_as<int>(C_Repair);
        if(!StrEqual(root,sRoots[i]) || (actualSchema!=schema && !(buildingSchema && actualSchema==2)))
        {
            if(i==view_as<int>(C_ZombieDrops)){LogMessage("[Shelter] WARNING zombie_drops root/schema mismatch: kill rewards disabled");delete gCfg[i];gCfg[i]=null;continue;}
            return ConfigFailure("root/schema_version mismatch");
        }
    }
    if(!Number(C_Mode,"","enabled_for_configured_maps",1))return false;
    // Missing on an older installation: retain instant placement. A later
    // administrator cvar change overrides this campaign's configured default.
    ConfigureRealConstruction(Number(C_Mode,"Rules","real_construction",0)!=0);
    gDaySeconds=Decimal(C_Mode,"Rules","day_seconds",180.0);
    gNightSeconds=Decimal(C_Mode,"Rules","night_seconds",180.0);
    gWarningSeconds=Decimal(C_Mode,"Rules","warning_seconds_before_night",30.0);
    gExposureEnabled=Number(C_Mode,"Rules","night_exposure_enabled",1)!=0;
    gExposureDamage=Number(C_Mode,"Rules","night_exposure_damage",1);
    gExposureInterval=Decimal(C_Mode,"Rules","night_exposure_interval_seconds",1.0);
    gUseDistance=Decimal(C_Mode,"Interactions","maximum_distance",128.0);
    gUseCooldown=Decimal(C_Mode,"Interactions","cooldown_seconds",0.3);
    gGatherCooldown=Decimal(C_Resources,"Gathering","cooldown_seconds",0.5);
    gRepairCooldown=Decimal(C_Bases,"Profiles/shelter_basic","repair_interval_seconds",0.5);
    gInventoryCap=Number(C_Resources,"Inventory","capacity",0);
    gPopulationLimit=Number(C_Zombies,"Population","raider_alive_limit",100);
    gNativePoolSize=Number(C_Zombies,"NativePool","initial_ordinary_count",100);
    if(!Node(C_Zombies,"NativePool"))return ConfigFailure("NativePool required; generation APIs are not used in this version");
    gCfg[C_Zombies].GetVector("parking_origin",gParking);
    if(gDaySeconds<1.0 || gNightSeconds<1.0 || gExposureInterval<0.1 || gExposureDamage<0
        || gUseDistance<16.0 || gUseDistance>512.0 || gUseCooldown<0.1 || gGatherCooldown<0.1
        || gPopulationLimit<1 || gPopulationLimit>(MAX_NPCS-MAX_BOSSES)/2 || gNativePoolSize<gPopulationLimit
        || gNativePoolSize>(MAX_NPCS-MAX_BOSSES)/2
        || gInventoryCap<0)return ConfigFailure("invalid time/range/entity limits");
    for(int axis=0;axis<3;axis++)if(FloatAbs(gParking[axis])>15000.0)return ConfigFailure("parking must remain inside engine +/-15000 bounds");
    if(FloatAbs(gParking[0])<8000.0 && FloatAbs(gParking[1])<8000.0)return ConfigFailure("parking point must be outside playable area");
    gDestroySummons=Number(C_Lifecycle,"Campaign","destroy_summons_on_failure",0)!=0;
    char policy[64];Text(C_Lifecycle,"Campaign","failure_policy",policy,sizeof(policy),"rollback_one_day");
    if(!StrEqual(policy,"rollback_one_day") && !StrEqual(policy,"lose_and_change_map"))return ConfigFailure("failure_policy");
    gDirectLoss=StrEqual(policy,"lose_and_change_map");
    gRollbackDays=Number(C_Lifecycle,"Campaign","rollback_days",1);
    gMinimumDay=Number(C_Lifecycle,"Campaign","minimum_day",1);
    char survivalLimit[64];Text(C_Lifecycle,"Campaign","survival_day_limit",survivalLimit,sizeof(survivalLimit),"30");
    if(strlen(survivalLimit)>=sizeof(survivalLimit)-1 || !ParseSurvivalDayLimit(survivalLimit,gSurvivalDayLimit))
        return ConfigFailure("lifecycle.cfg/Campaign/survival_day_limit: expected integer 0..2147483647");
    gRecoveryFraction=Decimal(C_Lifecycle,"BaseRecovery","upgraded_hp_fraction",0.5);
    gOutcomeSeconds=Decimal(C_Lifecycle,"Outcome","display_seconds",8.0);
    if(gRecoveryFraction<=0.0 || gRecoveryFraction>=1.0 || gOutcomeSeconds<1.0 || gRollbackDays<0 || gMinimumDay<1)return ConfigFailure("failure/recovery ranges");
    Text(C_Lifecycle,"Outcome","victory_phrase",gVictoryText,sizeof(gVictoryText),"CampaignVictory");
    Text(C_Lifecycle,"Outcome","loss_phrase",gFailureText,sizeof(gFailureText),"CampaignLoss");
    Text(C_Mode,"HumanTeamPolicy","t_client_spawn_policy",gSpawnPolicy,sizeof(gSpawnPolicy),"native_position");
    if(!StrEqual(gSpawnPolicy,"native_position") && !StrEqual(gSpawnPolicy,"alive_base") && !StrEqual(gSpawnPolicy,"random_ct_spawn"))return ConfigFailure("t_client_spawn_policy");
    bool valid=ReadMaterials() && ReadMapData() && LoadBuildAreaSettings()
        && (ValidateResourcePools() || ConfigFailure("resource pool weights, regrow option or map pool reference"))
        && ValidateLevelProfiles() && ReadRecipes() && LoadProgression()
        && LoadManagementSettings() && LoadEntitySettings() && LoadPowerSettings() && LoadSupportSettings()
        && LoadBeaconSettings() && LoadCampfireSettings() && LoadShelterSettings()
        && LoadExpansionSettings() && LoadWatchtowerSettings()
        && ValidateDayRanges() && ValidateNativeBridge() && LoadRepairWeapons();
    if(!valid)return false;
    ReadZombieDrops();
    gConfigRevision=gConfigRevision==2147483647 ? 1 : gConfigRevision+1;
    return true;
}

bool ValidResourceGrowth(float hold,float grow,float scale)
{
    // Ordered comparisons alone do not reject NaN; non-finite timing or scale
    // must never reach transparency calculations, maturity checks or the model.
    if((view_as<int>(hold)&0x7f800000)==0x7f800000
        || (view_as<int>(grow)&0x7f800000)==0x7f800000
        || (view_as<int>(scale)&0x7f800000)==0x7f800000)return false;
    float total=hold+grow;
    return (view_as<int>(total)&0x7f800000)!=0x7f800000
        && hold>=0.0 && grow>=0.25 && scale>0.0 && scale<=4.0;
}

bool IsBaseSupportKind(BuildingKind kind)
{
    return kind==Kind_AutoRepair || kind==Kind_Heal || kind==Kind_Armor || kind==Kind_Beacon;
}

bool LoadSupportSettings()
{
    ConfigFile files[]={C_Repair,C_Heal,C_Armor,C_Beacons};
    for(int f=0;f<sizeof(files);f++)
    {
        if(gCfg[files[f]]==null)continue;
        int limit;bool found;
        if(!StoredLevelInt(files[f],"Limits","max_per_base",files[f]==C_Beacons ? 1 : 2,limit,found)
            || limit<1 || limit>MAX_BUILDINGS)return ConfigFailure("support group max_per_base must be 1..1024");
    }
    return true;
}

int SupportBuildLimit(ConfigFile file,const char[] type,int level)
{
    if(file==C_Beacons)return 1; // One logical beacon per base, across every variant.
    if(file!=C_Repair && file!=C_Heal && file!=C_Armor)return 0;
    int group=Number(file,"Limits","max_per_base",2),limit;char profile[128];Format(profile,sizeof(profile),"Types/%s",type);
    if(!ReadLevelInt(file,profile,level,"max_per_base",group,limit))return 0;
    return limit<group ? limit : group;
}

bool ReadMaterials()
{
    static const char ids[][]={"wood","iron","copper","silver","gold"};
    gMaterialCount=0;
    // Stable legacy positions are independent of administrator section ordering.
    for(int i=0;i<LEGACY_MATERIALS;i++)
    {
        char path[96];Format(path,sizeof(path),"Materials/%s",ids[i]);
        if(!Node(C_Materials,path) || !RegisterMaterial(ids[i]))return ConfigFailure(path);
    }
    if(!Node(C_Materials,"Materials") || !gCfg[C_Materials].GotoFirstSubKey())return ConfigFailure("Materials empty");
    do
    {
        char id[64];gCfg[C_Materials].GetSectionName(id,sizeof(id));
        if(FindMaterial(id)>=0)continue;
        if(!RegisterMaterial(id))return ConfigFailure("material ID/kind/capacity (maximum 64)");
    }while(gCfg[C_Materials].GotoNextKey());
    if(!Node(C_Resources,"Gatherables") || !gCfg[C_Resources].GotoFirstSubKey())return ConfigFailure("Gatherables required");
    // Enumerate only explicit gatherable definitions: a core/currency needs no model.
    ArrayList gatherables=new ArrayList(ByteCountToCells(64));
    do{char id[64];gCfg[C_Resources].GetSectionName(id,sizeof(id));gatherables.PushString(id);}while(gCfg[C_Resources].GotoNextKey());
    for(int entry=0;entry<gatherables.Length;entry++)
    {
        char id[64],path[128];gatherables.GetString(entry,id,sizeof(id));int i=FindMaterial(id);
        if(i<0){delete gatherables;return ConfigFailure("unregistered Gatherables ID");}
        Format(path,sizeof(path),"Gatherables/%s",id);Node(C_Resources,path);
        gCfg[C_Resources].GetString("model",gMaterials[i].model,sizeof(gMaterials[].model));
        gMaterials[i].quota=gCfg[C_Resources].GetNum("hits_per_cycle");
        gMaterials[i].yield=gCfg[C_Resources].GetNum("yield_per_successful_hit");
        gMaterials[i].grow=gCfg[C_Resources].GetFloat("grow_seconds");
        gMaterials[i].hold=gCfg[C_Resources].GetFloat("hold_seconds");
        gMaterials[i].scale=gCfg[C_Resources].GetFloat("model_scale",1.0);
        char rgb[64],parts[3][16];gCfg[C_Resources].GetString("render_rgb",rgb,sizeof(rgb),"255 255 255");
        if(ExplodeString(rgb," ",parts,3,16)!=3){delete gatherables;return ConfigFailure("material render_rgb");}
        for(int axis=0;axis<3;axis++)gMaterials[i].rgb[axis]=StringToInt(parts[axis]);
        char curve[32];gCfg[C_Resources].GetString("render_alpha_curve",curve,sizeof(curve),"linear");
        gMaterials[i].ease=StrEqual(curve,"ease_in");
        if(gMaterials[i].quota<1 || gMaterials[i].yield<1
           || !ValidResourceGrowth(gMaterials[i].hold,gMaterials[i].grow,gMaterials[i].scale)
           || !gMaterials[i].model[0] || PrecacheModel(gMaterials[i].model,true)<=0)
        {delete gatherables;return ConfigFailure("material model/quota/growth");}
        gMaterials[i].gatherable=true;
    }
    delete gatherables;
    return true;
}

bool RegisterMaterial(const char[] id)
{
    if(!id[0] || strlen(id)>=sizeof(gMaterials[].id) || gMaterialCount>=MATERIALS)return false;
    for(int c=0;id[c];c++)if(!((id[c]>='a' && id[c]<='z') || (id[c]>='0' && id[c]<='9') || id[c]=='_'))return false;
    MaterialDef material;strcopy(material.id,sizeof(material.id),id);
    gCfg[C_Materials].GetString("display_name",material.label,sizeof(material.label),id);
    if(!material.label[0])strcopy(material.label,sizeof(material.label),id);
    gCfg[C_Materials].GetString("description",material.description,sizeof(material.description));
    char kind[32];gCfg[C_Materials].GetString("kind",kind,sizeof(kind),"material");
    if(!StrEqual(kind,"material") && !StrEqual(kind,"currency"))return false;
    material.currency=StrEqual(kind,"currency");gMaterials[gMaterialCount++]=material;return true;
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

bool ReadMetadataVector(ConfigFile file,const char[] path,const char[] key,float vector[3])
{
    if(!Node(file,path) || gCfg[file].GetDataType(key)==KvData_None)return true;
    char text[160],values[4][40];gCfg[file].GetString(key,text,sizeof(text));TrimString(text);
    ReplaceString(text,sizeof(text),"\t"," ");
    while(StrContains(text,"  ")>=0)ReplaceString(text,sizeof(text),"  "," ");
    if(ExplodeString(text," ",values,4,40)!=3)return false;
    for(int axis=0;axis<3;axis++)
        if(!values[axis][0] || StringToFloatEx(values[axis],vector[axis])!=strlen(values[axis])
            || !(vector[axis]>=-8192.0 && vector[axis]<=8192.0))return false;
    return true;
}

void RegisterBuildingSound(char[] sample,int length)
{
    if(!sample[0])return;
    if(StrContains(sample,"sound/")==0)strcopy(sample,length,sample[6]);
    char path[PLATFORM_MAX_PATH];Format(path,sizeof(path),"sound/%s",sample);
    if(StrContains(sample,"..")>=0 || !FileExists(path,true) || !PrecacheSound(sample,true))
    {LogError("[Shelter] Missing building sound %s; using category default",path);sample[0]=0;return;}
    AddFileToDownloadsTable(path);
}

int BuildingModelSkinCount(const char[] model)
{
    // Read only a validated Source studio header through the mounted GAME FS,
    // including VPK/BSP resources. Unknown header formats retain the safe cap.
    File asset=OpenFile(model,"rb",true,"GAME");if(asset==null)return 0;
    int signature,version,count;
    bool valid=asset.ReadInt32(signature) && asset.ReadInt32(version) && signature==0x54534449
        && version>=44 && version<=49 && asset.Seek(224,SEEK_SET) && asset.ReadInt32(count);
    delete asset;return valid && count>0 && count<=256 ? count : 0;
}

bool ReadBuildingMetadata(ConfigFile file,const char[] profile,BuildingKind kind,BuildingMetadata meta)
{
    if(!Node(file,profile) || !gCfg[file].GetNum("enabled",1))return false;
    Text(file,profile,"display_name",meta.label,sizeof(meta.label));
    Text(file,profile,"description",meta.description,sizeof(meta.description));
    char text[128],values[4][24];Text(file,profile,"render_rgb",text,sizeof(text),"255 255 255");
    if(ExplodeString(text," ",values,4,24)!=3)return false;
    for(int axis=0;axis<3;axis++)
    {
        int color;if(!ParseLevelInteger(values[axis],color,true))return false;
        meta.render[axis]=color<0 ? 0 : (color>255 ? 255 : color);
    }
    int alpha=Number(file,profile,"render_alpha",255);meta.render[3]=alpha<0 ? 0 : (alpha>255 ? 255 : alpha);
    meta.skin=Number(file,profile,"skin",0);
    Text(file,profile,"idle_animation",meta.idleAnimation,sizeof(meta.idleAnimation));
    bool scaleFound;
    if(!StoredLevelFloat(file,profile,"model_scale",1.0,meta.modelScale,scaleFound)
        || meta.modelScale<0.01 || meta.modelScale>16.0)return false;
    bool bodyFound;
    if(!StoredLevelInt(file,profile,"body",0,meta.body,bodyFound) || meta.body<0 || meta.body>255)return false;
    if(meta.skin<0 || meta.skin>255)
    {LogMessage("[Shelter] %s skin %d clamped to safe range 0..255; verify the model's actual skin count",profile,meta.skin);meta.skin=meta.skin<0 ? 0 : 255;}
    char model[PLATFORM_MAX_PATH];Text(file,profile,"model",model,sizeof(model));int skins=BuildingModelSkinCount(model);
    if(skins>0 && meta.skin>=skins)
    {LogMessage("[Shelter] %s skin %d exceeds model skin range 0..%d; clamped",profile,meta.skin,skins-1);meta.skin=skins-1;}
    char path[192];Format(path,sizeof(path),"%s/Placement",profile);
    Text(file,path,"mode",meta.placementMode,sizeof(meta.placementMode),"upright");
    if(!StrEqual(meta.placementMode,"upright") && !StrEqual(meta.placementMode,"surface") && !StrEqual(meta.placementMode,"wall"))return false;
    meta.allowOverlap=Number(file,path,"allow_overlap",1)!=0;
    meta.maxDistance=Decimal(file,path,"max_distance",512.0);
    bool groundFound;
    if(!StoredLevelFloat(file,path,"model_ground_z",0.0,meta.modelGroundZ,groundFound)
        || meta.modelGroundZ < -15000.0 || meta.modelGroundZ > 15000.0)return false;
    if(!StoredLevelFloat(file,path,"model_ground_curve_z",0.0,meta.modelGroundCurveZ,groundFound)
        || meta.modelGroundCurveZ < -15000.0 || meta.modelGroundCurveZ > 15000.0)return false;
    if(!(meta.maxDistance>=16.0 && meta.maxDistance<=4096.0)
        || !ReadMetadataVector(file,path,"origin_offset",meta.originOffset)
        || !ReadMetadataVector(file,path,"angle_offset",meta.angleOffset))return false;
    if(kind==Kind_Turret || kind==Kind_Flame)meta.muzzleOffset[2]=48.0;
    if(kind==Kind_Grenade)meta.muzzleOffset[2]=64.0;
    meta.lightOffset[2]=64.0;
    Format(path,sizeof(path),"%s/Attachments",profile);
    if(!ReadMetadataVector(file,path,"muzzle_offset",meta.muzzleOffset)
        || !ReadMetadataVector(file,path,"light_offset",meta.lightOffset))return false;
    Format(path,sizeof(path),"%s/Sounds",profile);
    Text(file,path,"build",meta.buildSound,sizeof(meta.buildSound));RegisterBuildingSound(meta.buildSound,sizeof(meta.buildSound));
    Text(file,path,"attack",meta.attackSound,sizeof(meta.attackSound));RegisterBuildingSound(meta.attackSound,sizeof(meta.attackSound));
    Text(file,path,"upgrade",meta.upgradeSound,sizeof(meta.upgradeSound));RegisterBuildingSound(meta.upgradeSound,sizeof(meta.upgradeSound));
    Text(file,path,"destroy",meta.destroySound,sizeof(meta.destroySound));RegisterBuildingSound(meta.destroySound,sizeof(meta.destroySound));
    Text(file,path,"pulse",meta.pulseSound,sizeof(meta.pulseSound));RegisterBuildingSound(meta.pulseSound,sizeof(meta.pulseSound));
    Format(path,sizeof(path),"%s/Downloads",profile);
    if(Node(file,path) && gCfg[file].GotoFirstSubKey(false))
    {
        do
        {
            char asset[PLATFORM_MAX_PATH];gCfg[file].GetString(NULL_STRING,asset,sizeof(asset));
            if(!asset[0] || StrContains(asset,"..")>=0 || !FileExists(asset,true))
            {LogError("[Shelter] Missing/invalid building download %s (%s)",asset,profile);continue;}
            AddFileToDownloadsTable(asset);
        }while(gCfg[file].GotoNextKey(false));
    }
    return true;
}

bool ReadTypeMetadata(ConfigFile file,const char[] type,BuildingMetadata meta)
{
    BuildingKind kind=Kind_Fence;
    if(file==C_Turrets)kind=Kind_Turret;else if(file==C_Flames)kind=Kind_Flame;else if(file==C_Grenades)kind=Kind_Grenade;
    else if(file==C_Tesla)kind=Kind_Tesla;else if(file==C_Plasma)kind=Kind_Plasma;else if(file==C_Watchtowers)kind=Kind_Watchtower;
    else if(file==C_Campfires)kind=Kind_Campfire;else if(file==C_Shelters)kind=Kind_Base;
    char path[128];Format(path,sizeof(path),"Types/%s",type);return ReadBuildingMetadata(file,path,kind,meta) && ValidateMortarType(file,type);
}

bool ReadRecipes()
{
    gRecipeCount=0;
    ConfigFile files[]={C_Turrets,C_Flames,C_Fences,C_Mines,C_Amplifiers,C_Generators,C_Lights,C_Grenades,C_Expanders,C_Repair,C_Heal,C_Armor,C_Beacons,C_Campfires,C_Shelters,C_Tesla,C_Plasma,C_Watchtowers};
    BuildingKind kinds[]={Kind_Turret,Kind_Flame,Kind_Fence,Kind_Mine,Kind_Amplifier,Kind_Generator,Kind_Light,Kind_Grenade,Kind_Expander,
        Kind_AutoRepair,Kind_Heal,Kind_Armor,Kind_Beacon,Kind_Campfire,Kind_Base,Kind_Tesla,Kind_Plasma,Kind_Watchtower};
    for(int file=0;file<sizeof(files);file++)
    {
        ConfigFile source=files[file];ArrayList ids=new ArrayList(ByteCountToCells(64));
        if(Node(source,"BuildRecipes") && gCfg[source].GotoFirstSubKey())
        {
            do{char id[64];gCfg[source].GetSectionName(id,sizeof(id));ids.PushString(id);}while(gCfg[source].GotoNextKey());
        }
        for(int entry=0;entry<ids.Length;entry++)
        {
            Recipe recipe;recipe.file=source;recipe.kind=kinds[file];ids.GetString(entry,recipe.id,sizeof(recipe.id));
            char path[192],profile[128],model[PLATFORM_MAX_PATH];Format(path,sizeof(path),"BuildRecipes/%s",recipe.id);
            if(!Number(source,path,"enabled",1))continue;
            Text(source,path,"output_type",recipe.type,sizeof(recipe.type));Text(source,path,"display_name",recipe.label,sizeof(recipe.label));
            Format(profile,sizeof(profile),"Types/%s",recipe.type);
            bool valid=recipe.id[0] && recipe.type[0] && StrContains(recipe.id,"/")<0 && StrContains(recipe.type,"/")<0
                && StrContains(recipe.id,":")<0 && StrContains(recipe.type,":")<0
                && ValidateLevelProfile(source,profile) && ReadTypeMetadata(source,recipe.type,recipe.meta);
            Text(source,profile,"model",model,sizeof(model));
            valid=valid && model[0] && PrecacheModel(model,true)>0;
            if(!recipe.label[0])strcopy(recipe.label,sizeof(recipe.label),recipe.meta.label[0] ? recipe.meta.label : recipe.type);
            strcopy(recipe.meta.label,sizeof(recipe.meta.label),recipe.label);
            Format(path,sizeof(path),"BuildRecipes/%s/cost",recipe.id);
            valid=valid && Node(source,path) && ValidateMaterialCostKeys(source,path);
            for(int m=0;m<gMaterialCount && valid;m++)
            {
                bool found;
                valid=StoredLevelInt(source,path,gMaterials[m].id,0,recipe.cost[m],found)
                    && recipe.cost[m]>=0 && recipe.cost[m]<=1000000;
            }
            for(int r=0;r<gRecipeCount && valid;r++)if(gRecipes[r].file==source && StrEqual(gRecipes[r].id,recipe.id))valid=false;
            if(!valid){LogError("[Shelter] Disabled recipe %s in %s: invalid type/model/metadata/material cost",recipe.id,sFiles[source]);continue;}
            if(gRecipeCount>=MAX_RECIPES){LogError("[Shelter] Recipe catalogue capacity %d exceeded; skipped %s",MAX_RECIPES,recipe.id);continue;}
            gRecipes[gRecipeCount++]=recipe;
        }
        delete ids;
    }
    return gRecipeCount>0 || ConfigFailure("no valid building recipes");
}

bool ValidateDayRanges()
{
    if(!Node(C_Days,"Stages") || !gCfg[C_Days].GotoFirstSubKey())return ConfigFailure("day stages");
    int expected=1;bool unlimited=false;
    do
    {
        int low=gCfg[C_Days].GetNum("from_day"),high=gCfg[C_Days].GetNum("to_day"),target=gCfg[C_Days].GetNum("night_target_alive");
        int dayTarget=gCfg[C_Days].GetNum("day_target_alive",8);
        if(unlimited || low!=expected || target<1 || target>gPopulationLimit || dayTarget<0 || dayTarget>target || (high!=0 && high<low))return ConfigFailure("day range/target");
        int extra=gCfg[C_Days].GetNum("extra_alive_per_day_after_from",0),cap=gCfg[C_Days].GetNum("night_target_cap",gPopulationLimit);
        float hp=gCfg[C_Days].GetFloat("raider_hp_multiplier",1.0),hpExtra=gCfg[C_Days].GetFloat("extra_hp_multiplier_per_day",0.0);
        float hpCap=gCfg[C_Days].GetFloat("hp_multiplier_cap",10.0);
        if(extra<0 || extra>gPopulationLimit || cap<target || cap>gPopulationLimit
            || !(hp>0.0 && hp<=1000.0) || !(hpExtra>=0.0 && hpExtra<=1000.0) || !(hpCap>=hp && hpCap<=1000.0))return ConfigFailure("day growth/caps");
        float playerBase,playerExtra,playerCap;
        if(!ReadDayPlayerDamageField("raider_player_damage_multiplier",1.0,playerBase)
            || !ReadDayPlayerDamageField("extra_player_damage_multiplier_per_day",0.0,playerExtra)
            || !ReadDayPlayerDamageField("player_damage_multiplier_cap",1.0,playerCap))return false;
        if(!(playerBase>0.0))return DayPlayerDamageFailure("raider_player_damage_multiplier","must be positive");
        if(!(playerExtra>=0.0))return DayPlayerDamageFailure("extra_player_damage_multiplier_per_day","must be nonnegative");
        if(!(playerCap>=playerBase))return DayPlayerDamageFailure("player_damage_multiplier_cap","must be at least raider_player_damage_multiplier");
        if(gCfg[C_Days].GetFloat("raider_hp_multiplier",1.0)<=0.0 || gCfg[C_Days].GetFloat("spawn_interval_seconds",1.0)<0.25
            || gCfg[C_Days].GetFloat("day_spawn_interval_seconds",2.0)<0.25 || gCfg[C_Days].GetNum("spawn_batch",2)<1
            || gCfg[C_Days].GetNum("day_spawn_batch",2)<1)return ConfigFailure("day growth/timing");
        if(!ValidateStageCvars(gCfg[C_Days]))return ConfigFailure("stage Cvars: unknown option, unsafe value or count exceeds native pool");
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
    int elapsed=gDay-gCfg[C_Days].GetNum("from_day"),extra=gCfg[C_Days].GetNum("extra_alive_per_day_after_from",0);
    int cap=gCfg[C_Days].GetNum("night_target_cap",gPopulationLimit);
    int stageCap=StagePopulationCap();if(cap>stageCap)cap=stageCap;
    if(target>cap)target=cap;
    if(extra>0 && elapsed>0 && target<cap)
    {
        // Saturate BEFORE multiplication; even an INT_MAX day cannot overflow.
        int remaining=cap-target,daysToCap=remaining/extra+(remaining%extra!=0 ? 1 : 0);
        target=elapsed>=daysToCap ? cap : target+elapsed*extra;
    }
    return target;
}

int DayTarget()
{
    if(!SelectDay())return 0;
    int target=gCfg[C_Days].GetNum("day_target_alive",8);
    int cap=StagePopulationCap();if(target>cap)target=cap;
    return target;
}

int OrdinaryTarget()
{
    if(gPhase==Phase_Day)return DayTarget();
    if(gPhase==Phase_Night)return NightTarget();
    return 0;
}

float HPScale()
{
    if(!SelectDay())return 1.0;
    float scale=gCfg[C_Days].GetFloat("raider_hp_multiplier",1.0)
        +float(gDay-gCfg[C_Days].GetNum("from_day"))*gCfg[C_Days].GetFloat("extra_hp_multiplier_per_day",0.0);
    float cap=gCfg[C_Days].GetFloat("hp_multiplier_cap",10.0);return scale>cap ? cap : scale;
}

bool ParseSurvivalDayLimit(const char[] token,int &value)
{
    // Do not let GetNum/StringToInt wrap an overflowing positive campaign goal.
    if(!token[0])return false;
    int result;
    for(int i=0;token[i];i++)
    {
        if(token[i]<'0' || token[i]>'9')return false;
        int digit=token[i]-'0';
        if(result>214748364 || (result==214748364 && digit>7))return false;
        result=result*10+digit;
    }
    value=result;return true;
}

bool ReadDayPlayerDamageField(const char[] key,float fallback,float &value)
{
    KeyValues stage=gCfg[C_Days];value=fallback;
    if(stage.GetDataType(key)==KvData_None)return true;
    char token[128];stage.GetString(key,token,sizeof(token));
    if(token[0] && strlen(token)<sizeof(token)-1 && StringToFloatEx(token,value)==strlen(token)
        && (view_as<int>(value)&0x7f800000)!=0x7f800000)return true;
    return DayPlayerDamageFailure(key,"expected finite decimal");
}

bool DayPlayerDamageFailure(const char[] key,const char[] detail)
{
    char name[64],message[256];gCfg[C_Days].GetSectionName(name,sizeof(name));
    Format(message,sizeof(message),"days.cfg/Stages/%s/%s: %s",name,key,detail);
    return ConfigFailure(message);
}

float DayPlayerDamageScale(float base,float extra,float cap,int elapsed)
{
    // Compare before multiplying so an extreme valid day cannot overflow.
    if(extra<=0.0 || elapsed<=0 || base>=cap)return base>cap ? cap : base;
    float remaining=cap-base;
    if(extra>=remaining/float(elapsed))return cap;
    float scale=base+float(elapsed)*extra;return scale>cap ? cap : scale;
}

float PlayerDamageScale()
{
    if(!SelectDay())return 1.0;
    return DayPlayerDamageScale(gCfg[C_Days].GetFloat("raider_player_damage_multiplier",1.0),
        gCfg[C_Days].GetFloat("extra_player_damage_multiplier_per_day",0.0),
        gCfg[C_Days].GetFloat("player_damage_multiplier_cap",1.0),gDay-gCfg[C_Days].GetNum("from_day"));
}
