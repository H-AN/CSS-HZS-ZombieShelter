public Action BuildMenuCommand(int client,int args)
{
    if(!LiveHuman(client) || !Running()){Tell(client,"%T","CmdNotStarted",client);return Plugin_Handled;}
    char title[128];Format(title,sizeof(title),"%T","MenuBuildTitle",client);
    Menu menu=new Menu(BuildMenuHandler);menu.SetTitle("%s",title);
    for(int i=0;i<gRecipeCount;i++)
    {
        char id[64],line[192],name[96];Format(id,sizeof(id),"%d:%d:%d",gEpoch,GetClientSerial(client),i);
        TranslateLabel(client,gRecipes[i].label,name,sizeof(name));
        Format(line,sizeof(line),"%T","MenuBuildRecipe",client,name,gRecipes[i].cost[0],gRecipes[i].cost[1],gRecipes[i].cost[2],gRecipes[i].cost[3],gRecipes[i].cost[4]);
        menu.AddItem(id,line);
    }
    menu.Display(client,30);return Plugin_Handled;
}

public int BuildMenuHandler(Menu menu,MenuAction action,int param1,int param2)
{
    if(action==MenuAction_End)delete menu;
    else if(action==MenuAction_Select)
    {
        char id[64],tokens[3][24];menu.GetItem(param2,id,sizeof(id));
        if(ExplodeString(id,":",tokens,3,24)==3 && LiveHuman(param1) && Running()
            && StringToInt(tokens[0])==gEpoch && StringToInt(tokens[1])==GetClientSerial(param1))BuildRecipe(param1,StringToInt(tokens[2]));
    }
    return 0;
}

public Action MaterialsCommand(int client,int args)
{
    if(client<1 || !IsClientInGame(client)){ReplyToCommand(client,"%T","CmdPlayerInventoryOnly",client);return Plugin_Handled;}
    Tell(client,"%T","CmdInventory",client,
        gPlayerInventory[client][0],gPlayerInventory[client][1],gPlayerInventory[client][2],gPlayerInventory[client][3],gPlayerInventory[client][4]);return Plugin_Handled;
}

public Action StatusCommand(int client,int args)
{
    int tracked,parked,activeBoss;
    for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && !gNPCs[i].dead){tracked++;if(gNPCs[i].paused)parked++;if(gNPCs[i].boss>=0 && gNPCs[i].active)activeBoss++;}
    ReplyToCommand(client,"%T","CmdStatus",client,
        SHELTER_VERSION,gEnabled,gCoreReady,gPhase,gDay,AliveCores(),gCoreCount,gNodeCount,SummonCount(),tracked,parked,ActiveOrdinary(),activeBoss,gEpoch,gRollback);
    for(int i=0;i<gCoreCount;i++){int b=gCores[i].building;if(b>=0)ReplyToCommand(client,"%T","CmdStatusBase",client,gCores[i].id,gBuildings[b].level,gBuildings[b].hp,gBuildings[b].maxHP,gBuildings[b].alive,gBuildings[b].ref);}
    for(int i=0;i<gBossCount;i++)ReplyToCommand(client,"%T","CmdStatusBoss",client,gBosses[i].id,gBosses[i].savedHP,gBosses[i].awakened,gBosses[i].defeated,gBosses[i].boundThisRound,gBosses[i].positionedHP);
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

public Action OnPlayerRunCmd(int client,int &buttons,int &impulse,float vel[3],float angles[3],int &weapon,int &subtype,int &cmdnum,int &tickcount,int &seed,int mouse[2])
{
    bool rising=(buttons&IN_USE)!=0 && (gLastButtons[client]&IN_USE)==0;gLastButtons[client]=buttons;
    if(!rising || !Running() || !LiveHuman(client) || GetGameTime()<gNextUse[client])return Plugin_Continue;
    int b=FindBuilding(AimedEntity(client,gUseDistance));
    if(b>=0 && gBuildings[b].kind!=Kind_Base)
    {
        gNextUse[client]=GetGameTime()+gUseCooldown;ShowDefenseMenu(client,b);return Plugin_Continue;
    }
    if(b>=0 && IsShelterAdmin(client))
    {
        gNextUse[client]=GetGameTime()+gUseCooldown;ShowAdminBuildingMenu(client,b,true);return Plugin_Continue;
    }
    float origin[3];GetClientAbsOrigin(client,origin);int core=SafeCoreAt(origin);
    if(core>=0){gNextUse[client]=GetGameTime()+gUseCooldown;ShowBaseMenu(client,core);}
    return Plugin_Continue;
}

int gDefenseMenuNonce[MAXPLAYERS+1];

bool CanViewDefense(int client,int building)
{
    return LiveHuman(client) && Running() && building>=0 && building<MAX_BUILDINGS
        && gBuildings[building].used && gBuildings[building].alive && gBuildings[building].kind!=Kind_Base
        && Resolve(gBuildings[building].ref)>0 && BuildingInUseRange(client,building);
}

void DefenseMenuToken(int client,int building,int operation,char[] token,int length)
{
    Format(token,length,"%d:%d:%d:%d:%d:%d:%d",gEpoch,GetClientSerial(client),gDefenseMenuNonce[client],
        building,gBuildings[building].uid,gBuildings[building].level,operation);
}

bool ValidateDefenseMenuToken(int client,const char[] token,int &building,int &operation)
{
    char fields[7][24];if(ExplodeString(token,":",fields,7,24)!=7 || !LiveHuman(client) || !Running())return false;
    if(StringToInt(fields[0])!=gEpoch || StringToInt(fields[1])!=GetClientSerial(client)
        || StringToInt(fields[2])!=gDefenseMenuNonce[client])return false;
    building=StringToInt(fields[3]);operation=StringToInt(fields[6]);
    if(!CanViewDefense(client,building) || gBuildings[building].uid!=StringToInt(fields[4])
        || gBuildings[building].level!=StringToInt(fields[5]) || operation<0 || operation>5)return false;
    if((operation==2 || operation==3 || operation==4) && !CanManageBuilding(client,building))return false;
    if(operation==5 && !IsShelterAdmin(client))return false;
    gDefenseMenuNonce[client]++;return true;
}

void AddDefenseMenuItem(Menu menu,int client,int building,int operation,const char[] phrase,bool enabled=true)
{
    char token[128],line[192];DefenseMenuToken(client,building,operation,token,sizeof(token));
    TranslateLabel(client,phrase,line,sizeof(line));menu.AddItem(token,line,enabled ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
}

void ShowDefenseMenu(int client,int building)
{
    if(!CanViewDefense(client,building))return;
    gDefenseMenuNonce[client]++;
    char name[64],title[512],access[128];BuildingDisplayName(building,name,sizeof(name),client);
    bool manage=CanManageBuilding(client,building);
    TranslateLabel(client,manage ? "DefenseManageAllowed" : "DefenseViewOnly",access,sizeof(access));
    Format(title,sizeof(title),"%T","DefenseTitle",client,name,gBuildings[building].level,
        gBuildings[building].hp,gBuildings[building].maxHP,RoundToNearest(gBuildings[building].range),
        RoundToNearest(gBuildings[building].damage),gBuildings[building].interval,access);
    if(gBuildings[building].kind==Kind_Amplifier)
    {
        float bonuses[4];int required;
        if(AmplifierLevelInfo(gBuildings[building].type,gBuildings[building].level,required,bonuses))
            Format(title,sizeof(title),"%T","DefenseAmplifierTitle",client,name,gBuildings[building].level,
                gBuildings[building].hp,gBuildings[building].maxHP,required,
                RoundToNearest(bonuses[0]*100.0),RoundToNearest(bonuses[1]*100.0),
                RoundToNearest(bonuses[2]*100.0),RoundToNearest(bonuses[3]*100.0),access);
    }
    if(gBuildings[building].kind==Kind_Generator || gBuildings[building].kind==Kind_Light)
        Format(title,sizeof(title),"%T","PowerFacilityTitle",client,name,gBuildings[building].level,gBuildings[building].hp,gBuildings[building].maxHP,access);
    if(gBuildings[building].kind==Kind_Light)
    {char line[96];Format(line,sizeof(line),"%T","LampRadiusInfo",client,gBuildings[building].lightRadius);StrCat(title,sizeof(title),line);}
    if(gBuildings[building].kind==Kind_Grenade)
    {char line[96];Format(line,sizeof(line),"%T","GrenadeRadiusInfo",client,gBuildings[building].blastRadius);StrCat(title,sizeof(title),line);}
    AppendBuildingPowerInfo(client,building,gBuildings[building].level,title,sizeof(title));
    Menu menu=new Menu(DefenseMenuHandler);menu.SetTitle("%s",title);
    int cost[MATERIALS],nextHP;bool upgrade=ReadBaseUpgrade(building,cost,nextHP);
    AddDefenseMenuItem(menu,client,building,1,upgrade ? "DefenseUpgradeView" : "DefenseUpgradeUnavailable",upgrade);
    AddDefenseMenuItem(menu,client,building,3,"DefenseDismantleView",manage);
    if(IsShelterAdmin(client))AddDefenseMenuItem(menu,client,building,5,"AdminBuildingControls");
    menu.Display(client,30);
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
    for(int m=0;m<MATERIALS;m++)
    {
        char material[64];TranslateLabel(client,gMaterials[m].label,material,sizeof(material));
        Format(line,sizeof(line),"%T","MenuUpgradeCost",client,material,gPlayerInventory[client][m],cost[m]);StrCat(title,sizeof(title),line);
    }
    bool progression=true;int required;
    if(gBuildings[building].kind==Kind_Amplifier)
    {
        progression=CanBuildAmplifier(gBuildings[building].type,gBuildings[building].level+1,required);
        Format(line,sizeof(line),"%T","DefenseBaseRequirement",client,ProgressionBaseLevel(),required);StrCat(title,sizeof(title),line);
    }
    Menu menu=new Menu(DefenseMenuHandler);menu.SetTitle("%s",title);
    bool manage=CanManageBuilding(client,building),enough=HasBaseUpgradeMaterials(cost,client);
    AddDefenseMenuItem(menu,client,building,2,!manage ? "DefenseViewOnly" : (!progression ? "DefenseBaseLevelLocked" : (enough ? "MenuUpgradeConfirm" : "MenuUpgradeInsufficient")),manage && enough && progression);
    AddDefenseMenuItem(menu,client,building,0,"DefenseBack");menu.Display(client,30);
}

void ShowDefenseDismantleMenu(int client,int building)
{
    if(!CanViewDefense(client,building) || !CanManageBuilding(client,building))return;
    gDefenseMenuNonce[client]++;
    char name[64],title[768],line[192];BuildingDisplayName(building,name,sizeof(name),client);
    Format(title,sizeof(title),"%T","DefenseDismantleTitle",client,name);
    int refund[MATERIALS];BuildingRefund(building,refund);
    for(int m=0;m<MATERIALS;m++)
    {
        char material[64];TranslateLabel(client,gMaterials[m].label,material,sizeof(material));
        Format(line,sizeof(line),"%T","DefenseRefundLine",client,material,refund[m]);StrCat(title,sizeof(title),line);
    }
    Menu menu=new Menu(DefenseMenuHandler);menu.SetTitle("%s",title);
    AddDefenseMenuItem(menu,client,building,4,"DefenseDismantleConfirm");
    AddDefenseMenuItem(menu,client,building,0,"DefenseBack");menu.Display(client,30);
}

public int DefenseMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)delete menu;
    else if(action==MenuAction_Select)
    {
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

void BaseMenuToken(int client,int core,int action,char[] token,int length)
{
    int building=gCores[core].building;
    Format(token,length,"%d:%d:%d:%d:%d:%d:%d",gEpoch,GetClientSerial(client),core,
        gBuildings[building].uid,gBuildings[building].level,gBaseMenuNonce[client],action);
}

bool ValidateBaseMenuToken(int client,const char[] token,int &core,int &operation)
{
    char fields[7][24];if(ExplodeString(token,":",fields,7,24)!=7)return false;
    if(!LiveHuman(client) || !Running() || StringToInt(fields[0])!=gEpoch
        || StringToInt(fields[1])!=GetClientSerial(client) || StringToInt(fields[5])!=gBaseMenuNonce[client])return false;
    core=StringToInt(fields[2]);if(!CanManageCore(client,core))return false;
    int building=gCores[core].building;
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used
        || StringToInt(fields[3])!=gBuildings[building].uid || StringToInt(fields[4])!=gBuildings[building].level)return false;
    operation=StringToInt(fields[6]);
    bool knownOperation=(operation>=0 && operation<=4) || (operation>=10 && operation<10+MATERIALS)
        || (operation>=20 && operation<20+MATERIALS*6);
    if(!knownOperation)return false;
    // A menu selection is a single-use transaction, including failures and navigation.
    gBaseMenuNonce[client]++;
    return true;
}

bool ReadBaseUpgrade(int building,int cost[MATERIALS],int &nextMaxHP)
{
    return ReadBuildingLevelQuote(building,gBuildings[building].level+1,cost,nextMaxHP);
}

bool HasBaseUpgradeMaterials(const int cost[MATERIALS],int client)
{
    if(!LiveHuman(client))return false;
    for(int m=0;m<MATERIALS;m++)if(cost[m]<0 || gPlayerInventory[client][m]<cost[m])return false;
    return true;
}

void ShowBaseMenu(int client,int core)
{
    if(!CanManageCore(client,core))return;
    int building=gCores[core].building;
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used)return;
    gBaseMenuNonce[client]++;
    char name[64],title[256],token[128],state[128],line[192];CoreDisplayName(core,name,sizeof(name),client);
    TranslateLabel(client,gBuildings[building].alive ? "MenuBaseAlive" : "MenuBaseDestroyed",state,sizeof(state));
    Format(title,sizeof(title),"%T","MenuBaseTitle",client,name,gBuildings[building].level,
        gBuildings[building].maxHP,gBuildings[building].hp,gBuildings[building].maxHP,state);
    AppendGridInfo(client,core,title,sizeof(title));
    Menu menu=new Menu(BaseMenuHandler);menu.SetTitle("%s",title);
    int cost[MATERIALS],nextMaxHP;bool upgrade=gBuildings[building].alive && ReadBaseUpgrade(building,cost,nextMaxHP);
    BaseMenuToken(client,core,0,token,sizeof(token));
    TranslateLabel(client,upgrade ? "MenuUpgradeView" : "MenuUpgradeUnavailable",line,sizeof(line));
    menu.AddItem(token,line,upgrade ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
    BaseMenuToken(client,core,4,token,sizeof(token));TranslateLabel(client,"MenuStorage",line,sizeof(line));menu.AddItem(token,line);
    BaseMenuToken(client,core,3,token,sizeof(token));TranslateLabel(client,"MenuBuild",line,sizeof(line));menu.AddItem(token,line);
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
    for(int m=0;m<MATERIALS;m++)
    {
        char material[64];TranslateLabel(client,gMaterials[m].label,material,sizeof(material));
        Format(line,sizeof(line),"%T","MenuUpgradeCost",client,material,gPlayerInventory[client][m],cost[m]);StrCat(title,sizeof(title),line);
    }
    Menu menu=new Menu(BaseMenuHandler);menu.SetTitle("%s",title);bool enough=HasBaseUpgradeMaterials(cost,client);
    BaseMenuToken(client,core,1,token,sizeof(token));
    TranslateLabel(client,enough ? "MenuUpgradeConfirm" : "MenuUpgradeInsufficient",line,sizeof(line));
    menu.AddItem(token,line,enough ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
    BaseMenuToken(client,core,2,token,sizeof(token));TranslateLabel(client,"MenuBackBase",line,sizeof(line));menu.AddItem(token,line);
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
    for(int m=0;m<MATERIALS;m++)
    {
        BaseMenuToken(client,core,10+m,token,sizeof(token));
        char material[64];TranslateLabel(client,gMaterials[m].label,material,sizeof(material));
        Format(line,sizeof(line),"%T","MenuStorageMaterial",client,material,gPlayerInventory[client][m],gBaseInventory[core][m]);
        menu.AddItem(token,line);
    }
    BaseMenuToken(client,core,2,token,sizeof(token));TranslateLabel(client,"MenuBackBase",line,sizeof(line));menu.AddItem(token,line);
    menu.Display(client,30);
}

void ShowBaseMaterialMenu(int client,int core,int material)
{
    if(!CanManageCore(client,core) || material<0 || material>=MATERIALS)return;
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
        BaseMenuToken(client,core,20+material*6+action,token,sizeof(token));
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
        char token[128];menu.GetItem(item,token,sizeof(token));int core,operation;
        if(!ValidateBaseMenuToken(client,token,core,operation))
        {
            if(LiveHuman(client))Tell(client,"%T","MenuExpired",client);
            return 0;
        }
        if(operation>=10 && operation<10+MATERIALS)
        {
            ShowBaseMaterialMenu(client,core,operation-10);return 0;
        }
        if(operation>=20 && operation<20+MATERIALS*6)
        {
            int material=(operation-20)/6,choice=(operation-20)%6;
            bool deposit=choice<3;int quantity=choice%3;
            int available=deposit ? gPlayerInventory[client][material] : gBaseInventory[core][material];
            int amount=quantity==0 ? 1 : (quantity==1 ? 10 : available);
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
        }
    }
    return 0;
}
