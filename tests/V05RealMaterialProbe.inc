// Test-only real-client path. Uses the existing native NPC pool; never creates clients/NPCs.
bool tV05RealMaterialBusy;
MaterialDef tV05RealMaterialCatalog[MATERIALS];
int tV05RealMaterialPersonal[MATERIALS],tV05RealMaterialBases[MAX_CORES][MATERIALS];

void RunV05RealMaterialTests(int client)
{
    if(client<1 || !LiveHuman(client) || IsFakeClient(client) || !Running() || tV05RealMaterialBusy)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP v05_real_materials requires a running campaign and living real client; no client or NPC is created");return;}
    int coreMaterial=FindMaterial("plague_core");
    if(coreMaterial<0 || gCoreCount<1 || gCores[0].building<0)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP v05_real_materials requires a registered plague_core and initialized base");return;}
    tV05RealMaterialBusy=true;
    int count=gMaterialCount,capacity=gInventoryCap,nonce=gBaseMenuNonce[client];
    float origin[3],angles[3],velocity[3];GetClientAbsOrigin(client,origin);GetClientEyeAngles(client,angles);GetEntPropVector(client,Prop_Data,"m_vecVelocity",velocity);
    for(int m=0;m<MATERIALS;m++){tV05RealMaterialCatalog[m]=gMaterials[m];tV05RealMaterialPersonal[m]=gPlayerInventory[client][m];}
    for(int c=0;c<gCoreCount;c++)for(int m=0;m<MATERIALS;m++)tV05RealMaterialBases[c][m]=gBaseInventory[c][m];
    ArrayList overflow=gSharedOverflow;gSharedOverflow=new ArrayList(3);
    char storageCapacity[32];Text(C_Resources,"BaseStorage","capacity",storageCapacity,sizeof(storageCapacity),"0");
    Node(C_Resources,"BaseStorage");gCfg[C_Resources].SetNum("capacity",0);gInventoryCap=0;
    TeleportEntity(client,gCorePoints[0][0],gCorePointAngles[0][0],view_as<float>({0.0,0.0,0.0}));

    int cost[MATERIALS];cost[coreMaterial]=3;gPlayerInventory[client][coreMaterial]=5;
    Check(Spend(cost,client) && gPlayerInventory[client][coreMaterial]==2,"real client Spend deducts the added plague core material exactly");
    Refund(cost,client);
    Check(gPlayerInventory[client][coreMaterial]==5,"real client Refund restores every spent plague core without changing another material");
    cost[coreMaterial]=6;
    Check(!Spend(cost,client) && gPlayerInventory[client][coreMaterial]==5,"real client insufficient core cost rejects the entire spend atomically");

    // Temporarily exercise an index above the old menu's colliding action ranges.
    while(gMaterialCount<12)
    {
        MaterialDef extra;Format(extra.id,sizeof(extra.id),"v05_token_%d",gMaterialCount);
        strcopy(extra.label,sizeof(extra.label),extra.id);gMaterials[gMaterialCount++]=extra;
    }
    int selected=11;gPlayerInventory[client][selected]=50;gBaseInventory[0][selected]=20;
    bool storageAvailable=CanManageCore(client,0);
    Check(storageAvailable,"real client reaches base storage inside its original green safe zone");
    if(storageAvailable)
    {
        char token[192],stale[192];int decodedCore,operation,material,quantity;
        BaseMenuToken(client,0,6,token,sizeof(token),gMaterials[selected].id,10);
        Check(ValidateBaseMenuToken(client,token,decodedCore,operation,material,quantity)
            && decodedCore==0 && operation==6 && material==selected && quantity==10,"real client storage token decodes action, material index above ten, and quantity independently");
        Check(TransferBaseMaterial(client,decodedCore,material,quantity,true)
            && gPlayerInventory[client][selected]==40 && gBaseInventory[0][selected]==30,"real client deposits a material above index ten through the actual shared-storage operation");
        Check(!ValidateBaseMenuToken(client,token,decodedCore,operation,material,quantity)
            && gPlayerInventory[client][selected]==40 && gBaseInventory[0][selected]==30,"replaying the consumed high-material transaction does not deduct twice");
        BaseMenuToken(client,0,7,token,sizeof(token),gMaterials[selected].id,10);
        int separator=FindCharInString(token,':');Format(stale,sizeof(stale),"%d%s",gEpoch-1,token[separator]);
        Check(!ValidateBaseMenuToken(client,stale,decodedCore,operation,material,quantity),"real client rejects a previous-epoch storage transaction");
        Check(ValidateBaseMenuToken(client,token,decodedCore,operation,material,quantity)
            && operation==7 && material==selected && TransferBaseMaterial(client,decodedCore,material,quantity,false)
            && gPlayerInventory[client][selected]==50 && gBaseInventory[0][selected]==20,"real client withdraws the high-index material and restores the original balances");
    }

    // Kill exactly one existing ordinary native NPC. Do not manufacture a pool member.
    int target=-1;
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && gNPCs[n].ready && !gNPCs[n].dead && gNPCs[n].boss<0 && Resolve(gNPCs[n].ref)>0)
    {target=n;break;}
    if(target<0 || !APIAvailable())
    {tSkipped++;PrintToServer("[ShelterTest] SKIP v05_real_materials native death: no existing ordinary native NPC is ready");}
    else
    {
        int entity=Resolve(gNPCs[target].ref),ref=gNPCs[target].ref,kills=gNativeKills;
        KeyValues previousDrops=gCfg[C_ZombieDrops],fixture=new KeyValues("ShelterZombieDrops");fixture.SetNum("schema_version",2);
        char name[128];bool named=Han_GetZombieName(entity,name,sizeof(name));
        if(named && name[0])
        {
            DropProbeRule(fixture,"real_native_core","plague_core","1","1",name);
            gCfg[C_ZombieDrops]=fixture;ReadZombieDrops();
            for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used)CacheNPCDrops(n,gNPCs[n].publicName);
            int before=gPlayerInventory[client][coreMaterial];
            ParkNPC(target);
            Check(!gNPCs[target].active && gPlayerInventory[client][coreMaterial]==before && gNativeKills==kills,"parking an existing native NPC performs cleanup without kill reward or bridge count");
            bool admitted=ActivateOrdinary(target);
            Check(admitted && gNPCs[target].ref==ref,"an existing parked ordinary NPC is admitted without creating or respawning an entity");
            if(admitted)
            {
                // Keep the native/configured HP; the public HZS API performs the death.
                int health=GetEntProp(entity,Prop_Data,"m_iHealth");
                bool damaged=health>0 && health<100000 && DamageNPC(target,health+1,GetClientSerial(client));
                Check(damaged && gNPCs[target].dead && gNPCs[target].settled && gNativeKills==kills+1,"one real HZS safe-damage call consumes one ordinary native death and one bridge kill");
                Check(gPlayerInventory[client][coreMaterial]==before+1,"a real native death commits its guaranteed plague core to the actual human killer");
                // HZS may synchronously send repeated notifications; consume the actual ref once.
                Han_OnZombieDeath(entity,client);
                Check(gPlayerInventory[client][coreMaterial]==before+1 && gNativeKills==kills+1,"a repeated notification for the actual native death cannot grant or count twice");
                PrintToServer("[ShelterRealMaterial] client_serial=%d npc_ref=%d original_hp=%d actual_death=%d bridge_delta=%d core_delta=%d",
                    GetClientSerial(client),ref,health,damaged && gNPCs[target].dead,gNativeKills-kills,gPlayerInventory[client][coreMaterial]-before);
            }
        }
        else{tSkipped++;PrintToServer("[ShelterTest] SKIP v05_real_materials native death: public name unavailable");}
        gCfg[C_ZombieDrops]=previousDrops;delete fixture;ReadZombieDrops();
        for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used)CacheNPCDrops(n,gNPCs[n].publicName);
    }
    tSkipped++;PrintToServer("[ShelterTest] SKIP v05_real_materials terminal Boss death: no Boss is killed/recreated solely to restore this single-client fixture");

    // The one native death is intentional and cannot be resurrected. Restore configuration,
    // inventories and menu/position state; its real bridge count is retained.
    gMaterialCount=count;gInventoryCap=capacity;gBaseMenuNonce[client]=nonce;
    for(int m=0;m<MATERIALS;m++){gMaterials[m]=tV05RealMaterialCatalog[m];gPlayerInventory[client][m]=tV05RealMaterialPersonal[m];}
    for(int c=0;c<gCoreCount;c++)for(int m=0;m<MATERIALS;m++)gBaseInventory[c][m]=tV05RealMaterialBases[c][m];
    delete gSharedOverflow;gSharedOverflow=overflow;Node(C_Resources,"BaseStorage");gCfg[C_Resources].SetString("capacity",storageCapacity);
    TeleportEntity(client,origin,angles,velocity);tV05RealMaterialBusy=false;
    PrintToServer("[ShelterRealMaterial] fixture restored; ordinary pool may contain the one actual death; no BOT, fake client, NPC creation or revival used");
}
