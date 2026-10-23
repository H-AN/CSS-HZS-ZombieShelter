// The world timer remains at 0.25 s. Each living station has its own deadline;
// downtime never accrues extra pulses and effects require an actual change.
int gRestorationPulseCount,gRestorationTargetCount;

bool IsRestorationKind(BuildingKind kind)
{
    return kind==Kind_AutoRepair || kind==Kind_Heal || kind==Kind_Armor;
}

int RestorationCount(int core,BuildingKind kind,int exclude=-1)
{
    if(core<0 || core>=gCoreCount || !IsRestorationKind(kind))return 0;
    int count;
    for(int b=0;b<MAX_BUILDINGS;b++)if(b!=exclude && gBuildings[b].used && gBuildings[b].alive
        && gBuildings[b].kind==kind && gBuildings[b].buildCore==core)count++;
    return count; // A logical facility awaiting its saved body still occupies its slot.
}

stock int RestorationBuildLimit(ConfigFile file,const char[] type,int level=1)
{
    if(file!=C_Repair && file!=C_Heal && file!=C_Armor)return 0;
    return SupportBuildLimit(file,type,level); // The build action and menus share this resolver.
}

bool RestorationReady(int b)
{
    if(b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive
        || !IsRestorationKind(gBuildings[b].kind) || gBuildings[b].hp<1
        || !gBuildings[b].gridAssigned || gBuildings[b].powerCore!=gBuildings[b].buildCore
        || !BuildCoreAlive(gBuildings[b].buildCore) || !BuildingPowered(b))return false;
    int base=gCores[gBuildings[b].buildCore].building;
    return base>=0 && base<MAX_BUILDINGS && Resolve(gBuildings[base].ref)>0;
}

bool RestorationSafeContains(int core,const float position[3])
{
    if(core<0 || core>=gCoreCount)return false;
    float local[3];LocalPoint(position,gCores[core].origin,gCores[core].angles,local);
    for(int box=0;box<gCores[core].boxCount;box++)
        if(InBox(local,gCoreMins[core][box],gCoreMaxs[core][box]))return true;
    return false; // Membership is per base, including overlapping green boxes.
}

bool RestorationHuman(int client,int core)
{
    if(!LiveHuman(client) || IsFakeClient(client) || IsClientSourceTV(client) || IsClientReplay(client))return false;
    float position[3];GetClientAbsOrigin(client,position);
    return RestorationSafeContains(core,position);
}

int RestorationHealthLimit(int client)
{
    int maximum;
    if(HasEntProp(client,Prop_Data,"m_iMaxHealth"))maximum=GetEntProp(client,Prop_Data,"m_iMaxHealth");
    if(maximum<1 && HasEntProp(client,Prop_Send,"m_iMaxHealth"))maximum=GetEntProp(client,Prop_Send,"m_iMaxHealth");
    if(maximum>0)return maximum;
    // Some player classes omit this property. Existing bonus HP is never reduced.
    int current=GetClientHealth(client);return current>100 ? current : 100;
}

int RestorationDelta(int current,int maximum,int amount)
{
    if(current<0 || maximum<1 || amount<1 || current>=maximum)return 0;
    int missing=maximum-current;return amount<missing ? amount : missing;
}

// Shared capped recovery; effects remain the responsibility of the source.
int RestorationArmorLimit(int client){return SkillsArmorLimit(client);}

int RestorationRestoreHealth(int client,int amount)
{
    if(!LiveHuman(client))return 0;
    int health=GetClientHealth(client),delta=RestorationDelta(health,RestorationHealthLimit(client),amount);
    if(delta>0)SetEntityHealth(client,health+delta);return delta;
}

int RestorationRestoreArmor(int client,int amount)
{
    if(!LiveHuman(client))return 0;
    int armor=GetEntProp(client,Prop_Send,"m_ArmorValue"),delta=RestorationDelta(armor,RestorationArmorLimit(client),amount);
    if(delta>0)SetEntProp(client,Prop_Send,"m_ArmorValue",armor+delta);return delta;
}

int RestorationRestoreBuilding(int building,int amount)
{
    if(!BuildingOperational(building) || Resolve(gBuildings[building].ref)<0)return 0;
    int delta=RestorationDelta(gBuildings[building].hp,gBuildings[building].maxHP,amount);
    if(delta>0)gBuildings[building].hp+=delta;return delta;
}

bool RestorationRepairTarget(int station,int target)
{
    return station>=0 && station<MAX_BUILDINGS && BuildingOperational(target) && gBuildings[target].kind!=Kind_Campfire
        && gBuildings[target].hp>0 && gBuildings[target].hp<gBuildings[target].maxHP
        && gBuildings[target].buildCore==gBuildings[station].buildCore && Resolve(gBuildings[target].ref)>0
        && BuildAreaContains(gBuildings[station].buildCore,gBuildings[target].placementAnchor);
}

float RestorationExtent(int b)
{
    int core=gBuildings[b].buildCore;float extent=32.0;
    if(gBuildings[b].kind==Kind_AutoRepair)
    {
        for(int p=0;p<4;p++)
        {
            float x=gBuildCorners[core][p][0]-gBuildings[b].origin[0],y=gBuildCorners[core][p][1]-gBuildings[b].origin[1];
            float distance=SquareRoot(x*x+y*y);if(distance>extent)extent=distance;
        }
    }
    else for(int box=0;box<gCores[core].boxCount;box++)for(int p=0;p<4;p++)
    {
        float local[3],world[3];local[0]=(p==0 || p==3) ? gCoreMins[core][box][0] : gCoreMaxs[core][box][0];
        local[1]=p<2 ? gCoreMins[core][box][1] : gCoreMaxs[core][box][1];
        WorldPoint(local,gCores[core].origin,gCores[core].angles,world);
        float x=world[0]-gBuildings[b].origin[0],y=world[1]-gBuildings[b].origin[1];
        float distance=SquareRoot(x*x+y*y);if(distance>extent)extent=distance;
    }
    return extent;
}

void EmitRestorationPulse(int b,int targets)
{
    int color[4]={255,235,150,190};
    if(gBuildings[b].kind==Kind_Heal){color[0]=145;color[1]=255;color[2]=170;}
    else if(gBuildings[b].kind==Kind_Armor){color[0]=150;color[1]=205;color[2]=255;}
    float center[3];center=gBuildings[b].origin;center[2]+=16.0;
    TE_SetupBeamRingPoint(center,8.0,RestorationExtent(b)*2.0,gBeamSprite,0,0,0,0.7,8.0,0.0,color,0,0);TE_SendToAll();
    if(gBuildings[b].meta.pulseSound[0])
        EmitAmbientSound(gBuildings[b].meta.pulseSound,center,Resolve(gBuildings[b].ref),SNDLEVEL_NORMAL,SND_NOFLAGS,0.65);
    gRestorationPulseCount++;gRestorationTargetCount+=targets;
}

int RestorationPulse(int b)
{
    if(!RestorationReady(b) || gBuildings[b].restoreAmount<1)return 0;
    RefreshBuildAreas();int targets,amount=gBuildings[b].restoreAmount,core=gBuildings[b].buildCore;bool powerChanged;
    if(gBuildings[b].kind==Kind_AutoRepair)
    {
        for(int target=0;target<MAX_BUILDINGS;target++)
        {
            if(!RestorationRepairTarget(b,target))continue;
            int delta=RestorationRestoreBuilding(target,amount);
            if(delta<1)continue;
            targets++;
            if(gBuildings[target].kind==Kind_Generator)powerChanged=true;
        }
    }
    else for(int client=1;client<=MaxClients;client++)
    {
        if(!RestorationHuman(client,core))continue;
        if(gBuildings[b].kind==Kind_Heal)
        {
            int delta=RestorationRestoreHealth(client,amount);
            if(delta<1)continue;
            targets++;
        }
        else
        {
            int delta=RestorationRestoreArmor(client,amount);
            if(delta<1)continue;
            targets++;
        }
    }
    if(targets>0)EmitRestorationPulse(b,targets);
    if(powerChanged)RecalculatePower();
    return targets;
}

void RestorationTick(float now)
{
    if(!Running())return;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!gBuildings[b].used || !gBuildings[b].alive || !IsRestorationKind(gBuildings[b].kind))continue;
        float interval=gBuildings[b].restoreInterval;
        if(!(interval>=1.0 && interval<=3600.0))interval=interval>3600.0 ? 3600.0 : 1.0;
        if(gBuildings[b].nextRestore<=0.0){gBuildings[b].nextRestore=now+interval;continue;}
        if(now<gBuildings[b].nextRestore)continue;
        gBuildings[b].nextRestore=now+interval; // Advance even when offline or nobody needs recovery.
        RestorationPulse(b);
    }
}
