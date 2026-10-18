bool ReadResourceWeights(ConfigFile file,const char[] path,float weights[MATERIALS],float &total)
{
    total=0.0;for(int m=0;m<gMaterialCount;m++)weights[m]=0.0;
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey(false))return false;
    bool seen[MATERIALS];float largest;
    do
    {
        char key[64],raw[128];gCfg[file].GetSectionName(key,sizeof(key));
        int material=-1;for(int m=0;m<gMaterialCount;m++)if(StrEqual(key,gMaterials[m].id)){material=m;break;}
        if(material<0 || !gMaterials[material].gatherable || seen[material] || gCfg[file].GetDataType(NULL_STRING)==KvData_None)return false;
        seen[material]=true;gCfg[file].GetString(NULL_STRING,raw,sizeof(raw));TrimString(raw);
        float value;
        if(!raw[0] || StringToFloatEx(raw,value)!=strlen(raw) || value<0.0
            || (view_as<int>(value)&0x7f800000)==0x7f800000)return false;
        weights[material]=value;if(value>largest)largest=value;
    }while(gCfg[file].GotoNextKey(false));
    if(largest<=0.0)return false;
    // Scale before summation: even very large finite relative weights cannot
    // overflow the total into infinity. Zero weight is never eligible.
    for(int m=0;m<gMaterialCount;m++)
    {
        weights[m]/=largest;total+=weights[m];
    }
    return total>0.0;
}

bool ValidateResourcePools()
{
    if(!Node(C_Resources,"Pools") || !gCfg[C_Resources].GotoFirstSubKey())return false;
    ArrayList pools=new ArrayList(ByteCountToCells(64));
    do
    {
        char id[64];gCfg[C_Resources].GetSectionName(id,sizeof(id));pools.PushString(id);
    }while(gCfg[C_Resources].GotoNextKey());
    bool valid=true;
    for(int p=0;p<pools.Length && valid;p++)
    {
        char id[64],path[160],flag[16];pools.GetString(p,id,sizeof(id));Format(path,sizeof(path),"Pools/%s",id);
        Text(C_Resources,path,"reroll_on_regrow",flag,sizeof(flag),"1");
        if(!StrEqual(flag,"0") && !StrEqual(flag,"1")){valid=false;break;}
        float weights[MATERIALS],total;Format(path,sizeof(path),"Pools/%s/Weights",id);
        if(!ReadResourceWeights(C_Resources,path,weights,total)){valid=false;break;}
        Format(path,sizeof(path),"Pools/%s/RegrowWeights",id);
        if(Node(C_Resources,path) && !ReadResourceWeights(C_Resources,path,weights,total)){valid=false;break;}
    }
    for(int n=0;n<gNodeCount && valid;n++)
    {
        bool found;
        for(int p=0;p<pools.Length;p++)
        {char id[64];pools.GetString(p,id,sizeof(id));if(StrEqual(id,gNodes[n].pool)){found=true;break;}}
        if(!found)valid=false;
    }
    delete pools;return valid;
}

int WeightedMaterial(ConfigFile file,const char[] path)
{
    float weights[MATERIALS],total;if(!ReadResourceWeights(file,path,weights,total))return -1;
    float roll=GetRandomFloat(0.0,total);
    for(int m=0;m<gMaterialCount;m++){roll-=weights[m];if(roll<=0.0 && weights[m]>0.0)return m;}
    for(int m=gMaterialCount-1;m>=0;m--)if(weights[m]>0.0)return m;
    return -1;
}

int CreateBody(const char[] model,const float origin[3],const float angles[3],const char[] name,bool dynamic=false,bool optional=false)
{
    if(!CanAllocateEntities(1,optional))return -1;
    int body=CreateEntityByName(dynamic ? "prop_dynamic_override" : "prop_physics_override");if(body==-1)return -1;
    DispatchKeyValue(body,"model",model);DispatchKeyValue(body,"spawnflags",dynamic ? "0" : "8");
    if(dynamic)DispatchKeyValue(body,"solid","6");
    DispatchKeyValue(body,"physdamagescale","0");DispatchKeyValue(body,"targetname",name);
    DispatchKeyValueVector(body,"origin",origin);DispatchKeyValueVector(body,"angles",angles);
    if(!DispatchSpawn(body)){RemoveEntity(body);return -1;}
    if(!CanAllocateEntities(1,optional)){RemoveEntity(body);return -1;}
    if(!dynamic)AcceptEntityInput(body,"DisableMotion");SetEntProp(body,Prop_Data,"m_takedamage",2,1);
    return body;
}

void RemoveOwnedRef(int ref)
{
    int entity=Resolve(ref);if(entity>0)RemoveEntity(entity);
}

stock void LevelPath(int building,char[] path,int size,int level=0)
{
    if(level==0)level=gBuildings[building].level;
    if(gBuildings[building].kind==Kind_Base)Format(path,size,"Profiles/%s/Levels/%d",gCores[gBuildings[building].core].profile,level);
    else Format(path,size,"Types/%s/Levels/%d",gBuildings[building].type,level);
}

bool ApplyBuildingLevel(int index,bool initializing)
{
    if(index<0 || index>=MAX_BUILDINGS || !gBuildings[index].used)return false;
    char profile[128];BuildingProfilePath(index,profile,sizeof(profile));
    ConfigFile file=gBuildings[index].file;int level=gBuildings[index].level,maximum,initialHP;
    if(!ReadLevelInt(file,profile,level,"max_hp",0,maximum)
        || !ReadLevelInt(file,profile,level,"initial_hp",maximum,initialHP) || initialHP>maximum)return false;
    Building next;next=gBuildings[index];
    int previousHP=next.hp;if(previousHP<0)previousHP=0;if(previousHP>next.maxHP)previousHP=next.maxHP;
    int missing=next.maxHP-previousHP;if(missing<0)missing=0;
    next.maxHP=maximum;next.hp=initializing ? initialHP : (missing>=maximum ? 1 : maximum-missing);
    if(!ReadLevelFloat(file,profile,level,"damage",0.0,next.damage)
        || !ReadLevelFloat(file,profile,level,"range",0.0,next.range)
        || !ReadLevelFloat(file,profile,level,"fire_interval",0.5,next.interval)
        || !ReadLevelFloat(file,profile,level,"cone_half_angle_degrees",30.0,next.halfAngle)
        || !ReadLevelInt(file,profile,level,"burn_damage",0,next.burnDamage)
        || !ReadLevelFloat(file,profile,level,"burn_interval",0.5,next.burnInterval)
        || !ReadLevelFloat(file,profile,level,"burn_duration",0.0,next.burnDuration)
        || !ReadLevelFloat(file,profile,level,"power_output",0.0,next.powerOutput)
        || !ReadLevelFloat(file,profile,level,"power_required",0.0,next.powerDemand)
        || !ReadLevelInt(file,profile,level,"power_priority",10,next.powerPriority)
        || !ReadLevelFloat(file,profile,level,"light_radius",500.0,next.lightRadius)
        || !ReadLevelInt(file,profile,level,"light_brightness",2,next.lightBrightness)
        || !ReadLevelFloat(file,profile,level,"projectile_speed",900.0,next.projectileSpeed)
        || !ReadLevelFloat(file,profile,level,"blast_radius",180.0,next.blastRadius)
        || !ReadLevelFloat(file,profile,level,"build_range_bonus",0.0,next.buildRangeBonus)
        || !ReadLevelInt(file,profile,level,"build_limit_bonus",0,next.buildLimitBonus)
        || !ReadLevelInt(file,profile,level,"restore_amount",0,next.restoreAmount)
        || !ReadLevelFloat(file,profile,level,"restore_interval_seconds",20.0,next.restoreInterval))return false;
    if(IsBaseSupportKind(next.kind))
    {
        next.maxPerBase=SupportBuildLimit(file,next.type,level);if(next.maxPerBase<1)return false;
        next.nextRestore=GetGameTime()+next.restoreInterval;
        next.powerCore=next.buildCore;next.gridAssigned=true;
    }
    if(next.kind==Kind_Base)
    {
        if(!ReadLevelInt(file,profile,level,"build_limit",24,next.baseBuildLimit)
            || !ReadBuildHalfExtents(file,profile,level,next.buildHalfExtents))return false;
    }
    if(next.kind==Kind_Mine && !ReadLevelFloat(file,profile,level,"blast_radius",180.0,next.range))return false;
    gBuildings[index]=next;InvalidateDefenseCache(index);MarkBuildAreasDirty();RecalculatePower();
    if(next.kind==Kind_Beacon)SyncBeaconMarker(index);
    return true;
}

bool SpawnBuildingBody(int index)
{
    char target[64];Format(target,sizeof(target),"hzsh_build_%d_%d",gBuildings[index].uid,++gBuildings[index].generation);
    int entity=CreateBody(gBuildings[index].model,gBuildings[index].origin,gBuildings[index].angles,target,false,gBuildings[index].kind!=Kind_Base);
    if(entity<0)return false;
    if(gBuildings[index].kind!=Kind_Base)
    {
        SetEntityRenderMode(entity,RENDER_TRANSCOLOR);
        SetEntityRenderColor(entity,gBuildings[index].meta.render[0],gBuildings[index].meta.render[1],gBuildings[index].meta.render[2],gBuildings[index].meta.render[3]);
        if(HasEntProp(entity,Prop_Send,"m_nSkin"))SetEntProp(entity,Prop_Send,"m_nSkin",gBuildings[index].meta.skin);
    }
    gBuildings[index].ref=EntIndexToEntRef(entity);gBuildings[index].nextFire=GetGameTime()+0.5;gBuildings[index].lastAttackAt=0.0;
    if(IsRestorationKind(gBuildings[index].kind))gBuildings[index].nextRestore=GetGameTime()+gBuildings[index].restoreInterval;
    float worldMin[3],worldMax[3];ModelWorldBounds(entity,gBuildings[index].origin,gBuildings[index].angles,worldMin,worldMax);
    for(int a=0;a<3;a++){gBuildings[index].restoreMins[a]=worldMin[a]-gBuildings[index].origin[a];gBuildings[index].restoreMaxs[a]=worldMax[a]-gBuildings[index].origin[a];}
    gBuildings[index].boundsKnown=true;
    if(entity<sizeof(gWorldBinding)){gWorldBinding[entity]=index+1;gWorldBindingRef[entity]=gBuildings[index].ref;}
    SDKHook(entity,SDKHook_OnTakeDamage,BuildingDamageHook);SDKHook(entity,SDKHook_Touch,BuildingTouch);
    if(gBuildings[index].kind==Kind_Light && !CreateBuildingLight(index))
    {
        int failedRef=gBuildings[index].ref;gBuildings[index].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(failedRef);return false;
    }
    InvalidateDefenseCache(index);MarkBuildAreasDirty();RecalculatePower();
    if(gBuildings[index].kind==Kind_Beacon)SyncBeaconMarker(index);
    return true;
}

void CleanWorld()
{
    ClearDefenseState();ClearGrenades();ClearBeaconMarkers();StopPowerEffects();MarkBuildAreasDirty();
    for(int i=0;i<MAX_BUILDINGS;i++)
    {
        RemoveBuildingEffect(i);
        int ref=gBuildings[i].ref;gBuildings[i].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
    }
    for(int i=0;i<gNodeCount;i++)
    {
        int ref=gNodes[i].ref;gNodes[i].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
    }
}

bool CreateWorld(bool restore)
{
    gRebuilding=true;
    for(int c=0;c<gCoreCount;c++)
    {
        int b=c;
        if(!restore)
        {
            Building value;value.used=true;value.alive=true;value.kind=Kind_Base;value.uid=++gUID;
            value.core=c;value.file=C_Bases;value.level=1;value.ref=INVALID_ENT_REFERENCE;
            value.buildCore=c;value.configRevision=gConfigRevision;value.placementAnchor=gCores[c].origin;
            value.origin=gCores[c].origin;value.angles=gCores[c].angles;strcopy(value.model,sizeof(value.model),gCores[c].model);
            gBuildings[b]=value;if(!ApplyBuildingLevel(b,true)){gRebuilding=false;return false;}
        }
        gCores[c].building=b;
        if(gBuildings[b].alive && !SpawnBuildingBody(b)){gRebuilding=false;return false;}
    }
    for(int i=gCoreCount;i<MAX_BUILDINGS;i++)
        if(gBuildings[i].used && gBuildings[i].alive)
        {
            RestoreSavedBuilding(i);
        }
    for(int n=0;n<gNodeCount;n++)
    {
        if(!restore)
        {
            char path[128];Format(path,sizeof(path),"Pools/%s/Weights",gNodes[n].pool);
            int material=WeightedMaterial(C_Resources,path);if(material<0){gRebuilding=false;return false;}
            gNodes[n].material=material;gNodes[n].remaining=gMaterials[material].quota;
            gNodes[n].growing=false;gNodes[n].rerollPending=false;gNodes[n].elapsed=0.0;
        }
        if(!SpawnResourceBody(n)){gRebuilding=false;return false;}
    }
    gRebuilding=false;MarkBuildAreasDirty();RecalculatePower();return true;
}

bool SpawnResourceBody(int node)
{
    if(!ResolveResourceReroll(node))return false;
    int material=gNodes[node].material;
    int entity=CreateBody(gMaterials[material].model,gNodes[node].origin,gNodes[node].angles,gNodes[node].id,true);
    if(entity<0)return false;
    gNodes[node].ref=EntIndexToEntRef(entity);
    if(entity<sizeof(gWorldBinding)){gWorldBinding[entity]=-(node+1);gWorldBindingRef[entity]=gNodes[node].ref;}
    ApplyResourceModel(node,entity);
    if(!gNodes[node].growing && ResourceSpaceOccupied(node,entity))
    {
        gNodes[node].growing=true;gNodes[node].remaining=0;
        gNodes[node].elapsed=gMaterials[material].hold+gMaterials[material].grow;
    }
    SDKHook(entity,SDKHook_OnTakeDamage,ResourceDamageHook);UpdateResourceVisual(node);
    return true;
}

bool ApplyResourceModel(int node,int entity)
{
    int material=gNodes[node].material;
    // SetModel refreshes the model bounds using the current scale. Writing
    // m_flModelScale afterwards leaves stale occupancy bounds on CS:S.
    SetEntPropFloat(entity,Prop_Send,"m_flModelScale",gMaterials[material].scale);
    SetEntityModel(entity,gMaterials[material].model);
    char scale[64];Format(scale,sizeof(scale),"%.6f 0 0",gMaterials[material].scale);
    SetVariantString(scale);bool nativeScale=AcceptEntityInput(entity,"SetModelScale");
    SetEntityRenderMode(entity,RENDER_TRANSCOLOR);
    return nativeScale;
}

bool ResolveResourceReroll(int node)
{
    if(!gNodes[node].rerollPending)return true;
    char path[160];Format(path,sizeof(path),"Pools/%s",gNodes[node].pool);
    if(Number(C_Resources,path,"reroll_on_regrow",1))
    {
        Format(path,sizeof(path),"Pools/%s/RegrowWeights",gNodes[node].pool);
        if(!Node(C_Resources,path))Format(path,sizeof(path),"Pools/%s/Weights",gNodes[node].pool);
        int next=WeightedMaterial(C_Resources,path);if(next<0)return false;
        gNodes[node].material=next;
    }
    // This bit belongs to the logical node, so reconstruction, maturity delays
    // and ordinary ticks cannot draw another result for the same depletion.
    gNodes[node].rerollPending=false;
    int entity=Resolve(gNodes[node].ref);
    if(entity>0)
    {
        AcceptEntityInput(entity,"DisableCollision");
        ApplyResourceModel(node,entity);UpdateResourceVisual(node);
    }
    return true;
}

void UpdateResourceVisual(int node)
{
    int entity=Resolve(gNodes[node].ref);if(entity<0)return;
    int m=gNodes[node].material,alpha=255;
    if(gNodes[node].growing)
    {
        float p=(gNodes[node].elapsed-gMaterials[m].hold)/gMaterials[m].grow;
        if(p<0.0)p=0.0;if(p>1.0)p=1.0;if(gMaterials[m].ease)p*=p;
        alpha=RoundToNearest(255.0*p);
    }
    SetEntityRenderColor(entity,gMaterials[m].rgb[0],gMaterials[m].rgb[1],gMaterials[m].rgb[2],alpha);
    // Native inputs update the collision partition as well as the solid flags.
    // Directly writing m_usSolidFlags left stale engine traces against physics props.
    AcceptEntityInput(entity,gNodes[node].growing ? "DisableCollision" : "EnableCollision");
}

void ResourceTick(float elapsed)
{
    for(int i=0;i<gNodeCount;i++)
    {
        if(!ResolveResourceReroll(i))continue;
        if(Resolve(gNodes[i].ref)<0){if(!SpawnResourceBody(i))continue;}
        if(!gNodes[i].growing)continue;
        int m=gNodes[i].material;gNodes[i].elapsed+=elapsed;
        if(gNodes[i].elapsed>=gMaterials[m].hold+gMaterials[m].grow && !ResourceSpaceOccupied(i,Resolve(gNodes[i].ref)))
        {
            gNodes[i].growing=false;gNodes[i].remaining=gMaterials[m].quota;gNodes[i].elapsed=0.0;
        }
        UpdateResourceVisual(i);
    }
}

bool ResourceSpaceOccupied(int node,int entity)
{
    if(entity<0)return true;
    // Source resource models can intentionally extend into the terrain. Check
    // actual actor/building occupancy, not terrain contact, before re-solidifying.
    float low[3],high[3];ModelWorldBounds(entity,gNodes[node].origin,gNodes[node].angles,low,high);
    for(int client=1;client<=MaxClients;client++)if(LiveHuman(client))
    {
        float origin[3],mins[3],maxs[3];GetClientAbsOrigin(client,origin);GetClientMins(client,mins);GetClientMaxs(client,maxs);
        for(int a=0;a<3;a++){mins[a]+=origin[a];maxs[a]+=origin[a];}
        if(BoxesOverlap(low,high,mins,maxs))return true;
    }
    for(int npc=0;npc<MAX_NPCS;npc++)if(gNPCs[npc].used && !gNPCs[npc].dead && !gNPCs[npc].paused)
    {
        int body=Resolve(gNPCs[npc].ref);if(body<0)continue;
        float origin[3],mins[3],maxs[3];GetEntPropVector(body,Prop_Data,"m_vecOrigin",origin);NPCBounds(body,mins,maxs);
        for(int a=0;a<3;a++){mins[a]+=origin[a];maxs[a]+=origin[a];}
        if(BoxesOverlap(low,high,mins,maxs))return true;
    }
    for(int b=0;b<MAX_BUILDINGS;b++)if(gBuildings[b].used && gBuildings[b].alive && Resolve(gBuildings[b].ref)>0)
    {
        float mins[3],maxs[3];for(int a=0;a<3;a++){mins[a]=gBuildings[b].origin[a]+gBuildings[b].restoreMins[a];maxs[a]=gBuildings[b].origin[a]+gBuildings[b].restoreMaxs[a];}
        if(BoxesOverlap(low,high,mins,maxs))return true;
    }
    return false;
}

void RestoreDeferredBuildings()
{
    if(!CanAllocateEntities(2,true))return;
    for(int b=gCoreCount;b<MAX_BUILDINGS;b++)
    {
        if(!gBuildings[b].used || !gBuildings[b].alive || Resolve(gBuildings[b].ref)>0)continue;
        if(GetGameTime()<gBuildings[b].restoreRetryAt)continue;
        RestoreSavedBuilding(b);
        if(!CanAllocateEntities(2,true))return;
    }
}

bool RestoreSavedBuilding(int b)
{
    // Saved transforms are final. Do not reapply offsets, charge materials, or
    // reject legal overlap / buildings left outside a shrunken build area.
    if(gBuildings[b].configRevision!=gConfigRevision)
    {
        gBuildings[b].restoreRetryAt=GetGameTime()+60.0;
        LogMessage("[Shelter] defense UID %d deferred: saved config revision %d differs from campaign %d",gBuildings[b].uid,gBuildings[b].configRevision,gConfigRevision);return false;
    }
    if(SpawnBuildingBody(b)){gBuildings[b].restoreRetryAt=0.0;return true;}
    gBuildings[b].restoreRetryAt=GetGameTime()+5.0;
    LogMessage("[Shelter] defense UID %d deferred: entity/model capacity unavailable",gBuildings[b].uid);return false;
}

bool KnifeDamage(int attacker,int damageType)
{
    // CS:S knife TraceAttack uses DMG_BULLET | DMG_NEVERGIB (4098).
    // Verify the actual active weapon; DMG_SLASH alone rejects real client swings.
    bool cssKnife=(damageType&(DMG_BULLET|DMG_NEVERGIB))==(DMG_BULLET|DMG_NEVERGIB);
    if(!LiveHuman(attacker) || (!(damageType&(DMG_SLASH|DMG_CLUB)) && !cssKnife))return false;
    int weapon=GetEntPropEnt(attacker,Prop_Send,"m_hActiveWeapon");if(weapon<=MaxClients || !IsValidEntity(weapon))return false;
    char classname[64];GetEntityClassname(weapon,classname,sizeof(classname));return StrEqual(classname,"weapon_knife");
}

public Action ResourceDamageHook(int entity,int &attacker,int &inflictor,float &damage,int &damageType)
{
    int n=FindNode(entity);damage=0.0;
    if(n<0 || !Running() || gNodes[n].growing || !KnifeDamage(attacker,damageType))return Plugin_Handled;
    // The engine has already traced this knife hit (including its swept hull).
    float now=GetGameTime();if(now<gNextGather[attacker])return Plugin_Handled;
    int m=gNodes[n].material,amount=Grant(m,gMaterials[m].yield,attacker);
    if(amount<=0){Tell(attacker,"%t","PersonalBackpackFull");return Plugin_Handled;}
    gNextGather[attacker]=now+gGatherCooldown;gNodes[n].remaining--;
    HudGatherFeedback(attacker,n,amount);
    PlayInteractionSound("gather",entity,gNodes[n].origin);
    if(gNodes[n].remaining<=0)
    {
        gNodes[n].remaining=0;gNodes[n].growing=true;gNodes[n].elapsed=0.0;gNodes[n].rerollPending=true;
        UpdateResourceVisual(n);
    }
    return Plugin_Handled;
}

public Action BuildingDamageHook(int entity,int &attacker,int &inflictor,float &damage,int &damageType)
{
    int b=FindBuilding(entity);damage=0.0;
    if(b<0 || !gBuildings[b].alive || !Running() || !KnifeDamage(attacker,damageType))return Plugin_Handled;
    float now=GetGameTime();if(now<gNextRepair[attacker])return Plugin_Handled;
    gNextRepair[attacker]=now+gRepairCooldown;
    int repair=Number(C_Bases,"Profiles/shelter_basic","repair_hp_per_hit",10);
    if(gBuildings[b].kind==Kind_Generator)
    {char profile[128];BuildingProfilePath(b,profile,sizeof(profile));repair=Number(C_Generators,profile,"repair_hp_per_hit",50);}
    if(gBuildings[b].hp<gBuildings[b].maxHP)
    {
        int missing=gBuildings[b].maxHP-gBuildings[b].hp;if(repair>missing)repair=missing;
        if(repair<1)return Plugin_Handled;
        int before=gBuildings[b].hp;gBuildings[b].hp+=repair;
        if(gBuildings[b].kind==Kind_Generator)RecalculatePower();
        HudRepairFeedback(attacker,b,gBuildings[b].hp-before);
        PlayInteractionSound("repair",entity,gBuildings[b].origin);
    }
    else HudRepairFeedback(attacker,b,0);
    return Plugin_Handled; // Friendly bullets and engine prop health never own logical HP.
}

public Action BuildingTouch(int entity,int other)
{
    if(!Running())return Plugin_Continue;
    int b=FindBuilding(entity),n=FindNPC(other);
    if(b>=0 && n>=0 && gBuildings[b].alive && gBuildings[b].kind!=Kind_Mine && gNPCs[n].active)
    {
        gNPCs[n].touchedUID=gBuildings[b].uid;gNPCs[n].touchedUntil=GetGameTime()+0.75;
    }
    return Plugin_Continue;
}

void DamageBuilding(int b,int damage,bool attacked=false,int attackerNPC=-1)
{
    if(!Running() || b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive || damage<1)return;
    FenceRetaliationEvent reply;
    if(attacked)CaptureFenceRetaliation(b,attackerNPC,reply);
    if(attacked)gBuildings[b].lastAttackAt=GetGameTime();
    gBuildings[b].hp-=damage;
    if(gBuildings[b].hp>0)
    {
        if(gBuildings[b].kind==Kind_Generator)RecalculatePower();
    }
    else
    {
        BuildingKind kind=gBuildings[b].kind;int core=gBuildings[b].core;
        int ref=gBuildings[b].ref,effectRef=gBuildings[b].effectRef;
        // Consume the old entity before sounds/removal/native callbacks can re-enter.
        gBuildings[b].hp=0;gBuildings[b].alive=false;gBuildings[b].ref=INVALID_ENT_REFERENCE;gBuildings[b].effectRef=INVALID_ENT_REFERENCE;
        InvalidateDefenseCache(b);MarkBuildAreasDirty();PlayBuildingSound(b,"destroy");
        if(effectRef!=0)RemoveOwnedRef(effectRef);
        RemoveOwnedRef(ref);RecalculatePower();
        if(kind==Kind_Base)
        {
            char label[2];label[0]='A'+core;label[1]='\0';AnnouncePhrase("BaseDestroyed",label);
            if(AliveCores()==0)QueueOutcome(false);
        }
    }
    // All data, including fixed base and owner, came from the attacking event.
    // No read of b occurs here: a destroyed fence's slot may already be reused.
    ApplyFenceRetaliation(reply);
}

int AliveCores()
{
    int count;for(int i=0;i<gCoreCount;i++){int b=gCores[i].building;if(b>=0 && gBuildings[b].alive)count++;}return count;
}

int SummonCount()
{
    int count;for(int i=0;i<MAX_BUILDINGS;i++)if(gBuildings[i].used && gBuildings[i].alive && gBuildings[i].kind!=Kind_Base)count++;return count;
}

bool TryUpgrade(int client,int b)
{
    if(!Running() || !LiveHuman(client) || b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive || Resolve(gBuildings[b].ref)<0)return false;
    if(!CanManageBuilding(client,b)){Tell(client,"%t","BuildingNotOwned");return false;}
    if(gBuildings[b].kind==Kind_Base)
    {float position[3];GetClientAbsOrigin(client,position);if(SafeCoreAt(position)!=gBuildings[b].core)return false;}
    else if(!BuildingInUseRange(client,b)){Tell(client,"%t","BuildingTooFar");return false;}
    int oldLevel=gBuildings[b].level;
    if(oldLevel>=LEVEL_CELL_LIMIT || !BuildingLevelAvailable(b,oldLevel+1)){Tell(client,"%t","FacilityMaximumLevel");return false;}
    int required;
    if(gBuildings[b].kind==Kind_Amplifier && !CanBuildAmplifier(gBuildings[b].type,oldLevel+1,required))
    {Tell(client,"%t","AmplifierBaseLevelRequired",required);return false;}
    int cost[MATERIALS],maximum;
    if(!ReadBuildingLevelQuote(b,oldLevel+1,cost,maximum)){Tell(client,"%t","FacilityUpgradeRecipeMissing");return false;}
    for(int m=0;m<gMaterialCount;m++)
    {
        if(cost[m]<0 || gBuildings[b].invested[m]<0 || gBuildings[b].invested[m]>LEVEL_CELL_LIMIT
            || cost[m]>LEVEL_CELL_LIMIT-gBuildings[b].invested[m])return false;
    }
    if(!Spend(cost,client)){Tell(client,"%t","UpgradePersonalMaterialsMissing");return false;}
    Building previous;previous=gBuildings[b];
    gBuildings[b].level++;
    if(!ApplyBuildingLevel(b,false)){gBuildings[b]=previous;Refund(cost,client);return false;}
    for(int m=0;m<gMaterialCount;m++)gBuildings[b].invested[m]+=cost[m];
    gBuildings[b].upgraded=true;PlayBuildingSound(b,"upgrade");
    Tell(client,"%t","FacilityUpgraded",gBuildings[b].level,gBuildings[b].hp,gBuildings[b].maxHP);return true;
}

bool PlacementError(int client,const char[] phrase,bool report)
{if(report)Tell(client,"%t",phrase);return false;}

void AnglesFromForwardUp(const float forwardVector[3],const float up[3],float angles[3])
{
    float left[3];GetVectorCrossProduct(up,forwardVector,left);NormalizeVector(left,left);
    GetVectorAngles(forwardVector,angles);
    angles[2]=RadToDeg(ArcTangent2(left[2],up[2]));
}

void ComposeModelAngles(const float frame[3],const float offset[3],float result[3])
{
    float localForward[3],localUp[3],forwardVector[3],up[3],zero[3];
    GetAngleVectors(offset,localForward,NULL_VECTOR,localUp);
    WorldPoint(localForward,zero,frame,forwardVector);WorldPoint(localUp,zero,frame,up);
    AnglesFromForwardUp(forwardVector,up,result);
}

void SurfacePlacementAngles(float yaw,const float normal[3],float frame[3])
{
    float up[3],forwardVector[3];up=normal;
    if(NormalizeVector(up,up)<0.001){frame[0]=0.0;frame[1]=yaw;frame[2]=0.0;return;}
    forwardVector[0]=Cosine(DegToRad(yaw));forwardVector[1]=Sine(DegToRad(yaw));
    float projection=GetVectorDotProduct(forwardVector,up);
    for(int a=0;a<3;a++)forwardVector[a]-=projection*up[a];
    if(GetVectorLength(forwardVector)<0.01)
    {
        float alternate[3];if(FloatAbs(up[2])<0.9)alternate[2]=1.0;else alternate[1]=1.0;
        GetVectorCrossProduct(alternate,up,forwardVector);
    }
    NormalizeVector(forwardVector,forwardVector);AnglesFromForwardUp(forwardVector,up,frame);
}

bool ComputePlacement(int client,const char[] model,BuildingMetadata meta,float anchor[3],float origin[3],float angles[3],bool report)
{
    if(!LiveHuman(client))return false;
    float eye[3],direction[3],view[3],end[3];GetClientEyePosition(client,eye);GetClientEyeAngles(client,view);GetAngleVectors(view,direction,NULL_VECTOR,NULL_VECTOR);
    for(int a=0;a<3;a++)end[a]=eye[a]+direction[a]*meta.maxDistance;
    Handle trace=TR_TraceRayFilterEx(eye,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
    if(!TR_DidHit(trace) || TR_StartSolid(trace) || TR_AllSolid(trace)){delete trace;return PlacementError(client,"BuildNoSurface",report);}
    int hit=TR_GetEntityIndex(trace);int surface=TR_GetSurfaceFlags(trace);float normal[3];TR_GetPlaneNormal(trace,normal);TR_GetEndPosition(anchor,trace);delete trace;
    if(surface&(SURF_SKY|SURF_SKY2D))return PlacementError(client,"BuildSkySurface",report);
    if(hit>0 && (hit<=MaxClients || FindNPC(hit)>=0 || (APIAvailable() && Han_IsZombie(hit))))return PlacementError(client,"BuildActorSurface",report);
    if(hit>MaxClients)
    {
        char classname[64];GetEntityClassname(hit,classname,sizeof(classname));
        if(StrEqual(classname,"hostage_entity"))return PlacementError(client,"BuildActorSurface",report);
        if(StrEqual(classname,"func_movelinear") || StrEqual(classname,"func_tracktrain"))return PlacementError(client,"BuildMovingSurface",report);
        char targetname[64];GetEntPropString(hit,Prop_Data,"m_iName",targetname,sizeof(targetname));
        if(StrContains(targetname,"hzsh_grenade_")==0)return PlacementError(client,"BuildMovingSurface",report);
        MoveType movement=GetEntityMoveType(hit);
        if(FindBuilding(hit)<0 && (movement==MOVETYPE_PUSH || movement==MOVETYPE_VPHYSICS
            || movement==MOVETYPE_FLY || movement==MOVETYPE_FLYGRAVITY || movement==MOVETYPE_STEP))return PlacementError(client,"BuildMovingSurface",report);
    }
    for(int a=0;a<3;a++)if(!(anchor[a]>=-15000.0 && anchor[a]<=15000.0))return PlacementError(client,"BuildInvalidWorld",report);
    if(GetVectorLength(normal)<0.5 || TR_PointOutsideWorld(anchor))return PlacementError(client,"BuildInvalidWorld",report);
    // Only the explicit NPC/Boss creation columns protect space. Model size and
    // human spawn candidates do not expand this anchor restriction.
    if(ReservedBounds(anchor,anchor))return PlacementError(client,"BuildSpawnProtected",report);
    float frame[3];frame[1]=view[1];
    if(StrEqual(meta.placementMode,"wall"))
    {
        if(FloatAbs(normal[2])>0.7)return PlacementError(client,"BuildInvalidSurface",report);
        GetVectorAngles(normal,frame);frame[2]=0.0;
    }
    else if(StrEqual(meta.placementMode,"surface"))
        SurfacePlacementAngles(view[1],normal,frame);
    ComposeModelAngles(frame,meta.angleOffset,angles);
    WorldPoint(meta.originOffset,anchor,angles,origin);
    for(int a=0;a<3;a++)if(!(origin[a]>=-15000.0 && origin[a]<=15000.0))return PlacementError(client,"BuildInvalidWorld",report);
    if(TR_PointOutsideWorld(origin))return PlacementError(client,"BuildInvalidWorld",report);
    if(!meta.allowOverlap)
    {
        int probe=CreateBody(model,origin,angles,"hzsh_placement_probe",true,true);
        if(probe<0)return PlacementError(client,"BuildEntityCreationFailed",report);
        AcceptEntityInput(probe,"DisableCollision");SetEntityRenderMode(probe,RENDER_TRANSCOLOR);SetEntityRenderColor(probe,255,255,255,0);
        float low[3],high[3],mins[3],maxs[3];ModelWorldBounds(probe,origin,angles,low,high);
        for(int a=0;a<3;a++){mins[a]=low[a]-origin[a];maxs[a]=high[a]-origin[a];}
        bool clear=ClearHull(origin,mins,maxs,probe,MASK_PLAYERSOLID);RemoveEntity(probe);
        if(!clear)return PlacementError(client,"BuildOverlapDenied",report);
    }
    return true;
}

bool PlacementForRecipe(int client,int recipe,float anchor[3],float origin[3],float angles[3],bool report=true)
{
    if(recipe<0 || recipe>=gRecipeCount)return false;
    char path[128],model[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s",gRecipes[recipe].type);
    Text(gRecipes[recipe].file,path,"model",model,sizeof(model));
    return ComputePlacement(client,model,gRecipes[recipe].meta,anchor,origin,angles,report);
}

// Kept for existing test helpers. Production uses the recipe-bound metadata.
stock bool Placement(int client,const char[] model,float origin[3],float angles[3])
{
    BuildingMetadata meta;strcopy(meta.placementMode,sizeof(meta.placementMode),"upright");meta.maxDistance=512.0;meta.allowOverlap=true;
    float anchor[3];return ComputePlacement(client,model,meta,anchor,origin,angles,false);
}

bool BuildRecipe(int client,int recipe,int expectedCore=-1)
{
    if(!Running() || !LiveHuman(client) || recipe<0 || recipe>=gRecipeCount)return false;
    if(!CheckConstructionBudget(client))return false;
    if(SummonCount()>=gSummonLimit){Tell(client,"%t","DefenseLimitReached",gSummonLimit);return false;}
    if(gRecipes[recipe].kind==Kind_Light && LightCount()>=gLightLimit){Tell(client,"%t","LightLimitReached",gLightLimit);return false;}
    int required;
    if(gRecipes[recipe].kind==Kind_Amplifier && !CanBuildAmplifier(gRecipes[recipe].type,1,required))
    {Tell(client,"%t","AmplifierBaseLevelRequired",required);return false;}
    int slot=-1;for(int i=gCoreCount;i<MAX_BUILDINGS;i++)if(!gBuildings[i].used || !gBuildings[i].alive){slot=i;break;}
    if(slot<0)return false;
    char path[128],model[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s",gRecipes[recipe].type);Text(gRecipes[recipe].file,path,"model",model,sizeof(model));
    float anchor[3],origin[3],angles[3];if(!PlacementForRecipe(client,recipe,anchor,origin,angles))return false;
    int buildCore=BuildCoreAt(anchor);
    if(expectedCore>=0 && buildCore!=expectedCore){Tell(client,"%t","BuildMenuTargetChanged");return false;}
    if(buildCore<0)
    {
        bool horizontal;for(int c=0;c<gCoreCount;c++)if(BuildAreaContains(c,anchor,false)){horizontal=true;break;}
        Tell(client,"%t",horizontal ? "BuildHeightOutside" : "BuildOutsideArea");return false;
    }
    if(!CheckBuildCapacity(client,buildCore))return false;
    if(IsBaseSupportKind(gRecipes[recipe].kind))
    {
        int limit=SupportBuildLimit(gRecipes[recipe].file,gRecipes[recipe].type,1);
        int count=gRecipes[recipe].kind==Kind_Beacon ? BeaconCount(buildCore) : RestorationCount(buildCore,gRecipes[recipe].kind);
        if(limit<1 || count>=limit){Tell(client,"%t","BuildSupportGroupLimitReached",count,limit);return false;}
    }
    if(!CheckConstructionBudget(client))return false;
    if(!Spend(gRecipes[recipe].cost,client)){Tell(client,"%t","BuildPersonalMaterialsMissing");return false;}
    Building value;value.used=true;value.alive=true;value.kind=gRecipes[recipe].kind;value.uid=++gUID;value.core=-1;
    value.file=gRecipes[recipe].file;value.level=1;value.ref=INVALID_ENT_REFERENCE;value.effectRef=INVALID_ENT_REFERENCE;value.origin=origin;value.angles=angles;
    value.buildCore=buildCore;value.configRevision=gConfigRevision;value.placementAnchor=anchor;value.meta=gRecipes[recipe].meta;
    value.powerCore=buildCore;value.gridAssigned=true;
    strcopy(value.type,sizeof(value.type),gRecipes[recipe].type);strcopy(value.model,sizeof(value.model),model);gBuildings[slot]=value;
    AssignBuildingOwner(slot,client);
    for(int m=0;m<gMaterialCount;m++)gBuildings[slot].invested[m]=gRecipes[recipe].cost[m];
    if(!ApplyBuildingLevel(slot,true) || !SpawnBuildingBody(slot))
    {
        RemoveBuildingEffect(slot);int failedRef=gBuildings[slot].ref;gBuildings[slot].ref=INVALID_ENT_REFERENCE;
        gBuildings[slot].used=false;gBuildings[slot].alive=false;RemoveOwnedRef(failedRef);
        Refund(gRecipes[recipe].cost,client);MarkBuildAreasDirty();RecalculatePower();Tell(client,"%t","BuildEntityCreationFailed");return false;
    }
    MarkBuildAreasDirty();RecalculatePower();PlayBuildingSound(slot,"build");
    char label[128];TranslateLabel(client,gRecipes[recipe].label,label,sizeof(label));
    Tell(client,"%t","DefenseBuilt",label);return true;
}
