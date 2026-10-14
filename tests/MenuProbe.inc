// Included only by ShelterIntegrationProbe.sp. No timers or persistent world changes.
void TestBaseMenus(int client)
{
    Check(LiveHuman(client) && Running() && gCoreCount>=3,"base menu fixture has a running A/B/C campaign");
    if(!LiveHuman(client) || !Running() || gCoreCount<3)return;
    for(int core=0;core<gCoreCount;core++)
    {
        int body=gCores[core].building;
        if(body<0 || body>=MAX_BUILDINGS || !gBuildings[body].used)
        {
            Check(false,"base menu fixture has every base snapshot");return;
        }
    }

    float savedOrigin[3],savedAngles[3],savedVelocity[3];
    GetClientAbsOrigin(client,savedOrigin);GetClientEyeAngles(client,savedAngles);
    GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",savedVelocity);
    int savedEpoch=gEpoch,savedNonce=gBaseMenuNonce[client],savedInventory[MATERIALS],savedWarehouses[MAX_CORES][MATERIALS];
    Building savedBases[MAX_CORES];
    for(int m=0;m<MATERIALS;m++)savedInventory[m]=gPlayerInventory[client][m];
    for(int core=0;core<gCoreCount;core++)
    {
        savedBases[core]=gBuildings[gCores[core].building];
        for(int m=0;m<MATERIALS;m++)savedWarehouses[core][m]=gBaseInventory[core][m];
    }

    int building=gCores[0].building,decodedCore,operation;
    char token[128];float noVelocity[3]={0.0,0.0,0.0};
    TeleportEntity(client,gCorePoints[0][0],NULL_VECTOR,noVelocity);
    BaseMenuToken(client,0,1,token,sizeof(token));
    Check(ValidateBaseMenuToken(client,token,decodedCore,operation) && decodedCore==0 && operation==1,
        "current base A menu token allows one intended confirmation");
    Check(!ValidateBaseMenuToken(client,token,decodedCore,operation),"replaying the identical base confirmation is rejected");

    BaseMenuToken(client,0,1,token,sizeof(token));gEpoch++;
    Check(!ValidateBaseMenuToken(client,token,decodedCore,operation),"previous-round base menu epoch is rejected");gEpoch=savedEpoch;

    BaseMenuToken(client,0,1,token,sizeof(token));gBuildings[building].level++;
    Check(!ValidateBaseMenuToken(client,token,decodedCore,operation),"another upgrade invalidates the previous level quote");gBuildings[building].level--;

    BaseMenuToken(client,0,1,token,sizeof(token));gBuildings[building].uid++;
    Check(!ValidateBaseMenuToken(client,token,decodedCore,operation),"replacement base UID invalidates its old menu");gBuildings[building].uid--;

    Format(token,sizeof(token),"%d:%d:0:%d:%d:%d:1",gEpoch,GetClientSerial(client)+1,
        gBuildings[building].uid,gBuildings[building].level,gBaseMenuNonce[client]);
    Check(!ValidateBaseMenuToken(client,token,decodedCore,operation),"a different client serial cannot use the menu token");

    BaseMenuToken(client,0,1,token,sizeof(token));
    float localOutside[3],outside[3];localOutside[0]=gCoreMaxs[0][0][0]+256.0;
    localOutside[1]=gCoreMaxs[0][0][1]+256.0;localOutside[2]=2.0;
    WorldPoint(localOutside,gCores[0].origin,gCores[0].angles,outside);
    TeleportEntity(client,outside,NULL_VECTOR,noVelocity);
    Check(SafeCoreAt(outside)<0 && !ValidateBaseMenuToken(client,token,decodedCore,operation),
        "leaving all safety zones prevents spending from a still-open base menu");

    for(int core=0;core<3;core++)
    {
        TeleportEntity(client,gCorePoints[core][0],NULL_VECTOR,noVelocity);
        Check(SafeCoreAt(gCorePoints[core][0])==core && CanManageCore(client,core),
            "each A/B/C safety zone selects its own base");
        Check(!CanManageCore(client,(core+1)%3),"a different base cannot be managed from this zone");
    }
    TeleportEntity(client,gCorePoints[0][0],NULL_VECTOR,noVelocity);
    BaseMenuToken(client,0,1,token,sizeof(token));
    TeleportEntity(client,gCorePoints[1][0],NULL_VECTOR,noVelocity);
    Check(!ValidateBaseMenuToken(client,token,decodedCore,operation),"moving from A to B rejects an open A upgrade confirmation");

    TeleportEntity(client,gCorePoints[0][0],NULL_VECTOR,noVelocity);
    gBuildings[building].alive=false;
    Check(AliveCores()>0 && SafeCoreAt(gCorePoints[0][0])==0 && CanManageCore(client,0),
        "destroyed base A still owns its safety/menu area while another base survives");
    Check(!TryUpgrade(client,building),"destroyed base cannot be upgraded through its surviving safety area");
    for(int core=0;core<gCoreCount;core++)gBuildings[gCores[core].building].alive=false;
    Check(SafeCoreAt(gCorePoints[0][0])<0 && !CanManageCore(client,0),"no safe base menu remains when every base is destroyed");
    for(int core=0;core<gCoreCount;core++)gBuildings[gCores[core].building]=savedBases[core];

    int quoteCore=-1,cost[MATERIALS],nextMaxHP;
    for(int core=0;core<gCoreCount;core++)
    {
        int candidate=gCores[core].building;
        if(gBuildings[candidate].alive && ReadBaseUpgrade(candidate,cost,nextMaxHP)){quoteCore=core;break;}
    }
    Check(quoteCore>=0,"menu transaction test has an upgrade with a material recipe");
    if(quoteCore>=0)
    {
        building=gCores[quoteCore].building;
        TeleportEntity(client,gCorePoints[quoteCore][0],NULL_VECTOR,noVelocity);
        // Make the final required material insufficient, after earlier materials pass.
        // This catches partial-debit bugs instead of only checking an empty first slot.
        int missing=-1;
        for(int m=0;m<MATERIALS;m++){gPlayerInventory[client][m]=cost[m];if(cost[m]>0)missing=m;}
        Check(missing>=0,"configured base upgrade has a nonzero material requirement");
        if(missing>=0)
        {
            gPlayerInventory[client][missing]--;
            int before[MATERIALS];for(int m=0;m<MATERIALS;m++)before[m]=gPlayerInventory[client][m];
            int oldLevel=gBuildings[building].level,oldHP=gBuildings[building].hp,oldMaxHP=gBuildings[building].maxHP;
            Check(!HasBaseUpgradeMaterials(cost,client) && !TryUpgrade(client,building),"missing the last required personal material rejects the upgrade");
            bool unchanged=true;for(int m=0;m<MATERIALS;m++)if(before[m]!=gPlayerInventory[client][m])unchanged=false;
            Check(unchanged && gBuildings[building].level==oldLevel && gBuildings[building].hp==oldHP
                && gBuildings[building].maxHP==oldMaxHP,"insufficient upgrade leaves every material and building statistic unchanged");
            for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=cost[m];
            Check(HasBaseUpgradeMaterials(cost,client),"exactly the quoted personal materials enable confirmation");
            BaseMenuToken(client,quoteCore,1,token,sizeof(token));gPlayerInventory[client][missing]--;
            Check(ValidateBaseMenuToken(client,token,decodedCore,operation) && !HasBaseUpgradeMaterials(cost,client)
                && !TryUpgrade(client,building),"personal materials spent after opening a menu are rechecked at confirmation");
            gBaseInventory[quoteCore][missing]=cost[missing]+100;
            Check(!HasBaseUpgradeMaterials(cost,client) && !TryUpgrade(client,building)
                && gBaseInventory[quoteCore][missing]==cost[missing]+100,"upgrading never silently consumes shared base stock");
        }
    }

    TestBaseStorageTransactions(client);
    for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=savedInventory[m];
    for(int core=0;core<gCoreCount;core++)
    {
        gBuildings[gCores[core].building]=savedBases[core];
        for(int m=0;m<MATERIALS;m++)gBaseInventory[core][m]=savedWarehouses[core][m];
    }
    gEpoch=savedEpoch;gBaseMenuNonce[client]=savedNonce;
    TeleportEntity(client,savedOrigin,savedAngles,savedVelocity);
}

void TestBaseStorageTransactions(int client)
{
    // Called only inside TestBaseMenus, which owns restoration of every edited balance.
    float noVelocity[3]={0.0,0.0,0.0};
    TeleportEntity(client,gCorePoints[0][0],NULL_VECTOR,noVelocity);
    int savedCap=gInventoryCap;
    gInventoryCap=100;gPlayerInventory[client][0]=50;gBaseInventory[0][0]=20;gBaseInventory[1][0]=7;
    Check(TransferBaseMaterial(client,0,0,10,true) && gPlayerInventory[client][0]==40 && gBaseInventory[0][0]==30
        && gBaseInventory[1][0]==7,"deposit moves personal materials only to the chosen base warehouse");
    Check(TransferBaseMaterial(client,0,0,10,false) && gPlayerInventory[client][0]==50 && gBaseInventory[0][0]==20,
        "withdrawal moves shared stock into the acting player backpack");
    Check(!TransferBaseMaterial(client,1,0,1,true) && gPlayerInventory[client][0]==50 && gBaseInventory[1][0]==7,
        "base B warehouse cannot be accessed while standing in A");

    char token[128];int core,operation;
    BaseMenuToken(client,0,20,token,sizeof(token));
    Check(ValidateBaseMenuToken(client,token,core,operation) && TransferBaseMaterial(client,core,0,1,true),
        "one valid storage menu selection transfers exactly once");
    Check(!ValidateBaseMenuToken(client,token,core,operation) && gPlayerInventory[client][0]==49 && gBaseInventory[0][0]==21,
        "replaying a storage button cannot duplicate its transfer");

    BaseMenuToken(client,0,24,token,sizeof(token));gBaseInventory[0][0]=9;
    Check(ValidateBaseMenuToken(client,token,core,operation) && !TransferBaseMaterial(client,0,0,10,false)
        && gPlayerInventory[client][0]==49 && gBaseInventory[0][0]==9,
        "withdrawal rechecks shared stock after another action changes it");
    gPlayerInventory[client][0]=99;gBaseInventory[0][0]=3;
    Check(!TransferBaseMaterial(client,0,0,3,false) && gPlayerInventory[client][0]==99 && gBaseInventory[0][0]==3,
        "insufficient backpack capacity rejects the whole transfer without partial debit");
    Check(!TransferBaseMaterial(client,0,0,0,true) && !TransferBaseMaterial(client,0,0,-1,true)
        && gPlayerInventory[client][0]==99 && gBaseInventory[0][0]==3,"zero and negative storage amounts never alter balances");

    int building=gCores[0].building;bool wasAlive=gBuildings[building].alive;gBuildings[building].alive=false;
    Check(TransferBaseMaterial(client,0,0,1,false) && gPlayerInventory[client][0]==100 && gBaseInventory[0][0]==2,
        "destroyed base stock remains accessible while its safety area survives");
    gBuildings[building].alive=wasAlive;gInventoryCap=savedCap;
}
