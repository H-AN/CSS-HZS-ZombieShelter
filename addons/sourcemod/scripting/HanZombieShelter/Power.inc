float gGridProduction[MAX_CORES],gGridDemand[MAX_CORES],gGridAllocated[MAX_CORES];
int gGridOffline[MAX_CORES],gLightLimit=16,gGrenadeLimit=24;
bool gPowerEnabled=true;
float gGrenadeGravity=400.0,gGrenadeLifetime=6.0;
char gGrenadeModel[PLATFORM_MAX_PATH];

bool LoadPowerSettings()
{
    char enabled[16];Text(C_Power,"Rules","enabled",enabled,sizeof(enabled),"1");
    if(!StrEqual(enabled,"0") && !StrEqual(enabled,"1"))return ConfigFailure("power enabled must be 0 or 1");
    gPowerEnabled=StrEqual(enabled,"1");bool found;
    if(!StoredLevelInt(C_Power,"Limits","max_lights",16,gLightLimit,found)
        || !StoredLevelInt(C_Power,"Limits","max_projectiles",24,gGrenadeLimit,found)
        || gLightLimit<1 || gLightLimit>32 || gGrenadeLimit<1 || gGrenadeLimit>32)return ConfigFailure("power light/projectile limits 1..32");
    if(!StoredLevelFloat(C_Grenades,"Projectile","gravity",400.0,gGrenadeGravity,found)
        || !StoredLevelFloat(C_Grenades,"Projectile","lifetime_seconds",6.0,gGrenadeLifetime,found)
        || gGrenadeGravity<0.0 || gGrenadeGravity>1000.0 || gGrenadeLifetime<0.5 || gGrenadeLifetime>15.0)return ConfigFailure("grenade projectile timing/gravity");
    Text(C_Grenades,"Projectile","model",gGrenadeModel,sizeof(gGrenadeModel),"models/Items/ar2_grenade.mdl");
    return (PrecacheModel(gGrenadeModel,true)>0) || ConfigFailure("grenade model");
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
        float distance=GetVectorDistance(origin,gCores[c].origin,true);
        if(distance<nearest){nearest=distance;selected=c;}
    }
    return selected;
}

float GeneratorOutput(int b)
{
    if(b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive
        || gBuildings[b].kind!=Kind_Generator || gBuildings[b].hp<=0 || gBuildings[b].maxHP<1 || Resolve(gBuildings[b].ref)<0)return 0.0;
    float fraction=float(gBuildings[b].hp)/float(gBuildings[b].maxHP);if(fraction>1.0)fraction=1.0;
    return gBuildings[b].powerOutput*fraction;
}

bool BuildingPowered(int b)
{
    return Running() && b>=0 && b<MAX_BUILDINGS && gBuildings[b].used && gBuildings[b].alive
        && Resolve(gBuildings[b].ref)>0 && (!gPowerEnabled || gBuildings[b].powerDemand<=0.0 || gBuildings[b].powered);
}

void RecalculatePower()
{
    float beforeOutput[MAX_CORES],beforeDemand[MAX_CORES],beforeAllocated[MAX_CORES];int beforeOffline[MAX_CORES];
    for(int c=0;c<gCoreCount;c++)
    {
        beforeOutput[c]=gGridProduction[c];beforeDemand[c]=gGridDemand[c];beforeAllocated[c]=gGridAllocated[c];beforeOffline[c]=gGridOffline[c];
    }
    ResetPowerState();int loads[MAX_BUILDINGS],loadCount;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        gBuildings[b].powered=false;
        if(!gBuildings[b].used || !gBuildings[b].alive || Resolve(gBuildings[b].ref)<0)continue;
        if(!gBuildings[b].gridAssigned)
        {gBuildings[b].powerCore=gBuildings[b].kind==Kind_Base ? gBuildings[b].core : GridAt(gBuildings[b].origin);gBuildings[b].gridAssigned=true;}
        int core=gBuildings[b].powerCore;
        if(!Running() || core<0 || core>=gCoreCount)continue;
        gGridProduction[core]+=GeneratorOutput(b);
        gGridDemand[core]+=gBuildings[b].powerDemand;
        if(!gPowerEnabled || gBuildings[b].powerDemand<=0.0)gBuildings[b].powered=true;
        if(gBuildings[b].powerDemand>0.0)
        {
            int at=loadCount;
            while(at>0)
            {
                int prior=loads[at-1];
                if(gBuildings[b].powerPriority>gBuildings[prior].powerPriority
                    || (gBuildings[b].powerPriority==gBuildings[prior].powerPriority && gBuildings[b].uid>=gBuildings[prior].uid))break;
                loads[at]=prior;at--;
            }
            loads[at]=b;loadCount++;
        }
    }
    // Full loads only. Smaller later loads may use leftover capacity. A reused
    // array slot must never jump ahead of older UIDs at the same priority.
    for(int pass=0;pass<loadCount;pass++)
    {
        int next=loads[pass];
        int core=gBuildings[next].powerCore;if(!Running() || core<0 || core>=gCoreCount)continue;
        float available=gGridProduction[core]-gGridAllocated[core];
        if(!gPowerEnabled || gBuildings[next].powerDemand<=available)
        {gBuildings[next].powered=true;gGridAllocated[core]+=gBuildings[next].powerDemand;}
        else gGridOffline[core]++;
    }
    for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].kind==Kind_Light)UpdateBuildingLight(b);
    for(int c=0;c<gCoreCount;c++)
        if(beforeOutput[c]!=gGridProduction[c] || beforeDemand[c]!=gGridDemand[c] || beforeAllocated[c]!=gGridAllocated[c] || beforeOffline[c]!=gGridOffline[c])NotifyPowerChanged(c);
}

int LightCount()
{
    int count;for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive && gBuildings[b].kind==Kind_Light)count++;return count;
}

void RemoveBuildingEffect(int b)
{
    int ref=gBuildings[b].effectRef;gBuildings[b].effectRef=INVALID_ENT_REFERENCE;
    if(ref!=0)RemoveOwnedRef(ref);
}

bool CreateBuildingLight(int b)
{
    int body=Resolve(gBuildings[b].ref);if(body<0 || !CanAllocateEntities(1,true))return false;
    int light=CreateEntityByName("light_dynamic");if(light<0)return false;
    DispatchKeyValue(light,"_light","255 235 200 255");DispatchKeyValue(light,"brightness","2");
    DispatchKeyValue(light,"style","0");DispatchKeyValue(light,"inner_cone","0");DispatchKeyValue(light,"cone","0");
    DispatchKeyValueFloat(light,"distance",gBuildings[b].lightRadius);
    if(!DispatchSpawn(light) || !CanAllocateEntities(1,true)){RemoveEntity(light);return false;}
    float source[3];source=gBuildings[b].origin;source[2]+=64.0;TeleportEntity(light,source,NULL_VECTOR,NULL_VECTOR);
    SetVariantString("!activator");AcceptEntityInput(light,"SetParent",body);
    gBuildings[b].effectRef=EntIndexToEntRef(light);UpdateBuildingLight(b);return true;
}

void UpdateBuildingLight(int b)
{
    int light=Resolve(gBuildings[b].effectRef);if(light<0)return;
    SetVariantFloat(gBuildings[b].lightRadius);AcceptEntityInput(light,"distance");
    SetVariantInt(gBuildings[b].lightBrightness);AcceptEntityInput(light,"brightness");
    AcceptEntityInput(light,BuildingPowered(b) ? "TurnOn" : "TurnOff");
}

void StopPowerEffects()
{
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        gBuildings[b].powered=false;int effect=Resolve(gBuildings[b].effectRef);
        if(effect>0 && gBuildings[b].kind==Kind_Light)AcceptEntityInput(effect,"TurnOff");
    }
    bool changed[MAX_CORES];
    for(int c=0;c<gCoreCount;c++)changed[c]=gGridProduction[c]!=0.0 || gGridDemand[c]!=0.0 || gGridAllocated[c]!=0.0 || gGridOffline[c]!=0;
    ResetPowerState();
    for(int c=0;c<gCoreCount;c++)if(changed[c])NotifyPowerChanged(c);
}

void PowerTick()
{
    for(int b=0;b<MAX_BUILDINGS;b++)
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
    char line[192],profile[128],state[64];BuildingProfilePath(b,profile,sizeof(profile));
    if(gBuildings[b].kind==Kind_Generator)
    {
        float output;ReadLevelFloat(gBuildings[b].file,profile,level,"power_output",0.0,output);
        Format(line,sizeof(line),"%T","GeneratorPowerInfo",client,GeneratorOutput(b),gBuildings[b].powerOutput,output);StrCat(title,length,line);
    }
    else if(gBuildings[b].powerDemand>0.0 || gBuildings[b].kind==Kind_Light)
    {
        float demand;ReadLevelFloat(gBuildings[b].file,profile,level,"power_required",0.0,demand);
        TranslateLabel(client,BuildingPowered(b) ? "PowerOnline" : "PowerOffline",state,sizeof(state));
        Format(line,sizeof(line),"%T","ConsumerPowerInfo",client,state,gBuildings[b].powerDemand,demand);StrCat(title,length,line);
    }
    AppendGridInfo(client,gBuildings[b].powerCore,title,length);
}
