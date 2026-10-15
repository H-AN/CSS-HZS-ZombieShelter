// Test-only rotation checks. No bot, client or NPC is created.
void TestFenceRotationMath()
{
    Check(FloatAbs(NormalizeFenceAngle(179.0+5.0)+176.0)<0.001,"defensive pitch/yaw +5 wraps through +180 into the normalized negative range");
    Check(FloatAbs(NormalizeFenceAngle(-179.0-5.0)-176.0)<0.001,"defensive pitch/yaw -5 wraps through -180 into the normalized positive range");
    Check(NormalizeFenceAngle(360.0)==0.0 && NormalizeFenceAngle(-360.0)==0.0,"complete rotations normalize to zero without changing the other axes");
    Check(NormalizeFenceAngle(180.0)==-180.0 && NormalizeFenceAngle(-180.0)==-180.0,"rotation has a deterministic inclusive -180 and exclusive +180 boundary");
}

// Invoke only while a real test player aims at their existing test-built fence.
// Original orientation and menu state are restored at the end of this callback.
void TestFenceRotationCurrentAim(int client)
{
    int building=client>0 ? FindBuilding(AimedEntity(client,gUseDistance)) : -1;
    if(client<1 || IsFakeClient(client) || !CanContinueFenceRotationMenu(client,building) || !BuildingInUseRange(client,building))
    {tSkipped++;PrintToServer("[ShelterTest] SKIP rotation security requires a living real client aiming at a manageable defensive structure");return;}
    Building before;before=gBuildings[building];int nonce=gDefenseMenuNonce[client],decoded,operation;char token[128];
    DefenseMenuToken(client,building,8,token,sizeof(token));
    Check(ValidateDefenseMenuToken(client,token,decoded,operation) && decoded==building && operation==8,
        "current real defensive rotation token validates its UID, generation, serial, epoch, revision, permission and OBB distance");
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"replaying a consumed rotation button cannot apply a second step");
    DefenseMenuToken(client,building,8,token,sizeof(token));gBuildings[building].generation++;
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"recreated defensive body generation rejects an old rotation menu");gBuildings[building].generation--;
    DefenseMenuToken(client,building,8,token,sizeof(token));gConfigRevision++;
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"a different configuration revision rejects a rotation menu");gConfigRevision--;
    bool rotated=RotateFenceBuilding(client,building,0,1);float actual[3];int entity=Resolve(gBuildings[building].ref);
    if(entity>0)GetEntPropVector(entity,Prop_Data,"m_angRotation",actual);
    Check(rotated && FloatAbs(gBuildings[building].angles[0]-NormalizeFenceAngle(before.angles[0]+5.0))<0.001
        && FloatAbs(NormalizeFenceAngle(actual[0]-gBuildings[building].angles[0]))<0.01
        && gBuildings[building].angles[1]==before.angles[1] && gBuildings[building].angles[2]==before.angles[2],
        "a real vertical rotation changes saved and actual pitch by exactly five degrees while preserving yaw and roll");
    Check(GetVectorDistance(gBuildings[building].placementAnchor,before.placementAnchor)<0.001
        && GetVectorDistance(gBuildings[building].origin,before.origin)<0.001
        && GetVectorDistance(gBuildings[building].meta.angleOffset,before.meta.angleOffset)<0.001,
        "rotation preserves the hit anchor, model origin and configured model angle offset");
    float rotatedAngles[3];rotatedAngles=gBuildings[building].angles;
    Check(ApplyBuildingLevel(building,false) && GetVectorDistance(gBuildings[building].angles,rotatedAngles)<0.001,
        "the level application path retains the saved defensive rotation instead of reapplying placement angles");
    float savedView[3],away[3];GetClientEyeAngles(client,savedView);away=savedView;away[1]=NormalizeFenceAngle(away[1]+180.0);
    TeleportEntity(client,NULL_VECTOR,away,NULL_VECTOR);
    Check(!BuildingInUseRange(client,building) && FenceRotationInRange(client,building),
        "rotation navigation fixture looks away from the thin face while retaining its original OBB interaction distance");
    for(int navigation=13;navigation<=15;navigation++)
    {
        DefenseMenuToken(client,building,navigation,token,sizeof(token));
        Check(ValidateDefenseMenuToken(client,token,decoded,operation) && decoded==building && operation==navigation,
            "selected defensive rotation can return and change axes through a bound one-shot nearby token without re-aiming");
        Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"rotation navigation cannot replay its consumed token");
    }
    DefenseMenuToken(client,building,6,token,sizeof(token));
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"initial vertical entry still requires an actual aimed defensive face");
    DefenseMenuToken(client,building,7,token,sizeof(token));
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"initial horizontal entry still requires an actual aimed defensive face");
    DefenseMenuToken(client,building,2,token,sizeof(token));
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"rotation continuation does not relax the upgrade action's actual aim requirement");
    DefenseMenuToken(client,building,4,token,sizeof(token));
    Check(!ValidateDefenseMenuToken(client,token,decoded,operation),"rotation continuation does not relax the dismantle action's actual aim requirement");
    TeleportEntity(client,NULL_VECTOR,savedView,NULL_VECTOR);
    gBuildings[building].kind=Kind_Turret;
    Check(!RotateFenceBuilding(client,building,1,1),"the same rotation action refuses every non-defensive building kind");gBuildings[building].kind=before.kind;
    // Restore only fields this test intentionally changes; do not roll UID or generation backwards.
    gBuildings[building].angles=before.angles;gBuildings[building].restoreMins=before.restoreMins;gBuildings[building].restoreMaxs=before.restoreMaxs;
    gBuildings[building].boundsKnown=before.boundsKnown;if(entity>0)TeleportEntity(entity,NULL_VECTOR,before.angles,NULL_VECTOR);
    gDefenseMenuNonce[client]=nonce+1;
}

bool tFenceRotationUIActive;
int tFenceRotationUISerial,tFenceRotationUIEpoch,tFenceRotationUIRevision,tFenceRotationUIBuilding=-1;
int tFenceRotationUIInventory[MATERIALS],tFenceRotationUICount,tFenceRotationUILimit;
float tFenceRotationUIHalfX,tFenceRotationUIHalfY;
float tFenceRotationUISourceFeet[3];
int tFenceRotationUIFrameAttempt;
Building tFenceRotationUIBefore;

void FenceRotationRayDiagnostic(int client,int building,const char[] stage)
{
    int body=Resolve(gBuildings[building].ref);float eye[3],view[3],direction[3],end[3],hitPoint[3],mins[3],maxs[3];
    GetClientEyePosition(client,eye);GetClientEyeAngles(client,view);GetAngleVectors(view,direction,NULL_VECTOR,NULL_VECTOR);
    for(int a=0;a<3;a++)end[a]=eye[a]+direction[a]*gUseDistance;
    Handle trace=TR_TraceRayFilterEx(eye,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
    int hit=TR_DidHit(trace) ? TR_GetEntityIndex(trace) : -1;TR_GetEndPosition(hitPoint,trace);
    GetEntPropVector(body,Prop_Send,"m_vecMins",mins);GetEntPropVector(body,Prop_Send,"m_vecMaxs",maxs);
    PrintToServer("[FenceRotationRay] stage=%s uid=%d body=%d hit=%d startsolid=%d allsolid=%d hit_distance=%.3f use_distance=%.3f eye=%.3f %.3f %.3f angles=%.3f %.3f %.3f local_min=%.3f %.3f %.3f local_max=%.3f %.3f %.3f solid_flags=%d collision_group=%d",
        stage,gBuildings[building].uid,body,hit,TR_StartSolid(trace),TR_AllSolid(trace),GetVectorDistance(eye,hitPoint),gUseDistance,
        eye[0],eye[1],eye[2],gBuildings[building].angles[0],gBuildings[building].angles[1],gBuildings[building].angles[2],
        mins[0],mins[1],mins[2],maxs[0],maxs[1],maxs[2],GetEntProp(body,Prop_Send,"m_usSolidFlags"),GetEntProp(body,Prop_Send,"m_CollisionGroup"));
    delete trace;
}

bool FenceRotationScanView(int client,int building,const float feet[3],int &views,int &rays)
{
    float hullMin[3]={-16.0,-16.0,0.0},hullMax[3]={16.0,16.0,72.0};
    if(!ClearHull(feet,hullMin,hullMax,client,MASK_PLAYERSOLID))return false;
    views++;int body=Resolve(gBuildings[building].ref);float low[3],high[3],eye[3];
    GetEntPropVector(body,Prop_Send,"m_vecMins",low);GetEntPropVector(body,Prop_Send,"m_vecMaxs",high);
    TeleportEntity(client,feet,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));GetClientEyePosition(client,eye);
    // The center of a wire barricade can be empty. Scan actual model-local
    // points, retain a real MASK_SOLID hit, then verify the production E ray.
    for(int x=0;x<7;x++)for(int y=0;y<7;y++)for(int z=0;z<5;z++)
    {
        float local[3],point[3],direction[3],end[3];
        local[0]=low[0]+(high[0]-low[0])*(0.05+float(x)*0.15);
        local[1]=low[1]+(high[1]-low[1])*(0.05+float(y)*0.15);
        local[2]=low[2]+(high[2]-low[2])*(0.1+float(z)*0.2);
        WorldPoint(local,gBuildings[building].origin,gBuildings[building].angles,point);
        MakeVectorFromPoints(eye,point,direction);if(GetVectorLength(direction)<0.01)continue;NormalizeVector(direction,direction);
        for(int a=0;a<3;a++)end[a]=eye[a]+direction[a]*gUseDistance;
        Handle trace=TR_TraceRayFilterEx(eye,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);rays++;
        bool realHit=TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace) && TR_GetEntityIndex(trace)==body;delete trace;
        if(!realHit)continue;
        V05AimAt(client,feet,point);
        if(BuildingInUseRange(client,building) && FenceRotationInRange(client,building))return true;
    }
    return false;
}

public void PrepareFenceRotationUIFrame(any serial)
{
    int client=GetClientFromSerial(serial),building=tFenceRotationUIBuilding;
    if(client<1 || !tV05RealActive || serial!=tFenceRotationUISerial || gEpoch!=tFenceRotationUIEpoch
        || gConfigRevision!=tFenceRotationUIRevision || building<0 || gBuildings[building].uid!=tFenceRotationUIBefore.uid
        || !CanContinueFenceRotationMenu(client,building))return;
    int body=Resolve(gBuildings[building].ref);float mins[3],maxs[3],center[3];
    ModelWorldBounds(body,gBuildings[building].origin,gBuildings[building].angles,mins,maxs);
    for(int a=0;a<3;a++)center[a]=(mins[a]+maxs[a])*0.5;
    V05AimAt(client,tFenceRotationUISourceFeet,center);bool aimed=BuildingInUseRange(client,building) && FenceRotationInRange(client,building);
    FenceRotationRayDiagnostic(client,building,tFenceRotationUIFrameAttempt==0 ? "first_frame_center" : "second_frame_center");
    if(!aimed && tFenceRotationUIFrameAttempt==0)
    {
        tFenceRotationUIFrameAttempt++;RequestFrame(PrepareFenceRotationUIFrame,serial);
        PrintToServer("[FenceRotationUI] WAIT second frame before scanning newly spawned physics body uid=%d",gBuildings[building].uid);return;
    }
    int views,rays;
    if(!aimed)aimed=FenceRotationScanView(client,building,tFenceRotationUISourceFeet,views,rays);
    float halfX=(maxs[0]-mins[0])*0.5,halfY=(maxs[1]-mins[1])*0.5;
    float radius=(halfX>halfY ? halfX : halfY)+24.0;
    for(int ring=0;!aimed && ring<3;ring++)for(int direction=0;!aimed && direction<16;direction++)
    {
        float candidate[3];candidate=tFenceRotationUISourceFeet;float angle=DegToRad(float(direction)*22.5),distance=radius+float(ring)*32.0;
        candidate[0]=center[0]+Cosine(angle)*distance;candidate[1]=center[1]+Sine(angle)*distance;
        aimed=FenceRotationScanView(client,building,candidate,views,rays);
    }
    PrintToServer("[FenceRotationUI] delayed_scan uid=%d frame=%d clear_views=%d rays=%d aimed=%d ring_radius=%.3f",gBuildings[building].uid,tFenceRotationUIFrameAttempt+1,views,rays,aimed,radius);
    FenceRotationRayDiagnostic(client,building,aimed ? "ready_actual_ray" : "failed_actual_ray");
    if(!aimed)
    {tSkipped++;PrintToServer("[ShelterTest] SKIP rotation UI no actual collision-surface E ray found after two frames and clear-hull surface sampling");return;}
    tFenceRotationUIActive=true;CancelClientMenu(client);gNextUse[client]=0.0;
    Check(CanManageBuilding(client,building) && BuildingInUseRange(client,building) && FenceRotationInRange(client,building),
        "rotation UI begins with the real owner aiming at their defensive structure inside its actual OBB use distance");
    PrintToServer("[FenceRotationUI] READY uid=%d building=%d owner=%d core=%d initial_angles=%.3f %.3f %.3f; client +use/-use, then 3,1,1,2,3,4,1,1; cleanup uses v05_real_cleanup",
        gBuildings[building].uid,building,gBuildings[building].ownerSerial,gBuildings[building].buildCore,gBuildings[building].angles[0],gBuildings[building].angles[1],gBuildings[building].angles[2]);
}

// Real Steam client input drives every rotation after this setup. This fixture
// makes one production build, then snapshots its paid balance and identity.
void SetupFenceRotationUI(int client)
{
    tFenceRotationUIActive=false;
    if(client<1 || client!=V05RealClient() || !V05BeginRealFixture(client))return;
    int recipe=-1;
    for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Fence){recipe=r;break;}
    float feet[3],point[3],normal[3];int hit,core;
    if(recipe<0 || !V05FindRealPlacement(client,recipe,0,feet,point,normal,hit,core))
    {tSkipped++;PrintToServer("[ShelterTest] SKIP rotation UI could not find a legal real interior placement for a defensive recipe");return;}
    SetEntityMoveType(client,MOVETYPE_WALK);V05EnsureRecipeMaterials(client,recipe);
    int building=V05BuildFromRealAim(client,recipe,core,"rotation UI creates exactly one real defensive structure and pays its recipe once");
    if(building<0 || !CanContinueFenceRotationMenu(client,building))return;
    RefreshBuildAreas();tFenceRotationUIBefore=gBuildings[building];tFenceRotationUIBuilding=building;
    tFenceRotationUISerial=GetClientSerial(client);tFenceRotationUIEpoch=gEpoch;tFenceRotationUIRevision=gConfigRevision;
    tFenceRotationUIInventory=gPlayerInventory[client];tFenceRotationUICount=gBuildAreas[core].count;tFenceRotationUILimit=gBuildAreas[core].limit;
    tFenceRotationUIHalfX=gBuildAreas[core].halfX;tFenceRotationUIHalfY=gBuildAreas[core].halfY;
    tFenceRotationUISourceFeet=feet;tFenceRotationUIFrameAttempt=0;RequestFrame(PrepareFenceRotationUIFrame,tFenceRotationUISerial);
    PrintToServer("[FenceRotationUI] SCHEDULED uid=%d; wait for next-frame READY before actual client +use",gBuildings[building].uid);
}

void VerifyFenceRotationUI(int client)
{
    int b=tFenceRotationUIBuilding;
    if(!tFenceRotationUIActive || client<1 || !LiveHuman(client) || IsFakeClient(client)
        || GetClientSerial(client)!=tFenceRotationUISerial || gEpoch!=tFenceRotationUIEpoch || gConfigRevision!=tFenceRotationUIRevision
        || b<0 || !gBuildings[b].used || !gBuildings[b].alive || gBuildings[b].uid!=tFenceRotationUIBefore.uid || Resolve(gBuildings[b].ref)<1)
    {Check(false,"rotation UI verification requires its original living real actor and unchanged built UID/session");return;}
    float expected[3],actual[3];expected=tFenceRotationUIBefore.angles;
    expected[0]=NormalizeFenceAngle(expected[0]+5.0);expected[1]=NormalizeFenceAngle(expected[1]+10.0);
    GetEntPropVector(Resolve(gBuildings[b].ref),Prop_Data,"m_angRotation",actual);
    bool anglesMatch=true,paidOnce=true;
    for(int a=0;a<3;a++)if(FloatAbs(NormalizeFenceAngle(gBuildings[b].angles[a]-expected[a]))>0.01
        || FloatAbs(NormalizeFenceAngle(actual[a]-expected[a]))>0.01)anglesMatch=false;
    for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=tFenceRotationUIInventory[m])paidOnce=false;
    Check(anglesMatch,"actual client vertical +5,+5,-5 and horizontal +5,+5 change saved/body pitch +5 and yaw +10 while retaining roll");
    Check(GetClientMenu(client)==MenuSource_Normal,"the continuous rotation menu remains open after the last actual client button");
    Check(paidOnce,"five actual menu rotation steps do not deduct any material after the single production build");
    Check(gBuildings[b].ownerSerial==tFenceRotationUIBefore.ownerSerial && StrEqual(gBuildings[b].ownerKey,tFenceRotationUIBefore.ownerKey)
        && gBuildings[b].generation==tFenceRotationUIBefore.generation && gBuildings[b].level==tFenceRotationUIBefore.level
        && gBuildings[b].buildCore==tFenceRotationUIBefore.buildCore && gBuildings[b].powerCore==tFenceRotationUIBefore.powerCore
        && CanManageBuilding(client,b) && FenceRotationInRange(client,b),
        "rotation retains the original actor ownership, generation, level, fixed build/power base and nearby management permission");
    Check(GetVectorDistance(gBuildings[b].origin,tFenceRotationUIBefore.origin)<0.01
        && GetVectorDistance(gBuildings[b].placementAnchor,tFenceRotationUIBefore.placementAnchor)<0.01
        && GetVectorDistance(gBuildings[b].meta.angleOffset,tFenceRotationUIBefore.meta.angleOffset)<0.01,
        "real rotation buttons retain the model origin, ray anchor and configured model angle offset");
    RefreshBuildAreas();int core=gBuildings[b].buildCore;
    Check(core>=0 && gBuildAreas[core].count==tFenceRotationUICount && gBuildAreas[core].limit==tFenceRotationUILimit
        && FloatAbs(gBuildAreas[core].halfX-tFenceRotationUIHalfX)<0.01 && FloatAbs(gBuildAreas[core].halfY-tFenceRotationUIHalfY)<0.01,
        "rotating an existing defensive structure changes neither the fixed build range nor the occupied quota");
    PrintToServer("[FenceRotationUI] VERIFY uid=%d actor_serial=%d saved=%.3f %.3f %.3f actual=%.3f %.3f %.3f menu=%d paid_once=%d",
        gBuildings[b].uid,GetClientSerial(client),gBuildings[b].angles[0],gBuildings[b].angles[1],gBuildings[b].angles[2],
        actual[0],actual[1],actual[2],GetClientMenu(client),paidOnce);
}
