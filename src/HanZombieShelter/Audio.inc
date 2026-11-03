static const char sSoundKeys[][]={"gather","repair","wall_hit","build_place","build_complete","destroyed",
    "build_failed","base_destroyed","base_upgrade","night_start","failure","victory","power_failure","base_alarm","gather_hit","facility_upgrade"};
char gInteractionSounds[sizeof(sSoundKeys)][PLATFORM_MAX_PATH];
float gInteractionVolumes[sizeof(sSoundKeys)],gWallSoundNext[MAX_BUILDINGS];
float gBaseAlarmCooldown=15.0,gBaseAlarmNext[MAX_CORES];
int gBaseAlarmEpoch[MAX_CORES],gBaseAlarmUID[MAX_CORES];
static const char sSkillsImmunitySound[]="player/bhit_helmet-1.wav";
bool gSkillsImmunitySoundReady;

void LoadInteractionSounds()
{
    gSkillsImmunitySoundReady=PrecacheSound(sSkillsImmunitySound,true);
    if(!gSkillsImmunitySoundReady)LogError("[Shelter] Could not precache built-in immunity sound %s",sSkillsImmunitySound);
    for(int b=0;b<MAX_BUILDINGS;b++)gWallSoundNext[b]=0.0;
    for(int c=0;c<MAX_CORES;c++){gBaseAlarmEpoch[c]=0;gBaseAlarmUID[c]=0;gBaseAlarmNext[c]=0.0;}
    gBaseAlarmCooldown=Decimal(C_Sounds,"base_alarm","cooldown_seconds",15.0);
    if(!(gBaseAlarmCooldown>=1.0 && gBaseAlarmCooldown<=3600.0))gBaseAlarmCooldown=15.0;
    for(int i=0;i<sizeof(sSoundKeys);i++)
    {
        Text(C_Sounds,sSoundKeys[i],"sample",gInteractionSounds[i],sizeof(gInteractionSounds[]));
        gInteractionVolumes[i]=Decimal(C_Sounds,sSoundKeys[i],"volume",0.7);
        if(gInteractionVolumes[i]<0.0)gInteractionVolumes[i]=0.0;
        if(gInteractionVolumes[i]>1.0)gInteractionVolumes[i]=1.0;
        if(!gInteractionSounds[i][0])continue;
        char path[PLATFORM_MAX_PATH];Format(path,sizeof(path),"sound/%s",gInteractionSounds[i]);
        if(!FileExists(path,true) || !PrecacheSound(gInteractionSounds[i],true))
        {
            LogError("[Shelter] Missing interaction sound %s",path);gInteractionSounds[i][0]=0;continue;
        }
        if(Number(C_Sounds,sSoundKeys[i],"download",0))AddFileToDownloadsTable(path);
    }
}

void PlaySkillsImmunitySound(int client)
{
    if(!gSkillsImmunitySoundReady || !LiveHuman(client) || IsFakeClient(client))return;
    EmitSoundToClient(client,sSkillsImmunitySound,SOUND_FROM_PLAYER,SNDCHAN_AUTO,SNDLEVEL_NONE,SND_NOFLAGS,SNDVOL_NORMAL);
}

int InteractionSoundIndex(const char[] action)
{
    for(int i=0;i<sizeof(sSoundKeys);i++)if(StrEqual(action,sSoundKeys[i]))return i;
    return -1;
}

void PlayClientInteractionSound(int client,const char[] action)
{
    int i=InteractionSoundIndex(action);
    if(i<0 || !gInteractionSounds[i][0] || gInteractionVolumes[i]<=0.0
        || client<1 || client>MaxClients || !IsClientInGame(client) || IsFakeClient(client))return;
    EmitSoundToClient(client,gInteractionSounds[i],SOUND_FROM_PLAYER,SNDCHAN_AUTO,SNDLEVEL_NONE,SND_NOFLAGS,gInteractionVolumes[i]);
}

void PlayGlobalInteractionSound(const char[] action)
{
    for(int client=1;client<=MaxClients;client++)PlayClientInteractionSound(client,action);
}

bool ResourceMeleeHit(int client,int inflictor)
{
    if(!LiveHuman(client) || IsFakeClient(client))return false;
    int weapon=GetEntPropEnt(client,Prop_Send,"m_hActiveWeapon");
    if(weapon<=MaxClients || !IsValidEntity(weapon) || (inflictor!=client && inflictor!=weapon))return false;
    // HWS weapon_axe is a custom classname on the engine's CKnife template.
    // This only classifies impact feedback; it never gates resource gathering.
    char netclass[64];GetEntityNetClass(weapon,netclass,sizeof(netclass));
    return StrEqual(netclass,"CKnife");
}

void PlayResourceImpactSound(int client,int inflictor,int entity,const float origin[3])
{
    int i=InteractionSoundIndex("gather_hit");
    if(i<0 || !gInteractionSounds[i][0] || gInteractionVolumes[i]<=0.0 || !ResourceMeleeHit(client,inflictor))return;
    // A separate channel lets a successful hit layer with the resource reward.
    EmitSoundToAll(gInteractionSounds[i],entity,SNDCHAN_BODY,SNDLEVEL_NORMAL,SND_NOFLAGS,gInteractionVolumes[i],SNDPITCH_NORMAL,_,origin);
}

void PlayCoreInteractionSound(int core,const char[] action)
{
    if(core<0 || core>=gCoreCount)return;
    int b=gCores[core].building,entity=-1;
    if(b>=0 && b<MAX_BUILDINGS && gBuildings[b].used)entity=Resolve(gBuildings[b].ref);
    PlayInteractionSound(action,entity,gCores[core].origin);
}

// The stable core, building UID and campaign epoch own this deadline. No global
// alarm timer can suppress a different fixed or player-created shelter.
bool ClaimBaseAttackAlarm(int building,float now)
{
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used
        || !gBuildings[building].alive || gBuildings[building].constructing
        || gBuildings[building].kind!=Kind_Base || gBuildings[building].hp<=0)return false;
    int core=gBuildings[building].core;
    if(core<0 || core>=gCoreCount || gCores[core].building!=building)return false;
    if(gBaseAlarmEpoch[core]!=gEpoch || gBaseAlarmUID[core]!=gBuildings[building].uid)
    {gBaseAlarmEpoch[core]=gEpoch;gBaseAlarmUID[core]=gBuildings[building].uid;gBaseAlarmNext[core]=0.0;}
    if(now<gBaseAlarmNext[core])return false;
    gBaseAlarmNext[core]=now+gBaseAlarmCooldown;return true;
}

void PlayInteractionSound(const char[] action,int entity,const float origin[3])
{
    for(int i=0;i<sizeof(sSoundKeys);i++)if(StrEqual(action,sSoundKeys[i]))
    {
        if(!gInteractionSounds[i][0] || gInteractionVolumes[i]<=0.0)return;
        EmitAmbientSound(gInteractionSounds[i],origin,entity>0 ? entity : SOUND_FROM_WORLD,SNDLEVEL_NORMAL,SND_NOFLAGS,gInteractionVolumes[i]);return;
    }
}

// Source model coordinates: X forward, Y left, Z up, transformed by final angles.
void BuildingAttachmentPosition(int building,const float offset[3],float position[3])
{
    float forwardVector[3],right[3],up[3],angles[3];angles=gBuildings[building].angles;
    int body=Resolve(gBuildings[building].ref);if(body>0)GetEntPropVector(body,Prop_Data,"m_angRotation",angles);
    GetAngleVectors(angles,forwardVector,right,up);
    for(int axis=0;axis<3;axis++)position[axis]=gBuildings[building].origin[axis]
        +forwardVector[axis]*offset[0]-right[axis]*offset[1]+up[axis]*offset[2];
}

void PlayBuildingSound(int building,const char[] action)
{
    if(building<0 || building>=MAX_BUILDINGS || !gBuildings[building].used)return;
    char sample[PLATFORM_MAX_PATH],fallback[32];
    int entity=Resolve(gBuildings[building].ref);
    if(gBuildings[building].kind==Kind_Base && (StrEqual(action,"upgrade") || StrEqual(action,"destroy")))
    {
        int sound=InteractionSoundIndex(StrEqual(action,"upgrade") ? "base_upgrade" : "base_destroyed");
        if(sound>=0 && gInteractionSounds[sound][0])
        {PlayInteractionSound(sSoundKeys[sound],entity,gBuildings[building].origin);return;}
    }
    if(StrEqual(action,"build"))
    {
        if(gBuildings[building].constructing)strcopy(sample,sizeof(sample),gBuildings[building].meta.buildSound);
        strcopy(fallback,sizeof(fallback),gBuildings[building].constructing ? "build_place" : "build_complete");
    }
    else if(StrEqual(action,"complete"))strcopy(fallback,sizeof(fallback),"build_complete");
    else if(StrEqual(action,"upgrade"))
    {
        strcopy(sample,sizeof(sample),gBuildings[building].meta.upgradeSound);
        int previous=InteractionSoundIndex("facility_upgrade");
        strcopy(fallback,sizeof(fallback),previous>=0 && gInteractionSounds[previous][0] ? "facility_upgrade" : "build_complete");
    }
    else if(StrEqual(action,"destroy")){strcopy(sample,sizeof(sample),gBuildings[building].meta.destroySound);strcopy(fallback,sizeof(fallback),"destroyed");}
    else if(StrEqual(action,"attack"))strcopy(sample,sizeof(sample),gBuildings[building].meta.attackSound);
    if(sample[0])EmitAmbientSound(sample,gBuildings[building].origin,entity>0 ? entity : SOUND_FROM_WORLD,SNDLEVEL_NORMAL,SND_NOFLAGS,0.7);
    else if(fallback[0])PlayInteractionSound(fallback,entity,gBuildings[building].origin);
}

void WallImpactSound(int building)
{
    float now=GetGameTime();if(now<gWallSoundNext[building])return;
    gWallSoundNext[building]=now+0.15;
    PlayInteractionSound("wall_hit",Resolve(gBuildings[building].ref),gBuildings[building].origin);
}
