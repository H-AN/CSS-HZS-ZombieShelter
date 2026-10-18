// Test binary only. One existing living Steam client; stock props are built
// through production ray placement/payment. Existing NPCs use V05's parked fixture.
bool tBeaconRealActive,tBeaconRealAimReady;
int tBeaconRealSerial,tBeaconRealEpoch,tBeaconRealRevision,tBeaconRealRecipe=-1;
int tBeaconRealSlots[3],tBeaconRealUIDs[3],tBeaconRealSource,tBeaconRealTarget,tBeaconRealAimAttempt;
int tBeaconRealBeforeNonce,tBeaconRealBeforeCount,tBeaconRealInventory[MATERIALS],tBeaconWallCore=-1,tBeaconWallBlocker;
float tBeaconRealFeet[3][3],tBeaconRealBefore[3],tBeaconWallPoint[3],tBeaconWallFeet[3];

bool BeaconRealSession(int client)
{
    return tBeaconRealActive && tV05RealActive && BeaconActor(client)
        && GetClientSerial(client)==tBeaconRealSerial && gEpoch==tBeaconRealEpoch && gConfigRevision==tBeaconRealRevision;
}

bool BeaconRealPlacementForCore(int client,int recipe,int core,float feet[3],float point[3])
{
    for(int p=0;p<gCores[core].pointCount;p++)for(int ring=0;ring<3;ring++)for(int direction=0;direction<16;direction++)
    {
        feet=gCorePoints[core][p];feet[2]+=2.0;
        float angle=DegToRad(float(direction)*22.5),start[3],end[3],normal[3];int hit;
        start=feet;start[0]+=Cosine(angle)*(64.0+float(ring)*24.0);start[1]+=Sine(angle)*(64.0+float(ring)*24.0);
        start[2]+=48.0;end=start;end[2]-=176.0;
        if(V05TraceSurface(client,start,end,point,normal,hit) && normal[2]>0.8 && BuildCoreAt(point)==core
            && V05AimMatches(client,recipe,feet,point,core))return true;
    }
    return false;
}

bool BeginBeaconReal(int client)
{
    if(BeaconRealSession(client))
    {
        for(int c=0;c<3;c++)if(tBeaconRealSlots[c]<0 || !BeaconBuildingAlive(tBeaconRealSlots[c])
            || gBuildings[tBeaconRealSlots[c]].uid!=tBeaconRealUIDs[c])
        {Check(false,"a partially failed beacon fixture must be cleaned up before another UI setup");return false;}
        return true;
    }
    if(!BeaconActor(client) || tV05RealActive || gCoreCount<3 || !gBeaconConfigAvailable)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP beacon real requires one existing living Steam client, three bases, and no other active V05 fixture");return false;}
    for(int core=0;core<3;core++)if(!TravelBaseAlive(core) || BeaconCount(core)>0)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP beacon real preserves pre-existing beacons and destroyed bases");return false;}
    tBeaconRealRecipe=-1;
    for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Beacon){tBeaconRealRecipe=r;break;}
    if(tBeaconRealRecipe<0){Check(false,"beacon real fixture finds a configured beacon recipe");return false;}
    if(!V05BeginRealFixture(client))return false;
    tBeaconRealActive=true;tBeaconRealSerial=GetClientSerial(client);tBeaconRealEpoch=gEpoch;tBeaconRealRevision=gConfigRevision;
    for(int core=0;core<3;core++){tBeaconRealSlots[core]=-1;tBeaconRealUIDs[core]=0;}
    for(int core=0;core<3;core++)
    {
        float point[3];
        if(!BeaconRealPlacementForCore(client,tBeaconRealRecipe,core,tBeaconRealFeet[core],point))
        {Check(false,"real beacon finds an actual buildable surface and eye ray in each requested base");return false;}
        int b=V05BuildFromRealAim(client,tBeaconRealRecipe,core,"real Steam client ray-builds one beacon in its requested base and pays the configured recipe once");
        if(b<0)return false;tBeaconRealSlots[core]=b;tBeaconRealUIDs[core]=gBuildings[b].uid;
        Check(gBuildings[b].buildCore==core && gBuildings[b].powerCore==core && BeaconCount(core)==1,
            "real beacon belongs to its original base/power grid and occupies the single group place");
    }
    RecalculatePower();BeaconTick(GetGameTime());
    PrintToServer("[BeaconReal] BUILT serial=%d uid_a=%d uid_b=%d uid_c=%d; power fixture disabled, no client/NPC added or respawned",
        tBeaconRealSerial,tBeaconRealUIDs[0],tBeaconRealUIDs[1],tBeaconRealUIDs[2]);
    return true;
}

bool BeaconScanRealAim(int client,int b,const float feet[3])
{
    float low[3],high[3],hullMin[3]={-16.0,-16.0,0.0},hullMax[3]={16.0,16.0,72.0};int body=Resolve(gBuildings[b].ref);
    if(body<0 || !ClearHull(feet,hullMin,hullMax,client,MASK_PLAYERSOLID))return false;
    GetEntPropVector(body,Prop_Send,"m_vecMins",low);GetEntPropVector(body,Prop_Send,"m_vecMaxs",high);
    for(int x=0;x<3;x++)for(int y=0;y<3;y++)for(int z=0;z<3;z++)
    {
        float local[3],point[3];local[0]=low[0]+(high[0]-low[0])*(0.1+float(x)*0.4);
        local[1]=low[1]+(high[1]-low[1])*(0.1+float(y)*0.4);local[2]=low[2]+(high[2]-low[2])*(0.1+float(z)*0.4);
        WorldPoint(local,gBuildings[b].origin,gBuildings[b].angles,point);V05AimAt(client,feet,point);
        if(BuildingInUseRange(client,b))return true;
    }
    return false;
}

public Action PrepareBeaconRealAim(Handle timer,any serial)
{
    int client=GetClientFromSerial(serial);
    if(!BeaconRealSession(client))return Plugin_Stop;
    int core=tBeaconRealSource,b=tBeaconRealSlots[core];
    if(b<0 || gBuildings[b].uid!=tBeaconRealUIDs[core])return Plugin_Stop;
    bool aimed=BeaconScanRealAim(client,b,tBeaconRealFeet[core]);
    float center[3],low[3],high[3];int body=Resolve(gBuildings[b].ref);
    if(body>0)ModelWorldBounds(body,gBuildings[b].origin,gBuildings[b].angles,low,high);
    for(int a=0;a<3;a++)center[a]=(low[a]+high[a])*0.5;
    for(int ring=0;!aimed && ring<3;ring++)for(int d=0;!aimed && d<16;d++)
    {
        float feet[3],angle=DegToRad(float(d)*22.5);feet=tBeaconRealFeet[core];
        feet[0]=center[0]+Cosine(angle)*(56.0+float(ring)*24.0);feet[1]=center[1]+Sine(angle)*(56.0+float(ring)*24.0);
        aimed=BeaconScanRealAim(client,b,feet);
    }
    tBeaconRealAimAttempt++;
    if(!aimed && tBeaconRealAimAttempt<5)
    {CreateTimer(0.1,PrepareBeaconRealAim,serial,TIMER_FLAG_NO_MAPCHANGE);return Plugin_Stop;}
    tBeaconRealAimReady=aimed;
    Check(aimed,"real beacon UI source has an actual production E ray after bounded collision initialization");
    if(!aimed){PrintToServer("[BeaconRealUI] FAILED source=%d target=%d; no E menu is claimed",core,tBeaconRealTarget);return Plugin_Stop;}
    GetClientAbsOrigin(client,tBeaconRealBefore);tBeaconRealBeforeNonce=gBeaconMenuNonce[client];
    tBeaconRealBeforeCount=SummonCount();tBeaconRealInventory=gPlayerInventory[client];
    SetEntityMoveType(client,MOVETYPE_NONE);gNextUse[client]=0.0;
    int targetKey=1;
    for(int c=0;c<tBeaconRealTarget;c++)if(c!=core && BeaconForCore(c,core)>=0)targetKey++;
    PrintToServer("[BeaconRealUI] READY source_core=%d target_core=%d source_uid=%d target_uid=%d actual_E_ray=1; press real E then menuselect %d; Setup did not call ShowBeaconMenu",
        core,tBeaconRealTarget,gBuildings[b].uid,tBeaconRealUIDs[tBeaconRealTarget],targetKey);
    return Plugin_Stop;
}

void SetupBeaconUI(int client,int sourceCore=0,int targetCore=1)
{
    if(!BeginBeaconReal(client))return;
    if(sourceCore<0 || sourceCore>2 || targetCore<0 || targetCore>2 || sourceCore==targetCore)
    {Check(false,"beacon UI fixture is given two distinct A/B/C bases");return;}
    ResetBeaconClient(client);CancelClientMenu(client);SetEntityMoveType(client,MOVETYPE_WALK);
    tBeaconRealSource=sourceCore;tBeaconRealTarget=targetCore;tBeaconRealAimReady=false;tBeaconRealAimAttempt=0;
    CreateTimer(0.1,PrepareBeaconRealAim,GetClientSerial(client),TIMER_FLAG_NO_MAPCHANGE);
}

void InspectBeaconUI(int client)
{
    if(!BeaconRealSession(client) || !tBeaconRealAimReady){Check(false,"beacon UI open-check has a ready source");return;}
    Menu menu=gBeaconMenus[client];int travel,management;bool containsSelf;
    if(menu!=null && IsValidHandle(menu))for(int i=0;i<menu.ItemCount;i++)
    {
        char token[256];int fields[BT_Count];menu.GetItem(i,token,sizeof(token));if(!ParseBeaconToken(token,fields))continue;
        if(fields[BT_Action]==0){travel++;if(fields[BT_TargetCore]==tBeaconRealSource)containsSelf=true;}
        if(fields[BT_Action]==1)management++;
    }
    Check(menu!=null && travel==2 && management==1 && !containsSelf,
        "actual client E opens the production beacon menu with the other two bases and real management return");
}

void TestBeaconUI(int client)
{
    if(!BeaconRealSession(client) || !tBeaconRealAimReady){Check(false,"beacon travel verification has its ready real-client UI setup");return;}
    float actual[3],velocity[3];GetClientAbsOrigin(client,actual);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",velocity);
    bool paid=true;for(int m=0;m<gMaterialCount;m++)if(tBeaconRealInventory[m]!=gPlayerInventory[client][m])paid=false;
    Check(TravelPointInsideBase(tBeaconRealTarget,actual) && GetVectorDistance(actual,tBeaconRealBefore)>32.0,
        "real E menu selection reaches exactly the selected other base's existing safe zone");
    float hullMin[3]={-16.0,-16.0,0.0},hullMax[3]={16.0,16.0,72.0},down[3],normal[3],landing[3];down=actual;down[2]-=96.0;
    Handle floor=TR_TraceHullFilterEx(actual,down,hullMin,hullMax,MASK_PLAYERSOLID,FilterExcept,client);
    bool supported=TR_DidHit(floor) && !TR_StartSolid(floor) && !TR_AllSolid(floor);
    if(supported){TR_GetEndPosition(landing,floor);TR_GetPlaneNormal(floor,normal);}delete floor;
    Check(ClearHull(actual,hullMin,hullMax,client,MASK_PLAYERSOLID) && supported && normal[2]>=0.7 && actual[2]-landing[2]<=48.0,
        "actual beacon landing has the real player hull free and nearby walkable floor support");
    Check(gBeaconMenus[client]==null && gBeaconMenuNonce[client]>tBeaconRealBeforeNonce
        && SummonCount()==tBeaconRealBeforeCount && paid && GetVectorLength(velocity)<1.0,
        "actual beacon travel consumes its menu once, stops velocity, and does not build or charge materials again");
    PrintToServer("[BeaconRealUI] VERIFY source=%d target=%d position=%.3f %.3f %.3f inside_target=%d inventory_unchanged=%d",
        tBeaconRealSource,tBeaconRealTarget,actual[0],actual[1],actual[2],TravelPointInsideBase(tBeaconRealTarget,actual),paid);
    tBeaconRealAimReady=false;
}

int BeaconRealMenuRow(Menu menu,int targetCore)
{
    if(menu==null || !IsValidHandle(menu))return -1;
    for(int i=0;i<menu.ItemCount;i++)
    {char token[256];int fields[BT_Count];menu.GetItem(i,token,sizeof(token));if(ParseBeaconToken(token,fields) && fields[BT_Action]==0 && fields[BT_TargetCore]==targetCore)return i;}
    return -1;
}

void TestBeaconRealGuards(int client)
{
    if(!BeaconRealSession(client) || !tBeaconRealAimReady || !BuildingInUseRange(client,tBeaconRealSlots[tBeaconRealSource]))
    {Check(false,"beacon real guard checks require a ready actual source aim");return;}
    int source=tBeaconRealSlots[tBeaconRealSource],target=tBeaconRealSlots[tBeaconRealTarget];
    int foundSource,foundTarget,core,operation;char token[256];
    BeaconMenuToken(client,source,tBeaconRealTarget,target,0,token,sizeof(token));
    Check(ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"living real client source aim validates a beacon transaction");
    Check(!ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"real client cannot replay a consumed beacon nonce");
    int fields[BT_Count];BeaconMenuToken(client,source,tBeaconRealTarget,target,0,token,sizeof(token));ParseBeaconToken(token,fields);fields[BT_Serial]++;
    BeaconEncodeFields(fields,token,sizeof(token));Check(!ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"another client serial cannot inherit this real traveller's beacon menu");
    BeaconMenuToken(client,source,tBeaconRealTarget,target,0,token,sizeof(token));gBuildings[source].generation++;
    Check(!ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"source entity-generation replacement invalidates a real beacon menu");gBuildings[source].generation--;
    BeaconMenuToken(client,source,tBeaconRealTarget,target,0,token,sizeof(token));gBuildings[target].generation++;
    Check(!ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"destination entity-generation replacement invalidates a real beacon menu");gBuildings[target].generation--;
    BeaconMenuToken(client,source,tBeaconRealTarget,target,0,token,sizeof(token));
    bool sourcePower=gBuildings[source].powered,targetPower=gBuildings[target].powered;gPowerEnabled=true;gBuildings[source].powered=false;
    Check(!ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"real source power loss after menu opening rejects travel");
    gBuildings[source].powered=true;gBuildings[target].powered=false;
    Check(!ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"real destination power loss rejects travel");
    gPowerEnabled=false;gBuildings[source].powered=sourcePower;gBuildings[target].powered=targetPower;
    Check(ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"power-disabled fixture uses the same production power.enabled=0 policy");

    float feet[3],view[3],away[3];GetClientAbsOrigin(client,feet);GetClientEyeAngles(client,view);
    BeaconMenuToken(client,source,tBeaconRealTarget,target,0,token,sizeof(token));away=feet;away[2]+=512.0;
    TeleportEntity(client,away,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    Check(!ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"moving away after opening a beacon menu fails the actual source-use range check");
    TeleportEntity(client,feet,view,view_as<float>({0.0,0.0,0.0}));

    BeaconMenuToken(client,source,tBeaconRealTarget,target,0,token,sizeof(token));
    int cost[MATERIALS],maximum,savedInventory[MATERIALS];savedInventory=gPlayerInventory[client];int oldLevel=gBuildings[source].level;
    bool quote=ReadBuildingLevelQuote(source,oldLevel+1,cost,maximum);
    if(quote)
    {
        for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]<cost[m])gPlayerInventory[client][m]=cost[m];
        bool upgraded=TryUpgrade(client,source);
        Check(upgraded && gBuildings[source].level==oldLevel+1 && BeaconCount(tBeaconRealSource)==1,
            "real production upgrade succeeds on the existing beacon without taking a second group place");
        Check(!ValidateBeaconMenuToken(client,token,foundSource,foundTarget,core,operation),"a real source upgrade invalidates the earlier-level beacon menu");
    }
    else{tSkipped++;PrintToServer("[ShelterTest] SKIP beacon upgrade guard: no configured next level");}
    gPlayerInventory[client]=savedInventory;

    // Exercise the production selection handler with a target destroyed at the
    // click boundary, then with only the requested base's landing candidates absent.
    ShowBeaconMenu(client,source);Menu menu=gBeaconMenus[client];int row=BeaconRealMenuRow(menu,tBeaconRealTarget);
    int base=gCores[tBeaconRealTarget].building;bool alive=gBuildings[base].alive;float before[3],after[3];GetClientAbsOrigin(client,before);
    gBuildings[base].alive=false;
    if(row>=0)BeaconMenuHandler(menu,MenuAction_Select,client,row);
    GetClientAbsOrigin(client,after);gBuildings[base].alive=alive;
    Check(row>=0 && GetVectorDistance(before,after)<0.01 && gBeaconMenus[client]==null,
        "actual beacon click after target-base destruction rejects travel and clears its menu");
    ShowBeaconMenu(client,source);menu=gBeaconMenus[client];row=BeaconRealMenuRow(menu,tBeaconRealTarget);
    int points=gCores[tBeaconRealTarget].pointCount;gCores[tBeaconRealTarget].pointCount=0;
    if(row>=0)BeaconMenuHandler(menu,MenuAction_Select,client,row);
    GetClientAbsOrigin(client,after);gCores[tBeaconRealTarget].pointCount=points;ResetBeaconClient(client);
    Check(row>=0 && GetVectorDistance(before,after)<0.01,"selected beacon base without safe landing candidates does not send the traveller to another base");

    bool savedAlive[3];
    for(int c=0;c<3;c++){savedAlive[c]=gBuildings[tBeaconRealSlots[c]].alive;if(c!=tBeaconRealSource)gBuildings[tBeaconRealSlots[c]].alive=false;}
    ShowBeaconMenu(client,source);menu=gBeaconMenus[client];int destinations;
    if(menu!=null && IsValidHandle(menu))for(int i=0;i<menu.ItemCount;i++)
    {menu.GetItem(i,token,sizeof(token));if(ParseBeaconToken(token,fields) && fields[BT_Action]==0)destinations++;}
    Check(menu!=null && destinations==0 && BeaconForCore(tBeaconRealSource,tBeaconRealSource)<0,
        "one remaining effective beacon offers no self-base destination");ResetBeaconClient(client);
    for(int c=0;c<3;c++)gBuildings[tBeaconRealSlots[c]].alive=savedAlive[c];
    BeaconTick(GetGameTime());tBeaconRealAimReady=false;
    PrintToServer("[BeaconReal] GUARDS complete; source upgrade kept only inside fixture; all base states, points and inventory restored");
}

bool BeaconWallOccluded(int client,const float point[3],int body,int &blocker)
{
    float eye[3];GetClientEyePosition(client,eye);
    Handle trace=TR_TraceRayFilterEx(eye,point,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
    bool hit=TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace) && TR_GetFraction(trace)<0.97;
    blocker=hit ? TR_GetEntityIndex(trace) : -1;delete trace;
    if(!hit || blocker==body)return false;
    if(blocker==0)return true;
    char classname[64];if(blocker>MaxClients && IsValidEntity(blocker))GetEntityClassname(blocker,classname,sizeof(classname));
    if(StrContains(classname,"func_")==0)return true;
    int building=FindBuilding(blocker);return building>=0 && gBuildings[building].kind==Kind_Base;
}

void SetupBeaconWallView(int client,int core)
{
    if(!BeaconRealSession(client) || core<0 || core>2){Check(false,"beacon wall view has an active real-client fixture and requested base");return;}
    int b=tBeaconRealSlots[core],body=Resolve(gBuildings[b].ref);SyncBeaconMarker(b);tBeaconWallCore=-1;
    char path[128];Format(path,sizeof(path),"Types/%s/Marker",gBuildings[b].type);float offset[3];
    if(body<0 || !BeaconMarkerVector(path,"offset","0 0 72",offset,-512.0,512.0) || BeaconMarkerEntity(b)<0)
    {Check(false,"beacon wall screenshot has a real effective marker entity");return;}
    BuildingAttachmentPosition(b,offset,tBeaconWallPoint);ResetBeaconClient(client);CancelClientMenu(client);
    float hullMin[3]={-16.0,-16.0,0.0},hullMax[3]={16.0,16.0,72.0};bool found;
    for(int ring=1;!found && ring<=8;ring++)for(int direction=0;!found && direction<32;direction++)for(int z=0;!found && z<3;z++)
    {
        float feet[3],angle=DegToRad(float(direction)*11.25);feet=gBuildings[b].origin;
        feet[0]+=Cosine(angle)*(96.0+float(ring)*64.0);feet[1]+=Sine(angle)*(96.0+float(ring)*64.0);feet[2]+=float(z)*48.0+2.0;
        if(TR_PointOutsideWorld(feet) || !ClearHull(feet,hullMin,hullMax,client,MASK_PLAYERSOLID))continue;
        V05AimAt(client,feet,tBeaconWallPoint);
        if(BeaconWallOccluded(client,tBeaconWallPoint,body,tBeaconWallBlocker)){tBeaconWallFeet=feet;found=true;}
    }
    if(!found){tSkipped++;PrintToServer("[ShelterTest] SKIP beacon wall view: no clear real-human camera behind an actual map/base wall; no synthetic wall substituted");return;}
    SetEntityMoveType(client,MOVETYPE_NONE);tBeaconWallCore=core;tBeaconRealAimReady=false;
    Check(true,"beacon visual camera has an actual world/brush/base-wall obstruction before the sprite");
    PrintToServer("[BeaconWall] READY core=%d uid=%d marker_ref=%d blocker=%d feet=%.3f %.3f %.3f sprite=%.3f %.3f %.3f; wait0.5 and capture original client JPEG; visual result requires image review",
        core,gBuildings[b].uid,gBeaconMarkerRef[b],tBeaconWallBlocker,tBeaconWallFeet[0],tBeaconWallFeet[1],tBeaconWallFeet[2],tBeaconWallPoint[0],tBeaconWallPoint[1],tBeaconWallPoint[2]);
}

void TestBeaconWallView(int client)
{
    if(!BeaconRealSession(client) || tBeaconWallCore<0){tSkipped++;PrintToServer("[ShelterTest] SKIP beacon wall verify without a real occluded camera");return;}
    int b=tBeaconRealSlots[tBeaconWallCore],blocker;
    Check(BeaconMarkerEntity(b)>0 && BeaconWallOccluded(client,tBeaconWallPoint,Resolve(gBuildings[b].ref),blocker),
        "saved beacon screenshot viewpoint remains behind a real wall with its live marker present");
    PrintToServer("[BeaconWall] VERIFY core=%d blocker=%d sprite_ref=%d; this assertion does not claim the screenshot was reviewed",
        tBeaconWallCore,blocker,gBeaconMarkerRef[b]);
}

void CleanupBeaconReal(int client)
{
    if(!tBeaconRealActive)return;
    int actor=GetClientFromSerial(tBeaconRealSerial);
    if(client!=actor || actor<1 || !IsClientInGame(actor))
    {Check(false,"beacon cleanup requires its original real-client serial; no replacement actor or respawn is used");return;}
    ResetBeaconClient(client);
    for(int c=0;c<3;c++)
    {
        int b=tBeaconRealSlots[c];
        if(b>=0 && b<MAX_BUILDINGS && gBuildings[b].uid==tBeaconRealUIDs[c])RemoveBeaconMarker(b);
        tBeaconRealSlots[c]=-1;tBeaconRealUIDs[c]=0;
    }
    tBeaconRealActive=false;tBeaconRealAimReady=false;tBeaconWallCore=-1;V05RealCleanup(client);
    PrintToServer("[BeaconReal] CLEANUP original actor/inventory/power/exposure/NPC fixture restored; no client or NPC respawned");
}

bool BeaconRealClientCommand(const char[] which,int client)
{
    if(StrEqual(which,"beacon_ui_a_b"))SetupBeaconUI(client,0,1);
    else if(StrEqual(which,"beacon_ui_b_c"))SetupBeaconUI(client,1,2);
    else if(StrEqual(which,"beacon_ui_c_a"))SetupBeaconUI(client,2,0);
    else if(StrEqual(which,"beacon_ui_opened"))InspectBeaconUI(client);
    else if(StrEqual(which,"beacon_ui_verify"))TestBeaconUI(client);
    else if(StrEqual(which,"beacon_real"))TestBeaconRealGuards(client);
    else if(StrEqual(which,"beacon_wall_a"))SetupBeaconWallView(client,0);
    else if(StrEqual(which,"beacon_wall_b"))SetupBeaconWallView(client,1);
    else if(StrEqual(which,"beacon_wall_c"))SetupBeaconWallView(client,2);
    else if(StrEqual(which,"beacon_wall_verify"))TestBeaconWallView(client);
    else if(StrEqual(which,"beacon_cleanup"))CleanupBeaconReal(client);
    else return false;
    return true;
}
