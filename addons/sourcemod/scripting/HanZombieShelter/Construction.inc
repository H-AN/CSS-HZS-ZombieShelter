// Construction reuses logical durability, with a separate state for activation.
ConVar gConstructionCvar;
bool gConstructionConfiguring;
bool gConstructionCompleting;

void RegisterConstructionCvar()
{
    gConstructionCvar=CreateConVar("sm_zsh_real_construction","0","New player buildings start at 1 HP; weapon hits add the configured construction HP until complete.",FCVAR_NOTIFY,true,0.0,true,1.0);
    gConstructionCvar.AddChangeHook(ConstructionModeChanged);
}

void ConfigureRealConstruction(bool enabled)
{
    if(gConstructionCvar==null)return;
    gConstructionConfiguring=true;gConstructionCvar.BoolValue=enabled;gConstructionConfiguring=false;
}

bool BuildingOperational(int b)
{
    return b>=0 && b<MAX_BUILDINGS && gBuildings[b].used && gBuildings[b].alive
        && gBuildings[b].hp>0 && !gBuildings[b].constructing;
}

void BeginBuildingConstruction(int b)
{
    if(gConstructionCvar==null || !gConstructionCvar.BoolValue)return;
    gBuildings[b].constructionTarget=gBuildings[b].maxHP;
    gBuildings[b].constructing=gBuildings[b].maxHP>1;
    if(gBuildings[b].constructing)gBuildings[b].hp=1;
}

void SyncConstructionVisual(int b)
{
    int entity=Resolve(gBuildings[b].ref);if(entity<0)return;
    SetEntityRenderMode(entity,RENDER_TRANSCOLOR);
    int alpha=gBuildings[b].constructing ? 80 : gBuildings[b].meta.render[3];
    // Map bases have no type metadata; completed dynamic bases do.
    if(gBuildings[b].kind==Kind_Base && !gBuildings[b].meta.label[0])
        SetEntityRenderColor(entity,255,255,255,gBuildings[b].constructing ? 80 : 255);
    else SetEntityRenderColor(entity,gBuildings[b].meta.render[0],gBuildings[b].meta.render[1],gBuildings[b].meta.render[2],alpha);
    // Group 2 permits actor movement while preserving CS:S knife traces.
    SetEntityCollisionGroup(entity,gBuildings[b].constructing ? 2 : 0);
}

void CompleteBuildingConstruction(int b,int client=0)
{
    if(!gBuildings[b].constructing)return;
    gBuildings[b].hp=gBuildings[b].constructionTarget;
    gBuildings[b].constructing=false;
    gBuildings[b].nextFire=GetGameTime()+gBuildings[b].interval;
    gBuildings[b].nextRestore=GetGameTime()+gBuildings[b].restoreInterval;
    SyncConstructionVisual(b);InvalidateDefenseCache(b);MarkBuildAreasDirty();RecalculatePower();
    if(client>0)Tell(client,"%t","ConstructionComplete");
}

public void ConstructionModeChanged(ConVar variable,const char[] oldValue,const char[] newValue)
{
    if(gConstructionConfiguring || gConstructionCompleting || variable.BoolValue)return;
    // Disabling does not strand existing ghosts; re-enabling never resets buildings.
    gConstructionCompleting=true;
    for(int b=0;b<MAX_BUILDINGS;b++)
        if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].constructing)CompleteBuildingConstruction(b);
    gConstructionCompleting=false;
}
