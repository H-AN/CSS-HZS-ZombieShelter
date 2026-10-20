// Fixed residual energy from a plasma impact. No force, slow or NPC status is
// attached: every pulse examines each eligible NPC's current position again.
#define MAX_PLASMA_FIELDS 8
#define PLASMA_FIELD_VISUAL_INTERVAL 0.25
#define PLASMA_FIELD_VISUAL_ARCS 4

enum struct PlasmaField
{
    bool used;
    int token;
    int epoch;
    int revision;
    int source;
    int sourceUID;
    int sourceGeneration;
    int sourceRef;
    int visualRef;
    int damage;
    float position[3];
    float radius;
    float interval;
    float expires;
    float nextPulse;
    float nextVisual;
}
PlasmaField gPlasmaFields[MAX_PLASMA_FIELDS];
RewardContext gPlasmaFieldReward[MAX_PLASMA_FIELDS];
int gPlasmaFieldToken,gPlasmaFieldCoreSprite;
char gPlasmaFieldCoreMaterial[PLATFORM_MAX_PATH];
bool gPlasmaFieldTicking;

bool ReadPlasmaFieldLevel(const char[] profile,int level,int &damage,float &duration,float &radius,float &interval)
{
    float amount;
    if(!ReadLevelFloat(C_Plasma,profile,level,"field_damage",0.0,amount)
        || !ReadLevelFloat(C_Plasma,profile,level,"field_duration",0.0,duration)
        || !ReadLevelFloat(C_Plasma,profile,level,"field_radius",96.0,radius)
        || !ReadLevelFloat(C_Plasma,profile,level,"field_interval",0.5,interval))return false;
    damage=RoundToNearest(amount);
    // Old custom profiles without these additions remain impact-only. An
    // explicitly active field must be able to deliver at least one real pulse.
    return (amount==0.0 && duration==0.0)
        || (amount>=1.0 && amount<=100000.0 && damage>=1 && duration>=interval && duration<=30.0
            && radius>0.0 && radius<=512.0 && interval>=0.1 && interval<=5.0);
}

bool ValidatePlasmaFieldProfile(const char[] type)
{
    char profile[128],path[160];Format(profile,sizeof(profile),"Types/%s",type);Format(path,sizeof(path),"%s/Levels",profile);
    int levels[MAX_RECIPES],count;
    if(Node(C_Plasma,path) && gCfg[C_Plasma].GotoFirstSubKey())do
    {
        char key[32];gCfg[C_Plasma].GetSectionName(key,sizeof(key));
        if(count>=MAX_RECIPES || !ParseLevelInteger(key,levels[count]) || levels[count]<1)return false;
        count++;
    }while(gCfg[C_Plasma].GotoNextKey());
    int damage;float duration,radius,interval;
    for(int i=0;i<count;i++)if(!ReadPlasmaFieldLevel(profile,levels[i],damage,duration,radius,interval))return false;
    bool growth;int anchor,maximum;
    return count>0 && ReadLevelGrowth(C_Plasma,profile,growth,anchor,maximum)
        && (!growth || ReadPlasmaFieldLevel(profile,maximum,damage,duration,radius,interval));
}

bool SnapshotPlasmaField(int b,ExpansionPlasma shot)
{
    char profile[128];Format(profile,sizeof(profile),"Types/%s",gBuildings[b].type);
    if(!ReadPlasmaFieldLevel(profile,gBuildings[b].level,shot.fieldDamage,shot.fieldDuration,shot.fieldRadius,shot.fieldInterval))return false;
    shot.source=b;shot.sourceUID=gBuildings[b].uid;shot.sourceGeneration=gBuildings[b].generation;
    shot.revision=gBuildings[b].configRevision;
    return true;
}

void PrecachePlasmaField()
{
    // HL2 stock dark energy sprite when shipped by this game; the already used
    // combine-ball glow is the native fallback. No PCF or download is installed.
    strcopy(gPlasmaFieldCoreMaterial,sizeof(gPlasmaFieldCoreMaterial),"materials/sprites/combineball_glow_blue_1.vmt");
    gPlasmaFieldCoreSprite=gExpansionBallGlow;
    if(FileExists("materials/sprites/strider_blackball.vmt",true))
    {
        int sprite=PrecacheModel("materials/sprites/strider_blackball.vmt",true);
        if(sprite>0){gPlasmaFieldCoreSprite=sprite;strcopy(gPlasmaFieldCoreMaterial,sizeof(gPlasmaFieldCoreMaterial),"materials/sprites/strider_blackball.vmt");}
    }
}

int PlasmaFieldCount()
{
    int count;for(int f=0;f<MAX_PLASMA_FIELDS;f++)if(gPlasmaFields[f].used)count++;return count;
}

bool PlasmaFieldSourceValid(int source,int uid,int generation,int ref,int epoch,int revision,RewardContext reward)
{
    return Running() && epoch==gEpoch && revision==gConfigRevision && reward.valid && reward.facility && reward.epoch==epoch
        && reward.sourceUID==uid && reward.sourceRef==ref && BuildingOperational(source)
        && gBuildings[source].kind==Kind_Plasma && gBuildings[source].uid==uid && gBuildings[source].generation==generation
        && gBuildings[source].ref==ref && gBuildings[source].configRevision==revision && Resolve(ref)>0
        && gBuildings[source].ownerSerial==reward.ownerSerial && StrEqual(gBuildings[source].ownerKey,reward.ownerKey);
}

bool PlasmaFieldIdentity(int f,int token)
{
    return f>=0 && f<MAX_PLASMA_FIELDS && gPlasmaFields[f].used && gPlasmaFields[f].token==token
        && PlasmaFieldSourceValid(gPlasmaFields[f].source,gPlasmaFields[f].sourceUID,gPlasmaFields[f].sourceGeneration,
            gPlasmaFields[f].sourceRef,gPlasmaFields[f].epoch,gPlasmaFields[f].revision,gPlasmaFieldReward[f]);
}

void RemovePlasmaField(int f)
{
    if(f<0 || f>=MAX_PLASMA_FIELDS)return;
    int ref=gPlasmaFields[f].visualRef;
    // Consume the logical identity before RemoveEntity can forward/reenter.
    PlasmaField empty;gPlasmaFields[f]=empty;RewardContext reward;gPlasmaFieldReward[f]=reward;
    RemoveOwnedRef(ref);
}

void ClearPlasmaFields()
{
    for(int f=0;f<MAX_PLASMA_FIELDS;f++)if(gPlasmaFields[f].used)RemovePlasmaField(f);
    // Never clear the in-flight guard from a damage callback. Its outer tick
    // owns it, even if the entire campaign was cleared inside that callback.
}

void CreatePlasmaFieldVisual(int f,int token)
{
    if(!PlasmaFieldIdentity(f,token) || gPlasmaFieldCoreSprite<=0 || !CanAllocateEntities(1,true))return;
    int entity=CreateEntityByName("env_sprite");if(entity<=MaxClients || !IsValidEntity(entity))return;
    int ref=EntIndexToEntRef(entity);
    if(!PlasmaFieldIdentity(f,token)){RemoveOwnedRef(ref);return;}
    gPlasmaFields[f].visualRef=ref;
    char name[64];Format(name,sizeof(name),"hzsh_plasma_field_%d_%d",gEpoch,token);
    DispatchKeyValue(entity,"targetname",name);DispatchKeyValue(entity,"model",gPlasmaFieldCoreMaterial);
    DispatchKeyValue(entity,"spawnflags","1");
    // strider_blackball's stock VMT declares Sprite render mode 8. Preserve
    // that alpha mode rather than making a dark core additive and invisible.
    DispatchKeyValue(entity,"rendermode",StrContains(gPlasmaFieldCoreMaterial,"strider_blackball")>=0 ? "8" : "5");
    DispatchKeyValue(entity,"rendercolor","100 160 255");DispatchKeyValue(entity,"renderamt","230");DispatchKeyValue(entity,"scale","0.45");
    bool spawned=DispatchSpawn(entity);
    if(!spawned || !PlasmaFieldIdentity(f,token) || Resolve(ref)!=entity || !CanAllocateEntities(1,true))
    {
        // The optional sprite can be omitted under budget pressure. The field
        // remains visible through its bounded, short-lived temporary effects.
        if(gPlasmaFields[f].used && gPlasmaFields[f].token==token)gPlasmaFields[f].visualRef=INVALID_ENT_REFERENCE;
        RemoveOwnedRef(ref);return;
    }
    SetEntityMoveType(entity,MOVETYPE_NONE);SetEntProp(entity,Prop_Data,"m_takedamage",0,1);
    TeleportEntity(entity,gPlasmaFields[f].position,NULL_VECTOR,NULL_VECTOR);
}

bool StartPlasmaField(ExpansionPlasma shot,RewardContext reward,const float position[3],float now)
{
    if(PlasmaFieldCount()>=MAX_PLASMA_FIELDS || shot.fieldDamage<1 || shot.fieldDamage>100000 || !(shot.fieldDuration>=shot.fieldInterval && shot.fieldDuration<=30.0)
        || !(shot.fieldRadius>0.0 && shot.fieldRadius<=512.0) || !(shot.fieldInterval>=0.1 && shot.fieldInterval<=5.0)
        || !PlasmaFieldSourceValid(shot.source,shot.sourceUID,shot.sourceGeneration,shot.sourceRef,shot.epoch,shot.revision,reward))return false;
    for(int a=0;a<3;a++)if(!(position[a]>=-15000.0 && position[a]<=15000.0))return false;
    int slot=-1;for(int f=0;f<MAX_PLASMA_FIELDS;f++)if(!gPlasmaFields[f].used){slot=f;break;}if(slot<0)return false;
    if(gPlasmaFieldToken>=2000000000)gPlasmaFieldToken=0;
    PlasmaField field;field.used=true;field.token=++gPlasmaFieldToken;field.epoch=shot.epoch;field.revision=shot.revision;
    field.source=shot.source;field.sourceUID=shot.sourceUID;field.sourceGeneration=shot.sourceGeneration;field.sourceRef=shot.sourceRef;
    field.visualRef=INVALID_ENT_REFERENCE;field.damage=shot.fieldDamage;field.position=position;
    field.radius=shot.fieldRadius;field.interval=shot.fieldInterval;field.expires=now+shot.fieldDuration;
    field.nextPulse=now;field.nextVisual=now;
    // Reserve this slot before any engine spawn callback. Overlapping fields
    // retain independent fixed origins, expiries and shot-level parameters.
    gPlasmaFields[slot]=field;gPlasmaFieldReward[slot]=reward;
    CreatePlasmaFieldVisual(slot,field.token);
    return PlasmaFieldIdentity(slot,field.token);
}

void DrawPlasmaField(int f,int token,float now)
{
    if(!PlasmaFieldIdentity(f,token))return;
    float center[3];center=gPlasmaFields[f].position;
    int color[4]={80,150,255,200};float radius=gPlasmaFields[f].radius;
    if(gExpansionArc>0)
    {
        TE_SetupBeamRingPoint(center,radius*2.0,8.0,gExpansionArc,gExpansionHalo,0,12,0.30,4.0,1.0,color,12,0);TE_SendToAll();
        if(!PlasmaFieldIdentity(f,token))return;
        // Four curved, rotating spokes suggest an energy vortex. Only the
        // visuals move; the actual damage sphere never follows an NPC.
        for(int arc=0;arc<PLASMA_FIELD_VISUAL_ARCS;arc++)
        {
            float angle=now*3.0+float(arc)*1.57079633,start[3],end[3];
            start[0]=center[0]+Cosine(angle)*radius;start[1]=center[1]+Sine(angle)*radius;start[2]=center[2];
            end[0]=center[0]+Cosine(angle+0.75)*radius*0.2;end[1]=center[1]+Sine(angle+0.75)*radius*0.2;end[2]=center[2]+12.0;
            TE_SetupBeamPoints(start,end,gExpansionArc,0,0,12,0.30,4.0,1.5,0,2.0,color,12);TE_SendToAll();
            if(!PlasmaFieldIdentity(f,token))return;
        }
    }
    if(Resolve(gPlasmaFields[f].visualRef)<0 && gPlasmaFieldCoreSprite>0)
    {TE_SetupGlowSprite(center,gPlasmaFieldCoreSprite,0.30,0.45,220);TE_SendToAll();}
}

void TickPlasmaFields(float now)
{
    if(gPlasmaFieldTicking)return;
    gPlasmaFieldTicking=true;
    int tokens[MAX_PLASMA_FIELDS];
    for(int f=0;f<MAX_PLASMA_FIELDS;f++)if(gPlasmaFields[f].used)tokens[f]=gPlasmaFields[f].token;
    for(int f=0;f<MAX_PLASMA_FIELDS;f++)
    {
        // Fields created by a reentrant damage forward wait for the next real
        // timer invocation, even when they reuse a still-unvisited array slot.
        int token=tokens[f];if(!token || !gPlasmaFields[f].used || gPlasmaFields[f].token!=token)continue;
        if(!PlasmaFieldIdentity(f,token) || now>=gPlasmaFields[f].expires)
        {RemovePlasmaField(f);continue;}
        if(now+0.00001>=gPlasmaFields[f].nextVisual)
        {gPlasmaFields[f].nextVisual=now+PLASMA_FIELD_VISUAL_INTERVAL;DrawPlasmaField(f,token,now);}
        if(!PlasmaFieldIdentity(f,token) || now+0.00001<gPlasmaFields[f].nextPulse)continue;
        // Advance before crossing the HZS damage boundary: no recursive second
        // pulse or catch-up burst, even after an unusually late server frame.
        gPlasmaFields[f].nextPulse=now+gPlasmaFields[f].interval;
        float position[3];position=gPlasmaFields[f].position;
        float radiusSquared=gPlasmaFields[f].radius*gPlasmaFields[f].radius;
        int damage=gPlasmaFields[f].damage;RewardContext reward;reward=gPlasmaFieldReward[f];
        for(int n=0;n<MAX_NPCS;n++)
        {
            if(!PlasmaFieldIdentity(f,token))break;
            if(!DefenseTarget(n) || gNPCs[n].paused || gNPCs[n].registrationEpoch!=gEpoch)continue;
            int ref=gNPCs[n].ref,entity=Resolve(ref);float point[3];EntityCenter(entity,point);
            if(GetVectorDistance(position,point,true)>radiusSquared || !LineClear(position,point,-1,entity))continue;
            if(!PlasmaFieldIdentity(f,token))break;
            if(gNPCs[n].ref!=ref || !DefenseTarget(n) || gNPCs[n].paused || gNPCs[n].registrationEpoch!=gEpoch)continue;
            DamageNPCWithContext(n,damage,reward);
        }
    }
    gPlasmaFieldTicking=false;
}
