// Only included in the test binary. Real client +attack is issued through its cfg.
int tLiveHits,tLiveDamageType,tLiveNode,tLiveBeforeInventory,tLiveBeforeQuota,tLiveRepairBefore,tLiveCollectorSerial;
int tLiveSavedInventory[MATERIALS],tLiveSavedSerial;
int tLiveToolSounds,tLiveWallSounds;
int tLiveSavedTeam[MATERIALS];
int tLiveGatherMaterial,tLiveGatherYield,tLiveGatherRef;
public void SetupLiveProbe(){RegServerCmd("zsh_live",LiveCommand);AddAmbientSoundHook(LiveAmbient);}
public Action LiveAmbient(char sample[PLATFORM_MAX_PATH],int &entity,float &volume,int &level,int &pitch,float pos[3],int &flags,float &delay)
{
    if(StrEqual(sample,"weapons/tfa_cso/tomahawk/wall.wav"))tLiveToolSounds++;
    if(StrEqual(sample,"physics/metal/metal_box_impact_hard1.wav"))tLiveWallSounds++;
    return Plugin_Continue;
}
public Action LiveTrace(int victim,int &attacker,int &inflictor,float &damage,int &type,int &ammo,int hitbox,int hitgroup)
{
    if(LiveHuman(attacker) && (FindNode(victim)>=0 || FindBuilding(victim)>=0) && KnifeDamage(attacker,type))
    {tLiveHits++;tLiveDamageType=type;PrintToServer("[LiveKnife] engine_hit=%d type=%d victim=%d",tLiveHits,type,victim);}
    return Plugin_Continue;
}
public Action LiveCommand(int args)
{
    int client;for(int i=1;i<=MaxClients;i++)if(LiveHuman(i) && !IsFakeClient(i)){client=i;break;}
    if(!client){PrintToServer("[Live] no living real client");return Plugin_Handled;}
    char which[32];GetCmdArg(1,which,sizeof(which));
    if(LiveManagementCommand(client,which))return Plugin_Handled;
    if(StrEqual(which,"closepanels"))
    {
        ShowVGUIPanel(client,"info",INVALID_HANDLE,false);
        ShowVGUIPanel(client,"team",INVALID_HANDLE,false);
        ShowVGUIPanel(client,"class_ct",INVALID_HANDLE,false);
        ShowVGUIPanel(client,"class_ter",INVALID_HANDLE,false);
        PrintToServer("[LiveUI] closed connection panels; inspect a client screenshot before UI tests");
    }
    else if(StrEqual(which,"resource"))
    {
        StartDay(false);gDeadline=GetGameTime()+600.0;tLiveNode=0;tLiveHits=0;
        for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].ready && !gNPCs[n].dead && gNPCs[n].boss<0)ParkNPC(n);
        gNextAdmissions=GetGameTime()+600.0;
        int ref=gNodes[0].ref;gNodes[0].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
        gNodes[0].material=0;gNodes[0].growing=false;gNodes[0].rerollPending=false;
        gNodes[0].elapsed=0.0;gNodes[0].remaining=gMaterials[0].quota;SpawnResourceBody(0);
        int e=Resolve(gNodes[0].ref);SDKHook(e,SDKHook_TraceAttack,LiveTrace);
        tLiveGatherMaterial=gNodes[0].material;tLiveGatherYield=gMaterials[tLiveGatherMaterial].yield;tLiveGatherRef=gNodes[0].ref;
        tLiveCollectorSerial=GetClientSerial(client);tLiveBeforeInventory=gPlayerInventory[client][tLiveGatherMaterial];tLiveBeforeQuota=gNodes[0].remaining;
        float center[3],p[3],maxs[3];EntityCenter(e,center);GetEntPropVector(e,Prop_Send,"m_vecMaxs",maxs);
        p=gNodes[0].origin;p[0]+=maxs[0]+28.0;p[2]+=2.0;Aim(client,p,center);SetEntityHealth(client,10000);
        int knife=GetPlayerWeaponSlot(client,2);if(knife<0)knife=GivePlayerItem(client,"weapon_knife");EquipPlayerWeapon(client,knife);
        PrintToServer("[Live] resource quota=%d inventory=%d hudmsg=%d",tLiveBeforeQuota,tLiveBeforeInventory,GetUserMessageId("HudMsg"));
    }
    else if(StrEqual(which,"verify_resource"))
    {
        int e=Resolve(gNodes[tLiveNode].ref),r,g,b,a;
        if(e<0){Check(false,"real client resource entity survives the depletion transition");return Plugin_Handled;}
        GetEntityRenderColor(e,r,g,b,a);int nextMaterial=gNodes[tLiveNode].material;
        char model[PLATFORM_MAX_PATH];GetEntPropString(e,Prop_Data,"m_ModelName",model,sizeof(model));
        float progress=(gNodes[tLiveNode].elapsed-gMaterials[nextMaterial].hold)/gMaterials[nextMaterial].grow;
        if(progress<0.0)progress=0.0;if(progress>1.0)progress=1.0;if(gMaterials[nextMaterial].ease)progress*=progress;
        int expectedAlpha=RoundToNearest(progress*255.0);
        PrintToServer("[LiveResource] engine_hits=%d type=%d remaining=%d growing=%d alpha=%d gained=%d expected=%d old_material=%s current_material=%s pending=%d elapsed=%.2f hold=%.2f grow=%.2f model=%s",
            tLiveHits,tLiveDamageType,gNodes[tLiveNode].remaining,gNodes[tLiveNode].growing,a,
            gPlayerInventory[client][tLiveGatherMaterial]-tLiveBeforeInventory,tLiveBeforeQuota*tLiveGatherYield,
            gMaterials[tLiveGatherMaterial].id,gMaterials[nextMaterial].id,gNodes[tLiveNode].rerollPending,
            gNodes[tLiveNode].elapsed,gMaterials[nextMaterial].hold,gMaterials[nextMaterial].grow,model);
        Check(GetClientSerial(client)==tLiveCollectorSerial && tLiveHits>=tLiveBeforeQuota && tLiveDamageType==(DMG_BULLET|DMG_NEVERGIB)
            && gNodes[tLiveNode].remaining==0 && gNodes[tLiveNode].growing && tLiveGatherYield==1
            && gPlayerInventory[client][tLiveGatherMaterial]-tLiveBeforeInventory==tLiveBeforeQuota*tLiveGatherYield,
            "real client engine knife grants the harvested cycle material per credited hit and depletes the resource");
        Check(!gNodes[tLiveNode].rerollPending && gNodes[tLiveNode].ref==tLiveGatherRef
            && StrEqual(model,gMaterials[nextMaterial].model,false) && a>=expectedAlpha-1 && a<=expectedAlpha+1,
            "real client depleted resource retains its entity and renders the selected next-cycle model and growth duration");
    }
    else if(StrEqual(which,"repair"))
    {
        int b=gCores[0].building;
        if(!gBuildings[b].alive){gBuildings[b].alive=true;SpawnBuildingBody(b);}
        gBuildings[b].hp=gBuildings[b].maxHP-30;tLiveRepairBefore=gBuildings[b].hp;tLiveHits=0;
        int e=Resolve(gBuildings[b].ref);SDKHook(e,SDKHook_TraceAttack,LiveTrace);
        float point[3],p[3],end[3];p=gCores[0].origin;p[0]-=160.0;p[1]-=400.0;p[2]+=45.0;
        end=p;end[1]+=500.0;Handle trace=TR_TraceRayFilterEx(p,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
        int hit=TR_GetEntityIndex(trace);TR_GetEndPosition(point,trace);delete trace;
        p=point;p[1]-=40.0;p[2]=gCores[0].origin[2]+2.0;Aim(client,p,point);SetEntityHealth(client,10000);
        PrintToServer("[LiveRepair] hp=%d/%d trace=%d body=%d aim=%d origin=%.1f %.1f %.1f",gBuildings[b].hp,gBuildings[b].maxHP,hit,e,AimedEntity(client,128.0),p[0],p[1],p[2]);
    }
    else if(StrEqual(which,"verify_repair"))
    {
        int b=gCores[0].building;PrintToServer("[LiveRepair] engine_hits=%d hp=%d/%d repaired=%d",tLiveHits,gBuildings[b].hp,gBuildings[b].maxHP,gBuildings[b].hp-tLiveRepairBefore);
        Check(tLiveHits>=3 && gBuildings[b].hp==gBuildings[b].maxHP,"real client knife repairs wall to full health");
    }
    else if(StrEqual(which,"base"))
    {
        char arg[8];GetCmdArg(2,arg,sizeof(arg));int core=StringToInt(arg);if(core<0 || core>=gCoreCount)core=0;
        TeleportEntity(client,gCorePoints[core][0],gCorePointAngles[core][0],view_as<float>({0.0,0.0,0.0}));SetEntityHealth(client,10000);
        for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=1000;
        PrintToServer("[LiveBase] core=%d level=%d hp=%d/%d",core,gBuildings[core].level,gBuildings[core].hp,gBuildings[core].maxHP);
    }
    else if(StrEqual(which,"hud"))
    {
        SetHudTextParams(0.02,0.6,4.0,0,255,0,255);int sent=ShowHudText(client,5,"HUD 通道测试");
        PrintToServer("[LiveHUD] message=%d channel_result=%d",GetUserMessageId("HudMsg"),sent);
    }
    else if(StrEqual(which,"audio"))
    {
        PrintToServer("[LiveAudio] tool=%d wall=%d",tLiveToolSounds,tLiveWallSounds);
        Check(tLiveToolSounds>0 && tLiveWallSounds>0,"engine emits original GMod tool hits and CSS breakable wall impact samples");
    }
    else if(StrEqual(which,"english") || StrEqual(which,"chinese"))
    {
        int language=GetLanguageByCode(StrEqual(which,"english") ? "en" : "chi");SetClientLanguage(client,language);gNextHUD=0.0;
        ShowBaseMenu(client,0);PrintToServer("[LiveLanguage] client=%d language=%d",client,language);
    }
    else if(StrEqual(which,"record_account"))
    {
        for(int m=0;m<MATERIALS;m++)tLiveSavedInventory[m]=gPlayerInventory[client][m];
        for(int m=0;m<MATERIALS;m++)
        {
            tLiveSavedTeam[m]=0;
            for(int c=0;c<gCoreCount;c++)tLiveSavedTeam[m]+=gBaseInventory[c][m]+SharedPendingAmount(c,m);
        }
        tLiveSavedSerial=GetClientSerial(client);PrintToServer("[LiveAccount] before serial=%d wood=%d iron=%d",tLiveSavedSerial,tLiveSavedInventory[0],tLiveSavedInventory[1]);
    }
    else if(StrEqual(which,"verify_account"))
    {
        bool equal=true;
        for(int m=0;m<MATERIALS;m++)
        {
            int total;for(int c=0;c<gCoreCount;c++)total+=gBaseInventory[c][m]+SharedPendingAmount(c,m);
            if(gPlayerInventory[client][m]!=0 || total!=tLiveSavedTeam[m]+tLiveSavedInventory[m])equal=false;
        }
        PrintToServer("[LiveAccount] after serial=%d wood=%d iron=%d",GetClientSerial(client),gPlayerInventory[client][0],gPlayerInventory[client][1]);
        Check(equal && tLiveSavedSerial!=GetClientSerial(client),"real disconnect shares the entire backpack exactly once and reconnect starts empty");
        if(tLiveDefense>=0)Check(gBuildings[tLiveDefense].ownerSerial==0 && CanManageBuilding(client,tLiveDefense),"real disconnect leaves its surviving defense unowned and manageable on reconnect");
    }
    return Plugin_Handled;
}
