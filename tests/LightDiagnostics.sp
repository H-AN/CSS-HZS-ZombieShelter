#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
public Plugin myinfo={name="Temporary light transform observation",author="H-AN",version="1"};
public void OnPluginStart(){RegServerCmd("zsh_light_read",ReadLight);}
void LightVector(int entity,const char[] name)
{
    if(!HasEntProp(entity,Prop_Data,name)){PrintToServer("[LightDiag] %s absent",name);return;}
    float v[3];GetEntPropVector(entity,Prop_Data,name,v);PrintToServer("[LightDiag] %s %.4f %.4f %.4f",name,v[0],v[1],v[2]);
}
public Action ReadLight(int args)
{
    int entity=-1;
    while((entity=FindEntityByClassname(entity,"light_dynamic"))!=-1)
    {
        PrintToServer("[LightDiag] light=%d ref=%d",entity,EntIndexToEntRef(entity));
        LightVector(entity,"m_vecOrigin");LightVector(entity,"m_vecAbsOrigin");
        LightVector(entity,"m_angRotation");LightVector(entity,"m_angAbsRotation");
        char names[][]={"m_hMoveParent","moveparent","m_pParent"};
        for(int p=0;p<sizeof(names);p++)
        {
            bool send=HasEntProp(entity,Prop_Send,names[p]),data=HasEntProp(entity,Prop_Data,names[p]);
            int parent=send ? GetEntPropEnt(entity,Prop_Send,names[p]) : (data ? GetEntPropEnt(entity,Prop_Data,names[p]) : -1);
            PrintToServer("[LightDiag] %s send=%d data=%d parent=%d",names[p],send,data,parent);
            if(parent>0){LightVector(parent,"m_vecOrigin");LightVector(parent,"m_angRotation");}
        }
    }
    return Plugin_Handled;
}
