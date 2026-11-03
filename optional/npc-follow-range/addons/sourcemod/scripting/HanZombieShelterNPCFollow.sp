/*
 * HZS NPC Follow Range
 * Copyright (c) 2025 Ducheese
 * Copyright (c) 2026 H-AN
 *
 * Upstream distance-check signatures and patch concept:
 * https://github.com/Ducheese/hzs-supporting-plugin
 * H-AN changes: independent optional packaging, original-byte preflight,
 * owned-byte restoration, two distance checks only, and read-only Idle guard.
 * The guarded implementation is retained from the reviewed local adapter.
 * See the package-root LICENSE.upstream-MIT.txt for the MIT copyright and permission notice.
 *
 * Reviewed Windows x64 server build only. This plugin does not hook or patch
 * CHostage::Idle and does not change rescue, dead-leader or explicit stop-follow
 * cleanup. Other engine builds require a separate byte/signature review.
 */

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

Address gRange;
Address gPath;
Address gIdle;
bool gRangeOwned;
bool gPathOwned;
bool gPreflight;

static const int ORIGINAL_RANGE[] = {0x0F, 0x87, 0xE1, 0x00, 0x00, 0x00};
static const int ORIGINAL_PATH[] = {0x0F, 0x87, 0xBF, 0x00, 0x00, 0x00};
static const int ORIGINAL_IDLE[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D,
    0x99, 0x0C, 0x0A, 0x00, 0x00, 0x4C, 0x8B, 0xC9, 0x8B, 0x13, 0x85, 0xD2,
    0x74, 0x43, 0xB8, 0xFF, 0x1F, 0x00, 0x00, 0x83, 0xFA, 0xFF, 0x74, 0x03};

public Plugin myinfo =
{
    name = "HZS NPC Follow Range",
    author = "Ducheese & H-AN",
    description = "Guarded NPC following distance patches for the reviewed CS:S Win64 build",
    version = "1.0.0",
    url = "https://github.com/H-AN/CSS-HZS-ZombieShelter"
};

// Only the two distance checks are changed. Idle remains intact: its rescue,
// dead-leader and explicit stop-follow cleanup paths must not be bypassed.
bool MatchIdleOriginal()
{
    if (gIdle == Address_Null)
        return false;
    for (int i = 0; i < sizeof(ORIGINAL_IDLE); i++)
        if ((LoadFromAddress(gIdle + view_as<Address>(i), NumberType_Int8) & 255) != ORIGINAL_IDLE[i])
            return false;
    return true;
}

bool MatchBranch(Address address, const int[] original, bool enabled)
{
    if (address == Address_Null)
        return false;
    for (int i = 0; i < 6; i++)
    {
        int actual = LoadFromAddress(address + view_as<Address>(i), NumberType_Int8) & 255;
        if (actual != (enabled ? 0x90 : original[i]))
            return false;
    }
    return true;
}

bool SetBranch(Address address, const int[] original, bool &owned, bool enabled)
{
    if (enabled == owned)
        return MatchBranch(address, original, enabled);
    if (!MatchBranch(address, original, owned))
    {
        LogError("[ZSHNPCFollow] Refused branch write: expected owned state does not match at %X", address);
        return false;
    }
    for (int i = 0; i < 6; i++)
        StoreToAddress(address + view_as<Address>(i), enabled ? 0x90 : original[i], NumberType_Int8);
    owned = enabled;
    return MatchBranch(address, original, enabled);
}

bool RestoreAll()
{
    // Independent restoration attempts. Unknown bytes are never overwritten.
    bool range = SetBranch(gRange, ORIGINAL_RANGE, gRangeOwned, false);
    bool path = SetBranch(gPath, ORIGINAL_PATH, gPathOwned, false);
    bool idle = MatchIdleOriginal();
    PrintToServer("[ZSHNPCFollow] RESTORE range_original=%d path_original=%d idle_original_all32=%d idle_writes=0 scope=distance_only", range, path, idle);
    return range && path && idle;
}

void HexBytes(Address address, int count, char[] output, int capacity)
{
    output[0] = '\0';
    if (address == Address_Null)
    {
        strcopy(output, capacity, "unavailable");
        return;
    }
    char byte[5];
    for (int i = 0; i < count; i++)
    {
        FormatEx(byte, sizeof(byte), i == 0 ? "%02X" : " %02X", LoadFromAddress(address + view_as<Address>(i), NumberType_Int8) & 255);
        StrCat(output, capacity, byte);
    }
}

void PrintState()
{
    bool range = MatchBranch(gRange, ORIGINAL_RANGE, gRangeOwned);
    bool path = MatchBranch(gPath, ORIGINAL_PATH, gPathOwned);
    bool idle = MatchIdleOriginal();
    int enabled = -1;
    if (range && path && idle)
    {
        if (!gRangeOwned && !gPathOwned)
            enabled = 0;
        else if (gRangeOwned && gPathOwned)
            enabled = 1;
    }
    PrintToServer("[ZSHNPCFollow] STATE enabled=%d preflight=%d range_owned=%d path_owned=%d range_readback_ok=%d path_readback_ok=%d idle_original_all32_ok=%d idle_writes=0 scope=distance_only",
        enabled, gPreflight, gRangeOwned, gPathOwned, range, path, idle);
    char bytes[128];
    HexBytes(gRange, 6, bytes, sizeof(bytes));
    PrintToServer("[ZSHNPCFollow] BYTES range=%s", bytes);
    HexBytes(gPath, 6, bytes, sizeof(bytes));
    PrintToServer("[ZSHNPCFollow] BYTES path=%s", bytes);
    HexBytes(gIdle, sizeof(ORIGINAL_IDLE), bytes, sizeof(bytes));
    PrintToServer("[ZSHNPCFollow] BYTES idle_original=%s", bytes);
}

// Internal implementation values only. Public commands expose enabled 0/1.
bool ChangeMode(int mode)
{
    if (mode == 0)
        return RestoreAll();
    if (mode != 2)
        return false;
    if (!MatchBranch(gRange, ORIGINAL_RANGE, gRangeOwned)
        || !MatchBranch(gPath, ORIGINAL_PATH, gPathOwned) || !MatchIdleOriginal())
    {
        LogError("[ZSHNPCFollow] Enable refused: current owned branches or original Idle guard mismatch");
        return false;
    }
    if (!SetBranch(gRange, ORIGINAL_RANGE, gRangeOwned, true))
    {
        RestoreAll();
        return false;
    }
    if (!SetBranch(gPath, ORIGINAL_PATH, gPathOwned, true))
    {
        RestoreAll();
        return false;
    }
    return MatchBranch(gRange, ORIGINAL_RANGE, true)
        && MatchBranch(gPath, ORIGINAL_PATH, true) && MatchIdleOriginal();
}

public Action CommandStatus(int args)
{
    PrintState();
    return Plugin_Handled;
}

public Action CommandEnabled(int args)
{
    if (!gPreflight)
    {
        PrintToServer("[ZSHNPCFollow] Refused: preflight unavailable");
        return Plugin_Handled;
    }
    if (args == 0)
    {
        PrintState();
        return Plugin_Handled;
    }
    char argument[16];
    GetCmdArg(1, argument, sizeof(argument));
    if (args != 1 || strlen(argument) != 1 || (argument[0] != '0' && argument[0] != '1'))
    {
        PrintToServer("[ZSHNPCFollow] Usage: sm_zsh_npc_follow_enabled [0|1]");
        return Plugin_Handled;
    }
    bool enabled = argument[0] == '1';
    bool okay = ChangeMode(enabled ? 2 : 0);
    if (!okay)
    {
        bool restored = RestoreAll();
        LogError("[ZSHNPCFollow] Change failed requested_enabled=%d independent_restoration_success=%d", enabled, restored);
    }
    PrintToServer("[ZSHNPCFollow] CHANGE requested_enabled=%d success=%d scope=distance_only", enabled, okay);
    PrintState();
    return Plugin_Handled;
}

public void OnPluginStart()
{
    GameData config = new GameData("hzsh_npc_follow.gamedata");
    if (config == null)
    {
        SetFailState("HZS NPC follow range gamedata unavailable; no writes performed");
        return;
    }
    gRange = config.GetAddress("RangeJAAddr");
    gPath = config.GetAddress("PathLengthJAAddr");
    gIdle = config.GetMemSig("CHostage_Idle");
    delete config;
    bool rangeOriginal = MatchBranch(gRange, ORIGINAL_RANGE, false);
    bool pathOriginal = MatchBranch(gPath, ORIGINAL_PATH, false);
    bool idleOriginal = MatchIdleOriginal();
    if (!rangeOriginal || !pathOriginal || !idleOriginal)
    {
        SetFailState("HZS NPC follow range original-byte preflight mismatch (range=%d path=%d idle=%d); no writes performed", rangeOriginal, pathOriginal, idleOriginal);
        return;
    }
    gPreflight = true;
    RegServerCmd("sm_zsh_npc_follow_status", CommandStatus);
    RegServerCmd("sm_zsh_npc_follow_enabled", CommandEnabled);
    if (!ChangeMode(2))
    {
        bool restored = RestoreAll();
        PrintState();
        SetFailState("HZS NPC follow range automatic enable failed; independent_restoration_success=%d", restored);
        return;
    }
    LogMessage("[ZSHNPCFollow] START enabled=1 original_all_sites_verified=1 current_Win64_build_only=1 idle_writes=0 scope=distance_only");
    PrintState();
}

public void OnMapStart()
{
    if (!gPreflight)
        return;
    // Verification only: no writes, no reclaiming bytes, no Idle bypass.
    bool range = MatchBranch(gRange, ORIGINAL_RANGE, gRangeOwned);
    bool path = MatchBranch(gPath, ORIGINAL_PATH, gPathOwned);
    bool idle = MatchIdleOriginal();
    if (!range || !path || !idle)
        LogError("[ZSHNPCFollow] Map-start verification failed range=%d path=%d idle_original=%d; no writes performed", range, path, idle);
    else
        LogMessage("[ZSHNPCFollow] Map-start owned-distance/original-Idle verification passed; no writes performed");
    PrintState();
}

public void OnPluginEnd()
{
    if (!gPreflight)
        return;
    bool okay = RestoreAll();
    PrintToServer("[ZSHNPCFollow] END restoration_success=%d idle_writes=0 scope=distance_only", okay);
    PrintState();
}
