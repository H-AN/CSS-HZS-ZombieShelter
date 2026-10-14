bool Spend(const int cost[MATERIALS])
{
    for(int m=0;m<MATERIALS;m++)if(cost[m]<0 || cost[m]>gInventory[m])return false;
    for(int m=0;m<MATERIALS;m++)gInventory[m]-=cost[m];return true;
}

void Refund(const int cost[MATERIALS])
{
    for(int m=0;m<MATERIALS;m++)Grant(m,cost[m]);
}

int Grant(int material,int amount)
{
    if(material<0 || material>=MATERIALS || amount<=0)return 0;
    int maximum=gInventoryCap>0 ? gInventoryCap : 2000000000;
    int actual=amount;if(actual>maximum-gInventory[material])actual=maximum-gInventory[material];
    gInventory[material]+=actual;return actual;
}

int WeightedMaterial(ConfigFile file,const char[] path)
{
    if(!Node(file,path))return -1;
    float weights[MATERIALS],total;
    for(int m=0;m<MATERIALS;m++)
    {
        weights[m]=gCfg[file].GetFloat(gMaterials[m].id,0.0);
        if(weights[m]<0.0)return -1;total+=weights[m];
    }
    if(total<=0.0)return -1;
    float roll=GetRandomFloat(0.0,total);
    for(int m=0;m<MATERIALS;m++){roll-=weights[m];if(roll<=0.0 && weights[m]>0.0)return m;}
    for(int m=MATERIALS-1;m>=0;m--)if(weights[m]>0.0)return m;
    return -1;
}

int CreateBody(const char[] model,const float origin[3],const float angles[3],const char[] name,bool dynamic=false)
{
    if(GetEntityCount()>GetMaxEntities()-128)return -1;
    int body=CreateEntityByName(dynamic ? "prop_dynamic_override" : "prop_physics_override");if(body==-1)return -1;
    DispatchKeyValue(body,"model",model);DispatchKeyValue(body,"spawnflags",dynamic ? "0" : "8");
    if(dynamic)DispatchKeyValue(body,"solid","6");
    DispatchKeyValue(body,"physdamagescale","0");DispatchKeyValue(body,"targetname",name);
    DispatchKeyValueVector(body,"origin",origin);DispatchKeyValueVector(body,"angles",angles);
    if(!DispatchSpawn(body)){RemoveEntity(body);return -1;}
    if(!dynamic)AcceptEntityInput(body,"DisableMotion");SetEntProp(body,Prop_Data,"m_takedamage",2,1);
    return body;
}

void RemoveOwnedRef(int ref)
{
    int entity=Resolve(ref);if(entity>0)RemoveEntity(entity);
}

void LevelPath(int building,char[] path,int size,int level=0)
{
    if(level==0)level=gBuildings[building].level;
    if(gBuildings[building].kind==Kind_Base)Format(path,size,"Profiles/%s/Levels/%d",gCores[gBuildings[building].core].profile,level);
    else Format(path,size,"Types/%s/Levels/%d",gBuildings[building].type,level);
}

bool ApplyBuildingLevel(int index,bool initializing)
{
    char path[192];LevelPath(index,path,sizeof(path));ConfigFile file=gBuildings[index].file;
    int maximum=Number(file,path,"max_hp",0);if(maximum<1)return false;
    int missing=gBuildings[index].maxHP-gBuildings[index].hp;
    gBuildings[index].maxHP=maximum;gBuildings[index].hp=initializing ? Number(file,path,"initial_hp",maximum) : maximum-missing;
    if(gBuildings[index].hp<1)gBuildings[index].hp=1;if(gBuildings[index].hp>maximum)gBuildings[index].hp=maximum;
    gBuildings[index].damage=Decimal(file,path,"damage",0.0);gBuildings[index].range=Decimal(file,path,"range",0.0);
    gBuildings[index].interval=Decimal(file,path,"fire_interval",0.5);gBuildings[index].halfAngle=Decimal(file,path,"cone_half_angle_degrees",30.0);
    gBuildings[index].burnDamage=Number(file,path,"burn_damage",0);gBuildings[index].burnInterval=Decimal(file,path,"burn_interval",0.5);
    gBuildings[index].burnDuration=Decimal(file,path,"burn_duration",0.0);
    if(gBuildings[index].kind==Kind_Mine)gBuildings[index].range=Decimal(file,path,"blast_radius",180.0);
    return gBuildings[index].interval>=0.1 && gBuildings[index].burnInterval>=0.1;
}

bool SpawnBuildingBody(int index)
{
    char target[64];Format(target,sizeof(target),"hzsh_build_%d_%d",gBuildings[index].uid,++gBuildings[index].generation);
    int entity=CreateBody(gBuildings[index].model,gBuildings[index].origin,gBuildings[index].angles,target);
    if(entity<0)return false;
    gBuildings[index].ref=EntIndexToEntRef(entity);gBuildings[index].nextFire=GetGameTime()+0.5;
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
        if(gBuildings[i].used && gBuildings[i].alive && !SpawnBuildingBody(i)){gRebuilding=false;return false;}
    for(int n=0;n<gNodeCount;n++)
    {
        if(!restore)
        {
            char path[128];Format(path,sizeof(path),"Pools/%s/Weights",gNodes[n].pool);
            int material=WeightedMaterial(C_Resources,path);if(material<0){gRebuilding=false;return false;}
            gNodes[n].material=material;gNodes[n].remaining=gMaterials[material].quota;
        }
        if(!SpawnResourceBody(n)){gRebuilding=false;return false;}
    }
    gRebuilding=false;return true;
}

bool SpawnResourceBody(int node)
{
    int material=gNodes[node].material;
    int entity=CreateBody(gMaterials[material].model,gNodes[node].origin,gNodes[node].angles,gNodes[node].id,true);
    if(entity<0)return false;
    gNodes[node].ref=EntIndexToEntRef(entity);
    if(entity<sizeof(gWorldBinding)){gWorldBinding[entity]=-(node+1);gWorldBindingRef[entity]=gNodes[node].ref;}
    SetEntPropFloat(entity,Prop_Send,"m_flModelScale",gMaterials[material].scale);
    SetEntityRenderMode(entity,RENDER_TRANSCOLOR);
    SDKHook(entity,SDKHook_OnTakeDamage,ResourceDamageHook);UpdateResourceVisual(node);
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
        if(Resolve(gNodes[i].ref)<0){if(!SpawnResourceBody(i))continue;}
        if(!gNodes[i].growing)continue;
        int m=gNodes[i].material;gNodes[i].elapsed+=elapsed;
        if(gNodes[i].elapsed>=gMaterials[m].hold+gMaterials[m].grow)
        {
            gNodes[i].growing=false;gNodes[i].remaining=gMaterials[m].quota;gNodes[i].elapsed=0.0;
        }
        UpdateResourceVisual(i);
    }
}

bool KnifeDamage(int attacker,int damageType)
{
    if(!LiveHuman(attacker) || !(damageType&(DMG_SLASH|DMG_CLUB)))return false;
    int weapon=GetEntPropEnt(attacker,Prop_Send,"m_hActiveWeapon");if(weapon<=MaxClients || !IsValidEntity(weapon))return false;
    char classname[64];GetEntityClassname(weapon,classname,sizeof(classname));return StrEqual(classname,"weapon_knife");
}

public Action ResourceDamageHook(int entity,int &attacker,int &inflictor,float &damage,int &damageType)
{
    int n=FindNode(entity);damage=0.0;
    if(n<0 || !Running() || gNodes[n].growing || !KnifeDamage(attacker,damageType))return Plugin_Handled;
    float now=GetGameTime();if(now<gNextGather[attacker] || AimedEntity(attacker,gUseDistance)!=entity)return Plugin_Handled;
    int m=gNodes[n].material,amount=Grant(m,gMaterials[m].yield);
    if(amount<=0){Tell(attacker,"材料仓库已满。");return Plugin_Handled;}
    gNextGather[attacker]=now+gGatherCooldown;gNodes[n].remaining--;
    if(gNodes[n].remaining<=0){gNodes[n].growing=true;gNodes[n].elapsed=0.0;UpdateResourceVisual(n);}
    Tell(attacker,"获得%s ×%d，剩余可采%d次。",gMaterials[m].label,amount,gNodes[n].remaining);
    return Plugin_Handled;
}

public Action BuildingDamageHook(int entity,int &attacker,int &inflictor,float &damage,int &damageType)
{
    int b=FindBuilding(entity);damage=0.0;
    if(b<0 || !gBuildings[b].alive || !Running() || !KnifeDamage(attacker,damageType))return Plugin_Handled;
    float now=GetGameTime();if(now<gNextRepair[attacker] || AimedEntity(attacker,gUseDistance)!=entity)return Plugin_Handled;
    int repair=Number(C_Bases,"Profiles/shelter_basic","repair_hp_per_hit",10);
    if(gBuildings[b].hp<gBuildings[b].maxHP)
    {
        gNextRepair[attacker]=now+gRepairCooldown;gBuildings[b].hp+=repair;
        if(gBuildings[b].hp>gBuildings[b].maxHP)gBuildings[b].hp=gBuildings[b].maxHP;
        Tell(attacker,"修复：%d / %d",gBuildings[b].hp,gBuildings[b].maxHP);
    }
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

void DamageBuilding(int b,int damage)
{
    if(!Running() || b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive || damage<1)return;
    gBuildings[b].hp-=damage;if(gBuildings[b].hp>0)return;
    gBuildings[b].hp=0;gBuildings[b].alive=false;int ref=gBuildings[b].ref;gBuildings[b].ref=INVALID_ENT_REFERENCE;RemoveOwnedRef(ref);
    if(gBuildings[b].kind==Kind_Base)
    {
        char text[128];Format(text,sizeof(text),"基地%s已被摧毁，请转移到其他基地！",gCores[gBuildings[b].core].id);Announce(text);
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
    if(!Running() || !LiveHuman(client) || b<0 || !gBuildings[b].alive || Resolve(gBuildings[b].ref)<0)return false;
    int oldLevel=gBuildings[b].level;char path[192];LevelPath(b,path,sizeof(path),oldLevel+1);
    if(!Node(gBuildings[b].file,path)){Tell(client,"此设施已经达到最高等级。");return false;}
    int cost[MATERIALS];if(!gCfg[gBuildings[b].file].JumpToKey("upgrade_cost")){Tell(client,"未配置升级配方。");return false;}
    for(int m=0;m<MATERIALS;m++)cost[m]=gCfg[gBuildings[b].file].GetNum(gMaterials[m].id,0);
    if(!Spend(cost)){Tell(client,"升级材料不足。");return false;}
    gBuildings[b].level++;
    if(!ApplyBuildingLevel(b,false)){gBuildings[b].level=oldLevel;Refund(cost);return false;}
    gBuildings[b].upgraded=true;Tell(client,"升级到%d级，耐久%d / %d。",gBuildings[b].level,gBuildings[b].hp,gBuildings[b].maxHP);return true;
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
    int probe=CreateBody(model,origin,angles,"hzsh_placement_probe",true);if(probe<0)return false;
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
    if(SummonCount()>=gSummonLimit){Tell(client,"团队设施数量已达上限%d。",gSummonLimit);return false;}
    int slot=-1;for(int i=gCoreCount;i<MAX_BUILDINGS;i++)if(!gBuildings[i].used || !gBuildings[i].alive){slot=i;break;}
    if(slot<0)return false;
    char path[128],model[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s",gRecipes[recipe].type);Text(gRecipes[recipe].file,path,"model",model,sizeof(model));
    float origin[3],angles[3];if(!Placement(client,model,origin,angles)){Tell(client,"此处空间不足、地面不平或位于出生保护区，请调整位置。");return false;}
    if(!Spend(gRecipes[recipe].cost)){Tell(client,"建造材料不足。");return false;}
    Building value;value.used=true;value.alive=true;value.kind=gRecipes[recipe].kind;value.uid=++gUID;value.core=-1;
    value.file=gRecipes[recipe].file;value.level=1;value.ref=INVALID_ENT_REFERENCE;value.origin=origin;value.angles=angles;
    strcopy(value.type,sizeof(value.type),gRecipes[recipe].type);strcopy(value.model,sizeof(value.model),model);gBuildings[slot]=value;
    if(!ApplyBuildingLevel(slot,true) || !SpawnBuildingBody(slot)){gBuildings[slot].used=false;Refund(gRecipes[recipe].cost);return false;}
    Tell(client,"已建造%s；瞄准设施按E可升级。",gRecipes[recipe].label);return true;
}
