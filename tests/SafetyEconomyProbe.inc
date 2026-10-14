// Test-only transactional checks. No clients are created or disconnected here:
// two existing fixtures exercise the same production join/leave helpers directly.
// Large snapshots are global so they do not consume the SourcePawn stack.
Building tSafetyBuildings[MAX_BUILDINGS];
int tSafetyPersonal[MAXPLAYERS+1][MATERIALS],tSafetyWarehouses[MAX_CORES][MATERIALS];
char tSafetyOwners[MAXPLAYERS+1][64];
int tSafetyBindings[2049],tSafetyBindingRefs[2049];
bool tSafetyRemovalPending,tSafetyRemovalChecked,tSafetyRemovalGone,tSafetyRemovalRebound;
int tSafetyRemovalCount,tSafetyRemovalRefs[MAX_BUILDINGS+16],tSafetyRemovalResolved[MAX_BUILDINGS+16];
float tSafetyRemovalCheckedAt;

bool SafetyPersonalMatches(int client,const int expected[MATERIALS])
{
    for(int material=0;material<MATERIALS;material++)
        if(gPlayerInventory[client][material]!=expected[material])return false;
    return true;
}

bool SafetyFindConstructionAim(int client,int recipe)
{
    char path[128],model[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s",gRecipes[recipe].type);
    Text(gRecipes[recipe].file,path,"model",model,sizeof(model));
    float placement[3],angles[3];
    for(int radius=0;radius<4;radius++)for(int step=0;step<16;step++)
    {
        float heading=DegToRad(float(step)*22.5),position[3],ground[3],end[3];position=gCores[0].origin;
        position[0]+=Cosine(heading)*(500.0+float(radius)*150.0);
        position[1]+=Sine(heading)*(500.0+float(radius)*150.0);
        position[2]+=300.0;end=position;end[2]-=500.0;
        Handle trace=TR_TraceRayFilterEx(position,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
        bool hit=TR_DidHit(trace) && !TR_StartSolid(trace);
        if(hit)TR_GetEndPosition(ground,trace);delete trace;if(!hit)continue;
        position=ground;position[2]+=2.0;
        float mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0};
        if(!ClearHull(position,mins,maxs,client,MASK_PLAYERSOLID))continue;
        float aim[3];aim=ground;aim[0]+=Cosine(heading)*120.0;aim[1]+=Sine(heading)*120.0;
        Aim(client,position,aim);
        if(Placement(client,model,placement,angles))return true;
    }
    return false;
}

bool SafetyAimBuilding(int client,int building)
{
    int entity=Resolve(gBuildings[building].ref);if(entity<0)return false;
    float center[3];EntityCenter(entity,center);
    for(int step=0;step<12;step++)
    {
        float heading=DegToRad(float(step)*30.0),position[3];position=gBuildings[building].origin;
        position[0]+=Cosine(heading)*96.0;position[1]+=Sine(heading)*96.0;position[2]+=2.0;
        Aim(client,position,center);if(BuildingInUseRange(client,building))return true;
    }
    return false;
}

int SafetyPaidFixture(int recipe,const float position[3],int owner)
{
    int building=TestBuild(recipe,position);if(building<0)return -1;
    if(!Spend(gRecipes[recipe].cost,owner))
    {
        int ref=gBuildings[building].ref;gBuildings[building].ref=INVALID_ENT_REFERENCE;
        gBuildings[building].alive=false;gBuildings[building].hp=0;RemoveOwnedRef(ref);return -1;
    }
    AssignBuildingOwner(building,owner);
    for(int material=0;material<MATERIALS;material++)gBuildings[building].invested[material]=gRecipes[recipe].cost[material];
    return building;
}

void SafetyEntityConfigChecks()
{
    int cached[7];cached[0]=gEntityConstructionCeiling;cached[1]=gEntityReserveMinimum;
    cached[2]=gEntityReservePerPlayer;cached[3]=gEntityReservePerNPC;cached[4]=gEntityTransientReserve;
    cached[5]=gEntityCriticalHeadroom;float warning=gEntityWarningInterval;
    bool hadBudget=Node(C_Entities,"Budget");
    if(!hadBudget){gCfg[C_Entities].Rewind();gCfg[C_Entities].JumpToKey("Budget",true);}
    bool hadCeiling=gCfg[C_Entities].GetDataType("construction_ceiling")!=KvData_None;
    cached[6]=gCfg[C_Entities].GetNum("construction_ceiling",1500);
    gCfg[C_Entities].DeleteKey("construction_ceiling");
    Check(LoadEntitySettings() && gEntityConstructionCeiling==1500 && ConstructionEntityLimit()<=1500,
        "entity loader defaults to construction ceiling 1500 while retaining all dynamic reserves");
    Node(C_Entities,"Budget");gCfg[C_Entities].SetNum("construction_ceiling",2000);
    Check(LoadEntitySettings() && gEntityConstructionCeiling==1800 && ConstructionEntityLimit()<=1800
        && ConstructionEntityLimit()<=GetMaxEntities()-ReservedEntityCount(),
        "entity config value 2000 is hard-clamped to 1800 and cannot bypass dynamic reserved capacity");
    Node(C_Entities,"Budget");
    if(hadCeiling)gCfg[C_Entities].SetNum("construction_ceiling",cached[6]);else gCfg[C_Entities].DeleteKey("construction_ceiling");
    if(!hadBudget){gCfg[C_Entities].Rewind();gCfg[C_Entities].DeleteKey("Budget");}
    gEntityConstructionCeiling=cached[0];gEntityReserveMinimum=cached[1];gEntityReservePerPlayer=cached[2];
    gEntityReservePerNPC=cached[3];gEntityTransientReserve=cached[4];gEntityCriticalHeadroom=cached[5];gEntityWarningInterval=warning;
}

void SafetyBudgetChecks(int client,int recipe)
{
    int ceiling=gEntityConstructionCeiling,critical=gEntityCriticalHeadroom;
    int entities=GetEntityCount(),summons=SummonCount(),inventory[MATERIALS];inventory=gPlayerInventory[client];
    gEntityConstructionCeiling=entities;
    Check(!BuildRecipe(client,recipe),"entity ceiling rejects construction before placement or material debit");
    Check(SafetyPersonalMatches(client,inventory) && SummonCount()==summons,"budget rejection leaves the entire backpack and building ledger unchanged");
    Check(GetEntityCount()==entities,"budget-rejected build allocates no temporary probe or permanent entity");
    char path[128],model[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s",gRecipes[recipe].type);
    Text(gRecipes[recipe].file,path,"model",model,sizeof(model));
    int probe=CreateBody(model,gCores[0].origin,gCores[0].angles,"hzsh_safety_optional",true,true);
    Check(probe<0 && GetEntityCount()==entities,"direct optional model-probe creation observes the same entity ceiling");
    if(probe>0)RemoveEntity(probe);
    gEntityConstructionCeiling=ceiling;gEntityCriticalHeadroom=GetMaxEntities()-GetEntityCount();
    probe=CreateBody(model,gCores[0].origin,gCores[0].angles,"hzsh_safety_essential",true,false);
    Check(probe<0 && GetEntityCount()==entities,"essential entity recreation also respects its final critical reserve");
    if(probe>0)RemoveEntity(probe);
    gEntityCriticalHeadroom=critical;
}

void SafetyManagementChecks(int client,int other,int recipe,float reusablePosition[3],ArrayList removedRefs)
{
    TeleportEntity(other,gCorePoints[1][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    bool found=SafetyFindConstructionAim(client,recipe);
    Check(found,"economy fixture finds a real collision-checked construction site");if(!found)return;
    int before[MATERIALS];before=gPlayerInventory[client];int uidBefore=gUID;
    bool built=BuildRecipe(client,recipe);int building=-1;
    for(int b=gCoreCount;b<MAX_BUILDINGS;b++)
        if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].uid>uidBefore){building=b;break;}
    Check(built && building>=0,"normal build transaction creates a real owned facility");if(!built || building<0)return;
    reusablePosition=gBuildings[building].origin;
    bool paid=true;
    for(int material=0;material<MATERIALS;material++)
        if(before[material]-gPlayerInventory[client][material]!=gRecipes[recipe].cost[material]
            || gBuildings[building].invested[material]!=gRecipes[recipe].cost[material])paid=false;
    Check(paid,"refundable investment records the material amounts actually debited by construction");
    Check(CanManageBuilding(client,building) && !CanManageBuilding(other,building),"two ordinary fixtures have different facility ownership permissions");

    // A different connection in the same client slot must not inherit a live
    // facility simply by presenting the same account key or array index.
    int ownerSerial=gBuildings[building].ownerSerial;
    gBuildings[building].ownerSerial=ownerSerial+100000;
    Check(!CanManageBuilding(client,building),"matching owner key with a stale connection serial cannot claim a facility through a reused player slot");
    gBuildings[building].ownerSerial=ownerSerial;
    TeleportEntity(client,gCorePoints[1][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    Check(SafetyAimBuilding(other,building),"non-owner fixture is genuinely close enough to interact with the facility");
    int otherBefore[MATERIALS];otherBefore=gPlayerInventory[other];int level=gBuildings[building].level;
    Check(!TryUpgrade(other,building) && !DismantleBuilding(other,building)
        && gBuildings[building].alive && gBuildings[building].level==level && SafetyPersonalMatches(other,otherBefore),
        "nearby non-owner cannot upgrade or dismantle and loses no materials");
    before=gPlayerInventory[client];
    Check(!BuildingInUseRange(client,building) && !TryUpgrade(client,building) && !DismantleBuilding(client,building)
        && SafetyPersonalMatches(client,before) && gBuildings[building].alive,
        "owner must still be in use range; remote mutations leave materials and facility intact");

    TeleportEntity(other,gCorePoints[1][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    Check(SafetyAimBuilding(client,building),"owner fixture can target the real facility collider within use range");
    int cost[MATERIALS];char path[192];LevelPath(building,path,sizeof(path),level+1);
    bool hasCost=Node(gBuildings[building].file,path) && gCfg[gBuildings[building].file].JumpToKey("upgrade_cost");
    if(hasCost)for(int material=0;material<MATERIALS;material++)cost[material]=gCfg[gBuildings[building].file].GetNum(gMaterials[material].id,0);
    before=gPlayerInventory[client];bool upgraded=hasCost && TryUpgrade(client,building);paid=upgraded;
    for(int material=0;material<MATERIALS;material++)
        if(before[material]-gPlayerInventory[client][material]!=cost[material]
            || gBuildings[building].invested[material]!=gRecipes[recipe].cost[material]+cost[material])paid=false;
    Check(paid && gBuildings[building].level==level+1,"successful upgrade adds only its actual payment to refundable investment");

    int refund[MATERIALS];BuildingRefund(building,refund);bool half=true;
    for(int material=0;material<MATERIALS;material++)
        if(refund[material]!=RoundToFloor(float(gRecipes[recipe].cost[material]+cost[material])*0.5))half=false;
    Check(half,"configured fifty-percent refund floors each actually paid material including upgrades");
    int uid=gBuildings[building].uid,ref=gBuildings[building].ref,hp=gBuildings[building].hp;
    gInventoryCap=100;for(int material=0;material<MATERIALS;material++)gPlayerInventory[client][material]=100;
    before=gPlayerInventory[client];
    Check(!DismantleBuilding(client,building) && SafetyPersonalMatches(client,before)
        && gBuildings[building].alive && gBuildings[building].ref==ref && gBuildings[building].hp==hp,
        "full recipient backpack rejects dismantling before removing the body or changing HP");
    int afterRefund[MATERIALS];BuildingRefund(building,afterRefund);bool investmentIntact=true;
    for(int material=0;material<MATERIALS;material++)if(refund[material]!=afterRefund[material])investmentIntact=false;
    Check(investmentIntact,"rejected full-backpack dismantle retains its complete refundable investment");
    gInventoryCap=0;before=gPlayerInventory[client];bool removed=DismantleBuilding(client,building);paid=removed;
    for(int material=0;material<MATERIALS;material++)
        if(gPlayerInventory[client][material]!=before[material]+refund[material] || gBuildings[building].invested[material]!=0)paid=false;
    PrintToServer("[ShelterSafetyRemoval] half returned=%d paid=%d alive=%d logical_ref=%d old_ref=%d resolved_now=%d",
        removed,paid,gBuildings[building].alive,gBuildings[building].ref,ref,Resolve(ref));
    if(!paid)for(int material=0;material<MATERIALS;material++)
        PrintToServer("[ShelterSafetyRefund] half material=%d before=%d expected_refund=%d after=%d remaining_investment=%d",
            material,before[material],refund[material],gPlayerInventory[client][material],gBuildings[building].invested[material]);
    Check(paid && !gBuildings[building].alive && gBuildings[building].ref==INVALID_ENT_REFERENCE,
        "successful dismantle invalidates its logical instance and refunds half its investment exactly once");
    if(removed)removedRefs.Push(ref);
    before=gPlayerInventory[client];
    Check(!DismantleBuilding(client,building) && SafetyPersonalMatches(client,before),"repeated dismantle cannot issue a second refund");

    // Reuse the same building slot with a different owner and a new UID.
    int replacement=SafetyPaidFixture(recipe,reusablePosition,other);
    Check(replacement==building && gBuildings[replacement].uid!=uid && !CanManageBuilding(client,replacement)
        && CanManageBuilding(other,replacement),"recycled building slot belongs only to the replacement instance owner");
    if(replacement<0)return;
    TeleportEntity(client,gCorePoints[1][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    Check(SafetyAimBuilding(other,replacement),"replacement owner targets the new collider before dismantling");
    gDismantleRefund=false;otherBefore=gPlayerInventory[other];ref=gBuildings[replacement].ref;
    removed=DismantleBuilding(other,replacement);
    PrintToServer("[ShelterSafetyRemoval] disabled returned=%d unchanged=%d alive=%d logical_ref=%d old_ref=%d resolved_now=%d",
        removed,SafetyPersonalMatches(other,otherBefore),gBuildings[replacement].alive,gBuildings[replacement].ref,ref,Resolve(ref));
    Check(removed && !gBuildings[replacement].alive && gBuildings[replacement].ref==INVALID_ENT_REFERENCE
        && SafetyPersonalMatches(other,otherBefore),"disabling refunds still invalidates the facility without crediting any material");
    if(removed)removedRefs.Push(ref);
    gDismantleRefund=true;

    replacement=SafetyPaidFixture(recipe,reusablePosition,other);
    Check(replacement>=0,"administrator refund fixture creates a separately owned paid facility");if(replacement<0)return;
    BuildingRefund(replacement,refund);before=gPlayerInventory[client];otherBefore=gPlayerInventory[other];
    AdminId administrator=CreateAdmin("ShelterSafetyEconomyProbe");SetAdminFlag(administrator,Admin_Root,true);
    SetUserAdmin(client,administrator,false);gAdminRefundToOwner=true;
    ref=gBuildings[replacement].ref;bool authorized=IsShelterAdmin(client);
    removed=DismantleBuilding(client,replacement,true);paid=removed;
    for(int material=0;material<MATERIALS;material++)if(gPlayerInventory[other][material]!=otherBefore[material]+refund[material])paid=false;
    PrintToServer("[ShelterSafetyAdmin] fake=%d admin_id=%d root_bits=%d authorized=%d returned=%d owner_paid=%d actor_unchanged=%d",
        IsFakeClient(client),view_as<int>(GetUserAdmin(client)),GetUserFlagBits(client),authorized,removed,paid,SafetyPersonalMatches(client,before));
    if(IsFakeClient(client))
        Check(!authorized && !removed && gBuildings[replacement].alive && gBuildings[replacement].ref==ref
            && SafetyPersonalMatches(client,before) && SafetyPersonalMatches(other,otherBefore),
            "SourceMod denies fake-client administrator access without dismantling or changing either backpack");
    else
        Check(authorized && paid && SafetyPersonalMatches(client,before),
            "real administrator dismantle refunds the online owner rather than the administrator");
    if(removed)removedRefs.Push(ref);
    SetUserAdmin(client,INVALID_ADMIN_ID,false);RemoveAdmin(administrator);
}

void SafetyDisconnectChecks(int client,int other,int recipe,const float reusablePosition[3],ArrayList removedRefs)
{
    for(int material=0;material<MATERIALS;material++)
    {gPlayerInventory[client][material]=1000;gPlayerInventory[other][material]=1000;}
    float position[3];position=reusablePosition;position[0]+=250.0;
    int ownedLive=SafetyPaidFixture(recipe,position,client);position[0]+=250.0;
    int ownedDead=SafetyPaidFixture(recipe,position,client);position[0]+=250.0;
    int otherOwned=SafetyPaidFixture(recipe,position,other);
    Check(ownedLive>=0 && ownedDead>=0 && otherOwned>=0,"disconnect fixture has two owner instances and a different player's facility");
    if(ownedLive<0 || ownedDead<0 || otherOwned<0)return;
    removedRefs.Push(gBuildings[ownedDead].ref);DamageBuilding(ownedDead,gBuildings[ownedDead].hp);
    int serial=GetClientSerial(client),otherSerial=gBuildings[otherOwned].ownerSerial;
    bool affected[MAX_BUILDINGS];
    for(int building=gCoreCount;building<MAX_BUILDINGS;building++)
        affected[building]=gBuildings[building].used && gBuildings[building].ownerSerial==serial;
    int leaving[MATERIALS]={17,19,23,29,31},otherBefore[MATERIALS];otherBefore=gPlayerInventory[other];
    for(int core=0;core<gCoreCount;core++)for(int material=0;material<MATERIALS;material++)gBaseInventory[core][material]=0;
    for(int material=0;material<MATERIALS;material++)gPlayerInventory[client][material]=leaving[material];
    TeleportEntity(client,gCorePoints[0][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    Node(C_Resources,"BaseStorage");gCfg[C_Resources].SetNum("capacity",0);
    SaveClientInventory(client);bool moved=true,abandoned=true,otherStockEmpty=true;
    for(int material=0;material<MATERIALS;material++)
    {
        if(gPlayerInventory[client][material]!=0 || gBaseInventory[0][material]!=leaving[material])moved=false;
        for(int core=1;core<gCoreCount;core++)if(gBaseInventory[core][material]!=0)otherStockEmpty=false;
    }
    for(int building=gCoreCount;building<MAX_BUILDINGS;building++)
        if(affected[building] && (gBuildings[building].ownerSerial!=0 || gBuildings[building].ownerKey[0]))abandoned=false;
    Check(moved && otherStockEmpty && gSharedOverflow.Length==0,"leave helper transfers every material to the current base without retaining a personal copy");
    Check(abandoned && !gInventoryOwner[client][0],"leave helper makes all of that connection's live and destroyed facilities ownerless");
    Check(gBuildings[otherOwned].ownerSerial==otherSerial && CanManageBuilding(other,otherOwned) && SafetyPersonalMatches(other,otherBefore),
        "one player's leave preserves another player's facility and private backpack");
    SaveClientInventory(client);moved=true;
    for(int material=0;material<MATERIALS;material++)
        if(gBaseInventory[0][material]!=leaving[material] || gPlayerInventory[client][material]!=0)moved=false;
    Check(moved && gSharedOverflow.Length==0,"repeating leave cleanup cannot deposit the same personal materials twice");
    StartClientInventory(client);bool empty=true;
    for(int material=0;material<MATERIALS;material++)if(gPlayerInventory[client][material]!=0)empty=false;
    Check(empty && gInventoryOwner[client][0] && gBuildings[ownedLive].ownerSerial==0 && !gBuildings[ownedLive].ownerKey[0],
        "new join starts empty and does not reclaim the prior connection's shared materials or orphan facility");

    // Force every warehouse full, then use the actual disconnect and withdrawal
    // paths to prove the overflow queue never loses or duplicates material.
    Node(C_Resources,"BaseStorage");gCfg[C_Resources].SetNum("capacity",5);
    for(int core=0;core<gCoreCount;core++)for(int material=0;material<MATERIALS;material++)gBaseInventory[core][material]=5;
    for(int material=0;material<MATERIALS;material++)gPlayerInventory[client][material]=material+2;
    SaveClientInventory(client);bool queued=true;
    for(int material=0;material<MATERIALS;material++)
    {
        if(SharedPendingAmount(0,material)!=material+2 || gPlayerInventory[client][material]!=0)queued=false;
        for(int core=0;core<gCoreCount;core++)if(gBaseInventory[core][material]!=5)queued=false;
    }
    Check(queued,"full warehouses retain all leaving materials in shared overflow without exceeding capacity");
    SaveClientInventory(client);queued=true;
    for(int material=0;material<MATERIALS;material++)if(SharedPendingAmount(0,material)!=material+2)queued=false;
    Check(queued,"duplicate leave cleanup also leaves pending overflow amounts unchanged");
    gPlayerInventory[other][0]=0;TeleportEntity(other,gCorePoints[0][1],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    bool withdrew=TransferBaseMaterial(other,0,0,2,false);
    Check(withdrew && gPlayerInventory[other][0]==2 && gBaseInventory[0][0]==5 && SharedPendingAmount(0,0)==0,
        "real withdrawal makes room and atomically moves exactly two queued materials back into shared storage");
    int pending[MATERIALS];
    for(int material=0;material<MATERIALS;material++)pending[material]=SharedPendingAmount(0,material);
    for(int core=0;core<gCoreCount;core++)for(int material=0;material<MATERIALS;material++)gBaseInventory[core][material]=0;
    FlushSharedOverflow();bool conserved=true;
    for(int material=0;material<MATERIALS;material++)
    {
        int stored;
        for(int core=0;core<gCoreCount;core++)
        {stored+=gBaseInventory[core][material];if(gBaseInventory[core][material]>5)conserved=false;}
        if(stored!=pending[material] || SharedPendingAmount(0,material)!=0)conserved=false;
    }
    Check(conserved && gSharedOverflow.Length==0,"free space flushes every remaining overflow row once and respects each warehouse capacity");
}

void TestSafetyEconomy(int client)
{
    static bool statusRegistered;
    if(!statusRegistered){RegServerCmd("zsh_test_safety_removal",SafetyRemovalStatusCommand);statusRegistered=true;}
    int other=0,recipe=-1;
    for(int player=1;player<=MaxClients;player++)if(player!=client && LiveHuman(player)){other=player;break;}
    for(int candidate=0;candidate<gRecipeCount;candidate++)if(gRecipes[candidate].kind==Kind_Turret){recipe=candidate;break;}
    Check(Running() && LiveHuman(client) && other>0 && recipe>=0 && gCoreCount>=2,
        "safety economy fixture has two existing living connections, multiple bases and a turret recipe");
    if(!Running() || !LiveHuman(client) || other==0 || recipe<0 || gCoreCount<2)return;

    for(int building=0;building<MAX_BUILDINGS;building++)tSafetyBuildings[building]=gBuildings[building];
    for(int player=0;player<=MaxClients;player++)
    {
        for(int material=0;material<MATERIALS;material++)tSafetyPersonal[player][material]=gPlayerInventory[player][material];
        strcopy(tSafetyOwners[player],sizeof(tSafetyOwners[]),gInventoryOwner[player]);
    }
    for(int core=0;core<MAX_CORES;core++)for(int material=0;material<MATERIALS;material++)tSafetyWarehouses[core][material]=gBaseInventory[core][material];
    for(int entity=0;entity<sizeof(gWorldBinding);entity++)
    {tSafetyBindings[entity]=gWorldBinding[entity];tSafetyBindingRefs[entity]=gWorldBindingRef[entity];}
    float origins[2][3],angles[2][3],velocities[2][3];int clients[2],health[2];clients[0]=client;clients[1]=other;
    AdminId administrators[2];
    for(int index=0;index<2;index++)
    {
        int player=clients[index];GetClientAbsOrigin(player,origins[index]);GetClientEyeAngles(player,angles[index]);
        GetEntPropVector(player,Prop_Data,"m_vecAbsVelocity",velocities[index]);health[index]=GetClientHealth(player);
        administrators[index]=GetUserAdmin(player);SetUserAdmin(player,INVALID_ADMIN_ID,false);BindClientInventory(player);
        for(int material=0;material<MATERIALS;material++)gPlayerInventory[player][material]=1000;
    }
    int inventoryCap=gInventoryCap,ceiling=gEntityConstructionCeiling,critical=gEntityCriticalHeadroom;
    int summonLimit=gSummonLimit;bool refundEnabled=gDismantleRefund,ownerRefund=gAdminRefundToOwner;float fraction=gDismantleFraction;
    bool hadStorage=Node(C_Resources,"BaseStorage");int storageCapacity=Number(C_Resources,"BaseStorage","capacity",0);
    Node(C_Resources,"BaseStorage");bool hadCapacity=hadStorage && gCfg[C_Resources].GetDataType("capacity")!=KvData_None;
    if(!hadStorage){gCfg[C_Resources].Rewind();gCfg[C_Resources].JumpToKey("BaseStorage",true);}
    ArrayList savedOverflow=gSharedOverflow;gSharedOverflow=new ArrayList(3);
    gInventoryCap=0;gDismantleRefund=true;gDismantleFraction=0.5;gAdminRefundToOwner=true;
    gSummonLimit=MAX_BUILDINGS-MAX_CORES;
    Check(!IsShelterAdmin(client) && !IsShelterAdmin(other),"ownership and distance checks run with both fixtures stripped of administrator bypass");

    SafetyEntityConfigChecks();SafetyBudgetChecks(client,recipe);
    float reusablePosition[3];reusablePosition=gBosses[0].origin;reusablePosition[0]+=900.0;
    ArrayList removedRefs=new ArrayList();
    SafetyManagementChecks(client,other,recipe,reusablePosition,removedRefs);
    SafetyDisconnectChecks(client,other,recipe,reusablePosition,removedRefs);

    for(int building=0;building<MAX_BUILDINGS;building++)
    {
        int ref=gBuildings[building].ref;
        if(ref!=tSafetyBuildings[building].ref)
        {
            gBuildings[building].ref=INVALID_ENT_REFERENCE;
            if(ref!=INVALID_ENT_REFERENCE){removedRefs.Push(ref);RemoveOwnedRef(ref);}
        }
        gBuildings[building]=tSafetyBuildings[building];
    }
    for(int entity=0;entity<sizeof(gWorldBinding);entity++)
    {gWorldBinding[entity]=tSafetyBindings[entity];gWorldBindingRef[entity]=tSafetyBindingRefs[entity];}
    for(int player=0;player<=MaxClients;player++)
    {
        for(int material=0;material<MATERIALS;material++)gPlayerInventory[player][material]=tSafetyPersonal[player][material];
        strcopy(gInventoryOwner[player],sizeof(gInventoryOwner[]),tSafetyOwners[player]);
    }
    for(int core=0;core<MAX_CORES;core++)for(int material=0;material<MATERIALS;material++)gBaseInventory[core][material]=tSafetyWarehouses[core][material];
    delete gSharedOverflow;gSharedOverflow=savedOverflow;
    Node(C_Resources,"BaseStorage");
    if(hadCapacity)gCfg[C_Resources].SetNum("capacity",storageCapacity);else gCfg[C_Resources].DeleteKey("capacity");
    if(!hadStorage){gCfg[C_Resources].Rewind();gCfg[C_Resources].DeleteKey("BaseStorage");}
    gInventoryCap=inventoryCap;gEntityConstructionCeiling=ceiling;gEntityCriticalHeadroom=critical;gSummonLimit=summonLimit;
    gDismantleRefund=refundEnabled;gDismantleFraction=fraction;gAdminRefundToOwner=ownerRefund;
    for(int index=0;index<2;index++)
    {
        int player=clients[index];SetUserAdmin(player,administrators[index],false);
        TeleportEntity(player,origins[index],angles[index],velocities[index]);SetEntityHealth(player,health[index]);
    }
    Check(gSharedOverflow==savedOverflow,"safety economy scenario restores original world ledger and shared overflow ownership before deferred deletion");
    tSafetyRemovalPending=true;tSafetyRemovalChecked=false;tSafetyRemovalCount=0;
    // RequestFrame can execute before the engine's deferred DeleteList pass.
    // Allow several complete engine frames, then still require every old ref
    // to be invalid. No production deletion behavior is changed for this test.
    CreateTimer(0.1,SafetyVerifyRemovedEntities,removedRefs,TIMER_FLAG_NO_MAPCHANGE|TIMER_DATA_HNDL_CLOSE);
}

public Action SafetyVerifyRemovedEntities(Handle timer,any value)
{
    ArrayList refs=view_as<ArrayList>(value);bool gone=true,rebound=false;int count=refs.Length;
    for(int index=0;index<count;index++)
    {
        int ref=refs.Get(index),entity=Resolve(ref);
        if(entity>0)gone=false;
        LogMessage("[ShelterSafetyRemoval] after_0.1 index=%d old_ref=%d resolved=%d",index,ref,entity);
        if(index<sizeof(tSafetyRemovalRefs)){tSafetyRemovalRefs[index]=ref;tSafetyRemovalResolved[index]=entity;}
        for(int building=0;building<MAX_BUILDINGS;building++)
            if(gBuildings[building].used && gBuildings[building].ref==ref)rebound=true;
    }
    tSafetyRemovalPending=false;tSafetyRemovalChecked=true;tSafetyRemovalGone=gone;tSafetyRemovalRebound=rebound;
    tSafetyRemovalCount=count;tSafetyRemovalCheckedAt=GetGameTime();
    LogMessage("[ShelterSafetyRemoval] checked=%d delay=0.1 all_gone=%d rebound=%d",count,gone,rebound);
    Check(count>0 && gone && !rebound,"after 0.1 seconds all dismantled and fixture entities are gone and no old EntRef controls a restored building");
    return Plugin_Stop;
}

void SafetyLastRemoval()
{
    PrintToServer("[ShelterSafetyRemovalStatus] pending=%d checked=%d count=%d all_gone=%d rebound=%d game_time=%.3f",
        tSafetyRemovalPending,tSafetyRemovalChecked,tSafetyRemovalCount,tSafetyRemovalGone,tSafetyRemovalRebound,tSafetyRemovalCheckedAt);
    for(int index=0;index<tSafetyRemovalCount && index<sizeof(tSafetyRemovalRefs);index++)
        PrintToServer("[ShelterSafetyRemovalStatus] index=%d old_ref=%d resolved_at_check=%d resolved_now=%d",
            index,tSafetyRemovalRefs[index],tSafetyRemovalResolved[index],Resolve(tSafetyRemovalRefs[index]));
}

public Action SafetyRemovalStatusCommand(int args)
{
    SafetyLastRemoval();return Plugin_Handled;
}
