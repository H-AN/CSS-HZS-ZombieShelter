#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#include <HanZombieShelterAPI>
public Action Snapshot(int args)
{
    #pragma unused args
    int humans,bots;
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client))
    {
        if(IsFakeClient(client)){bots++;continue;}humans++;
        int knife=GetPlayerWeaponSlot(client,2);char classname[64];if(knife>MaxClients && IsValidEntity(knife))GetEntityClassname(knife,classname,sizeof(classname));
        PrintToServer("[V083Final] userid=%d serial=%d team=%d alive=%d god=%d takedamage=%d move=%d hp=%d armor=%d slot2_class=%s",
            GetClientUserId(client),GetClientSerial(client),GetClientTeam(client),IsPlayerAlive(client),(GetEntityFlags(client)&FL_GODMODE)!=0,
            GetEntProp(client,Prop_Data,"m_takedamage"),GetEntityMoveType(client),GetClientHealth(client),GetClientArmor(client),classname);
        for(int m=0;m<HZSH_GetMaterialCount();m++)
        {char id[64];int amount;HZSH_GetMaterialId(m,id,sizeof(id));HZSH_GetPlayerMaterialAmount(client,id,amount);PrintToServer("[V083Final] material=%s amount=%d",id,amount);}
    }
    int buildings,axes,lights;
    for(int entity=MaxClients+1;entity<GetMaxEntities();entity++)if(IsValidEntity(entity))
    {
        char classname[64];GetEntityClassname(entity,classname,sizeof(classname));if(StrEqual(classname,"weapon_axe"))axes++;
        if(StrEqual(classname,"light_dynamic"))lights++;
        if(HasEntProp(entity,Prop_Data,"m_iName")){char name[128];GetEntPropString(entity,Prop_Data,"m_iName",name,sizeof(name));if(StrContains(name,"hzsh_build_")==0)buildings++;}
    }
    PrintToServer("[V083Final] running=%d humans=%d bots=%d logical_bases=%d owned_build_bodies=%d axe_entities=%d dynamic_lights_all=%d entity_count=%d tick_interval=%.9f; read_only=1",
        HZSH_IsRunning(),humans,bots,HZSH_GetBaseCount(),buildings,axes,lights,GetEntityCount(),GetTickInterval());return Plugin_Handled;
}
public void OnPluginStart(){RegServerCmd("sm_v083_final_snapshot",Snapshot);}
