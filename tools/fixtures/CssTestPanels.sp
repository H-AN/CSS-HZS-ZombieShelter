#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>

public Plugin myinfo={name="CSS test native panel closer",author="H-AN",description="Close native welcome/team/class panels for ordinary-client tests",version="1.0"};

public Action ClosePanels(int args)
{
    #pragma unused args
    int count;
    for(int client=1;client<=MaxClients;client++)
    {
        if(!IsClientInGame(client) || IsFakeClient(client) || IsClientSourceTV(client) || IsClientReplay(client))continue;
        ShowVGUIPanel(client,"info",null,false);
        ShowVGUIPanel(client,"team",null,false);
        ShowVGUIPanel(client,"class_ct",null,false);
        ShowVGUIPanel(client,"class_ter",null,false);
        count++;
    }
    PrintToServer("[CssTestPanels] closed=1 real_clients=%d panels=info,team,class_ct,class_ter",count);
    return Plugin_Handled;
}

public void OnPluginStart()
{
    RegServerCmd("sm_hzsh_test_close_panels",ClosePanels,"Close native panels for existing real clients only");
}
