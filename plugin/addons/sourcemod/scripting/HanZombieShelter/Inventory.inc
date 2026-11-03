// A campaign lasts across rollback rounds. Leaving transfers ownership to the team.
// Overflow rows are [preferred core, material, amount], with no disconnected account key.
ArrayList gSharedOverflow;
char gInventoryOwner[MAXPLAYERS+1][64];

void ResetInventories()
{
    delete gSharedOverflow;gSharedOverflow=new ArrayList(3);
    for(int c=0;c<MAX_CORES;c++)for(int m=0;m<MATERIALS;m++)gBaseInventory[c][m]=0;
    for(int client=1;client<=MaxClients;client++)
    {
        ResetClientGathering(client);
        gInventoryOwner[client][0]=0;
        for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=0;
        if(IsClientInGame(client))BindClientInventory(client);
    }
}

void BindClientInventory(int client)
{
    if(!IsClientInGame(client))return;
    char key[64];
    if(IsFakeClient(client) || !GetClientAuthId(client,AuthId_Steam2,key,sizeof(key),true))
        Format(key,sizeof(key),"serial:%d",GetClientSerial(client));
    if(StrEqual(key,gInventoryOwner[client]))return;
    strcopy(gInventoryOwner[client],sizeof(gInventoryOwner[]),key);
    int serial=GetClientSerial(client);
    for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].ownerSerial==serial)
        strcopy(gBuildings[b].ownerKey,sizeof(gBuildings[].ownerKey),key);
}

void StartClientInventory(int client)
{
    ResetClientGathering(client);
    gInventoryOwner[client][0]=0;
    for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=0;
    BindClientInventory(client);
}

void SaveClientInventory(int client)
{
    ResetClientGathering(client);
    bool hadMaterials;
    if(gEnabled && gCoreCount>0)
    {
        float position[3];if(IsClientInGame(client))GetClientAbsOrigin(client,position);
        int core=NearestStorageCore(position);
        for(int m=0;m<gMaterialCount;m++)
        {
            int amount=gPlayerInventory[client][m];gPlayerInventory[client][m]=0;
            if(amount>0){DepositSharedAutomatic(core,m,amount);hadMaterials=true;}
        }
        if(hadMaterials && !IsFakeClient(client) && Number(C_Management,"Disconnect","announce_transfer",1))
        {
            char name[MAX_NAME_LENGTH],letter[2];GetClientName(client,name,sizeof(name));letter[0]='A'+core;letter[1]=0;
            AnnouncePhrase("DisconnectMaterialsShared",name,letter);
        }
    }
    AbandonClientBuildings(client);
    gInventoryOwner[client][0]=0;
    for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=0;
}

int StorageCapacity()
{
    int capacity=Number(C_Resources,"BaseStorage","capacity",0);
    return capacity>0 ? capacity : 2000000000;
}

int NearestStorageCore(const float position[3])
{
    int local=SafeCoreAt(position);if(local>=0)return local;
    int best=-1;float distance=999999999999.0;
    for(int pass=0;pass<2 && best<0;pass++)for(int core=0;core<gCoreCount;core++)
    {
        int b=gCores[core].building;
        if(gCores[core].dynamic && !BuildingOperational(b))continue;
        if(pass==0 && (b<0 || !gBuildings[b].alive))continue;
        float candidate=GetVectorDistance(position,gCores[core].origin,true);
        if(candidate<distance){distance=candidate;best=core;}
    }
    return best>=0 ? best : 0;
}

int PlaceSharedMaterial(int preferred,int material,int amount)
{
    if(gCoreCount<1 || material<0 || material>=gMaterialCount || amount<=0)return amount;
    if(preferred<0 || preferred>=gCoreCount)preferred=0;
    int capacity=StorageCapacity();
    for(int step=0;step<gCoreCount && amount>0;step++)
    {
        int core=(preferred+step)%gCoreCount,room=capacity-gBaseInventory[core][material];
        if(gCores[core].dynamic && !BuildingOperational(gCores[core].building))continue;
        if(room<=0)continue;
        int deposit=amount<room ? amount : room;gBaseInventory[core][material]+=deposit;amount-=deposit;
    }
    return amount;
}

void DepositSharedAutomatic(int preferred,int material,int amount)
{
    if(amount<=0 || material<0 || material>=gMaterialCount)return;
    int remaining=PlaceSharedMaterial(preferred,material,amount);if(remaining<=0)return;
    if(gSharedOverflow==null)gSharedOverflow=new ArrayList(3);
    for(int i=0;i<gSharedOverflow.Length && remaining>0;i++)
    {
        int row[3];gSharedOverflow.GetArray(i,row,3);
        if(row[0]!=preferred || row[1]!=material)continue;
        int room=2000000000-row[2],take=remaining<room ? remaining : room;
        row[2]+=take;remaining-=take;gSharedOverflow.SetArray(i,row,3);
    }
    if(remaining>0){int row[3];row[0]=preferred;row[1]=material;row[2]=remaining;gSharedOverflow.PushArray(row,3);}
}

void FlushSharedOverflow()
{
    if(gSharedOverflow==null)return;
    for(int i=gSharedOverflow.Length-1;i>=0;i--)
    {
        int row[3];gSharedOverflow.GetArray(i,row,3);row[2]=PlaceSharedMaterial(row[0],row[1],row[2]);
        if(row[2]<=0)gSharedOverflow.Erase(i);else gSharedOverflow.SetArray(i,row,3);
    }
}

int SharedPendingAmount(int core,int material)
{
    if(gSharedOverflow==null)return 0;
    int total;
    for(int i=0;i<gSharedOverflow.Length;i++)
    {
        int row[3];gSharedOverflow.GetArray(i,row,3);
        if(row[0]!=core || row[1]!=material)continue;
        if(row[2]>2000000000-total)return 2000000000;total+=row[2];
    }
    return total;
}

bool Spend(const int cost[MATERIALS],int client)
{
    if(client<1 || client>MaxClients || !IsClientInGame(client))return false;
    for(int m=0;m<gMaterialCount;m++)if(cost[m]<0 || cost[m]>gPlayerInventory[client][m])return false;
    for(int m=0;m<gMaterialCount;m++)gPlayerInventory[client][m]-=cost[m];return true;
}

void Refund(const int cost[MATERIALS],int client)
{
    for(int m=0;m<gMaterialCount;m++)Grant(m,cost[m],client);
}

int Grant(int material,int amount,int client)
{
    if(client<1 || client>MaxClients || !IsClientInGame(client) || material<0 || material>=gMaterialCount || amount<=0)return 0;
    int maximum=gInventoryCap>0 ? gInventoryCap : 2000000000;
    int room=maximum-gPlayerInventory[client][material];if(room<=0)return 0;
    int actual=amount>room ? room : amount;gPlayerInventory[client][material]+=actual;return actual;
}

bool TransferBaseMaterial(int client,int core,int material,int amount,bool deposit)
{
    if(!Running() || !LiveHuman(client) || core<0 || core>=gCoreCount || material<0 || material>=gMaterialCount || amount<1)return false;
    float position[3];GetClientAbsOrigin(client,position);if(SafeCoreAt(position)!=core)return false;
    int capacity=deposit ? Number(C_Resources,"BaseStorage","capacity",0) : gInventoryCap;
    if(capacity<=0)capacity=2000000000;
    int source=deposit ? gPlayerInventory[client][material] : gBaseInventory[core][material];
    int destination=deposit ? gBaseInventory[core][material] : gPlayerInventory[client][material];
    if(source<amount || amount>capacity-destination)return false;
    if(deposit){gPlayerInventory[client][material]-=amount;gBaseInventory[core][material]+=amount;}
    else{gBaseInventory[core][material]-=amount;gPlayerInventory[client][material]+=amount;FlushSharedOverflow();}
    char label[64];TranslateLabel(client,gMaterials[material].label,label,sizeof(label));
    HudFeedback(client,"%t",deposit ? "StorageDeposited" : "StorageWithdrawn",label,amount,
        gPlayerInventory[client][material],gBaseInventory[core][material]);return true;
}
