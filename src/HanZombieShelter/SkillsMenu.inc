// Skill menus are disposable quotes. Account state lives independently of menus,
// rounds and client slots; every selection consumes one current quote.
Menu gSkillMenuHandle[MAXPLAYERS+1];
int gSkillMenuNonce[MAXPLAYERS+1],gSkillMenuCore[MAXPLAYERS+1],gSkillMenuBaseUID[MAXPLAYERS+1];

void ResetSkillMenu(int client)
{
    gSkillMenuHandle[client]=null;gSkillMenuNonce[client]++;
    gSkillMenuCore[client]=-1;gSkillMenuBaseUID[client]=0;
}

bool SkillMenuOriginValid(int client)
{
    int core=gSkillMenuCore[client];
    if(core<0)return true;
    if(!CanManageCore(client,core))return false;
    int b=gCores[core].building;
    return b>=0 && gBuildings[b].used && gBuildings[b].uid==gSkillMenuBaseUID[client];
}

Menu NewSkillMenu(int client,const char[] title)
{
    gSkillMenuNonce[client]++;
    Menu menu=new Menu(SkillMenuHandler);gSkillMenuHandle[client]=menu;menu.SetTitle("%s",title);
    return menu;
}

void SkillMenuItem(Menu menu,int client,int action,int skill,int level,const char[] label,bool enabled=true)
{
    char token[192];
    Format(token,sizeof(token),"%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",gSkillMenuNonce[client],GetClientSerial(client),gEpoch,
        gSkillsMapSession,gSkillsConfigRevision,SkillsAccountIndex(client),SkillsAccountRevision(client),action,skill,level);
    menu.AddItem(token,label,enabled ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
}

bool ValidateSkillMenu(int client,Menu menu,const char[] token,int &action,int &skill,int &level)
{
    char fields[11][24];int values[10];
    if(ExplodeString(token,":",fields,sizeof(fields),sizeof(fields[]))!=10)return false;
    for(int i=0;i<10;i++)if(!ParseLevelInteger(fields[i],values[i],true))return false;
    if(!SkillsActor(client) || gSkillMenuHandle[client]!=menu || !SkillMenuOriginValid(client)
        || values[0]!=gSkillMenuNonce[client] || values[1]!=GetClientSerial(client) || values[2]!=gEpoch
        || values[3]!=gSkillsMapSession || values[4]!=gSkillsConfigRevision
        || values[5]!=SkillsAccountIndex(client) || values[6]!=SkillsAccountRevision(client))return false;
    action=values[7];skill=values[8];level=values[9];
    if(action<0 || action>5 || (action<=2 && (skill<0 || skill>=sizeof(gSkillName) || level!=SkillsLevel(client,skill))))return false;
    if(action==3 && (skill!=-1 || (level!=0 && level!=1)))return false;
    if(action>=4 && (skill!=-1 || level!=0))return false;
    // Consume before any mutation, navigation, callback or failure response.
    gSkillMenuNonce[client]++;gSkillMenuHandle[client]=null;return true;
}

void SkillEffectDescription(int skill,int level,char[] text,int length)
{
    float amount=gSkillAmount[skill]*float(level);
    switch(skill)
    {
        case 0:Format(text,length,"工具采集倍率 +%.2f",amount);
        case 1,2:Format(text,length,"每次有效%s +%d HP",skill==1 ? "维修" : "施工",RoundToFloor(amount));
        case 3:Format(text,length,"移速 +%.1f%%",amount*100.0);
        case 4:Format(text,length,"个人重力 -%.2f（最低0.1）",amount);
        case 5:Format(text,length,"本次黑夜暴露免伤概率 %.1f%%",(amount>gSkillsImmunityCap ? gSkillsImmunityCap : amount)*100.0);
        case 6,7,8:Format(text,length,"每%.0f秒，半径%.0f内%s +%d%s",gSkillInterval[skill],gSkillRadius[skill],
            skill==8 ? "完工建筑" : "其他玩家",RoundToFloor(amount),skill==7 ? "护甲" : "HP");
        case 9,10:Format(text,length,"每%.0f秒，自身 +%d%s",gSkillInterval[skill],RoundToFloor(amount),skill==10 ? "护甲" : "HP");
        case 11,12:Format(text,length,"最大%s +%d；购买只补新增量",skill==11 ? "生命" : "护甲",RoundToFloor(amount));
    }
}

void ShowSkills(int client,int core=-2)
{
    if(!SkillsActor(client)){if(client>0 && IsClientInGame(client))Tell(client,"技能暂不可用，请等待身份验证及生存开始。");return;}
    if(core!=-2)
    {
        gSkillMenuCore[client]=core;gSkillMenuBaseUID[client]=core>=0 ? gBuildings[gCores[core].building].uid : 0;
    }
    if(!SkillMenuOriginValid(client)){ResetSkillMenu(client);return;}
    char title[256];Format(title,sizeof(title),"玩家技能\n可用点数：%d　已投入：%d\n每日自动结算，死亡与同图重连保留",SkillsBalance(client),SkillsSpent(client));
    Menu menu=NewSkillMenu(client,title);
    for(int skill=0;skill<sizeof(gSkillName);skill++)
    {
        int level=SkillsLevel(client,skill);char label[192];
        Format(label,sizeof(label),"%s　%d/%d%s",gSkillName[skill],level,SkillsMaximumLevel(client,skill),gSkillEnabled[skill] ? "" : "（已关闭）");
        SkillMenuItem(menu,client,0,skill,level,label);
    }
    SkillMenuItem(menu,client,3,-1,0,"材料洗点",SkillsSpent(client)>0);
    if(gSkillMenuCore[client]>=0)SkillMenuItem(menu,client,5,-1,0,"返回基地菜单");
    menu.Display(client,30);
}

void ShowSkillDetails(int client,int skill,bool confirmation=false)
{
    int level=SkillsLevel(client,skill);char effect[256],next[256],title[768],label[192];
    SkillEffectDescription(skill,level,effect,sizeof(effect));SkillEffectDescription(skill,level+1,next,sizeof(next));
    int maximum=SkillsMaximumLevel(client,skill);
    if(level>=maximum)strcopy(next,sizeof(next),"已达到当前等级或效果上限");
    bool available=gSkillEnabled[skill] && level<maximum && SkillsBalance(client)>=gSkillCost[skill];
    Format(title,sizeof(title),"%s　%d/%d\n当前：%s\n下一级：%s\n费用：%d点　余额：%d%s",gSkillName[skill],level,maximum,effect,next,
        gSkillCost[skill],SkillsBalance(client),confirmation ? "\n确认后立即生效" : "");
    Menu menu=NewSkillMenu(client,title);
    strcopy(label,sizeof(label),!gSkillEnabled[skill] ? "此技能已关闭" : (level>=maximum ? "已达等级或实际效果上限" :
        (SkillsBalance(client)<gSkillCost[skill] ? "技能点不足" : (confirmation ? "确认升级" : "升级详情与确认"))));
    SkillMenuItem(menu,client,confirmation ? 2 : 1,skill,level,label,available);
    SkillMenuItem(menu,client,4,-1,0,"返回玩家技能");menu.Display(client,30);
}

void ShowSkillRespec(int client)
{
    char title[768],line[128];
    Format(title,sizeof(title),"确认材料洗点\n返还实际投入：%d点\n洗点后可用：%d点\n从个人背包支付：",SkillsSpent(client),SkillsBalance(client)+SkillsSpent(client));
    bool enough=SkillsSpent(client)>0;
    for(int m=0;m<gMaterialCount;m++)
    {
        if(gPlayerInventory[client][m]<gSkillRespecCost[m])enough=false;
        if(gSkillRespecCost[m]<=0)continue;
        char name[64];TranslateLabel(client,gMaterials[m].label,name,sizeof(name));
        Format(line,sizeof(line),"\n%s：%d（持有%d）",name,gSkillRespecCost[m],gPlayerInventory[client][m]);StrCat(title,sizeof(title),line);
    }
    StrCat(title,sizeof(title),"\n不增加总点数，不补满血甲。");
    Menu menu=NewSkillMenu(client,title);
    SkillMenuItem(menu,client,3,-1,1,enough ? "确认支付材料并洗点" : "材料不足或没有已学技能",enough);
    SkillMenuItem(menu,client,4,-1,0,"返回玩家技能");menu.Display(client,30);
}

public Action SkillsCommand(int client,int args)
{
    ShowSkills(client,-1);return Plugin_Handled;
}

public Action SkillsStatusCommand(int client,int args)
{
    ReplyToCommand(client,"[ShelterSkills] map_session=%d revision=%d dmax=%d quota=%d goal=%d",gSkillsMapSession,gSkillsConfigRevision,gSkillsHighestDay,SkillsQuota(),gSurvivalDayLimit);
    ReplyToCommand(client,"[ShelterSkillsRate] configured=%d effective=%d override=%d",gSkillsPointsPerDay,SkillsDailyRate(),gSkillsDailyCvar==null ? -1 : gSkillsDailyCvar.IntValue);
    ReplyToCommand(client,"[ShelterSkillsRecovery] pulses=%d targets=%d",gSkillsPulseCount,gSkillsPulseTargetCount);
    for(int target=1;target<=MaxClients;target++)
    {
        if(!IsClientInGame(target) || IsFakeClient(target))continue;
        ReplyToCommand(client,"[ShelterSkillsPlayer] userid=%d account=%d revision=%d spent=%d balance=%d hp=%d/%d armor=%d/%d",GetClientUserId(target),
            SkillsAccountIndex(target),SkillsAccountRevision(target),SkillsSpent(target),SkillsBalance(target),GetClientHealth(target),SkillsHealthLimit(target),
            GetEntProp(target,Prop_Send,"m_ArmorValue"),SkillsArmorLimit(target));
        ReplyToCommand(client,"[ShelterSkillsBudget] userid=%d earned=%d adjustment=%d",GetClientUserId(target),SkillsEarnedQuota(),SkillsAdminAdjustment(target));
        ReplyToCommand(client,"[ShelterSkillsMovement] userid=%d speed=%.6f speed_base=%.6f speed_applied=%.6f gravity=%.6f gravity_base=%.6f gravity_applied=%.6f",GetClientUserId(target),
            GetEntPropFloat(target,Prop_Data,"m_flLaggedMovementValue"),gSkillsSpeedBase[target],gSkillsSpeedApplied[target],GetEntityGravity(target),gSkillsGravityBase[target],gSkillsGravityApplied[target]);
        for(int skill=0;skill<sizeof(gSkillName);skill++)ReplyToCommand(client,"[ShelterSkill] userid=%d id=%d level=%d",GetClientUserId(target),skill,SkillsLevel(target,skill));
    }
    return Plugin_Handled;
}

public int SkillMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)
    {
        for(int c=1;c<=MaxClients;c++)if(gSkillMenuHandle[c]==menu)ResetSkillMenu(c);
        delete menu;return 0;
    }
    if(action==MenuAction_Cancel)
    {
        if(client>0 && client<=MaxClients && gSkillMenuHandle[client]==menu)ResetSkillMenu(client);
        return 0;
    }
    if(action!=MenuAction_Select)return 0;
    char token[192];int operation,skill,level;menu.GetItem(item,token,sizeof(token));
    if(!ValidateSkillMenu(client,menu,token,operation,skill,level))
    {if(client>0 && IsClientInGame(client))Tell(client,"技能菜单已过期，请重新打开。");return 0;}
    switch(operation)
    {
        case 0:ShowSkillDetails(client,skill);
        case 1:ShowSkillDetails(client,skill,true);
        case 2:
        {
            if(!SkillsPurchase(client,skill,level,SkillsAccountRevision(client)))Tell(client,"升级未完成：点数、等级或资料已变化。");
            ShowSkillDetails(client,skill);
        }
        case 3:
        {
            if(level==0)ShowSkillRespec(client);
            else
            {
                if(!SkillsRespec(client,SkillsAccountRevision(client)))Tell(client,"洗点未完成：材料不足或资料已变化。");
                ShowSkills(client);
            }
        }
        case 4:ShowSkills(client);
        case 5:
        {
            int core=gSkillMenuCore[client];ResetSkillMenu(client);
            if(core>=0 && CanManageCore(client,core))ShowBaseMenu(client,core);
        }
    }
    return 0;
}
