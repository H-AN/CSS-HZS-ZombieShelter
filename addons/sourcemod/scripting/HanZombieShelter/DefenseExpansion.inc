// v0.7.2 combat extensions. HZS remains an unmodified API provider.
#define MAX_PLASMA_PROJECTILES 8
enum struct ExpansionDefense
{
    int uid;
    int ref;
    int epoch;
    int revision;
    int phase; // 0 idle, 1 startup/charge, 2 discharge, 3 cooldown
    float deadline;
    float nextPulse;
}
enum struct ExpansionPlasma
{
    bool used;
    int ref;
    int sourceRef;
    int source;
    int sourceUID;
    int sourceGeneration;
    int revision;
    int epoch;
    int damage;
    int fieldDamage;
    float radius;
    float fieldDuration;
    float fieldRadius;
    float fieldInterval;
    float position[3];
    float velocity[3];
    float expires;
    float previous;
}
enum struct ExpansionFreeze
{
    int ref;
    int epoch;
    float ownUntil;
    float nativeUntil;
    bool applied;
    int targetSerial;
}
ExpansionDefense gExpansionDefense[MAX_BUILDINGS];
ExpansionPlasma gExpansionPlasma[MAX_PLASMA_PROJECTILES];
RewardContext gExpansionPlasmaReward[MAX_PLASMA_PROJECTILES];
char gExpansionPlasmaImpact[MAX_PLASMA_PROJECTILES][PLATFORM_MAX_PATH];
ExpansionFreeze gExpansionFreeze[MAX_NPCS];
int gExpansionArc,gExpansionHalo,gExpansionGlow,gExpansionBallGlow;
int gExpansionPlasmaLimit=8;
float gExpansionPlasmaLifetime=3.0;
char gExpansionPlasmaModel[PLATFORM_MAX_PATH];
bool gExpansionSmokeHooked,gExpansionNativeIce=true;
float gExpansionNativeIceDuration=2.0,gExpansionNativeIceRadius=500.0;

#include "PlasmaFields.inc"

bool ExpansionFloat(ConfigFile file,const char[] path,const char[] key,float fallback,float minimum,float maximum)
{
    bool found;float value;
    return StoredLevelFloat(file,path,key,fallback,value,found) && value>=minimum && value<=maximum;
}

bool ValidateExpansionType(ConfigFile file,const char[] type)
{
    char profile[128],attack[160];Format(profile,sizeof(profile),"Types/%s",type);Format(attack,sizeof(attack),"%s/Attack",profile);
    if(file==C_Mines)
    {
        char mode[32];Text(file,profile,"effect_mode",mode,sizeof(mode),"explosive");
        if(!StrEqual(mode,"explosive") && !StrEqual(mode,"burn") && !StrEqual(mode,"freeze"))return ConfigFailure("mine effect_mode must be explosive/burn/freeze");
        if(!ExpansionFloat(file,profile,"trigger_radius",80.0,1.0,1024.0)
            || !ExpansionFloat(file,profile,"freeze_duration",2.0,0.1,30.0))return ConfigFailure("mine trigger/freeze bounds");
        if(StrEqual(mode,"freeze"))
        {
            char levels[160];Format(levels,sizeof(levels),"%s/Levels",profile);
            int values[MAX_RECIPES],count;
            if(Node(file,levels) && gCfg[file].GotoFirstSubKey())do
            {
                char key[32];gCfg[file].GetSectionName(key,sizeof(key));
                if(count>=MAX_RECIPES || !ParseLevelInteger(key,values[count]))return ConfigFailure("freeze mine explicit levels");
                count++;
            }while(gCfg[file].GotoNextKey());
            for(int i=0;i<count;i++)
            {
                float damage;if(!ReadLevelFloat(file,profile,values[i],"damage",0.0,damage) || damage!=0.0)
                    return ConfigFailure("freeze mine direct damage must be zero");
            }
            char growth[160];Format(growth,sizeof(growth),"%s/Growth/Stats",profile);
            if(!ExpansionFloat(file,growth,"damage_per_level",0.0,0.0,0.0))return ConfigFailure("freeze mine damage growth must be zero");
        }
        return true;
    }
    if(!ExpansionFloat(file,attack,"height_tolerance",120.0,8.0,1024.0))return ConfigFailure("expansion height_tolerance");
    if(file==C_Tesla)
    {
        if(!ExpansionFloat(file,attack,"startup_seconds",0.6,0.1,10.0)
            || !ExpansionFloat(file,attack,"window_seconds",2.0,0.1,20.0)
            || !ExpansionFloat(file,attack,"cooldown_seconds",3.5,0.1,60.0)
            || !ExpansionFloat(file,attack,"pulse_interval",0.25,0.1,5.0))return ConfigFailure("tesla timing bounds");
        bool found;int targets,arcs;
        if(!StoredLevelInt(file,attack,"max_targets",8,targets,found) || targets<1 || targets>32
            || !StoredLevelInt(file,attack,"max_arcs",4,arcs,found) || arcs<0 || arcs>16)return ConfigFailure("tesla target/effect budget");
    }
    else if(!ExpansionFloat(file,attack,"charge_seconds",1.5,0.1,10.0)
        || !ValidatePlasmaFieldProfile(type))return ConfigFailure("plasma charge/field level settings");
    static const char keys[][]={"start","attack","attack_alt","end","impact"};
    char soundPath[160],sample[PLATFORM_MAX_PATH],download[PLATFORM_MAX_PATH];Format(soundPath,sizeof(soundPath),"%s/Sounds",profile);
    for(int key=0;key<sizeof(keys);key++)
    {
        Text(file,soundPath,keys[key],sample,sizeof(sample));if(!sample[0])continue;
        if(sample[0]=='/' || StrContains(sample,"..")>=0 || StrContains(sample,":")>=0 || StrContains(sample,"\\")>=0)
            return ConfigFailure("expansion sound must be a relative path");
        Format(download,sizeof(download),"sound/%s",sample);
        if(!FileExists(download,true) || !PrecacheSound(sample,true))return ConfigFailure(download);
        AddFileToDownloadsTable(download);
    }
    return true;
}

void LoadExpansionNativeIce()
{
    gExpansionNativeIce=true;gExpansionNativeIceDuration=2.0;gExpansionNativeIceRadius=500.0;
    char path[PLATFORM_MAX_PATH];BuildPath(Path_SM,path,sizeof(path),"configs/HanZombieScenario/HanZombieScenarioGrenadeSysCFG.cfg");
    KeyValues ice=new KeyValues("GrenadeSysConfig");
    if(ice.ImportFromFile(path))
    {
        gExpansionNativeIce=ice.GetNum("Enable_GrenadeSys",1)!=0 && ice.GetNum("Enable_Ice_Grenade",1)!=0;
        gExpansionNativeIceDuration=ice.GetFloat("Ice_Grenade_duration",2.0);
        gExpansionNativeIceRadius=ice.GetFloat("Ice_Grenade_Radius",500.0);
    }
    delete ice;
    // Invalid native configuration cannot be mirrored with a guessed duration.
    if(!(gExpansionNativeIceDuration>0.0 && gExpansionNativeIceDuration<=120.0
        && gExpansionNativeIceRadius>0.0 && gExpansionNativeIceRadius<=8192.0))
    {LogError("[Shelter] Native ice configuration exceeds supported mirror duration/radius; freezing configuration requires review");gExpansionNativeIce=false;}
}

void ExpansionPrecache()
{
    gExpansionArc=PrecacheModel("materials/sprites/lgtning.vmt",true);
    if(gExpansionArc<=0)gExpansionArc=gBeamSprite;
    gExpansionHalo=PrecacheModel("materials/sprites/halo01.vmt",true);
    gExpansionGlow=PrecacheModel("sprites/blueglow2.vmt",true);
    gExpansionBallGlow=PrecacheModel("materials/sprites/combineball_glow_blue_1.vmt",true);
    PrecachePlasmaField();
    PrecacheSound("physics/glass/glass_impact_bullet4.wav",true);PrecacheSound("ui/freeze_cam.wav",true);
}

bool LoadExpansionSettings()
{
    ExpansionPrecache();LoadExpansionNativeIce();
    if(!gExpansionSmokeHooked){HookEvent("smokegrenade_detonate",ExpansionSmokeDetonate,EventHookMode_Post);gExpansionSmokeHooked=true;}
    ConfigFile files[]={C_Tesla,C_Plasma,C_Mines};
    for(int f=0;f<sizeof(files);f++)
    {
        ConfigFile file=files[f];if(!Node(file,"Types") || !gCfg[file].GotoFirstSubKey())continue;
        char names[MAX_RECIPES][64];int count;
        do{if(count>=MAX_RECIPES)return ConfigFailure("expansion Type catalogue capacity");gCfg[file].GetSectionName(names[count++],64);}while(gCfg[file].GotoNextKey());
        for(int i=0;i<count;i++)if(!ValidateExpansionType(file,names[i]))return false;
    }
    bool found;
    if(!StoredLevelInt(C_Plasma,"Projectile","max_projectiles",8,gExpansionPlasmaLimit,found)
        || gExpansionPlasmaLimit<1 || gExpansionPlasmaLimit>MAX_PLASMA_PROJECTILES
        || !StoredLevelFloat(C_Plasma,"Projectile","lifetime_seconds",3.0,gExpansionPlasmaLifetime,found)
        || gExpansionPlasmaLifetime<0.1 || gExpansionPlasmaLifetime>15.0)return ConfigFailure("plasma projectile capacity/lifetime");
    Text(C_Plasma,"Projectile","model",gExpansionPlasmaModel,sizeof(gExpansionPlasmaModel),"models/effects/combineball.mdl");
    if(gCfg[C_Plasma]!=null && PrecacheModel(gExpansionPlasmaModel,true)<=0)return ConfigFailure("plasma projectile model");
    return true;
}

void ExpansionAttackPath(int b,char[] path,int size)
{
    Format(path,size,"Types/%s/Attack",gBuildings[b].type);
}

void PlayExpansionSound(int b,const char[] action,const char[] fallback="")
{
    // This retained animated model already emits these two exact source WAVs
    // through its migrated MDL events. Avoid layering a second manual copy.
    if(gBuildings[b].kind==Kind_Tesla && StrEqual(gBuildings[b].model,"models/hzsh/han/turret/tesla_coil.mdl")
        && (StrEqual(action,"start") || StrEqual(action,"end")))return;
    char path[128],sample[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s/Sounds",gBuildings[b].type);
    Text(gBuildings[b].file,path,action,sample,sizeof(sample),fallback);if(!sample[0])return;
    int body=Resolve(gBuildings[b].ref);
    EmitAmbientSound(sample,gBuildings[b].origin,body>0 ? body : SOUND_FROM_WORLD,SNDLEVEL_NORMAL,SND_NOFLAGS,0.7);
}

void ExpansionAnimation(int b,const char[] animation)
{
    int entity=Resolve(gBuildings[b].ref);if(entity<0)return;
    SetVariantString(animation);AcceptEntityInput(entity,"SetAnimation");
}

bool ExpansionBuildingIdentity(int b)
{
    return b>=0 && b<MAX_BUILDINGS && gBuildings[b].used && gBuildings[b].alive
        && gExpansionDefense[b].uid==gBuildings[b].uid && gExpansionDefense[b].ref==gBuildings[b].ref
        && gExpansionDefense[b].epoch==gEpoch && gExpansionDefense[b].revision==gBuildings[b].configRevision
        && Resolve(gBuildings[b].ref)>0;
}

void ExpansionBindBuilding(int b)
{
    ExpansionDefense state;state.uid=gBuildings[b].uid;state.ref=gBuildings[b].ref;state.epoch=gEpoch;
    state.revision=gBuildings[b].configRevision;gExpansionDefense[b]=state;
}

int ExpansionTarget(int b,const float muzzle[3])
{
    char path[160];ExpansionAttackPath(b,path,sizeof(path));
    float height=Decimal(gBuildings[b].file,path,"height_tolerance",120.0),best=gBuildings[b].range*gBuildings[b].range;
    int selected=-1,body=Resolve(gBuildings[b].ref);
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!DefenseTarget(n) || gNPCs[n].paused)continue;
        int entity=Resolve(gNPCs[n].ref);float point[3];EntityCenter(entity,point);
        if(FloatAbs(point[2]-muzzle[2])>height)continue;
        float distance=GetVectorDistance(muzzle,point,true);
        if(distance<best && LineClear(muzzle,point,body,entity)){best=distance;selected=n;}
    }
    return selected;
}

void ExpansionEndTesla(int b,float now)
{
    if(gExpansionDefense[b].phase==1 || gExpansionDefense[b].phase==2)
    {ExpansionAnimation(b,"attack_end");PlayExpansionSound(b,"end");}
    char path[160];ExpansionAttackPath(b,path,sizeof(path));
    gExpansionDefense[b].phase=3;gExpansionDefense[b].deadline=now+Decimal(C_Tesla,path,"cooldown_seconds",3.5);
    gBuildings[b].nextFire=gExpansionDefense[b].deadline;
}

void ExpansionTeslaTick(int b,float now)
{
    char path[160];ExpansionAttackPath(b,path,sizeof(path));
    float muzzle[3];BuildingAttachmentPosition(b,gBuildings[b].meta.muzzleOffset,muzzle);
    int target=ExpansionTarget(b,muzzle),phase=gExpansionDefense[b].phase;
    if(phase==3)
    {
        if(now<gExpansionDefense[b].deadline)return;
        gExpansionDefense[b].phase=0;ExpansionAnimation(b,"idle");phase=0;
    }
    if(target<0){if(phase==1 || phase==2)ExpansionEndTesla(b,now);return;}
    if(phase==0)
    {
        gExpansionDefense[b].phase=1;gExpansionDefense[b].deadline=now+Decimal(C_Tesla,path,"startup_seconds",0.6);
        gBuildings[b].nextFire=gExpansionDefense[b].deadline;
        ExpansionAnimation(b,"attack");PlayExpansionSound(b,"start");return;
    }
    if(phase==1)
    {
        if(now<gExpansionDefense[b].deadline)return;
        gExpansionDefense[b].phase=2;gExpansionDefense[b].deadline=now+Decimal(C_Tesla,path,"window_seconds",2.0);
        gExpansionDefense[b].nextPulse=now;ExpansionAnimation(b,"attack_loop");
    }
    if(now>=gExpansionDefense[b].deadline){ExpansionEndTesla(b,now);return;}
    if(now+0.00001<gExpansionDefense[b].nextPulse)return;
    float interval=Decimal(C_Tesla,path,"pulse_interval",0.25);
    gExpansionDefense[b].nextPulse=NextFireDeadline(gExpansionDefense[b].nextPulse,now,interval);
    gBuildings[b].nextFire=gExpansionDefense[b].nextPulse;
    int maximum=Number(C_Tesla,path,"max_targets",8),arcs=Number(C_Tesla,path,"max_arcs",4),hits;
    float height=Decimal(C_Tesla,path,"height_tolerance",120.0);
    RewardContext context;if(!CaptureBuildingReward(b,context))return;
    PlayExpansionSound(b,GetRandomInt(0,1)==0 ? "attack" : "attack_alt");
    for(int n=0;n<MAX_NPCS && hits<maximum;n++)
    {
        if(!Running() || !ExpansionBuildingIdentity(b) || !BuildingPowered(b))return;
        if(!DefenseTarget(n) || gNPCs[n].paused)continue;
        int entity=Resolve(gNPCs[n].ref);float point[3];EntityCenter(entity,point);
        if(FloatAbs(point[2]-muzzle[2])>height || GetVectorDistance(muzzle,point)>gBuildings[b].range
            || !LineClear(muzzle,point,Resolve(context.sourceRef),entity))continue;
        if(hits<arcs)
        {
            int color[4]={60,160,255,230};
            TE_SetupBeamPoints(muzzle,point,gExpansionArc,0,0,20,0.15,5.0,2.0,0,12.0,color,10);TE_SendToAll();
        }
        hits++;TraceDefenseShot(b,now,n);DamageNPCWithContext(n,RoundToNearest(gBuildings[b].damage),context);
    }
}

int ExpansionProjectileCount()
{
    int count;for(int p=0;p<MAX_PLASMA_PROJECTILES;p++)if(gExpansionPlasma[p].used)count++;return count;
}

void ExpansionRemovePlasma(int p)
{
    int ref=gExpansionPlasma[p].ref;ExpansionPlasma empty;gExpansionPlasma[p]=empty;
    RewardContext context;gExpansionPlasmaReward[p]=context;gExpansionPlasmaImpact[p][0]='\0';RemoveOwnedRef(ref);
}

bool ExpansionLaunchPlasma(int b,const float muzzle[3],const float target[3],float now)
{
    if(!Running() || !BuildingPowered(b) || ExpansionProjectileCount()>=gExpansionPlasmaLimit
        || ExpansionProjectileCount()+GrenadeCount()>=MAX_GRENADES || !CanAllocateEntities(1,true))return false;
    int slot=-1;for(int p=0;p<MAX_PLASMA_PROJECTILES;p++)if(!gExpansionPlasma[p].used){slot=p;break;}if(slot<0)return false;
    RewardContext context;if(!CaptureBuildingReward(b,context))return false;
    float direction[3];MakeVectorFromPoints(muzzle,target,direction);direction[2]=0.0;
    if(GetVectorLength(direction)<1.0)return false;
    NormalizeVector(direction,direction);
    ExpansionPlasma shot;shot.position=muzzle;shot.previous=now;shot.expires=now+gExpansionPlasmaLifetime;
    shot.epoch=gEpoch;shot.sourceRef=context.sourceRef;shot.damage=RoundToNearest(gBuildings[b].damage);shot.radius=gBuildings[b].blastRadius;
    if(!SnapshotPlasmaField(b,shot))return false;
    for(int a=0;a<3;a++)shot.velocity[a]=direction[a]*gBuildings[b].projectileSpeed;
    int entity=CreateEntityByName("prop_dynamic_override");if(entity<0)return false;
    DispatchKeyValue(entity,"model",gExpansionPlasmaModel);DispatchKeyValue(entity,"solid","0");
    char name[64];Format(name,sizeof(name),"hzsh_plasma_%d_%d",gEpoch,slot);DispatchKeyValue(entity,"targetname",name);
    if(!DispatchSpawn(entity) || !CanAllocateEntities(1,true)){RemoveEntity(entity);return false;}
    if(!Running() || context.epoch!=gEpoch || !gBuildings[b].used || !gBuildings[b].alive
        || gBuildings[b].uid!=context.sourceUID || gBuildings[b].ref!=context.sourceRef || !BuildingPowered(b))
    {RemoveEntity(entity);return false;}
    SetEntityMoveType(entity,MOVETYPE_NONE);SetEntProp(entity,Prop_Data,"m_takedamage",0,1);
    TeleportEntity(entity,muzzle,NULL_VECTOR,NULL_VECTOR);shot.ref=EntIndexToEntRef(entity);shot.used=true;
    gExpansionPlasma[slot]=shot;gExpansionPlasmaReward[slot]=context;
    char soundPath[160];Format(soundPath,sizeof(soundPath),"Types/%s/Sounds",gBuildings[b].type);
    Text(C_Plasma,soundPath,"impact",gExpansionPlasmaImpact[slot],sizeof(gExpansionPlasmaImpact[]),"hzsh/han/plasmatower-exp.wav");
    // Follow the moving shot with the stock laser sprite.  The longer solid
    // blue tail makes horizontal flight visible before the swept impact.
    int color[4]={40,120,255,230};TE_SetupBeamFollow(entity,gBeamSprite,0,0.65,6.0,2.0,3,color);TE_SendToAll();
    return true;
}

void ExpansionPlasmaTick(int b,float now)
{
    float muzzle[3];BuildingAttachmentPosition(b,gBuildings[b].meta.muzzleOffset,muzzle);
    int target=ExpansionTarget(b,muzzle);
    if(target<0){gExpansionDefense[b].phase=0;gBuildings[b].nextFire=now+0.1;return;}
    float point[3];EntityCenter(Resolve(gNPCs[target].ref),point);
    float direction[3],angles[3];MakeVectorFromPoints(gBuildings[b].origin,point,direction);direction[2]=0.0;
    DefenseAimAngles(gBuildings[b].meta,gBuildings[b].angles,direction,angles);
    TeleportEntity(Resolve(gBuildings[b].ref),NULL_VECTOR,angles,NULL_VECTOR);
    BuildingAttachmentPosition(b,gBuildings[b].meta.muzzleOffset,muzzle);
    if(gExpansionDefense[b].phase==0)
    {
        char profile[128],path[160];Format(profile,sizeof(profile),"Types/%s",gBuildings[b].type);ExpansionAttackPath(b,path,sizeof(path));
        float charge=Decimal(C_Plasma,path,"charge_seconds",1.5);
        ReadLevelFloat(C_Plasma,profile,gBuildings[b].level,"charge_seconds",charge,charge);
        gExpansionDefense[b].phase=1;gExpansionDefense[b].deadline=now+charge;gBuildings[b].nextFire=now+0.1;
        PlayExpansionSound(b,"start");
        if(gExpansionBallGlow>0){TE_SetupGlowSprite(muzzle,gExpansionBallGlow,0.15,0.25,150);TE_SendToAll();}
        return;
    }
    if(now<gExpansionDefense[b].deadline)
    {
        gBuildings[b].nextFire=now+0.1;
        if(gExpansionBallGlow>0){TE_SetupGlowSprite(muzzle,gExpansionBallGlow,0.15,0.25,150);TE_SendToAll();}
        return;
    }
    if(ExpansionLaunchPlasma(b,muzzle,point,now)){TraceDefenseShot(b,now,target);PlayExpansionSound(b,"attack");}
    gExpansionDefense[b].phase=0;gBuildings[b].nextFire=now+gBuildings[b].interval;
}

void ExpansionDefenseTick(int b,float now)
{
    if(!ExpansionBuildingIdentity(b))ExpansionBindBuilding(b);
    if(!BuildingPowered(b) || !Running())return;
    if(gBuildings[b].kind==Kind_Tesla)ExpansionTeslaTick(b,now);
    else if(gBuildings[b].kind==Kind_Plasma)ExpansionPlasmaTick(b,now);
}

public bool ExpansionPlasmaTrace(int entity,int mask,any slot)
{
    if(slot<0 || slot>=MAX_PLASMA_PROJECTILES)return true;
    if(entity==Resolve(gExpansionPlasma[slot].ref) || entity==Resolve(gExpansionPlasma[slot].sourceRef))return false;
    if(IsBuildPreviewEntity(entity))return false;
    if(entity>MaxClients && IsValidEntity(entity))
    {
        char classname[64],name[64];GetEntityClassname(entity,classname,sizeof(classname));
        if(StrEqual(classname,"light_dynamic") || StrEqual(classname,"env_sprite"))return false;
        GetEntPropString(entity,Prop_Data,"m_iName",name,sizeof(name));
        if(StrEqual(name,"hzsh_model_bounds_probe") || StrContains(name,"hzsh_plasma_")==0 || StrContains(name,"hzsh_grenade_")==0)return false;
        int n=FindNPC(entity);if(n>=0 && (!DefenseTarget(n) || gNPCs[n].paused))return false;
    }
    return true;
}

void ExpansionExplodePlasma(int p,const float point[3])
{
    ExpansionPlasma shot;shot=gExpansionPlasma[p];RewardContext context;context=gExpansionPlasmaReward[p];
    char sample[PLATFORM_MAX_PATH];strcopy(sample,sizeof(sample),gExpansionPlasmaImpact[p]);
    ExpansionRemovePlasma(p);
    if(!Running() || shot.epoch!=gEpoch || GetGameTime()>=shot.expires)return;
    if(gExpansionBallGlow>0){TE_SetupGlowSprite(point,gExpansionBallGlow,0.3,1.0,255);TE_SendToAll();}
    int color[4]={80,170,255,230};TE_SetupBeamRingPoint(point,5.0,shot.radius*2.0,gExpansionArc,gExpansionHalo,0,10,0.25,8.0,2.0,color,10,0);TE_SendToAll();
    // The impact belongs to the shot snapshot even if its tower was removed.
    if(sample[0])EmitAmbientSound(sample,point,SOUND_FROM_WORLD,SNDLEVEL_NORMAL,SND_NOFLAGS,0.7);
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!Running() || shot.epoch!=gEpoch)return;
        if(!DefenseTarget(n) || gNPCs[n].paused)continue;
        int entity=Resolve(gNPCs[n].ref);float center[3];EntityCenter(entity,center);
        if(GetVectorDistance(point,center)<=shot.radius && LineClear(point,center,-1,entity))DamageNPCWithContext(n,shot.damage,context);
    }
    // The original one-shot explosion always comes first. Its residual field
    // is an independent fixed-origin object, not a burn attached to its victims.
    StartPlasmaField(shot,context,point,GetGameTime());
}

int ExpansionMineMode(int b)
{
    char path[128],mode[32];Format(path,sizeof(path),"Types/%s",gBuildings[b].type);
    Text(C_Mines,path,"effect_mode",mode,sizeof(mode),"explosive");
    return StrEqual(mode,"freeze") ? 2 : (StrEqual(mode,"burn") ? 1 : 0);
}

bool ExpansionLegacyMine(int b)
{
    char path[128];Format(path,sizeof(path),"Types/%s",gBuildings[b].type);
    return !Node(C_Mines,path) || gCfg[C_Mines].GetDataType("effect_mode")==KvData_None;
}

float ExpansionMineTrigger(int b)
{
    char path[128];Format(path,sizeof(path),"Types/%s",gBuildings[b].type);return Decimal(C_Mines,path,"trigger_radius",80.0);
}

float ExpansionMineFreezeDuration(int b)
{
    char path[128];Format(path,sizeof(path),"Types/%s",gBuildings[b].type);return Decimal(C_Mines,path,"freeze_duration",2.0);
}

void ExpansionFreezeBurst(const float point[3],float radius)
{
    int color[4]={75,75,255,230};TE_SetupBeamRingPoint(point,10.0,radius*2.0,gExpansionArc,gExpansionHalo,1,1,0.2,12.0,1.0,color,0,0);TE_SendToAll();
    EmitAmbientSound("ui/freeze_cam.wav",point,SOUND_FROM_WORLD,SNDLEVEL_NORMAL,SND_NOFLAGS,0.5);
}

bool ExpansionFreezeIdentity(int n)
{
    return n>=0 && n<MAX_NPCS && gNPCs[n].used && !gNPCs[n].dead && gNPCs[n].ready
        && gNPCs[n].ref==gExpansionFreeze[n].ref && gExpansionFreeze[n].epoch==gEpoch
        && gNPCs[n].registrationEpoch==gEpoch && Resolve(gNPCs[n].ref)>0;
}

void ExpansionClearNPCFreeze(int n,bool preserveNative=false)
{
    float nativeUntil=gExpansionFreeze[n].nativeUntil;int ref=gExpansionFreeze[n].ref,epoch=gExpansionFreeze[n].epoch;
    ExpansionFreeze empty;gExpansionFreeze[n]=empty;
    if(preserveNative && ref==gNPCs[n].ref && epoch==gEpoch)
    {gExpansionFreeze[n].ref=ref;gExpansionFreeze[n].epoch=epoch;gExpansionFreeze[n].nativeUntil=nativeUntil;}
}

bool ExpansionNPCFrozen(int n,float now)
{
    if(!ExpansionFreezeIdentity(n))return false;
    return gExpansionFreeze[n].ownUntil>now || gExpansionFreeze[n].nativeUntil>now;
}

bool ExpansionNPCProtected(int n)
{
    int boss=gNPCs[n].boss;
    return !Running() || !gNPCs[n].active || gNPCs[n].paused || (boss>=0 && !gBosses[boss].awakened);
}

void MaintainExpansionFreeze(int n,float now)
{
    if(!ExpansionFreezeIdentity(n))return;
    int entity=Resolve(gNPCs[n].ref);
    if(ExpansionNPCFrozen(n,now))
    {
        if(!gExpansionFreeze[n].applied)
        {
            int target=gLeaderOffset>=0 ? GetEntDataEnt2(entity,gLeaderOffset) : 0;
            gExpansionFreeze[n].targetSerial=LiveHuman(target) ? GetClientSerial(target) : 0;
            gNPCs[n].attackingUID=0;gExpansionFreeze[n].applied=true;
        }
        float until=gExpansionFreeze[n].ownUntil>gExpansionFreeze[n].nativeUntil ? gExpansionFreeze[n].ownUntil : gExpansionFreeze[n].nativeUntil;
        Han_SetZombieTarget(entity,entity,until-now+0.1);Han_SetCustomAnimationEx(entity,gNPCs[n].idle,until-now+0.1);
        SetEntityMoveType(entity,MOVETYPE_NONE);TeleportEntity(entity,NULL_VECTOR,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));
        if(gLeaderOffset>=0)SetEntDataEnt2(entity,gLeaderOffset,entity);
        return;
    }
    if(!gExpansionFreeze[n].applied)return;
    int serial=gExpansionFreeze[n].targetSerial;ExpansionClearNPCFreeze(n);
    // Releasing our reason never releases parking, dormant Boss or wall control.
    if(ExpansionNPCProtected(n) || gNPCs[n].attackingUID){FreezeNPC(n);return;}
    Han_UnlockZombie(entity);Han_SetCustomAnimationEx(entity,gNPCs[n].idle,0.01);
    int target=GetClientFromSerial(serial);
    if(LiveHuman(target))
    {Han_SetZombieTarget(entity,target,0.75);if(gLeaderOffset>=0)SetEntDataEnt2(entity,gLeaderOffset,target);}
    SetEntityMoveType(entity,MOVETYPE_STEP);
}

void ApplyExpansionFreeze(int n,float duration,float now,bool visual)
{
    if(!Running() || !DefenseTarget(n) || gNPCs[n].paused || duration<=0.0)return;
    if(!ExpansionFreezeIdentity(n)){ExpansionClearNPCFreeze(n);gExpansionFreeze[n].ref=gNPCs[n].ref;gExpansionFreeze[n].epoch=gEpoch;}
    float until=now+duration;if(until>gExpansionFreeze[n].ownUntil)gExpansionFreeze[n].ownUntil=until;
    MaintainExpansionFreeze(n,now);
    if(visual && gExpansionGlow>0)
    {
        float point[3];GetEntPropVector(Resolve(gNPCs[n].ref),Prop_Data,"m_vecOrigin",point);point[2]+=5.0;
        TE_SetupGlowSprite(point,gExpansionGlow,duration,0.7,120);TE_SendToAll();
    }
}

public bool ExpansionSmokeTrace(int entity,int mask,any target){return entity==target;}

bool ExpansionNativeSmokeAffects(int entity,const float origin[3])
{
    float point[3];GetEntPropVector(entity,Prop_Send,"m_vecOrigin",point);point[2]+=2.0;
    if(GetVectorDistance(origin,point)>gExpansionNativeIceRadius)return false;
    if(GetVectorDistance(origin,point)<=100.0)return true;
    Handle trace=TR_TraceRayFilterEx(origin,point,MASK_SOLID,RayType_EndPoint,ExpansionSmokeTrace,entity);
    bool hit=TR_DidHit(trace) && TR_GetEntityIndex(trace)==entity;delete trace;if(hit)return true;
    GetEntPropVector(entity,Prop_Send,"m_vecOrigin",point);point[2]+=53.0;
    if(GetVectorDistance(origin,point)<=100.0)return true;
    trace=TR_TraceRayFilterEx(origin,point,MASK_SOLID,RayType_EndPoint,ExpansionSmokeTrace,entity);
    hit=TR_DidHit(trace) && TR_GetEntityIndex(trace)==entity;delete trace;return hit;
}

public void ExpansionSmokeDetonate(Event event,const char[] name,bool dontBroadcast)
{
    if(!gEnabled || !gCoreReady || !gExpansionNativeIce)return;
    float origin[3];origin[0]=event.GetFloat("x");origin[1]=event.GetFloat("y");origin[2]=event.GetFloat("z")+10.0;
    float now=GetGameTime();
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!gNPCs[n].used || !gNPCs[n].ready || gNPCs[n].dead)continue;
        int entity=Resolve(gNPCs[n].ref);if(entity<0 || !ExpansionNativeSmokeAffects(entity,origin))continue;
        if(!ExpansionFreezeIdentity(n)){ExpansionClearNPCFreeze(n);gExpansionFreeze[n].ref=gNPCs[n].ref;gExpansionFreeze[n].epoch=gEpoch;}
        // HZS refreshes its timer on each smoke. Margin places our release after
        // that timer's unconditional STEP, including same-frame callback order.
        gExpansionFreeze[n].nativeUntil=now+gExpansionNativeIceDuration+0.1;
        MaintainExpansionFreeze(n,now);
    }
}

public Action ExpansionNPCThinkPre(int entity)
{
    int n=FindNPC(entity);if(n<0 || gNPCs[n].dead || !gNPCs[n].ready)return Plugin_Continue;
    MaintainExpansionFreeze(n,GetGameTime());
    if(ExpansionNPCProtected(n) || ExpansionNPCFrozen(n,GetGameTime()))
    {SetEntityMoveType(entity,MOVETYPE_NONE);TeleportEntity(entity,NULL_VECTOR,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));}
    return Plugin_Continue;
}

void ExpansionProjectileTick(float now)
{
    TickPlasmaFields(now);
    for(int n=0;n<MAX_NPCS;n++)
    {
        if(!ExpansionFreezeIdentity(n))continue;
        MaintainExpansionFreeze(n,now);
        if(ExpansionNPCProtected(n))
        {
            int entity=Resolve(gNPCs[n].ref);if(entity>0){SetEntityMoveType(entity,MOVETYPE_NONE);TeleportEntity(entity,NULL_VECTOR,NULL_VECTOR,view_as<float>({0.0,0.0,0.0}));}
        }
    }
    for(int p=0;p<MAX_PLASMA_PROJECTILES;p++)
    {
        if(!gExpansionPlasma[p].used)continue;
        int body=Resolve(gExpansionPlasma[p].ref);
        float elapsed=now-gExpansionPlasma[p].previous;
        if(!Running() || gExpansionPlasma[p].epoch!=gEpoch || body<0 || now>=gExpansionPlasma[p].expires || elapsed>0.25)
        {ExpansionRemovePlasma(p);continue;}
        if(elapsed<=0.0)continue;
        float end[3];for(int a=0;a<3;a++)end[a]=gExpansionPlasma[p].position[a]+gExpansionPlasma[p].velocity[a]*elapsed;
        float mins[3]={-3.0,-3.0,-3.0},maxs[3]={3.0,3.0,3.0};
        Handle trace=TR_TraceHullFilterEx(gExpansionPlasma[p].position,end,mins,maxs,MASK_SOLID,ExpansionPlasmaTrace,p);
        if(TR_DidHit(trace) || TR_StartSolid(trace))
        {
            float point[3],normal[3];TR_GetEndPosition(point,trace);TR_GetPlaneNormal(trace,normal);delete trace;
            for(int a=0;a<3;a++)point[a]+=normal[a]*4.0;
            ExpansionExplodePlasma(p,point);continue;
        }
        delete trace;bool outside;for(int a=0;a<3;a++)if(FloatAbs(end[a])>15000.0)outside=true;
        if(outside){ExpansionRemovePlasma(p);continue;}
        gExpansionPlasma[p].position=end;gExpansionPlasma[p].previous=now;TeleportEntity(body,end,NULL_VECTOR,NULL_VECTOR);
    }
}

void ExpansionWorldTick(float now)
{
    for(int b=0;b<MAX_BUILDINGS;b++)
    {
        if(!gExpansionDefense[b].uid)continue;
        if(!ExpansionBuildingIdentity(b)) {ExpansionDefense empty;gExpansionDefense[b]=empty;continue;}
        if(!Running() || !BuildingPowered(b))
        {
            if(gBuildings[b].kind==Kind_Tesla && (gExpansionDefense[b].phase==1 || gExpansionDefense[b].phase==2))ExpansionEndTesla(b,now);
            else if(gBuildings[b].kind==Kind_Plasma)gExpansionDefense[b].phase=0;
        }
    }
}

void ClearExpansionState()
{
    ClearPlasmaFields();
    for(int p=0;p<MAX_PLASMA_PROJECTILES;p++)if(gExpansionPlasma[p].used)ExpansionRemovePlasma(p);
    for(int b=0;b<MAX_BUILDINGS;b++){ExpansionDefense empty;gExpansionDefense[b]=empty;}
    for(int n=0;n<MAX_NPCS;n++)ExpansionClearNPCFreeze(n);
}
