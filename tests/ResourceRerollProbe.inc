// Isolated resource cycling fixture. It restores config, node, inventory,
// material definitions and client transform; only test bodies are replaced.
void RerollSetWeight(const char[] path,const char[] material,const char[] value)
{
    gCfg[C_Resources].Rewind();char parts[5][64];int count=ExplodeString(path,"/",parts,sizeof(parts),sizeof(parts[]));
    for(int i=0;i<count;i++)gCfg[C_Resources].JumpToKey(parts[i],true);
    gCfg[C_Resources].SetString(material,value);
}

void RerollForce(const char[] pool,int material,bool regrow=true)
{
    char path[160];Format(path,sizeof(path),"Pools/%s/%s",pool,regrow ? "RegrowWeights" : "Weights");
    for(int m=0;m<MATERIALS;m++)RerollSetWeight(path,gMaterials[m].id,m==material ? "1" : "0");
}

void RerollHit(int client,int node)
{
    gNextGather[client]=0.0;
    SDKHooks_TakeDamage(Resolve(gNodes[node].ref),client,client,1.0,DMG_BULLET|DMG_NEVERGIB,_,_,_,false);
}

bool RerollModelMatches(int entity,int material)
{
    char model[PLATFORM_MAX_PATH];GetEntPropString(entity,Prop_Data,"m_ModelName",model,sizeof(model));
    return StrEqual(model,gMaterials[material].model)
        && FloatAbs(GetEntPropFloat(entity,Prop_Send,"m_flModelScale")-gMaterials[material].scale)<0.001;
}

// Compare the switched body's actual engine collision with a fresh same-model
// reference. Multiple rays through the shape catch a stale previous model hull.
void RerollCheckCollision(int node)
{
    int entity=Resolve(gNodes[node].ref),material=gNodes[node].material;
    int reference=CreateBody(gMaterials[material].model,gNodes[node].origin,gNodes[node].angles,"hzsh_reroll_reference",true);
    Check(reference>0,"resource collision comparison creates its temporary reference body");
    if(reference<0)return;
    float unscaledLow[3],unscaledHigh[3],scaledLow[3],scaledHigh[3];
    GetEntPropVector(reference,Prop_Send,"m_vecMins",unscaledLow);GetEntPropVector(reference,Prop_Send,"m_vecMaxs",unscaledHigh);
    bool nativeScale=ApplyResourceModel(node,reference),scaled=true;
    GetEntPropVector(reference,Prop_Send,"m_vecMins",scaledLow);GetEntPropVector(reference,Prop_Send,"m_vecMaxs",scaledHigh);
    for(int axis=0;axis<3;axis++)if(FloatAbs(scaledLow[axis]-unscaledLow[axis]*gMaterials[material].scale)>0.01
        || FloatAbs(scaledHigh[axis]-unscaledHigh[axis]*gMaterials[material].scale)>0.01)scaled=false;
    PrintToServer("[ResourceReroll] scale_input=%d scaled_bounds=%d",nativeScale,scaled);
    Check(scaled,"resource occupancy bounds are refreshed to the configured model scale");
    float low[3],high[3],freshLow[3],freshHigh[3];
    ModelWorldBounds(entity,gNodes[node].origin,gNodes[node].angles,low,high);
    ModelWorldBounds(reference,gNodes[node].origin,gNodes[node].angles,freshLow,freshHigh);
    bool bounds=true;for(int a=0;a<3;a++)if(FloatAbs(low[a]-freshLow[a])>0.01 || FloatAbs(high[a]-freshHigh[a])>0.01)bounds=false;
    PrintToServer("[ResourceReroll] bounds material=%s scale=%.3f actual=(%.3f %.3f %.3f)-(%.3f %.3f %.3f) fresh=(%.3f %.3f %.3f)-(%.3f %.3f %.3f)",
        gMaterials[material].id,gMaterials[material].scale,low[0],low[1],low[2],high[0],high[1],high[2],freshLow[0],freshLow[1],freshLow[2],freshHigh[0],freshHigh[1],freshHigh[2]);
    Check(bounds,"changing resource model updates occupancy bounds to those of a freshly spawned model");
    int tested,hits,mismatches;
    for(int axis=0;axis<3;axis++)for(int u=1;u<6;u++)for(int v=1;v<6;v++)
    {
        int other=(axis+1)%3,third=(axis+2)%3;float start[3],end[3];
        start[axis]=freshLow[axis]-32.0;end[axis]=freshHigh[axis]+32.0;
        start[other]=freshLow[other]+(freshHigh[other]-freshLow[other])*float(u)/6.0;end[other]=start[other];
        start[third]=freshLow[third]+(freshHigh[third]-freshLow[third])*float(v)/6.0;end[third]=start[third];
        Handle actual=TR_ClipRayToEntityEx(start,end,MASK_SOLID,RayType_EndPoint,entity);
        Handle expected=TR_ClipRayToEntityEx(start,end,MASK_SOLID,RayType_EndPoint,reference);
        bool actualHit=TR_DidHit(actual),expectedHit=TR_DidHit(expected);tested++;if(expectedHit)hits++;
        if(actualHit!=expectedHit || (actualHit && FloatAbs(TR_GetFraction(actual)-TR_GetFraction(expected))>0.01))mismatches++;
        delete actual;delete expected;
    }
    PrintToServer("[ResourceReroll] collision material=%s rays=%d reference_hits=%d mismatches=%d",gMaterials[material].id,tested,hits,mismatches);
    Check(hits>0 && mismatches==0,"switched resource engine collision matches the new model on all reference rays");
    RemoveEntity(reference);
}

void TestResourceReroll(int client)
{
    Check(Running() && LiveHuman(client) && gNodeCount>0,"resource reroll fixture has a running world and living collector");
    if(!Running() || !LiveHuman(client) || gNodeCount<1)return;
    int knife=GetPlayerWeaponSlot(client,2);Check(knife>MaxClients,"resource reroll fixture uses the player's existing knife");
    if(knife<=MaxClients)return;
    KeyValues original=gCfg[C_Resources];original.Rewind();gCfg[C_Resources]=new KeyValues("ShelterResources");KvCopySubkeys(original,gCfg[C_Resources]);
    ResourceNode saved;saved=gNodes[0];MaterialDef gold;gold=gMaterials[4];int nodeCount=gNodeCount;gNodeCount=1;
    int personal[MATERIALS];personal=gPlayerInventory[client];int cap=gInventoryCap,weapon=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon");
    float origin[3],angles[3],velocity[3],cooldown=gNextGather[client];
    GetClientAbsOrigin(client,origin);GetClientEyeAngles(client,angles);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",velocity);
    gInventoryCap=0;SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",knife);
    for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=0;
    RemoveOwnedRef(gNodes[0].ref);gNodes[0].ref=INVALID_ENT_REFERENCE;
    gNodes[0].material=0;gNodes[0].growing=false;gNodes[0].rerollPending=false;gNodes[0].remaining=1;gNodes[0].elapsed=0.0;
    TeleportEntity(client,gCorePoints[0][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));SpawnResourceBody(0);
    char poolPath[128],weightPath[160];Format(poolPath,sizeof(poolPath),"Pools/%s",gNodes[0].pool);
    RerollSetWeight(poolPath,"reroll_on_regrow","1");RerollForce(gNodes[0].pool,4);
    gMaterials[4].hold=5.0;gMaterials[4].grow=20.0;gMaterials[4].scale=0.8;
    int entity=Resolve(gNodes[0].ref),ref=gNodes[0].ref,count=GetEntityCount();
    RerollHit(client,0);
    Check(gNodes[0].material==0 && gPlayerInventory[client][0]==gMaterials[0].yield && gPlayerInventory[client][4]==0
        && gNodes[0].remaining==0 && gNodes[0].rerollPending,"final wood knife hit grants and reports old wood before queuing one reroll");
    int r,g,b,a;GetEntityRenderColor(entity,r,g,b,a);
    Check(a==0 && (GetEntProp(entity,Prop_Send,"m_usSolidFlags")&4)!=0,"depletion immediately hides the old model and disables collision");
    ResourceTick(0.0);
    Check(gNodes[0].material==4 && !gNodes[0].rerollPending && gNodes[0].ref==ref && GetEntityCount()==count
        && RerollModelMatches(entity,4),"forced wood to gold changes model and scale on the same entity without allocating a cycle timer or body");
    GetEntityRenderColor(entity,r,g,b,a);
    Check(r==gMaterials[4].rgb[0] && g==gMaterials[4].rgb[1] && b==gMaterials[4].rgb[2] && a==0,
        "rerolled gold immediately uses the new tint while remaining hidden");
    RerollForce(gNodes[0].pool,1);ResourceTick(0.0);
    Check(gNodes[0].material==4,"changing pool weights during growth cannot redraw the selected cycle");
    gNodes[0].elapsed=15.0;UpdateResourceVisual(0);GetEntityRenderColor(entity,r,g,b,a);
    Check(a>=127 && a<=128 && gNodes[0].remaining==0,"regrowth uses new material hold and duration and remains unharvestable halfway");
    TeleportEntity(client,gNodes[0].origin,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
    gNodes[0].elapsed=25.0;ResourceTick(0.0);ResourceTick(0.0);
    Check(gNodes[0].material==4 && gNodes[0].growing && gNodes[0].remaining==0 && !gNodes[0].rerollPending,
        "occupied mature new model waits safely without rerolling or restoring quota");
    TeleportEntity(client,gCorePoints[0][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));ResourceTick(0.0);
    Check(!gNodes[0].growing && gNodes[0].remaining==gMaterials[4].quota && (GetEntProp(entity,Prop_Send,"m_usSolidFlags")&4)==0,
        "clearing the new model volume restores its own harvest quota and collision");
    RerollCheckCollision(0);
    int beforeGold=gPlayerInventory[client][4];
    for(int hit=0;hit<gMaterials[4].quota;hit++)RerollHit(client,0);
    ResourceTick(0.0);
    Check(gNodes[0].material==1 && gPlayerInventory[client][4]==beforeGold+gMaterials[4].quota*gMaterials[4].yield
        && gNodes[0].ref==ref && RerollModelMatches(entity,1),"next genuine depletion grants gold and selects iron as an independent second cycle");
    gNodes[0].elapsed=17.0;RerollForce(gNodes[0].pool,0);
    RemoveOwnedRef(gNodes[0].ref);gNodes[0].ref=INVALID_ENT_REFERENCE;ResourceTick(0.0);entity=Resolve(gNodes[0].ref);
    Check(gNodes[0].material==1 && gNodes[0].elapsed==17.0 && !gNodes[0].rerollPending && RerollModelMatches(entity,1),
        "unexpected entity loss restores the selected material and elapsed time without another draw");
    gNodes[0].elapsed=gMaterials[1].hold+gMaterials[1].grow;ResourceTick(0.0);RerollCheckCollision(0);
    RerollSetWeight(poolPath,"reroll_on_regrow","0");gNodes[0].remaining=1;RerollHit(client,0);ResourceTick(0.0);
    Check(gNodes[0].material==1 && !gNodes[0].rerollPending,"disabling reroll preserves the current material even when the pool selects wood");
    RerollSetWeight(poolPath,"reroll_on_regrow","1");Node(C_Resources,poolPath);gCfg[C_Resources].DeleteKey("RegrowWeights");
    RerollForce(gNodes[0].pool,0,false);gNodes[0].rerollPending=true;ResourceTick(0.0);
    Check(gNodes[0].material==0,"absent RegrowWeights falls back to initial Weights");
    gNodes[0].rerollPending=true;ResourceTick(0.0);
    Check(gNodes[0].material==0 && !gNodes[0].rerollPending,"same-material selections are valid and do not force a different resource");
    gNodes[0].elapsed=gMaterials[0].hold+gMaterials[0].grow;ResourceTick(0.0);RerollCheckCollision(0);
    gInventoryCap=1;gPlayerInventory[client][0]=1;gNodes[0].remaining=1;RerollHit(client,0);
    Check(gNodes[0].remaining==1 && !gNodes[0].rerollPending && !gNodes[0].growing,
        "full personal inventory neither consumes the final quota nor starts reroll");

    float weights[MATERIALS],total;Format(weightPath,sizeof(weightPath),"Pools/%s/RegrowWeights",gNodes[0].pool);
    RerollForce(gNodes[0].pool,-1);
    Check(!ReadResourceWeights(C_Resources,weightPath,weights,total) && !ValidateResourcePools(),"explicit all-zero regrowth pool is rejected instead of falling back");
    RerollSetWeight(weightPath,"gold","-1");Check(!ReadResourceWeights(C_Resources,weightPath,weights,total),"negative resource weight is rejected");
    RerollSetWeight(weightPath,"gold","NaN");Check(!ReadResourceWeights(C_Resources,weightPath,weights,total),"NaN resource weight is rejected");
    RerollSetWeight(weightPath,"gold","1e999");Check(!ReadResourceWeights(C_Resources,weightPath,weights,total),"infinite or overflow resource weight is rejected");
    RerollSetWeight(weightPath,"gold","12garbage");Check(!ReadResourceWeights(C_Resources,weightPath,weights,total),"partially numeric resource weight is rejected");
    RerollSetWeight(weightPath,"gold","3e38");RerollSetWeight(weightPath,"iron","3e38");
    Check(ReadResourceWeights(C_Resources,weightPath,weights,total) && total==2.0,"large finite weights are normalized before summation to prevent overflow");
    RerollForce(gNodes[0].pool,4);bool single=true;for(int trial=0;trial<64;trial++)if(WeightedMaterial(C_Resources,weightPath)!=4)single=false;
    Check(single,"single positive weight wins every draw and zero weight materials are excluded");
    RerollSetWeight(weightPath,"typo_material","1");Check(!ValidateResourcePools(),"unknown material names reject the resource pool configuration");
    float notANumber=view_as<float>(0x7fc00000),infinite=view_as<float>(0x7f800000);
    Check(ValidResourceGrowth(0.0,45.0,0.6),"ordinary resource hold duration and scale remain valid");
    Check(!ValidResourceGrowth(notANumber,45.0,0.6) && !ValidResourceGrowth(0.0,notANumber,0.6)
        && !ValidResourceGrowth(0.0,45.0,notANumber),"NaN is rejected in every resource growth field");
    Check(!ValidResourceGrowth(infinite,45.0,0.6) && !ValidResourceGrowth(0.0,infinite,0.6)
        && !ValidResourceGrowth(0.0,45.0,infinite),"infinity is rejected in every resource growth field");
    Check(!ValidResourceGrowth(3.0e38,3.0e38,0.6),"finite hold plus grow cannot overflow the maturity deadline");

    RemoveOwnedRef(gNodes[0].ref);gNodes[0].ref=INVALID_ENT_REFERENCE;
    delete gCfg[C_Resources];gCfg[C_Resources]=original;gMaterials[4]=gold;
    gNodes[0]=saved;gNodes[0].ref=INVALID_ENT_REFERENCE;
    gPlayerInventory[client]=personal;gInventoryCap=cap;gNextGather[client]=cooldown;
    if(weapon>MaxClients && IsValidEntity(weapon))SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",weapon);
    TeleportEntity(client,origin,angles,velocity);SpawnResourceBody(0);gNodeCount=nodeCount;
    Check(ValidateResourcePools(),"resource reroll fixture restores the shipped configuration");
}

KeyValues tRerollUIConfig;
ResourceNode tRerollUINode;
MaterialDef tRerollUIMaterials[MATERIALS];
int tRerollUIInventory[MATERIALS],tRerollUISerial,tRerollUIRef,tRerollUICap,tRerollUIWeapon;
float tRerollUIOrigin[3],tRerollUIAngles[3],tRerollUIVelocity[3],tRerollUICooldown;

void TestRestoreResourceRerollUI()
{
    if(tRerollUIConfig==null)return;
    RemoveOwnedRef(gNodes[0].ref);gNodes[0].ref=INVALID_ENT_REFERENCE;
    delete gCfg[C_Resources];gCfg[C_Resources]=tRerollUIConfig;tRerollUIConfig=null;
    for(int m=0;m<MATERIALS;m++)gMaterials[m]=tRerollUIMaterials[m];
    gNodes[0]=tRerollUINode;gNodes[0].ref=INVALID_ENT_REFERENCE;gInventoryCap=tRerollUICap;
    int client=GetClientFromSerial(tRerollUISerial);
    if(client>0 && IsClientInGame(client))
    {
        gPlayerInventory[client]=tRerollUIInventory;gNextGather[client]=tRerollUICooldown;
        TeleportEntity(client,tRerollUIOrigin,tRerollUIAngles,tRerollUIVelocity);
        if(tRerollUIWeapon>MaxClients && IsValidEntity(tRerollUIWeapon))SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",tRerollUIWeapon);
    }
    SpawnResourceBody(0);tRerollUISerial=0;tRerollUIRef=INVALID_ENT_REFERENCE;
    PrintToServer("[ResourceRerollUI] restored original resource config, timings, node and personal inventory");
}

void RerollAimUI(int client)
{
    int entity=Resolve(gNodes[0].ref);if(entity<0)return;
    float center[3],position[3],low[3],high[3];EntityCenter(entity,center);
    ModelWorldBounds(entity,gNodes[0].origin,gNodes[0].angles,low,high);
    position=gNodes[0].origin;position[0]=high[0]+28.0;position[2]+=2.0;
    Aim(client,position,center);gNextGather[client]=0.0;
}

void TestPrepareResourceRerollUI(int client)
{
    if(tRerollUIConfig!=null)TestRestoreResourceRerollUI();
    Check(Running() && LiveHuman(client) && !IsFakeClient(client) && gNodeCount>0,
        "live reroll fixture requires a real living client and running campaign");
    if(!Running() || !LiveHuman(client) || IsFakeClient(client) || gNodeCount<1)return;
    int knife=GetPlayerWeaponSlot(client,2);if(knife<=MaxClients){Check(false,"live reroll fixture has a knife");return;}
    tRerollUIConfig=gCfg[C_Resources];tRerollUIConfig.Rewind();
    gCfg[C_Resources]=new KeyValues("ShelterResources");KvCopySubkeys(tRerollUIConfig,gCfg[C_Resources]);
    tRerollUINode=gNodes[0];tRerollUIInventory=gPlayerInventory[client];
    for(int m=0;m<MATERIALS;m++)tRerollUIMaterials[m]=gMaterials[m];
    tRerollUISerial=GetClientSerial(client);tRerollUICap=gInventoryCap;tRerollUICooldown=gNextGather[client];
    tRerollUIWeapon=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon");
    GetClientAbsOrigin(client,tRerollUIOrigin);GetClientEyeAngles(client,tRerollUIAngles);GetEntPropVector(client,Prop_Data,"m_vecAbsVelocity",tRerollUIVelocity);
    SetEntPropEnt(client,Prop_Send,"m_hActiveWeapon",knife);gInventoryCap=0;
    for(int m=0;m<MATERIALS;m++)gPlayerInventory[client][m]=0;
    // A private pool prevents the forced fixture weights affecting other nodes.
    strcopy(gNodes[0].pool,sizeof(gNodes[].pool),"__live_resource_cycle");
    RerollSetWeight("Pools/__live_resource_cycle","reroll_on_regrow","1");
    RerollForce(gNodes[0].pool,0,false);RerollForce(gNodes[0].pool,4);
    gMaterials[4].hold=3.0;gMaterials[4].grow=4.0;gMaterials[1].hold=3.0;gMaterials[1].grow=4.0;
    RemoveOwnedRef(gNodes[0].ref);gNodes[0].ref=INVALID_ENT_REFERENCE;
    gNodes[0].material=0;gNodes[0].remaining=gMaterials[0].quota;gNodes[0].growing=false;
    gNodes[0].elapsed=0.0;gNodes[0].rerollPending=false;
    TeleportEntity(client,gCorePoints[0][0],NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));SpawnResourceBody(0);
    tRerollUIRef=gNodes[0].ref;RerollAimUI(client);
    PrintToServer("[ResourceRerollUI] ready ref=%d wood_hits=%d gold_hits=%d hold=3 grow=4; use real +attack",tRerollUIRef,gMaterials[0].quota,gMaterials[4].quota);
}

// Stages: 0 = gold still growing, 1 = gold mature (aims for real gold hit),
// 2 = iron still growing, 3 = iron mature. No simulated hits are performed.
void TestVerifyResourceRerollUI(int client,int stage)
{
    bool ready=tRerollUIConfig!=null && GetClientSerial(client)==tRerollUISerial && stage>=0 && stage<=3;
    Check(ready,"live reroll verification belongs to the active real collector");if(!ready)return;
    int material=stage<2 ? 4 : 1,entity=Resolve(gNodes[0].ref);bool mature=(stage%2)==1;
    Check(entity>0 && gNodes[0].ref==tRerollUIRef,"real knife regrowth retains the original live resource entity reference");if(entity<0)return;
    Check(gNodes[0].material==material && !gNodes[0].rerollPending && RerollModelMatches(entity,material),
        "real knife depletion switches to the forced next material and model");
    int r,g,b,a;GetEntityRenderColor(entity,r,g,b,a);
    Check(r==gMaterials[material].rgb[0] && g==gMaterials[material].rgb[1] && b==gMaterials[material].rgb[2],
        "live rerolled resource uses its new material tint");
    Check(gPlayerInventory[client][0]==gMaterials[0].quota*gMaterials[0].yield
        && (stage<2 || gPlayerInventory[client][4]==gMaterials[4].quota*gMaterials[4].yield),
        "real knife awards belong to the previous harvested material for every cycle");
    if(mature)
    {
        Check(!gNodes[0].growing && gNodes[0].remaining==gMaterials[material].quota && a==255
            && (GetEntProp(entity,Prop_Send,"m_usSolidFlags")&4)==0,"live regrowth reaches full opacity with new quota and collision");
        if(stage==1){RerollForce(gNodes[0].pool,1);RerollAimUI(client);}
    }
    else Check(gNodes[0].growing && gNodes[0].remaining==0 && a<255
        && (GetEntProp(entity,Prop_Send,"m_usSolidFlags")&4)!=0,"live regrowth remains partly or fully hidden and cannot be harvested yet");
    PrintToServer("[ResourceRerollUI] stage=%d ref=%d material=%s remaining=%d growing=%d elapsed=%.2f alpha=%d wood=%d gold=%d",
        stage,gNodes[0].ref,gMaterials[gNodes[0].material].id,gNodes[0].remaining,gNodes[0].growing,gNodes[0].elapsed,a,gPlayerInventory[client][0],gPlayerInventory[client][4]);
}
