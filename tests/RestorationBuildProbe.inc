// Test binary only. Real ray placement/payment and real stock model bodies.
// The V05 fixture preserves the existing human, inventory and native NPC pool.
int RestorationRecipe(const char[] type)
{
    for(int r=0;r<gRecipeCount;r++)if(StrEqual(gRecipes[r].type,type) && IsRestorationKind(gRecipes[r].kind))return r;
    return -1;
}

bool RestorationBuildSpacing(const float point[3],int core)
{
    for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive
        && gBuildings[b].buildCore==core && gBuildings[b].ownerSerial==tV05Serial && gBuildings[b].uid>tV05StartUID)
    {
        float x=point[0]-gBuildings[b].origin[0],y=point[1]-gBuildings[b].origin[1];
        if(x*x+y*y<25600.0)return false; // Leave both real model colliders accessible for upgrades.
    }
    return true;
}

bool RestorationBuildAim(int client,int recipe,int expectedCore,float feet[3],int &core)
{
    float point[3],normal[3];int hit,foundCore=-1;
    if(V05FindRealPlacement(client,recipe,0,feet,point,normal,hit,foundCore)
        && (expectedCore<0 || foundCore==expectedCore) && RestorationBuildSpacing(point,foundCore)){core=foundCore;return true;}
    if(expectedCore<0)return false;
    // A previously built model can obstruct the first sample. Keep tracing real
    // floor/model surfaces around the requested base's existing safe points.
    for(int p=0;p<gCores[expectedCore].pointCount;p++)for(int ring=0;ring<4;ring++)for(int direction=0;direction<16;direction++)
    {
        feet=gCorePoints[expectedCore][p];feet[2]+=2.0;
        float angle=DegToRad(float(direction)*22.5),start[3],end[3];start=feet;
        start[0]+=Cosine(angle)*(64.0+float(ring)*32.0);start[1]+=Sine(angle)*(64.0+float(ring)*32.0);
        start[2]+=48.0;end=start;end[2]-=176.0;
        if(V05TraceSurface(client,start,end,point,normal,hit) && normal[2]>0.8 && BuildCoreAt(point)==expectedCore
            && RestorationBuildSpacing(point,expectedCore)
            && V05AimMatches(client,recipe,feet,point,expectedCore)){core=expectedCore;return true;}
    }
    return false;
}

bool RestorationActualModel(int b)
{
    if(b<0 || b>=MAX_BUILDINGS)return false;
    int entity=Resolve(gBuildings[b].ref);if(entity<0)return false;
    char model[PLATFORM_MAX_PATH];GetEntPropString(entity,Prop_Data,"m_ModelName",model,sizeof(model));
    return StrEqual(model,gBuildings[b].model,false);
}

void RestorationCheckUpgrade(int client,int b,int expectedHP,float expectedPower,int expectedAmount,float expectedInterval)
{
    if(b<0)return;
    if(!SafetyAimBuilding(client,b) && !PowerAimBuilding(client,b))
    {Check(false,"a real client can aim at the built station within the production upgrade use distance");return;}
    int cost[MATERIALS],maximum;
    if(!ReadBuildingLevelQuote(b,gBuildings[b].level+1,cost,maximum))
    {Check(false,"the built station has a payable production next-level quote");return;}
    for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]<cost[m])gPlayerInventory[client][m]=cost[m];
    int before[MATERIALS],invested[MATERIALS];before=gPlayerInventory[client];invested=gBuildings[b].invested;
    int missing=17,ref=gBuildings[b].ref;gBuildings[b].hp=gBuildings[b].maxHP-missing;
    gBuildings[b].nextRestore=GetGameTime()-100.0;float started=GetGameTime();
    bool upgraded=TryUpgrade(client,b),paid=true;
    for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-cost[m]
        || gBuildings[b].invested[m]!=invested[m]+cost[m])paid=false;
    Check(upgraded && paid && gBuildings[b].level==2 && gBuildings[b].maxHP==expectedHP && maximum==expectedHP
        && gBuildings[b].hp==expectedHP-missing && gBuildings[b].ref==ref && RestorationActualModel(b),
        "actual station upgrade spends its quoted personal materials once, retains the real body and preserves the missing HP amount");
    Check(FloatAbs(gBuildings[b].powerDemand-expectedPower)<0.01 && gBuildings[b].restoreAmount==expectedAmount
        && FloatAbs(gBuildings[b].restoreInterval-expectedInterval)<0.01
        && gBuildings[b].nextRestore>=started+expectedInterval-0.05
        && gBuildings[b].nextRestore<=GetGameTime()+expectedInterval+0.05,
        "actual station upgrade applies new power/recovery fields and schedules a full new recovery interval");
    PrintToServer("[RestorationBuildUpgrade] type=%s uid=%d level=%d max_hp=%d hp=%d missing=%d power=%.1f amount=%d interval=%.3f next_in=%.3f paid_exactly_once=%d",
        gBuildings[b].type,gBuildings[b].uid,gBuildings[b].level,gBuildings[b].maxHP,gBuildings[b].hp,
        gBuildings[b].maxHP-gBuildings[b].hp,gBuildings[b].powerDemand,gBuildings[b].restoreAmount,
        gBuildings[b].restoreInterval,gBuildings[b].nextRestore-GetGameTime(),paid);
}

void TestRestorationBuildReal(int client)
{
    if(client<1 || !LiveHuman(client) || IsFakeClient(client) || !Running() || tV05RealActive)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP paid restoration build needs one existing living real client and no other active fixture");return;}
    int lowRecipe=RestorationRecipe("repair_low"),highRecipe=RestorationRecipe("repair_high");
    int healRecipe=RestorationRecipe("heal_low"),armorRecipe=RestorationRecipe("armor_low");
    if(lowRecipe<0 || highRecipe<0 || healRecipe<0 || armorRecipe<0)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP paid restoration build requires all four optional default station recipes");return;}
    if(!V05BeginRealFixture(client))return;
    float feet[3];int core=-1;
    if(!RestorationBuildAim(client,lowRecipe,-1,feet,core) || RestorationCount(core,Kind_AutoRepair)!=0
        || RestorationBuildLimit(C_Repair,"repair_low",1)!=2 || RestorationBuildLimit(C_Repair,"repair_high",1)!=2)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP paid restoration fixture preserves existing stations or non-default group caps and requires a real free surface");V05RealCleanup(client);return;}
    int low=V05BuildFromRealAim(client,lowRecipe,core,"the real client ray-builds repair_low and pays its exact configured recipe once");
    int high=-1;
    if(RestorationBuildAim(client,highRecipe,core,feet,core))
        high=V05BuildFromRealAim(client,highRecipe,core,"the same real client ray-builds repair_high in the same base through the second Types entry");
    else Check(false,"the second repair variant finds another real buildable surface in the original base");
    if(low>=0 && high>=0)
    {
        Check(RestorationActualModel(low) && RestorationActualModel(high) && RestorationCount(core,Kind_AutoRepair)==2
            && gBuildings[low].maxHP==500 && gBuildings[low].powerDemand==5.0 && gBuildings[low].restoreAmount==1 && gBuildings[low].restoreInterval==20.0
            && gBuildings[high].maxHP==900 && gBuildings[high].powerDemand==20.0 && gBuildings[high].restoreAmount==5 && gBuildings[high].restoreInterval==10.0,
            "two actual low/high station models share the class cap and carry their distinct HP, power and 20s/1 versus 10s/5 production settings");
        if(RestorationBuildAim(client,lowRecipe,core,feet,core))
        {
            V05EnsureRecipeMaterials(client,lowRecipe);int before[MATERIALS];before=gPlayerInventory[client];
            int count=SummonCount(),uid=gUID;float anchor[3],origin[3],angles[3];RefreshBuildAreas();
            bool otherGates=PlacementForRecipe(client,lowRecipe,anchor,origin,angles,false) && BuildCoreAt(anchor)==core
                && gBuildAreas[core].count<gBuildAreas[core].limit && count<gSummonLimit && CanAllocateEntities(2,true);
            bool built=BuildRecipe(client,lowRecipe,core),unchanged=true;
            for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m])unchanged=false;
            Check(otherGates && !built && unchanged && SummonCount()==count && gUID==uid && RestorationCount(core,Kind_AutoRepair)==2,
                "the third same-class station is rejected across Types without charging materials or consuming any building UID");
        }
        else Check(false,"the full-group rejection test still has a valid real placement ray");
        RestorationCheckUpgrade(client,low,650,6.0,2,18.0);
        RestorationCheckUpgrade(client,high,1100,25.0,6,9.0);
        Check(RestorationCount(core,Kind_AutoRepair)==2,"upgrading existing stations remains legal at the shared two-station group cap");
    }
    int heal=-1,armor=-1;
    if(RestorationBuildAim(client,healRecipe,core,feet,core))
        heal=V05BuildFromRealAim(client,healRecipe,core,"the real client ray-builds and pays for an actual configured healing-station model");
    else Check(false,"the healing class finds a real buildable surface");
    if(RestorationBuildAim(client,armorRecipe,core,feet,core))
        armor=V05BuildFromRealAim(client,armorRecipe,core,"the real client ray-builds and pays for an actual configured armor-station model");
    else Check(false,"the armor class finds a real buildable surface");
    Check(heal>=0 && armor>=0 && RestorationActualModel(heal) && RestorationActualModel(armor)
        && gBuildings[heal].kind==Kind_Heal && gBuildings[armor].kind==Kind_Armor
        && gBuildings[heal].buildCore==core && gBuildings[armor].buildCore==core,
        "repair, heal and armor each use a separately paid real model with the original fixed-base assignment");
    PrintToServer("[RestorationBuild] core=%d low_uid=%d high_uid=%d heal_uid=%d armor_uid=%d client_npc_creation=0",
        core,low>=0 ? gBuildings[low].uid : 0,high>=0 ? gBuildings[high].uid : 0,heal>=0 ? gBuildings[heal].uid : 0,armor>=0 ? gBuildings[armor].uid : 0);
    V05RealCleanup(client);
}

bool tRestorationVisualActive;
int tRestorationVisualSerial,tRestorationVisualEpoch,tRestorationVisualRevision,tRestorationVisualStation=-1,tRestorationVisualUID;
int tRestorationVisualArmor,tRestorationVisualPulses,tRestorationVisualTicks;
int tRestorationVisualHP[MAX_BUILDINGS],tRestorationVisualSavedUID[MAX_BUILDINGS],tRestorationVisualSavedRef[MAX_BUILDINGS];
float tRestorationVisualNext[MAX_BUILDINGS];
Handle tRestorationVisualTimer;

bool RestorationVisualSession(int client)
{
    return tRestorationVisualActive && tV05RealActive && LiveHuman(client) && !IsFakeClient(client)
        && GetClientSerial(client)==tRestorationVisualSerial && gEpoch==tRestorationVisualEpoch
        && gConfigRevision==tRestorationVisualRevision && tRestorationVisualStation>=0
        && gBuildings[tRestorationVisualStation].used && gBuildings[tRestorationVisualStation].alive
        && gBuildings[tRestorationVisualStation].uid==tRestorationVisualUID && Resolve(gBuildings[tRestorationVisualStation].ref)>0;
}

int RestorationVisualPulse(int client)
{
    if(!RestorationVisualSession(client))return 0;
    int station=tRestorationVisualStation;
    if(gBuildings[station].restoreAmount<1)return 0;
    if(gBuildings[station].kind==Kind_AutoRepair)
    {if(gBuildings[station].maxHP<=1)return 0;gBuildings[station].hp=gBuildings[station].maxHP-1;}
    else if(gBuildings[station].kind==Kind_Heal)
    {
        int maximum=RestorationHealthLimit(client);if(maximum<=1)return 0;
        SetEntityHealth(client,maximum-1);
    }
    else SetEntProp(client,Prop_Send,"m_ArmorValue",99);
    int changed=RestorationPulse(station);if(changed>0)tRestorationVisualPulses++;
    return changed;
}

public Action RestorationVisualTimer(Handle timer,any serial)
{
    int client=GetClientFromSerial(serial);
    if(timer!=tRestorationVisualTimer || !RestorationVisualSession(client) || tRestorationVisualTicks>=40)
    {tRestorationVisualTimer=null;return Plugin_Stop;}
    tRestorationVisualTicks++;RestorationVisualPulse(client);return Plugin_Continue;
}

void SetupRestorationVisual(int client,BuildingKind kind)
{
    if(client<1 || !LiveHuman(client) || IsFakeClient(client) || !Running() || !IsRestorationKind(kind) || tV05RealActive)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP station visual needs an existing living real client and an unused fixture");return;}
    int humans;for(int c=1;c<=MaxClients;c++)if(LiveHuman(c) && !IsFakeClient(c))humans++;
    if(humans!=1){tSkipped++;PrintToServer("[ShelterTest] SKIP station visual preserves other living real players");return;}
    char type[32];strcopy(type,sizeof(type),kind==Kind_AutoRepair ? "repair_low" : (kind==Kind_Heal ? "heal_low" : "armor_low"));
    int recipe=RestorationRecipe(type);
    if(recipe<0 || !V05BeginRealFixture(client)){tSkipped++;return;}
    tRestorationVisualArmor=GetEntProp(client,Prop_Send,"m_ArmorValue");
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        tRestorationVisualHP[b]=gBuildings[b].hp;tRestorationVisualSavedUID[b]=gBuildings[b].uid;
        tRestorationVisualSavedRef[b]=gBuildings[b].ref;tRestorationVisualNext[b]=gBuildings[b].nextRestore;
        if(gBuildings[b].used && gBuildings[b].alive)gBuildings[b].hp=gBuildings[b].maxHP;
        if(IsRestorationKind(gBuildings[b].kind))gBuildings[b].nextRestore=GetGameTime()+3600.0;
    }
    tRestorationVisualActive=true;tRestorationVisualSerial=GetClientSerial(client);tRestorationVisualEpoch=gEpoch;tRestorationVisualRevision=gConfigRevision;
    tRestorationVisualPulses=0;tRestorationVisualTicks=0;tRestorationVisualStation=-1;
    float feet[3];int core=-1;
    if(!RestorationBuildAim(client,recipe,-1,feet,core))
    {Check(false,"station visual finds an actual ray-buildable floor");CleanupRestorationVisual(client);return;}
    int station=V05BuildFromRealAim(client,recipe,core,"visual fixture pays for a real station model through the production build transaction");
    if(station<0){CleanupRestorationVisual(client);return;}
    tRestorationVisualStation=station;tRestorationVisualUID=gBuildings[station].uid;gBuildings[station].nextRestore=GetGameTime()+3600.0;
    SetEntityMoveType(client,MOVETYPE_NOCLIP);float target[3],camera[3];EntityCenter(Resolve(gBuildings[station].ref),target);camera=feet;
    float mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0};bool cameraFound;
    for(int height=0;height<3 && !cameraFound;height++)for(int angle=0;angle<16 && !cameraFound;angle++)
    {
        float candidate[3],heading=DegToRad(float(angle)*22.5);candidate=gBuildings[station].origin;
        candidate[0]+=Cosine(heading)*180.0;candidate[1]+=Sine(heading)*180.0;candidate[2]+=2.0+float(height)*36.0;
        if((kind!=Kind_AutoRepair && !RestorationSafeContains(core,candidate)) || !ClearHull(candidate,mins,maxs,client,MASK_PLAYERSOLID))continue;
        float eye[3];eye=candidate;eye[2]+=64.0;
        if(LineClear(eye,target,client,Resolve(gBuildings[station].ref))){camera=candidate;cameraFound=true;}
    }
    V05AimAt(client,camera,target);CancelClientMenu(client);
    if(kind!=Kind_AutoRepair && !RestorationHuman(client,core))
    {Check(false,"real healing/armor visual camera remains in the original green volume");CleanupRestorationVisual(client);return;}
    tRestorationVisualTimer=CreateTimer(0.5,RestorationVisualTimer,tRestorationVisualSerial,TIMER_REPEAT|TIMER_FLAG_NO_MAPCHANGE);
    Check(RestorationActualModel(station) && RestorationVisualPulse(client)>0,"the real paid station and an actual useful production recovery pulse are ready for screenshot review");
    PrintToServer("[RestorationVisual] READY kind=%d type=%s core=%d uid=%d entity=%d camera=%.2f %.2f %.2f fixture_pulse_every=0.5 lifetime=0.7 window=20s; inspect screenshots for the actual model and ring color; gameplay interval unchanged=%.1f",
        view_as<int>(kind),gBuildings[station].type,core,gBuildings[station].uid,Resolve(gBuildings[station].ref),camera[0],camera[1],camera[2],gBuildings[station].restoreInterval);
}

void VerifyRestorationVisual(int client)
{
    if(!RestorationVisualSession(client))
    {tSkipped++;PrintToServer("[ShelterTest] SKIP station visual verify requires its original active real-client setup");return;}
    int before=gRestorationPulseCount,changed=RestorationVisualPulse(client);
    Check(changed>0 && gRestorationPulseCount==before+1 && tRestorationVisualPulses>0
        && RestorationActualModel(tRestorationVisualStation),"the screenshot fixture still has its actual station model and emits another useful recovery pulse");
    PrintToServer("[RestorationVisual] VERIFY actual_useful_pulses=%d callback_ticks=%d screenshot_color_review=required_separately",tRestorationVisualPulses,tRestorationVisualTicks);
}

void CleanupRestorationVisual(int client)
{
    if(!tRestorationVisualActive)return;
    if(tRestorationVisualTimer!=null){delete tRestorationVisualTimer;tRestorationVisualTimer=null;}
    if(client<1 || !IsClientInGame(client) || GetClientSerial(client)!=tRestorationVisualSerial
        || gEpoch!=tRestorationVisualEpoch || gConfigRevision!=tRestorationVisualRevision)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP station visual restore crossed client/epoch/config identity; isolate and stop this fixture session");return;}
    for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].uid==tRestorationVisualSavedUID[b] && gBuildings[b].ref==tRestorationVisualSavedRef[b])
    {gBuildings[b].hp=tRestorationVisualHP[b];gBuildings[b].nextRestore=tRestorationVisualNext[b];}
    SetEntProp(client,Prop_Send,"m_ArmorValue",tRestorationVisualArmor);
    tRestorationVisualActive=false;tRestorationVisualStation=-1;V05RealCleanup(client);
    PrintToServer("[RestorationVisual] CLEANUP original_HP_armor_deadlines_inventory_and_NPC_fixture_restored=1");
}
