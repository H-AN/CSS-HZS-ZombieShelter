// Test binary only. Reuse a living real client; never add or respawn clients.
bool tV05RealActive,tV05SavedPower,tV05SavedExposure,tV05Parked[MAX_NPCS];
int tV05Serial,tV05Epoch,tV05Revision,tV05StartUID,tV05Recipe=-1,tV05Core=-1;
int tV05SavedInventory[MATERIALS],tV05SavedDamage,tV05SavedHP,tV05UIInventory[MATERIALS],tV05UICount,tV05UIUID;
float tV05SavedPosition[3],tV05SavedAngles[3],tV05SavedVelocity[3],tV05Feet[3],tV05Point[3];
float tV05DeadlineRemaining,tV05AdmissionsRemaining,tV05NPCPositions[MAX_NPCS][3],tV05NPCAngles[MAX_NPCS][3];
MoveType tV05SavedMovement;
NPC tV05SavedNPCs[MAX_NPCS];
int tV05RedBefore,tV05RedDeliveriesBefore;

int V05RealClient()
{
    int client=TestHuman();
    if(client<1 || !Running())
    {tSkipped++;PrintToServer("[ShelterTest] SKIP v05 real-client fixture requires an existing living real client and running campaign");return 0;}
    if(tV05RealActive && (GetClientSerial(client)!=tV05Serial || gEpoch!=tV05Epoch || gConfigRevision!=tV05Revision))
    {tSkipped++;PrintToServer("[ShelterTest] SKIP v05 fixture session changed; stop this isolated session rather than reusing its saved state");return 0;}
    return client;
}

bool V05BeginRealFixture(int client)
{
    if(tV05RealActive)return true;
    tV05Serial=GetClientSerial(client);tV05Epoch=gEpoch;tV05Revision=gConfigRevision;tV05StartUID=gUID;
    GetClientAbsOrigin(client,tV05SavedPosition);GetClientEyeAngles(client,tV05SavedAngles);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",tV05SavedVelocity);
    tV05SavedHP=GetClientHealth(client);tV05SavedDamage=GetEntProp(client,Prop_Data,"m_takedamage");tV05SavedMovement=GetEntityMoveType(client);
    tV05SavedInventory=gPlayerInventory[client];tV05SavedPower=gPowerEnabled;tV05SavedExposure=gExposureEnabled;
    float now=GetGameTime();tV05DeadlineRemaining=gDeadline-now;tV05AdmissionsRemaining=gNextAdmissions-now;
    gDeadline=now+900.0;gNextAdmissions=now+900.0;gPowerEnabled=false;gExposureEnabled=false;
    SetEntProp(client,Prop_Data,"m_takedamage",0,1);SetEntityMoveType(client,MOVETYPE_WALK);
    for(int n=0;n<MAX_NPCS;n++)
    {
        tV05Parked[n]=false;int entity=Resolve(gNPCs[n].ref);
        if(entity<0 || !gNPCs[n].used || !gNPCs[n].ready || gNPCs[n].dead || !gNPCs[n].active || gNPCs[n].boss>=0)continue;
        tV05SavedNPCs[n]=gNPCs[n];GetEntPropVector(entity,Prop_Data,"m_vecOrigin",tV05NPCPositions[n]);GetEntPropVector(entity,Prop_Data,"m_angRotation",tV05NPCAngles[n]);
        tV05Parked[n]=true;ParkNPC(n);
    }
    tV05RealActive=true;RecalculatePower();
    PrintToServer("[V05Real] begin serial=%d epoch=%d revision=%d; existing active ordinary NPCs parked temporarily; no NPC/client created",tV05Serial,tV05Epoch,tV05Revision);
    return true;
}

void V05AimAt(int client,const float feet[3],const float point[3])
{
    float zero[3],eye[3],direction[3],angles[3];TeleportEntity(client,feet,NULL_VECTOR,zero);GetClientEyePosition(client,eye);
    MakeVectorFromPoints(eye,point,direction);GetVectorAngles(direction,angles);TeleportEntity(client,NULL_VECTOR,angles,zero);
}

bool V05TraceSurface(int client,const float start[3],const float end[3],float point[3],float normal[3],int &hit)
{
    Handle trace=TR_TraceRayFilterEx(start,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
    bool valid=TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);
    if(valid){TR_GetEndPosition(point,trace);TR_GetPlaneNormal(trace,normal);hit=TR_GetEntityIndex(trace);}
    delete trace;return valid;
}

bool V05AimMatches(int client,int recipe,const float feet[3],const float point[3],int core)
{
    float mins[3]={-16.0,-16.0,0.0},maxs[3]={16.0,16.0,72.0};
    if(!ClearHull(feet,mins,maxs,client,MASK_PLAYERSOLID))return false;
    V05AimAt(client,feet,point);float anchor[3],origin[3],angles[3];
    return PlacementForRecipe(client,recipe,anchor,origin,angles,false) && BuildCoreAt(anchor)==core && GetVectorDistance(anchor,point)<12.0;
}

// Select real collision surfaces, never substitute a synthetic hull or floor.
bool V05FindRealPlacement(int client,int recipe,int scenario,float feet[3],float point[3],float normal[3],int &hit,int &core)
{
    for(int c=0;c<gCoreCount;c++)
    {
        int base=gCores[c].building,body=base>=0 ? Resolve(gBuildings[base].ref) : -1;
        if(body<0 || !gBuildings[base].alive)continue;
        float boundsMin[3],boundsMax[3];GetEntPropVector(body,Prop_Send,"m_vecMins",boundsMin);GetEntPropVector(body,Prop_Send,"m_vecMaxs",boundsMax);
        if(scenario==0)
        {
            for(int p=0;p<gCores[c].pointCount;p++)for(int direction=0;direction<8;direction++)
            {
                feet=gCorePoints[c][p];feet[2]+=2.0;
                float start[3],end[3],angle=DegToRad(float(direction)*45.0);start=feet;
                start[0]+=Cosine(angle)*96.0;start[1]+=Sine(angle)*96.0;start[2]+=48.0;end=start;end[2]-=176.0;
                if(!V05TraceSurface(client,start,end,point,normal,hit) || normal[2]<0.8 || BuildCoreAt(point)!=c)continue;
                float local[3];LocalPoint(point,gBuildings[base].origin,gBuildings[base].angles,local);
                if(local[0]<=boundsMin[0]+8.0 || local[0]>=boundsMax[0]-8.0 || local[1]<=boundsMin[1]+8.0 || local[1]>=boundsMax[1]-8.0
                    || local[2]>80.0 || !V05AimMatches(client,recipe,feet,point,c))continue;
                core=c;return true;
            }
        }
        else
        {
            float halfX=gBuildings[base].buildHalfExtents[0],halfY=gBuildings[base].buildHalfExtents[1];
            for(int x=-8;x<=8;x++)for(int y=-8;y<=8;y++)
            {
                float local[3],start[3],end[3];local[0]=float(x)*halfX/9.0;local[1]=float(y)*halfY/9.0;
                local[2]=scenario==1 ? boundsMax[2]+128.0 : 400.0;WorldPoint(local,gCores[c].origin,gCores[c].angles,start);end=start;end[2]=gCores[c].origin[2]-96.0;
                if(!V05TraceSurface(client,start,end,point,normal,hit) || BuildCoreAt(point)!=c)continue;
                if(scenario==1 && (hit!=body || normal[2]<0.8 || point[2]<gCores[c].origin[2]+80.0))continue;
                if(scenario==2 && (hit!=0 || normal[2]<0.25 || normal[2]>=0.93))continue;
                feet=point;feet[0]-=96.0;feet[2]+=96.0;
                if(!V05AimMatches(client,recipe,feet,point,c))continue;
                core=c;return true;
            }
        }
    }
    return false;
}

void V05EnsureRecipeMaterials(int client,int recipe,int copies=1)
{
    for(int m=0;m<gMaterialCount;m++)
    {int amount=gRecipes[recipe].cost[m]*copies;if(gPlayerInventory[client][m]<amount)gPlayerInventory[client][m]=amount;}
}

int V05NewestOwnedBuilding(int client,int afterUID)
{
    int newest=-1;
    for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].ownerSerial==GetClientSerial(client)
        && gBuildings[b].uid>afterUID && (newest<0 || gBuildings[b].uid>gBuildings[newest].uid))newest=b;
    return newest;
}

int V05BuildFromRealAim(int client,int recipe,int core,const char[] scenario)
{
    V05EnsureRecipeMaterials(client,recipe);int before[MATERIALS];before=gPlayerInventory[client];int count=SummonCount(),uid=gUID;
    float anchor[3],origin[3],angles[3];bool placement=PlacementForRecipe(client,recipe,anchor,origin,angles,false);
    bool built=BuildRecipe(client,recipe,core);int b=V05NewestOwnedBuilding(client,uid);
    bool paid=true;for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=before[m]-gRecipes[recipe].cost[m])paid=false;
    Check(placement && built && b>=0 && Resolve(gBuildings[b].ref)>0 && SummonCount()==count+1 && paid,
        scenario);
    if(b>=0)
    {
        Check(gBuildings[b].configRevision==gConfigRevision && gBuildings[b].buildCore==core
            && GetVectorDistance(gBuildings[b].placementAnchor,anchor)<0.1 && GetVectorDistance(gBuildings[b].origin,origin)<0.1,
            "real construction commits the ray anchor, final model origin, fixed base and immutable configuration revision");
        PrintToServer("[V05RealBuild] scenario=%s uid=%d ref=%d core=%d anchor=%.3f %.3f %.3f origin=%.3f %.3f %.3f",scenario,gBuildings[b].uid,gBuildings[b].ref,core,anchor[0],anchor[1],anchor[2],origin[0],origin[1],origin[2]);
    }
    return b;
}

void V05RestoreBuiltBody(int b)
{
    if(b<0)return;Building saved;saved=gBuildings[b];saved.hp=saved.maxHP-17;if(saved.hp<1)saved.hp=1;gBuildings[b].hp=saved.hp;
    int oldRef=gBuildings[b].ref;gBuildings[b].ref=INVALID_ENT_REFERENCE;RemoveBuildingEffect(b);RemoveOwnedRef(oldRef);
    bool restored=RestoreSavedBuilding(b);int entity=Resolve(gBuildings[b].ref),r,g,blue,a;
    if(entity>0)GetEntityRenderColor(entity,r,g,blue,a);
    Check(restored && entity>0 && gBuildings[b].ref!=oldRef && gBuildings[b].uid==saved.uid && gBuildings[b].hp==saved.hp
        && GetVectorDistance(gBuildings[b].origin,saved.origin)<0.01 && GetVectorDistance(gBuildings[b].placementAnchor,saved.placementAnchor)<0.01
        && gBuildings[b].ownerSerial==saved.ownerSerial && gBuildings[b].configRevision==saved.configRevision,
        "a real ray-built body restores through the production saved-building path with HP, UID, ownership and transforms intact");
    Check(entity>0 && r==saved.meta.render[0] && g==saved.meta.render[1] && blue==saved.meta.render[2] && a==saved.meta.render[3],
        "restored ray-built entity retains its actual saved render color");
}

void V05PrepareRealUI(int client)
{
    if(!V05BeginRealFixture(client))return;
    SetEntityMoveType(client,MOVETYPE_WALK);
    tV05Recipe=-1;for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Turret){tV05Recipe=r;break;}
    float normal[3];int hit;
    if(tV05Recipe<0 || !V05FindRealPlacement(client,tV05Recipe,0,tV05Feet,tV05Point,normal,hit,tV05Core))
    {tSkipped++;PrintToServer("[ShelterTest] SKIP v05 UI: no real interior surface with a clear human viewpoint was found");return;}
    V05EnsureRecipeMaterials(client,tV05Recipe,2);tV05UIInventory=gPlayerInventory[client];tV05UICount=SummonCount();tV05UIUID=gUID;
    ShowBuildCategories(client);
    PrintToServer("[V05RealUI] READY serial=%d core=%d recipe=%s hit=%d normal=%.3f %.3f %.3f feet=%.1f %.1f %.1f target=%.1f %.1f %.1f; menuselect 1 four times: category, variant, details continue, confirm",tV05Serial,tV05Core,gRecipes[tV05Recipe].id,hit,normal[0],normal[1],normal[2],tV05Feet[0],tV05Feet[1],tV05Feet[2],tV05Point[0],tV05Point[1],tV05Point[2]);
}

void V05VerifyRealUI(int client)
{
    if(!tV05RealActive || tV05Recipe<0){tSkipped++;PrintToServer("[ShelterTest] SKIP UI verify without v05_ui_setup");return;}
    int b=V05NewestOwnedBuilding(client,tV05UIUID);bool paid=true;
    for(int m=0;m<gMaterialCount;m++)if(gPlayerInventory[client][m]!=tV05UIInventory[m]-gRecipes[tV05Recipe].cost[m])paid=false;
    Check(b>=0 && Resolve(gBuildings[b].ref)>0 && SummonCount()==tV05UICount+1 && paid
        && gBuildings[b].buildCore==tV05Core && StrEqual(gBuildings[b].type,gRecipes[tV05Recipe].type),
        "real numeric category/variant/details/confirmation input builds exactly one selected variant and charges its exact materials once");
    if(b>=0)V05RestoreBuiltBody(b);
    PrintToServer("[V05RealUI] VERIFY real_serial=%d new_building=%d uid=%d count_delta=%d paid_exactly_once=%d",GetClientSerial(client),b,b>=0 ? gBuildings[b].uid : 0,SummonCount()-tV05UICount,paid);
}

void V05TestRealConstruction(int client)
{
    if(!V05BeginRealFixture(client))return;
    SetEntityMoveType(client,MOVETYPE_WALK);
    int recipe=-1;for(int r=0;r<gRecipeCount;r++)if(gRecipes[r].kind==Kind_Turret){recipe=r;break;}if(recipe<0)return;
    float feet[3],point[3],normal[3];int hit,core;
    if(V05FindRealPlacement(client,recipe,0,feet,point,normal,hit,core))
    {
        int first=V05BuildFromRealAim(client,recipe,core,"a living real client builds inside the base model footprint from an actual eye ray");
        // Repeating the same physical aim now hits the existing building's body.
        V05AimAt(client,feet,point);int second=V05BuildFromRealAim(client,recipe,core,"real construction permits a second building at the same aim despite an existing collider");
        if(first>=0 && second>=0)
        {
            float loA[3],hiA[3],loB[3],hiB[3];ModelWorldBounds(Resolve(gBuildings[first].ref),gBuildings[first].origin,gBuildings[first].angles,loA,hiA);
            ModelWorldBounds(Resolve(gBuildings[second].ref),gBuildings[second].origin,gBuildings[second].angles,loB,hiB);bool overlap=true;
            for(int axis=0;axis<3;axis++)if(hiA[axis]<loB[axis] || hiB[axis]<loA[axis])overlap=false;
            Check(overlap,"two real ray-built model bounds overlap without the old global hull rejection");
        }
        V05RestoreBuiltBody(first);
    }
    else {tSkipped++;PrintToServer("[ShelterTest] SKIP real interior build: no suitable collision surface/viewpoint found");}
    if(V05FindRealPlacement(client,recipe,1,feet,point,normal,hit,core))
    {PrintToServer("[V05RealSurface] roof hit=%d normal_z=%.3f anchor_z=%.3f",hit,normal[2],point[2]);V05BuildFromRealAim(client,recipe,core,"a real eye ray builds on the actual base entity roof collider");}
    else {tSkipped++;PrintToServer("[ShelterTest] SKIP real roof build: no actual base roof ray/viewpoint found");}
    if(V05FindRealPlacement(client,recipe,2,feet,point,normal,hit,core))
    {PrintToServer("[V05RealSurface] slope hit=%d normal_z=%.3f anchor=%.1f %.1f %.1f",hit,normal[2],point[0],point[1],point[2]);V05BuildFromRealAim(client,recipe,core,"a real eye ray builds on existing map slope below the previous normal_z 0.93 cutoff");}
    else {tSkipped++;PrintToServer("[ShelterTest] SKIP real slope build: no suitable existing map slope inside current rectangles was found; no synthetic slope substituted");}
}

void V05RealCleanup(int client)
{
    if(!tV05RealActive)return;
    int removed;
    for(int b=gCoreCount;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].ownerSerial==tV05Serial && gBuildings[b].uid>tV05StartUID)
    {RemoveBuildingEffect(b);int ref=gBuildings[b].ref;gBuildings[b].ref=INVALID_ENT_REFERENCE;gBuildings[b].used=false;gBuildings[b].alive=false;RemoveOwnedRef(ref);removed++;}
    for(int n=0;n<MAX_NPCS;n++)if(tV05Parked[n] && gNPCs[n].ref==tV05SavedNPCs[n].ref && !gNPCs[n].dead)
    {
        int entity=Resolve(gNPCs[n].ref);gNPCs[n]=tV05SavedNPCs[n];
        if(entity>0){TeleportEntity(entity,tV05NPCPositions[n],tV05NPCAngles[n],view_as<float>({0.0,0.0,0.0}));RestoreNPCBody(n);}
        tV05Parked[n]=false;
    }
    gPlayerInventory[client]=tV05SavedInventory;SetEntProp(client,Prop_Data,"m_takedamage",tV05SavedDamage,1);SetEntityHealth(client,tV05SavedHP);SetEntityMoveType(client,tV05SavedMovement);
    TeleportEntity(client,tV05SavedPosition,tV05SavedAngles,tV05SavedVelocity);
    gPowerEnabled=tV05SavedPower;gExposureEnabled=tV05SavedExposure;gDeadline=GetGameTime()+tV05DeadlineRemaining;
    gNextAdmissions=GetGameTime()+(tV05AdmissionsRemaining>0.0 ? tV05AdmissionsRemaining : 0.0);
    gBuildMenuNonce[client]++;CancelClientMenu(client);tV05RealActive=false;MarkBuildAreasDirty();RecalculatePower();
    PrintToServer("[V05Real] CLEANUP removed_only_fixture_owner_new_buildings=%d original_inventory_position_damage_flags_and_parked_NPCs_restored=1",removed);
}

bool V05RealClientCommand(const char[] which)
{
    bool known=StrEqual(which,"v05_ui_setup") || StrEqual(which,"v05_ui_verify") || StrEqual(which,"v05_single") || StrEqual(which,"v05_real_cleanup")
        || StrEqual(which,"v05_red_inside") || StrEqual(which,"v05_red_verify_inside") || StrEqual(which,"v05_red_outside") || StrEqual(which,"v05_red_verify_outside")
        || StrEqual(which,"v05_visual_overview");
    if(!known)return false;int client=V05RealClient();if(client<1)return true;
    if(StrEqual(which,"v05_ui_setup"))V05PrepareRealUI(client);
    else if(StrEqual(which,"v05_ui_verify"))V05VerifyRealUI(client);
    else if(StrEqual(which,"v05_single"))V05TestRealConstruction(client);
    else if(StrEqual(which,"v05_real_cleanup"))V05RealCleanup(client);
    else if(!tV05RealActive || tV05Core<0){tSkipped++;PrintToServer("[ShelterTest] SKIP red fixture requires successful v05_ui_setup");}
    else if(StrEqual(which,"v05_red_inside"))
    {
        SetEntityMoveType(client,MOVETYPE_WALK);
        V05AimAt(client,tV05Feet,tV05Point);tV05RedBefore=gRedConstructed;tV05RedDeliveriesBefore=gRedDeliveries;gNextBuildDisplay=0.0;DrawBuildZones();
        PrintToServer("[V05RealRed] INSIDE begin constructed=%d deliveries=%d core=%d real=1; wait at least 1.3 seconds",tV05RedBefore,tV05RedDeliveriesBefore,tV05Core);
    }
    else if(StrEqual(which,"v05_red_verify_inside"))
    {
        Check(gRedConstructed>tV05RedBefore && gRedDeliveries>tV05RedDeliveriesBefore,"one living real client inside the build rectangle triggers actual red beam construction and delivery");
        PrintToServer("[V05RealRed] INSIDE constructed_delta=%d line_delivery_delta=%d (not UDP packets)",gRedConstructed-tV05RedBefore,gRedDeliveries-tV05RedDeliveriesBefore);
    }
    else if(StrEqual(which,"v05_red_outside"))
    {
        float outside[3];outside=tV05Feet;outside[2]=gCores[tV05Core].buildCenter[2]+gCores[tV05Core].buildZMax+96.0;
        SetEntityMoveType(client,MOVETYPE_NOCLIP);
        TeleportEntity(client,outside,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));tV05RedBefore=gRedConstructed;tV05RedDeliveriesBefore=gRedDeliveries;gNextBuildDisplay=0.0;DrawBuildZones();
        PrintToServer("[V05RealRed] OUTSIDE begin constructed=%d deliveries=%d z=%.1f; wait at least 1.3 seconds",tV05RedBefore,tV05RedDeliveriesBefore,outside[2]);
    }
    else if(StrEqual(which,"v05_red_verify_outside"))
    {
        Check(gRedConstructed==tV05RedBefore && gRedDeliveries==tV05RedDeliveriesBefore,"a living real client above build height does not trigger or receive further red beams");
        PrintToServer("[V05RealRed] OUTSIDE constructed_delta=%d line_delivery_delta=%d",gRedConstructed-tV05RedBefore,gRedDeliveries-tV05RedDeliveriesBefore);
        // Keep this actual outside viewpoint for the following screenshot.
    }
    else if(StrEqual(which,"v05_visual_overview"))
    {
        CancelClientMenu(client);SetEntityMoveType(client,MOVETYPE_NOCLIP);
        float position[3],target[3];position=gCores[tV05Core].buildCenter;target=position;
        position[2]+=gCores[tV05Core].buildZMax-8.0;target[2]+=4.0;
        V05AimAt(client,position,target);gNextBuildDisplay=0.0;DrawBuildZones();DrawSafeZones();
        float actual[3];GetClientAbsOrigin(client,actual);
        Check(BuildAreaContains(tV05Core,actual) && GetEntityMoveType(client)==MOVETYPE_NOCLIP,
            "real overview camera remains inside build height and uses explicit noclip without changing red-line height");
        PrintToServer("[V05RealOverview] real_serial=%d core=%d feet=%.1f %.1f %.1f inside=1; looking down at fixed center; original red/green line heights retained",
            GetClientSerial(client),tV05Core,actual[0],actual[1],actual[2]);
    }
    return true;
}
