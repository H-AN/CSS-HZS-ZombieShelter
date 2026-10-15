#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>

// Ducheese's hzs_botaddfix Reset-loop patch, with audited Win64 byte checks.
public Plugin myinfo={name="HZS Bot Add Fix guarded Win64",author="Ducheese / Codex",
    description="Validated InitializeHostageInfo loop bypass; no BOT creation",version="1.0.0",url=""};
Address gSite;
bool gOwned,gReady,gReused;
char gStartupLog[PLATFORM_MAX_PATH];
static const int ORIGINAL[]={0x3B,0x35,0x2C,0x44,0x4A,0x00};
static const int PATCHED[]={0x83,0xFE,0x00,0x90,0x90,0x90};

void StartupMark(const char[] message)
{
    LogToFileEx(gStartupLog,"[BotAddFix] %s",message);
    PrintToServer("[BotAddFix] %s",message);
}

bool Matches(bool patched)
{
    if(gSite==Address_Null)return false;
    for(int i=0;i<6;i++)if(LoadFromAddress(gSite+view_as<Address>(i),NumberType_Int8)!=(patched ? PATCHED[i] : ORIGINAL[i]))return false;
    return true;
}

public void OnPluginStart()
{
    BuildPath(Path_SM,gStartupLog,sizeof(gStartupLog),"logs/support_startup.log");
    StartupMark("OnPluginStart ENTER");
    RegAdminCmd("sm_hzsh_botadd_patch_status",Status,ADMFLAG_ROOT);
    StartupMark("LoadGameConfigFile BEFORE");
    Handle gc=LoadGameConfigFile("hzs_botaddfix_guarded.gamedata");
    StartupMark("LoadGameConfigFile AFTER");
    if(gc==null)SetFailState("[BotAddFix] guarded gamedata missing");
    Address reset=GameConfGetAddress(gc,"CSGameState_Reset");int offset=GameConfGetOffset(gc,"LoopBackedge");
    if(GameConfGetOffset(gc,"PlatformPointerBytes")!=8 || reset==Address_Null || offset!=382)
    {delete gc;SetFailState("[BotAddFix] guarded Win64 signature/offset mismatch; no writes");}
    delete gc;gSite=reset+view_as<Address>(offset);
    StartupMark("reset site resolved; read-only existing/original validation BEFORE");
    if(Matches(true)){gReused=true;gReady=true;LogMessage("[BotAddFix] guarded recognized pre-existing verified reset-loop patch; no ownership claimed");return;}
    if(!Matches(false))SetFailState("[BotAddFix] guarded unexpected original reset-loop bytes; no writes");
    StartupMark("original reset-loop bytes verified; StoreToAddress block BEFORE (6 bytes)");
    gOwned=true;
    for(int i=0;i<6;i++)StoreToAddress(gSite+view_as<Address>(i),PATCHED[i],NumberType_Int8);
    StartupMark("StoreToAddress block AFTER; post-write verification BEFORE");
    if(!Matches(true))
    {
        StartupMark("post-write verification FAILED; rollback StoreToAddress block BEFORE");
        for(int i=0;i<6;i++)StoreToAddress(gSite+view_as<Address>(i),ORIGINAL[i],NumberType_Int8);
        StartupMark("rollback StoreToAddress block AFTER");
        gOwned=false;SetFailState("[BotAddFix] guarded post-write validation failed; own writes rolled back");
    }
    StartupMark("post-write verification AFTER success");
    gReady=true;LogMessage("[BotAddFix] patched back-edge (guarded Win64) reset=%X offset=382 site=%X; original and result verified",reset,gSite);
}

public Action Status(int client,int args)
{
    ReplyToCommand(client,"[BotAddFix] guarded ready=%d bytes_verified=%d owned=%d reused=%d",gReady,Matches(true),gOwned,gReused);return Plugin_Handled;
}

public void OnPluginEnd()
{
    gReady=false;
    if(!gOwned)return;
    if(!Matches(true)){LogError("[BotAddFix] guarded patch changed by another writer; restore refused");return;}
    StartupMark("OnPluginEnd owned restore StoreToAddress block BEFORE");
    for(int i=0;i<6;i++)StoreToAddress(gSite+view_as<Address>(i),ORIGINAL[i],NumberType_Int8);
    StartupMark("OnPluginEnd owned restore StoreToAddress block AFTER");
    gOwned=false;LogMessage("[BotAddFix] guarded restored owned reset-loop bytes; BOT safety prerequisite no longer active");
}
