// Diagnostic-only import into a freshly loaded main plugin. Never ship this
// include in the formal SMX. Public snapshot data is supplemented by the exact
// sm_zsh_skills_status and sm_zsh_status outputs before import.
bool gV082UserRestoreBusy,gV082UserRestoreDone;

enum struct V082UserRestoreCandidate
{
    int day;
    int highest;
    int earned;
    int spent;
    int adjustment;
    int rate;
    int rateOverride;
    int skillLevels[SKILL_COUNT];
    int playerMaterials[MATERIALS];
    int baseMaterials[MAX_CORES*MATERIALS];
    int baseOldUID[MAX_CORES];
    int baseLevel[MAX_CORES];
    int baseHP[MAX_CORES];
    int baseMaxHP[MAX_CORES];
    int bossHP[MAX_BOSSES];
    bool bossDefeated[MAX_BOSSES];
    int generatorOldUID;
    int generatorRecipe;
    int generatorSlot;
    int generatorPowerCore;
    int generatorLevel;
    int generatorHP;
    int generatorMaxHP;
    float generatorActualOutput;
    Building generator;
}
V082UserRestoreCandidate gV082UserRestoreCandidate;

bool V082RestoreInt(KeyValues kv,const char[] name,int minimum,int maximum,int &value)
{
    char text[64];kv.GetString(name,text,sizeof(text));
    return strlen(text)<sizeof(text)-1 && ParseLevelInteger(text,value,true) && value>=minimum && value<=maximum;
}

bool V082RestoreFloat(KeyValues kv,const char[] name,float minimum,float maximum,float &value)
{
    char text[64];kv.GetString(name,text,sizeof(text));int length=strlen(text);
    return length>0 && length<sizeof(text)-1 && StringToFloatEx(text,value)==length
        && (view_as<int>(value)&0x7f800000)!=0x7f800000 && value>=minimum && value<=maximum;
}

bool V082RestoreFlag(KeyValues kv,const char[] name)
{
    int value;return V082RestoreInt(kv,name,1,1,value);
}

bool V082RestoreFail(char[] reason,int size,const char[] message)
{
    strcopy(reason,size,message);return false;
}

bool V082RestoreMaterialRows(KeyValues kv,int amounts[MATERIALS],int maximum,bool player)
{
    if(!kv.JumpToKey("Materials") || !kv.GotoFirstSubKey())return false;
    bool seen[MATERIALS];int count;
    do
    {
        char id[128];kv.GetString("id",id,sizeof(id));int material=FindMaterial(id),amount;
        if(material<0 || material>=gMaterialCount || seen[material]
            || !V082RestoreFlag(kv,"amount_valid") || (player && !V082RestoreFlag(kv,"id_valid"))
            || !V082RestoreInt(kv,"amount",0,maximum,amount))return false;
        seen[material]=true;amounts[material]=amount;count++;
    }while(kv.GotoNextKey());
    kv.GoBack();kv.GoBack();return count==gMaterialCount;
}

int V082RestoreSoleHuman()
{
    int actor,count;
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client)
        && !IsFakeClient(client) && !IsClientSourceTV(client) && !IsClientReplay(client))
    {actor=client;count++;}
    return count==1 ? actor : 0;
}

bool V082RestoreFresh(int client,char[] reason,int size)
{
    if(!gEnabled || !gCoreReady || !gRoundPrepared || gTerminal || gPhase!=Phase_Waiting
        || gDay>1 || gCoreCount!=gFixedCoreCount || gCoreCount<1)
        return V082RestoreFail(reason,size,"requires a fresh prepared main in waiting phase with fixed bases");
    if(!gSkillsConfigReady || !gSkillsEnabled || !SkillsIdentity(client) || gSkillsRows==null
        || gSkillsRows.Length!=1 || gSkillsHighestDay>1 || gSkillsEarned>gSkillsDailyRate)
        return V082RestoreFail(reason,size,"requires a fresh sole-user skills account");
    int account=SkillsAccountIndex(client);
    if(account<0 || gSkillsRows.Get(account,SKILL_SPENT)!=0 || gSkillsRows.Get(account,SKILL_ADMIN)!=0)
        return V082RestoreFail(reason,size,"skill spending/admin adjustment is no longer empty");
    for(int skill=0;skill<SKILL_COUNT;skill++)if(gSkillsRows.Get(account,skill)!=0)
        return V082RestoreFail(reason,size,"learned skills are no longer empty");
    for(int c=1;c<=MaxClients;c++)for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[c][m]!=0)
        return V082RestoreFail(reason,size,"personal material inventory is no longer empty");
    for(int c=0;c<gCoreCount;c++)
    {
        int b=gCores[c].building;
        if(gCores[c].dynamic || b<0 || !gBuildings[b].used || !gBuildings[b].alive
            || gBuildings[b].constructing || gBuildings[b].kind!=Kind_Base || gBuildings[b].file!=C_Bases
            || Resolve(gBuildings[b].ref)<0)return V082RestoreFail(reason,size,"fresh fixed base bodies are unavailable");
        for(int m=0;m<gMaterialCount;m++)if(gBaseInventory[c][m]!=0)
            return V082RestoreFail(reason,size,"base material inventory is no longer empty");
    }
    if(gSharedOverflow!=null && gSharedOverflow.Length>0)return V082RestoreFail(reason,size,"shared material overflow is not empty");
    for(int b=MAX_CORES;b<MAX_BUILDINGS;b++)if(gBuildings[b].used)
        return V082RestoreFail(reason,size,"non-base building slots are no longer empty");
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used)
        return V082RestoreFail(reason,size,"native NPC registration has already begun; import must precede the new round");
    return true;
}

bool V082RestoreReadSkills(KeyValues kv,V082UserRestoreCandidate state)
{
    kv.Rewind();if(!kv.JumpToKey("Skills"))return false;
    if(!V082RestoreInt(kv,"highest_day",state.day,SKILL_CELL_LIMIT,state.highest)
        || !V082RestoreInt(kv,"earned",0,SKILL_CELL_LIMIT,state.earned)
        || !V082RestoreInt(kv,"spent",0,SKILL_CELL_LIMIT,state.spent)
        || !V082RestoreInt(kv,"adjustment",-SKILL_CELL_LIMIT,SKILL_CELL_LIMIT,state.adjustment)
        || !V082RestoreInt(kv,"daily_rate",0,1000000,state.rate)
        || !V082RestoreInt(kv,"rate_override",-1,1000000,state.rateOverride)
        || state.rate!=(state.rateOverride<0 ? gSkillsPointsPerDay : state.rateOverride))return false;
    int budget=state.earned;
    if(state.adjustment>0)budget=state.adjustment>SKILL_CELL_LIMIT-budget ? SKILL_CELL_LIMIT : budget+state.adjustment;
    else budget=state.adjustment < -budget ? 0 : budget+state.adjustment;
    if(state.spent>budget || !kv.JumpToKey("Levels") || !kv.GotoFirstSubKey(false))return false;
    bool seen[SKILL_COUNT];int count,expectedSpent;
    do
    {
        char id[64],raw[64];kv.GetSectionName(id,sizeof(id));int skill=SkillsFindID(id),level;
        if(skill<0 || seen[skill] || kv.GetDataType(NULL_STRING)==KvData_None)return false;
        kv.GetString(NULL_STRING,raw,sizeof(raw));
        if(strlen(raw)>=sizeof(raw)-1 || !ParseLevelInteger(raw,level) || level<0 || level>gSkillMaxLevel[skill]
            || (!gSkillEnabled[skill] && level>0) || gSkillCost[skill]<1
            || level>(SKILL_CELL_LIMIT-expectedSpent)/gSkillCost[skill])return false;
        expectedSpent+=level*gSkillCost[skill];state.skillLevels[skill]=level;seen[skill]=true;count++;
    }while(kv.GotoNextKey(false));
    return count==SKILL_COUNT && expectedSpent==state.spent;
}

bool V082RestoreReadBases(KeyValues kv,V082UserRestoreCandidate state)
{
    kv.Rewind();if(!kv.JumpToKey("Bases") || !kv.GotoFirstSubKey())return false;
    bool seen[MAX_CORES];int count;
    do
    {
        int core,uid,level,hp,maximum;char section[32];kv.GetSectionName(section,sizeof(section));
        int sectionCore;if(!ParseLevelInteger(section,sectionCore)
            || !V082RestoreInt(kv,"core",0,gCoreCount-1,core) || core!=sectionCore || seen[core]
            || !V082RestoreFlag(kv,"api_valid") || !V082RestoreFlag(kv,"api_alive")
            || !V082RestoreInt(kv,"uid",1,2147483646,uid)
            || !V082RestoreInt(kv,"level",1,1000,level)
            || !V082RestoreInt(kv,"max_hp",1,LEVEL_CELL_LIMIT,maximum)
            || !V082RestoreInt(kv,"hp",1,maximum,hp))return false;
        char profile[128];Format(profile,sizeof(profile),"Profiles/%s",gCores[core].profile);
        int resolvedMaximum;
        if(!LevelAvailable(C_Bases,profile,level) || !ValidateResolvedLevel(C_Bases,profile,level)
            || !ReadLevelInt(C_Bases,profile,level,"max_hp",0,resolvedMaximum) || resolvedMaximum!=maximum)return false;
        int materials[MATERIALS];if(!V082RestoreMaterialRows(kv,materials,StorageCapacity(),false))return false;
        for(int m=0;m<gMaterialCount;m++)state.baseMaterials[core*MATERIALS+m]=materials[m];
        state.baseOldUID[core]=uid;state.baseLevel[core]=level;state.baseHP[core]=hp;state.baseMaxHP[core]=maximum;
        seen[core]=true;count++;
    }while(kv.GotoNextKey());
    return count==gCoreCount;
}

bool V082RestoreReadBuildings(KeyValues kv,V082UserRestoreCandidate state)
{
    kv.Rewind();if(!kv.JumpToKey("Buildings") || !kv.GotoFirstSubKey())return false;
    bool bases[MAX_CORES];int count,generators;
    do
    {
        int uid,kind,core,level,hp,maximum;char name[32];kv.GetSectionName(name,sizeof(name));int namedUID;
        if(!ParseLevelInteger(name,namedUID) || !V082RestoreFlag(kv,"api_valid") || !V082RestoreFlag(kv,"api_alive")
            || !V082RestoreInt(kv,"uid",1,2147483646,uid) || uid!=namedUID
            || !V082RestoreInt(kv,"kind",0,100,kind) || !V082RestoreInt(kv,"api_core",0,gCoreCount-1,core)
            || !V082RestoreInt(kv,"level",1,1000,level) || !V082RestoreInt(kv,"max_hp",1,LEVEL_CELL_LIMIT,maximum)
            || !V082RestoreInt(kv,"hp",1,maximum,hp))return false;
        if(kind==view_as<int>(Kind_Base))
        {
            if(bases[core] || uid!=state.baseOldUID[core] || level!=state.baseLevel[core]
                || hp!=state.baseHP[core] || maximum!=state.baseMaxHP[core])return false;
            bases[core]=true;
        }
        else if(kind==view_as<int>(Kind_Generator) && generators==0)
        {
            if(!V082RestoreFloat(kv,"power_output",0.000001,1000000000.0,state.generatorActualOutput))return false;
            state.generatorOldUID=uid;state.generatorLevel=level;state.generatorHP=hp;
            state.generatorMaxHP=maximum;state.generatorPowerCore=core;generators++;
        }
        else return false;
        count++;
    }while(kv.GotoNextKey());
    return generators==1 && count==gCoreCount+1;
}

bool V082RestoreReadGeneratorBody(KeyValues kv,V082UserRestoreCandidate state)
{
    kv.Rewind();if(!kv.JumpToKey("BuildingBodies") || !kv.GotoFirstSubKey())return false;
    int bodies,matching;bool baseBodies[MAX_CORES];
    do
    {
        int uid;if(!V082RestoreInt(kv,"uid",1,2147483646,uid))return false;
        if(uid==state.generatorOldUID)
        {
            if(matching++ || !V082RestoreFlag(kv,"origin_available") || !V082RestoreFlag(kv,"angles_available"))return false;
            char model[PLATFORM_MAX_PATH+1];kv.GetString("model",model,sizeof(model));
            if(!model[0] || strlen(model)>=PLATFORM_MAX_PATH
                || !BossReadPointVector(kv,"origin",state.generator.origin)
                || !BossReadPointVector(kv,"angles",state.generator.angles,false,true))return false;
            int recipe=-1,matches;
            for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Generator && gRecipes[r].file==C_Generators)
            {
                char path[128],configured[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s",gRecipes[r].type);
                Text(C_Generators,path,"model",configured,sizeof(configured));
                if(StrEqual(model,configured)){recipe=r;matches++;}
            }
            if(matches!=1)return false;
            state.generatorRecipe=recipe;state.generator.meta=gRecipes[recipe].meta;
            float scale;int skin,body;
            if(!V082RestoreFlag(kv,"model_scale_available") || !V082RestoreFloat(kv,"model_scale",0.01,16.0,scale)
                || FloatAbs(scale-state.generator.meta.modelScale)>0.0001
                || !V082RestoreFlag(kv,"skin_available") || !V082RestoreInt(kv,"skin",0,2147483646,skin)
                || !V082RestoreFlag(kv,"body_available") || !V082RestoreInt(kv,"body",0,2147483646,body)
                || skin!=state.generator.meta.skin || body!=state.generator.meta.body)return false;
            strcopy(state.generator.model,sizeof(state.generator.model),model);
            strcopy(state.generator.type,sizeof(state.generator.type),gRecipes[recipe].type);
        }
        else
        {
            int core=-1;for(int c=0;c<gCoreCount;c++)if(state.baseOldUID[c]==uid){core=c;break;}
            if(core<0 || baseBodies[core])return false;baseBodies[core]=true;
            float origin[3];if(!V082RestoreFlag(kv,"origin_available") || !BossReadPointVector(kv,"origin",origin)
                || GetVectorDistance(origin,gBuildings[gCores[core].building].origin)>0.5)return false;
        }
        bodies++;
    }while(kv.GotoNextKey());
    if(matching!=1 || bodies!=gCoreCount+1)return false;
    char profile[128];Format(profile,sizeof(profile),"Types/%s",state.generator.type);
    int maximum;float nominalOutput;
    if(!LevelAvailable(C_Generators,profile,state.generatorLevel)
        || !ValidateResolvedLevel(C_Generators,profile,state.generatorLevel)
        || !ReadLevelInt(C_Generators,profile,state.generatorLevel,"max_hp",0,maximum) || maximum!=state.generatorMaxHP
        || !ReadLevelFloat(C_Generators,profile,state.generatorLevel,"power_output",0.0,nominalOutput)
        || FloatAbs(nominalOutput*float(state.generatorHP)/float(maximum)-state.generatorActualOutput)>0.01)return false;
    // Reverse the normal placement offset using the already observed final
    // orientation. Preserve the exact body origin instead of placing it again.
    float offset[3],zero[3],rotated[3];offset=state.generator.meta.originOffset;
    offset[2]-=state.generator.meta.modelGroundZ*state.generator.meta.modelScale
        +state.generator.meta.modelGroundCurveZ*state.generator.meta.modelScale*(state.generator.meta.modelScale-1.0);
    WorldPoint(offset,zero,state.generator.angles,rotated);
    for(int a=0;a<3;a++)state.generator.placementAnchor[a]=state.generator.origin[a]-rotated[a];
    int buildCore=BuildCoreAt(state.generator.placementAnchor);
    if(buildCore<0 || buildCore!=state.generatorPowerCore)return false;
    state.generator.used=true;state.generator.alive=true;state.generator.constructing=false;
    state.generator.kind=Kind_Generator;state.generator.core=-1;state.generator.buildCore=buildCore;
    state.generator.file=C_Generators;state.generator.level=state.generatorLevel;
    state.generator.configRevision=gConfigRevision;state.generator.powerCore=state.generatorPowerCore;state.generator.gridAssigned=true;
    state.generator.ref=INVALID_ENT_REFERENCE;state.generator.effectRef=INVALID_ENT_REFERENCE;state.generator.supportRef=INVALID_ENT_REFERENCE;
    state.generator.upgraded=state.generatorLevel>1;
    for(int m=0;m<gMaterialCount;m++)state.generator.invested[m]=gRecipes[state.generatorRecipe].cost[m];
    for(int level=2;level<=state.generatorLevel;level++)
    {
        int cost[MATERIALS];if(!ReadLevelUpgradeCost(C_Generators,profile,level,cost))return false;
        for(int m=0;m<gMaterialCount;m++)
        {
            if(cost[m]<0 || cost[m]>LEVEL_CELL_LIMIT-state.generator.invested[m])return false;
            state.generator.invested[m]+=cost[m];
        }
    }
    return true;
}

bool V082RestoreReadBosses(KeyValues kv,V082UserRestoreCandidate state)
{
    kv.Rewind();if(!kv.JumpToKey("Bosses") || !kv.GotoFirstSubKey())return false;
    bool seen[MAX_BOSSES];int count;
    do
    {
        char id[128];kv.GetSectionName(id,sizeof(id));int boss=-1;
        for(int b=0;b<gBossCount;b++)if(StrEqual(id,gBosses[b].id)){boss=b;break;}
        int hp,defeated;
        if(boss<0 || seen[boss] || !V082RestoreInt(kv,"saved_hp",0,gBosses[boss].maxHP,hp)
            || !V082RestoreInt(kv,"defeated",0,1,defeated) || (defeated!=0 ? hp!=0 : hp<1))return false;
        state.bossHP[boss]=hp;state.bossDefeated[boss]=defeated!=0;seen[boss]=true;count++;
    }while(kv.GotoNextKey());
    return count==gBossCount;
}

bool V082RestoreReadCandidate(KeyValues kv,int client,V082UserRestoreCandidate state,char[] reason,int size)
{
    kv.Rewind();char root[64],map[PLATFORM_MAX_PATH];kv.GetSectionName(root,sizeof(root));kv.GetString("map",map,sizeof(map));
    int schema,materials,bases;
    if(!StrEqual(root,"V082UserSnapshot") || !StrEqual(map,gMap)
        || !V082RestoreInt(kv,"schema_version",1,1,schema)
        || !V082RestoreInt(kv,"material_count",gMaterialCount,gMaterialCount,materials)
        || !V082RestoreInt(kv,"base_count",gCoreCount,gCoreCount,bases)
        || !V082RestoreFlag(kv,"all_api_reads_valid") || !V082RestoreFlag(kv,"same_client_serial_at_end"))
        return V082RestoreFail(reason,size,"snapshot header/map/material/base count or export consistency failed");
    if(!kv.JumpToKey("Environment") || !V082RestoreFlag(kv,"enabled") || !V082RestoreInt(kv,"day",1,SKILL_CELL_LIMIT,state.day))
        return V082RestoreFail(reason,size,"snapshot Environment is incomplete");
    kv.Rewind();if(!kv.JumpToKey("player"))return V082RestoreFail(reason,size,"snapshot player is missing");
    char savedAuth[64],currentAuth[64];kv.GetString("verified_steam2",savedAuth,sizeof(savedAuth));
    if(!GetClientAuthId(client,AuthId_Steam2,currentAuth,sizeof(currentAuth),true) || !StrEqual(savedAuth,currentAuth)
        || kv.GetNum("steam_account_id")!=GetSteamAccountID(client,true) || !StrEqual(currentAuth,gInventoryOwner[client]))
        return V082RestoreFail(reason,size,"verified player identity does not match snapshot and inventory owner");
    if(!V082RestoreMaterialRows(kv,state.playerMaterials,gInventoryCap>0 ? gInventoryCap : 2000000000,true))
        return V082RestoreFail(reason,size,"personal material rows are incomplete, duplicate or outside capacity");
    if(!V082RestoreReadSkills(kv,state))return V082RestoreFail(reason,size,"Skills supplement is incomplete or budget/levels disagree");
    if(!V082RestoreReadBases(kv,state))return V082RestoreFail(reason,size,"fixed bases/storage differ from supported configuration");
    if(!V082RestoreReadBuildings(kv,state))return V082RestoreFail(reason,size,"snapshot must contain the fixed bases and exactly one operational generator");
    if(!V082RestoreReadGeneratorBody(kv,state))return V082RestoreFail(reason,size,"generator body/model/recipe/level/HP/output/build-area evidence is ambiguous");
    if(!V082RestoreReadBosses(kv,state))return V082RestoreFail(reason,size,"Bosses supplement is incomplete or invalid");
    state.generatorSlot=-1;
    for(int b=MAX_CORES;b<MAX_BUILDINGS;b++)if(!gBuildings[b].used){state.generatorSlot=b;break;}
    return state.generatorSlot>=0 || V082RestoreFail(reason,size,"no fresh generator slot available");
}

bool V082RestoreCommit(int client,V082UserRestoreCandidate state,char[] reason,int size)
{
    // Apply all potentially failing world operations before touching ledgers.
    // Retain exact base structs for rollback; existing base bodies are reused.
    static Building beforeBases[MAX_CORES];static Building beforeGenerator;
    for(int c=0;c<gCoreCount;c++)beforeBases[c]=gBuildings[gCores[c].building];
    int slot=state.generatorSlot,oldUID=gUID;beforeGenerator=gBuildings[slot];
    bool success=true;
    for(int c=0;c<gCoreCount;c++)
    {
        int b=gCores[c].building;gBuildings[b].level=state.baseLevel[c];
        if(!ApplyBuildingLevel(b,true)){success=false;break;}
        gBuildings[b].hp=state.baseHP[c];gBuildings[b].upgraded=state.baseLevel[c]>1;
    }
    if(success)
    {
        state.generator.uid=++gUID;gBuildings[slot]=state.generator;AssignBuildingOwner(slot,client);
        success=ApplyBuildingLevel(slot,true);
        if(success)
        {
            gBuildings[slot].hp=state.generatorHP;gBuildings[slot].constructionTarget=state.generatorMaxHP;
            success=SpawnBuildingBody(slot);
        }
    }
    if(!success)
    {
        RemoveBuildingEffect(slot);int failedRef=gBuildings[slot].ref;
        gBuildings[slot].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(failedRef);gBuildings[slot]=beforeGenerator;gUID=oldUID;
        for(int c=0;c<gCoreCount;c++)gBuildings[gCores[c].building]=beforeBases[c];
        MarkBuildAreasDirty();RecalculatePower();return V082RestoreFail(reason,size,"world restoration failed; fresh base/generator state rolled back, ledgers untouched");
    }
    int account=SkillsAccountIndex(client);
    for(int m=0;m<gMaterialCount;m++)gPlayerInventory[client][m]=state.playerMaterials[m];
    for(int c=0;c<gCoreCount;c++)for(int m=0;m<gMaterialCount;m++)gBaseInventory[c][m]=state.baseMaterials[c*MATERIALS+m];
    gDay=state.day;gSkillsHighestDay=state.highest;gSkillsEarned=state.earned;gSkillsDailyRate=state.rate;
    for(int skill=0;skill<SKILL_COUNT;skill++)gSkillsRows.Set(account,state.skillLevels[skill],skill);
    gSkillsRows.Set(account,state.spent,SKILL_SPENT);gSkillsRows.Set(account,state.adjustment,SKILL_ADMIN);
    gSkillsRows.Set(account,gSkillsRows.Get(account,SKILL_REVISION)+1,SKILL_REVISION);
    gSkillsConfigRevision++;SkillsClearPulses(client);gSkillsSpawnAwardPending[client]=false;
    if(gSkillsRateSchedule==null)gSkillsRateSchedule=new ArrayList(2);else gSkillsRateSchedule.Clear();
    SkillsBudgetSchedule(state.rate); // Explicit import boundary; do not invent historical rate changes.
    if(gSkillsDailyCvar!=null)gSkillsDailyCvar.IntValue=state.rateOverride;
    for(int b=0;b<gBossCount;b++)
    {gBosses[b].savedHP=state.bossHP[b];gBosses[b].defeated=state.bossDefeated[b];}
    ResetBossRoundState();gSnapshot=true;gRoundRestored=true;gNextHUD=0.0;
    MarkBuildAreasDirty();RecalculatePower();gV082UserRestoreDone=true;
    PrintToServer("[V082Restore] imported=1 day=%d highest=%d earned=%d spent=%d adjustment=%d rate=%d generator_old_uid=%d generator_new_uid=%d hp=%d/%d",
        gDay,gSkillsHighestDay,gSkillsEarned,state.spent,state.adjustment,gSkillsDailyRate,state.generatorOldUID,gBuildings[slot].uid,gBuildings[slot].hp,gBuildings[slot].maxHP);
    PrintToServer("[V082Restore] restored materials/base storage/skill ledger/Boss progress; awake reset; rate-history boundary=%d; no actor teleport, NPC spawn or round-start callback",gSkillsHighestDay+1);
    return true;
}

public Action V082RestoreUserStateCommand(int args)
{
    char reason[256],basename[128];GetCmdArg(1,basename,sizeof(basename));
    if(args!=1 || gV082UserRestoreBusy || gV082UserRestoreDone)
    {PrintToServer("[V082Restore] REFUSED: one filename required; import is single-use per diagnostic plugin instance");return Plugin_Handled;}
    int length=strlen(basename);bool valid=length>17 && length<96 && StrContains(basename,"v082_routes_")==0
        && StrContains(basename,"..")==-1 && StrEqual(basename[length-4],".cfg");
    for(int i=0;valid && i<length;i++)if(!((basename[i]>='a' && basename[i]<='z') || (basename[i]>='A' && basename[i]<='Z')
        || (basename[i]>='0' && basename[i]<='9') || basename[i]=='_' || basename[i]=='-' || basename[i]=='.'))valid=false;
    if(!valid){PrintToServer("[V082Restore] REFUSED: filename must be a plain v082_routes_*.cfg name in SourceMod data");return Plugin_Handled;}
    int client=V082RestoreSoleHuman();
    if(client<1 || !V082RestoreFresh(client,reason,sizeof(reason)))
    {PrintToServer("[V082Restore] REFUSED: %s",client<1 ? "exactly one real actor required" : reason);return Plugin_Handled;}
    gV082UserRestoreBusy=true;int serial=GetClientSerial(client),epoch=gEpoch,revision=gConfigRevision;
    static V082UserRestoreCandidate empty;gV082UserRestoreCandidate=empty;
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"data/%s",basename);
    KeyValues kv=new KeyValues("V082UserSnapshot");
    bool loaded=kv.ImportFromFile(path);
    bool prepared=loaded && V082RestoreReadCandidate(kv,client,gV082UserRestoreCandidate,reason,sizeof(reason));delete kv;
    if(!loaded)strcopy(reason,sizeof(reason),"snapshot could not be imported");
    if(prepared && (GetClientFromSerial(serial)!=client || gEpoch!=epoch || gConfigRevision!=revision
        || !V082RestoreFresh(client,reason,sizeof(reason))))
    {prepared=false;strcopy(reason,sizeof(reason),"fresh identity/session changed before commit");}
    bool restored=prepared && V082RestoreCommit(client,gV082UserRestoreCandidate,reason,sizeof(reason));
    gV082UserRestoreBusy=false;
    if(!restored)PrintToServer("[V082Restore] REFUSED: %s",reason);
    return Plugin_Handled;
}

void RegisterV082UserStateRestore()
{
    if(CommandExists("sm_v082_restore_user_state"))
    {LogError("[V082Restore] command already exists; refusing to override it");return;}
    RegServerCmd("sm_v082_restore_user_state",V082RestoreUserStateCommand,"Diagnostic single-use restore of the exported sole-user state into a fresh waiting main");
}
