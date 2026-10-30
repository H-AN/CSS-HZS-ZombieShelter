int gV083IndexChecks,gV083IndexFailures;
void IndexCheck(bool pass)
{
    gV083IndexChecks++;if(!pass){gV083IndexFailures++;PrintToServer("[V083Index] failed check=%d",gV083IndexChecks);}
}
void V083IndexMatrix()
{
    Building empty;gV083IndexChecks=0;gV083IndexFailures=0;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        gBuildings[b]=empty;gBuildings[b].used=true;gBuildings[b].alive=b%3!=0;
        gBuildings[b].uid=(b+1)*2048+1;gBuildings[b].kind=view_as<BuildingKind>(b%20);gBuildings[b].powerCore=b%MAX_CORES;
    }
    InvalidateRuntimeBuildings();EnsureRuntimeBuildings();IndexCheck(gRuntimeUsedCount==MAX_BUILDINGS);
    for(int b=0;b<MAX_BUILDINGS;b++)IndexCheck(RuntimeFindBuildingUID(gBuildings[b].uid)==b);
    for(int kind=0;kind<20;kind++)
    {
        int last=-1;
        for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].alive && view_as<int>(gBuildings[b].kind)==kind)
        {int next=RuntimeNextBuilding(last,kind);IndexCheck(next==b);last=next;}
        IndexCheck(RuntimeNextBuilding(last,kind)==-1);
    }
    for(int core=0;core<MAX_CORES;core++)
    {
        int last=-1;
        for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].powerCore==core)
        {int next=RuntimeNextPowerBuilding(last,core);IndexCheck(next==b);last=next;}
        IndexCheck(RuntimeNextPowerBuilding(last,core)==-1);
    }
    int last=-1;for(int b=0;b<MAX_BUILDINGS;b++){int next=RuntimeNextBuilding(last,-1,true);IndexCheck(next==b);last=next;}
    IndexCheck(RuntimeFindBuildingUID(0)==-1 && RuntimeFindBuildingUID(-1)==-1);
    gBuildings[11].alive=false;InvalidateRuntimeBuildings();IndexCheck(RuntimeNextBuilding(10)==13);
    gBuildings[11].alive=true;InvalidateRuntimeBuildings();IndexCheck(RuntimeNextBuilding(10)==11);
    gBuildings[5].uid=123;InvalidateRuntimeBuildings();IndexCheck(RuntimeNextBuilding(10)==11);
    int replaced=gBuildings[5].uid;gBuildings[5].uid=456;InvalidateRuntimeBuildings();IndexCheck(RuntimeFindBuildingUID(replaced)==-1 && RuntimeFindBuildingUID(456)==5);
    int duplicate=gBuildings[900].uid;gBuildings[1].uid=duplicate;InvalidateRuntimeBuildings();IndexCheck(RuntimeFindBuildingUID(duplicate)==1);
    gBuildings[1].used=false;InvalidateRuntimeBuildings();IndexCheck(RuntimeFindBuildingUID(duplicate)==900);
    for(int b=0;b<MAX_BUILDINGS;b++)gBuildings[b].used=false;InvalidateRuntimeBuildings();EnsureRuntimeBuildings();
    IndexCheck(gRuntimeUsedCount==0 && gRuntimeAliveCount==0 && RuntimeFindBuildingUID(duplicate)==-1 && RuntimeNextPowerBuilding(-1,0)==-1);
    PrintToServer("[V083Index] checks=%d failures=%d; exact production ordered views/UID hash, full storage, deliberate 1024-way hash collisions, structural mutation and reuse; isolated records",gV083IndexChecks,gV083IndexFailures);
}
