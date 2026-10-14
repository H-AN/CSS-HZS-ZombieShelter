// Test-only checks of authorization and one-shot menu transactions.
// Save/restore admin identity, tokens, building state and player transform.
void TestAdminMenus(int client)
{
    Check(LiveHuman(client) && Running() && gCoreCount>=2,"admin menu fixture has a living client and multiple bases");
    if(!LiveHuman(client) || !Running() || gCoreCount<2)return;
    int building=gCores[0].building;
    if(building<0 || !gBuildings[building].alive || Resolve(gBuildings[building].ref)<0)
    {Check(false,"admin menu fixture base exists");return;}
    Building saved;saved=gBuildings[building];
    int savedEpoch=gEpoch,savedAdminNonce=gAdminMenuNonce[client],savedDefenseNonce=gDefenseMenuNonce[client];
    char savedPending[256];strcopy(savedPending,sizeof(savedPending),gAdminPendingToken[client]);
    float savedUntil=gAdminPendingUntil[client],savedOrigin[3],savedAngles[3],savedVelocity[3];
    GetClientAbsOrigin(client,savedOrigin);GetClientEyeAngles(client,savedAngles);
    GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",savedVelocity);
    AdminId savedAdmin=GetUserAdmin(client),temporary=CreateAdmin("ShelterMenuProbe");
    SetAdminFlag(temporary,Admin_Root,true);SetUserAdmin(client,temporary,false);

    char token[256];int fields[AT_Count];
    int parsed;
    Check(ParseAdminAmount("0",parsed) && parsed==0 && ParseAdminAmount("2000000000",parsed) && parsed==2000000000,
        "administrator exact amount accepts both configured numeric boundaries");
    Check(!ParseAdminAmount("2000000001",parsed) && !ParseAdminAmount("4294967296",parsed)
        && !ParseAdminAmount("-1",parsed) && !ParseAdminAmount("1abc",parsed) && !ParseAdminAmount("",parsed),
        "administrator exact amount rejects overflow and malformed numbers before integer conversion");
    AdminToken(client,Admin_Home,token,sizeof(token));
    bool realAdministrator=!IsFakeClient(client);
    Check(IsShelterAdmin(client)==realAdministrator && ValidateAdminToken(client,token,fields)==realAdministrator,
        realAdministrator ? "root administrator can select the current menu once" : "SourceMod refuses administrator access to fake clients even when their AdminId has ROOT");
    Check(!ValidateAdminToken(client,token,fields),"identical administrator menu selection cannot replay");
    AdminToken(client,Admin_Home,token,sizeof(token));gEpoch++;
    Check(!ValidateAdminToken(client,token,fields),"previous-round administrator menu is rejected");gEpoch=savedEpoch;
    Format(token,sizeof(token),"%d:%d:%d:0:-1:0:0:-1:0:0",gEpoch,GetClientSerial(client)+1,gAdminMenuNonce[client]);
    Check(!ValidateAdminToken(client,token,fields),"administrator menu is bound to its client serial");
    AdminToken(client,view_as<AdminMenuAction>(999),token,sizeof(token));
    Check(!ValidateAdminToken(client,token,fields),"administrator menu rejects unsupported operations");
    AdminToken(client,Admin_Reset,token,sizeof(token),-1,0,gDay);gDay++;
    Check(!ValidateAdminToken(client,token,fields),"day reset confirmation expires when the campaign day changes");gDay--;

    AdminToken(client,Admin_Home,token,sizeof(token));SetUserAdmin(client,INVALID_ADMIN_ID,false);
    Check(!IsShelterAdmin(client) && !ValidateAdminToken(client,token,fields),"removing ROOT permission invalidates an already-open menu");
    int savedStock=gBaseInventory[0][0];
    Check(!AdminSetBaseMaterial(client,0,0,savedStock+1) && gBaseInventory[0][0]==savedStock,
        "direct base material mutation also rechecks administrator permission");
    SetUserAdmin(client,temporary,false);

    AdminToken(client,Admin_BaseAmounts,token,sizeof(token),0,saved.uid,saved.level,0);
    Check(ValidateAdminToken(client,token,fields)==realAdministrator && (!realAdministrator || fields[AT_Subject]==0),
        realAdministrator ? "base material token resolves the intended warehouse" : "fake client cannot access administrator base materials");
    AdminToken(client,Admin_BaseAmounts,token,sizeof(token),0,saved.uid,saved.level,MATERIALS);
    Check(!ValidateAdminToken(client,token,fields),"administrator material token rejects out-of-range materials");
    Check(!AdminSetBaseMaterial(client,0,0,-1) && gBaseInventory[0][0]==savedStock,
        "negative administrator material amounts leave warehouse unchanged");
    AdminToken(client,Admin_PlayerAmounts,token,sizeof(token),-1,0,0,0);
    Check(!ValidateAdminToken(client,token,fields),"disconnected player serial is never redirected to another player slot");
    AdminToken(client,Admin_PlayerAmounts,token,sizeof(token),GetClientSerial(client),0,0,0);
    Check(ValidateAdminToken(client,token,fields)==!IsFakeClient(client),"player material selection accepts real connected players and rejects fake clients");

    AdminToken(client,Admin_Building,token,sizeof(token),building,saved.uid,saved.level);
    gBuildings[building].uid++;
    Check(!ValidateAdminToken(client,token,fields),"replaced building UID invalidates administrator selection");gBuildings[building].uid=saved.uid;
    AdminToken(client,Admin_Building,token,sizeof(token),building,saved.uid,saved.level);
    gBuildings[building].level++;
    Check(!ValidateAdminToken(client,token,fields),"changed building level invalidates an administrator quote");gBuildings[building].level=saved.level;
    AdminToken(client,Admin_BuildingApply,token,sizeof(token),building,saved.uid,saved.level,-1,saved.level+5);
    Check(!ValidateAdminToken(client,token,fields),"administrator level action cannot skip unquoted levels");

    float point[3],player[3];point=gCores[0].origin;point[0]-=160.0;point[1]-=128.0;point[2]+=45.0;
    player=point;player[1]-=70.0;player[2]=gCores[0].origin[2]+2.0;Aim(client,player,point);
    Check(BuildingInUseRange(client,building),"nearby menu fixture reaches the actual base wall");
    AdminToken(client,Admin_Building,token,sizeof(token),building,saved.uid,saved.level,-1,0,true);
    Check(ValidateAdminToken(client,token,fields)==realAdministrator,
        realAdministrator ? "E administrator action accepts a live target within reach" : "being near a building does not grant fake clients administrator permission");
    AdminToken(client,Admin_Building,token,sizeof(token),building,saved.uid,saved.level,-1,0,true);
    TeleportEntity(client,gCorePoints[1][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    Check(!ValidateAdminToken(client,token,fields),"walking away invalidates a nearby administrator action");
    AdminToken(client,Admin_Building,token,sizeof(token),building,saved.uid,saved.level,-1,0,false);
    Check(ValidateAdminToken(client,token,fields)==realAdministrator,
        realAdministrator ? "administrator global-list action deliberately allows remote management" : "global-list actions continue rejecting fake administrator clients");

    // Borrow the existing collider as a defense for token tests, without deleting or spawning entities.
    gBuildings[building].kind=Kind_Turret;Aim(client,player,point);
    SetUserAdmin(client,INVALID_ADMIN_ID,false);gBuildings[building].ownerSerial=GetClientSerial(client)+1;
    strcopy(gBuildings[building].ownerKey,sizeof(gBuildings[].ownerKey),"STEAM_1:0:999999999");
    int decoded,operation;
    DefenseMenuToken(client,building,1,token,sizeof(token));
    Check(ValidateDefenseMenuToken(client,token,decoded,operation),"another player's defense permits public statistics and cost viewing");
    DefenseMenuToken(client,building,2,token,sizeof(token));
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"another player's defense rejects upgrade confirmation");
    DefenseMenuToken(client,building,4,token,sizeof(token));
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"another player's defense rejects dismantle confirmation");
    DefenseMenuToken(client,building,5,token,sizeof(token));
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"ordinary player cannot open administrator controls");
    gBuildings[building].ownerSerial=0;gBuildings[building].ownerKey[0]=0;
    DefenseMenuToken(client,building,4,token,sizeof(token));
    Check(ValidateDefenseMenuToken(client,token,decoded,operation),"orphan defense can be managed by a nearby player");
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"defense transaction nonce prevents duplicate dismantling");
    DefenseMenuToken(client,building,2,token,sizeof(token));gBuildings[building].level++;
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"defense upgrade rechecks its quoted level");gBuildings[building].level=saved.level;
    DefenseMenuToken(client,building,2,token,sizeof(token));gBuildings[building].uid++;
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"defense management rejects a recycled building slot");gBuildings[building].uid=saved.uid;
    DefenseMenuToken(client,building,2,token,sizeof(token));gEpoch++;
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"previous-round defense transaction is rejected");gEpoch=savedEpoch;
    DefenseMenuToken(client,building,2,token,sizeof(token));
    TeleportEntity(client,gCorePoints[1][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"defense confirmation rechecks physical reach");
    gBuildings[building]=saved;

    float attacks[MAX_CORES];for(int core=0;core<gCoreCount;core++){attacks[core]=gBuildings[gCores[core].building].lastAttackAt;gBuildings[gCores[core].building].lastAttackAt=0.0;}
    float duration=Decimal(C_Mode,"HUD","base_attack_alert_seconds",3.0),now=GetGameTime();
    if(duration>0.0)
    {
        int second=gCores[1].building;bool secondAlive=gBuildings[second].alive;gBuildings[second].alive=true;
        gBuildings[building].lastAttackAt=now;gBuildings[second].lastAttackAt=now;
        char list[256],firstName[64],secondName[64];CoreDisplayName(0,firstName,sizeof(firstName),client);CoreDisplayName(1,secondName,sizeof(secondName),client);
        Check(BuildBaseAttackList(client,now,list,sizeof(list))==2 && StrContains(list,firstName)>=0 && StrContains(list,secondName)>=0,
            "simultaneous base alarms include every attacked base in one HUD channel");
        Check(BuildBaseAttackList(client,now+duration+0.1,list,sizeof(list))==0,"base alarms expire after the configured duration");
        gBuildings[second].alive=secondAlive;
    }
    for(int core=0;core<gCoreCount;core++)gBuildings[gCores[core].building].lastAttackAt=attacks[core];
    gBuildings[building]=saved;gEpoch=savedEpoch;gAdminMenuNonce[client]=savedAdminNonce;gDefenseMenuNonce[client]=savedDefenseNonce;
    strcopy(gAdminPendingToken[client],sizeof(gAdminPendingToken[]),savedPending);gAdminPendingUntil[client]=savedUntil;
    SetUserAdmin(client,savedAdmin,false);RemoveAdmin(temporary);TeleportEntity(client,savedOrigin,savedAngles,savedVelocity);
}
