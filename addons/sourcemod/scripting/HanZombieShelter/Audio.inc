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

void WallImpactSound(int building)
{
    float now=GetGameTime();if(now<gWallSoundNext[building])return;
    gWallSoundNext[building]=now+0.15;
    PlayInteractionSound("wall_hit",Resolve(gBuildings[building].ref),gBuildings[building].origin);
}
