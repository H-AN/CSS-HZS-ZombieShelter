#define SHELTER_HARD_ENTITY_LIMIT 1800
int gEntityConstructionCeiling=1500,gEntityReserveMinimum=248,gEntityReservePerPlayer=4;
int gEntityReservePerNPC=2,gEntityTransientReserve=64,gEntityCriticalHeadroom=248;
float gEntityWarningInterval=30.0,gNextEntityWarning;

bool LoadEntitySettings()
{
    gEntityConstructionCeiling=Number(C_Entities,"Budget","construction_ceiling",1500);
    if(gEntityConstructionCeiling>SHELTER_HARD_ENTITY_LIMIT)gEntityConstructionCeiling=SHELTER_HARD_ENTITY_LIMIT;
    gEntityReserveMinimum=Number(C_Entities,"Budget","minimum_reserved_edicts",248);
    gEntityReservePerPlayer=Number(C_Entities,"Budget","reserve_per_player_slot",4);
    gEntityReservePerNPC=Number(C_Entities,"Budget","reserve_per_native_npc",2);
    gEntityTransientReserve=Number(C_Entities,"Budget","transient_reserve",64);
    gEntityCriticalHeadroom=Number(C_Entities,"Budget","critical_headroom",248);
    gEntityWarningInterval=Decimal(C_Entities,"Budget","warning_interval_seconds",30.0);
    if(gEntityConstructionCeiling<256 || gEntityConstructionCeiling>GetMaxEntities()-248
        || gEntityReserveMinimum<248 || gEntityReserveMinimum>1536
        || gEntityReservePerPlayer<4 || gEntityReservePerPlayer>32
        || gEntityReservePerNPC<2 || gEntityReservePerNPC>8
        || gEntityTransientReserve<64 || gEntityTransientReserve>512
        || gEntityCriticalHeadroom<248 || gEntityCriticalHeadroom>1024
        || gEntityWarningInterval<1.0 || gEntityWarningInterval>300.0)return ConfigFailure("entity safety reserves/ceilings");
    return ConstructionEntityLimit()>128 || ConfigFailure("entity reserves leave no construction capacity");
}

int ReservedEntityCount()
{
    int dynamicReserve=MaxClients*gEntityReservePerPlayer+(gNativePoolSize+gBossCount)*gEntityReservePerNPC
        +gEntityTransientReserve+gCoreCount+gNodeCount;
    return dynamicReserve>gEntityReserveMinimum ? dynamicReserve : gEntityReserveMinimum;
}

int ConstructionEntityLimit()
{
    int limit=GetMaxEntities()-ReservedEntityCount();
    if(limit>gEntityConstructionCeiling)limit=gEntityConstructionCeiling;
    int essential=GetMaxEntities()-gEntityCriticalHeadroom;
    if(limit>essential)limit=essential;
    return limit>SHELTER_HARD_ENTITY_LIMIT ? SHELTER_HARD_ENTITY_LIMIT : limit;
}

bool CanAllocateEntities(int count=1,bool optional=true)
{
    if(count<1)return false;
    int limit=optional ? ConstructionEntityLimit() : GetMaxEntities()-gEntityCriticalHeadroom;
    if(limit>SHELTER_HARD_ENTITY_LIMIT)limit=SHELTER_HARD_ENTITY_LIMIT;
    return count<=limit && GetEntityCount()<=limit-count;
}

bool CheckConstructionBudget(int client,int count=2)
{
    if(CanAllocateEntities(count,true))return true;
    Tell(client,"%t","EntityBudgetReached",GetEntityCount(),ConstructionEntityLimit(),ReservedEntityCount());
    return false;
}

void EntityBudgetTick(float now)
{
    if(now<gNextEntityWarning)return;
    gNextEntityWarning=now+gEntityWarningInterval;
    if(GetEntityCount()<ConstructionEntityLimit())return;
    PrintToServer("[Shelter] construction paused: edicts=%d build_limit=%d reserved=%d engine_limit=%d",
        GetEntityCount(),ConstructionEntityLimit(),ReservedEntityCount(),GetMaxEntities());
}
