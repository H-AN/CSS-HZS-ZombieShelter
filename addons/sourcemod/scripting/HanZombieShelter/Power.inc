float gGridProduction[MAX_CORES],gGridDemand[MAX_CORES],gGridAllocated[MAX_CORES];
int gGridOffline[MAX_CORES],gLightLimit=16,gGrenadeLimit=24;
bool gPowerEnabled=true;
bool gPowerOverloadPenalty;
ConVar gPowerOverloadCvar;
float gGrenadeGravity=400.0,gGrenadeLifetime=6.0;
char gGrenadeModel[PLATFORM_MAX_PATH];
int gPowerLoadOrder[MAX_BUILDINGS],gPowerLoadCount,gPowerOrderRevision;
int gLightObservedRef[MAX_BUILDINGS];
bool gLightCanObserve[MAX_BUILDINGS];

void EnsurePowerLoadOrder()
{
    EnsureRuntimeBuildings();
    if(gPowerOrderRevision==gRuntimeBuildingRevision)return;
    gPowerLoadCount=0;
    for(int b=RuntimeNextBuilding(-1);b>=0;b=RuntimeNextBuilding(b))
    {
        if(gBuildings[b].powerDemand<=0.0)continue;
        int at=gPowerLoadCount;
        while(at>0)
        {
            int prior=gPowerLoadOrder[at-1];
            if(gBuildings[b].powerPriority>gBuildings[prior].powerPriority
                || (gBuildings[b].powerPriority==gBuildings[prior].powerPriority && gBuildings[b].uid>=gBuildings[prior].uid))break;
            gPowerLoadOrder[at]=prior;at--;
        }
        gPowerLoadOrder[at]=b;gPowerLoadCount++;
    }
    gPowerOrderRevision=gRuntimeBuildingRevision;
}

// HP changes are synchronous, but cannot affect an unrelated base grid.
void RecalculateBuildingPower(int b)
{
    if(gRuntimeBuildingsDirty || b<0 || b>=MAX_BUILDINGS || !gBuildings[b].gridAssigned
        || gBuildings[b].powerCore<0 || gBuildings[b].powerCore>=gCoreCount)
    {RecalculatePower();return;}
    RecalculatePower(gBuildings[b].powerCore);
}

void RegisterPowerModeCvar()
{
    gPowerOverloadCvar=CreateConVar("sm_zsh_power_overload","-1","Power shortage mode: -1 uses power.cfg, 0 allocates by priority, 1 stops all positive loads on an overloaded base grid.",FCVAR_NOTIFY,true,-1.0,true,1.0);
    gPowerOverloadCvar.AddChangeHook(PowerModeChanged);
}

bool PowerOverloadPenaltyEnabled()
{
    if(gPowerOverloadCvar!=null && gPowerOverloadCvar.IntValue>=0)return gPowerOverloadCvar.IntValue==1;
    return gPowerOverloadPenalty;
}

public void PowerModeChanged(ConVar convar,const char[] oldValue,const char[] newValue)
{
    gNextHUD=0.0;
    RecalculatePower();
}

bool LoadPowerSettings()
{
    char enabled[16];Text(C_Power,"Rules","enabled",enabled,sizeof(enabled),"1");
    if(!StrEqual(enabled,"0") && !StrEqual(enabled,"1"))return ConfigFailure("power enabled must be 0 or 1");
    gPowerEnabled=StrEqual(enabled,"1");
    char overload[16];Text(C_Power,"Rules","overload_penalty",overload,sizeof(overload),"0");
    if(!StrEqual(overload,"0") && !StrEqual(overload,"1"))return ConfigFailure("power overload_penalty must be 0 or 1");
    gPowerOverloadPenalty=StrEqual(overload,"1");bool found;
    if(!StoredLevelInt(C_Power,"Limits","max_lights",16,gLightLimit,found)
        || !StoredLevelInt(C_Power,"Limits","max_projectiles",24,gGrenadeLimit,found)
        || gLightLimit<1 || gLightLimit>32 || gGrenadeLimit<1 || gGrenadeLimit>32)return ConfigFailure("power light/projectile limits 1..32");
    if(!StoredLevelFloat(C_Grenades,"Projectile","gravity",400.0,gGrenadeGravity,found)
        || !StoredLevelFloat(C_Grenades,"Projectile","lifetime_seconds",6.0,gGrenadeLifetime,found)
        || gGrenadeGravity<0.0 || gGrenadeGravity>1000.0 || gGrenadeLifetime<0.5 || gGrenadeLifetime>15.0)return ConfigFailure("grenade projectile timing/gravity");
    Text(C_Grenades,"Projectile","model",gGrenadeModel,sizeof(gGrenadeModel),"models/Items/ar2_grenade.mdl");
    if(PrecacheModel(gGrenadeModel,true)<=0)return ConfigFailure("grenade model");
    return LoadGrenadeExplosionSounds();
}

void ResetPowerState()
{
    for(int c=0;c<MAX_CORES;c++){gGridProduction[c]=0.0;gGridDemand[c]=0.0;gGridAllocated[c]=0.0;gGridOffline[c]=0;}
}

int GridAt(const float origin[3])
{
    int safe=SafeCoreAt(origin);if(safe>=0)return safe;
    int selected=-1;float nearest=999999999.0;
    for(int c=0;c<gCoreCount;c++)
    {
        if(IsReleasedDynamicBaseSlot(c))continue;
        float distance=GetVectorDistance(origin,gCores[c].origin,true);
        if(distance<nearest){nearest=distance;selected=c;}
    }
    return selected;
}

float GeneratorOutput(int b)
{
    if(!BuildingOperational(b)
        || gBuildings[b].kind!=Kind_Generator || gBuildings[b].hp<=0 || gBuildings[b].maxHP<1 || Resolve(gBuildings[b].ref)<0
        || !BuildCoreAlive(gBuildings[b].powerCore))return 0.0;
    float fraction=float(gBuildings[b].hp)/float(gBuildings[b].maxHP);if(fraction>1.0)fraction=1.0;
    return gBuildings[b].powerOutput*fraction;
}

bool BuildingPowered(int b)
{
    return Running() && BuildingOperational(b)
        && (gBuildings[b].kind!=Kind_Light || (!gBuildings[b].lightManualOff && LampNightPowerActive()))
        && Resolve(gBuildings[b].ref)>0 && BuildCoreAlive(gBuildings[b].powerCore)
        && (!gPowerEnabled || gBuildings[b].powerDemand<=0.0 || gBuildings[b].powered);
}

float EffectiveBuildingPowerDemand(int b)
{
    if(b<0 || b>=MAX_BUILDINGS)return 0.0;
    if(gBuildings[b].kind==Kind_Light && (gBuildings[b].lightManualOff || !LampNightPowerActive()))return 0.0;
    return gBuildings[b].powerDemand;
}

void BuildingPowerStateLabel(int b,char[] label,int length)
{
    if(gBuildings[b].kind==Kind_Light && gBuildings[b].lightManualOff)
        strcopy(label,length,"PowerLightManualOff");
    else if(gBuildings[b].kind==Kind_Light && !LampNightPowerActive())
        strcopy(label,length,Running() && gPhase==Phase_Day ? "PowerLightDayOff" : "PowerLightInactive");
    else strcopy(label,length,BuildingPowered(b) ? "PowerOnline" : "PowerOffline");
}

void RecalculatePower(int onlyCore=-1)
{
    bool lampsActive=LampNightPowerActive();
    float beforeOutput[MAX_CORES],beforeDemand[MAX_CORES],beforeAllocated[MAX_CORES];int beforeOffline[MAX_CORES];
    for(int c=0;c<gCoreCount;c++)
    {
        beforeOutput[c]=gGridProduction[c];beforeDemand[c]=gGridDemand[c];beforeAllocated[c]=gGridAllocated[c];beforeOffline[c]=gGridOffline[c];
    }
    bool wasPowered[MAX_BUILDINGS];
    for(int b=RuntimeNextPowerBuilding(-1,onlyCore);b>=0;b=RuntimeNextPowerBuilding(b,onlyCore))wasPowered[b]=gBuildings[b].powered;
    if(onlyCore<0)ResetPowerState();
    else{gGridProduction[onlyCore]=0.0;gGridDemand[onlyCore]=0.0;gGridAllocated[onlyCore]=0.0;gGridOffline[onlyCore]=0;}
    for(int b=RuntimeNextPowerBuilding(-1,onlyCore);b>=0;b=RuntimeNextPowerBuilding(b,onlyCore))
    {
        gBuildings[b].powered=false;
        if(!BuildingOperational(b) || Resolve(gBuildings[b].ref)<0 || gBuildings[b].kind==Kind_Campfire)continue;
        if(IsBaseSupportKind(gBuildings[b].kind))
        {
            if(!gBuildings[b].gridAssigned || gBuildings[b].powerCore!=gBuildings[b].buildCore)
            {gBuildings[b].powerCore=gBuildings[b].buildCore;gBuildings[b].gridAssigned=true;InvalidateRuntimeBuildings();}
        }
        if(!gBuildings[b].gridAssigned)
        {gBuildings[b].powerCore=gBuildings[b].kind==Kind_Base ? gBuildings[b].core : GridAt(gBuildings[b].origin);gBuildings[b].gridAssigned=true;InvalidateRuntimeBuildings();}
        int core=gBuildings[b].powerCore;
        if(!Running() || core<0 || core>=gCoreCount)continue;
        // Preserve the configured level demand, but sleeping daytime lamps do
        // not enter total demand, priority allocation, overload or outage counts.
        if(gBuildings[b].kind==Kind_Light && (!lampsActive || gBuildings[b].lightManualOff))continue;
        gGridProduction[core]+=GeneratorOutput(b);
        gGridDemand[core]+=gBuildings[b].powerDemand;
        // A destroyed base disconnects its entire grid, including free loads
        // and the power-disabled compatibility mode. Other bases stay independent.
        if(!BuildCoreAlive(core)){if(gBuildings[b].powerDemand>0.0)gGridOffline[core]++;continue;}
        if(!gPowerEnabled || gBuildings[b].powerDemand<=0.0)gBuildings[b].powered=true;
    }
    // Keep the full connected demand while stopped, so an overloaded grid
    // cannot recover merely because its own loads were disconnected.
    bool overloaded[MAX_CORES];
    if(gPowerEnabled && PowerOverloadPenaltyEnabled())
        for(int c=0;c<gCoreCount;c++)overloaded[c]=gGridDemand[c]>gGridProduction[c];
    // Full loads only. Smaller later loads may use leftover capacity. A reused
    // array slot must never jump ahead of older UIDs at the same priority.
    EnsurePowerLoadOrder();
    for(int pass=0;pass<gPowerLoadCount;pass++)
    {
        int next=gPowerLoadOrder[pass];
        int core=gBuildings[next].powerCore;if(!Running() || core<0 || core>=gCoreCount)continue;
        if(onlyCore>=0 && core!=onlyCore)continue;
        if(!BuildingOperational(next) || Resolve(gBuildings[next].ref)<0 || gBuildings[next].kind==Kind_Campfire
            || !BuildCoreAlive(core) || (gBuildings[next].kind==Kind_Light && (!lampsActive || gBuildings[next].lightManualOff)))continue;
        if(overloaded[core]){gGridOffline[core]++;continue;}
        float available=gGridProduction[core]-gGridAllocated[core];
        if(!gPowerEnabled || gBuildings[next].powerDemand<=available)
        {gBuildings[next].powered=true;gGridAllocated[core]+=gBuildings[next].powerDemand;}
        else gGridOffline[core]++;
    }
    bool powerLost[MAX_CORES];
    for(int b=RuntimeNextPowerBuilding(-1,onlyCore);b>=0;b=RuntimeNextPowerBuilding(b,onlyCore))
    {
        // A fresh unpowered load is not a fault. Coalesce actual lost loads into
        // one edge per grid, including a destroyed base's surviving loads.
        int core=gBuildings[b].powerCore;
        if(Running() && gPowerEnabled && wasPowered[b] && !gBuildings[b].powered
            && BuildingOperational(b) && gBuildings[b].powerDemand>0.0 && Resolve(gBuildings[b].ref)>0
            && (gBuildings[b].kind!=Kind_Light || (lampsActive && !gBuildings[b].lightManualOff))
            && core>=0 && core<gCoreCount)powerLost[core]=true;
        if(wasPowered[b]!=gBuildings[b].powered)
        {
            InvalidateDefenseCache(b);
            if(gBuildings[b].kind==Kind_Expander)MarkBuildAreasDirty(false);
        }
        if(gBuildings[b].used && gBuildings[b].kind==Kind_Light)UpdateBuildingLight(b);
        if(gBuildings[b].used && gBuildings[b].kind==Kind_Beacon)SyncBeaconMarker(b);
    }
    RefreshBuildAreas();
    // Allocation is committed before audio and forwards. A notification that
    // recalculates the unchanged grid sees no additional powered-to-off edge.
    for(int c=0;c<gCoreCount;c++)if(powerLost[c])PlayCoreInteractionSound(c,"power_failure");
    for(int c=0;c<gCoreCount;c++)
        if(beforeOutput[c]!=gGridProduction[c] || beforeDemand[c]!=gGridDemand[c] || beforeAllocated[c]!=gGridAllocated[c] || beforeOffline[c]!=gGridOffline[c])NotifyPowerChanged(c);
}

int LightCount()
{
    EnsureRuntimeBuildings();return gRuntimeKindCount[view_as<int>(Kind_Light)];
}

void RemoveBuildingEffect(int b)
{
    RemoveBeaconMarker(b);
    int ref=gBuildings[b].effectRef;gBuildings[b].effectRef=INVALID_ENT_REFERENCE;
    if(ref!=0)RemoveOwnedRef(ref);
}

bool CreateBuildingLight(int b)
{
    int body=Resolve(gBuildings[b].ref);if(body<0 || !CanAllocateEntities(1,true))return false;
    int light=CreateEntityByName("light_dynamic");if(light<0)return false;
    DispatchKeyValue(light,"_light","255 235 200 255");DispatchKeyValue(light,"brightness","2");
    // A style conflict leaves an owned off entity, preserving the building and
    // allowing explicit diagnosis without borrowing environment/HZS styles.
    if(LampLightingReady())
    {char style[16];IntToString(gLampStyleIndex,style,sizeof(style));DispatchKeyValue(light,"style",style);}
    DispatchKeyValue(light,"_inner_cone","0");DispatchKeyValue(light,"_cone","0");
    DispatchKeyValueFloat(light,"distance",gBuildings[b].lightRadius);
    if(!DispatchSpawn(light) || !CanAllocateEntities(1,true)){RemoveEntity(light);return false;}
    SetVariantString("!activator");AcceptEntityInput(light,"SetParent",body);
    gBuildings[b].effectRef=EntIndexToEntRef(light);SyncBuildingLightTransform(b);UpdateBuildingLight(b);return true;
}

void SyncBuildingLightTransform(int b)
{
    if(b<0 || b>=MAX_BUILDINGS || gBuildings[b].kind!=Kind_Light)return;
    int light=Resolve(gBuildings[b].effectRef);if(light<0)return;
    // Parented entities consume local coordinates; the body supplies world rotation.
    float localAngles[3];
    TeleportEntity(light,gBuildings[b].meta.lightOffset,localAngles,NULL_VECTOR);
}

void UpdateBuildingLight(int b)
{
    int light=Resolve(gBuildings[b].effectRef);if(light<0)return;
    if(gLightObservedRef[b]!=gBuildings[b].effectRef)
    {
        gLightObservedRef[b]=gBuildings[b].effectRef;
        gLightCanObserve[b]=HasEntProp(light,Prop_Send,"m_Radius") && HasEntProp(light,Prop_Send,"m_Exponent")
            && HasEntProp(light,Prop_Send,"m_Flags") && HasEntProp(light,Prop_Data,"m_ActualFlags") && HasEntProp(light,Prop_Data,"m_On");
    }
    bool observe=gLightCanObserve[b],on=BuildingPowered(b);
    if(!observe || GetEntPropFloat(light,Prop_Send,"m_Radius")!=gBuildings[b].lightRadius)
    {SetVariantFloat(gBuildings[b].lightRadius);AcceptEntityInput(light,"distance");}
    if(!observe || GetEntProp(light,Prop_Send,"m_Exponent")!=gBuildings[b].lightBrightness)
    {SetVariantInt(gBuildings[b].lightBrightness);AcceptEntityInput(light,"brightness");}
    // CDynamicLight::InputTurnOn restores ActualFlags; TurnOff uses the two
    // NO_*_ILLUMINATION bits. Read both state and flags to repair outside edits.
    int flags=on && observe ? GetEntProp(light,Prop_Data,"m_ActualFlags",1):3;
    if(!observe || (GetEntProp(light,Prop_Data,"m_On",1)!=0)!=on || GetEntProp(light,Prop_Send,"m_Flags",1)!=flags)
        AcceptEntityInput(light,on ? "TurnOn" : "TurnOff");
}

void StopPowerEffects()
{
    ClearBeaconMarkers();
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        gBuildings[b].powered=false;int effect=Resolve(gBuildings[b].effectRef);
        if(effect>0 && gBuildings[b].kind==Kind_Light)AcceptEntityInput(effect,"TurnOff");
    }
    bool changed[MAX_CORES];
    for(int c=0;c<gCoreCount;c++)changed[c]=gGridProduction[c]!=0.0 || gGridDemand[c]!=0.0 || gGridAllocated[c]!=0.0 || gGridOffline[c]!=0;
    ClearDefenseState();MarkBuildAreasDirty();ResetPowerState();RefreshBuildAreas();
    for(int c=0;c<gCoreCount;c++)if(changed[c])NotifyPowerChanged(c);
}

void PowerTick()
{
    for(int b=RuntimeNextBuilding(-1,view_as<int>(Kind_Light));b>=0;b=RuntimeNextBuilding(b,view_as<int>(Kind_Light)))
        if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].kind==Kind_Light
            && Resolve(gBuildings[b].ref)>0 && Resolve(gBuildings[b].effectRef)<0)CreateBuildingLight(b);
    RecalculatePower();
}

void AppendGridInfo(int client,int core,char[] title,int length)
{
    if(core<0 || core>=gCoreCount)return;
    char name[64],line[192];CoreDisplayName(core,name,sizeof(name),client);
    Format(line,sizeof(line),"%T","PowerGridInfo",client,name,gGridProduction[core],gGridDemand[core],gGridAllocated[core],gGridOffline[core]);StrCat(title,length,line);
}

void AppendBuildingPowerInfo(int client,int b,int level,char[] title,int length)
{
    char line[192],profile[128],state[64],stateLabel[64];BuildingProfilePath(b,profile,sizeof(profile));
    if(gBuildings[b].kind==Kind_Generator)
    {
        float output;ReadLevelFloat(gBuildings[b].file,profile,level,"power_output",0.0,output);
        Format(line,sizeof(line),"%T","GeneratorPowerInfo",client,GeneratorOutput(b),gBuildings[b].powerOutput,output);StrCat(title,length,line);
    }
    else if(gBuildings[b].powerDemand>0.0 || gBuildings[b].kind==Kind_Light)
    {
        float demand;ReadLevelFloat(gBuildings[b].file,profile,level,"power_required",0.0,demand);
        BuildingPowerStateLabel(b,stateLabel,sizeof(stateLabel));TranslateLabel(client,stateLabel,state,sizeof(state));
        Format(line,sizeof(line),"%T","ConsumerPowerInfo",client,state,EffectiveBuildingPowerDemand(b),demand);StrCat(title,length,line);
    }
    AppendGridInfo(client,gBuildings[b].powerCore,title,length);
}
