public Action BuildMenuCommand(int client,int args)
{
    if(!LiveHuman(client) || !Running()){Tell(client,"昼夜求生尚未开始。");return Plugin_Handled;}
    Menu menu=new Menu(BuildMenuHandler);menu.SetTitle("建造防御设施（团队材料）");
    for(int i=0;i<gRecipeCount;i++)
    {
        char id[64],line[192];Format(id,sizeof(id),"%d:%d:%d",gEpoch,GetClientSerial(client),i);
        Format(line,sizeof(line),"%s  木%d 铁%d 铜%d 银%d 金%d",gRecipes[i].label,gRecipes[i].cost[0],gRecipes[i].cost[1],gRecipes[i].cost[2],gRecipes[i].cost[3],gRecipes[i].cost[4]);
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
    Tell(client,"团队材料：木%d 铁%d 铜%d 银%d 金%d",gInventory[0],gInventory[1],gInventory[2],gInventory[3],gInventory[4]);return Plugin_Handled;
}

public Action StatusCommand(int client,int args)
{
    int tracked,parked,activeBoss;
    for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && !gNPCs[i].dead){tracked++;if(gNPCs[i].paused)parked++;if(gNPCs[i].boss>=0 && gNPCs[i].active)activeBoss++;}
    ReplyToCommand(client,"[Shelter] version=%s enabled=%d ready=%d phase=%d day=%d bases=%d/%d nodes=%d summons=%d tracked=%d parked=%d raiders=%d bosses=%d epoch=%d rollback=%d",
        SHELTER_VERSION,gEnabled,gCoreReady,gPhase,gDay,AliveCores(),gCoreCount,gNodeCount,SummonCount(),tracked,parked,ActiveOrdinary(),activeBoss,gEpoch,gRollback);
    for(int i=0;i<gCoreCount;i++){int b=gCores[i].building;if(b>=0)ReplyToCommand(client,"[ShelterBase] %s level=%d hp=%d/%d alive=%d ref=%d",gCores[i].id,gBuildings[b].level,gBuildings[b].hp,gBuildings[b].maxHP,gBuildings[b].alive,gBuildings[b].ref);}
    for(int i=0;i<gBossCount;i++)ReplyToCommand(client,"[ShelterBoss] %s hp=%d awake=%d defeated=%d bound=%d positioned_hp=%d",gBosses[i].id,gBosses[i].savedHP,gBosses[i].awakened,gBosses[i].defeated,gBosses[i].boundThisRound,gBosses[i].positionedHP);
    return Plugin_Handled;
}

public Action PhaseCommand(int client,int args)
{
    if(!Running()){ReplyToCommand(client,"[Shelter] Native Han_OnGameStart has not started this session");return Plugin_Handled;}
    char phase[16];GetCmdArg(1,phase,sizeof(phase));
    if(StrEqual(phase,"night"))StartNight();else if(StrEqual(phase,"day"))StartDay(false);
    else ReplyToCommand(client,"usage: sm_zsh_phase day|night");return Plugin_Handled;
}

public Action DayCommand(int client,int args)
{
    char day[16];GetCmdArg(1,day,sizeof(day));int value=StringToInt(day);
    if(Running() && value>=1 && value<=100000){gDay=value;StartDay(false);}else ReplyToCommand(client,"usage: sm_zsh_day <1..100000>");return Plugin_Handled;
}

public Action GrantCommand(int client,int args)
{
    char material[32],amount[16];GetCmdArg(1,material,sizeof(material));GetCmdArg(2,amount,sizeof(amount));
    int m=FindMaterial(material),count=StringToInt(amount);
    if(gEnabled && m>=0 && count>0 && count<=1000000)ReplyToCommand(client,"[Shelter] Granted %d %s",Grant(m,count),material);
    else ReplyToCommand(client,"usage: sm_zsh_grant wood|iron|copper|silver|gold <1..1000000>");return Plugin_Handled;
}

public Action FailCommand(int client,int args)
{
    if(Running())QueueOutcome(false);return Plugin_Handled;
}

public Action ReloadCommand(int client,int args)
{
    if(gSnapshot || gRoundPrepared){ReplyToCommand(client,"[Shelter] 战役中不热换模型/位置；修改配置后重新加载地图生效。");return Plugin_Handled;}
    BootMode();ReplyToCommand(client,"[Shelter] reload enabled=%d",gEnabled);return Plugin_Handled;
}

public Action OnPlayerRunCmd(int client,int &buttons,int &impulse,float vel[3],float angles[3],int &weapon,int &subtype,int &cmdnum,int &tickcount,int &seed,int mouse[2])
{
    bool rising=(buttons&IN_USE)!=0 && (gLastButtons[client]&IN_USE)==0;gLastButtons[client]=buttons;
    if(!rising || !Running() || !LiveHuman(client) || GetGameTime()<gNextUse[client])return Plugin_Continue;
    int b=FindBuilding(AimedEntity(client,gUseDistance));
    if(b>=0){gNextUse[client]=GetGameTime()+gUseCooldown;TryUpgrade(client,b);}
    return Plugin_Continue;
}
