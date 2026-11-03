enum AdminMenuAction
{
    Admin_Home=0, Admin_ResetConfirm, Admin_Reset, Admin_BaseList, Admin_PlayerList, Admin_BuildingList,
    Admin_BaseMaterials=10, Admin_BaseAmounts, Admin_SetBase, Admin_ChangeBase,
    Admin_PlayerMaterials=20, Admin_PlayerAmounts, Admin_SetPlayer, Admin_ChangePlayer,
    Admin_Building=30, Admin_BuildingConfirm, Admin_BuildingApply,
    Admin_CustomBase=40, Admin_CustomPlayer,
    Admin_TravelList=50, Admin_Travel,
    Admin_SkillPlayers=60, Admin_SkillPlayer, Admin_SkillChange, Admin_SkillCustomAdd, Admin_SkillCustomRemove, Admin_SkillCustomApply
}

enum AdminTokenField { AT_Epoch, AT_Client, AT_Nonce, AT_Action, AT_Subject, AT_Identity, AT_Revision, AT_Material, AT_Value, AT_Nearby, AT_Config, AT_Count }
int gAdminMenuNonce[MAXPLAYERS+1];
Menu gAdminCurrentMenu[MAXPLAYERS+1];
char gAdminPendingToken[MAXPLAYERS+1][256];
float gAdminPendingUntil[MAXPLAYERS+1];

void RegisterShelterAdminCommands()
{
    RegAdminCmd("sm_zsh_admin",ShelterAdminCommand,ADMFLAG_ROOT);
    // The same IsShelterAdmin override governs both commands, including custom overrides.
    RegConsoleCmd("sm_zsh_admin_value",ShelterAdminValueCommand);
}

bool AdminActor(int client)
{
    return LiveHuman(client) && !IsFakeClient(client) && Running() && IsShelterAdmin(client);
}

bool TravelBaseAlive(int core)
{
    if(!Running() || core<0 || core>=gCoreCount)return false;
    int b=gCores[core].building;
    return BuildingOperational(b)
        && gBuildings[b].kind==Kind_Base && Resolve(gBuildings[b].ref)>0;
}

bool TravelPointInsideBase(int core,const float point[3])
{
    float local[3];LocalPoint(point,gCores[core].origin,gCores[core].angles,local);
    for(int box=0;box<gCores[core].boxCount;box++)if(InBox(local,gCoreMins[core][box],gCoreMaxs[core][box]))return true;
    return false;
}

bool SafeHumanPointForBase(int client,int core,float position[3],float angles[3])
{
    if(!TravelBaseAlive(core))return false;
    if(gCores[core].dynamic)return SafeHumanPointForDynamicBase(client,core,position,angles);
    if(gCores[core].pointCount<1)return false;
    float mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0};int first=GetRandomInt(0,gCores[core].pointCount-1);
    for(int offset=0;offset<gCores[core].pointCount;offset++)
    {
        int point=(first+offset)%gCores[core].pointCount;position=gCorePoints[core][point];angles=gCorePointAngles[core][point];
        bool valid=true;
        for(int axis=0;axis<3;axis++)if(!(position[axis]>=-15000.0 && position[axis]<=15000.0)
            || !(angles[axis]>=-3600.0 && angles[axis]<=3600.0))valid=false;
        if(!valid || TR_PointOutsideWorld(position) || !TravelPointInsideBase(core,position)
            || !ClearHull(position,mins,maxs,client,MASK_PLAYERSOLID))continue;
        // Confirm an actual nearby support surface; never drop into a shaft.
        float down[3],landing[3],normal[3];down=position;down[2]-=96.0;
        Handle trace=TR_TraceHullFilterEx(position,down,mins,maxs,MASK_PLAYERSOLID,FilterExcept,client);
        bool supported=TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);
        if(supported){TR_GetEndPosition(landing,trace);TR_GetPlaneNormal(trace,normal);}delete trace;
        if(!supported || normal[2]<0.7 || position[2]-landing[2]>48.0)continue;
        landing[2]+=1.0;
        if(TR_PointOutsideWorld(landing) || !TravelPointInsideBase(core,landing)
            || !ClearHull(landing,mins,maxs,client,MASK_PLAYERSOLID))continue;
        bool occupied;
        for(int player=1;player<=MaxClients;player++)if(player!=client && LiveHuman(player) && !IsFakeClient(player))
        {float other[3];GetClientAbsOrigin(player,other);if(GetVectorDistance(landing,other)<40.0){occupied=true;break;}}
        if(occupied)continue;position=landing;return true;
    }
    return false;
}

// Shared by the admin menu and public native. Calling plugins own any fees.
bool TeleportPlayerToBase(int client,int core)
{
    if(gBaseTeleportClosing || !LiveHuman(client) || IsFakeClient(client) || IsClientSourceTV(client) || IsClientReplay(client) || !TravelBaseAlive(core))return false;
    int serial=GetClientSerial(client),epoch=gEpoch,revision=gConfigRevision,uid=gBuildings[gCores[core].building].uid;
    float position[3],angles[3];if(!SafeHumanPointForBase(client,core,position,angles))return false;
    if(!LiveHuman(client) || GetClientSerial(client)!=serial || epoch!=gEpoch || revision!=gConfigRevision
        || !TravelBaseAlive(core) || gBuildings[gCores[core].building].uid!=uid)return false;
    float before[3],beforeAngles[3],beforeVelocity[3],zero[3];GetClientAbsOrigin(client,before);GetClientEyeAngles(client,beforeAngles);
    GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",beforeVelocity);TeleportEntity(client,position,angles,zero);
    float actual[3];GetClientAbsOrigin(client,actual);
    if(LiveHuman(client) && GetClientSerial(client)==serial && epoch==gEpoch && revision==gConfigRevision && TravelBaseAlive(core)
        && gBuildings[gCores[core].building].uid==uid && GetVectorDistance(actual,position)<4.0 && TravelPointInsideBase(core,actual))return true;
    if(IsClientInGame(client) && GetClientSerial(client)==serial && IsPlayerAlive(client))TeleportEntity(client,before,beforeAngles,beforeVelocity);
    return false;
}

bool AdminOnlinePlayer(int serial)
{
    int target=GetClientFromSerial(serial);
    return target>0 && IsClientInGame(target) && !IsFakeClient(target) && !IsClientSourceTV(target) && !IsClientReplay(target);
}

void AdminToken(int client,AdminMenuAction operation,char[] token,int length,int subject=-1,int identity=0,
    int revision=0,int material=-1,int value=0,bool nearby=false)
{
    Format(token,length,"%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",gEpoch,GetClientSerial(client),gAdminMenuNonce[client],
        operation,subject,identity,revision,material,value,nearby,gConfigRevision);
}

// Bind points quotes to the verified account, its revision, map session and
// current skill rules as well as the ordinary actor serial/round/menu identity.
void AdminSkillToken(int client,AdminMenuAction operation,int target,char[] token,int length,int value=0)
{
    Format(token,length,"%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",gEpoch,GetClientSerial(client),gAdminMenuNonce[client],
        operation,GetClientSerial(target),SkillsAccountIndex(target),SkillsAccountRevision(target),
        gSkillsMapSession,value,gSkillsConfigRevision,gConfigRevision);
}

bool AdminSkillTarget(int serial,int account,int revision,int session,int rules)
{
    if(!gSkillsEnabled || !gSkillsConfigReady || gTerminal || session!=gSkillsMapSession || rules!=gSkillsConfigRevision
        || !AdminOnlinePlayer(serial))return false;
    int target=GetClientFromSerial(serial);
    return SkillsIdentity(target) && SkillsAccountIndex(target)==account && SkillsAccountRevision(target)==revision;
}

void AdminDisplayMenu(Menu menu,int client)
{
    // Set before Display: the replaced menu can synchronously receive Cancel.
    // Its later Cancel/End must never clear the new menu or pending input.
    gAdminPendingToken[client][0]=0;gAdminPendingUntil[client]=0.0;
    gAdminCurrentMenu[client]=menu;menu.Display(client,30);
}

bool AdminCurrentMenu(int client,Menu menu)
{
    return client>0 && client<=MaxClients && menu!=null && gAdminCurrentMenu[client]==menu;
}

void AdminCloseMenu(int client,Menu menu)
{
    if(!AdminCurrentMenu(client,menu))return;
    gAdminCurrentMenu[client]=null;gAdminMenuNonce[client]++;
    gAdminPendingToken[client][0]=0;gAdminPendingUntil[client]=0.0;
}

bool ValidateAdminToken(int client,const char[] token,int fields[AT_Count])
{
    if(!AdminActor(client))return false;
    char parts[view_as<int>(AT_Count)+1][24];if(ExplodeString(token,":",parts,view_as<int>(AT_Count)+1,24)!=view_as<int>(AT_Count))return false;
    for(int i=0;i<view_as<int>(AT_Count);i++)if(!ParseLevelInteger(parts[i],fields[i],true))return false;
    if(fields[AT_Epoch]!=gEpoch || fields[AT_Client]!=GetClientSerial(client) || fields[AT_Nonce]!=gAdminMenuNonce[client]
        || fields[AT_Config]!=gConfigRevision)return false;
    int action=fields[AT_Action],subject=fields[AT_Subject];bool base,player,building,material;
    switch(view_as<AdminMenuAction>(action))
    {
        case Admin_Home,Admin_ResetConfirm,Admin_Reset,Admin_BaseList,Admin_PlayerList,Admin_BuildingList: {}
        case Admin_TravelList: {if(fields[AT_Revision]!=gConfigRevision)return false;}
        case Admin_SkillPlayers: {if(!gSkillsEnabled || !gSkillsConfigReady || gTerminal)return false;}
        case Admin_SkillPlayer,Admin_SkillChange,Admin_SkillCustomAdd,Admin_SkillCustomRemove,Admin_SkillCustomApply:
        {
            if(!AdminSkillTarget(subject,fields[AT_Identity],fields[AT_Revision],fields[AT_Material],fields[AT_Nearby]))return false;
            if(action==view_as<int>(Admin_SkillChange) && fields[AT_Value]!=1 && fields[AT_Value]!=10 && fields[AT_Value]!=100
                && fields[AT_Value]!=-1 && fields[AT_Value]!=-10 && fields[AT_Value]!=-100)return false;
            if(action==view_as<int>(Admin_SkillCustomApply) && fields[AT_Value]!=1 && fields[AT_Value]!=-1)return false;
        }
        case Admin_Travel:
        {
            if(fields[AT_Revision]!=gConfigRevision || !TravelBaseAlive(subject)
                || gBuildings[gCores[subject].building].uid!=fields[AT_Identity])return false;
        }
        case Admin_BaseMaterials: base=true;
        case Admin_BaseAmounts,Admin_SetBase,Admin_ChangeBase,Admin_CustomBase: {base=true;material=true;}
        case Admin_PlayerMaterials: player=true;
        case Admin_PlayerAmounts,Admin_SetPlayer,Admin_ChangePlayer,Admin_CustomPlayer: {player=true;material=true;}
        case Admin_Building,Admin_BuildingConfirm,Admin_BuildingApply: building=true;
        default: return false;
    }
    if(base)
    {
        if(subject<0 || subject>=gCoreCount)return false;
        int b=gCores[subject].building;
        if(b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || gBuildings[b].uid!=fields[AT_Identity]
            || gBuildings[b].level!=fields[AT_Revision])return false;
    }
    if(player && !AdminOnlinePlayer(subject))return false;
    if(material && (fields[AT_Material]<0 || fields[AT_Material]>=gMaterialCount))return false;
    if(building)
    {
        if(subject<0 || subject>=MAX_BUILDINGS || !gBuildings[subject].used || !gBuildings[subject].alive
            || Resolve(gBuildings[subject].ref)<0 || gBuildings[subject].uid!=fields[AT_Identity]
            || gBuildings[subject].level!=fields[AT_Revision] || fields[AT_Nearby]<0 || fields[AT_Nearby]>1)return false;
        if(fields[AT_Nearby] && !BuildingInUseRange(client,subject))return false;
        if((action==view_as<int>(Admin_BuildingConfirm) || action==view_as<int>(Admin_BuildingApply))
            && fields[AT_Value]!=0 && fields[AT_Value]!=gBuildings[subject].level-1
            && fields[AT_Value]!=gBuildings[subject].level+1)return false;
    }
    if((action==view_as<int>(Admin_ChangeBase) || action==view_as<int>(Admin_ChangePlayer))
        && fields[AT_Value]!=10 && fields[AT_Value]!=100 && fields[AT_Value]!=-10 && fields[AT_Value]!=-100)return false;
    if((action==view_as<int>(Admin_ResetConfirm) || action==view_as<int>(Admin_Reset)) && fields[AT_Revision]!=gDay)return false;
    gAdminMenuNonce[client]++;return true;
}

void AdminAdd(Menu menu,int client,AdminMenuAction action,const char[] phrase,int subject=-1,int identity=0,
    int revision=0,int material=-1,int value=0,bool nearby=false,bool enabled=true)
{
    char token[256],line[192];AdminToken(client,action,token,sizeof(token),subject,identity,revision,material,value,nearby);
    TranslateLabel(client,phrase,line,sizeof(line));menu.AddItem(token,line,enabled ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
}

public Action ShelterAdminCommand(int client,int args)
{
    if(!AdminActor(client)){ReplyToCommand(client,"%T","AdminAccessDenied",client);return Plugin_Handled;}
    ShowShelterAdminMenu(client);return Plugin_Handled;
}

void ShowShelterAdminMenu(int client)
{
    if(!AdminActor(client))return;
    gAdminMenuNonce[client]++;
    Menu menu=new Menu(ShelterAdminMenuHandler);char title[128];Format(title,sizeof(title),"%T","AdminTitle",client,gDay);menu.SetTitle("%s",title);
    AdminAdd(menu,client,Admin_ResetConfirm,"AdminResetDay",-1,0,gDay);
    AdminAdd(menu,client,Admin_BaseList,"AdminBaseSupplies");
    AdminAdd(menu,client,Admin_PlayerList,"AdminPlayerSupplies");
    AdminAdd(menu,client,Admin_SkillPlayers,"AdminSkillPoints",-1,0,0,-1,0,false,gSkillsEnabled && gSkillsConfigReady && !gTerminal);
    AdminAdd(menu,client,Admin_BuildingList,"AdminAllBuildings");
    AdminAdd(menu,client,Admin_TravelList,"AdminTravelMenu",-1,0,gConfigRevision);AdminDisplayMenu(menu,client);
}

void ShowAdminTravelMenu(int client)
{
    if(!AdminActor(client))return;gAdminMenuNonce[client]++;
    char title[192];TranslateLabel(client,"AdminTravelTitle",title,sizeof(title));Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    int count;
    for(int core=0;core<gCoreCount;core++)if(TravelBaseAlive(core))
    {
        char token[256],name[64],label[128];CoreDisplayName(core,name,sizeof(name),client);
        Format(label,sizeof(label),"%T","AdminTravelDestination",client,name);
        AdminToken(client,Admin_Travel,token,sizeof(token),core,gBuildings[gCores[core].building].uid,gConfigRevision);menu.AddItem(token,label);count++;
    }
    if(count==0){char text[128];TranslateLabel(client,"AdminTravelNoBases",text,sizeof(text));menu.AddItem("",text,ITEMDRAW_DISABLED);}
    AdminAdd(menu,client,Admin_Home,"AdminBack");AdminDisplayMenu(menu,client);
}

void ShowAdminResetMenu(int client)
{
    if(!AdminActor(client))return;
    gAdminMenuNonce[client]++;char title[256];Format(title,sizeof(title),"%T","AdminResetTitle",client,gDay);
    Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    AdminAdd(menu,client,Admin_Reset,"AdminConfirmReset",-1,0,gDay);AdminAdd(menu,client,Admin_Home,"AdminBack");AdminDisplayMenu(menu,client);
}

void ShowAdminBaseList(int client)
{
    if(!AdminActor(client))return;
    gAdminMenuNonce[client]++;char title[128];TranslateLabel(client,"AdminBaseSupplies",title,sizeof(title));
    Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    for(int core=0;core<gCoreCount;core++)
    {
        int b=gCores[core].building;if(b<0 || !gBuildings[b].used)continue;
        char token[256],name[64];CoreDisplayName(core,name,sizeof(name),client);
        AdminToken(client,Admin_BaseMaterials,token,sizeof(token),core,gBuildings[b].uid,gBuildings[b].level);menu.AddItem(token,name);
    }
    AdminAdd(menu,client,Admin_Home,"AdminBack");AdminDisplayMenu(menu,client);
}

void ShowAdminPlayerList(int client)
{
    if(!AdminActor(client))return;
    gAdminMenuNonce[client]++;char title[128];TranslateLabel(client,"AdminPlayerSupplies",title,sizeof(title));
    Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    for(int target=1;target<=MaxClients;target++)
    {
        if(!IsClientInGame(target) || IsFakeClient(target) || IsClientSourceTV(target) || IsClientReplay(target))continue;
        char token[256],name[MAX_NAME_LENGTH];GetClientName(target,name,sizeof(name));
        AdminToken(client,Admin_PlayerMaterials,token,sizeof(token),GetClientSerial(target));menu.AddItem(token,name);
    }
    AdminAdd(menu,client,Admin_Home,"AdminBack");AdminDisplayMenu(menu,client);
}

void ShowAdminSkillPlayers(int client)
{
    if(!AdminActor(client) || !gSkillsEnabled || !gSkillsConfigReady || gTerminal)return;
    SkillsObserveDay();gAdminMenuNonce[client]++;
    char title[192];TranslateLabel(client,"AdminSkillPlayersTitle",title,sizeof(title));
    Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);int count;
    for(int target=1;target<=MaxClients;target++)
    {
        // A dead connected human still has an account to administer. Never
        // replace verified Steam identity with a slot, serial or fake client.
        if(!SkillsIdentity(target))continue;
        char token[256],name[MAX_NAME_LENGTH],line[192];GetClientName(target,name,sizeof(name));
        Format(line,sizeof(line),"%T","AdminSkillPlayerEntry",client,name,SkillsBalance(target));
        AdminSkillToken(client,Admin_SkillPlayer,target,token,sizeof(token));menu.AddItem(token,line);count++;
    }
    if(count==0){char line[128];TranslateLabel(client,"AdminSkillNoPlayers",line,sizeof(line));menu.AddItem("",line,ITEMDRAW_DISABLED);}
    AdminAdd(menu,client,Admin_Home,"AdminBack");AdminDisplayMenu(menu,client);
}

void AdminSkillAdd(Menu menu,int client,int target,AdminMenuAction action,const char[] phrase,int value=0)
{
    char token[256],line[192];AdminSkillToken(client,action,target,token,sizeof(token),value);
    TranslateLabel(client,phrase,line,sizeof(line));menu.AddItem(token,line);
}

void ShowAdminSkillPlayer(int client,int serial)
{
    if(!AdminActor(client) || !AdminOnlinePlayer(serial))return;
    int target=GetClientFromSerial(serial);SkillsObserveDay();
    if(!SkillsIdentity(target) || !gSkillsEnabled || !gSkillsConfigReady || gTerminal)return;
    gAdminMenuNonce[client]++;
    char name[MAX_NAME_LENGTH],title[512];GetClientName(target,name,sizeof(name));
    Format(title,sizeof(title),"%T","AdminSkillPlayerTitle",client,name,SkillsBalance(target),SkillsEarnedQuota(),
        SkillsAdminAdjustment(target),SkillsSpent(target));
    Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    AdminSkillAdd(menu,client,target,Admin_SkillChange,"AdminSkillAdd1",1);
    AdminSkillAdd(menu,client,target,Admin_SkillChange,"AdminSkillAdd10",10);
    AdminSkillAdd(menu,client,target,Admin_SkillChange,"AdminSkillAdd100",100);
    AdminSkillAdd(menu,client,target,Admin_SkillChange,"AdminSkillRemove1",-1);
    AdminSkillAdd(menu,client,target,Admin_SkillChange,"AdminSkillRemove10",-10);
    AdminSkillAdd(menu,client,target,Admin_SkillChange,"AdminSkillRemove100",-100);
    AdminSkillAdd(menu,client,target,Admin_SkillCustomAdd,"AdminSkillCustomAdd");
    AdminSkillAdd(menu,client,target,Admin_SkillCustomRemove,"AdminSkillCustomRemove");
    AdminAdd(menu,client,Admin_SkillPlayers,"AdminBack");AdminDisplayMenu(menu,client);
}

void ApplyAdminSkillChange(int client,int fields[AT_Count],int delta)
{
    if(!AdminActor(client) || !AdminSkillTarget(fields[AT_Subject],fields[AT_Identity],fields[AT_Revision],
        fields[AT_Material],fields[AT_Nearby]))return;
    int target=GetClientFromSerial(fields[AT_Subject]),actual;
    if(!SkillsAdminAdjust(client,target,delta,fields[AT_Identity],fields[AT_Revision],actual))
        Tell(client,"%T","AdminMutationRejected",client);
    else HudFeedback(client,"%T","AdminSkillChanged",client,actual,SkillsBalance(target));
    ShowAdminSkillPlayer(client,fields[AT_Subject]);
}

void BeginAdminSkillCustom(int client,int fields[AT_Count],bool add)
{
    if(!AdminActor(client) || !AdminSkillTarget(fields[AT_Subject],fields[AT_Identity],fields[AT_Revision],
        fields[AT_Material],fields[AT_Nearby]))return;
    int target=GetClientFromSerial(fields[AT_Subject]);
    AdminSkillToken(client,Admin_SkillCustomApply,target,gAdminPendingToken[client],sizeof(gAdminPendingToken[]),add ? 1 : -1);
    gAdminPendingUntil[client]=GetGameTime()+30.0;Tell(client,"%T","AdminSkillCustomInput",client);
}

void ShowAdminMaterialList(int client,int subject,bool base)
{
    if(!AdminActor(client) || (base ? (subject<0 || subject>=gCoreCount) : !AdminOnlinePlayer(subject)))return;
    int identity,revision,target;
    char name[64];
    if(base)
    {
        int b=gCores[subject].building;if(b<0 || !gBuildings[b].used)return;
        identity=gBuildings[b].uid;revision=gBuildings[b].level;CoreDisplayName(subject,name,sizeof(name),client);
    }
    else{target=GetClientFromSerial(subject);GetClientName(target,name,sizeof(name));}
    gAdminMenuNonce[client]++;
    char title[192];Format(title,sizeof(title),"%T","AdminMaterialsTitle",client,name);
    Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    for(int m=0;m<gMaterialCount;m++)
    {
        char token[256],label[64],line[128];TranslateLabel(client,gMaterials[m].label,label,sizeof(label));
        int count=base ? gBaseInventory[subject][m] : gPlayerInventory[target][m];
        Format(line,sizeof(line),"%T","AdminMaterialCount",client,label,count);
        AdminToken(client,base ? Admin_BaseAmounts : Admin_PlayerAmounts,token,sizeof(token),subject,identity,revision,m);menu.AddItem(token,line);
    }
    AdminAdd(menu,client,base ? Admin_BaseList : Admin_PlayerList,"AdminBack");AdminDisplayMenu(menu,client);
}

void ShowAdminAmounts(int client,int subject,int material,bool base)
{
    if(!AdminActor(client) || material<0 || material>=gMaterialCount
        || (base ? (subject<0 || subject>=gCoreCount) : !AdminOnlinePlayer(subject)))return;
    int identity,revision,target,count;char name[64],label[64],title[256];
    if(base)
    {
        int b=gCores[subject].building;if(b<0 || !gBuildings[b].used)return;
        identity=gBuildings[b].uid;revision=gBuildings[b].level;count=gBaseInventory[subject][material];CoreDisplayName(subject,name,sizeof(name),client);
    }
    else{target=GetClientFromSerial(subject);GetClientName(target,name,sizeof(name));count=gPlayerInventory[target][material];}
    TranslateLabel(client,gMaterials[material].label,label,sizeof(label));
    Format(title,sizeof(title),"%T","AdminAmountTitle",client,name,label,count);
    gAdminMenuNonce[client]++;Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    AdminAdd(menu,client,base ? Admin_ChangeBase : Admin_ChangePlayer,"AdminAdd10",subject,identity,revision,material,10);
    AdminAdd(menu,client,base ? Admin_ChangeBase : Admin_ChangePlayer,"AdminAdd100",subject,identity,revision,material,100);
    AdminAdd(menu,client,base ? Admin_ChangeBase : Admin_ChangePlayer,"AdminRemove10",subject,identity,revision,material,-10);
    AdminAdd(menu,client,base ? Admin_ChangeBase : Admin_ChangePlayer,"AdminRemove100",subject,identity,revision,material,-100);
    AdminAdd(menu,client,base ? Admin_SetBase : Admin_SetPlayer,"AdminSetZero",subject,identity,revision,material,0);
    AdminAdd(menu,client,base ? Admin_CustomBase : Admin_CustomPlayer,"AdminSetCustom",subject,identity,revision,material);
    AdminAdd(menu,client,base ? Admin_BaseMaterials : Admin_PlayerMaterials,"AdminBack",subject,identity,revision);AdminDisplayMenu(menu,client);
}

void ShowAdminBuildingList(int client)
{
    if(!AdminActor(client))return;
    gAdminMenuNonce[client]++;char title[128];TranslateLabel(client,"AdminAllBuildings",title,sizeof(title));
    Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!gBuildings[b].used || !gBuildings[b].alive || Resolve(gBuildings[b].ref)<0)continue;
        char token[256],name[64],line[192];BuildingDisplayName(b,name,sizeof(name),client);
        Format(line,sizeof(line),"%T","AdminBuildingEntry",client,name,gBuildings[b].uid,gBuildings[b].level);
        AdminToken(client,Admin_Building,token,sizeof(token),b,gBuildings[b].uid,gBuildings[b].level);menu.AddItem(token,line);
    }
    AdminAdd(menu,client,Admin_Home,"AdminBack");AdminDisplayMenu(menu,client);
}

bool AdminBuildingAvailable(int client,int b,bool nearby)
{
    return AdminActor(client) && b>=0 && b<MAX_BUILDINGS && gBuildings[b].used && gBuildings[b].alive
        && Resolve(gBuildings[b].ref)>0 && (!nearby || BuildingInUseRange(client,b));
}

void ShowAdminBuildingMenu(int client,int b,bool nearby)
{
    if(!AdminBuildingAvailable(client,b,nearby))return;
    gAdminMenuNonce[client]++;
    char title[384],name[64],scope[128];BuildingDisplayName(b,name,sizeof(name),client);
    TranslateLabel(client,nearby ? "AdminNearbyScope" : "AdminGlobalScope",scope,sizeof(scope));
    Format(title,sizeof(title),"%T","AdminBuildingTitle",client,name,gBuildings[b].uid,gBuildings[b].level,gBuildings[b].hp,gBuildings[b].maxHP,scope);
    Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    bool canUpgrade=!gBuildings[b].constructing && BuildingLevelAvailable(b,gBuildings[b].level+1);
    AdminAdd(menu,client,Admin_BuildingConfirm,"AdminLevelUp",b,gBuildings[b].uid,gBuildings[b].level,-1,gBuildings[b].level+1,nearby,canUpgrade);
    AdminAdd(menu,client,Admin_BuildingConfirm,"AdminLevelDown",b,gBuildings[b].uid,gBuildings[b].level,-1,gBuildings[b].level-1,nearby,!gBuildings[b].constructing && gBuildings[b].level>1);
    AdminAdd(menu,client,Admin_BuildingConfirm,"AdminDestroyBuilding",b,gBuildings[b].uid,gBuildings[b].level,-1,0,nearby);
    AdminAdd(menu,client,Admin_BuildingList,"AdminAllBuildings");AdminDisplayMenu(menu,client);
}

void ShowAdminBuildingConfirm(int client,int b,int nextLevel,bool nearby)
{
    if(!AdminBuildingAvailable(client,b,nearby))return;
    if(nextLevel!=0 && gBuildings[b].constructing){Tell(client,"%t","ConstructionIncomplete");return;}
    gAdminMenuNonce[client]++;
    char title[512],name[64];BuildingDisplayName(b,name,sizeof(name),client);
    if(nextLevel==0)Format(title,sizeof(title),"%T","AdminDestroyTitle",client,name,gBuildings[b].uid);
    else Format(title,sizeof(title),"%T","AdminLevelTitle",client,name,gBuildings[b].level,nextLevel);
    Menu menu=new Menu(ShelterAdminMenuHandler);menu.SetTitle("%s",title);
    AdminAdd(menu,client,Admin_BuildingApply,"AdminConfirm",b,gBuildings[b].uid,gBuildings[b].level,-1,nextLevel,nearby);
    AdminAdd(menu,client,Admin_Building,"AdminBack",b,gBuildings[b].uid,gBuildings[b].level,-1,0,nearby);AdminDisplayMenu(menu,client);
}

void ApplyAdminMaterialChange(int client,int fields[AT_Count],bool base,bool delta)
{
    if(!AdminActor(client))return;
    int subject=fields[AT_Subject],material=fields[AT_Material],value=fields[AT_Value];
    if(delta)
    {
        int current=base ? gBaseInventory[subject][material] : gPlayerInventory[GetClientFromSerial(subject)][material];
        value+=current;
    }
    bool success=base ? AdminSetBaseMaterial(client,subject,material,value) : AdminSetPlayerMaterial(client,subject,material,value);
    if(!success)Tell(client,"%T","AdminMutationRejected",client);
    else HudFeedback(client,"%T","AdminMaterialChanged",client,value);
    ShowAdminAmounts(client,subject,material,base);
}

void BeginAdminCustomValue(int client,int fields[AT_Count],bool base)
{
    if(!AdminActor(client))return;
    AdminToken(client,base ? Admin_SetBase : Admin_SetPlayer,gAdminPendingToken[client],sizeof(gAdminPendingToken[]),
        fields[AT_Subject],fields[AT_Identity],fields[AT_Revision],fields[AT_Material]);
    gAdminPendingUntil[client]=GetGameTime()+30.0;Tell(client,"%T","AdminCustomInput",client);
}

bool ParseAdminAmount(const char[] number,int &value)
{
    int length=strlen(number);
    if(length<1 || length>10 || (length==10 && strcmp(number,"2000000000")>0))return false;
    for(int i=0;i<length;i++)if(number[i]<'0' || number[i]>'9')return false;
    value=StringToInt(number);return true;
}

bool AdminPendingSkillInput(int client)
{
    // The token is created only by this plugin. This is a prompt choice; the
    // complete identity and signed action are still checked before mutation.
    char parts[view_as<int>(AT_Count)+1][24];
    return ExplodeString(gAdminPendingToken[client],":",parts,view_as<int>(AT_Count)+1,sizeof(parts[]))==view_as<int>(AT_Count)
        && StringToInt(parts[AT_Action])==view_as<int>(Admin_SkillCustomApply);
}

public Action ShelterAdminValueCommand(int client,int args)
{
    if(!AdminActor(client)){ReplyToCommand(client,"%T","AdminAccessDenied",client);return Plugin_Handled;}
    char number[32];GetCmdArg(1,number,sizeof(number));int value;
    if(args!=1 || !ParseAdminAmount(number,value))
    {Tell(client,"%T",AdminPendingSkillInput(client) ? "AdminSkillCustomInput" : "AdminCustomInput",client);return Plugin_Handled;}
    int fields[AT_Count];
    if(!gAdminPendingToken[client][0] || GetGameTime()>gAdminPendingUntil[client]
        || !ValidateAdminToken(client,gAdminPendingToken[client],fields))
    {gAdminPendingToken[client][0]=0;Tell(client,"%T","AdminMenuExpired",client);return Plugin_Handled;}
    gAdminPendingToken[client][0]=0;gAdminPendingUntil[client]=0.0;
    if(fields[AT_Action]==view_as<int>(Admin_SkillCustomApply))
    {
        if(value<1){Tell(client,"%T","AdminSkillCustomInput",client);return Plugin_Handled;}
        ApplyAdminSkillChange(client,fields,value*fields[AT_Value]);return Plugin_Handled;
    }
    fields[AT_Value]=value;
    ApplyAdminMaterialChange(client,fields,fields[AT_Action]==view_as<int>(Admin_SetBase),false);return Plugin_Handled;
}

public int ShelterAdminMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)
    {
        for(int owner=1;owner<=MaxClients;owner++)AdminCloseMenu(owner,menu);
        delete menu;
    }
    else if(action==MenuAction_Cancel)AdminCloseMenu(client,menu);
    else if(action==MenuAction_Select)
    {
        if(!AdminCurrentMenu(client,menu))return 0;
        char token[256];int fields[AT_Count];menu.GetItem(item,token,sizeof(token));
        if(!ValidateAdminToken(client,token,fields))
        {if(client>0 && IsClientInGame(client))Tell(client,"%T","AdminMenuExpired",client);return 0;}
        // Selection consumes this handle before any replacement menu or pending
        // exact input is created. Old Cancel/End cannot revoke the new session.
        gAdminCurrentMenu[client]=null;
        int subject=fields[AT_Subject],material=fields[AT_Material];bool nearby=fields[AT_Nearby]!=0;
        switch(view_as<AdminMenuAction>(fields[AT_Action]))
        {
            case Admin_Home: ShowShelterAdminMenu(client);
            case Admin_ResetConfirm: ShowAdminResetMenu(client);
            case Admin_Reset: RequestDayReset();
            case Admin_BaseList: ShowAdminBaseList(client);
            case Admin_PlayerList: ShowAdminPlayerList(client);
            case Admin_SkillPlayers: ShowAdminSkillPlayers(client);
            case Admin_SkillPlayer: ShowAdminSkillPlayer(client,subject);
            case Admin_SkillChange: ApplyAdminSkillChange(client,fields,fields[AT_Value]);
            case Admin_SkillCustomAdd: BeginAdminSkillCustom(client,fields,true);
            case Admin_SkillCustomRemove: BeginAdminSkillCustom(client,fields,false);
            case Admin_BuildingList: ShowAdminBuildingList(client);
            case Admin_TravelList: ShowAdminTravelMenu(client);
            case Admin_Travel:
            {
                if(TeleportPlayerToBase(client,subject))
                {char name[64];CoreDisplayName(subject,name,sizeof(name),client);Tell(client,"%T","AdminTravelSuccess",client,name);}
                else Tell(client,"%T","AdminTravelFailed",client);
                ShowAdminTravelMenu(client);
            }
            case Admin_BaseMaterials: ShowAdminMaterialList(client,subject,true);
            case Admin_PlayerMaterials: ShowAdminMaterialList(client,subject,false);
            case Admin_BaseAmounts: ShowAdminAmounts(client,subject,material,true);
            case Admin_PlayerAmounts: ShowAdminAmounts(client,subject,material,false);
            case Admin_SetBase: ApplyAdminMaterialChange(client,fields,true,false);
            case Admin_SetPlayer: ApplyAdminMaterialChange(client,fields,false,false);
            case Admin_ChangeBase: ApplyAdminMaterialChange(client,fields,true,true);
            case Admin_ChangePlayer: ApplyAdminMaterialChange(client,fields,false,true);
            case Admin_CustomBase: BeginAdminCustomValue(client,fields,true);
            case Admin_CustomPlayer: BeginAdminCustomValue(client,fields,false);
            case Admin_Building: ShowAdminBuildingMenu(client,subject,nearby);
            case Admin_BuildingConfirm: ShowAdminBuildingConfirm(client,subject,fields[AT_Value],nearby);
            case Admin_BuildingApply:
            {
                if(!AdminBuildingAvailable(client,subject,nearby))return 0;
                if(fields[AT_Value]!=0 && gBuildings[subject].constructing){Tell(client,"%t","ConstructionIncomplete");return 0;}
                bool success=fields[AT_Value]==0 ? DismantleBuilding(client,subject,true)
                    : AdminSetBuildingLevel(client,subject,fields[AT_Value]);
                if(!success)Tell(client,"%T","AdminMutationRejected",client);
                else HudFeedback(client,"%T","AdminBuildingChanged",client);
                if(fields[AT_Value]!=0)ShowAdminBuildingMenu(client,subject,nearby);else ShowAdminBuildingList(client);
            }
        }
    }
    return 0;
}
