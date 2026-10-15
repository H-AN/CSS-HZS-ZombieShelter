#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>

// Read-only startup localization. Never writes memory, enables a detour,
// creates an entity/client, or sends a BOT command.
public Plugin myinfo = {
    name = "HZS patch read-only diagnostic",
    author = "Codex",
    description = "Step markers around gameconfig address resolution and byte reads",
    version = "1.0.0",
    url = ""
};

char gLogPath[PLATFORM_MAX_PATH];
int gStep;

void Mark(const char[] message)
{
    gStep++;
    // LogToFileEx opens/closes its own output for each marker. The fixed file
    // complements console output when a following native faults the process.
    LogToFileEx(gLogPath, "[PatchDiagnostic] step=%d %s", gStep, message);
    PrintToServer("[PatchDiagnostic] step=%d %s", gStep, message);
}

Address ReadAddress(Handle config, const char[] name)
{
    char message[256];
    Format(message, sizeof(message), "GameConfGetAddress(%s) BEFORE", name);
    Mark(message);
    Address result = GameConfGetAddress(config, name);
    Format(message, sizeof(message), "GameConfGetAddress(%s) AFTER pseudo=0x%08X", name, result);
    Mark(message);
    return result;
}

int ReadOffset(Handle config, const char[] name)
{
    char message[256];
    Format(message, sizeof(message), "GameConfGetOffset(%s) BEFORE", name);
    Mark(message);
    int result = GameConfGetOffset(config, name);
    Format(message, sizeof(message), "GameConfGetOffset(%s) AFTER value=%d", name, result);
    Mark(message);
    return result;
}

void ReadBytes(Address base, int offset, int count, const char[] label)
{
    char message[256];
    if (base == Address_Null || offset < 0)
    {
        Format(message, sizeof(message), "%s SKIP null base or negative offset; no read", label);
        Mark(message);
        return;
    }
    for (int i = 0; i < count; i++)
    {
        Address current = base + view_as<Address>(offset + i);
        Format(message, sizeof(message), "LoadFromAddress(%s,+%d) BEFORE pseudo=0x%08X", label, offset + i, current);
        Mark(message);
        int value = LoadFromAddress(current, NumberType_Int8);
        Format(message, sizeof(message), "LoadFromAddress(%s,+%d) AFTER byte=%02X", label, offset + i, value & 0xFF);
        Mark(message);
    }
}

public void OnPluginStart()
{
    BuildPath(Path_SM, gLogPath, sizeof(gLogPath), "logs/hzsh_patch_diagnostic.log");
    Mark("OnPluginStart ENTER; READ ONLY: no StoreToAddress, detour or entity/client creation");

    Mark("LoadGameConfigFile(hzs_botaddfix_guarded.gamedata) BEFORE");
    Handle bot = LoadGameConfigFile("hzs_botaddfix_guarded.gamedata");
    Mark("LoadGameConfigFile(hzs_botaddfix_guarded.gamedata) AFTER");
    if (bot != null)
    {
        int pointerBytes = ReadOffset(bot, "PlatformPointerBytes");
        Address reset = ReadAddress(bot, "CSGameState_Reset");
        int offset = ReadOffset(bot, "LoopBackedge");
        Mark("bot config resolved; entering read-only byte samples");
        if (pointerBytes == 8)
        {
            ReadBytes(reset, 0, 1, "Reset entry");
            ReadBytes(reset, offset, 6, "Reset backedge");
        }
        else Mark("bot byte samples SKIP: audited pointer-size offset is not 8");
        Mark("delete bot gameconfig BEFORE");
        delete bot;
        Mark("delete bot gameconfig AFTER");
    }
    else Mark("bot gameconfig NULL; no bot address or byte reads");

    Mark("LoadGameConfigFile(hzsh_followrange_fix.gamedata) BEFORE");
    Handle follow = LoadGameConfigFile("hzsh_followrange_fix.gamedata");
    Mark("LoadGameConfigFile(hzsh_followrange_fix.gamedata) AFTER");
    if (follow != null)
    {
        int pointerBytes = ReadOffset(follow, "PlatformPointerBytes");
        Address range = ReadAddress(follow, "RangeContext");
        Address path = ReadAddress(follow, "PathContext");
        Address idle = ReadAddress(follow, "CHostage_Idle");
        int rangeOffset = ReadOffset(follow, "RangePatchOffset");
        int pathOffset = ReadOffset(follow, "PathPatchOffset");
        Mark("follow config resolved; entering read-only byte samples");
        if (pointerBytes == 8)
        {
            ReadBytes(range, 0, 1, "Range context entry");
            ReadBytes(range, rangeOffset, 6, "Range branch");
            ReadBytes(path, 0, 1, "Path context entry");
            ReadBytes(path, pathOffset, 6, "Path branch");
            ReadBytes(idle, 0, 1, "Idle entry");
        }
        else Mark("follow byte samples SKIP: audited pointer-size offset is not 8");
        Mark("delete follow gameconfig BEFORE");
        delete follow;
        Mark("delete follow gameconfig AFTER");
    }
    else Mark("follow gameconfig NULL; no follow address or byte reads");
    Mark("OnPluginStart COMPLETE; no memory writes or detours attempted");
}
