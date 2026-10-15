static const char sSoundKeys[][]={"gather","repair","wall_hit","build_place","build_complete","destroyed"};
char gInteractionSounds[6][PLATFORM_MAX_PATH];
float gInteractionVolumes[6],gWallSoundNext[MAX_BUILDINGS];

void LoadInteractionSounds()
{
    for(int b=0;b<MAX_BUILDINGS;b++)gWallSoundNext[b]=0.0;
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
    if(StrEqual(action,"build")){strcopy(sample,sizeof(sample),gBuildings[building].meta.buildSound);strcopy(fallback,sizeof(fallback),"build_place");}
    else if(StrEqual(action,"upgrade")){strcopy(sample,sizeof(sample),gBuildings[building].meta.upgradeSound);strcopy(fallback,sizeof(fallback),"build_complete");}
    else if(StrEqual(action,"destroy")){strcopy(sample,sizeof(sample),gBuildings[building].meta.destroySound);strcopy(fallback,sizeof(fallback),"destroyed");}
    else if(StrEqual(action,"attack"))strcopy(sample,sizeof(sample),gBuildings[building].meta.attackSound);
    int entity=Resolve(gBuildings[building].ref);
    if(sample[0])EmitAmbientSound(sample,gBuildings[building].origin,entity>0 ? entity : SOUND_FROM_WORLD,SNDLEVEL_NORMAL,SND_NOFLAGS,0.7);
    else if(fallback[0])PlayInteractionSound(fallback,entity,gBuildings[building].origin);
}

void WallImpactSound(int building)
{
    float now=GetGameTime();if(now<gWallSoundNext[building])return;
    gWallSoundNext[building]=now+0.15;
    PlayInteractionSound("wall_hit",Resolve(gBuildings[building].ref),gBuildings[building].origin);
}
