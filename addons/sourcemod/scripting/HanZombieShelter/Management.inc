bool gDismantleRefund=true,gAdminRefundToOwner=true;
float gDismantleFraction=0.5;

bool LoadManagementSettings()
{
    gDismantleRefund=Number(C_Management,"Dismantle","refund_enabled",1)!=0;
    gDismantleFraction=Decimal(C_Management,"Dismantle","refund_fraction",0.5);
    gAdminRefundToOwner=Number(C_Management,"Dismantle","admin_refund_to_owner",1)!=0;
    bool found;
    if(!StoredLevelInt(C_Management,"Limits","max_total_buildings",768,gSummonLimit,found))return ConfigFailure("max_total_buildings must be an integer");
    if(!found && Node(C_Management,"Limits") && gCfg[C_Management].GetDataType("max_managed_defenses")!=KvData_None)
    {
        gSummonLimit=Number(C_Management,"Limits","max_managed_defenses",96);
        LogMessage("[Shelter] migrate Limits/max_managed_defenses=%d to max_total_buildings (global logical defenses; bases excluded)",gSummonLimit);
    }
    return (gDismantleFraction>=0.0 && gDismantleFraction<=1.0 && gSummonLimit>=1
        && gSummonLimit<=MAX_BUILDINGS-MAX_CORES) || ConfigFailure("management refund ratio/defense count");
}

bool IsShelterAdmin(int client)
{
    return client>0 && client<=MaxClients && IsClientInGame(client)
        && CheckCommandAccess(client,"sm_zsh_admin",ADMFLAG_ROOT);
}

bool CanManageBuilding(int client,int b)
{
    if(client<1 || client>MaxClients || !IsClientInGame(client) || b<0 || b>=MAX_BUILDINGS
        || !gBuildings[b].used || !gBuildings[b].alive)return false;
    if(IsShelterAdmin(client) || gBuildings[b].kind==Kind_Base)return true;
    if(gBuildings[b].ownerSerial==0 && !gBuildings[b].ownerKey[0])return true;
    return gBuildings[b].ownerSerial==GetClientSerial(client)
        && StrEqual(gBuildings[b].ownerKey,gInventoryOwner[client]);
}

bool BuildingInUseRange(int client,int b)
{
    if(!LiveHuman(client) || b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive)return false;
    int entity=Resolve(gBuildings[b].ref);if(entity<0)return false;
    return AimedEntity(client,gUseDistance)==entity;
}

void AssignBuildingOwner(int b,int client)
{
    gBuildings[b].ownerSerial=GetClientSerial(client);
    strcopy(gBuildings[b].ownerKey,sizeof(gBuildings[].ownerKey),gInventoryOwner[client]);
}

int BuildingOwnerOnline(int b)
{
    if(b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used)return 0;
    int client=GetClientFromSerial(gBuildings[b].ownerSerial);
    return client>0 && IsClientInGame(client) && !IsFakeClient(client)
        && !IsClientSourceTV(client) && !IsClientReplay(client)
        && gBuildings[b].ownerKey[0] && StrEqual(gBuildings[b].ownerKey,gInventoryOwner[client]) ? client : 0;
}

bool TakeBuildingOwnership(int client,int b)
{
    if(!Running() || !LiveHuman(client) || IsFakeClient(client) || b<0 || b>=MAX_BUILDINGS
        || !gBuildings[b].used || !gBuildings[b].alive || gBuildings[b].kind!=Kind_Fence
        || BuildingOwnerOnline(b)>0 || !BuildingInUseRange(client,b))return false;
    AssignBuildingOwner(b,client);return true;
}

void AbandonClientBuildings(int client)
{
    int serial=GetClientSerial(client);
    for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].ownerSerial==serial)
    {gBuildings[b].ownerSerial=0;gBuildings[b].ownerKey[0]=0;}
}

void BuildingRefund(int b,int refund[MATERIALS])
{
    for(int m=0;m<gMaterialCount;m++)refund[m]=0;
    if(b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive || gBuildings[b].kind==Kind_Base || !gDismantleRefund)return;
    for(int m=0;m<gMaterialCount;m++)
    {
        refund[m]=RoundToFloor(float(gBuildings[b].invested[m])*gDismantleFraction);
        if(refund[m]>gBuildings[b].invested[m])refund[m]=gBuildings[b].invested[m];
    }
}

bool DismantleBuilding(int client,int b,bool adminAction=false)
{
    if(!Running() || b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive)return false;
    if(adminAction){if(!IsShelterAdmin(client))return false;}
    else
    {
        if(!LiveHuman(client) || !CanManageBuilding(client,b)){Tell(client,"%t","BuildingNotOwned");return false;}
        if(!BuildingInUseRange(client,b)){Tell(client,"%t","BuildingTooFar");return false;}
    }
    if(gBuildings[b].kind==Kind_Base)
    {
        if(!adminAction)return false;
        DamageBuilding(b,gBuildings[b].hp);Tell(client,"%t","BaseAdminDestroyed");return true;
    }
    int refund[MATERIALS];BuildingRefund(b,refund);
    int recipient=client;
    if(adminAction && gAdminRefundToOwner)
    {
        recipient=GetClientFromSerial(gBuildings[b].ownerSerial);
        if(recipient<1 || !IsClientInGame(recipient))recipient=0;
    }
    int maximum=gInventoryCap>0 ? gInventoryCap : 2000000000;
    if(recipient>0)for(int m=0;m<gMaterialCount;m++)
        if(refund[m]>maximum-gPlayerInventory[recipient][m]){Tell(client,"%t","DismantleNoRoom");return false;}
    // Commit consumption before removing the entity or publishing a refund.
    InvalidateDefenseCache(b);MarkBuildAreasDirty();gBuildings[b].alive=false;gBuildings[b].hp=0;
    RemoveBuildingEffect(b);
    for(int m=0;m<gMaterialCount;m++)gBuildings[b].invested[m]=0;
    int ref=gBuildings[b].ref;gBuildings[b].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
    if(recipient>0)Refund(refund,recipient);
    else
    {
        int core=gBuildings[b].buildCore;
        for(int m=0;m<gMaterialCount;m++)DepositSharedAutomatic(core,m,refund[m]);
    }
    RecalculatePower();Tell(client,"%t","BuildingDismantled");return true;
}

bool AdminSetBuildingLevel(int client,int b,int level)
{
    if(!IsShelterAdmin(client) || !Running() || b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used
        || !gBuildings[b].alive || Resolve(gBuildings[b].ref)<0 || !BuildingLevelAvailable(b,level))return false;
    Building previous;previous=gBuildings[b];gBuildings[b].level=level;
    if(!ApplyBuildingLevel(b,false)){gBuildings[b]=previous;return false;}
    gBuildings[b].upgraded=level>1;
    // Admin changes are free and never manufacture refundable investment.
    LogAction(client,-1,"[Shelter] admin set building UID %d level %d -> %d",gBuildings[b].uid,previous.level,level);
    return true;
}

bool AdminSetBaseMaterial(int client,int core,int material,int value)
{
    if(!IsShelterAdmin(client) || !Running() || core<0 || core>=gCoreCount || material<0 || material>=gMaterialCount || value<0)return false;
    int maximum=StorageCapacity();if(value>maximum)return false;
    int before=gBaseInventory[core][material];gBaseInventory[core][material]=value;
    FlushSharedOverflow();LogAction(client,-1,"[Shelter] base %d material %d: %d -> %d",core,material,before,value);return true;
}

bool AdminSetPlayerMaterial(int client,int targetSerial,int material,int value)
{
    int target=GetClientFromSerial(targetSerial);
    if(!IsShelterAdmin(client) || !Running() || target<1 || !IsClientInGame(target) || IsFakeClient(target)
        || material<0 || material>=gMaterialCount || value<0)return false;
    int maximum=gInventoryCap>0 ? gInventoryCap : 2000000000;if(value>maximum)return false;
    int before=gPlayerInventory[target][material];gPlayerInventory[target][material]=value;
    LogAction(client,target,"[Shelter] player material %d: %d -> %d",material,before,value);return true;
}

void RequestDayReset()
{
    if(!Running() || gTerminal || gOutcomeQueued)return;
    Phase previous=gPhase;
    gPhase=Phase_Ending;gSnapshot=true;gRollback=true;gWarningSent=false;gTerminationExpected=true;
    ClearGrenades();StopPowerEffects();NotifyPhaseChanged(previous,0.0);
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].ready && !gNPCs[n].dead)
    {
        int entity=Resolve(gNPCs[n].ref);if(entity<0)continue;
        if(gNPCs[n].boss>=0)
        {
            int boss=gNPCs[n].boss;if(!gBosses[boss].defeated)gBosses[boss].savedHP=GetEntProp(entity,Prop_Data,"m_iHealth");
            FreezeNPC(n);
        }
        else ParkNPC(n);
    }
    RecoverBases();AnnouncePhrase("DayResetByAdmin",gDay);CS_TerminateRound(1.0,CSRoundEnd_Draw,true);
}
