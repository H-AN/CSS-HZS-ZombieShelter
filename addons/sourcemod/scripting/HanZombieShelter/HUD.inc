// CS:S has six HUD text channels. Keep persistent information and short feedback separate.
#define HUD_CAMPAIGN 0
#define HUD_MATERIALS 1
#define HUD_TARGET 2
#define HUD_FEEDBACK 3
#define HUD_OUTCOME 4
#define HUD_ALERTS 5

void CoreDisplayName(int core,char[] name,int length,int client=LANG_SERVER)
{
    if(core>=0 && core<gCoreCount)
    {
        char letter[2];letter[0]='A'+core;letter[1]=0;
        Format(name,length,"%T","HUDBaseName",client,letter);
    }
    else TranslateLabel(client,"HUDBaseGeneric",name,length);
}

void BuildingDisplayName(int building,char[] name,int length,int client=LANG_SERVER)
{
    if(gBuildings[building].kind!=Kind_Base && gBuildings[building].meta.label[0])
    {TranslateLabel(client,gBuildings[building].meta.label,name,length);return;}
    switch(gBuildings[building].kind)
    {
        case Kind_Base: CoreDisplayName(gBuildings[building].core,name,length,client);
        case Kind_Turret: TranslateLabel(client,"HUDTurretName",name,length);
        case Kind_Flame: TranslateLabel(client,"HUDFlameName",name,length);
        case Kind_Fence: TranslateLabel(client,"HUDFenceName",name,length);
        case Kind_Mine: TranslateLabel(client,"HUDMineName",name,length);
        case Kind_Amplifier: TranslateLabel(client,"HUDAmplifierName",name,length);
        case Kind_Generator: TranslateLabel(client,"HUDGeneratorName",name,length);
        case Kind_Light: TranslateLabel(client,"HUDLightName",name,length);
        case Kind_Grenade: TranslateLabel(client,"HUDGrenadeName",name,length);
        case Kind_Expander: TranslateLabel(client,"BuildExpanderName",name,length);
    }
}

bool HudClient(int client)
{
    return client>0 && client<=MaxClients && IsClientInGame(client) && !IsFakeClient(client);
}

void HudFeedback(int client,const char[] format,any ...)
{
    if(!HudClient(client))return;
    SetGlobalTransTarget(client);
    char text[256];VFormat(text,sizeof(text),format,3);
    SetHudTextParams(-1.0,0.85,1.8,160,255,160,255,0,0.0,0.0,0.2);
    ShowHudText(client,HUD_FEEDBACK,"%s",text);
}

void HudBuildingTarget(int client,int building)
{
    char name[64],action[128];BuildingDisplayName(building,name,sizeof(name),client);
    TranslateLabel(client,gBuildings[building].kind==Kind_Base ? "HUDBaseUseHint" : "HUDDefenseUseHint",action,sizeof(action));
    SetHudTextParams(-1.0,0.54,0.4,225,245,255,255,0,0.0,0.0,0.1);
    char text[512];
    Format(text,sizeof(text),"%T",BuildingUnderAttack(building,GetGameTime()) ? "HUDBuildingAttacked" : "HUDBuildingTarget",client,name,
        gBuildings[building].level,gBuildings[building].hp,gBuildings[building].maxHP,action);
    char line[160];int core=gBuildings[building].kind==Kind_Base ? gBuildings[building].core : gBuildings[building].buildCore;
    if(gBuildings[building].kind==Kind_Base)
    {Format(line,sizeof(line),"%T","HUDTargetGridCompact",client,gGridProduction[core],gGridAllocated[core],gGridDemand[core]);StrCat(text,sizeof(text),line);}
    else if(gBuildings[building].kind==Kind_Generator)
    {Format(line,sizeof(line),"%T","HUDTargetGeneratorCompact",client,GeneratorOutput(building),gBuildings[building].powerOutput);StrCat(text,sizeof(text),line);}
    else if(gBuildings[building].powerDemand>0.0)
    {
        char state[48];TranslateLabel(client,BuildingPowered(building) ? "PowerOnline" : "PowerOffline",state,sizeof(state));
        Format(line,sizeof(line),"%T","HUDTargetPowerCompact",client,state,gBuildings[building].powerDemand);StrCat(text,sizeof(text),line);
    }
    RefreshBuildAreas();
    if(core>=0 && core<gCoreCount)
    {
        char baseName[64];CoreDisplayName(core,baseName,sizeof(baseName),client);
        Format(line,sizeof(line),"%T","HUDTargetCapacityCompact",client,baseName,gBuildAreas[core].count,gBuildAreas[core].limit);StrCat(text,sizeof(text),line);
    }
    ShowHudText(client,HUD_TARGET,"%s",text);
}

bool BuildingUnderAttack(int building,float now)
{
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used || !gBuildings[building].alive
        || gBuildings[building].kind!=Kind_Base || gBuildings[building].lastAttackAt<=0.0)return false;
    float duration=Decimal(C_Mode,"HUD","base_attack_alert_seconds",3.0);
    return duration>0.0 && now-gBuildings[building].lastAttackAt<=duration;
}

int BuildBaseAttackList(int client,float now,char[] text,int length)
{
    text[0]=0;int count;
    for(int core=0;core<gCoreCount;core++)
    {
        if(!BuildingUnderAttack(gCores[core].building,now))continue;
        char name[64];CoreDisplayName(core,name,sizeof(name),client);
        if(count>0)StrCat(text,length,count%3==0 ? "\n" : " / ");
        StrCat(text,length,name);count++;
    }
    return count;
}

void HudResourceTarget(int client,int node)
{
    int material=gNodes[node].material;
    char name[64],state[64];TranslateLabel(client,gMaterials[material].label,name,sizeof(name));
    TranslateLabel(client,gNodes[node].growing ? "HUDResourceGrowing" : "HUDResourceKnife",state,sizeof(state));
    SetHudTextParams(-1.0,0.69,0.4,240,235,185,255,0,0.0,0.0,0.1);
    ShowHudText(client,HUD_TARGET,"%T","HUDResourceTarget",client,name,
        gNodes[node].remaining,gMaterials[material].quota,state);
}

void HudGatherFeedback(int client,int node,int amount)
{
    if(!HudClient(client) || node<0 || node>=gNodeCount)return;
    int material=gNodes[node].material;
    char name[64];TranslateLabel(client,gMaterials[material].label,name,sizeof(name));
    // The final hit must remain readable even after collision and alpha are disabled.
    HudFeedback(client,"%T",gNodes[node].remaining==0 ? "HUDGatherDepleted" : "HUDGather",client,name,amount,
        gNodes[node].remaining,gMaterials[material].quota);
    HudResourceTarget(client,node);
}

void HudRepairFeedback(int client,int building,int restoredHP)
{
    if(!HudClient(client) || building<0 || building>=MAX_BUILDINGS)return;
    char name[64];BuildingDisplayName(building,name,sizeof(name),client);
    if(restoredHP>0)HudFeedback(client,"%T","HUDRepair",client,name,restoredHP,gBuildings[building].hp,gBuildings[building].maxHP);
    else HudFeedback(client,"%T","HUDRepairFull",client,name,gBuildings[building].hp,gBuildings[building].maxHP);
    HudBuildingTarget(client,building);
}

void RenderCampaignHUD()
{
    float now=GetGameTime();bool refreshStatus=now>=gNextHUD;
    if(refreshStatus)
    {
        gNextHUD=now+1.0;
        if(Running())DrawSafeZones();
    }
    if(Running())DrawBuildZones();
    int bosses;for(int i=0;i<gBossCount;i++)if(gBosses[i].defeated)bosses++;
    for(int client=1;client<=MaxClients;client++)
    {
        if(!HudClient(client))continue;
        if(gTerminal)
        {
            if(refreshStatus)
            {
                SetHudTextParams(-1.0,0.34,1.2,255,225,120,255,0,0.0,0.0,0.1);
                ShowHudText(client,HUD_OUTCOME,"%T",gVictoryCandidate ? gVictoryText : gFailureText,client);
            }
            continue;
        }
        if(!Running())continue;
        char attacked[256];
        if(BuildBaseAttackList(client,now,attacked,sizeof(attacked))>0)
        {
            SetHudTextParams(0.64,0.41,0.4,255,110,80,255,0,0.0,0.0,0.1);
            ShowHudText(client,HUD_ALERTS,"%T","HUDBaseAttackList",client,attacked);
        }
        if(refreshStatus)
        {
            float seconds=gDeadline-now;if(seconds<0.0)seconds=0.0;
            char phase[32];TranslateLabel(client,gPhase==Phase_Day ? "HUDPhaseDay" : "HUDPhaseNight",phase,sizeof(phase));
            SetHudTextParams(0.64,0.06,1.2,230,240,255,255,0,0.0,0.0,0.1);
            ShowHudText(client,HUD_CAMPAIGN,"%T","HUDCampaign",client,
                gDay,phase,RoundToCeil(seconds),AliveCores(),gCoreCount,
                ActiveOrdinary(),OrdinaryTarget(),bosses,gBossCount);
            SetHudTextParams(0.64,0.18,1.2,220,225,170,255,0,0.0,0.0,0.1);
            char balances[512],entry[100];int shown;
            for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]>0)
            {
                char materialName[64];TranslateLabel(client,gMaterials[m].label,materialName,sizeof(materialName));
                Format(entry,sizeof(entry),"%s%s: %d",shown>0 ? " / " : "",materialName,gPlayerInventory[client][m]);
                if(strlen(balances)+strlen(entry)>=sizeof(balances)-1)break;
                StrCat(balances,sizeof(balances),entry);shown++;
            }
            if(shown==0)TranslateLabel(client,"PersonalBackpackEmpty",balances,sizeof(balances));
            ShowHudText(client,HUD_MATERIALS,"%s",balances);
        }
        if(!LiveHuman(client))continue;
        int entity=AimedEntity(client,1024.0),building=FindBuilding(entity),node=FindNode(entity);
        if(building>=0 && gBuildings[building].alive)HudBuildingTarget(client,building);
        else if(node>=0)HudResourceTarget(client,node);
    }
}
