#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#include <HanZombieShelterAPI>

public Plugin myinfo={name="v0.8.2 temporary close-aim fixture",author="H-AN",description="Position one real tester for actual melee; no simulated damage",version="1.0"};

public bool V082CloseFilter(int entity,int mask,any client)
{
    #pragma unused mask
    return entity!=client;
}

public Action V082CloseAimCommand(int args)
{
    if(args!=1){PrintToServer("[V082CloseAim] usage: sm_v082_close_aim <exact building UID>");return Plugin_Handled;}
    char text[32],prefix[64];GetCmdArg(1,text,sizeof(text));int uid=StringToInt(text);
    if(uid<1)return Plugin_Handled;
    int actor,count;
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client) && !IsFakeClient(client) && IsPlayerAlive(client))
    {actor=client;count++;}
    if(count!=1){PrintToServer("[V082CloseAim] requires one existing living real client");return Plugin_Handled;}
    Format(prefix,sizeof(prefix),"hzsh_build_%d_",uid);int body,found;
    for(int entity=MaxClients+1;entity<GetMaxEntities();entity++)if(IsValidEntity(entity) && HasEntProp(entity,Prop_Data,"m_iName"))
    {
        char name[128],model[PLATFORM_MAX_PATH];GetEntPropString(entity,Prop_Data,"m_iName",name,sizeof(name));
        if(StrContains(name,prefix)!=0 || !HasEntProp(entity,Prop_Data,"m_ModelName"))continue;
        GetEntPropString(entity,Prop_Data,"m_ModelName",model,sizeof(model));
        if(!StrEqual(model,"models/props/de_piranesi/pi_sundial.mdl"))continue;
        body=entity;found++;
    }
    if(found!=1){PrintToServer("[V082CloseAim] exact owned sundial entity not unique");return Plugin_Handled;}
    float origin[3],old[3],oldAngles[3],oldVelocity[3],zero[3],playerMins[3]={-16.0,-16.0,0.0},playerMaxs[3]={16.0,16.0,72.0};
    GetEntPropVector(body,Prop_Data,"m_vecOrigin",origin);GetClientAbsOrigin(actor,old);GetClientEyeAngles(actor,oldAngles);
    GetEntPropVector(actor,Prop_Data,"m_vecAbsVelocity",oldVelocity);
    for(int ring=0;ring<3;ring++)for(int direction=0;direction<16;direction++)for(int height=0;height<4;height++)
    {
        float feet[3],target[3],eye[3],delta[3],angles[3],angle=float(direction)*6.2831853/16.0;
        feet=origin;feet[0]+=Cosine(angle)*(60.0+float(ring)*8.0);feet[1]+=Sine(angle)*(60.0+float(ring)*8.0);feet[2]=old[2];
        Handle hull=TR_TraceHullFilterEx(feet,feet,playerMins,playerMaxs,MASK_PLAYERSOLID,V082CloseFilter,actor);
        bool clear=!TR_DidHit(hull) && !TR_StartSolid(hull) && !TR_AllSolid(hull);delete hull;if(!clear)continue;
        target=origin;target[2]+=44.0+float(height)*8.0;
        TeleportEntity(actor,feet,NULL_VECTOR,zero);GetClientEyePosition(actor,eye);MakeVectorFromPoints(eye,target,delta);GetVectorAngles(delta,angles);
        TeleportEntity(actor,NULL_VECTOR,angles,zero);
        Handle ray=TR_TraceRayFilterEx(eye,target,MASK_SOLID,RayType_EndPoint,V082CloseFilter,actor);
        float point[3];TR_GetEndPosition(point,ray);bool hit=TR_DidHit(ray) && TR_GetEntityIndex(ray)==body;
        float distance=GetVectorDistance(eye,point);delete ray;
        if(hit && distance<=55.0)
        {
            PrintToServer("[V082CloseAim] entity=%d uid=%d real_eye_hit_distance=%.3f feet=%.3f,%.3f,%.3f angles=%.3f,%.3f,%.3f; no damage or repair invoked",
                body,uid,distance,feet[0],feet[1],feet[2],angles[0],angles[1],angles[2]);return Plugin_Handled;
        }
    }
    TeleportEntity(actor,old,oldAngles,oldVelocity);PrintToServer("[V082CloseAim] no close supported hull/ray; original transform restored");return Plugin_Handled;
}

public Action V082EquipKnifeCommand(int args)
{
    #pragma unused args
    int actor,count;
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client) && !IsFakeClient(client) && IsPlayerAlive(client))
    {actor=client;count++;}
    if(count!=1)return Plugin_Handled;
    int weapon=GetPlayerWeaponSlot(actor,2);
    if(weapon<=MaxClients || !IsValidEntity(weapon) || GetEntPropEnt(weapon,Prop_Send,"m_hOwnerEntity")!=actor)return Plugin_Handled;
    int before=GetEntPropEnt(actor,Prop_Send,"m_hActiveWeapon");EquipPlayerWeapon(actor,weapon);
    char classname[128];GetEntityClassname(weapon,classname,sizeof(classname));
    PrintToServer("[V082CloseAim] DIAGNOSTIC EquipPlayerWeapon existing owned slot2=%d class=%s before=%d after=%d; no weapon created",
        weapon,classname,before,GetEntPropEnt(actor,Prop_Send,"m_hActiveWeapon"));return Plugin_Handled;
}

public Action V082ResumeFormalCommand(int args)
{
    #pragma unused args
    if(HZSH_IsRunning()){PrintToServer("[V082Resume] already running; no callback replayed");return Plugin_Handled;}
    Handle main=FindPluginByFile("HanZombieShelter.smx");
    Function start=main!=null ? GetFunctionByName(main,"Han_OnGameStart") : INVALID_FUNCTION;
    if(start==INVALID_FUNCTION){PrintToServer("[V082Resume] provider callback unavailable");return Plugin_Handled;}
    Call_StartFunction(main,start);Call_Finish();
    PrintToServer("[V082Resume] explicit existing-round callback replay after formal plugin hot reload; running=%d; no server restart, client or NPC respawn command",HZSH_IsRunning());
    return Plugin_Handled;
}

public Action V082CleanupWeaponsCommand(int args)
{
    if(args!=1)return Plugin_Handled;
    char raw[32],prefix[64];GetCmdArg(1,raw,sizeof(raw));int uid=StringToInt(raw);
    if(uid<1)return Plugin_Handled;
    int actor,count,body;float center[3];
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client) && !IsFakeClient(client) && IsPlayerAlive(client))
    {actor=client;count++;}
    if(count!=1)return Plugin_Handled;
    Format(prefix,sizeof(prefix),"hzsh_build_%d_",uid);
    for(int entity=MaxClients+1;entity<GetMaxEntities();entity++)if(IsValidEntity(entity) && HasEntProp(entity,Prop_Data,"m_iName"))
    {
        char name[128],model[PLATFORM_MAX_PATH];GetEntPropString(entity,Prop_Data,"m_iName",name,sizeof(name));
        if(StrContains(name,prefix)!=0 || !HasEntProp(entity,Prop_Data,"m_ModelName"))continue;
        GetEntPropString(entity,Prop_Data,"m_ModelName",model,sizeof(model));
        if(StrEqual(model,"models/props/de_piranesi/pi_sundial.mdl")){body=entity;GetEntPropVector(body,Prop_Data,"m_vecOrigin",center);break;}
    }
    if(body<1)return Plugin_Handled;
    static const char allowed[][]={"weapon_axe","weapon_beijixing","weapon_beijixing2","weapon_chongsu","weapon_xintiao","weapon_xintiao2",
        "weapon_lianming","weapon_longya","weapon_zhuixing","weapon_wange","weapon_apexshuangren"};
    int removed;
    for(int entity=MaxClients+1;entity<GetMaxEntities();entity++)if(IsValidEntity(entity))
    {
        char classname[128];GetEntityClassname(entity,classname,sizeof(classname));bool matched;
        for(int item=0;item<sizeof(allowed);item++)if(StrEqual(classname,allowed[item])){matched=true;break;}
        if(!matched || !HasEntProp(entity,Prop_Send,"m_hOwnerEntity"))continue;
        int owner=GetEntPropEnt(entity,Prop_Send,"m_hOwnerEntity");
        if(owner>0 && owner!=actor)continue;
        float position[3];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",position);
        if(owner!=actor && GetVectorDistance(position,center)>512.0)continue;
        PrintToServer("[V082Cleanup] exact test-tool class=%s ref=%d owner=%d removed near fixture UID=%d",classname,EntIndexToEntRef(entity),owner,uid);
        if(owner==actor)RemovePlayerItem(actor,entity);
        if(IsValidEntity(entity))RemoveEntity(entity);removed++;
    }
    int knife=GetPlayerWeaponSlot(actor,2);bool restored;
    if(knife<=MaxClients || !IsValidEntity(knife)){knife=GivePlayerItem(actor,"weapon_knife");restored=knife>MaxClients;}
    PrintToServer("[V082Cleanup] removed_test_tools=%d default_knife=%d starter_tool_restored=%d; no player/NPC respawn",removed,knife,restored);
    return Plugin_Handled;
}

public Action V082FinalStateCommand(int args)
{
    #pragma unused args
    int humans,bots;
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client))
    {
        if(IsFakeClient(client)){bots++;continue;}humans++;
        PrintToServer("[V082Final] userid=%d alive=%d god=%d takedamage=%d move=%d hp=%d armor=%d",
            GetClientUserId(client),IsPlayerAlive(client),(GetEntityFlags(client)&FL_GODMODE)!=0,
            GetEntProp(client,Prop_Data,"m_takedamage"),GetEntityMoveType(client),GetClientHealth(client),GetClientArmor(client));
    }
    PrintToServer("[V082Final] running=%d humans=%d bots=%d; read-only snapshot",HZSH_IsRunning(),humans,bots);
    return Plugin_Handled;
}

public void OnPluginStart()
{
    RegServerCmd("sm_v082_close_aim",V082CloseAimCommand);
    RegServerCmd("sm_v082_equip_knife",V082EquipKnifeCommand);
    RegServerCmd("sm_v082_resume_formal",V082ResumeFormalCommand);
    RegServerCmd("sm_v082_cleanup_weapons",V082CleanupWeaponsCommand);
    RegServerCmd("sm_v082_final_state",V082FinalStateCommand);
}
