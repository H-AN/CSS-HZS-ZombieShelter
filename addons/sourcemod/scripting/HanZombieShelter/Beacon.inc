// Beacon travel uses the existing base landing transaction. The marker follows
// H-AN-CSS-ZipLine's env_sprite + sprites/wh_frame.vmt ($ignorez) implementation.
// One shared marker per beacon; no client/NPC creation and no frame-world scan.
#define BEACON_MARKER_ENTITY_SLOTS 4096

enum BeaconTokenField
{
    BT_Epoch, BT_Serial, BT_Nonce, BT_Revision,
    BT_Source, BT_SourceUID, BT_SourceGeneration, BT_SourceBaseUID, BT_SourceBaseGeneration,
    BT_TargetCore, BT_TargetBaseUID, BT_TargetBaseGeneration, BT_TargetUID, BT_TargetGeneration,
    BT_Action, BT_SourceLevel, BT_TargetLevel, BT_Count
}

bool gBeaconConfigAvailable;
StringMap gBeaconMarkerTypes;
int gBeaconMarkerRef[MAX_BUILDINGS],gBeaconMarkerUID[MAX_BUILDINGS],gBeaconMarkerGeneration[MAX_BUILDINGS];
int gBeaconMarkerEpoch[MAX_BUILDINGS],gBeaconMarkerRevision[MAX_BUILDINGS],gBeaconMarkerBodyRef[MAX_BUILDINGS];
int gBeaconMarkerBuilding[BEACON_MARKER_ENTITY_SLOTS],gBeaconMarkerCount;
float gBeaconNextMarkerAttempt[MAX_BUILDINGS];
int gBeaconMenuNonce[MAXPLAYERS+1];
Menu gBeaconMenus[MAXPLAYERS+1];

bool BeaconActor(int client)
{
    return Running() && LiveHuman(client) && !IsFakeClient(client)
        && !IsClientSourceTV(client) && !IsClientReplay(client);
}

// Logical live buildings waiting for restoration still occupy their one place.
// Type and recipe IDs intentionally do not participate in this group limit.
int BeaconCount(int core,int exclude=-1)
{
    if(core<0 || core>=gCoreCount)return 0;
    int count;
    for(int b=0;b<MAX_BUILDINGS;b++)
        if(b!=exclude && gBuildings[b].used && gBuildings[b].alive
            && gBuildings[b].kind==Kind_Beacon && gBuildings[b].buildCore==core)count++;
    return count;
}

bool BeaconBuildingAlive(int b)
{
    if(!Running() || b<0 || b>=MAX_BUILDINGS || !gBuildings[b].used || !gBuildings[b].alive
        || gBuildings[b].hp<=0 || gBuildings[b].kind!=Kind_Beacon || gBuildings[b].buildCore<0
        || gBuildings[b].buildCore>=gCoreCount || Resolve(gBuildings[b].ref)<0)return false;
    int base=gCores[gBuildings[b].buildCore].building;
    return base>=0 && base<MAX_BUILDINGS && gBuildings[base].used && gBuildings[base].kind==Kind_Base;
}

bool BeaconEffective(int b)
{
    return gBeaconConfigAvailable && BeaconBuildingAlive(b)
        && gBuildings[b].configRevision==gConfigRevision && TravelBaseAlive(gBuildings[b].buildCore)
        && gBuildings[b].powerCore==gBuildings[b].buildCore && BuildingPowered(b);
}

int BeaconForCore(int core,int excludedCore=-1)
{
    if(core==excludedCore || !TravelBaseAlive(core))return -1;
    int selected=-1;
    for(int b=0;b<MAX_BUILDINGS;b++)
        if(gBuildings[b].buildCore==core && BeaconEffective(b)
            && (selected<0 || gBuildings[b].uid<gBuildings[selected].uid))selected=b;
    return selected;
}

void ResetBeaconClient(int client)
{
    if(client<1 || client>MaxClients)return;
    gBeaconMenuNonce[client]++;
    Menu previous=gBeaconMenus[client];gBeaconMenus[client]=null;
    if(previous!=null && IsValidHandle(previous) && IsClientInGame(client))CancelClientMenu(client);
}

void BeaconTokenFields(int source,int targetCore,int target,int action,int fields[BT_Count])
{
    for(int i=0;i<view_as<int>(BT_Count);i++)fields[i]=0;
    fields[BT_Epoch]=gEpoch;fields[BT_Revision]=gConfigRevision;fields[BT_Source]=source;
    fields[BT_SourceUID]=gBuildings[source].uid;fields[BT_SourceGeneration]=gBuildings[source].generation;
    fields[BT_SourceLevel]=gBuildings[source].level;
    int base=gCores[gBuildings[source].buildCore].building;
    fields[BT_SourceBaseUID]=gBuildings[base].uid;fields[BT_SourceBaseGeneration]=gBuildings[base].generation;
    fields[BT_TargetCore]=targetCore;fields[BT_Action]=action;
    if(action==0)
    {
        base=gCores[targetCore].building;
        fields[BT_TargetBaseUID]=gBuildings[base].uid;fields[BT_TargetBaseGeneration]=gBuildings[base].generation;
        fields[BT_TargetUID]=gBuildings[target].uid;fields[BT_TargetGeneration]=gBuildings[target].generation;
        fields[BT_TargetLevel]=gBuildings[target].level;
    }
}

void BeaconEncodeFields(const int fields[BT_Count],char[] token,int length)
{
    token[0]='\0';
    for(int i=0;i<view_as<int>(BT_Count);i++)
    {char part[24];Format(part,sizeof(part),i==0 ? "%d" : ":%d",fields[i]);StrCat(token,length,part);}
}

void BeaconMenuToken(int client,int source,int targetCore,int target,int action,char[] token,int length)
{
    int fields[BT_Count];BeaconTokenFields(source,targetCore,target,action,fields);
    fields[BT_Serial]=GetClientSerial(client);fields[BT_Nonce]=gBeaconMenuNonce[client];BeaconEncodeFields(fields,token,length);
}

bool ParseBeaconToken(const char[] token,int fields[BT_Count])
{
    char parts[view_as<int>(BT_Count)+1][24];
    if(ExplodeString(token,":",parts,sizeof(parts),sizeof(parts[]))!=view_as<int>(BT_Count))return false;
    for(int i=0;i<view_as<int>(BT_Count);i++)
        if(!ParseLevelInteger(parts[i],fields[i],i==view_as<int>(BT_TargetCore)))return false;
    return true;
}

bool BeaconSourceTokenValid(const int fields[BT_Count],int &source)
{
    source=fields[BT_Source];
    if(fields[BT_Epoch]!=gEpoch || fields[BT_Revision]!=gConfigRevision || !BeaconBuildingAlive(source)
        || gBuildings[source].uid!=fields[BT_SourceUID] || gBuildings[source].generation!=fields[BT_SourceGeneration]
        || gBuildings[source].level!=fields[BT_SourceLevel])return false;
    int sourceCore=gBuildings[source].buildCore,base=gCores[sourceCore].building;
    if(base<0 || base>=MAX_BUILDINGS || !gBuildings[base].used || gBuildings[base].kind!=Kind_Base)return false;
    if(gBuildings[base].uid!=fields[BT_SourceBaseUID] || gBuildings[base].generation!=fields[BT_SourceBaseGeneration])return false;
    return true;
}

// World identity checks are independently testable without a real/fake client.
bool BeaconWorldTokenValid(const int fields[BT_Count],int &source,int &target,int &core)
{
    source=fields[BT_Source];target=-1;core=fields[BT_TargetCore];
    if(!BeaconSourceTokenValid(fields,source))return false;
    if(fields[BT_Action]==1)return core==-1; // Management still goes through ShowDefenseMenu's real aim check.
    int sourceCore=gBuildings[source].buildCore,base;
    if(!BeaconEffective(source))return false;
    if(fields[BT_Action]!=0 || core==sourceCore || !TravelBaseAlive(core))return false;
    base=gCores[core].building;
    if(gBuildings[base].uid!=fields[BT_TargetBaseUID] || gBuildings[base].generation!=fields[BT_TargetBaseGeneration])return false;
    target=BeaconForCore(core,sourceCore);
    return target>=0 && gBuildings[target].uid==fields[BT_TargetUID]
        && gBuildings[target].generation==fields[BT_TargetGeneration] && gBuildings[target].level==fields[BT_TargetLevel];
}

bool ValidateBeaconMenuToken(int client,const char[] token,int &source,int &target,int &core,int &action)
{
    if(!BeaconActor(client))return false;
    int fields[BT_Count];if(!ParseBeaconToken(token,fields))return false;
    if(fields[BT_Serial]!=GetClientSerial(client) || fields[BT_Nonce]!=gBeaconMenuNonce[client]
        || !BeaconWorldTokenValid(fields,source,target,core) || !BuildingInUseRange(client,source))return false;
    action=fields[BT_Action];gBeaconMenuNonce[client]++;return true; // Consume before travel/menu callbacks.
}

int BeaconDestroyedTokenBase(int client,const char[] token)
{
    int fields[BT_Count],source;
    if(!BeaconActor(client) || !ParseBeaconToken(token,fields) || fields[BT_Action]!=0
        || fields[BT_Serial]!=GetClientSerial(client) || fields[BT_Nonce]!=gBeaconMenuNonce[client]
        || !BeaconSourceTokenValid(fields,source))return -1;
    int core=gBuildings[source].buildCore;if(!TravelBaseAlive(core))return core;
    core=fields[BT_TargetCore];
    if(core<0 || core>=gCoreCount)return -1;
    int base=gCores[core].building;
    return base>=0 && base<MAX_BUILDINGS && gBuildings[base].uid==fields[BT_TargetBaseUID]
        && !TravelBaseAlive(core) ? core : -1;
}

void TellBeaconBaseDestroyed(int client,int core)
{
    char name[64];CoreDisplayName(core,name,sizeof(name),client);Tell(client,"%t","BeaconBaseDestroyed",name);
}

void ShowBeaconMenu(int client,int b)
{
    if(!BeaconActor(client))return;
    if(!BeaconBuildingAlive(b) || !BuildingInUseRange(client,b)){Tell(client,"%t","BeaconUnavailable");return;}
    bool effective=BeaconEffective(b);
    if(!TravelBaseAlive(gBuildings[b].buildCore))TellBeaconBaseDestroyed(client,gBuildings[b].buildCore);
    else if(!effective)Tell(client,"%t","BeaconNoPower");
    ResetBeaconClient(client);
    Menu menu=new Menu(BeaconMenuHandler);gBeaconMenus[client]=menu;
    char title[192],name[64],token[256],line[192];CoreDisplayName(gBuildings[b].buildCore,name,sizeof(name),client);
    Format(title,sizeof(title),"%T","BeaconMenuTitle",client,name);menu.SetTitle("%s",title);
    int choices;
    for(int core=0;core<gCoreCount;core++)
    {
        int target=BeaconForCore(core,gBuildings[b].buildCore);if(target<0)continue;
        BeaconMenuToken(client,b,core,target,0,token,sizeof(token));CoreDisplayName(core,name,sizeof(name),client);
        Format(line,sizeof(line),"%T","BeaconDestination",client,name);menu.AddItem(token,line,effective ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);choices++;
    }
    if(choices==0){TranslateLabel(client,"BeaconNoDestinations",line,sizeof(line));menu.AddItem("-",line,ITEMDRAW_DISABLED);}
    BeaconMenuToken(client,b,-1,-1,1,token,sizeof(token));TranslateLabel(client,"BeaconManage",line,sizeof(line));menu.AddItem(token,line);
    if(!menu.Display(client,30))
    {
        if(gBeaconMenus[client]==menu){gBeaconMenus[client]=null;gBeaconMenuNonce[client]++;}
        if(IsValidHandle(menu))delete menu;
    }
}

public int BeaconMenuHandler(Menu menu,MenuAction action,int client,int item)
{
    if(action==MenuAction_End)
    {
        for(int c=1;c<=MaxClients;c++)if(gBeaconMenus[c]==menu){gBeaconMenus[c]=null;gBeaconMenuNonce[c]++;}
        delete menu;
    }
    else if(action==MenuAction_Cancel)
    {
        if(client>0 && client<=MaxClients && gBeaconMenus[client]==menu)
        {gBeaconMenus[client]=null;gBeaconMenuNonce[client]++;}
    }
    else if(action==MenuAction_Select)
    {
        if(client<1 || client>MaxClients || gBeaconMenus[client]!=menu)return 0;
        char token[256];menu.GetItem(item,token,sizeof(token));int source,target,core,operation;
        if(!ValidateBeaconMenuToken(client,token,source,target,core,operation))
        {
            int destroyed=BeaconDestroyedTokenBase(client,token);ResetBeaconClient(client);
            if(BeaconActor(client))
            {if(destroyed>=0)TellBeaconBaseDestroyed(client,destroyed);else Tell(client,"%t","BeaconMenuExpired");}
            return 0;
        }
        gBeaconMenus[client]=null;
        if(operation==1){ShowDefenseMenu(client,source);return 0;}
        // No ownership requirement and no alternative-base fallback. The shared
        // transaction validates hull/support/occupancy and rolls back on failure.
        if(!TeleportPlayerToBase(client,core)){Tell(client,"%t","BeaconTravelFailed");return 0;}
        char name[64];CoreDisplayName(core,name,sizeof(name),client);Tell(client,"%t","BeaconTravelSuccess",name);
    }
    return 0;
}

int BeaconMarkerEntity(int b)
{
    return gBeaconMarkerRef[b]==0 || gBeaconMarkerRef[b]==INVALID_ENT_REFERENCE ? -1 : Resolve(gBeaconMarkerRef[b]);
}

void RemoveBeaconMarker(int b)
{
    if(b<0 || b>=MAX_BUILDINGS)return;
    int marker=BeaconMarkerEntity(b);bool owned=gBeaconMarkerRef[b]!=0 && gBeaconMarkerRef[b]!=INVALID_ENT_REFERENCE;
    int ref=gBeaconMarkerRef[b];gBeaconMarkerRef[b]=INVALID_ENT_REFERENCE;
    gBeaconMarkerUID[b]=0;gBeaconMarkerGeneration[b]=0;gBeaconMarkerEpoch[b]=0;
    gBeaconMarkerRevision[b]=0;gBeaconMarkerBodyRef[b]=INVALID_ENT_REFERENCE;gBeaconNextMarkerAttempt[b]=0.0;
    if(owned && gBeaconMarkerCount>0)gBeaconMarkerCount--;
    if(marker>0)
    {
        if(marker<BEACON_MARKER_ENTITY_SLOTS)gBeaconMarkerBuilding[marker]=0;
        SDKUnhook(marker,SDKHook_SetTransmit,BeaconMarkerTransmit);
        RemoveOwnedRef(ref);
    }
}

void ClearBeaconMarkers()
{
    for(int b=0;b<MAX_BUILDINGS;b++)RemoveBeaconMarker(b);
    for(int e=0;e<BEACON_MARKER_ENTITY_SLOTS;e++)gBeaconMarkerBuilding[e]=0;
    gBeaconMarkerCount=0;
    for(int c=1;c<=MaxClients;c++)ResetBeaconClient(c);
}

bool BeaconMarkerVector(const char[] path,const char[] key,const char[] fallback,float output[3],float minimum,float maximum)
{
    char raw[160],parts[4][40];Text(C_Beacons,path,key,raw,sizeof(raw),fallback);TrimString(raw);ReplaceString(raw,sizeof(raw),"\t"," ");
    while(StrContains(raw,"  ")>=0)ReplaceString(raw,sizeof(raw),"  "," ");
    if(ExplodeString(raw," ",parts,4,40)!=3)return false;
    for(int a=0;a<3;a++)if(StringToFloatEx(parts[a],output[a])!=strlen(parts[a])
        || !(output[a]>=minimum && output[a]<=maximum))return false;
    return true;
}

bool LoadBeaconSettings()
{
    ClearBeaconMarkers();delete gBeaconMarkerTypes;gBeaconMarkerTypes=new StringMap();
    gBeaconConfigAvailable=gCfg[C_Beacons]!=null;
    if(!gBeaconConfigAvailable)return true; // Optional new group: preserve old-server compatibility.
    if(!Node(C_Beacons,"Types") || !gCfg[C_Beacons].GotoFirstSubKey())return true;
    ArrayList types=new ArrayList(ByteCountToCells(64));
    do{char type[64];gCfg[C_Beacons].GetSectionName(type,sizeof(type));types.PushString(type);}while(gCfg[C_Beacons].GotoNextKey());
    for(int i=0;i<types.Length;i++)
    {
        char type[64],path[128],sprite[PLATFORM_MAX_PATH],asset[PLATFORM_MAX_PATH],texture[PLATFORM_MAX_PATH];types.GetString(i,type,sizeof(type));
        Format(path,sizeof(path),"Types/%s/Marker",type);
        if(Number(C_Beacons,path,"enabled",1)==0){gBeaconMarkerTypes.SetValue(type,0);continue;}
        Text(C_Beacons,path,"sprite",sprite,sizeof(sprite),"sprites/wh_frame.vmt");
        Format(asset,sizeof(asset),"materials/%s",sprite);
        strcopy(texture,sizeof(texture),asset);ReplaceString(texture,sizeof(texture),".vmt",".vtf",false);
        float offset[3],rgb[3],scale=Decimal(C_Beacons,path,"scale",0.7);int alpha=Number(C_Beacons,path,"alpha",220);
        bool valid=StrContains(sprite,"sprites/")==0 && StrContains(sprite,"..")<0
            && strlen(sprite)>4 && StrEqual(sprite[strlen(sprite)-4],".vmt",false) && FileExists(asset,true) && FileExists(texture,true)
            && BeaconMarkerVector(path,"offset","0 0 72",offset,-512.0,512.0)
            && BeaconMarkerVector(path,"rgb","80 180 255",rgb,0.0,255.0)
            && scale>=0.05 && scale<=8.0 && alpha>=1 && alpha<=255;
        int model=valid ? PrecacheModel(sprite,true) : 0;
        gBeaconMarkerTypes.SetValue(type,model>0 ? 1 : 0);
        if(model>0)
        {
            AddFileToDownloadsTable(asset);AddFileToDownloadsTable(texture);
        }
        else LogMessage("[Shelter] WARNING beacon marker for %s disabled: invalid/missing sprite or visual settings; travel remains available",type);
    }
    delete types;return true;
}

void SyncBeaconMarker(int b,float now=-1.0)
{
    if(b<0 || b>=MAX_BUILDINGS)return;
    if(!BeaconEffective(b)){RemoveBeaconMarker(b);return;}
    int enabled;
    if(gBeaconMarkerTypes==null || !gBeaconMarkerTypes.GetValue(gBuildings[b].type,enabled) || enabled==0)
    {RemoveBeaconMarker(b);return;}
    int marker=BeaconMarkerEntity(b),body=Resolve(gBuildings[b].ref);
    if(marker>0 && gBeaconMarkerUID[b]==gBuildings[b].uid && gBeaconMarkerGeneration[b]==gBuildings[b].generation
        && gBeaconMarkerEpoch[b]==gEpoch && gBeaconMarkerRevision[b]==gConfigRevision && gBeaconMarkerBodyRef[b]==gBuildings[b].ref)
    {
        SetEdictFlags(marker,GetEdictFlags(marker)|FL_EDICT_ALWAYS);return;
    }
    if(gBeaconMarkerRef[b]!=0 && gBeaconMarkerRef[b]!=INVALID_ENT_REFERENCE)RemoveBeaconMarker(b);
    if(now<0.0)now=GetGameTime();if(now<gBeaconNextMarkerAttempt[b])return;gBeaconNextMarkerAttempt[b]=now+1.0;
    if(gBeaconMarkerCount>=MAX_CORES || !CanAllocateEntities(1,true))return;
    char path[128],sprite[PLATFORM_MAX_PATH];Format(path,sizeof(path),"Types/%s/Marker",gBuildings[b].type);
    Text(C_Beacons,path,"sprite",sprite,sizeof(sprite),"sprites/wh_frame.vmt");
    float offset[3],rgb[3],position[3];
    if(!BeaconMarkerVector(path,"offset","0 0 72",offset,-512.0,512.0)
        || !BeaconMarkerVector(path,"rgb","80 180 255",rgb,0.0,255.0))return;
    BuildingAttachmentPosition(b,offset,position);
    marker=CreateEntityByName("env_sprite");if(marker<0)return;
    if(marker>=BEACON_MARKER_ENTITY_SLOTS){RemoveEntity(marker);return;}
    DispatchKeyValueVector(marker,"origin",position);DispatchKeyValue(marker,"model",sprite);
    DispatchKeyValue(marker,"rendermode","0");DispatchKeyValue(marker,"renderfx","0");
    DispatchKeyValue(marker,"renderamt","255");DispatchKeyValueFloat(marker,"scale",Decimal(C_Beacons,path,"scale",0.7));
    DispatchKeyValue(marker,"GlowProxySize","64.0");DispatchKeyValue(marker,"framerate","10");
    DispatchKeyValue(marker,"distance","99999");DispatchKeyValue(marker,"spawnflags","0");
    if(!DispatchSpawn(marker) || !CanAllocateEntities(1,true)){RemoveEntity(marker);return;}
    SetEntityRenderColor(marker,RoundToNearest(rgb[0]),RoundToNearest(rgb[1]),RoundToNearest(rgb[2]),Number(C_Beacons,path,"alpha",220));
    SetVariantString("!activator");
    if(!AcceptEntityInput(marker,"SetParent",body,marker)){RemoveEntity(marker);return;}
    // Once parented, TeleportEntity consumes LOCAL coordinates. Supplying the
    // world attachment here would add the parent's origin/rotation a second time.
    float localAngles[3];TeleportEntity(marker,offset,localAngles,NULL_VECTOR);
    SDKHook(marker,SDKHook_SetTransmit,BeaconMarkerTransmit);SetEdictFlags(marker,GetEdictFlags(marker)|FL_EDICT_ALWAYS);
    gBeaconMarkerRef[b]=EntIndexToEntRef(marker);gBeaconMarkerUID[b]=gBuildings[b].uid;gBeaconMarkerGeneration[b]=gBuildings[b].generation;
    gBeaconMarkerEpoch[b]=gEpoch;gBeaconMarkerRevision[b]=gConfigRevision;gBeaconMarkerBodyRef[b]=gBuildings[b].ref;
    gBeaconMarkerBuilding[marker]=b+1;gBeaconMarkerCount++;
}

public Action BeaconMarkerTransmit(int entity,int client)
{
    if(entity<=0 || entity>=BEACON_MARKER_ENTITY_SLOTS || !BeaconActor(client))return Plugin_Handled;
    int b=gBeaconMarkerBuilding[entity]-1;
    return b>=0 && b<MAX_BUILDINGS && BeaconMarkerEntity(b)==entity && BeaconEffective(b)
        && gBeaconMarkerUID[b]==gBuildings[b].uid && gBeaconMarkerGeneration[b]==gBuildings[b].generation
        && gBeaconMarkerEpoch[b]==gEpoch && gBeaconMarkerRevision[b]==gConfigRevision ? Plugin_Continue : Plugin_Handled;
}

void BeaconTick(float now)
{
    // Called by the existing 0.25-second world timer. No player/world trace scan.
    for(int b=0;b<MAX_BUILDINGS;b++)
        if((gBuildings[b].used && gBuildings[b].kind==Kind_Beacon)
            || (gBeaconMarkerRef[b]!=0 && gBeaconMarkerRef[b]!=INVALID_ENT_REFERENCE))SyncBeaconMarker(b,now);
}
