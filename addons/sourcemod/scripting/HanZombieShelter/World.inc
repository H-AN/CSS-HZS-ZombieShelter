bool ReadResourceWeights(ConfigFile file,const char[] path,float weights[MATERIALS],float &total)
{
    total=0.0;for(int m=0;m<MATERIALS;m++)weights[m]=0.0;
    if(!Node(file,path) || !gCfg[file].GotoFirstSubKey(false))return false;
    int seen;float largest;
    do
    {
        char key[64],raw[128];gCfg[file].GetSectionName(key,sizeof(key));
        int material=-1;for(int m=0;m<MATERIALS;m++)if(StrEqual(key,gMaterials[m].id)){material=m;break;}
        if(material<0 || (seen&(1<<material)) || gCfg[file].GetDataType(NULL_STRING)==KvData_None)return false;
        seen|=1<<material;gCfg[file].GetString(NULL_STRING,raw,sizeof(raw));TrimString(raw);
        float value;
        if(!raw[0] || StringToFloatEx(raw,value)!=strlen(raw) || value<0.0
            || (view_as<int>(value)&0x7f800000)==0x7f800000)return false;
        weights[material]=value;if(value>largest)largest=value;
    }while(gCfg[file].GotoNextKey(false));
    if(largest<=0.0)return false;
    // Scale before summation: even very large finite relative weights cannot
    // overflow the total into infinity. Zero weight is never eligible.
    for(int m=0;m<MATERIALS;m++)
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
    for(int m=0;m<MATERIALS;m++){roll-=weights[m];if(roll<=0.0 && weights[m]>0.0)return m;}
    for(int m=MATERIALS-1;m>=0;m--)if(weights[m]>0.0)return m;
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
        || !ReadLevelFloat(file,profile,level,"burn_duration",0.0,next.burnDuration))return false;
    if(next.kind==Kind_Mine && !ReadLevelFloat(file,profile,level,"blast_radius",180.0,next.range))return false;
    gBuildings[index]=next;return true;
}

bool SpawnBuildingBody(int index)
{
    char target[64];Format(target,sizeof(target),"hzsh_build_%d_%d",gBuildings[index].uid,++gBuildings[index].generation);
    int entity=CreateBody(gBuildings[index].model,gBuildings[index].origin,gBuildings[index].angles,target,false,gBuildings[index].kind!=Kind_Base);
    if(entity<0)return false;
    gBuildings[index].ref=EntIndexToEntRef(entity);gBuildings[index].nextFire=GetGameTime()+0.5;gBuildings[index].lastAttackAt=0.0;
    float worldMin[3],worldMax[3];ModelWorldBounds(entity,gBuildings[index].origin,gBuildings[index].angles,worldMin,worldMax);
    for(int a=0;a<3;a++){gBuildings[index].restoreMins[a]=worldMin[a]-gBuildings[index].origin[a];gBuildings[index].restoreMaxs[a]=worldMax[a]-gBuildings[index].origin[a];}
    gBuildings[index].boundsKnown=true;
    if(entity<sizeof(gWorldBinding)){gWorldBinding[entity]=index+1;gWorldBindingRef[entity]=gBuildings[index].ref;}
    SDKHook(entity,SDKHook_OnTakeDamage,BuildingDamageHook);SDKHook(entity,SDKHook_Touch,BuildingTouch);
    return true;
}

void CleanWorld()
{
    for(int i=0;i<MAX_BUILDINGS;i++)
    {
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
            value.origin=gCores[c].origin;value.angles=gCores[c].angles;strcopy(value.model,sizeof(value.model),gCores[c].model);
            gBuildings[b]=value;if(!ApplyBuildingLevel(b,true)){gRebuilding=false;return false;}
        }
        gCores[c].building=b;
        if(gBuildings[b].alive && !SpawnBuildingBody(b)){gRebuilding=false;return false;}
    }
    for(int i=gCoreCount;i<MAX_BUILDINGS;i++)
        if(gBuildings[i].used && gBuildings[i].alive)
        {
            if(!gBuildings[i].boundsKnown || !ClearHull(gBuildings[i].origin,gBuildings[i].restoreMins,gBuildings[i].restoreMaxs,-1,MASK_NPCSOLID|MASK_PLAYERSOLID)
                || !SpawnBuildingBody(i))LogMessage("[Shelter] defense UID %d awaiting safe entity capacity or clear placement",gBuildings[i].uid);
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
    gRebuilding=false;return true;
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
        if(!gBuildings[b].boundsKnown || !ClearHull(gBuildings[b].origin,gBuildings[b].restoreMins,gBuildings[b].restoreMaxs,-1,MASK_NPCSOLID|MASK_PLAYERSOLID))continue;
        if(!SpawnBuildingBody(b))return;
    }
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
    if(gBuildings[b].hp<gBuildings[b].maxHP)
    {
        int missing=gBuildings[b].maxHP-gBuildings[b].hp;if(repair>missing)repair=missing;
        if(repair<1)return Plugin_Handled;
        int before=gBuildings[b].hp;gBuildings[b].hp+=repair;
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

void DamageBuilding(int b,int damage,bool attacked=false)
{
    if(!Running() || b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive || damage<1)return;
    if(attacked)gBuildings[b].lastAttackAt=GetGameTime();
    gBuildings[b].hp-=damage;if(gBuildings[b].hp>0)return;
    PlayInteractionSound("destroyed",Resolve(gBuildings[b].ref),gBuildings[b].origin);
    gBuildings[b].hp=0;gBuildings[b].alive=false;int ref=gBuildings[b].ref;gBuildings[b].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
    if(gBuildings[b].kind==Kind_Base)
    {
        char label[2];label[0]='A'+gBuildings[b].core;label[1]='\0';AnnouncePhrase("BaseDestroyed",label);
        if(AliveCores()==0)QueueOutcome(false);
    }
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
    for(int m=0;m<MATERIALS;m++)
    {
        if(cost[m]<0 || gBuildings[b].invested[m]<0 || gBuildings[b].invested[m]>LEVEL_CELL_LIMIT
            || cost[m]>LEVEL_CELL_LIMIT-gBuildings[b].invested[m])return false;
    }
    if(!Spend(cost,client)){Tell(client,"%t","UpgradePersonalMaterialsMissing");return false;}
    Building previous;previous=gBuildings[b];
    gBuildings[b].level++;
    if(!ApplyBuildingLevel(b,false)){gBuildings[b]=previous;Refund(cost,client);return false;}
    for(int m=0;m<MATERIALS;m++)gBuildings[b].invested[m]+=cost[m];
    gBuildings[b].upgraded=true;PlayInteractionSound("build_complete",Resolve(gBuildings[b].ref),gBuildings[b].origin);
    Tell(client,"%t","FacilityUpgraded",gBuildings[b].level,gBuildings[b].hp,gBuildings[b].maxHP);return true;
}

bool Placement(int client,const char[] model,float origin[3],float angles[3])
{
    float eye[3],direction[3],view[3],end[3];GetClientEyePosition(client,eye);GetClientEyeAngles(client,view);GetAngleVectors(view,direction,NULL_VECTOR,NULL_VECTOR);
    for(int a=0;a<3;a++)end[a]=eye[a]+direction[a]*256.0;
    Handle trace=TR_TraceRayFilterEx(eye,end,MASK_SOLID,RayType_EndPoint,FilterExcept,client);
    if(!TR_DidHit(trace) || TR_StartSolid(trace)){delete trace;return false;}
    float normal[3];TR_GetPlaneNormal(trace,normal);TR_GetEndPosition(origin,trace);delete trace;
    if(normal[2]<0.93)return false;
    angles[0]=0.0;angles[1]=view[1];angles[2]=0.0;
    origin[2]+=2.0;
    int probe=CreateBody(model,origin,angles,"hzsh_placement_probe",true,true);if(probe<0)return false;
    AcceptEntityInput(probe,"DisableCollision");
    SetEntityRenderMode(probe,RENDER_TRANSCOLOR);SetEntityRenderColor(probe,255,255,255,0);
    float worldMin[3],worldMax[3];ModelWorldBounds(probe,origin,angles,worldMin,worldMax);
    float mins[3],maxs[3];for(int a=0;a<3;a++){mins[a]=worldMin[a]-origin[a];maxs[a]=worldMax[a]-origin[a];}
    float lift=-mins[2]+0.5;origin[2]+=lift;worldMin[2]+=lift;worldMax[2]+=lift;
    bool clear=ClearHull(origin,mins,maxs,probe,MASK_PLAYERSOLID) && !ReservedBounds(worldMin,worldMax);
    float human[3];GetClientAbsOrigin(client,human);if(GetVectorDistance(human,origin)<48.0)clear=false;
    RemoveEntity(probe);return clear;
}

bool BuildRecipe(int client,int recipe)
{
    if(!Running() || !LiveHuman(client) || recipe<0 || recipe>=gRecipeCount)return false;
    if(!CheckConstructionBudget(client))return false;
    if(SummonCount()>=gSummonLimit){Tell(client,"%t","DefenseLimitReached",gSummonLimit);return false;}
    int required;
    if(gRecipes[recipe].kind==Kind_Amplifier && !CanBuildAmplifier(gRecipes[recipe].type,1,required))
    {Tell(client,"%t","AmplifierBaseLevelRequired",required);return false;}
    int slot=-1;for(int i=gCoreCount;i<MAX_BUILDINGS;i++)if(!gBuildings[i].used || !gBuildings[i].alive){slot=i;break;}
    if(slot<0)return false;
    char path[128],model[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s",gRecipes[recipe].type);Text(gRecipes[recipe].file,path,"model",model,sizeof(model));
    float origin[3],angles[3];if(!Placement(client,model,origin,angles)){Tell(client,"%t","BuildPlacementUnavailable");return false;}
    if(!CheckConstructionBudget(client))return false;
    if(!Spend(gRecipes[recipe].cost,client)){Tell(client,"%t","BuildPersonalMaterialsMissing");return false;}
    Building value;value.used=true;value.alive=true;value.kind=gRecipes[recipe].kind;value.uid=++gUID;value.core=-1;
    value.file=gRecipes[recipe].file;value.level=1;value.ref=INVALID_ENT_REFERENCE;value.origin=origin;value.angles=angles;
    strcopy(value.type,sizeof(value.type),gRecipes[recipe].type);strcopy(value.model,sizeof(value.model),model);gBuildings[slot]=value;
    AssignBuildingOwner(slot,client);
    for(int m=0;m<MATERIALS;m++)gBuildings[slot].invested[m]=gRecipes[recipe].cost[m];
    if(!ApplyBuildingLevel(slot,true) || !SpawnBuildingBody(slot)){gBuildings[slot].used=false;Refund(gRecipes[recipe].cost,client);return false;}
    PlayInteractionSound("build_place",Resolve(gBuildings[slot].ref),gBuildings[slot].origin);
    char label[128];TranslateLabel(client,gRecipes[recipe].label,label,sizeof(label));
    Tell(client,"%t","DefenseBuilt",label);return true;
}
