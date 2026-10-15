// Isolated, synchronous real-client checks. No walls, clients or NPCs created.
void TestAdminTravelReal(int client)
{
    if(client<1 || !LiveHuman(client) || IsFakeClient(client) || !Running())
    {tSkipped++;PrintToServer("[ShelterTest] SKIP administrator travel checks need an existing living real client and running campaign");return;}
    int first=-1;
    for(int c=0;c<gCoreCount;c++)if(TravelBaseAlive(c)){first=c;break;}
    if(first<0){tSkipped++;PrintToServer("[ShelterTest] SKIP administrator travel requires an existing living base");return;}
    AdminId savedAdmin=GetUserAdmin(client),temporary=CreateAdmin("ShelterTravelProbe");
    SetAdminFlag(temporary,Admin_Root,true);SetUserAdmin(client,temporary,false);
    int savedNonce=gAdminMenuNonce[client],fields[AT_Count],base=gCores[first].building;
    char token[256];int uid=gBuildings[base].uid;
    AdminToken(client,Admin_Travel,token,sizeof(token),first,uid,gConfigRevision);
    Check(ValidateAdminToken(client,token,fields) && fields[AT_Subject]==first && fields[AT_Identity]==uid,
        "administrator travel token resolves its selected living base UID and current configuration revision");
    Check(!ValidateAdminToken(client,token,fields),"an administrator travel selection cannot replay its consumed nonce");
    AdminToken(client,Admin_Travel,token,sizeof(token),first,uid,gConfigRevision);gBuildings[base].uid++;
    Check(!ValidateAdminToken(client,token,fields),"a replaced base UID invalidates an existing administrator travel selection");gBuildings[base].uid=uid;
    AdminToken(client,Admin_Travel,token,sizeof(token),first,uid,gConfigRevision);gConfigRevision++;
    Check(!ValidateAdminToken(client,token,fields),"a different configuration revision invalidates administrator travel");gConfigRevision--;
    AdminToken(client,Admin_Travel,token,sizeof(token),first,uid,gConfigRevision);gEpoch++;
    Check(!ValidateAdminToken(client,token,fields),"a different campaign epoch invalidates administrator travel");gEpoch--;
    AdminToken(client,Admin_Travel,token,sizeof(token),first,uid,gConfigRevision);SetUserAdmin(client,INVALID_ADMIN_ID,false);
    if(!IsShelterAdmin(client))Check(!ValidateAdminToken(client,token,fields),"removing the actor's administrator permission invalidates an already-open travel token");
    else{tSkipped++;PrintToServer("[ShelterTest] SKIP administrator removal assertion: server override grants travel administrator access without AdminId");}
    SetUserAdmin(client,temporary,false);
    AdminToken(client,Admin_TravelList,token,sizeof(token),-1,0,gConfigRevision);
    Check(ValidateAdminToken(client,token,fields),"the administrator base-list entry is bound to its current revision");
    AdminToken(client,Admin_Travel,token,sizeof(token),first,uid,gConfigRevision);
    char extra[288];Format(extra,sizeof(extra),"%s:1",token);
    Check(!ValidateAdminToken(client,extra,fields),"an administrator travel token rejects an extra unbound field");
    float before[3],after[3],savedPoints[MAX_POINTS][3];GetClientAbsOrigin(client,before);
    int checked;
    // Make only the selected base's configured candidates invalid. Other bases
    // stay untouched; production must return false rather than select one.
    for(int core=0;core<gCoreCount;core++)
    {
        if(!TravelBaseAlive(core) || gCores[core].pointCount<1)continue;
        int count=gCores[core].pointCount;
        for(int p=0;p<count;p++){savedPoints[p]=gCorePoints[core][p];gCorePoints[core][p][0]=16000.0;}
        bool teleported=TeleportPlayerToBase(client,core);GetClientAbsOrigin(client,after);
        for(int p=0;p<count;p++)gCorePoints[core][p]=savedPoints[p];
        Check(!teleported && GetVectorDistance(before,after)<0.01,
            "a selected base with every configured safe point blocked returns false and never teleports to another base");
        PrintToServer("[TravelProbe] blocked_core=%d uid=%d candidates=%d result=%d displacement=%.3f",core,
            gBuildings[gCores[core].building].uid,count,teleported,GetVectorDistance(before,after));checked++;
    }
    Check(checked==AliveCores(),"fixed-base failure behavior was checked independently for every living configured base");
    gAdminMenuNonce[client]=savedNonce+1;SetUserAdmin(client,savedAdmin,false);RemoveAdmin(temporary);
    PrintToServer("[TravelProbe] real_client=%d checked_bases=%d; all candidate points and original administrator identity restored",client,checked);
}
