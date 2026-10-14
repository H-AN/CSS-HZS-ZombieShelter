// Test-only compilation of the production source with extra server assertions.
// Never ship this SMX as the production plugin.
#include "../addons/sourcemod/scripting/HanZombieShelter.sp"
#include "LiveManagementProbe.inc"
#include "LiveClientProbe.inc"
#include "MenuProbe.inc"
#include "ProgressionProbe.inc"
#include "SafetyEconomyProbe.inc"
#include "AdminProbe.inc"
#include "DayPopulationProbe.inc"
#include "GrowthProbe.inc"
#include "ResourceRerollProbe.inc"
#include "PowerProbe.inc"

int tPassed,tFailed;
int tSavedBossHP;
int tNativeAttacks;
int tKilledOrdinaryRef=INVALID_ENT_REFERENCE;
int tPopulationBossHP;
int tResetDay,tResetStock,tResetPersonal;
int tSavedResourceMaterial,tSavedResourceRef;

public void Han_OnZombieAttack(int zombie,int target)
{
    tNativeAttacks++;
}

public void OnAllPluginsLoaded()
{
    SetupLiveProbe();
    RegServerCmd("zsh_test",TestCommand);
    RegServerCmd("zsh_test_bounds",BoundsCommand);
    RegServerCmd("zsh_test_client",TestFixtureCommand);
}

public Action TestFixtureCommand(int args)
{
    if(Han_GetZombieCount()>0){PrintToServer("[ShelterTest] FAIL fixture must join before native NPCs");return Plugin_Handled;}
    int client=TestHuman();
    if(client==0)
    {
        client=CreateFakeClient("ShelterFixture");
        if(client>0){ChangeClientTeam(client,CS_TEAM_CT);CS_RespawnPlayer(client);}
    }
    int fixtures;for(int i=1;i<=MaxClients;i++)if(LiveHuman(i))fixtures++;
    if(fixtures<2)
    {
        int second=CreateFakeClient("ShelterFixtureTwo");
        if(second>0){ChangeClientTeam(second,CS_TEAM_CT);CS_RespawnPlayer(second);}
    }
    PrintToServer("[ShelterFixture] client=%d alive=%d",client,LiveHuman(client));
    return Plugin_Handled;
}

public Action BoundsCommand(int args)
{
    PrintToServer("[ShelterBounds] blackout_in_GAME=%d",FileExists("models/blackout.mdl",true));
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!gNPCs[n].used || (n!=0 && gNPCs[n].boss<0))continue;
        int entity=Resolve(gNPCs[n].ref);if(entity<0)continue;
        float mins[3],maxs[3];NPCBounds(entity,mins,maxs);
        PrintToServer("[ShelterBounds] boss=%d scale=%.2f mins=%.2f %.2f %.2f maxs=%.2f %.2f %.2f",gNPCs[n].boss,GetEntPropFloat(entity,Prop_Send,"m_flModelScale"),mins[0],mins[1],mins[2],maxs[0],maxs[1],maxs[2]);
    }
    return Plugin_Handled;
}

void Check(bool condition,const char[] name)
{
    char logPath[PLATFORM_MAX_PATH];BuildPath(Path_SM,logPath,sizeof(logPath),"logs/shelter_probe_assertions.log");
    LogToFileEx(logPath,"%s %s",condition ? "PASS" : "FAIL",name);
    if(condition){tPassed++;PrintToServer("[ShelterTest] PASS %s",name);}
    else{tFailed++;PrintToServer("[ShelterTest] FAIL %s",name);}
}

int TestHuman()
{
    for(int i=1;i<=MaxClients;i++)if(LiveHuman(i) && !IsFakeClient(i))return i;
    for(int i=1;i<=MaxClients;i++)if(LiveHuman(i))return i;return 0;
}

void Aim(int client,const float origin[3],const float point[3])
{
    float eye[3],direction[3],angles[3];eye=origin;eye[2]+=64.0;MakeVectorFromPoints(eye,point,direction);GetVectorAngles(direction,angles);
    TeleportEntity(client,origin,angles,view_as<float>({0.0,0.0,0.0}));
}

void AtBase(int client)
{
    TeleportEntity(client,gCorePoints[0][0],gCorePointAngles[0][0],view_as<float>({0.0,0.0,0.0}));SetEntityHealth(client,1000);
}

public Action TestCommand(int args)
{
    char which[32];GetCmdArg(1,which,sizeof(which));
    if(StrEqual(which,"summary")){PrintToServer("[ShelterTestSummary] Passed=%d Failed=%d",tPassed,tFailed);return Plugin_Handled;}
    int client=TestHuman();if(client==0){PrintToServer("[ShelterTest] FAIL no living fixture client");return Plugin_Handled;}
    bool savedPower=gPowerEnabled;
    // Existing independent economy/combat cases also verify the supported
    // power-disabled compatibility setting. The power cases use real supply.
    if(StrContains(which,"power")!=0)gPowerEnabled=false;
    if(StrEqual(which,"world"))TestWorld(client);
    else if(StrEqual(which,"progression"))TestProgression(client);
    else if(StrEqual(which,"safety_economy"))TestSafetyEconomy(client);
    else if(StrEqual(which,"admin_menus"))TestAdminMenus(client);
    else if(StrEqual(which,"reset_day"))
    {
        tResetDay=gDay;tResetStock=gBaseInventory[0][0];tResetPersonal=gPlayerInventory[client][0];
        RequestDayReset();Check(gPhase==Phase_Ending && gDay==tResetDay,"administrator day reset queues a real round end without decrementing the day");
    }
    else if(StrEqual(which,"reset_restored"))
    {
        Check(Running() && gDay==tResetDay && gPhase==Phase_Day,"administrator reset rebuilds the same day through native round lifecycle");
        Check(gBaseInventory[0][0]==tResetStock && gPlayerInventory[client][0]==tResetPersonal,"administrator reset retains shared and personal materials");
    }
    else if(StrEqual(which,"day_population"))TestDayPopulation(client);
    else if(StrEqual(which,"growth"))TestGrowth(client);
    else if(StrEqual(which,"resource_reroll"))TestResourceReroll(client);
    else if(StrEqual(which,"power"))TestPower(client);
    else if(StrEqual(which,"power_retention"))TestPowerRetention(client);
    else if(StrEqual(which,"power_restored"))TestPowerRestored(client);
    else if(StrEqual(which,"power_upgrade_prepare"))TestPowerUpgradePrepare(client);
    else if(StrEqual(which,"power_upgrade_verify"))TestPowerUpgradeVerify(client);
    else if(StrEqual(which,"resource_ui"))TestPrepareResourceRerollUI(client);
    else if(StrEqual(which,"resource_verify_ui"))
    {
        char stage[8];GetCmdArg(2,stage,sizeof(stage));TestVerifyResourceRerollUI(client,StringToInt(stage));
    }
    else if(StrEqual(which,"resource_restore_ui"))TestRestoreResourceRerollUI();
    else if(StrEqual(which,"growth_base_ui"))TestPrepareGrowthUI(client,false);
    else if(StrEqual(which,"growth_turret_ui"))TestPrepareGrowthUI(client,true);
    else if(StrEqual(which,"growth_verify_ui"))TestVerifyGrowthUI(client);
    else if(StrEqual(which,"menus"))TestBaseMenus(client);
    else if(StrEqual(which,"gather"))TestGather(client);
    else if(StrEqual(which,"inventory"))TestInventory(client);
    else if(StrEqual(which,"upgrade"))TestUpgrade(client);
    else if(StrEqual(which,"placement"))TestPlacement(client);
    else if(StrEqual(which,"defenses"))TestDefenses(client);
    else if(StrEqual(which,"wall"))TestWall(client);
    else if(StrEqual(which,"boss"))TestBoss(client);
    else if(StrEqual(which,"death"))TestDeath(client);
    else if(StrEqual(which,"reborn"))TestReborn();
    else if(StrEqual(which,"night_population"))TestNightPopulation();
    else if(StrEqual(which,"dawn_population"))TestDawnPopulation();
    else if(StrEqual(which,"retention"))TestRetention(client);
    else if(StrEqual(which,"restored"))TestRestored(client);
    else if(StrEqual(which,"wipe"))TestWipe(client);
    else if(StrEqual(which,"purged"))TestPurged(client);
    else if(StrEqual(which,"victory"))TestVictory(client);
    else if(StrEqual(which,"summary"))PrintToServer("[ShelterTestSummary] Passed=%d Failed=%d",tPassed,tFailed);
    else PrintToServer("[ShelterTest] Unknown case");
    gPowerEnabled=savedPower;RecalculatePower();
    return Plugin_Handled;
}

void TestWorld(int client)
{
    Check(Running() && gCoreCount==3 && AliveCores()==3 && gNodeCount==32,"world has three live bases and 32 nodes");
    Check(Han_GetZombieCount()==gNativePoolSize+gBossCount,"actual unmodified core registry matches configured ordinary pool plus bosses");
    Check(gPhase==Phase_Day && DayTarget()>0 && ActiveOrdinary()==DayTarget(),"daytime keeps the configured small ordinary population active");
    int parked;
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!gNPCs[n].used || gNPCs[n].boss>=0 || gNPCs[n].dead)continue;
        int entity=Resolve(gNPCs[n].ref);if(entity<0)continue;
        float p[3];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",p);
        if(gNPCs[n].paused && p[2]<-10000.0 && GetEntProp(entity,Prop_Data,"m_takedamage")==0 && GetEntityMoveType(entity)==MOVETYPE_NONE)parked++;
    }
    Check(parked==gNativePoolSize-DayTarget(),"only excess ordinary bodies are frozen outside gameplay area");
    for(int c=0;c<gCoreCount;c++)
    {
        Check(InSafeBase(gCorePoints[c][0]),"each measured base interior protects feet");
        float local[3],outsideWall[3];
        local[0]=gCoreMins[c][0][0]+16.0;local[1]=(gCoreMins[c][0][1]+gCoreMaxs[c][0][1])*0.5;local[2]=2.0;
        WorldPoint(local,gCores[c].origin,gCores[c].angles,outsideWall);
        Check(InSafeBase(outsideWall) && SafeCoreAt(outsideWall)==c,"expanded outdoor safety identifies its own base");
        float roof[3];roof=gCorePoints[c][0];roof[2]=gCores[c].origin[2]+gCoreMaxs[c][0][2]+20.0;
        Check(!InSafeBase(roof),"positions above the configured safety height remain outside");
    }
    AtBase(client);float position[3];GetClientAbsOrigin(client,position);Check(InSafeBase(position),"fixture player is inside safe union");
    gPhase=Phase_Night;gNextExposure=0.0;int hp=GetClientHealth(client);ExposureTick(GetGameTime());Check(GetClientHealth(client)==hp,"indoor night exposure causes no damage");
    position=gBosses[0].origin;TeleportEntity(client,position,NULL_VECTOR,NULL_VECTOR);SetEntityHealth(client,1000);gNextExposure=0.0;
    ExposureTick(GetGameTime());Check(gExposureDamage==1 && GetClientHealth(client)==999,"outdoor night exposure deducts the configured one actual HP");
    int building=gCores[0].building,savedHP=gBuildings[building].hp;
    DamageBuilding(building,savedHP);
    Check(AliveCores()==2 && !gFailureCandidate && InSafeBase(gCorePoints[0][0]),"destroyed base A retains safety while B and C survive");
    AtBase(client);gNextExposure=0.0;hp=GetClientHealth(client);ExposureTick(GetGameTime());
    Check(GetClientHealth(client)==hp,"night exposure does not punish defenders in a destroyed base zone");
    gBuildings[building].alive=true;gBuildings[building].hp=savedHP;
    Check(SpawnBuildingBody(building) && AliveCores()==3,"safety test restores its destroyed base without a campaign reset");
    AtBase(client);StartDay(false);
}

void TestNightPopulation()
{
    Check(gPhase==Phase_Night && ActiveOrdinary()==NightTarget() && NightTarget()>DayTarget(),"night admits the configured larger ordinary population");
    Check(Han_GetZombieCount()==gNativePoolSize+gBossCount,"night expansion reuses the native pool without extra spawns");
    tPopulationBossHP=gBosses[0].savedHP;
}

void TestDawnPopulation()
{
    Check(gPhase==Phase_Day && ActiveOrdinary()==DayTarget(),"dawn keeps daytime raiders and parks only the excess");
    int boss=-1;for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && !gNPCs[n].dead && gNPCs[n].boss==0){boss=n;break;}
    Check(boss>=0 && gNPCs[boss].active && gBosses[0].savedHP==tPopulationBossHP,"dawn population reduction preserves fixed boss health and presence");
}

void TestGather(int client)
{
    StartDay(false);int n=0,entity=Resolve(gNodes[n].ref);Check(entity>0,"resource body exists");if(entity<0)return;
    int knife=GetPlayerWeaponSlot(client,2);if(knife==-1)knife=GivePlayerItem(client,"weapon_knife");EquipPlayerWeapon(client,knife);SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",knife);
    float center[3],player[3];EntityCenter(entity,center);player=gNodes[n].origin;player[0]+=75.0;Aim(client,player,center);
    Check(AimedEntity(client,gUseDistance)==entity,"real player aim reaches resource collider");
    int material=gNodes[n].material,before=gPlayerInventory[client][material],quota=gNodes[n].remaining;
    bool unitYield=true;for(int m=0;m<MATERIALS;m++)if(gMaterials[m].yield!=1)unitYield=false;
    Check(unitYield,"all sample materials grant one item per successful knife hit");
    int rifle=GetPlayerWeaponSlot(client,0);if(rifle<0)rifle=GivePlayerItem(client,"weapon_ak47");
    EquipPlayerWeapon(client,rifle);SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",rifle);gNextGather[client]=0.0;
    SDKHooks_TakeDamage(entity,client,client,1.0,DMG_BULLET|DMG_NEVERGIB,_,_,_,false);
    Check(gNodes[n].remaining==quota && gPlayerInventory[client][material]==before,"4098 damage from a firearm never harvests materials");
    EquipPlayerWeapon(client,knife);SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",knife);
    for(int i=0;i<quota;i++)
    {
        gNextGather[client]=0.0;Aim(client,player,center);SDKHooks_TakeDamage(entity,client,client,1.0,DMG_BULLET|DMG_NEVERGIB,_,_,_,false);
        Check(gNodes[n].remaining==quota-i-1 && gPlayerInventory[client][material]==before+i+1,"CS:S 4098 knife hit consumes one quota and grants one personal item");
    }
    Check(gPlayerInventory[client][material]-before==quota*gMaterials[material].yield,"last successful knife hit grants once and quota is exact");
    Check(gNodes[n].growing && gNodes[n].remaining==0,"resource depletes into disabled growth state");
    int red,green,blue,alpha;GetEntityRenderColor(entity,red,green,blue,alpha);Check(alpha==0 && (GetEntProp(entity,Prop_Send,"m_usSolidFlags")&4)!=0,"exhausted resource is transparent and nonblocking");
    Check(AimedEntity(client,gUseDistance)!=entity,"engine solid trace passes through exhausted resource");
    int inventory[MATERIALS];inventory=gPlayerInventory[client];gNextGather[client]=0.0;
    SDKHooks_TakeDamage(entity,client,client,1.0,DMG_BULLET|DMG_NEVERGIB,_,_,_,false);
    bool unchanged=true;for(int m=0;m<MATERIALS;m++)if(gPlayerInventory[client][m]!=inventory[m])unchanged=false;
    Check(unchanged,"disabled resource cannot grant any material, including its next cycle type");
    int previousRef=gNodes[n].ref;ResourceTick(0.0);int nextMaterial=gNodes[n].material;
    Check(!gNodes[n].rerollPending && gNodes[n].ref==previousRef && nextMaterial>=0 && nextMaterial<MATERIALS,
        "depleted resource selects its next cycle once while retaining the same entity reference");
    gNodes[n].elapsed=gMaterials[nextMaterial].hold+gMaterials[nextMaterial].grow*0.5;UpdateResourceVisual(n);GetEntityRenderColor(entity,red,green,blue,alpha);Check(alpha>=126 && alpha<=129,"half growth uses the newly selected material duration");
    TeleportEntity(client,gNodes[n].origin,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    gNodes[n].elapsed=gMaterials[nextMaterial].hold+gMaterials[nextMaterial].grow-0.1;ResourceTick(0.11);
    Check(gNodes[n].growing && gNodes[n].remaining==0 && (GetEntProp(entity,Prop_Send,"m_usSolidFlags")&4)!=0,"mature resource waits without collision when a player occupies its model bounds");
    AtBase(client);ResourceTick(0.11);
    GetEntityRenderColor(entity,red,green,blue,alpha);Check(!gNodes[n].growing && gNodes[n].remaining==gMaterials[nextMaterial].quota && alpha==255,"full maturity resets the newly selected material harvest quota");
    AtBase(client);
}

void TestInventory(int client)
{
    StartDay(false);AtBase(client);
    int savedPersonal[MATERIALS],savedBases[MAX_CORES][MATERIALS];
    for(int m=0;m<MATERIALS;m++)
    {
        savedPersonal[m]=gPlayerInventory[client][m];gPlayerInventory[client][m]=0;
        for(int c=0;c<gCoreCount;c++){savedBases[c][m]=gBaseInventory[c][m];gBaseInventory[c][m]=0;}
    }
    gPlayerInventory[client][0]=10;
    Check(Grant(0,1,client)==1 && gPlayerInventory[client][0]==11 && gBaseInventory[0][0]==0,"material grant credits only the collecting player's backpack");
    Check(TransferBaseMaterial(client,0,0,7,true) && gPlayerInventory[client][0]==4 && gBaseInventory[0][0]==7,
        "deposit atomically moves personal wood into base A warehouse");
    Check(gBaseInventory[1][0]==0 && gBaseInventory[2][0]==0,"base B and C do not share base A warehouse balance");
    Check(!TransferBaseMaterial(client,0,0,5,true) && gPlayerInventory[client][0]==4 && gBaseInventory[0][0]==7,
        "deposit exceeding personal stock changes neither ledger");
    Check(!TransferBaseMaterial(client,0,0,8,false) && gPlayerInventory[client][0]==4 && gBaseInventory[0][0]==7,
        "withdrawal exceeding warehouse stock changes neither ledger");
    Check(!TransferBaseMaterial(client,0,0,0,true) && !TransferBaseMaterial(client,0,0,-1,false)
        && gPlayerInventory[client][0]==4 && gBaseInventory[0][0]==7,"zero and negative transfers cannot mint materials");
    TeleportEntity(client,gCorePoints[1][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    Check(!TransferBaseMaterial(client,0,0,1,true) && !TransferBaseMaterial(client,0,0,1,false)
        && gPlayerInventory[client][0]==4 && gBaseInventory[0][0]==7,"base B visitors cannot remotely deposit into or withdraw from base A");
    Check(!TransferBaseMaterial(client,1,0,1,false) && gPlayerInventory[client][0]==4 && gBaseInventory[1][0]==0,
        "an empty local warehouse cannot spend another base's materials");
    AtBase(client);
    Check(TransferBaseMaterial(client,0,0,3,false) && gPlayerInventory[client][0]==7 && gBaseInventory[0][0]==4,
        "local withdrawal atomically returns warehouse material to the backpack");
    int other;for(int i=1;i<=MaxClients;i++)if(i!=client && LiveHuman(i)){other=i;break;}
    if(other>0)
    {
        float origin[3],angles[3],velocity[3];GetClientAbsOrigin(other,origin);GetClientEyeAngles(other,angles);
        GetEntPropVector(other,Prop_Data,"m_vecAbsVelocity",velocity);int savedWood=gPlayerInventory[other][0];
        gPlayerInventory[other][0]=0;TeleportEntity(other,gCorePoints[0][1],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
        Check(TransferBaseMaterial(other,0,0,2,false) && gPlayerInventory[other][0]==2 && gBaseInventory[0][0]==2
            && gPlayerInventory[client][0]==7,"a second player's backpack can withdraw shared base stock without changing the depositor's backpack");
        gPlayerInventory[other][0]=savedWood;TeleportEntity(other,origin,angles,velocity);
    }
    else PrintToServer("[ShelterInventory] One living fixture: shared warehouse ledger verified; second-client transfer requires a second living client");
    for(int m=0;m<MATERIALS;m++)
    {
        gPlayerInventory[client][m]=savedPersonal[m];
        for(int c=0;c<gCoreCount;c++)gBaseInventory[c][m]=savedBases[c][m];
    }
    AtBase(client);
}

void TestUpgrade(int client)
{
    StartDay(false);AtBase(client);for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=1000;
    int b=gCores[0].building;gBuildings[b].hp=gBuildings[b].maxHP-100;
    int beforeWood=gPlayerInventory[client][0],beforeIron=gPlayerInventory[client][1];Check(TryUpgrade(client,b),"base upgrade succeeds with recipe materials");
    Check(gBuildings[b].level==2 && gBuildings[b].hp==1900 && gBuildings[b].maxHP==2000,"upgrade preserves missing HP amount");
    Check(beforeWood-gPlayerInventory[client][0]==50 && beforeIron-gPlayerInventory[client][1]==20,"base upgrade spends exact wood and iron once");
    int hp=gBuildings[b].hp,entity=Resolve(gBuildings[b].ref);SDKHooks_TakeDamage(entity,client,client,100.0,DMG_BULLET,_,_,_,false);Check(gBuildings[b].hp==hp,"friendly bullet damage does not damage base");
    int knife=GetPlayerWeaponSlot(client,2);EquipPlayerWeapon(client,knife);SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",knife);
    float point[3],player[3];point=gCores[0].origin;point[0]-=160.0;point[1]-=128.0;point[2]+=45.0;
    player=point;player[1]-=70.0;player[2]=gCores[0].origin[2]+2.0;Aim(client,player,point);
    Check(AimedEntity(client,gUseDistance)==entity,"real knife aim reaches base wall collider");
    gNextRepair[client]=0.0;SDKHooks_TakeDamage(entity,client,client,1.0,DMG_BULLET|DMG_NEVERGIB,_,_,_,false);Check(gBuildings[b].hp==hp+10,"knife repairs logical shared house HP");
    for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=0;int level=gBuildings[b].level;
    Check(!TryUpgrade(client,b) && gBuildings[b].level==level,"insufficient materials leave instance unchanged");
    AtBase(client);
}

int TestBuild(int recipe,const float origin[3])
{
    int slot=-1;for(int i=gCoreCount;i<MAX_BUILDINGS;i++)if(!gBuildings[i].used || !gBuildings[i].alive){slot=i;break;}
    if(slot<0)return -1;
    Building value;value.used=true;value.alive=true;value.kind=gRecipes[recipe].kind;value.uid=++gUID;value.core=-1;value.file=gRecipes[recipe].file;value.level=1;value.ref=INVALID_ENT_REFERENCE;value.origin=origin;
    strcopy(value.type,sizeof(value.type),gRecipes[recipe].type);char path[128];Format(path,sizeof(path),"Types/%s",value.type);Text(value.file,path,"model",value.model,sizeof(value.model));
    gBuildings[slot]=value;if(!ApplyBuildingLevel(slot,true) || !SpawnBuildingBody(slot))return -1;return slot;
}

void TestDefenses(int client)
{
    StartNight();int n=-1;for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && gNPCs[i].ready && !gNPCs[i].dead && gNPCs[i].boss<0){n=i;break;}
    Check(n>=0 && ActivateOrdinary(n),"ordinary NPC activates at a collision checked native T point");if(n<0)return;
    int entity=Resolve(gNPCs[n].ref);float target[3];target=gBosses[0].origin;target[0]+=450.0;TeleportEntity(entity,target,NULL_VECTOR,NULL_VECTOR);SetEntProp(entity,Prop_Data,"m_iHealth",2000);
    int hp=GetEntProp(entity,Prop_Data,"m_iHealth"),expected=25;ApplyProgressionDamage(n,client,expected,true);
    Check(DamageNPC(n,25,GetClientSerial(client),true),"existing Han safe damage API accepts external team defense");
    Check(GetEntProp(entity,Prop_Data,"m_iHealth")==hp-expected,"API deducts the progressed defense damage exactly once");
    float tower[3];tower=target;tower[0]-=160.0;int b=TestBuild(0,tower);Check(b>=0,"ballistic turret entity uses prepared BSP model");
    if(b>=0){gBuildings[b].nextFire=0.0;hp=GetEntProp(entity,Prop_Data,"m_iHealth");TurretFire(b,GetGameTime());Check(GetEntProp(entity,Prop_Data,"m_iHealth")<hp,"automatic turret damages visible target");}
    int flame=-1,mine=-1;
    for(int r=0;r<gRecipeCount;r++){if(gRecipes[r].kind==Kind_Flame)flame=r;if(gRecipes[r].kind==Kind_Mine)mine=r;}
    tower=target;tower[1]+=120.0;b=TestBuild(flame,tower);Check(b>=0,"flame defense entity exists");
    if(b>=0)
    {
        gBuildings[b].nextFire=0.0;hp=GetEntProp(entity,Prop_Data,"m_iHealth");float now=GetGameTime();TurretFire(b,now);
        Check(GetEntProp(entity,Prop_Data,"m_iHealth")<hp && gNPCs[n].burnUntil>now,"flame cone applies primary hit and schedules burn");
        float next=gNPCs[n].nextBurn;TurretFire(b,now+0.25);Check(gNPCs[n].nextBurn==next,"continuous flame does not postpone every burn tick");
    }
    tower=target;tower[0]+=10.0;b=TestBuild(mine,tower);Check(b>=0,"mine entity exists");
    if(b>=0){MineTick(b);Check(!gBuildings[b].alive && gBuildings[b].hp==0 && Resolve(gBuildings[b].ref)<0,"mine consumes exactly once before damage and removes body");}
    AtBase(client);StartDay(false);
}

void TestBoss(int client)
{
    int n=-1;for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && gNPCs[i].boss==0 && !gNPCs[i].dead){n=i;break;}
    Check(n>=0 && !gBosses[0].awakened,"fixed boss starts dormant");if(n<0)return;
    Check(!DefenseTarget(n) && DefenseTarget(n,true),"turret and flame exclude dormant boss but owned mines may hit");
    int entity=Resolve(gNPCs[n].ref),hp=GetEntProp(entity,Prop_Data,"m_iHealth"),expected=10;ApplyProgressionDamage(n,client,expected,false);
    DamageNPC(n,10,GetClientSerial(client));NPCTick(GetGameTime());
    Check(gBosses[0].awakened && gBosses[0].savedHP==hp-expected,"valid progressed HP loss alone awakens boss and updates shadow HP");
    StartNight();StartDay(false);Check(gNPCs[n].active && !gNPCs[n].paused,"boss is not parked at dawn");AtBase(client);
}

void TestWall(int client)
{
    StartNight();int n=-1;for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && gNPCs[i].ready && !gNPCs[i].dead && gNPCs[i].boss<0){n=i;break;}
    if(n<0){Check(false,"wall test has an ordinary native NPC");return;}
    ActivateOrdinary(n);int entity=Resolve(gNPCs[n].ref),b=gCores[0].building;
    float local[3]={-390.0,128.0,2.0},position[3];WorldPoint(local,gCores[0].origin,gCores[0].angles,position);TeleportEntity(entity,position,NULL_VECTOR,NULL_VECTOR);
    float mins[3],maxs[3];NPCBounds(entity,mins,maxs);Check(ClearHull(position,mins,maxs,entity),"wall approach starts with a clear native NPC hull");
    local[0]=-288.0;WorldPoint(local,gCores[0].origin,gCores[0].angles,position);TeleportEntity(client,position,NULL_VECTOR,NULL_VECTOR);SetEntityHealth(client,1000);SetEntProp(client,Prop_Data,"m_takedamage",2,1);
    // Use the genuine core Think callback to establish its own target.
    Han_SetCustomAnimationEx(entity,gNPCs[n].idle,0.0);
    InvokeCoreThink(entity);
    Check(GetEntDataEnt2(entity,gLeaderOffset)==client,"HZS core itself acquires fixture player");
    DebugWall(n,client,b);
    Check(BlockingBuilding(n,client)==b,"core target route is blocked by the actual base wall collider");
    int hp=gBuildings[b].hp;gNPCs[n].nextAttack=0.0;NPCTick(GetGameTime());
    Check(gBuildings[b].hp<hp && gNPCs[n].attackingUID==gBuildings[b].uid,"close zombie stops and attacks registered house HP");
    int attacks=tNativeAttacks;InvokeCoreThink(entity);
    Check(tNativeAttacks==attacks && GetEntityMoveType(entity)==MOVETYPE_NONE,"existing custom animation API suppresses core melee and movement");
    hp=GetClientHealth(client);SDKHooks_TakeDamage(client,entity,entity,25.0,DMG_SLASH,_,_,_,false);
    Check(GetClientHealth(client)==hp,"infinite acquisition does not allow melee damage through house wall");
    StartDay(false);AtBase(client);
}

void InvokeCoreThink(int entity)
{
    Handle iterator=GetPluginIterator();Handle core=null;
    while(MorePlugins(iterator))
    {
        Handle candidate=ReadPlugin(iterator);char file[128];GetPluginFilename(candidate,file,sizeof(file));
        if(StrEqual(file,"UnmodifiedHZS.smx")){core=candidate;break;}
    }
    delete iterator;
    if(core==null)return;
    Function brain=GetFunctionByName(core,"ZombieThink");
    if(brain!=INVALID_FUNCTION){Call_StartFunction(core,brain);Call_PushCell(entity);Call_Finish();}
}

void TestPlacement(int client)
{
    StartDay(false);for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=1000;
    char model[PLATFORM_MAX_PATH],path[128];Format(path,sizeof(path),"Types/%s",gRecipes[0].type);Text(gRecipes[0].file,path,"model",model,sizeof(model));
    bool found=false;float placement[3],angles[3];
    for(int r=0;r<3 && !found;r++)for(int step=0;step<16 && !found;step++)
    {
        float heading=DegToRad(float(step)*22.5),position[3],ground[3];position=gCores[0].origin;
        position[0]+=Cosine(heading)*(500.0+float(r)*150.0);position[1]+=Sine(heading)*(500.0+float(r)*150.0);
        float end[3];position[2]+=300.0;end=position;end[2]-=500.0;
        Handle trace=TR_TraceRayFilterEx(position,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
        bool hit=TR_DidHit(trace) && !TR_StartSolid(trace);if(hit)TR_GetEndPosition(ground,trace);delete trace;if(!hit)continue;
        position=ground;position[2]+=2.0;float mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0};
        if(!ClearHull(position,mins,maxs,client,MASK_PLAYERSOLID))continue;
        float aim[3];aim=ground;aim[0]+=Cosine(heading)*120.0;aim[1]+=Sine(heading)*120.0;
        Aim(client,position,aim);found=Placement(client,model,placement,angles);
    }
    Check(found,"player ground aim finds a complete collision checked turret placement");
    if(found)
    {
        int before=SummonCount(),materials[MATERIALS];materials=gPlayerInventory[client];
        Check(BuildRecipe(client,0) && SummonCount()==before+1,"actual construction transaction creates one owned defense");
        bool exact=true;for(int m=0;m<MATERIALS;m++)if(materials[m]-gPlayerInventory[client][m]!=gRecipes[0].cost[m])exact=false;
        Check(exact,"construction spends each recipe material exactly once");
        materials=gPlayerInventory[client];Check(!BuildRecipe(client,0),"overlapping repeated construction is rejected");
        exact=true;for(int m=0;m<MATERIALS;m++)if(materials[m]!=gPlayerInventory[client][m])exact=false;
        Check(exact,"rejected placement never spends team materials");
        for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive)DamageBuilding(b,gBuildings[b].hp);
    }
    AtBase(client);
}

void TestWipe(int client)
{
    gDestroySummons=true;gDirectLoss=false;
    for(int i=1;i<=MaxClients;i++)if(LiveHuman(i))ForcePlayerSuicide(i);
    PrintToServer("[ShelterWipe] client=%d waiting for actual core player_death and Han_OnZombieWin",client);
}

void TestVictory(int client)
{
    int n=-1;for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && !gNPCs[i].dead && gNPCs[i].boss==0){n=i;break;}
    Check(n>=0,"victory uses a real native boss after round restoration");if(n<0)return;
    gOutcomeSeconds=1.5;SetNextMap(gMap);
    // Opposite candidates within one frame must settle on confirmed Boss victory.
    for(int c=0;c<gCoreCount;c++)DamageBuilding(gCores[c].building,gBuildings[gCores[c].building].hp);
    Check(gFailureCandidate && AliveCores()==0,"all three bases destroyed queues external failure");
    int entity=Resolve(gNPCs[n].ref);int hp=GetEntProp(entity,Prop_Data,"m_iHealth");
    bool progression=gProgressionEnabled;gProgressionEnabled=false;
    Check(DamageNPC(n,hp+1,GetClientSerial(client)) && gBosses[0].defeated,"actual Han safe damage boss kill queues confirmed victory");
    gProgressionEnabled=progression;
    RequestFrame(VerifyVictory,gEpoch);
}

public void VerifyVictory(any epoch)
{
    Check(epoch==gEpoch && gTerminal && gVictoryCandidate,"confirmed boss victory wins same-frame loss and enters outcome HUD");
}

void DebugWall(int n,int client,int b)
{
    int entity=Resolve(gNPCs[n].ref),body=Resolve(gBuildings[b].ref);
    float from[3],to[3],mins[3],maxs[3];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",from);GetClientAbsOrigin(client,to);NPCBounds(entity,mins,maxs);
    PrintToServer("[ShelterWall] npc=%d body=%d target=%d from=%.1f %.1f %.1f to=%.1f %.1f %.1f clear=%d",entity,body,GetEntDataEnt2(entity,gLeaderOffset),from[0],from[1],from[2],to[0],to[1],to[2],ClearHull(from,mins,maxs,entity));
    Handle trace=TR_TraceHullFilterEx(from,to,mins,maxs,MASK_NPCSOLID,FilterNoClients,entity);
    PrintToServer("[ShelterWall] hull hit=%d start=%d all=%d fraction=%.3f",TR_GetEntityIndex(trace),TR_StartSolid(trace),TR_AllSolid(trace),TR_GetFraction(trace));delete trace;
    GetEntPropVector(body,Prop_Send,"m_vecMins",mins);GetEntPropVector(body,Prop_Send,"m_vecMaxs",maxs);
    PrintToServer("[ShelterWall] base bounds %.1f %.1f %.1f / %.1f %.1f %.1f",mins[0],mins[1],mins[2],maxs[0],maxs[1],maxs[2]);
    for(int h=0;h<3;h++)
    {
        float a[3],z[3];a=from;z=to;a[2]+=18.0+18.0*float(h);z[2]+=18.0+18.0*float(h);
        trace=TR_TraceRayFilterEx(a,z,MASK_NPCSOLID,RayType_EndPoint,FilterNoClients,entity);
        PrintToServer("[ShelterWall] ray %d hit=%d start=%d fraction=%.3f",h,TR_GetEntityIndex(trace),TR_StartSolid(trace),TR_GetFraction(trace));delete trace;
    }
}

void TestRetention(int client)
{
    StartDay(false);gDay=5;gDestroySummons=false;gDirectLoss=false;
    int b=gCores[0].building;gBuildings[b].hp=500;
    int c=gCores[1].building;DamageBuilding(c,gBuildings[c].hp);
    Check(AliveCores()==2 && !gFailureCandidate,"one destroyed base does not lose the campaign");
    ResourceTick(0.0); // Finish any actual depletion draw before recording its next-cycle snapshot.
    gNodes[0].growing=true;gNodes[0].rerollPending=false;gNodes[0].remaining=0;gNodes[0].elapsed=12.0;UpdateResourceVisual(0);
    tSavedResourceMaterial=gNodes[0].material;tSavedResourceRef=gNodes[0].ref;
    for(int m=0;m<MATERIALS;m++)
    {
        gPlayerInventory[client][m]=123;gBaseInventory[0][m]=41;gBaseInventory[1][m]=67;
    }
    QueueOutcome(false);RequestFrame(RecordExpected);AtBase(client);
}

public void RecordExpected(any unused)
{
    tSavedBossHP=gBosses[0].savedHP;
}

void TestDeath(int client)
{
    StartDay(false);int n=-1;for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && gNPCs[i].ready && gNPCs[i].active && !gNPCs[i].dead && gNPCs[i].boss<0){n=i;break;}
    if(n<0){Check(false,"daytime combat death has an active ordinary native body");return;}
    int entity=Resolve(gNPCs[n].ref),before=ActiveOrdinary(),kills=gNativeKills;tKilledOrdinaryRef=gNPCs[n].ref;
    SetEntProp(entity,Prop_Data,"m_iHealth",10);
    DamageNPC(n,20,GetClientSerial(client));Check(gNPCs[n].dead && gNativeKills==kills+1,"daytime real Han API death notification is received once");
    Check(ActiveOrdinary()==before-1,"a daytime death removes exactly one active ordinary NPC");AtBase(client);
}

void TestReborn()
{
    Check(Resolve(tKilledOrdinaryRef)<0 && Han_GetZombieCount()==gNativePoolSize+gBossCount,"native corpse cleanup replenishes the configured daytime pool");
    Check(gPhase==Phase_Day && ActiveOrdinary()==DayTarget(),"daytime ordinary deaths are replenished back to the day target");
}

void TestRestored(int client)
{
    Check(gDay==4 && AliveCores()==3,"failure rolls one day back and restores all bases");
    int b=gCores[0].building;Check(gBuildings[b].level==2 && gBuildings[b].hp==500 && gBuildings[b].maxHP==2000,"upgraded live base keeps level and partial HP");
    b=gCores[1].building;Check(gBuildings[b].level==1 && gBuildings[b].hp==1250,"destroyed unupgraded base returns as basic base");
    Check(SummonCount()>=2,"surviving defenses rebuild without material cost");
    bool consumedStillGone=true;for(int i=gCoreCount;i<MAX_BUILDINGS;i++)if(gBuildings[i].used && gBuildings[i].kind==Kind_Mine && gBuildings[i].alive)consumedStillGone=false;
    Check(consumedStillGone,"consumed mine is not revived across round cleanup");
    Check(gPlayerInventory[client][0]==123 && gPlayerInventory[client][1]==123,"personal backpack survives the failed round without construction refund");
    Check(gBaseInventory[0][0]==41 && gBaseInventory[0][1]==41 && gBaseInventory[1][0]==67 && gBaseInventory[1][1]==67,
        "separate base warehouses survive failure and destroyed-base reconstruction");
    Check(gNodes[0].growing && gNodes[0].remaining==0 && gNodes[0].elapsed>=12.0 && gNodes[0].elapsed<20.0,"resource growth progress persists and pauses during waiting");
    int resource=Resolve(gNodes[0].ref);char resourceModel[PLATFORM_MAX_PATH];
    if(resource>0)GetEntPropString(resource,Prop_Data,"m_ModelName",resourceModel,sizeof(resourceModel));
    Check(resource>0 && gNodes[0].ref!=tSavedResourceRef && gNodes[0].material==tSavedResourceMaterial
        && !gNodes[0].rerollPending && StrEqual(resourceModel,gMaterials[tSavedResourceMaterial].model,false),
        "round restoration rebuilds the already selected next-cycle model without rerolling its material");
    Check(gBosses[0].awakened && gBosses[0].positionedHP==tSavedBossHP && tSavedBossHP>0 && gBosses[0].savedHP<=tSavedBossHP,"boss restores exact shadow HP before ongoing defense damage");
    AtBase(client);
}

void TestPurged(int client)
{
    Check(gDay==3 && AliveCores()==3 && SummonCount()==0,"real native all-dead loss rolls back and purges summons when enabled");
    Check(gPlayerInventory[client][0]==123 && gBaseInventory[0][0]==41 && gBaseInventory[1][0]==67,
        "summon purge does not erase personal backpacks or independent base warehouses");
}
