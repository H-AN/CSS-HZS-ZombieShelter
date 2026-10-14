#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#include <HanZombieScenarioAPI>

public Plugin myinfo = {name="Shelter NPC read-only audit", author="Codex", description="Temporary read-only entity health report", version="1.0"};
public void OnPluginStart() { RegAdminCmd("sm_zsh_npc_audit", Audit, ADMFLAG_ROOT); }
public Action Audit(int client, int args)
{
    int count=Han_GetZombieCount();
    ReplyToCommand(client,"[NPCAudit] registered=%d",count);
    for(int i=0;i<count;i++)
    {
        int ent=Han_GetZombieByIndex(i); if(ent<=MaxClients || !IsValidEntity(ent))continue;
        char name[96]; Han_GetZombieName(ent,name,sizeof(name));
        float origin[3]; GetEntPropVector(ent,Prop_Data,"m_vecOrigin",origin);
        ReplyToCommand(client,"[NPCAudit] ent=%d hp=%d damageable=%d pos=%.1f %.1f %.1f name=%s",ent,GetEntProp(ent,Prop_Data,"m_iHealth"),GetEntProp(ent,Prop_Data,"m_takedamage"),origin[0],origin[1],origin[2],name);
    }
    return Plugin_Handled;
}
