static const char sBuildCategoryPhrases[][]={"BuildShelterName","HUDTurretName","HUDFlameName","BuildStructureName","HUDMineName",
    "HUDAmplifierName","HUDGeneratorName","HUDLightName","HUDGrenadeName","BuildExpanderName",
    "BuildAutoRepairName","BuildHealName","BuildArmorName","BuildBeaconName","BuildCampfireName","BuildTeslaName","BuildPlasmaName","BuildWatchtowerName","BuildMortarName","BuildWorkbenchName"};
#define BUILD_CATEGORY_MORTAR view_as<BuildingKind>(18)
enum BuildMenuGroup { BuildGroup_Shelter, BuildGroup_Autonomous, BuildGroup_Restoration, BuildGroup_BaseFacilities,
    BuildGroup_Lighting, BuildGroup_Defenses, BuildGroup_Campfire, BuildGroup_Count }
enum BuildMenuView { BuildView_Closed, BuildView_Root, BuildView_Group, BuildView_Variants, BuildView_Details, BuildView_Confirm }
static const char sBuildGroupPhrases[][]={"BuildShelterName","BuildGroupAutonomous","BuildGroupRestoration","BuildGroupBaseFacilities",
    "BuildGroupLighting","BuildGroupDefenses","BuildCampfireName"};
static const BuildingKind sBuildCategoryOrder[]={Kind_Base,Kind_Turret,Kind_Flame,Kind_Grenade,BUILD_CATEGORY_MORTAR,Kind_Tesla,Kind_Plasma,Kind_Heal,Kind_AutoRepair,Kind_Armor,
    Kind_Expander,Kind_Beacon,Kind_Workbench,Kind_Amplifier,Kind_Generator,Kind_Light,Kind_Fence,Kind_Mine,Kind_Watchtower,Kind_Campfire};
int gBuildMenuNonce[MAXPLAYERS+1],gBuildMenuEpoch[MAXPLAYERS+1],gBuildMenuSerial[MAXPLAYERS+1],gBuildMenuRevision[MAXPLAYERS+1];
BuildMenuGroup gBuildMenuGroup[MAXPLAYERS+1];
BuildMenuView gBuildMenuView[MAXPLAYERS+1];
Menu gBuildMenuHandle[MAXPLAYERS+1];
int gBuildMenuPreviewCore[MAXPLAYERS+1];
BuildingKind gBuildMenuCategory[MAXPLAYERS+1];
ConfigFile gBuildMenuFile[MAXPLAYERS+1];
char gBuildMenuRecipe[MAXPLAYERS+1][64],gBuildMenuType[MAXPLAYERS+1][64];

void BeginBuildMenu(int client)
{
    StopBuildPreview(client);
    gBuildMenuNonce[client]++;gBuildMenuEpoch[client]=gEpoch;gBuildMenuSerial[client]=GetClientSerial(client);
    gBuildMenuRevision[client]=gConfigRevision;
    gBuildMenuView[client]=BuildView_Closed;gBuildMenuHandle[client]=null;
}

BuildMenuGroup BuildGroupForKind(BuildingKind kind)
{
    switch(kind)
    {
        case Kind_Base:return BuildGroup_Shelter;
        case Kind_Turret,Kind_Flame,Kind_Grenade,BUILD_CATEGORY_MORTAR,Kind_Tesla,Kind_Plasma:return BuildGroup_Autonomous;
        case Kind_Heal,Kind_AutoRepair,Kind_Armor:return BuildGroup_Restoration;
        case Kind_Expander,Kind_Beacon,Kind_Workbench,Kind_Amplifier,Kind_Generator:return BuildGroup_BaseFacilities;
        case Kind_Light:return BuildGroup_Lighting;
        case Kind_Fence,Kind_Mine,Kind_Watchtower:return BuildGroup_Defenses;
        case Kind_Campfire:return BuildGroup_Campfire;
    }
    return BuildGroup_Count;
}

bool BuildCategoryAvailable(BuildingKind category)
{
    for(int r=0;r<gRecipeCount;r++)if(RecipeBuildCategory(r)==category)return true;
    return false;
}

BuildingKind RecipeBuildCategory(int recipe)
{
    if(recipe<0 || recipe>=gRecipeCount)return view_as<BuildingKind>(-1);
    if(gRecipes[recipe].kind==Kind_Grenade
        && MortarTrajectory(gRecipes[recipe].file,gRecipes[recipe].type))return BUILD_CATEGORY_MORTAR;
    return gRecipes[recipe].kind;
}

bool BuildGroupAvailable(BuildMenuGroup group)
{
    if(group<BuildGroup_Shelter || group>=BuildGroup_Count)return false;
    for(int r=0;r<gRecipeCount;r++)if(BuildGroupForKind(gRecipes[r].kind)==group)return true;
    return false;
}

bool BuildGroupHasSubmenu(BuildMenuGroup group)
{
    return group>BuildGroup_Shelter && group<BuildGroup_Campfire;
}

int BuildVariantParentOperation(BuildingKind category)
{
    return BuildGroupHasSubmenu(BuildGroupForKind(category)) ? 8 : 4;
}

bool BuildMenuSessionMatches(int client,int nonce,int serial,int epoch,int revision)
{
    return client>0 && client<=MaxClients && nonce==gBuildMenuNonce[client] && serial==gBuildMenuSerial[client]
        && epoch==gBuildMenuEpoch[client] && revision==gBuildMenuRevision[client];
}

bool BuildMenuOperationAllowed(int client,int operation,int key)
{
    if(client<1 || client>MaxClients)return false;
    BuildMenuView view=gBuildMenuView[client];
    switch(operation)
    {
        case 0:return view==BuildView_Group && key>=0 && key<sizeof(sBuildCategoryPhrases)
            && BuildGroupForKind(view_as<BuildingKind>(key))==gBuildMenuGroup[client] && BuildCategoryAvailable(view_as<BuildingKind>(key));
        case 1:return view==BuildView_Variants;
        case 2,5:return view==BuildView_Details;
        case 3,6:return view==BuildView_Confirm;
        case 4:return view==BuildView_Group || (view==BuildView_Variants && !BuildGroupHasSubmenu(gBuildMenuGroup[client]));
        case 7:return view==BuildView_Root && key>=0 && key<view_as<int>(BuildGroup_Count) && BuildGroupAvailable(view_as<BuildMenuGroup>(key));
        case 8:return view==BuildView_Variants && key==view_as<int>(gBuildMenuGroup[client]) && BuildGroupHasSubmenu(gBuildMenuGroup[client]);
    }
    return false;
}

int FindRecipeByID(ConfigFile file,const char[] id)
{
    for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].file==file && StrEqual(gRecipes[r].id,id))return r;
    return -1;
}

void AddBuildMenuItem(Menu menu,int client,int operation,int key,const char[] id,const char[] label,bool enabled=true)
{
    char token[192];Format(token,sizeof(token),"%d:%d:%d:%s",gBuildMenuNonce[client],operation,key,id);
    menu.AddItem(token,label,enabled ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
}

void AddBuildAction(Menu menu,int client,int recipe,int operation,const char[] phrase,bool enabled=true)
{
    char label[192];TranslateLabel(client,phrase,label,sizeof(label));
    AddBuildMenuItem(menu,client,operation,view_as<int>(gRecipes[recipe].file),gRecipes[recipe].id,label,enabled);
}

Menu CreateBuildCategoriesMenu(int client)
{
    char title[192];Format(title,sizeof(title),"%T","BuildCategoryTitle",client);
    Menu menu=new Menu(BuildMenuHandler);menu.SetTitle("%s",title);
    for(int k=0;k<view_as<int>(BuildGroup_Count);k++)
    {
        if(!BuildGroupAvailable(view_as<BuildMenuGroup>(k)))continue;
        char label[128];TranslateLabel(client,sBuildGroupPhrases[k],label,sizeof(label));
        AddBuildMenuItem(menu,client,7,k,"-",label);
    }
    return menu;
}

void ShowBuildCategories(int client)
{
    if(!LiveHuman(client) || !Running())return;BeginBuildMenu(client);
    gBuildMenuView[client]=BuildView_Root;
    Menu menu=CreateBuildCategoriesMenu(client);gBuildMenuHandle[client]=menu;
    menu.Display(client,30);
}

Menu CreateBuildGroupMenu(int client,BuildMenuGroup group)
{
    char title[256],groupName[128];TranslateLabel(client,sBuildGroupPhrases[group],groupName,sizeof(groupName));
    Format(title,sizeof(title),"%T","BuildGroupTitle",client,groupName);
    Menu menu=new Menu(BuildMenuHandler);menu.SetTitle("%s",title);
    for(int i=0;i<sizeof(sBuildCategoryOrder);i++)
    {
        BuildingKind category=sBuildCategoryOrder[i];
        if(BuildGroupForKind(category)!=group || !BuildCategoryAvailable(category))continue;
        char label[128];TranslateLabel(client,sBuildCategoryPhrases[category],label,sizeof(label));
        AddBuildMenuItem(menu,client,0,view_as<int>(category),"-",label);
    }
    TranslateLabel(client,"BuildBackCategories",title,sizeof(title));AddBuildMenuItem(menu,client,4,0,"-",title);
    return menu;
}

void ShowBuildGroup(int client,BuildMenuGroup group)
{
    if(!LiveHuman(client) || !Running() || !BuildGroupAvailable(group))return;
    if(!BuildGroupHasSubmenu(group))
    {ShowBuildVariants(client,group==BuildGroup_Shelter ? Kind_Base : Kind_Campfire);return;}
    BeginBuildMenu(client);gBuildMenuGroup[client]=group;gBuildMenuView[client]=BuildView_Group;
    Menu menu=CreateBuildGroupMenu(client,group);gBuildMenuHandle[client]=menu;menu.Display(client,30);
}

Menu CreateBuildVariantsMenu(int client,BuildingKind category)
{
    char title[256],categoryName[128];TranslateLabel(client,sBuildCategoryPhrases[category],categoryName,sizeof(categoryName));
    Format(title,sizeof(title),"%T","BuildVariantTitle",client,categoryName);
    Menu menu=new Menu(BuildMenuHandler);menu.SetTitle("%s",title);
    for(int r=0;r<gRecipeCount;r++)if(RecipeBuildCategory(r)==category)
    {
        char label[192],name[128];TranslateLabel(client,gRecipes[r].label,name,sizeof(name));
        int required;
        if(!RecipeBaseLevelAllowed(r,required))Format(label,sizeof(label),"%T","BuildVariantBaseLocked",client,name,required);
        else Format(label,sizeof(label),"%T",HasBaseUpgradeMaterials(gRecipes[r].cost,client) ? "BuildVariantAvailable" : "BuildVariantNeedsMaterials",client,name);
        AddBuildMenuItem(menu,client,1,view_as<int>(gRecipes[r].file),gRecipes[r].id,label);
    }
    int parent=BuildVariantParentOperation(category);
    TranslateLabel(client,parent==8 ? "BuildBackParentGroup" : "BuildBackCategories",title,sizeof(title));
    AddBuildMenuItem(menu,client,parent,parent==8 ? view_as<int>(BuildGroupForKind(category)) : 0,"-",title);
    return menu;
}

void ShowBuildVariants(int client,BuildingKind category)
{
    if(!LiveHuman(client) || !Running() || !BuildCategoryAvailable(category))return;BeginBuildMenu(client);
    gBuildMenuCategory[client]=category;gBuildMenuGroup[client]=BuildGroupForKind(category);gBuildMenuView[client]=BuildView_Variants;
    Menu menu=CreateBuildVariantsMenu(client,category);gBuildMenuHandle[client]=menu;menu.Display(client,30);
}

void AddRecipeStat(Menu menu,int client,const char[] phrase,float value)
{
    char label[128],line[256];TranslateLabel(client,phrase,label,sizeof(label));
    Format(line,sizeof(line),"%T","BuildStatValue",client,label,value);menu.AddItem("",line,ITEMDRAW_DISABLED);
}

bool SupportMenuKind(BuildingKind kind)
{
    return kind==Kind_AutoRepair || kind==Kind_Heal || kind==Kind_Armor || kind==Kind_Beacon || kind==Kind_Workbench;
}

int SupportMenuCount(int core,BuildingKind kind)
{
    if(core<0 || core>=gCoreCount || !SupportMenuKind(kind))return 0;
    if(kind==Kind_Workbench)return WorkbenchCount(core);
    return kind==Kind_Beacon ? BeaconCount(core,-1) : RestorationCount(core,kind,-1);
}

void AppendSupportMenuInfo(int client,int core,BuildingKind kind,int limit,char[] text,int length)
{
    if(core<0 || core>=gCoreCount || !SupportMenuKind(kind))return;
    char base[64],line[192];CoreDisplayName(core,base,sizeof(base),client);
    Format(line,sizeof(line),"%T","BuildSupportGroupInfo",client,base,SupportMenuCount(core,kind),limit);StrCat(text,length,line);
}

void AddRecipeCostItems(Menu menu,int client,const int cost[MATERIALS])
{
    bool any;
    for(int m=0;m<gMaterialCount;m++)if(cost[m]>0)
    {
        char name[128],line[256];TranslateLabel(client,gMaterials[m].label,name,sizeof(name));
        Format(line,sizeof(line),"%T","BuildCostRow",client,name,gPlayerInventory[client][m],cost[m]);menu.AddItem("",line,ITEMDRAW_DISABLED);any=true;
    }
    if(!any){char line[128];TranslateLabel(client,"BuildNoMaterialsCost",line,sizeof(line));menu.AddItem("",line,ITEMDRAW_DISABLED);}
}

void AddProfileLevelItems(Menu menu,int client,ConfigFile file,BuildingKind kind,const char[] profile,int level,bool showInitial=false)
{
    char line[256];
    int hp,initial;ReadLevelInt(file,profile,level,"max_hp",0,hp);ReadLevelInt(file,profile,level,"initial_hp",hp,initial);
    if(showInitial)Format(line,sizeof(line),"%T","BuildLevelHP",client,level,initial,hp);
    else Format(line,sizeof(line),"%T","BuildLevelMaxHP",client,level,hp);
    menu.AddItem("",line,ITEMDRAW_DISABLED);
    float value;
    if(kind==Kind_Turret || kind==Kind_Flame || kind==Kind_Grenade || kind==Kind_Mine || kind==Kind_Tesla || kind==Kind_Plasma)
    {
        ReadLevelFloat(file,profile,level,"damage",0.0,value);AddRecipeStat(menu,client,"BuildStatDamage",value);
        ReadLevelFloat(file,profile,level,"range",0.0,value);AddRecipeStat(menu,client,"BuildStatRange",value);
        if(kind!=Kind_Mine){ReadLevelFloat(file,profile,level,"fire_interval",0.5,value);AddRecipeStat(menu,client,"BuildStatInterval",value);}
    }
    if(kind==Kind_Grenade || kind==Kind_Mine || kind==Kind_Plasma)
    {ReadLevelFloat(file,profile,level,"blast_radius",180.0,value);AddRecipeStat(menu,client,"BuildStatBlastRadius",value);}
    if(kind==Kind_Mine){TranslateLabel(client,"BuildMineSingleUse",line,sizeof(line));menu.AddItem("",line,ITEMDRAW_DISABLED);}
    if(kind==Kind_Grenade || kind==Kind_Plasma)
    {ReadLevelFloat(file,profile,level,"projectile_speed",900.0,value);AddRecipeStat(menu,client,"BuildStatProjectileSpeed",value);}
    if(kind==Kind_Plasma)
    {
        ReadLevelFloat(file,profile,level,"field_damage",0.0,value);AddRecipeStat(menu,client,"BuildStatPlasmaFieldDamage",value);
        ReadLevelFloat(file,profile,level,"field_duration",0.0,value);AddRecipeStat(menu,client,"BuildStatPlasmaFieldDuration",value);
        ReadLevelFloat(file,profile,level,"field_radius",96.0,value);AddRecipeStat(menu,client,"BuildStatPlasmaFieldRadius",value);
        ReadLevelFloat(file,profile,level,"field_interval",0.5,value);AddRecipeStat(menu,client,"BuildStatPlasmaFieldInterval",value);
    }
    if(kind==Kind_Flame)
    {
        ReadLevelFloat(file,profile,level,"cone_half_angle_degrees",30.0,value);AddRecipeStat(menu,client,"BuildStatCone",value);
        int burn;ReadLevelInt(file,profile,level,"burn_damage",0,burn);AddRecipeStat(menu,client,"BuildStatBurnDamage",float(burn));
        ReadLevelFloat(file,profile,level,"burn_interval",0.5,value);AddRecipeStat(menu,client,"BuildStatBurnInterval",value);
        ReadLevelFloat(file,profile,level,"burn_duration",0.0,value);AddRecipeStat(menu,client,"BuildStatBurnDuration",value);
    }
    if(kind==Kind_Generator)
    {ReadLevelFloat(file,profile,level,"power_output",0.0,value);AddRecipeStat(menu,client,"BuildStatPowerOutput",value);}
    if(kind==Kind_Fence)
    {ReadLevelFloat(file,profile,level,"damage",0.0,value);if(value>0.0)AddRecipeStat(menu,client,"BuildStatReflectDamage",value);}
    if(SupportMenuKind(kind))
    {
        char type[64];if(StrContains(profile,"Types/")==0)strcopy(type,sizeof(type),profile[6]);
        AddRecipeStat(menu,client,"BuildStatMaxPerBase",float(SupportBuildLimit(file,type,level)));
        if(kind!=Kind_Beacon && kind!=Kind_Workbench)
        {
            ReadLevelFloat(file,profile,level,"restore_interval_seconds",20.0,value);if(value<1.0)value=1.0;
            AddRecipeStat(menu,client,"BuildStatRestoreInterval",value);
            int amount;ReadLevelInt(file,profile,level,"restore_amount",0,amount);
            AddRecipeStat(menu,client,kind==Kind_AutoRepair ? "BuildStatRepairAmount" : (kind==Kind_Heal ? "BuildStatHealAmount" : "BuildStatArmorAmount"),float(amount));
            TranslateLabel(client,kind==Kind_AutoRepair ? "BuildRepairScope" : (kind==Kind_Heal ? "BuildHealScope" : "BuildArmorScope"),line,sizeof(line));
            menu.AddItem("",line,ITEMDRAW_DISABLED);
        }
    }
    ReadLevelFloat(file,profile,level,"power_required",0.0,value);if(value>0.0 || SupportMenuKind(kind))AddRecipeStat(menu,client,"BuildStatPowerRequired",value);
    if(kind==Kind_Light)
    {
        ReadLevelFloat(file,profile,level,"light_radius",500.0,value);AddRecipeStat(menu,client,"BuildStatLightRadius",value);
        int brightness;ReadLevelInt(file,profile,level,"light_brightness",2,brightness);AddRecipeStat(menu,client,"BuildStatBrightness",float(brightness));
    }
    if(kind==Kind_Expander)
    {
        ReadLevelFloat(file,profile,level,"build_range_bonus",0.0,value);AddRecipeStat(menu,client,"BuildStatRangeBonus",value);
        int bonus;ReadLevelInt(file,profile,level,"build_limit_bonus",0,bonus);AddRecipeStat(menu,client,"BuildStatLimitBonus",float(bonus));
    }
    if(kind==Kind_Base)
    {
        int limit;float extents[3];ReadLevelInt(file,profile,level,"build_limit",24,limit);
        AddRecipeStat(menu,client,"BuildStatBaseQuota",float(limit));
        if(ReadBuildHalfExtents(file,profile,level,extents))
        {AddRecipeStat(menu,client,"BuildStatHalfX",extents[0]);AddRecipeStat(menu,client,"BuildStatHalfY",extents[1]);}
    }
    if(kind==Kind_Campfire)
    {
        char type[64];if(StrContains(profile,"Types/")==0)strcopy(type,sizeof(type),profile[6]);
        float radius,interval;int decay,threshold;bool noDecay;
        if(ReadCampfireLevel(file,type,level,radius,decay,interval,threshold,noDecay))
        {
            AddRecipeStat(menu,client,"BuildStatSafeRadius",radius);
            AddRecipeStat(menu,client,"BuildStatDecayAmount",float(decay));
            AddRecipeStat(menu,client,"BuildStatDecayInterval",interval);
            int capacity,gain,cost;
            if(ReadCampfireFuelProfileLevel(profile,level,capacity,gain,cost))
            {
                AddRecipeStat(menu,client,"BuildStatFuelCapacity",float(capacity));
                AddRecipeStat(menu,client,"BuildStatFuelGain",float(gain));
                AddRecipeStat(menu,client,"BuildStatFuelCost",float(cost));
            }
        }
    }
    if(kind==Kind_Amplifier)
    {
        static const char fields[][]={"player_ordinary_bonus","player_boss_bonus","defense_ordinary_bonus","defense_boss_bonus"};
        static const char labels[][]={"BuildStatPlayerOrdinary","BuildStatPlayerBoss","BuildStatDefenseOrdinary","BuildStatDefenseBoss"};
        for(int f=0;f<4;f++){ReadLevelFloat(file,profile,level,fields[f],0.0,value);AddRecipeStat(menu,client,labels[f],value*100.0);}
    }
    int required;ReadLevelInt(file,profile,level,"minimum_base_level",1,required);
    if(required>1 || kind==Kind_Amplifier)AddRecipeStat(menu,client,"BuildStatBaseLevel",float(required));
}

void AddRecipeLevelItems(Menu menu,int client,int recipe,int level)
{
    char profile[128];Format(profile,sizeof(profile),"Types/%s",gRecipes[recipe].type);
    AddProfileLevelItems(menu,client,gRecipes[recipe].file,gRecipes[recipe].kind,profile,level,level==1);
}

void ShowBuildRecipeDetails(int client,int recipe,bool confirmation=false,bool redraw=false)
{
    if(!LiveHuman(client) || !Running() || recipe<0 || recipe>=gRecipeCount)return;
    if(redraw && (!confirmation || !PreviewSessionValid(client) || gBuildPreviewRecipe[client]!=recipe))return;
    if(!redraw)BeginBuildMenu(client);
    gBuildMenuCategory[client]=RecipeBuildCategory(recipe);gBuildMenuFile[client]=gRecipes[recipe].file;
    gBuildMenuGroup[client]=BuildGroupForKind(gRecipes[recipe].kind);
    gBuildMenuView[client]=confirmation ? BuildView_Confirm : BuildView_Details;
    strcopy(gBuildMenuRecipe[client],sizeof(gBuildMenuRecipe[]),gRecipes[recipe].id);
    strcopy(gBuildMenuType[client],sizeof(gBuildMenuType[]),gRecipes[recipe].type);
    char name[128],description[256],title[768],profile[128],line[256];TranslateLabel(client,gRecipes[recipe].label,name,sizeof(name));
    TranslateLabel(client,gRecipes[recipe].meta.description,description,sizeof(description));
    Format(profile,sizeof(profile),"Types/%s",gRecipes[recipe].type);
    Format(title,sizeof(title),"%T",confirmation ? "BuildConfirmTitle" : "BuildDetailsTitle",client,name,description,ProfileMaximumLevel(gRecipes[recipe].file,profile));
    if(confirmation && !redraw)BeginBuildPreview(client,recipe,gBuildMenuNonce[client]);
    float anchor[3];gBuildMenuPreviewCore[client]=-1;
    bool outsideKind=gRecipes[recipe].kind==Kind_Campfire || gRecipes[recipe].file==C_Shelters;
    bool previewValid;
    if(confirmation)
    {previewValid=gBuildPreviewValid[client];gBuildMenuPreviewCore[client]=gBuildPreviewCore[client];}
    else
    {
        PlacementCandidate candidate;previewValid=ComputeRecipePlacementCandidate(client,recipe,candidate,false);
        anchor=candidate.anchor;
        if(previewValid)gBuildMenuPreviewCore[client]=RecipeBuildCoreAt(recipe,candidate);
        if(previewValid && gRecipes[recipe].file==C_Shelters)
        {previewValid=DynamicBasePositionAllowed(recipe,anchor,false,client);gBuildMenuPreviewCore[client]=NextDynamicBaseSlot();}
    }
    if(outsideKind && previewValid){Format(line,sizeof(line),"%T","BuildIndependentPlacement",client);StrCat(title,sizeof(title),line);}
    else if(gBuildMenuPreviewCore[client]>=0)AppendBuildCapacityInfo(client,gBuildMenuPreviewCore[client],title,sizeof(title));
    else{Format(line,sizeof(line),"%T","BuildPreviewUnavailable",client);StrCat(title,sizeof(title),line);}
    bool groupAvailable=true;
    if(SupportMenuKind(gRecipes[recipe].kind) && gBuildMenuPreviewCore[client]>=0)
    {
        int limit=SupportBuildLimit(gRecipes[recipe].file,gRecipes[recipe].type,1);
        AppendSupportMenuInfo(client,gBuildMenuPreviewCore[client],gRecipes[recipe].kind,limit,title,sizeof(title));
        groupAvailable=SupportMenuCount(gBuildMenuPreviewCore[client],gRecipes[recipe].kind)<limit;
    }
    Menu menu=new Menu(BuildMenuHandler);menu.SetTitle("%s",title);
    bool enough=HasBaseUpgradeMaterials(gRecipes[recipe].cost,client);
    int requiredBase;bool unlocked=RecipeBaseLevelAllowed(recipe,requiredBase);
    if(!unlocked)
    {
        char locked[192];Format(locked,sizeof(locked),"%T","FacilityBaseLevelRequired",client,requiredBase);
        menu.AddItem("",locked,ITEMDRAW_DISABLED);
    }
    // A user opening an invalid confirmation gets one private failure cue.
    // Silent placement queries (and a future moving preview) never emit audio.
    if(confirmation && !redraw && (!previewValid || (!outsideKind && gBuildMenuPreviewCore[client]<0) || !groupAvailable || !enough || !unlocked))
        PlayClientInteractionSound(client,"build_failed");
    AddBuildAction(menu,client,recipe,confirmation ? 3 : 2,confirmation ? (groupAvailable ? "BuildConfirmPlace" : "BuildSupportQuotaFull") : "BuildOpenConfirm",
        unlocked && (!confirmation || (enough && groupAvailable && previewValid && (outsideKind || gBuildMenuPreviewCore[client]>=0))));
    AddBuildAction(menu,client,recipe,confirmation ? 6 : 5,confirmation ? "BuildBackDetails" : "BuildBackVariants");
    AddRecipeCostItems(menu,client,gRecipes[recipe].cost);
    if(!confirmation)
    {
        AddRecipeLevelItems(menu,client,recipe,1);
        char mode[128];TranslateLabel(client,StrEqual(gRecipes[recipe].meta.placementMode,"upright") ? "BuildPlacementUpright" :
            (StrEqual(gRecipes[recipe].meta.placementMode,"surface") ? "BuildPlacementSurface" : "BuildPlacementWall"),mode,sizeof(mode));
        menu.AddItem("",mode,ITEMDRAW_DISABLED);
        bool growth;int anchorLevel,maximum;ReadLevelGrowth(gRecipes[recipe].file,profile,growth,anchorLevel,maximum);
        if(growth){Format(line,sizeof(line),"%T","BuildGrowthSummary",client,anchorLevel,maximum);menu.AddItem("",line,ITEMDRAW_DISABLED);}
        if(LevelAvailable(gRecipes[recipe].file,profile,2))
        {
            int cost[MATERIALS];
            if(ReadLevelUpgradeCost(gRecipes[recipe].file,profile,2,cost))
            {
                TranslateLabel(client,"BuildNextLevelDetails",line,sizeof(line));menu.AddItem("",line,ITEMDRAW_DISABLED);
                AddRecipeLevelItems(menu,client,recipe,2);AddRecipeCostItems(menu,client,cost);
            }
        }
    }
    if(confirmation)
    {
        // Install the new handle before Display cancels the old display. Its
        // late Cancel callback must not remove this same-session preview.
        gBuildPreviewMenu[client]=menu;gBuildMenuHandle[client]=menu;gBuildPreviewSignature[client]=BuildPreviewMenuSignature(client);
        int remaining=RoundToCeil(gBuildPreviewDeadline[client]-GetGameTime());if(remaining<1)remaining=1;
        if(!menu.Display(client,remaining)){StopBuildPreview(client,menu);delete menu;}
    }
    else {gBuildMenuHandle[client]=menu;menu.Display(client,45);}
}

public Action BuildMenuCommand(int client,int args)
{
    if(!LiveHuman(client) || !Running()){Tell(client,"%T","CmdNotStarted",client);return Plugin_Handled;}
    ShowBuildCategories(client);return Plugin_Handled;
}

public int BuildMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)
    {
        for(int c=1;c<=MaxClients;c++)if(gBuildMenuHandle[c]==menu)
        {gBuildMenuHandle[c]=null;gBuildMenuView[c]=BuildView_Closed;gBuildMenuNonce[c]++;}
        delete menu;
    }
    else if(action==MenuAction_Cancel)
    {
        if(client>0 && client<=MaxClients && gBuildMenuHandle[client]==menu)
        {gBuildMenuHandle[client]=null;gBuildMenuView[client]=BuildView_Closed;gBuildMenuNonce[client]++;}
        StopBuildPreview(client,menu);
    }
    else if(action==MenuAction_Select)
    {
        char token[192],fields[5][80];int nonce,operation,key;menu.GetItem(item,token,sizeof(token));
        bool valid=LiveHuman(client) && Running() && ExplodeString(token,":",fields,5,80)==4
            && ParseLevelInteger(fields[0],nonce) && ParseLevelInteger(fields[1],operation) && ParseLevelInteger(fields[2],key)
            && BuildMenuSessionMatches(client,nonce,GetClientSerial(client),gEpoch,gConfigRevision)
            && gBuildMenuHandle[client]==menu && BuildMenuOperationAllowed(client,operation,key);
        if(valid && (operation==3 || operation==6) && gBuildPreviewMenu[client]!=menu)valid=false;
        if(!valid){StopBuildPreview(client,menu);if(LiveHuman(client))Tell(client,"%T","MenuExpired",client);return 0;}
        gBuildMenuNonce[client]++; // Consume before navigation, validation failure, or payment.
        if(operation==0){if(key>=0 && key<sizeof(sBuildCategoryPhrases))ShowBuildVariants(client,view_as<BuildingKind>(key));return 0;}
        if(operation==4){ShowBuildCategories(client);return 0;}
        if(operation==7 || operation==8){ShowBuildGroup(client,view_as<BuildMenuGroup>(key));return 0;}
        int recipe=key>=0 && key<view_as<int>(C_Count) ? FindRecipeByID(view_as<ConfigFile>(key),fields[3]) : -1;
        if(recipe<0){Tell(client,"%T","MenuExpired",client);return 0;}
        if(operation==1 && (RecipeBuildCategory(recipe)!=gBuildMenuCategory[client] || BuildGroupForKind(gRecipes[recipe].kind)!=gBuildMenuGroup[client]))
        {Tell(client,"%T","MenuExpired",client);return 0;}
        if(operation!=1 && (gBuildMenuFile[client]!=gRecipes[recipe].file || !StrEqual(gBuildMenuRecipe[client],gRecipes[recipe].id)
            || !StrEqual(gBuildMenuType[client],gRecipes[recipe].type)))
        {Tell(client,"%T","MenuExpired",client);return 0;}
        switch(operation)
        {
            case 1,6: ShowBuildRecipeDetails(client,recipe);
            case 2: ShowBuildRecipeDetails(client,recipe,true);
            case 3:
            {
                ConfirmBuildPreview(client,recipe,nonce);ShowBuildRecipeDetails(client,recipe);
            }
            case 5: ShowBuildVariants(client,gBuildMenuCategory[client]);
        }
    }
    return 0;
}

public Action MaterialsCommand(int client,int args)
{
    if(client<1 || !IsClientInGame(client)){ReplyToCommand(client,"%T","CmdPlayerInventoryOnly",client);return Plugin_Handled;}
    Menu menu=new Menu(InventoryMenuHandler);char title[128];TranslateLabel(client,"PersonalBackpackTitle",title,sizeof(title));menu.SetTitle("%s",title);
    for(int m=0;m<gMaterialCount;m++)
    {
        char name[128],line[192];TranslateLabel(client,gMaterials[m].label,name,sizeof(name));
        Format(line,sizeof(line),"%T","PersonalBackpackRow",client,name,gPlayerInventory[client][m]);menu.AddItem("",line,ITEMDRAW_DISABLED);
    }
    menu.Display(client,30);return Plugin_Handled;
}

public int InventoryMenuHandler(Menu menu,MenuAction action,int client,int item){if(action==MenuAction_End)delete menu;return 0;}

public Action StatusCommand(int client,int args)
{
    int tracked,parked,activeBoss;
    for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && !gNPCs[i].dead){tracked++;if(gNPCs[i].paused)parked++;if(gNPCs[i].boss>=0 && gNPCs[i].active)activeBoss++;}
    ReplyToCommand(client,"%T","CmdStatus",client,
        SHELTER_VERSION,gEnabled,gCoreReady,gPhase,gDay,AliveCores(),ExistingCoreCount(),gNodeCount,SummonCount(),tracked,parked,ActiveOrdinary(),activeBoss,gEpoch,gRollback);
    ReplyToCommand(client,"[Shelter] revision=%d materials=%d recipes=%d global_build_cap=%d red_constructed=%d red_line_deliveries=%d",
        gConfigRevision,gMaterialCount,gRecipeCount,gSummonLimit,gRedConstructed,gRedDeliveries);
    ReplyToCommand(client,"[ShelterSupport] native_pool=%d day_target=%d night_target=%d restoration_pulses=%d restored_targets=%d beacon_markers=%d",
        gNativePoolSize,DayTarget(),NightTarget(),gRestorationPulseCount,gRestorationTargetCount,gBeaconMarkerCount);
    ReplyToCommand(client,"[ShelterPowerMode] enabled=%d config_overload=%d override=%d effective_overload=%d (override -1=config, 0=priority, 1=overload)",
        gPowerEnabled,gPowerOverloadPenalty,gPowerOverloadCvar==null ? -1 : gPowerOverloadCvar.IntValue,PowerOverloadPenaltyEnabled());
    for(int i=0;i<gCoreCount;i++){int b=gCores[i].building;if(b>=0)ReplyToCommand(client,"%T","CmdStatusBase",client,gCores[i].id,gBuildings[b].level,gBuildings[b].hp,gBuildings[b].maxHP,gBuildings[b].alive,gBuildings[b].ref);}
    for(int i=0;i<gBossCount;i++)ReplyToCommand(client,"%T","CmdStatusBoss",client,gBosses[i].id,gBosses[i].savedHP,gBosses[i].awakened,gBosses[i].defeated,gBosses[i].boundThisRound,gBosses[i].positionedHP);
    for(int i=0;i<gBossCount;i++)ReplyToCommand(client,"[ShelterBossSpawn] %s point=boss%d origin=%.3f %.3f %.3f angles=%.1f %.1f %.1f",
        gBosses[i].id,gBosses[i].spawnPoint+1,gBosses[i].origin[0],gBosses[i].origin[1],gBosses[i].origin[2],gBosses[i].angles[0],gBosses[i].angles[1],gBosses[i].angles[2]);
    for(int c=0;c<gCoreCount;c++)ReplyToCommand(client,"%T","CmdPowerStatus",client,gCores[c].id,gGridProduction[c],gGridDemand[c],gGridAllocated[c],gGridOffline[c]);
    return Plugin_Handled;
}

public Action PhaseCommand(int client,int args)
{
    if(!Running()){ReplyToCommand(client,"%T","CmdNativeNotStarted",client);return Plugin_Handled;}
    char phase[16];GetCmdArg(1,phase,sizeof(phase));
    if(StrEqual(phase,"night"))StartNight();else if(StrEqual(phase,"day"))StartDay(false);
    else ReplyToCommand(client,"%T","CmdPhaseUsage",client);return Plugin_Handled;
}

public Action DayCommand(int client,int args)
{
    char day[16];GetCmdArg(1,day,sizeof(day));int value=StringToInt(day);
    if(Running() && value>=1 && value<=100000){gDay=value;StartDay(false);}else ReplyToCommand(client,"%T","CmdDayUsage",client);return Plugin_Handled;
}

public Action GrantCommand(int client,int args)
{
    char material[32],amount[16];GetCmdArg(1,material,sizeof(material));GetCmdArg(2,amount,sizeof(amount));
    int m=FindMaterial(material),count=StringToInt(amount),target=client;
    if(args>=3)
    {
        char selection[64];GetCmdArg(3,selection,sizeof(selection));target=FindTarget(client,selection,false,false);
        if(target<1)return Plugin_Handled;
    }
    else if(!LiveHuman(target))
    {
        target=0;for(int i=1;i<=MaxClients;i++)if(LiveHuman(i)){target=i;break;}
    }
    if(gEnabled && m>=0 && count>0 && count<=1000000 && LiveHuman(target))
    {
        char targetName[MAX_NAME_LENGTH],materialName[64];GetClientName(target,targetName,sizeof(targetName));
        TranslateLabel(client,gMaterials[m].label,materialName,sizeof(materialName));
        ReplyToCommand(client,"%T","CmdGrantResult",client,Grant(m,count,target),materialName,targetName);
    }
    else ReplyToCommand(client,"%T","CmdGrantUsage",client);return Plugin_Handled;
}

public Action FailCommand(int client,int args)
{
    if(Running())QueueOutcome(false);return Plugin_Handled;
}

public Action ReloadCommand(int client,int args)
{
    if(gSnapshot || gRoundPrepared){ReplyToCommand(client,"%T","CmdReloadBlocked",client);return Plugin_Handled;}
    BootMode();ReplyToCommand(client,"%T","CmdReloadResult",client,gEnabled);return Plugin_Handled;
}

// Outdoor E is navigation only. Existing object, tower and base interactions
// keep their priority; both destinations retain their own purchase/build checks.
Menu gQuickMenuHandle[MAXPLAYERS+1];
int gQuickMenuNonce[MAXPLAYERS+1];
int gQuickMenuSerial[MAXPLAYERS+1],gQuickMenuEpoch[MAXPLAYERS+1],gQuickMenuRevision[MAXPLAYERS+1];
int gQuickMenuSkillsSession[MAXPLAYERS+1],gQuickMenuSkillsRevision[MAXPLAYERS+1];

void ResetQuickMenu(int client)
{
    if(client<1 || client>MaxClients)return;
    gQuickMenuHandle[client]=null;gQuickMenuNonce[client]++;
    gQuickMenuSerial[client]=0;gQuickMenuEpoch[client]=0;gQuickMenuRevision[client]=0;
    gQuickMenuSkillsSession[client]=0;gQuickMenuSkillsRevision[client]=0;
}

void ResetAllQuickMenus()
{
    for(int client=1;client<=MaxClients;client++)ResetQuickMenu(client);
}

public void QuickMenuRoundReset(Event event,const char[] name,bool dontBroadcast)
{
    ResetAllQuickMenus();
    ResetAllCraftingMenus();
}

public void QuickMenuPlayerReset(Event event,const char[] name,bool dontBroadcast)
{
    int client=GetClientOfUserId(event.GetInt("userid"));
    ResetQuickMenu(client);ResetCraftingMenu(client);
}

bool QuickMenuAvailable(int client)
{
    if(!LiveHuman(client) || IsFakeClient(client) || !Running())return false;
    float origin[3];GetClientAbsOrigin(client,origin);
    for(int axis=0;axis<3;axis++)
        if((view_as<int>(origin[axis])&0x7f800000)==0x7f800000)return false;
    return SafeCoreAt(origin)<0 && BuildCoreAt(origin)<0;
}

bool ParseQuickMenuToken(const char[] token,int values[7])
{
    // Seven nonnegative cell fields, at most ten digits each. Reject overlong
    // fields before ExplodeString can turn a truncated prefix into another ID.
    if(strlen(token)>76)return false;
    char parts[8][64];
    if(ExplodeString(token,":",parts,sizeof(parts),sizeof(parts[]))!=7)return false;
    for(int i=0;i<7;i++)if(!ParseLevelInteger(parts[i],values[i]))return false;
    return values[6]==0 || values[6]==1;
}

bool QuickMenuSessionMatches(int client,const int values[7])
{
    return client>0 && client<=MaxClients && values[0]==gQuickMenuNonce[client]
        && values[1]==gQuickMenuSerial[client] && values[2]==gQuickMenuEpoch[client]
        && values[3]==gQuickMenuRevision[client] && values[4]==gQuickMenuSkillsSession[client]
        && values[5]==gQuickMenuSkillsRevision[client];
}

bool ValidateQuickMenu(int client,Menu menu,const char[] token,int &operation)
{
    int values[7];
    if(!ParseQuickMenuToken(token,values) || !QuickMenuSessionMatches(client,values)
        || gQuickMenuHandle[client]!=menu || !QuickMenuAvailable(client)
        || values[1]!=GetClientSerial(client) || values[2]!=gEpoch || values[3]!=gConfigRevision
        || values[4]!=gSkillsMapSession || values[5]!=gSkillsConfigRevision)return false;
    operation=values[6];
    return operation==0 || SkillsActor(client);
}

void ShowQuickMenu(int client)
{
    if(!QuickMenuAvailable(client))return;
    ResetQuickMenu(client);
    gQuickMenuSerial[client]=GetClientSerial(client);gQuickMenuEpoch[client]=gEpoch;
    gQuickMenuRevision[client]=gConfigRevision;gQuickMenuSkillsSession[client]=gSkillsMapSession;
    gQuickMenuSkillsRevision[client]=gSkillsConfigRevision;
    Menu menu=new Menu(QuickMenuHandler);gQuickMenuHandle[client]=menu;
    menu.SetTitle("快捷菜单");
    for(int operation=0;operation<2;operation++)
    {
        char token[128];
        Format(token,sizeof(token),"%d:%d:%d:%d:%d:%d:%d",gQuickMenuNonce[client],gQuickMenuSerial[client],
            gQuickMenuEpoch[client],gQuickMenuRevision[client],gQuickMenuSkillsSession[client],gQuickMenuSkillsRevision[client],operation);
        menu.AddItem(token,operation==0 ? "建造菜单" : "角色技能菜单",
            operation==0 || SkillsActor(client) ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
    }
    menu.Display(client,30);
}

public int QuickMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)
    {
        // Ending an older menu must never invalidate its replacement.
        for(int c=1;c<=MaxClients;c++)if(gQuickMenuHandle[c]==menu)ResetQuickMenu(c);
        delete menu;return 0;
    }
    if(client<1 || client>MaxClients || gQuickMenuHandle[client]!=menu)return 0;
    if(action==MenuAction_Cancel){ResetQuickMenu(client);return 0;}
    if(action!=MenuAction_Select)return 0;
    char token[128];int operation;
    if(item<0 || item>=menu.ItemCount || !menu.GetItem(item,token,sizeof(token))
        || !ValidateQuickMenu(client,menu,token,operation))
    {
        ResetQuickMenu(client);
        if(IsClientInGame(client))Tell(client,"快捷菜单已过期，请在基地和建造范围外重新按 E。");
        return 0;
    }
    // Consume the quote before Display synchronously cancels the old handle.
    ResetQuickMenu(client);
    if(operation==0)ShowBuildCategories(client);
    else ShowSkills(client,-1);
    return 0;
}

public Action OnPlayerRunCmd(int client,int &buttons,int &impulse,float vel[3],float angles[3],int &weapon,int &subtype,int &cmdnum,int &tickcount,int &seed,int mouse[2])
{
    int previousButtons=gLastButtons[client];gLastButtons[client]=buttons;
    if(HandleConstructionCancelInput(client,buttons,previousButtons))return Plugin_Changed;
    bool rising=(buttons&IN_USE)!=0 && (previousButtons&IN_USE)==0;
    if(!rising || !Running() || !LiveHuman(client) || GetGameTime()<gNextUse[client])return Plugin_Continue;
    int b=FindBuilding(AimedEntity(client,gUseDistance));
    if(b>=0 && gBuildings[b].constructing)
    {gNextUse[client]=GetGameTime()+gUseCooldown;Tell(client,"%t","ConstructionIncomplete");return Plugin_Continue;}
    if(b>=0 && gBuildings[b].kind==Kind_Beacon)
    {
        gNextUse[client]=GetGameTime()+gUseCooldown;ShowBeaconMenu(client,b);return Plugin_Continue;
    }
    if(b>=0 && gBuildings[b].kind==Kind_Workbench)
    {
        gNextUse[client]=GetGameTime()+gUseCooldown;ShowCraftingMenu(client,b);return Plugin_Continue;
    }
    if(b>=0 && gBuildings[b].kind!=Kind_Base)
    {
        gNextUse[client]=GetGameTime()+gUseCooldown;ShowDefenseMenu(client,b);return Plugin_Continue;
    }
    if(b>=0 && IsShelterAdmin(client))
    {
        gNextUse[client]=GetGameTime()+gUseCooldown;ShowAdminBuildingMenu(client,b,true);return Plugin_Continue;
    }
    // A completed tower can be selected from its own ground circle even when
    // its visible body is beyond ordinary close facility interaction distance.
    int aimedTower=WatchtowerAimedBuilding(client);
    if(aimedTower>=0){gNextUse[client]=GetGameTime()+gUseCooldown;ShowDefenseMenu(client,aimedTower);return Plugin_Continue;}
    // A real platform beneath the feet is a tower-only interaction source.
    // Explicitly aimed facilities above already retained their old priority.
    int tower=WatchtowerUnderPlayer(client);
    if(tower>=0){gNextUse[client]=GetGameTime()+gUseCooldown;ShowDefenseMenu(client,tower);return Plugin_Continue;}
    float origin[3];GetClientAbsOrigin(client,origin);int core=SafeCoreAt(origin);
    if(core>=0){gNextUse[client]=GetGameTime()+gUseCooldown;ShowBaseMenu(client,core);}
    else if(BuildCoreAt(origin)>=0){gNextUse[client]=GetGameTime()+gUseCooldown;ShowBuildCategories(client);}
    else {gNextUse[client]=GetGameTime()+gUseCooldown;ShowQuickMenu(client);}
    return Plugin_Continue;
}

int gDefenseMenuNonce[MAXPLAYERS+1];
Menu gDefenseMenuHandle[MAXPLAYERS+1];

void ResetDefenseMenuClient(int client)
{
    gDefenseMenuNonce[client]++;gDefenseMenuHandle[client]=null;
}

bool WatchtowerMenuRefreshCurrent(int client,int nonce)
{
    return client>0 && client<=MaxClients && gDefenseMenuNonce[client]==nonce
        && gDefenseMenuHandle[client]==null && GetClientMenu(client)==MenuSource_None;
}

void EndDefenseMenu(Menu menu)
{
    for(int client=1;client<=MaxClients;client++)
        if(gDefenseMenuHandle[client]==menu)ResetDefenseMenuClient(client);
    delete menu;
}

bool DefenseMenuCurrent(Menu menu,int client)
{
    return client>0 && client<=MaxClients && gDefenseMenuHandle[client]==menu;
}

bool CanViewDefense(int client,int building)
{
    return LiveHuman(client) && Running() && building>=0 && building<MAX_BUILDINGS
        && gBuildings[building].used && gBuildings[building].alive && gBuildings[building].kind!=Kind_Base
        && Resolve(gBuildings[building].ref)>0 && BuildingInUseRange(client,building);
}

void DefenseMenuToken(int client,int building,int operation,char[] token,int length)
{
    Format(token,length,"%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",gEpoch,GetClientSerial(client),gDefenseMenuNonce[client],
        building,gBuildings[building].uid,gBuildings[building].level,operation,gBuildings[building].generation,gConfigRevision,gBuildings[building].ref);
}

bool FenceRotationContinuationOperation(int operation)
{
    // 13 returns an already selected rotation to management; 14/15 change
    // axes from that continuation page. Initial E axis entries remain 6/7.
    return (operation>=8 && operation<=11) || (operation>=13 && operation<=15);
}

// Engine EntRefs use every signed 32-bit cell value; level/config integers
// intentionally retain their smaller economic bound. Accumulate negatively so
// INT_MIN is accepted without ever negating it or overflowing an intermediate.
bool ParseDefenseEntityRefToken(const char[] text,int &value)
{
    int length=strlen(text),start;bool negative;
    if(length<1)return false;
    if(text[0]=='-'){negative=true;start=1;}
    if(length==start || length-start>10)return false;
    int limit=negative ? (-2147483647-1) : -2147483647;
    int minimumBeforeMultiply=limit/10,result;
    for(int i=start;i<length;i++)
    {
        if(text[i]<'0' || text[i]>'9')return false;
        int digit=text[i]-'0';
        if(result<minimumBeforeMultiply)return false;
        result*=10;
        if(result<limit+digit)return false;
        result-=digit;
    }
    value=negative ? result : -result;return true;
}

bool ValidateDefenseMenuToken(int client,const char[] token,int &building,int &operation,bool requireAim=true)
{
    char fields[11][24];int values[10];if(ExplodeString(token,":",fields,11,24)!=10 || !LiveHuman(client) || !Running())return false;
    for(int i=0;i<9;i++)if(!ParseLevelInteger(fields[i],values[i]))return false;
    if(!ParseDefenseEntityRefToken(fields[9],values[9]))return false;
    if(values[0]!=gEpoch || values[1]!=GetClientSerial(client) || values[2]!=gDefenseMenuNonce[client] || values[8]!=gConfigRevision)return false;
    building=values[3];operation=values[6];
    if(!DefenseMenuBuildingAvailable(client,building) || (requireAim && !(FenceRotationContinuationOperation(operation)
        ? FenceRotationInRange(client,building) : BuildingInUseRange(client,building)))
        || gBuildings[building].uid!=values[4] || gBuildings[building].generation!=values[7]
        || gBuildings[building].level!=values[5] || gBuildings[building].ref!=values[9] || operation<0 || operation>21)return false;
    if((operation==2 || operation==3 || operation==4) && !CanManageBuilding(client,building))return false;
    if(operation==5 && !IsShelterAdmin(client))return false;
    if(((operation>=6 && operation<=11) || (operation>=13 && operation<=15))
        && (!CanContinueFenceRotationMenu(client,building) || !CanManageBuilding(client,building)))return false;
    if(operation==12 && (gBuildings[building].kind!=Kind_Fence || BuildingOwnerOnline(building)>0 || IsFakeClient(client)))return false;
    if(operation==16 && (gBuildings[building].kind!=Kind_Beacon || IsFakeClient(client)))return false;
    if((operation==17 || operation==18) && (gBuildings[building].kind!=Kind_Watchtower || !WatchtowerActor(client)))return false;
    if((operation==19 || operation==20) && !CanSetBuildingLightManualState(client,building,operation==19))return false;
    if(operation==21 && gBuildings[building].kind!=Kind_Workbench)return false;
    gDefenseMenuNonce[client]++;return true;
}

bool DefenseMenuBuildingAvailable(int client,int building)
{
    return LiveHuman(client) && Running() && building>=0 && building<MAX_BUILDINGS && gBuildings[building].used
        && gBuildings[building].alive && gBuildings[building].kind!=Kind_Base && Resolve(gBuildings[building].ref)>0;
}

bool CanContinueFenceRotationMenu(int client,int building)
{
    return DefenseMenuBuildingAvailable(client,building) && !IsFakeClient(client) && BuildingRotationKind(gBuildings[building].kind)
        && gBuildings[building].configRevision==gConfigRevision && CanManageBuilding(client,building);
}

bool BuildingRotationKind(BuildingKind kind)
{
    return kind==Kind_Fence || kind==Kind_Light;
}

float NormalizeFenceAngle(float angle)
{
    if(!(angle>=-1000000.0 && angle<=1000000.0))return 0.0;
    angle-=360.0*float(RoundToFloor((angle+180.0)/360.0));
    if(angle>=180.0)angle-=360.0;if(angle< -180.0)angle+=360.0;return angle;
}

bool FenceRotationInRange(int client,int building)
{
    if(!CanContinueFenceRotationMenu(client,building))return false;
    int body=Resolve(gBuildings[building].ref);float eye[3],origin[3],angles[3],local[3],mins[3],maxs[3];GetClientEyePosition(client,eye);
    GetEntPropVector(body,Prop_Data,"m_vecOrigin",origin);GetEntPropVector(body,Prop_Data,"m_angRotation",angles);
    GetEntPropVector(body,Prop_Send,"m_vecMins",mins);GetEntPropVector(body,Prop_Send,"m_vecMaxs",maxs);LocalPoint(eye,origin,angles,local);
    float distanceSquared;
    for(int axis=0;axis<3;axis++)
    {
        float nearest=local[axis]<mins[axis] ? mins[axis] : (local[axis]>maxs[axis] ? maxs[axis] : local[axis]);
        float delta=local[axis]-nearest;distanceSquared+=delta*delta;
    }
    return distanceSquared<=gUseDistance*gUseDistance;
}

bool RotateFenceBuilding(int client,int building,int axis,int direction)
{
    if(!CanContinueFenceRotationMenu(client,building) || !FenceRotationInRange(client,building)
        || (axis!=0 && axis!=1) || (direction!=1 && direction!=-1))return false;
    int body=Resolve(gBuildings[building].ref),ref=gBuildings[building].ref,uid=gBuildings[building].uid;
    float nextAngles[3];nextAngles=gBuildings[building].angles;nextAngles[axis]=NormalizeFenceAngle(nextAngles[axis]+float(direction)*10.0);
    TeleportEntity(body,NULL_VECTOR,nextAngles,NULL_VECTOR);
    if(Resolve(ref)!=body || !gBuildings[building].alive || gBuildings[building].uid!=uid)return false;
    gBuildings[building].angles=nextAngles;
    float mins[3],maxs[3];ModelWorldBounds(body,gBuildings[building].origin,nextAngles,mins,maxs);
    for(int a=0;a<3;a++){gBuildings[building].restoreMins[a]=mins[a]-gBuildings[building].origin[a];gBuildings[building].restoreMaxs[a]=maxs[a]-gBuildings[building].origin[a];}
    gBuildings[building].boundsKnown=true;
    if(gBuildings[building].kind==Kind_Light)SyncBuildingLightTransform(building);
    return true;
}

void ShowFenceRotationMenu(int client,int building,int axis)
{
    // Keep the selected object/axis after a rotation. Following steps check its
    // actual rotated OBB distance without requiring another thin-face ray hit.
    if(!CanContinueFenceRotationMenu(client,building) || (axis!=0 && axis!=1))return;
    gDefenseMenuNonce[client]++;
    char name[128],axisName[64],title[512];BuildingDisplayName(building,name,sizeof(name),client);
    TranslateLabel(client,axis==0 ? "DefenseRotateVertical" : "DefenseRotateHorizontal",axisName,sizeof(axisName));
    Format(title,sizeof(title),"%T","DefenseRotationTitle",client,name,axisName,gBuildings[building].angles[0],gBuildings[building].angles[1]);
    Menu menu=new Menu(FenceRotationMenuHandler);menu.SetTitle("%s",title);
    AddDefenseMenuItem(menu,client,building,axis==0 ? 8 : 10,"DefenseRotatePlus");
    AddDefenseMenuItem(menu,client,building,axis==0 ? 9 : 11,"DefenseRotateMinus");
    AddDefenseMenuItem(menu,client,building,13,"DefenseBack");gDefenseMenuHandle[client]=menu;menu.Display(client,MENU_TIME_FOREVER);
}

public int FenceRotationMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)EndDefenseMenu(menu);
    else if(action==MenuAction_Cancel){if(DefenseMenuCurrent(menu,client))ResetDefenseMenuClient(client);}
    else if(action==MenuAction_Select)
    {
        if(!DefenseMenuCurrent(menu,client))return 0;
        char token[128];int building,operation;menu.GetItem(item,token,sizeof(token));
        if(!ValidateDefenseMenuToken(client,token,building,operation))
        {
            // A genuine current token may keep its menu while the player moves nearer.
            // This path never rotates and still consumes its nonce once.
            if(ValidateDefenseMenuToken(client,token,building,operation,false) && operation>=8 && operation<=11)
            {Tell(client,"%T","DefenseRotationAimAgain",client);ShowFenceRotationMenu(client,building,operation<10 ? 0 : 1);}
            else if(LiveHuman(client))Tell(client,"%T","DefenseExpired",client);
            return 0;
        }
        if(operation==13){ShowDefenseMenu(client,building,true);return 0;}
        if(operation<8 || operation>11)return 0;
        int axis=operation<10 ? 0 : 1,direction=(operation==8 || operation==10) ? 1 : -1;
        if(!RotateFenceBuilding(client,building,axis,direction))Tell(client,"%T","DefenseRotationAimAgain",client);
        ShowFenceRotationMenu(client,building,axis);
    }
    return 0;
}

void AddDefenseMenuItem(Menu menu,int client,int building,int operation,const char[] phrase,bool enabled=true)
{
    char token[128],line[192];DefenseMenuToken(client,building,operation,token,sizeof(token));
    TranslateLabel(client,phrase,line,sizeof(line));menu.AddItem(token,line,enabled ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
}

void AppendBuildingCapacityInfo(int client,int building,char[] text,int length,int level=0)
{
    int core=gBuildings[building].kind==Kind_Base ? gBuildings[building].core : gBuildings[building].buildCore;
    AppendBuildCapacityInfo(client,core,text,length);
    if(SupportMenuKind(gBuildings[building].kind))
    {
        int limit=level>0 ? SupportBuildLimit(gBuildings[building].file,gBuildings[building].type,level) : gBuildings[building].maxPerBase;
        AppendSupportMenuInfo(client,core,gBuildings[building].kind,limit,text,length);
    }
    if(gBuildings[building].kind==Kind_Expander)
    {
        bool effective=EffectiveExpander(building);char state[64],line[192];
        TranslateLabel(client,effective ? "BuildExpanderEffective" : "BuildExpanderInactive",state,sizeof(state));
        Format(line,sizeof(line),"%T","BuildExpanderContributionInfo",client,state,
            effective ? gBuildings[building].buildRangeBonus : 0.0,effective ? gBuildings[building].buildLimitBonus : 0);
        StrCat(text,length,line);
    }
}

void ShowDefenseMenu(int client,int building,bool rotationContinuation=false)
{
    if(rotationContinuation ? !FenceRotationInRange(client,building) : !CanViewDefense(client,building))return;
    gDefenseMenuNonce[client]++;
    char name[64],title[768],access[128];BuildingDisplayName(building,name,sizeof(name),client);
    bool manage=CanManageBuilding(client,building);
    TranslateLabel(client,manage ? "DefenseManageAllowed" : "DefenseViewOnly",access,sizeof(access));
    Format(title,sizeof(title),"%T","PowerFacilityTitle",client,name,gBuildings[building].level,gBuildings[building].hp,gBuildings[building].maxHP,access);
    AppendBuildingPowerInfo(client,building,gBuildings[building].level,title,sizeof(title));
    AppendBuildingCapacityInfo(client,building,title,sizeof(title));
    Menu menu=new Menu(DefenseMenuHandler);menu.SetTitle("%s",title);
    int cost[MATERIALS],nextHP;bool upgrade=ReadBaseUpgrade(building,cost,nextHP);
    AddDefenseMenuItem(menu,client,building,1,upgrade ? "DefenseUpgradeView" : "DefenseUpgradeUnavailable",upgrade);
    if(gBuildings[building].kind==Kind_Workbench)AddDefenseMenuItem(menu,client,building,21,"CraftingOpen");
    if(gBuildings[building].kind==Kind_Light && BuildingOperational(building))
    {
        bool manualOff=!gBuildings[building].lightManualOff;
        AddDefenseMenuItem(menu,client,building,manualOff ? 19 : 20,manualOff ? "LightManualSwitchOff" : "LightManualSwitchOn",
            CanSetBuildingLightManualState(client,building,manualOff));
    }
    if(gBuildings[building].kind==Kind_Watchtower)
    {
        bool down=WatchtowerOnPlatform(client,building);
        AddDefenseMenuItem(menu,client,building,down ? 18 : 17,down ? "WatchtowerTravelDown" : "WatchtowerTravelUp",WatchtowerTravelAllowed(client,building,down));
    }
    AddDefenseMenuItem(menu,client,building,3,"DefenseDismantleView",manage);
    if(BuildingRotationKind(gBuildings[building].kind))
    {
        AddDefenseMenuItem(menu,client,building,rotationContinuation ? 14 : 6,"DefenseRotateVertical",manage);
        AddDefenseMenuItem(menu,client,building,rotationContinuation ? 15 : 7,"DefenseRotateHorizontal",manage);
    }
    if(gBuildings[building].kind==Kind_Fence && BuildingOwnerOnline(building)<1)AddDefenseMenuItem(menu,client,building,12,"DefenseClaimStructure");
    if(gBuildings[building].kind==Kind_Beacon)AddDefenseMenuItem(menu,client,building,16,"BeaconOpenDestinations");
    if(IsShelterAdmin(client))AddDefenseMenuItem(menu,client,building,5,"AdminBuildingControls");
    char profile[128];BuildingProfilePath(building,profile,sizeof(profile));
    AddProfileLevelItems(menu,client,gBuildings[building].file,gBuildings[building].kind,profile,gBuildings[building].level);
    gDefenseMenuHandle[client]=menu;menu.Display(client,30);
}

void ShowDefenseUpgradeMenu(int client,int building)
{
    if(!CanViewDefense(client,building))return;
    int cost[MATERIALS],nextHP;if(!ReadBaseUpgrade(building,cost,nextHP)){ShowDefenseMenu(client,building);return;}
    gDefenseMenuNonce[client]++;
    char name[64],title[768],line[192];BuildingDisplayName(building,name,sizeof(name),client);
    Format(title,sizeof(title),"%T","MenuUpgradeTitle",client,name,gBuildings[building].level,
        gBuildings[building].level+1,gBuildings[building].maxHP,nextHP);
    AppendBuildingPowerInfo(client,building,gBuildings[building].level+1,title,sizeof(title));
    AppendBuildingCapacityInfo(client,building,title,sizeof(title),gBuildings[building].level+1);
    int required;bool progression=BuildingBaseLevelAllowed(building,gBuildings[building].level+1,required);
    if(required>1 || !progression)
    {
        Format(line,sizeof(line),"%T","DefenseBaseRequirement",client,ProgressionBaseLevel(),required);StrCat(title,sizeof(title),line);
    }
    Menu menu=new Menu(DefenseMenuHandler);menu.SetTitle("%s",title);
    bool manage=CanManageBuilding(client,building),enough=HasBaseUpgradeMaterials(cost,client);
    AddDefenseMenuItem(menu,client,building,2,!manage ? "DefenseViewOnly" : (!progression ? "DefenseBaseLevelLocked" : (enough ? "MenuUpgradeConfirm" : "MenuUpgradeInsufficient")),manage && enough && progression);
    AddDefenseMenuItem(menu,client,building,0,"DefenseBack");AddRecipeCostItems(menu,client,cost);
    char profile[128];BuildingProfilePath(building,profile,sizeof(profile));
    AddProfileLevelItems(menu,client,gBuildings[building].file,gBuildings[building].kind,profile,gBuildings[building].level+1);gDefenseMenuHandle[client]=menu;menu.Display(client,30);
}

void ShowDefenseDismantleMenu(int client,int building)
{
    if(!CanViewDefense(client,building) || !CanManageBuilding(client,building))return;
    gDefenseMenuNonce[client]++;
    char name[64],title[768],line[192];BuildingDisplayName(building,name,sizeof(name),client);
    Format(title,sizeof(title),"%T","DefenseDismantleTitle",client,name);
    int refund[MATERIALS];BuildingRefund(building,refund);
    Menu menu=new Menu(DefenseMenuHandler);menu.SetTitle("%s",title);
    AddDefenseMenuItem(menu,client,building,4,"DefenseDismantleConfirm");
    AddDefenseMenuItem(menu,client,building,0,"DefenseBack");
    for(int m=0;m<gMaterialCount;m++)if(refund[m]>0)
    {
        char material[64];TranslateLabel(client,gMaterials[m].label,material,sizeof(material));
        Format(line,sizeof(line),"%T","DefenseRefundLine",client,material,refund[m]);menu.AddItem("",line,ITEMDRAW_DISABLED);
    }
    gDefenseMenuHandle[client]=menu;menu.Display(client,30);
}

public int DefenseMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)EndDefenseMenu(menu);
    else if(action==MenuAction_Cancel){if(DefenseMenuCurrent(menu,client))ResetDefenseMenuClient(client);}
    else if(action==MenuAction_Select)
    {
        if(!DefenseMenuCurrent(menu,client))return 0;
        char token[128];int building,operation;menu.GetItem(item,token,sizeof(token));
        if(!ValidateDefenseMenuToken(client,token,building,operation))
        {
            if(LiveHuman(client))Tell(client,"%T","DefenseExpired",client);return 0;
        }
        switch(operation)
        {
            case 0: ShowDefenseMenu(client,building);
            case 1: ShowDefenseUpgradeMenu(client,building);
            case 2:
            {
                if(!CanManageBuilding(client,building) || !BuildingInUseRange(client,building) || !TryUpgrade(client,building))
                    Tell(client,"%T","MenuUpgradeChanged",client);
                ShowDefenseMenu(client,building);
            }
            case 3: ShowDefenseDismantleMenu(client,building);
            case 4:
            {
                if(!CanManageBuilding(client,building) || !BuildingInUseRange(client,building) || !DismantleBuilding(client,building,IsShelterAdmin(client)))
                {
                    Tell(client,"%T","DefenseDismantleFailed",client);ShowDefenseMenu(client,building);
                }
                else HudFeedback(client,"%T","DefenseDismantled",client);
            }
            case 5: ShowAdminBuildingMenu(client,building,true);
            case 6: ShowFenceRotationMenu(client,building,0);
            case 7: ShowFenceRotationMenu(client,building,1);
            case 14: ShowFenceRotationMenu(client,building,0);
            case 15: ShowFenceRotationMenu(client,building,1);
            case 16: ShowBeaconMenu(client,building);
            case 21: ShowCraftingMenu(client,building);
            case 17,18:
            {
                ResetDefenseMenuClient(client);
                QueueWatchtowerMenuRefresh(client,building,gDefenseMenuNonce[client]);
                bool down=operation==18;
                if(TryWatchtowerTravel(client,building,down))Tell(client,"%t",down ? "WatchtowerTravelDownSuccess" : "WatchtowerTravelUpSuccess");
                else ReportWatchtowerTravelFailure(client);
            }
            case 19,20:
            {
                bool manualOff=operation==19;
                bool changed=SetBuildingLightManualState(client,building,manualOff);
                Tell(client,"%t",changed ? (manualOff ? "LightManualClosed" : "LightManualAutomatic") : "LightManualSwitchFailed");
                ShowDefenseMenu(client,building);
            }
            case 12:
            {
                Tell(client,"%T",TakeBuildingOwnership(client,building) ? "DefenseClaimed" : "DefenseClaimFailed",client);
                ShowDefenseMenu(client,building);
            }
        }
    }
    return 0;
}

int gBaseMenuNonce[MAXPLAYERS+1];

bool CanManageCore(int client,int core)
{
    if(!LiveHuman(client) || !Running() || core<0 || core>=gCoreCount)return false;
    float origin[3];GetClientAbsOrigin(client,origin);
    return SafeCoreAt(origin)==core;
}

void BaseMenuToken(int client,int core,int action,char[] token,int length,const char[] materialID="-",int quantity=0)
{
    int building=gCores[core].building;
    Format(token,length,"%d:%d:%d:%d:%d:%d:%d:%d:%s:%d",gEpoch,GetClientSerial(client),core,
        gBuildings[building].uid,gBuildings[building].level,gBaseMenuNonce[client],action,gConfigRevision,materialID,quantity);
}

bool ValidateBaseMenuToken(int client,const char[] token,int &core,int &operation,int &material,int &quantity)
{
    char fields[11][40];int values[8];if(ExplodeString(token,":",fields,11,40)!=10)return false;
    for(int i=0;i<8;i++)if(!ParseLevelInteger(fields[i],values[i]))return false;
    if(!LiveHuman(client) || !Running() || values[0]!=gEpoch || values[1]!=GetClientSerial(client)
        || values[5]!=gBaseMenuNonce[client] || values[7]!=gConfigRevision)return false;
    core=values[2];if(!CanManageCore(client,core))return false;
    int building=gCores[core].building;
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used
        || values[3]!=gBuildings[building].uid || values[4]!=gBuildings[building].level)return false;
    operation=values[6];material=FindMaterial(fields[8]);
    if(!ParseLevelInteger(fields[9],quantity,true) || operation<0 || operation>8)return false;
    if(operation>=5 && operation<=7 && material<0)return false;
    if((operation==6 || operation==7) && quantity!=1 && quantity!=10 && quantity!=-1)return false;
    // A menu selection is a single-use transaction, including failures and navigation.
    gBaseMenuNonce[client]++;
    return true;
}

bool ReadBaseUpgrade(int building,int cost[MATERIALS],int &nextMaxHP)
{
    if(gBuildings[building].constructing)return false;
    return ReadBuildingLevelQuote(building,gBuildings[building].level+1,cost,nextMaxHP);
}

bool HasBaseUpgradeMaterials(const int cost[MATERIALS],int client)
{
    if(!LiveHuman(client))return false;
    for(int m=0;m<gMaterialCount;m++)if(cost[m]<0 || gPlayerInventory[client][m]<cost[m])return false;
    return true;
}

void ShowBaseMenu(int client,int core)
{
    if(!CanManageCore(client,core))return;
    int building=gCores[core].building;
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used)return;
    gBaseMenuNonce[client]++;
    char name[64],title[768],token[128],state[128],line[192];CoreDisplayName(core,name,sizeof(name),client);
    TranslateLabel(client,gBuildings[building].alive ? "MenuBaseAlive" : "MenuBaseDestroyed",state,sizeof(state));
    Format(title,sizeof(title),"%T","MenuBaseTitle",client,name,gBuildings[building].level,
        gBuildings[building].maxHP,gBuildings[building].hp,gBuildings[building].maxHP,state);
    AppendGridInfo(client,core,title,sizeof(title));
    AppendBuildingCapacityInfo(client,building,title,sizeof(title));
    Menu menu=new Menu(BaseMenuHandler);menu.SetTitle("%s",title);
    int cost[MATERIALS],nextMaxHP;bool upgrade=gBuildings[building].alive && ReadBaseUpgrade(building,cost,nextMaxHP);
    BaseMenuToken(client,core,0,token,sizeof(token));
    TranslateLabel(client,upgrade ? "MenuUpgradeView" : "MenuUpgradeUnavailable",line,sizeof(line));
    menu.AddItem(token,line,upgrade ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
    BaseMenuToken(client,core,4,token,sizeof(token));TranslateLabel(client,"MenuStorage",line,sizeof(line));menu.AddItem(token,line);
    BaseMenuToken(client,core,3,token,sizeof(token));TranslateLabel(client,"MenuBuild",line,sizeof(line));menu.AddItem(token,line);
    BaseMenuToken(client,core,8,token,sizeof(token));menu.AddItem(token,"玩家技能");
    menu.Display(client,30);
}

void ShowBaseUpgradeMenu(int client,int core)
{
    if(!CanManageCore(client,core))return;
    int building=gCores[core].building;
    if(building<0 || !gBuildings[building].used || !gBuildings[building].alive)return;
    int cost[MATERIALS],nextMaxHP;
    if(!ReadBaseUpgrade(building,cost,nextMaxHP)){ShowBaseMenu(client,core);return;}
    gBaseMenuNonce[client]++;
    char name[64],title[768],line[192],token[128];CoreDisplayName(core,name,sizeof(name),client);
    Format(title,sizeof(title),"%T","MenuUpgradeTitle",client,name,
        gBuildings[building].level,gBuildings[building].level+1,gBuildings[building].maxHP,nextMaxHP);
    AppendBuildingCapacityInfo(client,building,title,sizeof(title));
    Menu menu=new Menu(BaseMenuHandler);menu.SetTitle("%s",title);bool enough=HasBaseUpgradeMaterials(cost,client);
    BaseMenuToken(client,core,1,token,sizeof(token));
    TranslateLabel(client,enough ? "MenuUpgradeConfirm" : "MenuUpgradeInsufficient",line,sizeof(line));
    menu.AddItem(token,line,enough ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
    BaseMenuToken(client,core,2,token,sizeof(token));TranslateLabel(client,"MenuBackBase",line,sizeof(line));menu.AddItem(token,line);
    AddRecipeCostItems(menu,client,cost);
    char profile[128];BuildingProfilePath(building,profile,sizeof(profile));
    AddProfileLevelItems(menu,client,gBuildings[building].file,gBuildings[building].kind,profile,gBuildings[building].level+1);
    menu.Display(client,30);
}

void ShowBaseStorageMenu(int client,int core)
{
    if(!CanManageCore(client,core))return;
    int building=gCores[core].building;
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used)return;
    gBaseMenuNonce[client]++;
    char name[64],title[256],token[128],line[192];CoreDisplayName(core,name,sizeof(name),client);
    Format(title,sizeof(title),"%T","MenuStorageTitle",client,name);
    Menu menu=new Menu(BaseMenuHandler);menu.SetTitle("%s",title);
    for(int m=0;m<gMaterialCount;m++)
    {
        BaseMenuToken(client,core,5,token,sizeof(token),gMaterials[m].id);
        char material[64];TranslateLabel(client,gMaterials[m].label,material,sizeof(material));
        Format(line,sizeof(line),"%T","MenuStorageMaterial",client,material,gPlayerInventory[client][m],gBaseInventory[core][m]);
        menu.AddItem(token,line);
    }
    BaseMenuToken(client,core,2,token,sizeof(token));TranslateLabel(client,"MenuBackBase",line,sizeof(line));menu.AddItem(token,line);
    menu.Display(client,30);
}

void ShowBaseMaterialMenu(int client,int core,int material)
{
    if(!CanManageCore(client,core) || material<0 || material>=gMaterialCount)return;
    int building=gCores[core].building;
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used)return;
    gBaseMenuNonce[client]++;
    char name[64],title[256],token[128],line[128],materialName[64];CoreDisplayName(core,name,sizeof(name),client);
    TranslateLabel(client,gMaterials[material].label,materialName,sizeof(materialName));
    Format(title,sizeof(title),"%T","MenuMaterialTitle",client,name,materialName,
        gPlayerInventory[client][material],gBaseInventory[core][material]);
    int pending=SharedPendingAmount(core,material);
    if(pending>0){Format(line,sizeof(line),"%T","MenuStoragePending",client,pending);StrCat(title,sizeof(title),line);}
    Menu menu=new Menu(BaseMenuHandler);menu.SetTitle("%s",title);
    for(int action=0;action<6;action++)
    {
        bool deposit=action<3;int choice=action%3;
        int available=deposit ? gPlayerInventory[client][material] : gBaseInventory[core][material];
        int amount=choice==0 ? 1 : (choice==1 ? 10 : available);
        int current=deposit ? gBaseInventory[core][material] : gPlayerInventory[client][material];
        int maximum=deposit ? Number(C_Resources,"BaseStorage","capacity",0) : gInventoryCap;
        if(maximum<=0)maximum=2000000000;
        bool allowed=amount>0 && available>=amount && amount<=maximum-current;
        if(choice==2)Format(line,sizeof(line),"%T",deposit ? "MenuDepositAll" : "MenuWithdrawAll",client,available);
        else Format(line,sizeof(line),"%T",deposit ? "MenuDepositAmount" : "MenuWithdrawAmount",client,amount);
        BaseMenuToken(client,core,deposit ? 6 : 7,token,sizeof(token),gMaterials[material].id,choice==2 ? -1 : amount);
        menu.AddItem(token,line,allowed ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
    }
    BaseMenuToken(client,core,4,token,sizeof(token));TranslateLabel(client,"MenuBackStorage",line,sizeof(line));menu.AddItem(token,line);
    menu.Display(client,30);
}

public int BaseMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)delete menu;
    else if(action==MenuAction_Select)
    {
        char token[192];menu.GetItem(item,token,sizeof(token));int core,operation,material,quantity;
        if(!ValidateBaseMenuToken(client,token,core,operation,material,quantity))
        {
            if(LiveHuman(client))Tell(client,"%T","MenuExpired",client);
            return 0;
        }
        if(operation==5)
        {
            ShowBaseMaterialMenu(client,core,material);return 0;
        }
        if(operation==6 || operation==7)
        {
            bool deposit=operation==6;
            int available=deposit ? gPlayerInventory[client][material] : gBaseInventory[core][material];
            int amount=quantity==-1 ? available : quantity;
            if(!TransferBaseMaterial(client,core,material,amount,deposit))Tell(client,"%T","MenuTransferFailed",client);
            ShowBaseMaterialMenu(client,core,material);return 0;
        }
        switch(operation)
        {
            case 0: ShowBaseUpgradeMenu(client,core);
            case 1:
            {
                int building=gCores[core].building,cost[MATERIALS],nextMaxHP;
                // Re-read the level and personal inventory; another action may have spent materials.
                if(gBuildings[building].alive && ReadBaseUpgrade(building,cost,nextMaxHP) && HasBaseUpgradeMaterials(cost,client)
                    && TryUpgrade(client,building))
                {
                    char name[64];CoreDisplayName(core,name,sizeof(name),client);
                    HudFeedback(client,"%T","HUDUpgrade",client,name,gBuildings[building].level,gBuildings[building].hp,gBuildings[building].maxHP);
                    ShowBaseMenu(client,core);
                }
                else
                {
                    Tell(client,"%T","MenuUpgradeChanged",client);ShowBaseUpgradeMenu(client,core);
                }
            }
            case 2: ShowBaseMenu(client,core);
            case 3: BuildMenuCommand(client,0);
            case 4: ShowBaseStorageMenu(client,core);
            case 8: ShowSkills(client,core);
        }
    }
    return 0;
}
