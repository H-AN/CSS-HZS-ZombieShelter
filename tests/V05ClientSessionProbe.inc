// Session observation only. No client is created and nobody is respawned.
bool tV05ExternalDay;

public Action V05ClientSessionCommand(int args)
{
    char action[32];GetCmdArg(1,action,sizeof(action));
    if(StrEqual(action,"status"))
    {
        int realClients,alive,bots;
        for(int i=1;i<=MaxClients;i++)if(IsClientInGame(i))
        {
            if(IsFakeClient(i)){bots++;continue;}
            if(IsClientSourceTV(i) || IsClientReplay(i))continue;
            realClients++;if(LiveHuman(i))alive++;
        }
        int npcs=APIAvailable() ? Han_GetZombieCount() : 0;
        PrintToServer("[V05ClientSession] real_clients=%d alive_real=%d bots=%d hzs_npcs=%d phase=%d running=%d external_day_fixture=%d",
            realClients,alive,bots,npcs,gPhase,Running(),tV05ExternalDay);return Plugin_Handled;
    }
    int client=TestHuman();
    if(client<1){PrintToServer("[V05ClientSession] SKIP no living real client");return Plugin_Handled;}
    if(StrEqual(action,"closepanels"))
    {
        ShowVGUIPanel(client,"info",INVALID_HANDLE,false);ShowVGUIPanel(client,"team",INVALID_HANDLE,false);
        ShowVGUIPanel(client,"class_ct",INVALID_HANDLE,false);ShowVGUIPanel(client,"class_ter",INVALID_HANDLE,false);
        PrintToServer("[V05ClientSession] connection panels closed for existing real client");
    }
    else if(StrEqual(action,"external_day"))
    {
        if(Running()){PrintToServer("[V05ClientSession] already running; no fixture applied");return Plugin_Handled;}
        if(!gEnabled || !gRoundPrepared){PrintToServer("[V05ClientSession] FAIL external map world is not prepared");return Plugin_Handled;}
        tV05ExternalDay=true;StartDay(false);
        PrintToServer("[V05ClientSession] external StartDay fixture only; HZS natural startup and native NPC combat are NOT verified");
    }
    else{Check(false,"unknown v05 client-session action");}
    return Plugin_Handled;
}
