// Placement previews are optional visuals, never logical buildings or cores.
// This record is also the immutable geometry consumed by final confirmation.
enum struct PlacementCandidate
{
    bool surface;
    int epoch;
    int revision;
    int surfaceFlags;
    int surfaceRef;
    float yaw;
    float anchor[3];
    float origin[3];
    float angles[3];
    float normal[3];
}

#define BUILD_PREVIEW_ENTITY_SLOTS 2049
bool gBuildPreviewActive[MAXPLAYERS+1],gBuildPreviewValid[MAXPLAYERS+1];
int gBuildPreviewRef[MAXPLAYERS+1],gBuildPreviewSerial[MAXPLAYERS+1];
int gBuildPreviewEpoch[MAXPLAYERS+1],gBuildPreviewRevision[MAXPLAYERS+1];
int gBuildPreviewRecipe[MAXPLAYERS+1],gBuildPreviewNonce[MAXPLAYERS+1],gBuildPreviewGeneration[MAXPLAYERS+1];
int gBuildPreviewCore[MAXPLAYERS+1],gBuildPreviewSignature[MAXPLAYERS+1];
int gBuildPreviewEntityOwner[BUILD_PREVIEW_ENTITY_SLOTS],gBuildPreviewEntityRef[BUILD_PREVIEW_ENTITY_SLOTS];
int gBuildPreviewEntityGeneration[BUILD_PREVIEW_ENTITY_SLOTS];
float gBuildPreviewDisplayAt[MAXPLAYERS+1],gBuildPreviewNextAttempt[MAXPLAYERS+1],gBuildPreviewNextMenu[MAXPLAYERS+1],gBuildPreviewDeadline[MAXPLAYERS+1];
Menu gBuildPreviewMenu[MAXPLAYERS+1];
PlacementCandidate gBuildPreviewCandidate[MAXPLAYERS+1];
StringMap gPlacementModelBounds;
ArrayList gPlacementModelBoundsValues;

int ActiveBuildPreviewCore(int client)
{
    return client>0 && client<=MaxClients && gBuildPreviewActive[client] ? gBuildPreviewCore[client] : -1;
}

bool IsBuildPreviewEntity(int entity)
{
    return entity>MaxClients && entity<BUILD_PREVIEW_ENTITY_SLOTS && gBuildPreviewEntityOwner[entity]>0
        && IsValidEntity(entity) && gBuildPreviewEntityRef[entity]==EntIndexToEntRef(entity);
}

public bool FilterPlacement(int entity,int mask,any ignored)
{
    return entity!=ignored && !IsBuildPreviewEntity(entity);
}

// Clear identity before RemoveEntity, so an old destruction/menu callback cannot
// adopt a replacement visual or an entity that reused its edict index.
void StopBuildPreview(int client,Menu expected=null)
{
    if(client<1 || client>MaxClients || (expected!=null && gBuildPreviewMenu[client]!=expected))return;
    int ref=gBuildPreviewRef[client],entity=Resolve(ref);
    gBuildPreviewActive[client]=false;gBuildPreviewValid[client]=false;gBuildPreviewRef[client]=INVALID_ENT_REFERENCE;
    gBuildPreviewMenu[client]=null;gBuildPreviewSerial[client]=0;gBuildPreviewNonce[client]=0;
    gBuildPreviewDisplayAt[client]=0.0;gBuildPreviewGeneration[client]++;
    if(entity>MaxClients && entity<BUILD_PREVIEW_ENTITY_SLOTS
        && gBuildPreviewEntityOwner[entity]==client && gBuildPreviewEntityRef[entity]==ref)
    {
        gBuildPreviewEntityOwner[entity]=0;gBuildPreviewEntityRef[entity]=INVALID_ENT_REFERENCE;
        SDKUnhook(entity,SDKHook_SetTransmit,BuildPreviewTransmit);RemoveEntity(entity);
    }
}

void ClearAllBuildPreviews()
{
    for(int client=1;client<=MaxClients;client++)StopBuildPreview(client);
}

void ResetBuildPreviewState()
{
    ClearAllBuildPreviews();
    for(int entity=0;entity<BUILD_PREVIEW_ENTITY_SLOTS;entity++)
    {gBuildPreviewEntityOwner[entity]=0;gBuildPreviewEntityRef[entity]=INVALID_ENT_REFERENCE;}
    delete gPlacementModelBounds;delete gPlacementModelBoundsValues;
    gPlacementModelBounds=new StringMap();gPlacementModelBoundsValues=new ArrayList(6);
}

bool PreviewSessionValid(int client)
{
    return client>0 && client<=MaxClients && gBuildPreviewActive[client] && Running() && LiveHuman(client)
        && !IsFakeClient(client) && gBuildPreviewSerial[client]==GetClientSerial(client)
        && gBuildPreviewEpoch[client]==gEpoch && gBuildPreviewRevision[client]==gConfigRevision
        && gBuildPreviewRecipe[client]>=0 && gBuildPreviewRecipe[client]<gRecipeCount;
}

public Action BuildPreviewTransmit(int entity,int client)
{
    if(!IsBuildPreviewEntity(entity))return Plugin_Handled;
    int owner=gBuildPreviewEntityOwner[entity];
    return client==owner && PreviewSessionValid(owner) && gBuildPreviewRef[owner]==gBuildPreviewEntityRef[entity]
        && gBuildPreviewEntityGeneration[entity]==gBuildPreviewGeneration[owner]
        ? Plugin_Continue : Plugin_Handled;
}

void CachePlacementModelBounds(const char[] model,int entity)
{
    if(gPlacementModelBounds==null){gPlacementModelBounds=new StringMap();gPlacementModelBoundsValues=new ArrayList(6);}
    int existing;if(gPlacementModelBounds.GetValue(model,existing))return;
    float low[3],high[3],values[6];
    GetEntPropVector(entity,Prop_Send,"m_vecMins",low);GetEntPropVector(entity,Prop_Send,"m_vecMaxs",high);
    for(int a=0;a<3;a++){values[a]=low[a];values[a+3]=high[a];}
    gPlacementModelBounds.SetValue(model,gPlacementModelBoundsValues.PushArray(values,6));
}

int CreatePlacementVisual(const char[] model,const float origin[3],const float angles[3],const char[] name)
{
    if(!CanAllocateEntities(1,true))return -1;
    int entity=CreateEntityByName("prop_dynamic_override");if(entity<0)return -1;
    DispatchKeyValue(entity,"model",model);DispatchKeyValue(entity,"solid","0");
    DispatchKeyValue(entity,"targetname",name);DispatchKeyValue(entity,"spawnflags","0");
    DispatchKeyValue(entity,"rendermode","1");DispatchKeyValue(entity,"renderamt","0");
    DispatchKeyValueVector(entity,"origin",origin);DispatchKeyValueVector(entity,"angles",angles);
    if(!DispatchSpawn(entity)){RemoveEntity(entity);return -1;}
    if(!CanAllocateEntities(1,true)){RemoveEntity(entity);return -1;}
    AcceptEntityInput(entity,"DisableCollision");SetEntityMoveType(entity,MOVETYPE_NONE);
    SetEntProp(entity,Prop_Data,"m_takedamage",0,1);SetEntityRenderMode(entity,RENDER_TRANSCOLOR);
    SetEntityRenderColor(entity,255,255,255,0);CachePlacementModelBounds(model,entity);
    return entity;
}

bool PlacementModelBounds(const char[] model,float low[3],float high[3])
{
    int row;
    if(gPlacementModelBounds==null || !gPlacementModelBounds.GetValue(model,row))
    {
        float zero[3];int probe=CreatePlacementVisual(model,zero,zero,"hzsh_model_bounds_probe");
        if(probe<0)return false;RemoveEntity(probe);
        if(!gPlacementModelBounds.GetValue(model,row))return false;
    }
    float values[6];gPlacementModelBoundsValues.GetArray(row,values,6);
    for(int a=0;a<3;a++){low[a]=values[a];high[a]=values[a+3];}
    return true;
}

bool PlacementHullClear(const char[] model,const float origin[3],const float angles[3],int ignored)
{
    float low[3],high[3],mins[3],maxs[3],zero[3];if(!PlacementModelBounds(model,low,high))return false;
    for(int a=0;a<3;a++){mins[a]=99999.0;maxs[a]=-99999.0;}
    for(int corner=0;corner<8;corner++)
    {
        float local[3],world[3];for(int a=0;a<3;a++)local[a]=(corner&(1<<a)) ? high[a] : low[a];
        WorldPoint(local,zero,angles,world);
        for(int a=0;a<3;a++){if(world[a]<mins[a])mins[a]=world[a];if(world[a]>maxs[a])maxs[a]=world[a];}
    }
    Handle trace=TR_TraceHullFilterEx(origin,origin,mins,maxs,MASK_PLAYERSOLID,FilterPlacement,ignored);
    bool clear=!TR_DidHit(trace) && !TR_StartSolid(trace) && !TR_AllSolid(trace);delete trace;return clear;
}

void BeginBuildPreview(int client,int recipe,int nonce)
{
    StopBuildPreview(client);gBuildPreviewActive[client]=true;gBuildPreviewSerial[client]=GetClientSerial(client);
    gBuildPreviewEpoch[client]=gEpoch;gBuildPreviewRevision[client]=gConfigRevision;
    gBuildPreviewRecipe[client]=recipe;gBuildPreviewNonce[client]=nonce;
    gBuildPreviewDeadline[client]=GetGameTime()+45.0;
    gBuildPreviewNextAttempt[client]=0.0;gBuildPreviewNextMenu[client]=GetGameTime()+0.5;
    gBuildPreviewSignature[client]=-1;UpdateBuildPreview(client,GetGameTime());
}

void UpdateBuildPreview(int client,float now)
{
    if(!PreviewSessionValid(client)){StopBuildPreview(client);return;}
    int recipe=gBuildPreviewRecipe[client],entity=Resolve(gBuildPreviewRef[client]);
    char model[PLATFORM_MAX_PATH],profile[128];Format(profile,sizeof(profile),"Types/%s",gRecipes[recipe].type);
    Text(gRecipes[recipe].file,profile,"model",model,sizeof(model));
    if(entity<0 && now>=gBuildPreviewNextAttempt[client])
    {
        gBuildPreviewNextAttempt[client]=now+1.0;
        float zero[3];entity=CreatePlacementVisual(model,zero,zero,"hzsh_build_preview");
        if(entity>MaxClients)
        {
            if(entity>=BUILD_PREVIEW_ENTITY_SLOTS || !SDKHookEx(entity,SDKHook_SetTransmit,BuildPreviewTransmit))
            {RemoveEntity(entity);entity=-1;}
            else
            {
                int ref=EntIndexToEntRef(entity);gBuildPreviewRef[client]=ref;
                gBuildPreviewEntityOwner[entity]=client;gBuildPreviewEntityRef[entity]=ref;
                gBuildPreviewEntityGeneration[entity]=gBuildPreviewGeneration[client];
                if(HasEntProp(entity,Prop_Send,"m_nSkin"))SetEntProp(entity,Prop_Send,"m_nSkin",gRecipes[recipe].meta.skin);
                if(HasEntProp(entity,Prop_Send,"m_nBody"))SetEntProp(entity,Prop_Send,"m_nBody",gRecipes[recipe].meta.body);
            }
        }
    }
    PlacementCandidate candidate;bool valid=ComputeRecipePlacementCandidate(client,recipe,candidate,false);
    int core=-1;if(valid)valid=RecipePlacementRules(client,recipe,candidate,core,false,entity>0 ? 1 : 0);
    gBuildPreviewValid[client]=valid && entity>0 && candidate.surface;
    gBuildPreviewCandidate[client]=candidate;gBuildPreviewCore[client]=core;
    if(entity>0)
    {
        if(candidate.surface)
        {
            TeleportEntity(entity,candidate.origin,candidate.angles,NULL_VECTOR);
            if(valid)SetEntityRenderColor(entity,gRecipes[recipe].meta.render[0],gRecipes[recipe].meta.render[1],gRecipes[recipe].meta.render[2],CONSTRUCTION_ALPHA);
            else SetEntityRenderColor(entity,255,65,65,CONSTRUCTION_ALPHA);
            gBuildPreviewDisplayAt[client]=now;
        }
        else {SetEntityRenderColor(entity,255,65,65,0);gBuildPreviewDisplayAt[client]=0.0;}
    }
    else gBuildPreviewDisplayAt[client]=0.0;
}

int BuildPreviewMenuSignature(int client)
{
    int core=gBuildPreviewCore[client],recipe=gBuildPreviewRecipe[client];
    int signature=(gBuildPreviewValid[client] ? 1 : 0)+(core+1)*2;
    if(core>=0 && core<gCoreCount)
    {
        RefreshBuildAreas();signature+=gBuildAreas[core].count*32+gBuildAreas[core].limit*32768;
        if(IsBaseSupportKind(gRecipes[recipe].kind))
            signature+=SupportMenuCount(core,gRecipes[recipe].kind)*1024;
    }
    return signature;
}

void BuildPreviewTick(float now)
{
    for(int client=1;client<=MaxClients;client++)
    {
        if(!gBuildPreviewActive[client])continue;
        if(!PreviewSessionValid(client) || GetClientMenu(client)!=MenuSource_Normal || now>=gBuildPreviewDeadline[client])
        {StopBuildPreview(client);continue;}
        UpdateBuildPreview(client,now);if(!gBuildPreviewActive[client])continue;
        int signature=BuildPreviewMenuSignature(client);
        if(signature!=gBuildPreviewSignature[client] && now>=gBuildPreviewNextMenu[client])
        {
            gBuildPreviewNextMenu[client]=now+0.5;
            ShowBuildRecipeDetails(client,gBuildPreviewRecipe[client],true,true);
        }
    }
}

bool ConfirmBuildPreview(int client,int recipe,int nonce)
{
    if(!PreviewSessionValid(client) || gBuildPreviewRecipe[client]!=recipe || gBuildPreviewNonce[client]!=nonce
        || !gBuildPreviewValid[client] || gBuildPreviewDisplayAt[client]<=0.0
        || GetGameTime()-gBuildPreviewDisplayAt[client]>0.35)
    {StopBuildPreview(client);Tell(client,"%t","BuildPreviewUnavailable");PlayClientInteractionSound(client,"build_failed");return false;}
    PlacementCandidate candidate;candidate=gBuildPreviewCandidate[client];int expectedCore=gBuildPreviewCore[client];
    // Consume and release the optional edict before checking the real allocation.
    StopBuildPreview(client);return BuildRecipeAtCandidate(client,recipe,expectedCore,candidate);
}
