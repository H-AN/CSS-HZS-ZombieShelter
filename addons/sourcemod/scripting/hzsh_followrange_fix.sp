#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <sdktools>
#include <dhooks>
#undef REQUIRE_PLUGIN
#include <HanZombieScenarioAPI>

// Focused extraction of Ducheese's HZSZombieSkill/detours.inc.
// No skill, sky, BOT, client creation or NPC target selection is included.
#define GAMEDATA "hzsh_followrange_fix.gamedata"
#define PATCH_SIZE 6

public Plugin myinfo={name="HZS focused follow range fix",author="Ducheese / Codex",
    description="Validated Win64 following range/path patches and HZS-only Idle detour",version="1.0.0",url=""};

Address gSites[2];
int gOriginal[2][PATCH_SIZE];
bool gOwned[2],gReused[2],gReady,gIdleEnabled;
Handle gIdle;
char gStartupLog[PLATFORM_MAX_PATH];
static const int EXPECTED[2][PATCH_SIZE]={{0x0F,0x87,0xE1,0x00,0x00,0x00},{0x0F,0x87,0xBF,0x00,0x00,0x00}};
static const char LABELS[][]={"RangeJA(distance>2000)","PathLengthJA(path>4000)"};

void StartupMark(const char[] message)
{
    LogToFileEx(gStartupLog,"[HZSHFollowFix] %s",message);
    PrintToServer("[HZSHFollowFix] %s",message);
}

public APLRes AskPluginLoad2(Handle myself,bool late,char[] error,int maxlength)
{
    MarkNativeAsOptional("Han_IsZombie");return APLRes_Success;
}

bool BytesMatch(int site,bool patched)
{
    if(gSites[site]==Address_Null)return false;
    for(int i=0;i<PATCH_SIZE;i++)
        if(LoadFromAddress(gSites[site]+view_as<Address>(i),NumberType_Int8)!=(patched ? 0x90 : EXPECTED[site][i]))return false;
    return true;
}

bool ValidateSite(int site)
{
    if(gSites[site]==Address_Null)return false;
    bool original=BytesMatch(site,false),patched=BytesMatch(site,true);
    if(!original && !patched)
    {
        char actual[64];
        for(int i=0;i<PATCH_SIZE;i++)Format(actual,sizeof(actual),"%s%02X ",actual,LoadFromAddress(gSites[site]+view_as<Address>(i),NumberType_Int8));
        LogError("[HZSHFollowFix] unexpected %s bytes: %s; no patches applied",LABELS[site],actual);return false;
    }
    for(int i=0;i<PATCH_SIZE;i++)gOriginal[site][i]=LoadFromAddress(gSites[site]+view_as<Address>(i),NumberType_Int8);
    gReused[site]=patched;return true;
}

void RestoreOwned(bool rollback=false)
{
    for(int site=0;site<2;site++)if(gOwned[site])
    {
        if(!rollback && !BytesMatch(site,true))
        {LogError("[HZSHFollowFix] %s changed by another writer; restore refused",LABELS[site]);continue;}
        StartupMark(site==0 ? "Range owned restore StoreToAddress block BEFORE" : "Path owned restore StoreToAddress block BEFORE");
        for(int i=0;i<PATCH_SIZE;i++)StoreToAddress(gSites[site]+view_as<Address>(i),gOriginal[site][i],NumberType_Int8);
        StartupMark(site==0 ? "Range owned restore StoreToAddress block AFTER" : "Path owned restore StoreToAddress block AFTER");
        gOwned[site]=false;LogMessage("[HZSHFollowFix] restored owned %s bytes",LABELS[site]);
    }
}

public void OnPluginStart()
{
    BuildPath(Path_SM,gStartupLog,sizeof(gStartupLog),"logs/support_startup.log");
    StartupMark("OnPluginStart ENTER");
    RegAdminCmd("sm_hzsh_follow_patch_status",PatchStatus,ADMFLAG_ROOT);
    StartupMark("LoadGameConfigFile BEFORE");
    Handle gc=LoadGameConfigFile(GAMEDATA);if(gc==null)SetFailState("[HZSHFollowFix] gamedata missing");
    StartupMark("LoadGameConfigFile AFTER");
    if(GameConfGetOffset(gc,"PlatformPointerBytes")!=8){delete gc;SetFailState("[HZSHFollowFix] this audited patch supports Windows x64 only");}
    Address range=GameConfGetAddress(gc,"RangeContext"),path=GameConfGetAddress(gc,"PathContext");
    int rangeOffset=GameConfGetOffset(gc,"RangePatchOffset"),pathOffset=GameConfGetOffset(gc,"PathPatchOffset");
    Address idle=GameConfGetAddress(gc,"CHostage_Idle");
    if(range==Address_Null || path==Address_Null || idle==Address_Null || rangeOffset!=23 || pathOffset!=19)
    {delete gc;SetFailState("[HZSHFollowFix] audited Win64 signatures/offsets missing; no writes");}
    gSites[0]=range+view_as<Address>(rangeOffset);gSites[1]=path+view_as<Address>(pathOffset);
    if(!ValidateSite(0) || !ValidateSite(1)){delete gc;SetFailState("[HZSHFollowFix] original/already-patched byte validation failed");}
    StartupMark("both original/already-patched byte sites verified");
    // Both byte sites and the exact Idle entry signature resolve before any write.
    StartupMark("DHookCreateDetour BEFORE");
    // Win64 uses the same Microsoft register ABI for CDECL and member calls.
    // Declare CHostage* explicitly as the first RCX pointer argument, avoiding
    // the extension's implicit-this parameter setup while preserving Idle's
    // original void(CHostage*) machine signature.
    gIdle=DHookCreateDetour(Address_Null,CallConv_CDECL,ReturnType_Void,ThisPointer_Ignore);
    StartupMark("DHookCreateDetour AFTER");
    if(gIdle!=null)
    {
        StartupMark("DHookAddParam explicit first CBaseEntity (Win64 RCX) BEFORE");
        DHookAddParam(gIdle,HookParamType_CBaseEntity);
        StartupMark("DHookAddParam explicit first CBaseEntity (Win64 RCX) AFTER");
    }
    StartupMark("DHookSetFromConf BEFORE (only if non-null handle)");
    bool configured=gIdle!=null && DHookSetFromConf(gIdle,gc,SDKConf_Signature,"CHostage_Idle");
    StartupMark("DHookSetFromConf AFTER");
    if(!configured)
    {delete gc;SetFailState("[HZSHFollowFix] cannot prepare Idle detour; no writes");}
    delete gc;
    // Configuration/startup has not finished during the initial plugin load.
    // Keep the audited addresses and prepared handle, then enable Idle and
    // write the two byte sites together after the map configs have completed.
    StartupMark("OnPluginStart prepared; waiting for OnConfigsExecuted; ready=0, no detour or byte writes");
}

public void OnConfigsExecuted()
{
    if(gReady || gIdleEnabled)return;
    StartupMark("OnConfigsExecuted ENTER; deferred activation BEFORE");
    if(gIdle==null)SetFailState("[HZSHFollowFix] deferred activation missing prepared Idle detour; no writes");
    // Another plugin may have changed bytes between preparation and configs.
    // Revalidate both sites before enabling or claiming either byte patch.
    if(!ValidateSite(0) || !ValidateSite(1))SetFailState("[HZSHFollowFix] deferred byte validation failed; no detour or byte writes");
    StartupMark("DHookEnableDetour BEFORE");
    bool enabled=DHookEnableDetour(gIdle,false,IdlePre);
    StartupMark("DHookEnableDetour AFTER");
    if(!enabled)SetFailState("[HZSHFollowFix] cannot enable Idle detour; byte sites unchanged");
    gIdleEnabled=true;
    for(int site=0;site<2;site++)
    {
        if(gReused[site]){LogMessage("[HZSHFollowFix] recognized pre-existing %s NOPs; no ownership claimed",LABELS[site]);continue;}
        gOwned[site]=true;
        StartupMark(site==0 ? "Range StoreToAddress block BEFORE (6 bytes)" : "Path StoreToAddress block BEFORE (6 bytes)");
        for(int i=0;i<PATCH_SIZE;i++)StoreToAddress(gSites[site]+view_as<Address>(i),0x90,NumberType_Int8);
        StartupMark(site==0 ? "Range StoreToAddress block AFTER; verification BEFORE" : "Path StoreToAddress block AFTER; verification BEFORE");
        if(!BytesMatch(site,true))
        {RestoreOwned(true);SetFailState("[HZSHFollowFix] post-write verification failed; own byte writes rolled back");}
        StartupMark(site==0 ? "Range post-write verification AFTER success" : "Path post-write verification AFTER success");
        LogMessage("[HZSHFollowFix] patched verified %s site=%X",LABELS[site],gSites[site]);
    }
    gReady=true;LogMessage("[HZSHFollowFix] READY Win64 Range2000+Path4000 removed; Idle suppressed only for Han_IsZombie entities");
}

public MRESReturn IdlePre(DHookParam params)
{
    int entity=DHookGetParam(params,1);
    if(entity>MaxClients && IsValidEntity(entity)
        && GetFeatureStatus(FeatureType_Native,"Han_IsZombie")==FeatureStatus_Available && Han_IsZombie(entity))return MRES_Supercede;
    return MRES_Ignored;
}

public Action PatchStatus(int client,int args)
{
    ReplyToCommand(client,"[HZSHFollowFix] ready=%d idle=%d range_nop=%d path_nop=%d owned=%d,%d reused=%d,%d",
        gReady,gIdleEnabled,BytesMatch(0,true),BytesMatch(1,true),gOwned[0],gOwned[1],gReused[0],gReused[1]);return Plugin_Handled;
}

public void OnPluginEnd()
{
    gReady=false;
    if(gIdleEnabled && gIdle!=null)
    {
        StartupMark("OnPluginEnd DHookDisableDetour BEFORE");
        DHookDisableDetour(gIdle,false,IdlePre);
        StartupMark("OnPluginEnd DHookDisableDetour AFTER");
    }
    gIdleEnabled=false;delete gIdle;RestoreOwned();
}
