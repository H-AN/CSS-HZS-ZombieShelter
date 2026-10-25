// Diagnostic main only: include after the production includes, then call
// RegisterV082CraftingProbe() from OnPluginStart. No client/NPC is created.
// setup leaves the production construction blueprint unfinished. finish is an
// explicitly diagnostic completion, never evidence of a real melee build.
bool tV082Active,tV082OwnBench,tV082SavedExposure,tV082WeaponsRunning,tV082WorkActive;
bool tV082PowerGeneratorOff;
int tV082Serial,tV082Epoch,tV082Revision,tV082Bench=-1,tV082BenchUID,tV082Recipe=-1;
int tV082SavedInventory[MATERIALS],tV082Delta[MATERIALS],tV082SavedHP,tV082SavedDamage,tV082SavedGod;
int tV082Passed,tV082Failed,tV082WeaponIndex,tV082PendingWeapon=-1,tV082PendingBeforeRef;
float tV082SavedPosition[3],tV082SavedAngles[3],tV082SavedVelocity[3],tV082Feet[3];
MoveType tV082SavedMove;
Building tV082WeaponBench;
Building tV082WorkBench;
Building tV082PowerGeneratorSaved;
int tV082PowerGenerator=-1,tV082PowerGeneratorUID;
int tV082PlacementRejects[10],tV082PlacementAttempts,tV082PlacementFloorMisses;
static const char tV082PlacementReasons[][]={"viewer_hull","eye_surface","surface_policy","reserved_column","world_origin","model_hull","other_compute","anchor_mismatch","base_recipe_rules","accepted"};

void V082Log(const char[] row)
{
    PrintToServer("[V082Probe] %s",row);
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"logs/v082_crafting_probe.jsonl");
    File file=OpenFile(path,"a");if(file!=null){file.WriteLine("%s",row);delete file;}
}

void V082Check(bool passed,const char[] name)
{
    if(passed)tV082Passed++;else tV082Failed++;
    char row[512];Format(row,sizeof(row),"{\"time\":%d,\"kind\":\"assert\",\"name\":\"%s\",\"passed\":%s}",GetTime(),name,passed ? "true" : "false");V082Log(row);
}

int V082Human()
{
    int human,count,fakes;
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client))
    {
        if(IsFakeClient(client)){fakes++;continue;}
        if(LiveHuman(client) && !IsClientSourceTV(client) && !IsClientReplay(client)){human=client;count++;}
    }
    return count==1 && fakes==0 ? human : 0;
}

bool V082Session(int client)
{
    return tV082Active && client>0 && LiveHuman(client) && !IsFakeClient(client)
        && GetClientSerial(client)==tV082Serial && gEpoch==tV082Epoch && gConfigRevision==tV082Revision;
}

bool V082Bench()
{
    return tV082Bench>=0 && tV082Bench<MAX_BUILDINGS && gBuildings[tV082Bench].used && gBuildings[tV082Bench].alive
        && gBuildings[tV082Bench].uid==tV082BenchUID && Resolve(gBuildings[tV082Bench].ref)>0;
}

void V082LedgerChange(const int before[MATERIALS],const int after[MATERIALS])
{
    for(int m=0;m<gMaterialCount;m++)tV082Delta[m]+=after[m]-before[m];
}

void V082SetBalance(int client,int material,int amount)
{
    if(material<0 || material>=gMaterialCount)return;
    tV082Delta[material]+=amount-gPlayerInventory[client][material];gPlayerInventory[client][material]=amount;
}

void V082Fund(int client)
{
    int cap=gInventoryCap>0 ? gInventoryCap : LEVEL_CELL_LIMIT;
    int amount=cap<1000000 ? cap : 1000000;
    for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]<amount)V082SetBalance(client,m,amount);
}

bool V082SameInventory(int client,const int before[MATERIALS])
{
    for(int m=0;m<gMaterialCount;m++)if(before[m]!=gPlayerInventory[client][m])return false;
    return true;
}

void V082AimAt(int client,const float feet[3],const float target[3])
{
    float zero[3],eye[3],direction[3],angles[3];TeleportEntity(client,feet,NULL_VECTOR,zero);GetClientEyePosition(client,eye);
    MakeVectorFromPoints(eye,target,direction);GetVectorAngles(direction,angles);TeleportEntity(client,NULL_VECTOR,angles,zero);
}

bool V082TraceFloor(int client,const float start[3],const float end[3],float point[3])
{
    Handle trace=TR_TraceRayFilterEx(start,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
    bool good=TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);float normal[3];
    if(good){TR_GetEndPosition(point,trace);TR_GetPlaneNormal(trace,normal);good=normal[2]>=0.8;}
    delete trace;return good;
}

void V082PlacementCount(int reason,int core,const float feet[3],PlacementCandidate candidate,bool details)
{
    tV082PlacementRejects[reason]++;
    if(!details || tV082PlacementRejects[reason]>3)return;
    char row[1024];Format(row,sizeof(row),"{\"kind\":\"placement_candidate\",\"reason\":\"%s\",\"core\":%d,\"feet\":[%.3f,%.3f,%.3f],\"surface\":%d,\"surface_ref\":%d,\"anchor\":[%.3f,%.3f,%.3f],\"origin\":[%.3f,%.3f,%.3f],\"normal\":[%.3f,%.3f,%.3f]}",
        tV082PlacementReasons[reason],core,feet[0],feet[1],feet[2],candidate.surface,candidate.surfaceRef,
        candidate.anchor[0],candidate.anchor[1],candidate.anchor[2],candidate.origin[0],candidate.origin[1],candidate.origin[2],candidate.normal[0],candidate.normal[1],candidate.normal[2]);V082Log(row);
}

bool V082TryPlacementView(int client,int recipe,int core,const char[] model,const float feet[3],const float point[3],bool details)
{
    tV082PlacementAttempts++;float mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0};PlacementCandidate candidate;
    if(!ClearHull(feet,mins,maxs,client,MASK_PLAYERSOLID)){V082PlacementCount(0,core,feet,candidate,details);return false;}
    V082AimAt(client,feet,point);bool computed=ComputeRecipePlacementCandidate(client,recipe,candidate,false);
    if(!computed)
    {
        int reason=6,hit=candidate.surfaceRef==0 ? 0 : EntRefToEntIndex(candidate.surfaceRef);
        if(!candidate.surface)reason=1;
        else if(hit<0 || !PlacementSurfaceAllowed(hit,candidate.surfaceFlags,client,false))reason=2;
        else if(ReservedBounds(candidate.anchor,candidate.anchor))reason=3;
        else if(!PlacementWorldOriginAllowed(model,candidate,gRecipes[recipe].meta.modelScale))reason=4;
        else if(!gRecipes[recipe].meta.allowOverlap && !PlacementHullClear(model,candidate.origin,candidate.angles,-1,gRecipes[recipe].meta.modelScale))reason=5;
        V082PlacementCount(reason,core,feet,candidate,details);return false;
    }
    if(GetVectorDistance(candidate.anchor,point)>8.0){V082PlacementCount(7,core,feet,candidate,details);return false;}
    int chosen=-1;
    if(!RecipePlacementRules(client,recipe,candidate,chosen,false) || chosen!=core)
    {V082PlacementCount(8,core,feet,candidate,details);return false;}
    V082PlacementCount(9,core,feet,candidate,details);tV082Feet=feet;return true;
}

bool V082FindPlacement(int client,int recipe,int &core,bool details=false,int requiredCore=-1)
{
    for(int i=0;i<sizeof(tV082PlacementRejects);i++)tV082PlacementRejects[i]=0;
    tV082PlacementAttempts=0;tV082PlacementFloorMisses=0;
    char path[128],model[PLATFORM_MAX_PATH],row[1024];Format(path,sizeof(path),"Types/%s",gRecipes[recipe].type);Text(gRecipes[recipe].file,path,"model",model,sizeof(model));
    float low[3],high[3];bool bounds=PlacementModelBounds(model,low,high,gRecipes[recipe].meta.modelScale);
    if(details)
    {
        Format(row,sizeof(row),"{\"kind\":\"placement_model\",\"model\":\"%s\",\"bounds_available\":%d,\"mins\":[%.6f,%.6f,%.6f],\"maxs\":[%.6f,%.6f,%.6f],\"scale\":%.3f,\"ground_z\":%.6f,\"offset_z\":%.6f,\"allow_overlap\":%d,\"has_build_materials\":%d}",
            model,bounds,low[0],low[1],low[2],high[0],high[1],high[2],gRecipes[recipe].meta.modelScale,gRecipes[recipe].meta.modelGroundZ,gRecipes[recipe].meta.originOffset[2],gRecipes[recipe].meta.allowOverlap,HasBaseUpgradeMaterials(gRecipes[recipe].cost,client));V082Log(row);
    }
    RefreshBuildAreas();bool found;
    for(int c=0;c<gCoreCount && !found;c++)if(BuildCoreAlive(c) && (requiredCore<0 || c==requiredCore)
        && (gRecipes[recipe].kind!=Kind_Workbench || WorkbenchCount(c)==0))
    {
        // Cover the actual current base territory, not only its respawn points.
        // Every point still requires a real floor, free player hull, real eye
        // ray and all production placement/payment/base rules.
        for(int x=-4;x<=4 && !found;x++)for(int y=-4;y<=4 && !found;y++)
        {
            float local[3],start[3],end[3],point[3];local[0]=float(x)*gBuildAreas[c].halfX/5.0;local[1]=float(y)*gBuildAreas[c].halfY/5.0;local[2]=gCores[c].buildZMax+96.0;
            WorldPoint(local,gCores[c].buildCenter,gCores[c].buildAngles,start);end=start;end[2]=gCores[c].buildCenter[2]+gCores[c].buildZMin-96.0;
            if(!V082TraceFloor(client,start,end,point)){tV082PlacementFloorMisses++;continue;}
            if(gRecipes[recipe].kind==Kind_Generator && V082Bench() && GetVectorDistance(point,gBuildings[tV082Bench].origin)<160.0)continue;
            for(int ring=0;ring<2 && !found;ring++)for(int d=0;d<8 && !found;d++)
            {
                float viewer[3],floor[3],angle=DegToRad(float(d)*45.0);viewer=point;
                viewer[0]+=Cosine(angle)*(96.0+float(ring)*64.0);viewer[1]+=Sine(angle)*(96.0+float(ring)*64.0);viewer[2]+=128.0;
                end=viewer;end[2]-=384.0;if(!V082TraceFloor(client,viewer,end,floor)){tV082PlacementFloorMisses++;continue;}floor[2]+=2.0;
                if(V082TryPlacementView(client,recipe,c,model,floor,point,details)){found=true;core=c;}
            }
        }
    }
    if(details)
    {
        Format(row,sizeof(row),"{\"kind\":\"placement_summary\",\"found\":%d,\"attempts\":%d,\"floor_misses\":%d,\"viewer_hull\":%d,\"eye_surface\":%d,\"surface_policy\":%d,\"reserved_column\":%d,\"world_origin\":%d,\"model_hull\":%d,\"other_compute\":%d,\"anchor_mismatch\":%d,\"base_recipe_rules\":%d,\"accepted\":%d}",
            found,tV082PlacementAttempts,tV082PlacementFloorMisses,tV082PlacementRejects[0],tV082PlacementRejects[1],tV082PlacementRejects[2],tV082PlacementRejects[3],tV082PlacementRejects[4],tV082PlacementRejects[5],tV082PlacementRejects[6],tV082PlacementRejects[7],tV082PlacementRejects[8],tV082PlacementRejects[9]);V082Log(row);
    }
    return found;
}

bool V082AimBuildingNow(int client,int building)
{
    if(!V082Session(client) || building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used || !gBuildings[building].alive)return false;
    int body=Resolve(gBuildings[building].ref);if(body<0)return false;
    float low[3],high[3],mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0};
    GetEntPropVector(body,Prop_Send,"m_vecMins",low);GetEntPropVector(body,Prop_Send,"m_vecMaxs",high);
    for(int ring=0;ring<4;ring++)for(int d=0;d<16;d++)
    {
        float feet[3],angle=DegToRad(float(d)*22.5);feet=tV082Feet;
        if(ring>0){feet=gBuildings[building].origin;feet[0]+=Cosine(angle)*(64.0+float(ring)*24.0);feet[1]+=Sine(angle)*(64.0+float(ring)*24.0);feet[2]=tV082Feet[2];}
        if(!ClearHull(feet,mins,maxs,client,MASK_PLAYERSOLID))continue;
        for(int z=0;z<3;z++)for(int x=0;x<3;x++)for(int y=0;y<3;y++)
        {
            float local[3],target[3];local[0]=low[0]+(high[0]-low[0])*(0.15+0.35*float(x));
            local[1]=low[1]+(high[1]-low[1])*(0.15+0.35*float(y));local[2]=low[2]+(high[2]-low[2])*(0.15+0.35*float(z));
            WorldPoint(local,gBuildings[building].origin,gBuildings[building].angles,target);V082AimAt(client,feet,target);
            if(BuildingInUseRange(client,building)){tV082Feet=feet;SetEntityMoveType(client,MOVETYPE_NONE);gNextUse[client]=0.0;return true;}
        }
    }
    return false;
}

bool V082AimBenchNow(int client)
{
    return V082Bench() && V082AimBuildingNow(client,tV082Bench);
}

void V082Status(int client)
{
    char row[1024],classname[128];int active=-1,slot=-1;
    if(client>0){active=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon");slot=GetPlayerWeaponSlot(client,2);if(slot>MaxClients && IsValidEntity(slot))GetEntityClassname(slot,classname,sizeof(classname));}
    bool bench=V082Bench();
    Format(row,sizeof(row),"{\"time\":%d,\"kind\":\"status\",\"active\":%d,\"serial\":%d,\"epoch\":%d,\"revision\":%d,\"weapons\":%d,\"materials\":%d,\"station_types\":%d,\"bench\":%d,\"bench_uid\":%d,\"bench_level\":%d,\"hp\":%d,\"max_hp\":%d,\"constructing\":%d,\"actual_aim\":%d,\"slot2\":%d,\"active_weapon\":%d,\"classname\":\"%s\",\"passed\":%d,\"failed\":%d}",
        GetTime(),tV082Active,tV082Serial,gEpoch,gConfigRevision,gWeaponCraftCount,gMaterialCraftCount,gCraftStationCount,
        bench ? tV082Bench : -1,bench ? gBuildings[tV082Bench].uid : 0,bench ? gBuildings[tV082Bench].level : 0,
        bench ? gBuildings[tV082Bench].hp : 0,bench ? gBuildings[tV082Bench].maxHP : 0,bench && gBuildings[tV082Bench].constructing,
        client>0 && bench && BuildingInUseRange(client,tV082Bench),slot,active,classname,tV082Passed,tV082Failed);V082Log(row);
    if(client>0)for(int m=0;m<gMaterialCount;m++)
    {Format(row,sizeof(row),"{\"kind\":\"inventory\",\"material\":\"%s\",\"amount\":%d,\"initial\":%d,\"test_delta\":%d}",gMaterials[m].id,gPlayerInventory[client][m],tV082SavedInventory[m],tV082Delta[m]);V082Log(row);}
}

void V082Setup(int client)
{
    if(client<1 || !Running()){V082Check(false,"setup_requires_one_living_real_human_and_running_mode");return;}
    if(tV082Active){V082Check(V082Session(client),"existing_fixture_serial_epoch_revision");V082Status(client);return;}
    tV082Serial=GetClientSerial(client);tV082Epoch=gEpoch;tV082Revision=gConfigRevision;tV082Bench=-1;tV082OwnBench=false;
    tV082SavedInventory=gPlayerInventory[client];for(int m=0;m<MATERIALS;m++)tV082Delta[m]=0;
    GetClientAbsOrigin(client,tV082SavedPosition);GetClientEyeAngles(client,tV082SavedAngles);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",tV082SavedVelocity);
    tV082SavedHP=GetClientHealth(client);tV082SavedDamage=GetEntProp(client,Prop_Data,"m_takedamage");tV082SavedGod=GetEntityFlags(client)&FL_GODMODE;
    tV082SavedMove=GetEntityMoveType(client);tV082SavedExposure=gExposureEnabled;tV082Active=true;
    SetEntityFlags(client,GetEntityFlags(client)|FL_GODMODE);SetEntProp(client,Prop_Data,"m_takedamage",0,1);gExposureEnabled=false;
    V082Fund(client);SetEntityMoveType(client,MOVETYPE_WALK);
    V082Check(gWeaponCraftCount==11 && gMaterialCraftCount==5,"production_configuration_has_11_weapon_and_5_material_recipes");
    V082Check(gCraftStationCount>0 && StrEqual(gCraftStationNames[0],"合成台"),"first_available_station_name_is_chinese_crafting_bench");
    tV082Recipe=-1;for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Workbench){tV082Recipe=r;break;}
    for(int b=MAX_CORES;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].kind==Kind_Workbench)
    {tV082Bench=b;tV082BenchUID=gBuildings[b].uid;break;}
    if(tV082Bench<0 && tV082Recipe>=0)
    {
        int core=-1;if(!V082FindPlacement(client,tV082Recipe,core)){V082Check(false,"legal_floor_ray_placement_found_without_hardcoded_world_point");return;}
        int before[MATERIALS];before=gPlayerInventory[client];int beforeUID=gUID;
        bool built=BuildRecipe(client,tV082Recipe,core);V082LedgerChange(before,gPlayerInventory[client]);
        for(int b=MAX_CORES;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].kind==Kind_Workbench
            && gBuildings[b].uid>beforeUID && gBuildings[b].ownerSerial==tV082Serial){tV082Bench=b;tV082BenchUID=gBuildings[b].uid;tV082OwnBench=true;break;}
        bool paid=built && V082Bench();for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-gRecipes[tV082Recipe].cost[m])paid=false;
        V082Check(paid,"production_ray_build_path_creates_bench_and_charges_exact_configured_cost");
    }
    else if(tV082Bench>=0)GetClientAbsOrigin(client,tV082Feet);
    V082Check(V082Bench(),"fixture_has_exact_live_bench_uid");
    PrintToServer("[V082Probe] setup keeps construction state unchanged; actual client must melee-complete blueprint. Use aim, then physical E. finish is diagnostic only. Exposure disabled only inside this fixture; NPC policy unchanged.");
    if(V082Bench())V082Check(V082AimBenchNow(client),"real_player_aim_matches_actual_bench_entity");V082Status(client);
}

void V082PlacementDiagnostic(int client)
{
    if(client<1 || !Running()){PrintToServer("[V082Probe] placement requires one existing living real client and running mode; nothing created or charged.");return;}
    int recipe=-1;for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Workbench){recipe=r;break;}
    if(recipe<0){PrintToServer("[V082Probe] placement has no valid configured workbench recipe; nothing created or charged.");return;}
    float position[3],angles[3],velocity[3],savedFeet[3];GetClientAbsOrigin(client,position);GetClientEyeAngles(client,angles);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",velocity);
    MoveType movement=GetEntityMoveType(client);savedFeet=tV082Feet;int core=-1;
    V082FindPlacement(client,recipe,core,true);
    TeleportEntity(client,position,angles,velocity);SetEntityMoveType(client,movement);tV082Feet=savedFeet;
    PrintToServer("[V082Probe] placement diagnostic restored the current actor transform; no bench created, no material charged, no production metadata changed.");
}

void V082Bootstrap(int client)
{
    bool allowed=client>0 && V082Human()==client && gEnabled && gCoreReady && gRoundPrepared
        && !gTerminal && gPhase==Phase_Waiting && !tV082Active;
    if(!allowed)
    {
        PrintToServer("[V082Probe] bootstrap refused: requires unique live real human, enabled/core-ready/prepared waiting mode, and no active fixture. No lifecycle action taken.");return;
    }
    int beforeDay=gDay,beforeEpoch=gEpoch;Phase beforePhase=gPhase;
    // Test-only replay after replacing the diagnostic SMX in an already-live
    // HZS session. StartDay(false) performs no HZS start/restart/changelevel,
    // no client creation and no NPC spawn/respawn call. Do not invoke the full
    // Han_OnGameStart here: its native-pool bootstrap is outside this fixture.
    StartDay(false);
    char row[512];Format(row,sizeof(row),"{\"kind\":\"diagnostic_bootstrap\",\"before_phase\":%d,\"after_phase\":%d,\"before_day\":%d,\"after_day\":%d,\"same_epoch\":%d,\"running\":%d,\"natural_lifecycle_evidence\":false,\"hzs_restart_invoked\":false,\"npc_respawn_invoked\":false}",
        view_as<int>(beforePhase),view_as<int>(gPhase),beforeDay,gDay,gEpoch==beforeEpoch,Running());V082Log(row);
    PrintToServer("[V082Probe] explicit diagnostic StartDay(false) replay for already-running HZS after hot reload; this is not natural lifecycle acceptance evidence.");
}

void V082BuildFixture(int client)
{
    if(!V082Session(client)){V082Check(false,"build_requires_original_fixture_session");return;}
    if(V082Bench()){PrintToServer("[V082Probe] build preserved the existing exact fixture bench; no second payment or spawn.");V082Status(client);return;}
    tV082Recipe=-1;for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Workbench){tV082Recipe=r;break;}
    int core=-1;
    if(tV082Recipe<0 || !V082FindPlacement(client,tV082Recipe,core,true)){PrintToServer("[V082Probe] build found no production-legal candidate; no payment attempted.");return;}
    int before[MATERIALS];before=gPlayerInventory[client];int beforeUID=gUID;
    bool built=BuildRecipe(client,tV082Recipe,core);V082LedgerChange(before,gPlayerInventory[client]);
    for(int b=MAX_CORES;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].kind==Kind_Workbench
        && gBuildings[b].uid>beforeUID && gBuildings[b].ownerSerial==tV082Serial){tV082Bench=b;tV082BenchUID=gBuildings[b].uid;tV082OwnBench=true;break;}
    bool paid=built && V082Bench();for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-gRecipes[tV082Recipe].cost[m])paid=false;
    V082Check(paid,"production_ray_build_retry_creates_one_exact_paid_bench");if(V082Bench())V082AimBenchNow(client);V082Status(client);
}

int V082WeaponRecipe(const char[] id)
{
    for(int r=0;r<gWeaponCraftCount;r++)if(StrEqual(gWeaponCrafts[r].id,id))return r;return -1;
}

void V082Transactions(int client)
{
    if(!V082Session(client) || !V082AimBenchNow(client) || !CraftingStationUsable(client,tV082Bench))
    {V082Check(false,"transactions_require_completed_powered_bench_and_exact_real_aim");return;}
    int axe=V082WeaponRecipe("axe"),before[MATERIALS];before=gPlayerInventory[client];
    V082Check(axe>=0 && gWeaponCrafts[axe].cost[FindMaterial("wood")]==50 && gWeaponCrafts[axe].cost[FindMaterial("iron")]==20,"axe_production_cost_is_wood50_iron20");
    if(axe>=0)
    {
        char saved[64];strcopy(saved,sizeof(saved),gBuildings[tV082Bench].type);strcopy(gBuildings[tV082Bench].type,sizeof(gBuildings[].type),"__diagnostic_other_type");
        bool rejected=!TryCraftWeapon(client,tV082Bench,axe) && V082SameInventory(client,before);
        strcopy(gBuildings[tV082Bench].type,sizeof(gBuildings[].type),saved);V082Check(rejected,"wrong_station_type_refuses_weapon_without_charge");
    }
    for(int r=0;r<gWeaponCraftCount;r++)if(gWeaponCrafts[r].requiredLevel>1)
    {
        int saved=gBuildings[tV082Bench].level;gBuildings[tV082Bench].level=gWeaponCrafts[r].requiredLevel-1;before=gPlayerInventory[client];
        bool rejected=!TryCraftWeapon(client,tV082Bench,r) && V082SameInventory(client,before);
        gBuildings[tV082Bench].level=gWeaponCrafts[r].requiredLevel;bool exact=WeaponCraftAllowed(tV082Bench,r);
        gBuildings[tV082Bench].level=saved;V082Check(rejected && exact,"minimum_level_boundary_uses_exact_production_gate_with_temporary_diagnostic_level");break;
    }
    int maximum=gInventoryCap>0 ? gInventoryCap : LEVEL_CELL_LIMIT;
    for(int r=0;r<gMaterialCraftCount;r++)
    {
        for(int m=0;m<gMaterialCount;m++)V082SetBalance(client,m,gMaterialCrafts[r].cost[m]);
        before=gPlayerInventory[client];bool crafted=TryCraftMaterial(client,tV082Bench,r);bool exact=crafted;
        for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-gMaterialCrafts[r].cost[m]+(m==gMaterialCrafts[r].outputMaterial ? gMaterialCrafts[r].outputAmount : 0))exact=false;
        V082LedgerChange(before,gPlayerInventory[client]);char label[192];Format(label,sizeof(label),"material_recipe_%s_atomic_exact_input_and_output",gMaterialCrafts[r].id);V082Check(exact,label);
        int input=-1;for(int m=0;m<gMaterialCount;m++)if(gMaterialCrafts[r].cost[m]>0){input=m;break;}
        for(int m=0;m<gMaterialCount;m++)V082SetBalance(client,m,gMaterialCrafts[r].cost[m]);
        if(input>=0)V082SetBalance(client,input,gMaterialCrafts[r].cost[input]-1);
        before=gPlayerInventory[client];bool refused=!TryCraftMaterial(client,tV082Bench,r) && V082SameInventory(client,before);
        Format(label,sizeof(label),"material_recipe_%s_insufficient_is_uncharged",gMaterialCrafts[r].id);V082Check(refused,label);
        for(int m=0;m<gMaterialCount;m++)V082SetBalance(client,m,gMaterialCrafts[r].cost[m]);
        V082SetBalance(client,gMaterialCrafts[r].outputMaterial,maximum);before=gPlayerInventory[client];
        refused=!TryCraftMaterial(client,tV082Bench,r) && V082SameInventory(client,before);
        Format(label,sizeof(label),"material_recipe_%s_full_output_is_uncharged",gMaterialCrafts[r].id);V082Check(refused,label);
    }
    V082Fund(client);V082Status(client);
}

void V082ExpectedClass(int recipe,char[] classname,int length)
{
    char id[64];strcopy(id,sizeof(id),gWeaponCrafts[recipe].id);
    if(StrEqual(id,"axe"))strcopy(classname,length,"weapon_axe");
    else if(StrContains(id,"delta_")==0)Format(classname,length,"weapon_%s",id[6]);else classname[0]=0;
}

int V082ExpectedHP(const char[] classname)
{
    if(StrEqual(classname,"weapon_beijixing") || StrEqual(classname,"weapon_beijixing2") || StrEqual(classname,"weapon_chongsu"))return 340;
    if(StrEqual(classname,"weapon_xintiao") || StrEqual(classname,"weapon_xintiao2"))return 250;
    if(StrEqual(classname,"weapon_lianming") || StrEqual(classname,"weapon_longya"))return 320;
    if(StrEqual(classname,"weapon_zhuixing"))return 330;if(StrEqual(classname,"weapon_wange"))return 370;
    if(StrEqual(classname,"weapon_apexshuangren"))return 500;return -1;
}

public Action V082WeaponStep(Handle timer,any serial)
{
    int client=GetClientFromSerial(serial);
    if(!tV082WeaponsRunning || !V082Session(client) || !V082Bench()){tV082WeaponsRunning=false;return Plugin_Stop;}
    if(tV082PendingWeapon>=0)
    {
        int r=tV082PendingWeapon,weapon=GetPlayerWeaponSlot(client,2);char classname[128],expected[128],label[192];
        if(weapon>MaxClients && IsValidEntity(weapon))GetEntityClassname(weapon,classname,sizeof(classname));V082ExpectedClass(r,expected,sizeof(expected));
        bool actual=weapon>MaxClients && IsValidEntity(weapon) && EntIndexToEntRef(weapon)!=tV082PendingBeforeRef && expected[0] && StrEqual(classname,expected)
            && GetEntPropEnt(weapon,Prop_Send,"m_hOwnerEntity")==client;
        Format(label,sizeof(label),"weapon_%s_real_new_slot2_owned_class_output",gWeaponCrafts[r].id);V082Check(actual,label);
        int repair,construction,gather;WeaponWorkAmountsForClass(classname,repair,construction,gather);int expectedHP=V082ExpectedHP(expected);
        if(expectedHP>0){Format(label,sizeof(label),"weapon_%s_production_work_hp_is_v081_times10",gWeaponCrafts[r].id);V082Check(actual && repair==expectedHP && construction==expectedHP,label);}
        int active=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon");
        char row[512];Format(row,sizeof(row),"{\"kind\":\"weapon_output\",\"recipe\":\"%s\",\"classname\":\"%s\",\"entity\":%d,\"previous_ref\":%d,\"active_weapon\":%d,\"active_is_slot2\":%d,\"repair\":%d,\"construction\":%d,\"gather_per_1000\":%d}",gWeaponCrafts[r].id,classname,weapon,tV082PendingBeforeRef,active,active==weapon,repair,construction,gather);V082Log(row);
        tV082PendingWeapon=-1;
    }
    if(tV082WeaponIndex>=gWeaponCraftCount)
    {
        gBuildings[tV082Bench]=tV082WeaponBench;InvalidateDefenseCache(tV082Bench);MarkBuildAreasDirty();RecalculatePower();
        tV082WeaponsRunning=false;V082AimBenchNow(client);V082Status(client);
        PrintToServer("[V082Probe] weapons complete; temporary diagnostic max-level restored. Output checks are actual HWS entities; construction/repair hits require separate real input evidence.");return Plugin_Stop;
    }
    int r=tV082WeaponIndex++;V082Fund(client);V082AimBenchNow(client);int before[MATERIALS];before=gPlayerInventory[client];
    int oldWeapon=GetPlayerWeaponSlot(client,2);tV082PendingBeforeRef=oldWeapon>MaxClients && IsValidEntity(oldWeapon) ? EntIndexToEntRef(oldWeapon) : INVALID_ENT_REFERENCE;
    bool crafted=TryCraftWeapon(client,tV082Bench,r),paid=crafted;
    for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-gWeaponCrafts[r].cost[m])paid=false;
    V082LedgerChange(before,gPlayerInventory[client]);char label[192];Format(label,sizeof(label),"weapon_%s_exact_personal_material_charge",gWeaponCrafts[r].id);V082Check(paid,label);
    tV082PendingWeapon=r;CreateTimer(0.35,V082WeaponStep,serial,TIMER_FLAG_NO_MAPCHANGE);return Plugin_Stop;
}

void V082Weapons(int client)
{
    if(!V082Session(client) || !V082AimBenchNow(client) || !CraftingStationUsable(client,tV082Bench) || tV082WeaponsRunning || tV082WorkActive)
    {V082Check(false,"weapons_requires_ready_bench_without_existing_sequence");return;}
    tV082WeaponBench=gBuildings[tV082Bench];char profile[128];BuildingProfilePath(tV082Bench,profile,sizeof(profile));
    gBuildings[tV082Bench].level=ProfileMaximumLevel(C_Workbenches,profile);
    if(!ApplyBuildingLevel(tV082Bench,false)){gBuildings[tV082Bench]=tV082WeaponBench;V082Check(false,"diagnostic_max_level_applies_valid_production_profile");return;}
    PrintToServer("[V082Probe] weapons uses an explicit temporary diagnostic max-level; not evidence of natural upgrades.");
    tV082WeaponsRunning=true;tV082WeaponIndex=0;tV082PendingWeapon=-1;CreateTimer(0.1,V082WeaponStep,tV082Serial,TIMER_FLAG_NO_MAPCHANGE);
}

void V082WorkRestore(int client)
{
    if(!tV082WorkActive){PrintToServer("[V082Probe] work_restore has no active work fixture; no building changed.");return;}
    if(!V082Session(client) || !V082Bench() || gBuildings[tV082Bench].uid!=tV082WorkBench.uid
        || gBuildings[tV082Bench].ref!=tV082WorkBench.ref || gBuildings[tV082Bench].generation!=tV082WorkBench.generation)
    {PrintToServer("[V082Probe] work_restore refused: original actor or exact building UID/ref/generation changed; no replacement building adopted.");return;}
    gBuildings[tV082Bench]=tV082WorkBench;tV082WorkActive=false;
    SyncConstructionVisual(tV082Bench);InvalidateDefenseCache(tV082Bench);MarkBuildAreasDirty();RecalculatePower();
    PrintToServer("[V082Probe] work_restore restored the exact saved Building state; no repair/construct/damage function was called.");V082Status(client);
}

void V082WorkPrepare(int client,const char[] mode)
{
    if(!V082Session(client) || !V082Bench() || !tV082OwnBench || gBuildings[tV082Bench].ownerSerial!=tV082Serial
        || tV082WeaponsRunning || tV082WorkActive || (!StrEqual(mode,"repair") && !StrEqual(mode,"construction")))
    {PrintToServer("[V082Probe] work_prepare requires exact fixture-owned bench, original actor, repair|construction, and no active work/weapon fixture.");return;}
    if(!V082AimBenchNow(client)){PrintToServer("[V082Probe] work_prepare requires real close aim at the exact bench; no state changed.");return;}
    tV082WorkBench=gBuildings[tV082Bench];char profile[128];BuildingProfilePath(tV082Bench,profile,sizeof(profile));
    gBuildings[tV082Bench].level=ProfileMaximumLevel(C_Workbenches,profile);
    if(!ApplyBuildingLevel(tV082Bench,false))
    {gBuildings[tV082Bench]=tV082WorkBench;PrintToServer("[V082Probe] work_prepare could not resolve configured maximum level; original state restored.");return;}
    gBuildings[tV082Bench].hp=1;gBuildings[tV082Bench].constructionTarget=gBuildings[tV082Bench].maxHP;
    gBuildings[tV082Bench].constructing=StrEqual(mode,"construction");tV082WorkActive=true;gNextRepair[client]=0.0;
    SyncConstructionVisual(tV082Bench);InvalidateDefenseCache(tV082Bench);MarkBuildAreasDirty();RecalculatePower();V082AimBenchNow(client);
    int repair,construction,gather;bool held=HeldWeaponWorkAmounts(client,repair,construction,gather);
    int active=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon"),slot=GetPlayerWeaponSlot(client,2);char classname[128];
    if(active>MaxClients && IsValidEntity(active))GetEntityClassname(active,classname,sizeof(classname));
    char row[768];Format(row,sizeof(row),"{\"kind\":\"work_prepare\",\"mode\":\"%s\",\"diagnostic_level\":%d,\"hp\":%d,\"max_hp\":%d,\"constructing\":%d,\"active_weapon\":%d,\"slot2\":%d,\"active_class\":\"%s\",\"active_is_eligible_knife\":%d,\"expected_repair_hp\":%d,\"expected_construction_hp\":%d,\"gather_per_1000\":%d,\"synthetic_damage_or_repair_invoked\":false}",
        mode,gBuildings[tV082Bench].level,gBuildings[tV082Bench].hp,gBuildings[tV082Bench].maxHP,gBuildings[tV082Bench].constructing,active,slot,classname,held,repair,construction,gather);V082Log(row);
    PrintToServer("[V082Probe] work fixture only prepares HP/state: use real client slot selection and physical left click; no SDK damage or repair helper has been invoked.");
}

bool V082PowerGeneratorValid()
{
    return tV082PowerGenerator>=MAX_CORES && tV082PowerGenerator<MAX_BUILDINGS && gBuildings[tV082PowerGenerator].used
        && gBuildings[tV082PowerGenerator].kind==Kind_Generator && gBuildings[tV082PowerGenerator].uid==tV082PowerGeneratorUID
        && gBuildings[tV082PowerGenerator].ref==tV082PowerGeneratorSaved.ref
        && gBuildings[tV082PowerGenerator].generation==tV082PowerGeneratorSaved.generation && Resolve(gBuildings[tV082PowerGenerator].ref)>0;
}

bool V082PowerCoreIsolated()
{
    if(!V082Bench())return false;int core=gBuildings[tV082Bench].buildCore;
    for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive && b!=tV082Bench && b!=tV082PowerGenerator)
    {
        int other=gBuildings[b].gridAssigned ? gBuildings[b].powerCore : GridAt(gBuildings[b].origin);
        if(other==core && (gBuildings[b].kind==Kind_Generator || gBuildings[b].powerDemand>0.0))return false;
    }
    return true;
}

void V082PowerLog(const char[] stage)
{
    if(!V082Bench())return;int core=gBuildings[tV082Bench].buildCore;char row[640];
    Format(row,sizeof(row),"{\"kind\":\"power_fixture\",\"stage\":\"%s\",\"core\":%d,\"bench_level\":%d,\"bench_demand\":%.3f,\"bench_powered\":%d,\"grid_production\":%.3f,\"grid_demand\":%.3f,\"generator_uid\":%d,\"generator_level\":%d,\"generator_hp\":%d,\"generator_off\":%d,\"generator_output\":%.3f,\"generator_effective_output\":%.3f}",
        stage,core,gBuildings[tV082Bench].level,gBuildings[tV082Bench].powerDemand,BuildingPowered(tV082Bench),gGridProduction[core],gGridDemand[core],
        V082PowerGeneratorValid() ? tV082PowerGeneratorUID : 0,V082PowerGeneratorValid() ? gBuildings[tV082PowerGenerator].level : 0,
        V082PowerGeneratorValid() ? gBuildings[tV082PowerGenerator].hp : 0,tV082PowerGeneratorOff,V082PowerGeneratorValid() ? gBuildings[tV082PowerGenerator].powerOutput : 0.0,
        V082PowerGeneratorValid() ? GeneratorOutput(tV082PowerGenerator) : 0.0);V082Log(row);
}

void V082PowerRejectTransactions(int client,const char[] prefix)
{
    V082Fund(client);bool context=V082AimBenchNow(client) && CraftingStationInteractable(client,tV082Bench) && !BuildingPowered(tV082Bench);
    char label[192];Format(label,sizeof(label),"%s_has_real_aim_and_unpowered_interactable_bench",prefix);V082Check(context,label);
    if(!context)return;
    int axe=V082WeaponRecipe("axe"),before[MATERIALS];
    if(axe>=0 && WeaponCraftAllowed(tV082Bench,axe) && WeaponCraftCommandExists(axe))
    {
        before=gPlayerInventory[client];bool rejected=!TryCraftWeapon(client,tV082Bench,axe) && V082SameInventory(client,before);
        V082LedgerChange(before,gPlayerInventory[client]);Format(label,sizeof(label),"%s_weapon_rejected_without_charge",prefix);V082Check(rejected,label);
    }
    else V082Check(false,"power_fixture_requires_valid_available_axe_recipe_for_power_only_rejection");
    if(gMaterialCraftCount>0)
    {
        before=gPlayerInventory[client];bool rejected=!TryCraftMaterial(client,tV082Bench,0) && V082SameInventory(client,before);
        V082LedgerChange(before,gPlayerInventory[client]);Format(label,sizeof(label),"%s_material_rejected_without_charge",prefix);V082Check(rejected,label);
    }
}

void V082PowerOn(int client,bool report=true)
{
    if(!V082Session(client) || !V082PowerGeneratorValid() || tV082WeaponsRunning || tV082WorkActive)
    {PrintToServer("[V082Probe] power_on requires exact fixture generator and no work/weapon sequence; no existing user facility changed.");return;}
    gBuildings[tV082PowerGenerator]=tV082PowerGeneratorSaved;tV082PowerGeneratorOff=false;
    SyncConstructionVisual(tV082PowerGenerator);InvalidateDefenseCache(tV082PowerGenerator);RecalculatePower();V082AimBenchNow(client);
    if(report){V082Check(BuildingPowered(tV082Bench) && CraftingStationUsable(client,tV082Bench),"restoring_exact_fixture_generator_recovers_crafting_power");V082PowerLog("power_on");}
}

void V082PowerOff(int client)
{
    if(!V082Session(client) || !V082PowerGeneratorValid() || !V082PowerCoreIsolated() || tV082WeaponsRunning || tV082WorkActive)
    {PrintToServer("[V082Probe] power_off refused: exact fixture generator/isolated core required; user facilities remain unchanged.");return;}
    gBuildings[tV082PowerGenerator].hp=0;tV082PowerGeneratorOff=true;RecalculatePower();
    V082PowerRejectTransactions(client,"generator_off");V082PowerLog("power_off");
}

void V082PowerSetup(int client)
{
    if(!V082Session(client) || !V082Bench() || !BuildingOperational(tV082Bench) || !gPowerEnabled
        || !V082PowerCoreIsolated() || tV082WeaponsRunning || tV082WorkActive)
    {PrintToServer("[V082Probe] power_setup requires completed exact bench, enabled power, and no pre-existing user generator/positive load on this core; no facility changed.");return;}
    if(V082PowerGeneratorValid()){V082PowerOn(client);PrintToServer("[V082Probe] power_setup preserved existing fixture generator; no duplicate build or payment.");return;}
    RecalculatePower();V082PowerRejectTransactions(client,"no_generator");V082PowerLog("before_generator");
    int recipe=-1;for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Generator && StrEqual(gRecipes[r].type,"generator_basic")){recipe=r;break;}
    int core=-1,expected=gBuildings[tV082Bench].buildCore;
    if(recipe<0 || !V082FindPlacement(client,recipe,core,true,expected))
    {V082Check(false,"same_core_fixture_generator_has_legal_production_placement");return;}
    V082Fund(client);int before[MATERIALS];before=gPlayerInventory[client];int beforeUID=gUID;
    bool built=BuildRecipe(client,recipe,expected);V082LedgerChange(before,gPlayerInventory[client]);
    for(int b=MAX_CORES;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].kind==Kind_Generator
        && gBuildings[b].uid>beforeUID && gBuildings[b].ownerSerial==tV082Serial && gBuildings[b].buildCore==expected)
    {tV082PowerGenerator=b;tV082PowerGeneratorUID=gBuildings[b].uid;tV082PowerGeneratorSaved=gBuildings[b];break;}
    bool buildPaid=built && V082PowerGeneratorValid();
    for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-gRecipes[recipe].cost[m])buildPaid=false;
    V082Check(buildPaid,"production_build_path_creates_exact_same_core_fixture_generator_with_exact_payment");if(!V082PowerGeneratorValid())return;
    if(gBuildings[tV082PowerGenerator].constructing)
    {CompleteBuildingConstruction(tV082PowerGenerator,client);PrintToServer("[V082Probe] generator completion is explicit diagnostic preparation, not real melee construction evidence.");}
    while(gBuildings[tV082PowerGenerator].level<3)
    {
        if(!V082AimBuildingNow(client,tV082PowerGenerator)){V082Check(false,"production_generator_upgrade_has_exact_real_aim");break;}
        V082Fund(client);before=gPlayerInventory[client];int old=gBuildings[tV082PowerGenerator].level,cost[MATERIALS],maximum;
        bool quote=ReadBuildingLevelQuote(tV082PowerGenerator,old+1,cost,maximum);
        bool upgraded=TryUpgrade(client,tV082PowerGenerator);V082LedgerChange(before,gPlayerInventory[client]);
        bool paid=quote && upgraded;
        for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-cost[m])paid=false;
        V082Check(paid && gBuildings[tV082PowerGenerator].level==old+1,"fixture_generator_uses_production_exact_paid_upgrade_path");if(!upgraded)break;
    }
    tV082PowerGeneratorSaved=gBuildings[tV082PowerGenerator];tV082PowerGeneratorOff=false;RecalculatePower();
    V082Check(gBuildings[tV082PowerGenerator].level==3 && FloatAbs(GeneratorOutput(tV082PowerGenerator)-350.0)<0.01,"unmodified_basic_generator_level3_normally_outputs350");
    Building bench;bench=gBuildings[tV082Bench];char profile[128];BuildingProfilePath(tV082Bench,profile,sizeof(profile));
    gBuildings[tV082Bench].level=ProfileMaximumLevel(C_Workbenches,profile);
    if(!ApplyBuildingLevel(tV082Bench,false)){gBuildings[tV082Bench]=bench;RecalculatePower();V082Check(false,"power_boundary_uses_valid_configured_maximum_bench_level");return;}
    V082Check(gBuildings[tV082Bench].level==10 && FloatAbs(gBuildings[tV082Bench].powerDemand-320.0)<0.01,"configured_bench_level10_requires320_power");
    // Only this newly built instance is varied for exact edge measurements.
    // No generator config, base facility or global power rule is changed.
    gBuildings[tV082PowerGenerator].powerOutput=319.0;gBuildings[tV082PowerGenerator].hp=gBuildings[tV082PowerGenerator].maxHP;
    RecalculatePower();V082PowerRejectTransactions(client,"production319_for_demand320");V082PowerLog("diagnostic_output319");
    gBuildings[tV082PowerGenerator].powerOutput=320.0;RecalculatePower();V082AimBenchNow(client);
    V082Check(BuildingPowered(tV082Bench) && CraftingStationUsable(client,tV082Bench),"production320_exactly_meets_bench_demand320");V082PowerLog("diagnostic_output320");
    V082Fund(client);int axe=V082WeaponRecipe("axe");
    if(axe>=0)
    {
        before=gPlayerInventory[client];bool sent=TryCraftWeapon(client,tV082Bench,axe),paid=sent;
        for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-gWeaponCrafts[axe].cost[m])paid=false;
        V082LedgerChange(before,gPlayerInventory[client]);V082Check(paid,"powered320_accepts_one_weapon_dispatch_with_exact_payment");
    }
    if(gMaterialCraftCount>0)
    {
        before=gPlayerInventory[client];bool made=TryCraftMaterial(client,tV082Bench,0),exact=made;
        for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-gMaterialCrafts[0].cost[m]+(m==gMaterialCrafts[0].outputMaterial ? gMaterialCrafts[0].outputAmount : 0))exact=false;
        V082LedgerChange(before,gPlayerInventory[client]);V082Check(exact,"powered320_accepts_one_material_recipe_with_exact_output");
    }
    gBuildings[tV082Bench]=bench;gBuildings[tV082PowerGenerator]=tV082PowerGeneratorSaved;RecalculatePower();V082AimBenchNow(client);
    V082PowerLog("normal_output350_and_original_bench_restored");
    PrintToServer("[V082Probe] 319/320 were diagnostic output changes on the exact new generator instance; normal 350 output and original bench state restored. No production config changed.");
}

void V082Cleanup(int client)
{
    if(!V082Session(client)){V082Check(false,"cleanup_requires_original_live_serial_epoch_revision");return;}
    int cap=gInventoryCap>0 ? gInventoryCap : LEVEL_CELL_LIMIT;bool ledger=true;
    for(int m=0;m<gMaterialCount;m++)
    {
        int current=gPlayerInventory[client][m],delta=tV082Delta[m];
        if((delta>0 && current<delta) || (delta<0 && current>cap+delta))ledger=false;
    }
    if(!ledger)
    {
        V082Check(false,"cleanup_inventory_delta_preflight_requires_manual_transaction_audit");
        PrintToServer("[V082Probe] cleanup left fixture and all inventories unchanged: audit/record missing manual transactions before retrying; no blanket inventory reset.");V082Status(client);return;
    }
    if(tV082WorkActive)
    {
        V082WorkRestore(client);
        if(tV082WorkActive){PrintToServer("[V082Probe] cleanup stopped because exact work state could not be restored.");return;}
    }
    if(tV082WeaponsRunning && V082Bench())gBuildings[tV082Bench]=tV082WeaponBench;
    tV082WeaponsRunning=false;CancelClientMenu(client);ResetCraftingMenu(client);
    if(V082PowerGeneratorValid())
    {
        gBuildings[tV082PowerGenerator]=tV082PowerGeneratorSaved;RemoveBuildingEffect(tV082PowerGenerator);int generatorRef=gBuildings[tV082PowerGenerator].ref;
        gBuildings[tV082PowerGenerator].ref=INVALID_ENT_REFERENCE;gBuildings[tV082PowerGenerator].alive=false;gBuildings[tV082PowerGenerator].used=false;RemoveOwnedRef(generatorRef);
    }
    tV082PowerGenerator=-1;tV082PowerGeneratorUID=0;tV082PowerGeneratorOff=false;
    if(tV082OwnBench && V082Bench())
    {
        RemoveBuildingEffect(tV082Bench);int ref=gBuildings[tV082Bench].ref;gBuildings[tV082Bench].ref=INVALID_ENT_REFERENCE;
        gBuildings[tV082Bench].alive=false;gBuildings[tV082Bench].used=false;RemoveOwnedRef(ref);
    }
    for(int m=0;m<gMaterialCount;m++)
    {
        int current=gPlayerInventory[client][m],delta=tV082Delta[m];
        gPlayerInventory[client][m]=current-delta;tV082Delta[m]=0;
    }
    gExposureEnabled=tV082SavedExposure;SetEntProp(client,Prop_Data,"m_takedamage",tV082SavedDamage,1);
    SetEntityFlags(client,(GetEntityFlags(client)&~FL_GODMODE)|tV082SavedGod);SetEntityHealth(client,tV082SavedHP);SetEntityMoveType(client,tV082SavedMove);
    TeleportEntity(client,tV082SavedPosition,tV082SavedAngles,tV082SavedVelocity);MarkBuildAreasDirty();RecalculatePower();
    V082Check(ledger,"cleanup_reverses_only_recorded_test_inventory_deltas_preserving_other_changes");
    V082Check(GetEntProp(client,Prop_Data,"m_takedamage")==tV082SavedDamage && (GetEntityFlags(client)&FL_GODMODE)==tV082SavedGod && gExposureEnabled==tV082SavedExposure,
        "cleanup_restores_original_human_protection_and_exposure_policy");
    tV082Active=false;V082Status(client);
}

public Action V082ProbeCommand(int client,int args)
{
    int actor=V082Human();char stage[64];if(args>0)GetCmdArg(1,stage,sizeof(stage));else strcopy(stage,sizeof(stage),"status");
    if(StrEqual(stage,"setup"))V082Setup(actor);
    else if(StrEqual(stage,"status"))V082Status(actor);
    else if(StrEqual(stage,"bootstrap"))V082Bootstrap(actor);
    else if(StrEqual(stage,"placement"))V082PlacementDiagnostic(actor);
    else if(StrEqual(stage,"cleanup"))V082Cleanup(actor);
    else if(!V082Session(actor)){V082Check(false,"stage_requires_original_live_fixture_session");}
    else if(StrEqual(stage,"aim")){V082Check(V082AimBenchNow(actor),"actual_eye_ray_targets_bench_no_menu_was_opened_by_probe");V082Status(actor);}
    else if(StrEqual(stage,"finish")){if(V082Bench()){CompleteBuildingConstruction(tV082Bench,actor);PrintToServer("[V082Probe] DIAGNOSTIC finish; not real construction evidence.");V082AimBenchNow(actor);}V082Status(actor);}
    else if(StrEqual(stage,"release")){SetEntityMoveType(actor,MOVETYPE_WALK);V082Status(actor);}
    else if(StrEqual(stage,"fund")){V082Fund(actor);V082Status(actor);}
    else if(StrEqual(stage,"build"))V082BuildFixture(actor);
    else if(StrEqual(stage,"transactions"))V082Transactions(actor);
    else if(StrEqual(stage,"weapons"))V082Weapons(actor);
    else if(StrEqual(stage,"work_prepare") && args==2){char mode[32];GetCmdArg(2,mode,sizeof(mode));V082WorkPrepare(actor,mode);}
    else if(StrEqual(stage,"work_restore"))V082WorkRestore(actor);
    else if(StrEqual(stage,"power_setup"))V082PowerSetup(actor);
    else if(StrEqual(stage,"power_off"))V082PowerOff(actor);
    else if(StrEqual(stage,"power_on"))V082PowerOn(actor);
    else if(StrEqual(stage,"record_weapon") && args==2)
    {
        char id[64];GetCmdArg(2,id,sizeof(id));int r=V082WeaponRecipe(id);
        if(r>=0){for(int m=0;m<gMaterialCount;m++)tV082Delta[m]-=gWeaponCrafts[r].cost[m];PrintToServer("[V082Probe] recorded external real-UI weapon cost once for %s; operator must verify that UI transaction actually succeeded.",id);}
    }
    else if(StrEqual(stage,"record_material") && args==2)
    {
        char id[64];GetCmdArg(2,id,sizeof(id));int r=-1;
        for(int i=0;i<gMaterialCraftCount;i++)if(StrEqual(gMaterialCrafts[i].id,id)){r=i;break;}
        if(r>=0)
        {
            for(int m=0;m<gMaterialCount;m++)tV082Delta[m]-=gMaterialCrafts[r].cost[m];
            tV082Delta[gMaterialCrafts[r].outputMaterial]+=gMaterialCrafts[r].outputAmount;
            PrintToServer("[V082Probe] recorded external real-UI material recipe once for %s; operator must verify exact input/output before recording.",id);
        }
    }
    else if(StrEqual(stage,"record_upgrade") && (args==2 || args==3) && V082Bench())
    {
        char text[32];GetCmdArg(2,text,sizeof(text));int first,last;
        bool valid=ParseLevelInteger(text,first) && first>=2;last=first;
        if(args==3){GetCmdArg(3,text,sizeof(text));valid=valid && ParseLevelInteger(text,last);}
        valid=valid && last>=first && last<=gBuildings[tV082Bench].level && last-first<=100;
        int total[MATERIALS],cost[MATERIALS];char profile[128];BuildingProfilePath(tV082Bench,profile,sizeof(profile));
        for(int level=first;valid && level<=last;level++)
        {
            valid=ReadLevelUpgradeCost(C_Workbenches,profile,level,cost);
            for(int m=0;m<gMaterialCount && valid;m++)
            {if(cost[m]>LEVEL_CELL_LIMIT-total[m])valid=false;else total[m]+=cost[m];}
        }
        if(valid)
        {
            for(int m=0;m<gMaterialCount;m++)tV082Delta[m]-=total[m];
            PrintToServer("[V082Probe] recorded external real-UI target upgrade levels %d through %d once; no inventory was altered by recording.",first,last);
        }
        else V082Check(false,"record_upgrade_requires_valid_already_reached_target_levels");
    }
    else ReplyToCommand(client,"sm_v082_probe bootstrap|setup|status|placement|build|aim|finish|release|fund|transactions|weapons|power_setup|power_off|power_on|work_prepare repair|construction|work_restore|record_weapon <id>|record_material <id>|record_upgrade <target_level> [last_target_level]|cleanup");
    return Plugin_Handled;
}

void RegisterV082CraftingProbe()
{
    RegAdminCmd("sm_v082_probe",V082ProbeCommand,ADMFLAG_ROOT,"Temporary v0.8.2 one-real-human crafting diagnostics; no bots or respawns.");
}
