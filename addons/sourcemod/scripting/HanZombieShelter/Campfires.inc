// A campfire grants exposure protection only. It never becomes a storage core,
// a build area, a grid, or a condition that keeps a lost campaign alive.
enum struct CampfireState
{
    int uid;
    int epoch;
    int revision;
    int level;
    bool initialized;
    float radius;
    float height;
    int decay;
    float interval;
    int threshold;
    int fuel;
    int fuelMaximum;
    int fuelPerWood;
    int fuelWoodCost;
    bool fuelInitialized;
    bool noDecay;
    bool burning;
    float nextDecay;
    float nextEffect;
}

CampfireState gCampfireState[MAX_BUILDINGS];
bool gCampfireFlame=true,gCampfireRing=true;
float gCampfireEffectInterval=1.0;
float gCampfireLastTick=-1.0;
int gCampfireClockEpoch=-1;
bool gCampfireRefuelBusy;

void ResetCampfireFuelState()
{
    CampfireState empty;
    for(int b=0;b<MAX_BUILDINGS;b++)gCampfireState[b]=empty;
    gCampfireLastTick=-1.0;gCampfireClockEpoch=gEpoch;gCampfireRefuelBusy=false;
}

void SuspendCampfires()
{
    for(int b=0;b<MAX_BUILDINGS;b++)gCampfireState[b].initialized=false;
    gCampfireLastTick=-1.0;gCampfireClockEpoch=gEpoch;
}

bool LoadCampfireSettings()
{
    SuspendCampfires();
    int flame,ring;bool found;
    if(!StoredLevelInt(C_Campfires,"Effects","flame",1,flame,found) || flame>1
        || !StoredLevelInt(C_Campfires,"Effects","safe_ring",1,ring,found) || ring>1
        || !StoredLevelFloat(C_Campfires,"Effects","interval_seconds",1.0,gCampfireEffectInterval,found)
        || gCampfireEffectInterval<0.25 || gCampfireEffectInterval>5.0)
        return ConfigFailure("campfire Effects: flame/safe_ring 0..1, interval 0.25..5 seconds");
    gCampfireFlame=flame!=0;gCampfireRing=ring!=0;return true;
}

bool ReadCampfireProfileLevel(const char[] profile,int level,float &radius,float &height,
    int &decayAmount,float &decayInterval,int &inactiveHP,bool &noDecay)
{
    int defaultNever,never;bool found;
    if(!StoredLevelInt(C_Campfires,profile,"no_decay_at_max_level",0,defaultNever,found) || defaultNever>1
        || !ReadLevelFloat(C_Campfires,profile,level,"safe_radius",160.0,radius)
        || !ReadLevelFloat(C_Campfires,profile,level,"safe_height",160.0,height)
        || !ReadLevelInt(C_Campfires,profile,level,"decay_amount",1,decayAmount)
        || !ReadLevelFloat(C_Campfires,profile,level,"decay_interval_seconds",1.0,decayInterval)
        || !ReadLevelInt(C_Campfires,profile,level,"no_decay_at_max_level",defaultNever,never))return false;
    // Kept in the read API for callers from older releases. Durability no
    // longer decides whether a surviving campfire provides heat.
    inactiveHP=0;
    int fuelMaximum,fuelPerWood,fuelWoodCost;
    if(!ReadCampfireFuelProfileLevel(profile,level,fuelMaximum,fuelPerWood,fuelWoodCost))return false;
    noDecay=decayAmount==0 || (never!=0 && level==ProfileMaximumLevel(C_Campfires,profile));
    return true;
}

bool ReadCampfireFuelProfileLevel(const char[] profile,int level,int &maximum,int &perWood,int &woodCost)
{
    int defaultMaximum,defaultPerWood,defaultWoodCost;bool found;
    if(!StoredLevelInt(C_Campfires,profile,"fuel_capacity",60,defaultMaximum,found) || defaultMaximum<1
        || !StoredLevelInt(C_Campfires,profile,"fuel_per_wood",10,defaultPerWood,found) || defaultPerWood<1
        || !StoredLevelInt(C_Campfires,profile,"fuel_wood_cost",1,defaultWoodCost,found) || defaultWoodCost<1)return false;
    return ReadLevelInt(C_Campfires,profile,level,"fuel_capacity",defaultMaximum,maximum)
        && ReadLevelInt(C_Campfires,profile,level,"fuel_per_wood",defaultPerWood,perWood)
        && ReadLevelInt(C_Campfires,profile,level,"fuel_wood_cost",defaultWoodCost,woodCost);
}

// Shared by recipe/upgrade descriptions and the focused server probe.
stock bool ReadCampfireLevel(ConfigFile file,const char[] type,int level,float &radius,
    int &decayAmount,float &decayInterval,int &inactiveHP,bool &noDecay)
{
    if(file!=C_Campfires)return false;
    char profile[128];Format(profile,sizeof(profile),"Types/%s",type);float height;
    return ReadCampfireProfileLevel(profile,level,radius,height,decayAmount,decayInterval,inactiveHP,noDecay);
}

bool PrepareCampfireState(int b,float now)
{
    if(b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || gBuildings[b].kind!=Kind_Campfire)return false;
    if(gCampfireState[b].initialized && gCampfireState[b].uid==gBuildings[b].uid
        && gCampfireState[b].epoch==gEpoch && gCampfireState[b].revision==gConfigRevision
        && gCampfireState[b].level==gBuildings[b].level)
    {
        if(!gCampfireState[b].fuelInitialized && BuildingOperational(b))
        {
            gCampfireState[b].fuel=gCampfireState[b].fuelMaximum;gCampfireState[b].fuelInitialized=true;
            gCampfireState[b].burning=!gCampfireState[b].noDecay;
            gCampfireState[b].nextDecay=now+gCampfireState[b].interval;
        }
        return true;
    }
    CampfireState state;
    char profile[128];BuildingProfilePath(b,profile,sizeof(profile));
    if(!ReadCampfireProfileLevel(profile,gBuildings[b].level,state.radius,state.height,
        state.decay,state.interval,state.threshold,state.noDecay)
        || !ReadCampfireFuelProfileLevel(profile,gBuildings[b].level,state.fuelMaximum,state.fuelPerWood,state.fuelWoodCost))return false;
    state.uid=gBuildings[b].uid;state.epoch=gEpoch;state.revision=gConfigRevision;state.level=gBuildings[b].level;
    // The logical UID survives body removal, round preparation and upgrades.
    // Those transitions must never mint free fuel; a new UID starts full only
    // once construction is complete. A reduced configured cap clamps in place.
    state.fuelInitialized=gCampfireState[b].uid==state.uid && gCampfireState[b].fuelInitialized;
    state.fuel=state.fuelInitialized ? gCampfireState[b].fuel : 0;
    if(!state.fuelInitialized && BuildingOperational(b)){state.fuel=state.fuelMaximum;state.fuelInitialized=true;}
    if(state.fuel>state.fuelMaximum)state.fuel=state.fuelMaximum;
    if(state.fuel<0)state.fuel=0;
    state.burning=state.fuel>0 && !state.noDecay;
    state.initialized=true;state.nextDecay=now+state.interval;state.nextEffect=now;
    gCampfireState[b]=state;return true;
}

void CampfireConstructionCompleted(int b)
{
    if(b>=0 && b<MAX_BUILDINGS && BuildingOperational(b) && gBuildings[b].kind==Kind_Campfire)
        PrepareCampfireState(b,GetGameTime());
}

int CampfireFuelCurrent(int b)
{
    return PrepareCampfireState(b,GetGameTime()) ? gCampfireState[b].fuel : 0;
}

int CampfireFuelMaximum(int b)
{
    return PrepareCampfireState(b,GetGameTime()) ? gCampfireState[b].fuelMaximum : 0;
}

int CampfireFuelPerWood(int b)
{
    return PrepareCampfireState(b,GetGameTime()) ? gCampfireState[b].fuelPerWood : 0;
}

int CampfireFuelWoodCost(int b)
{
    return PrepareCampfireState(b,GetGameTime()) ? gCampfireState[b].fuelWoodCost : 0;
}

bool CampfireActive(int b)
{
    return BuildingOperational(b) && gBuildings[b].kind==Kind_Campfire
        && Resolve(gBuildings[b].ref)>0 && PrepareCampfireState(b,GetGameTime())
        && gCampfireState[b].fuel>0;
}

// A single bounded batch consumes its entire configured cost, even when the
// campfire has only one unit of free space. The division avoids overflowing
// woodCost * fuelPerWood before clamping to that space.
int CampfireRefuelGain(int current,int maximum,int perWood,int woodCost)
{
    if(current<0 || maximum<1 || current>=maximum || perWood<1 || woodCost<1)return 0;
    int missing=maximum-current;
    return woodCost>missing/perWood ? missing : woodCost*perWood;
}

bool CanRefuelCampfire(int client,int b)
{
    return Running() && LiveHuman(client) && !IsFakeClient(client) && !IsClientSourceTV(client) && !IsClientReplay(client)
        && BuildingOperational(b) && gBuildings[b].kind==Kind_Campfire
        && gBuildings[b].configRevision==gConfigRevision && Resolve(gBuildings[b].ref)>0;
}

bool RefuelCampfire(int client,int b)
{
    if(gCampfireRefuelBusy || !CanRefuelCampfire(client,b))return false;
    int serial=GetClientSerial(client),epoch=gEpoch,revision=gConfigRevision;
    int uid=gBuildings[b].uid,ref=gBuildings[b].ref,generation=gBuildings[b].generation,knife;
    if(!HeldKnifeSlotWeapon(client,knife) || !ResourceMeleeHit(client,client) || !BuildingInUseRange(client,b)
        || !PrepareCampfireState(b,GetGameTime()))return false;
    int material=FindMaterial("wood");
    if(material<0)return false;
    int before=gCampfireState[b].fuel,maximum=gCampfireState[b].fuelMaximum;
    int woodCost=gCampfireState[b].fuelWoodCost,gain=CampfireRefuelGain(before,maximum,gCampfireState[b].fuelPerWood,woodCost);
    if(gain<1){Tell(client,"%t","CampfireFuelFull");return false;}
    if(gPlayerInventory[client][material]<woodCost){Tell(client,"%t","CampfireFuelNeedsWood",woodCost);return false;}
    int checkedKnife;
    if(gEpoch!=epoch || gConfigRevision!=revision || GetClientFromSerial(serial)!=client
        || !CanRefuelCampfire(client,b) || gBuildings[b].uid!=uid || gBuildings[b].ref!=ref || gBuildings[b].generation!=generation
        || !HeldKnifeSlotWeapon(client,checkedKnife) || checkedKnife!=knife || !ResourceMeleeHit(client,client)
        || !BuildingInUseRange(client,b) || gCampfireState[b].uid!=uid || gCampfireState[b].fuel!=before
        || gCampfireState[b].revision!=revision || gPlayerInventory[client][material]<woodCost)return false;
    // Commit both logical balances before messages or any engine effect. No
    // callback can observe one side of the exchange or charge it a second time.
    gCampfireRefuelBusy=true;
    gPlayerInventory[client][material]-=woodCost;gCampfireState[b].fuel=before+gain;
    if(before==0)
    {
        gCampfireState[b].burning=!gCampfireState[b].noDecay;
        gCampfireState[b].nextDecay=GetGameTime()+gCampfireState[b].interval;
        gCampfireState[b].nextEffect=GetGameTime();
    }
    gCampfireRefuelBusy=false;
    Tell(client,"%t","CampfireFueled",gain,gCampfireState[b].fuel,maximum,woodCost);return true;
}

bool CampfireSafeAt(const float position[3])
{
    if(!Running())return false;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!CampfireActive(b))continue;
        float x=position[0]-gBuildings[b].origin[0],y=position[1]-gBuildings[b].origin[1];
        if(FloatAbs(position[2]-gBuildings[b].origin[2])<=gCampfireState[b].height
            && x*x+y*y<=gCampfireState[b].radius*gCampfireState[b].radius)return true;
    }
    return false;
}

void EmitCampfireEffect(int b)
{
    float center[3];center=gBuildings[b].origin;center[2]+=18.0;
    // Existing CS:S sprites, already precached by the main plugin. Temporary
    // effects use no edicts, cause no fire damage and expire if the fire dies.
    float life=gCampfireEffectInterval+0.1;
    if(gCampfireFlame && gFireSprite>0)
    {TE_SetupGlowSprite(center,gFireSprite,life,0.45,200);TE_SendToAll();}
    if(gCampfireRing && gBeamSprite>0)
    {
        int color[4]={255,160,55,180};center[2]-=14.0;
        TE_SetupBeamRingPoint(center,gCampfireState[b].radius*2.0,gCampfireState[b].radius*2.0+1.0,
            gBeamSprite,0,0,0,life,2.0,0.0,color,0,0);TE_SendToAll();
    }
}

// Shared by the real world timer and deterministic fixture timelines. Advancing
// the old deadline avoids adding timer quantization/frame delay to every second.
void CampfireDecayAt(int b,float now)
{
    float interval=gCampfireState[b].interval;
    if(gCampfireState[b].noDecay || gCampfireState[b].decay<1 || gCampfireState[b].fuel<1)
    {
        gCampfireState[b].burning=false;gCampfireState[b].nextDecay=now+interval;return;
    }
    if(!gCampfireState[b].burning)
    {
        // A refueled ember or newly resumed fire starts a fresh interval. Cold
        // and disabled periods cannot become an immediate fuel penalty.
        gCampfireState[b].burning=true;gCampfireState[b].nextDecay=now+interval;return;
    }
    if(now<gCampfireState[b].nextDecay)return;
    int available=gCampfireState[b].fuel;
    int needed=(available-1)/gCampfireState[b].decay+1;
    float overdue=(now-gCampfireState[b].nextDecay)/interval;
    // Clamp before converting a potentially enormous elapsed period count to a
    // cell or multiplying. The non-clamped product is strictly below available.
    if(overdue>=float(needed-1))
    {
        gCampfireState[b].fuel=0;gCampfireState[b].burning=false;
        gCampfireState[b].nextDecay=now+interval;return;
    }
    int periods=RoundToFloor(overdue)+1;
    if(periods>=needed)
    {
        gCampfireState[b].fuel=0;gCampfireState[b].burning=false;
        gCampfireState[b].nextDecay=now+interval;return;
    }
    gCampfireState[b].fuel-=periods*gCampfireState[b].decay;
    gCampfireState[b].nextDecay+=float(periods)*interval;
    // At extremely long engine uptimes float resolution may exceed interval.
    // Never leave an already-expired deadline that could charge the same batch.
    if(gCampfireState[b].nextDecay<=now)gCampfireState[b].nextDecay=now+interval;
}

void CampfireTick(float elapsed)
{
    if(!Running() || elapsed<=0.0){SuspendCampfires();return;}
    float now=GetGameTime();
    // WorldTick continues measuring elapsed while the campaign is inactive but
    // skips this module. Detect that gap as well as explicit suspension/epoch.
    // A real delayed frame has the same larger elapsed and retains catch-up.
    if(gCampfireClockEpoch!=gEpoch || gCampfireLastTick<0.0 || now<gCampfireLastTick
        || now-gCampfireLastTick>elapsed+0.5)SuspendCampfires();
    gCampfireClockEpoch=gEpoch;gCampfireLastTick=now;
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!BuildingOperational(b) || gBuildings[b].kind!=Kind_Campfire || Resolve(gBuildings[b].ref)<0)
        {gCampfireState[b].initialized=false;continue;}
        if(!PrepareCampfireState(b,now))continue;
        CampfireDecayAt(b,now);
        if(gCampfireState[b].fuel>0 && now>=gCampfireState[b].nextEffect)
        {gCampfireState[b].nextEffect=now+gCampfireEffectInterval;EmitCampfireEffect(b);}
    }
}
