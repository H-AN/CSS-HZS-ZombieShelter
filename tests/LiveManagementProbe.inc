// Manual-input fixtures only: no production entry points are added by this file.
int tLiveDefense=-1,tLiveRefundBefore,tLiveRefundExpected;
AdminId tLiveOriginalAdmin=INVALID_ADMIN_ID,tLiveTemporaryAdmin=INVALID_ADMIN_ID;

bool LiveManagementCommand(int client,const char[] which)
{
    if(StrEqual(which,"clear_fixture"))
    {
        if(tLiveDefense>=0 && gBuildings[tLiveDefense].alive)DamageBuilding(tLiveDefense,gBuildings[tLiveDefense].hp);
        tLiveDefense=-1;
    }
    else if(StrEqual(which,"alerts"))
    {
        DamageBuilding(gCores[0].building,1,true);DamageBuilding(gCores[1].building,1,true);
        Node(C_Mode,"HUD");gCfg[C_Mode].SetFloat("base_attack_alert_seconds",30.0);gNextHUD=0.0;
        Check(BuildingUnderAttack(gCores[0].building,GetGameTime()) && BuildingUnderAttack(gCores[1].building,GetGameTime()),"two simultaneous base attacks are visible to the shared alert channel");
    }
    else if(StrEqual(which,"admin_on"))
    {
        if(tLiveTemporaryAdmin==INVALID_ADMIN_ID)
        {
            tLiveOriginalAdmin=GetUserAdmin(client);tLiveTemporaryAdmin=CreateAdmin("ShelterLiveProbe");
            SetAdminFlag(tLiveTemporaryAdmin,Admin_Root,true);SetUserAdmin(client,tLiveTemporaryAdmin,false);
        }
        Check(IsShelterAdmin(client),"live fixture temporarily receives ROOT permission");
    }
    else if(StrEqual(which,"admin_off"))
    {
        SetUserAdmin(client,tLiveOriginalAdmin,false);
        if(tLiveTemporaryAdmin!=INVALID_ADMIN_ID)RemoveAdmin(tLiveTemporaryAdmin);
        tLiveTemporaryAdmin=INVALID_ADMIN_ID;
    }
    else if(StrEqual(which,"defense") || StrEqual(which,"amplifier"))
    {
        int recipe=0;
        if(StrEqual(which,"amplifier"))for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Amplifier){recipe=r;break;}
        // Existing base collider has an open strip along its southern side.
        float origin[3];origin=gCores[0].origin;origin[0]-=160.0;origin[1]-=280.0;origin[2]+=2.0;
        if(tLiveDefense>=0 && gBuildings[tLiveDefense].alive)DamageBuilding(tLiveDefense,gBuildings[tLiveDefense].hp);
        tLiveDefense=TestBuild(recipe,origin);
        Check(tLiveDefense>=0,"live interaction fixture creates a real defense collider");if(tLiveDefense<0)return true;
        AssignBuildingOwner(tLiveDefense,client);gBuildings[tLiveDefense].invested=gRecipes[recipe].cost;
        gBuildings[tLiveDefense].nextFire=GetGameTime()+600.0;
        for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=1000;
        LiveAimDefense(client,false);gNextUse[client]=0.0;
    }
    else if(StrEqual(which,"aim_defense"))LiveAimDefense(client);
    else if(StrEqual(which,"verify_upgrade"))
    {
        Check(tLiveDefense>=0 && gBuildings[tLiveDefense].level==2,"physical E and numeric menu input upgrade the aimed owned defense");
    }
    else if(StrEqual(which,"record_refund"))
    {
        int refund[MATERIALS];BuildingRefund(tLiveDefense,refund);tLiveRefundBefore=gPlayerInventory[client][0];tLiveRefundExpected=refund[0];
    }
    else if(StrEqual(which,"verify_dismantle"))
    {
        Check(!gBuildings[tLiveDefense].alive && Resolve(gBuildings[tLiveDefense].ref)<0
            && gPlayerInventory[client][0]==tLiveRefundBefore+tLiveRefundExpected,"physical dismantle confirmation removes the body and refunds the quoted investment once");
    }
    else if(StrEqual(which,"verify_admin_material"))
    {
        Check(gPlayerInventory[client][0]==1010,"physical administrator player selection and +10 changes only the intended real player's material");
    }
    else if(StrEqual(which,"budget"))
    {
        int saved=gEntityConstructionCeiling;gEntityConstructionCeiling=GetEntityCount();
        int before=gPlayerInventory[client][0],entities=GetEntityCount();
        Check(!BuildRecipe(client,0) && before==gPlayerInventory[client][0] && GetEntityCount()==entities,"live full-budget feedback refuses construction without consuming materials or spawning an entity");
        gEntityConstructionCeiling=saved;
    }
    else return false;
    return true;
}

void LiveAimDefense(int client,bool verify=true)
{
    if(tLiveDefense<0 || !gBuildings[tLiveDefense].alive)return;
    int entity=Resolve(gBuildings[tLiveDefense].ref);if(entity<0)return;
    float center[3],position[3];EntityCenter(entity,center);position=gBuildings[tLiveDefense].origin;
    position[1]-=65.0;Aim(client,position,center);
    if(verify)Check(InSafeBase(position) && AimedEntity(client,gUseDistance)==entity,"live aim reaches its defense within use range while standing in the base safety zone");
}
