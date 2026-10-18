#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <sdktools>
#undef REQUIRE_PLUGIN
#include <HanZombieShelterAPI>

public Plugin myinfo =
{
    name = "HanZombieShelter Environment",
    author = "H-AN",
    description = "API-driven sky and opt-in map lighting for Zombie Shelter",
    version = "0.6.0",
    url = ""
};

enum struct EnvironmentConfig
{
    bool enabled;
    bool driveLights;
    bool controlStyle;
    char daySky[128];
    char nightSky[128];
    char dayRelay[64];
    char nightRelay[64];
    char dayStyle[2];
    char nightStyle[2];
    float initialDelay;
    float phaseDelay;
    float correctionDelay;
}

EnvironmentConfig g_Config;
ConVar g_Enabled, g_Sky;
Handle g_Pending, g_Watch;
bool g_MapReady, g_Applying, g_Owned;
int g_MapEpoch, g_ConfigEpoch, g_ProviderEpoch;
int g_Swapper[2] = {INVALID_ENT_REFERENCE, INVALID_ENT_REFERENCE};
int g_LastPhase = -1, g_LastRevision = -1;
float g_LastWrite = -100.0;
char g_LastSky[128], g_PreviousStyle[64];
char g_LastProblem[192];

public void OnPluginStart()
{
    g_Enabled = CreateConVar("sm_hzsh_environment_enabled", "1", "Enable API-driven Shelter environment", FCVAR_NONE, true, 0.0, true, 1.0);
    g_Enabled.AddChangeHook(EnabledChanged);
    g_Sky = FindConVar("sv_skyname");
    if (g_Sky != null) g_Sky.AddChangeHook(SkyChanged);
    RegAdminCmd("sm_hzsh_env_status", CommandStatus, ADMFLAG_GENERIC, "Show current API, sky and map-light contract");
    RegAdminCmd("sm_hzsh_env_reload", CommandReload, ADMFLAG_ROOT, "Reload global and map environment configuration");
    RegAdminCmd("sm_hzsh_env_sync", CommandSync, ADMFLAG_ROOT, "Reapply the latest authoritative environment state");
    HookEvent("round_start", RoundStart, EventHookMode_PostNoCopy);
    g_Watch = CreateTimer(1.0, Watchdog, _, TIMER_REPEAT);
}

public void OnMapStart()
{
    g_MapEpoch++;
    g_MapReady = true;
    g_Owned = false;
    g_LastPhase = -1;
    g_LastRevision = -1;
    g_LastProblem[0] = '\0';
    g_Swapper[0] = g_Swapper[1] = INVALID_ENT_REFERENCE;
    LoadConfiguration();
    PrepareSkies();
    RequestSync(g_Config.initialDelay);
}

public void OnMapEnd()
{
    g_MapReady = false;
    g_MapEpoch++;
    g_Owned = false;
    CancelPending();
    g_Swapper[0] = g_Swapper[1] = INVALID_ENT_REFERENCE;
}

public void OnPluginEnd()
{
    CancelPending();
    ReleaseEnvironment();
    RemoveSwappers();
    delete g_Watch;
}

public void OnConfigsExecuted() { RequestSync(g_Config.initialDelay); }
public void OnAllPluginsLoaded() { g_ProviderEpoch++; RequestSync(0.5); }
public void OnLibraryAdded(const char[] name)
{
    if (StrEqual(name, "HanZombieShelter")) { g_ProviderEpoch++; RequestSync(0.5); }
}
public void OnLibraryRemoved(const char[] name)
{
    if (StrEqual(name, "HanZombieShelter")) { g_ProviderEpoch++; CancelPending(); ReleaseEnvironment(); }
}
public void HZSH_OnEnvironmentStateChanged(int revision) { RequestSync(g_Config.phaseDelay); }
public void RoundStart(Event event, const char[] name, bool dontBroadcast) { RequestSync(g_Config.initialDelay); }
public void EnabledChanged(ConVar cvar, const char[] oldValue, const char[] newValue)
{
    if (!g_Enabled.BoolValue) { CancelPending(); ReleaseEnvironment(); }
    else RequestSync(g_Config.initialDelay);
}
public void SkyChanged(ConVar cvar, const char[] oldValue, const char[] newValue)
{
    if (!g_Applying && g_Owned) RequestSync(g_Config.correctionDelay);
}

void Problem(const char[] message)
{
    if (!StrEqual(message, g_LastProblem))
    {
        LogMessage("[HZSH Environment] %s", message);
        strcopy(g_LastProblem, sizeof(g_LastProblem), message);
    }
}

bool APIAvailable()
{
    return LibraryExists("HanZombieShelter")
        && GetFeatureStatus(FeatureType_Native, "HZSH_GetEnvironmentAPIVersion") == FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native, "HZSH_GetEnvironmentState") == FeatureStatus_Available
        && HZSH_GetEnvironmentAPIVersion() == HZSH_ENVIRONMENT_API_VERSION;
}

void CancelPending() { if (g_Pending != null) { delete g_Pending; g_Pending = null; } }

void RequestSync(float delay)
{
    if (!g_MapReady || !g_Enabled.BoolValue) return;
    CancelPending();
    DataPack pack;
    g_Pending = CreateDataTimer(delay < 0.05 ? 0.05 : delay, ApplyDelayed, pack, TIMER_FLAG_NO_MAPCHANGE);
    pack.WriteCell(g_MapEpoch);
    pack.WriteCell(g_ConfigEpoch);
    pack.WriteCell(g_ProviderEpoch);
}

public Action ApplyDelayed(Handle timer, DataPack pack)
{
    if (g_Pending == timer) g_Pending = null;
    pack.Reset();
    if (pack.ReadCell() != g_MapEpoch || pack.ReadCell() != g_ConfigEpoch || pack.ReadCell() != g_ProviderEpoch) return Plugin_Stop;
    ApplyEnvironment();
    return Plugin_Stop;
}

public Action Watchdog(Handle timer)
{
    if (!g_MapReady || !g_Enabled.BoolValue || !g_Config.enabled) return Plugin_Continue;
    if (!APIAvailable()) { ReleaseEnvironment(); Problem("Waiting for compatible Shelter environment API v1; environment released."); return Plugin_Continue; }
    bool enabled;
    HZSHPhase gamePhase;
    HZSHEnvironmentPhase phase;
    int day, revision;
    if (!HZSH_GetEnvironmentState(enabled, gamePhase, phase, day, revision)) return Plugin_Continue;
    if (!enabled) { ReleaseEnvironment(); return Plugin_Continue; }
    char actual[128], expected[128], style[64];
    if (phase == HZSHEnvironment_Night) strcopy(expected, sizeof(expected), g_Config.nightSky);
    else strcopy(expected, sizeof(expected), g_Config.daySky);
    if (g_Sky != null) g_Sky.GetString(actual, sizeof(actual));
    bool mismatch = !StrEqual(actual, expected);
    if (g_Config.controlStyle)
    {
        GetStringTableData(FindStringTable("lightstyles"), 0, style, sizeof(style));
        if (phase == HZSHEnvironment_Night) mismatch = mismatch || !StrEqual(style, g_Config.nightStyle);
        else mismatch = mismatch || !StrEqual(style, g_Config.dayStyle);
    }
    if (!g_Owned || view_as<int>(phase) != g_LastPhase || revision != g_LastRevision || mismatch)
        RequestSync(g_Config.correctionDelay);
    return Plugin_Continue;
}

void ApplyEnvironment()
{
    if (!g_MapReady || !g_Enabled.BoolValue || !g_Config.enabled || !APIAvailable()) return;
    bool enabled;
    HZSHPhase gamePhase;
    HZSHEnvironmentPhase phase;
    int day, revision;
    if (!HZSH_GetEnvironmentState(enabled, gamePhase, phase, day, revision)) return;
    if (!enabled) { ReleaseEnvironment(); return; }
    int index = phase == HZSHEnvironment_Night ? 1 : 0;
    if (!g_Owned)
    {
        GetStringTableData(FindStringTable("lightstyles"), 0, g_PreviousStyle, sizeof(g_PreviousStyle));
    }
    char desired[128], actual[128];
    if (index == 1) strcopy(desired, sizeof(desired), g_Config.nightSky);
    else strcopy(desired, sizeof(desired), g_Config.daySky);
    if (g_Sky != null) g_Sky.GetString(actual, sizeof(actual));
    if (!StrEqual(actual, desired))
    {
        // Bound competing writers to two applications per second; never write each frame.
        if (GetEngineTime() - g_LastWrite < 0.5) { RequestSync(0.5); return; }
        int swapper = g_Swapper[index];
        if (swapper == INVALID_ENT_REFERENCE || !IsValidEntity(swapper)) { PrepareSkies(); swapper = g_Swapper[index]; }
        g_Applying = true;
        if (swapper != INVALID_ENT_REFERENCE && IsValidEntity(swapper)) AcceptEntityInput(swapper, "Trigger");
        else if (g_Sky != null) g_Sky.SetString(desired);
        g_Applying = false;
        g_LastWrite = GetEngineTime();
    }
    if (g_Sky != null) g_Sky.GetString(actual, sizeof(actual));
    if (!StrEqual(actual, desired)) { Problem("Sky readback mismatch; map lights were not applied."); return; }
    bool lights = !g_Config.driveLights || DriveLights(index == 1);
    if (g_Config.controlStyle) SetLightStyle(0, index == 1 ? g_Config.nightStyle : g_Config.dayStyle);
    strcopy(g_LastSky, sizeof(g_LastSky), desired);
    if (!g_Owned || view_as<int>(phase) != g_LastPhase || revision != g_LastRevision)
        LogMessage("[HZSH Environment] applied phase=%d day=%d revision=%d sky=%s lights=%d style0=%d", index, day, revision, desired, lights, g_Config.controlStyle);
    g_Owned = true;
    g_LastPhase = view_as<int>(phase);
    g_LastRevision = revision;
    if (lights) g_LastProblem[0] = '\0';
}

bool DriveLights(bool night)
{
    int day = FindRelay(g_Config.dayRelay), dark = FindRelay(g_Config.nightRelay);
    if (day == -1 || dark == -1) { Problem("Map light contract incomplete: each configured name must identify exactly one logic_relay."); return false; }
    return AcceptEntityInput(night ? dark : day, "Trigger");
}

int FindRelay(const char[] name)
{
    int found = -1;
    char target[64];
    int entity = -1;
    // logic_relay is a non-networked server entity; compatible references
    // can be negative and must not be converted through EntIndexToEntRef.
    while ((entity = FindEntityByClassname(entity, "logic_relay")) != -1)
    {
        if (!IsValidEntity(entity)) continue;
        GetEntPropString(entity, Prop_Data, "m_iName", target, sizeof(target));
        if (!StrEqual(target, name)) continue;
        if (found != -1) return -1;
        found = entity;
    }
    return found;
}

void ReleaseEnvironment()
{
    if (!g_MapReady || !g_Owned) return;
    char actual[128];
    if (g_Sky != null) g_Sky.GetString(actual, sizeof(actual));
    // Do not overwrite a newer external owner. Release only our last sky.
    if (StrEqual(actual, g_LastSky))
    {
        g_Applying = true;
        if (g_Sky != null) g_Sky.SetString(g_Config.daySky);
        if (g_Config.driveLights) DriveLights(false);
        if (g_Config.controlStyle) SetLightStyle(0, g_PreviousStyle[0] ? g_PreviousStyle : "m");
        g_Applying = false;
    }
    g_Owned = false;
    g_LastPhase = -1;
    g_LastRevision = -1;
}

void RemoveSwappers()
{
    for (int index = 0; index < 2; index++)
    {
        int entity = g_Swapper[index];
        if (entity != INVALID_ENT_REFERENCE && IsValidEntity(entity)) RemoveEntity(entity);
        g_Swapper[index] = INVALID_ENT_REFERENCE;
    }
    // Recover only this module's named entities after an interrupted load.
    // Read the next compatible reference before removing the current one.
    int orphan = FindEntityByClassname(-1, "skybox_swapper");
    char target[64];
    while (orphan != -1)
    {
        int next = FindEntityByClassname(orphan, "skybox_swapper");
        GetEntPropString(orphan, Prop_Data, "m_iName", target, sizeof(target));
        if (StrEqual(target, "hzsh_environment_day_swapper") || StrEqual(target, "hzsh_environment_night_swapper")) RemoveEntity(orphan);
        orphan = next;
    }
}

void PrepareSkies()
{
    if (!g_MapReady) return;
    RemoveSwappers();
    for (int index = 0; index < 2; index++)
    {
        int entity = CreateEntityByName("skybox_swapper");
        if (entity == -1) { Problem("skybox_swapper unavailable; using validated sv_skyname fallback."); continue; }
        DispatchKeyValue(entity, "targetname", index == 0 ? "hzsh_environment_day_swapper" : "hzsh_environment_night_swapper");
        DispatchKeyValue(entity, "SkyboxName", index == 0 ? g_Config.daySky : g_Config.nightSky);
        DispatchSpawn(entity);
        if (IsValidEntity(entity)) g_Swapper[index] = entity;
    }
}

bool ValidSky(const char[] name)
{
    if (!name[0] || StrContains(name, "..") != -1 || name[0] == '/') return false;
    for (int i = 0; name[i]; i++)
        if (!IsCharAlpha(name[i]) && !IsCharNumeric(name[i]) && name[i] != '_' && name[i] != '/' && name[i] != '-') return false;
    static const char sides[][] = {"rt", "bk", "lf", "ft", "up", "dn"};
    char path[PLATFORM_MAX_PATH];
    for (int i = 0; i < 6; i++)
    {
        Format(path, sizeof(path), "materials/skybox/%s%s.vmt", name, sides[i]);
        if (!FileExists(path, true)) return false;
    }
    return true;
}

void ReadLayer(KeyValues kv, EnvironmentConfig config)
{
    config.enabled = kv.GetNum("enabled", config.enabled ? 1 : 0) != 0;
    config.driveLights = kv.GetNum("drive_map_lights", config.driveLights ? 1 : 0) != 0;
    config.controlStyle = kv.GetNum("control_style0", config.controlStyle ? 1 : 0) != 0;
    kv.GetString("day_skybox", config.daySky, sizeof(config.daySky), config.daySky);
    kv.GetString("night_skybox", config.nightSky, sizeof(config.nightSky), config.nightSky);
    kv.GetString("day_relay", config.dayRelay, sizeof(config.dayRelay), config.dayRelay);
    kv.GetString("night_relay", config.nightRelay, sizeof(config.nightRelay), config.nightRelay);
    kv.GetString("day_lightstyle", config.dayStyle, sizeof(config.dayStyle), config.dayStyle);
    kv.GetString("night_lightstyle", config.nightStyle, sizeof(config.nightStyle), config.nightStyle);
    config.initialDelay = kv.GetFloat("initial_apply_delay", config.initialDelay);
    config.phaseDelay = kv.GetFloat("phase_apply_delay", config.phaseDelay);
    config.correctionDelay = kv.GetFloat("correction_delay", config.correctionDelay);
}

bool LoadConfiguration()
{
    EnvironmentConfig next;
    next.enabled = true;
    strcopy(next.daySky, sizeof(next.daySky), "sky_dust");
    strcopy(next.nightSky, sizeof(next.nightSky), "sky_borealis01");
    strcopy(next.dayRelay, sizeof(next.dayRelay), "hzsh_env_day");
    strcopy(next.nightRelay, sizeof(next.nightRelay), "hzsh_env_night");
    strcopy(next.dayStyle, sizeof(next.dayStyle), "n");
    strcopy(next.nightStyle, sizeof(next.nightStyle), "b");
    next.initialDelay = 0.5;
    next.phaseDelay = 0.1;
    next.correctionDelay = 0.1;
    char path[PLATFORM_MAX_PATH], map[PLATFORM_MAX_PATH];
    GetCurrentMap(map, sizeof(map));
    for (int layer = 0; layer < 2; layer++)
    {
        if (layer == 0) BuildPath(Path_SM, path, sizeof(path), "configs/HanZombieShelterEnvironment/Environment.cfg");
        else BuildPath(Path_SM, path, sizeof(path), "configs/HanZombieShelterEnvironment/maps/%s.cfg", map);
        if (!FileExists(path)) continue;
        KeyValues kv = new KeyValues("HanZombieShelterEnvironment");
        if (!kv.ImportFromFile(path)) { delete kv; LogError("Environment configuration rejected; previous configuration retained: %s", path); return false; }
        ReadLayer(kv, next);
        delete kv;
    }
    if (!ValidSky(next.daySky)) { LogError("Invalid day sky '%s'; fallback sky_dust", next.daySky); strcopy(next.daySky, sizeof(next.daySky), "sky_dust"); }
    if (!ValidSky(next.nightSky)) { LogError("Invalid night sky '%s'; fallback sky_borealis01", next.nightSky); strcopy(next.nightSky, sizeof(next.nightSky), "sky_borealis01"); }
    if (next.dayStyle[0] < 'a' || next.dayStyle[0] > 'z' || next.nightStyle[0] < 'a' || next.nightStyle[0] > 'z')
    { LogError("Environment lightstyle must be a single a-z character; configuration retained."); return false; }
    if (next.initialDelay < 0.05 || next.initialDelay > 10.0 || next.phaseDelay < 0.05 || next.phaseDelay > 10.0 || next.correctionDelay < 0.05 || next.correctionDelay > 10.0)
    { LogError("Environment delays must be 0.05..10 seconds; configuration retained."); return false; }
    ReleaseEnvironment();
    g_Config = next;
    g_ConfigEpoch++;
    CancelPending();
    return true;
}

public Action CommandReload(int client, int args)
{
    bool ok = LoadConfiguration();
    if (ok) { PrepareSkies(); RequestSync(g_Config.initialDelay); }
    ReplyToCommand(client, "[HZSH Environment] reload=%d config_epoch=%d", ok, g_ConfigEpoch);
    return Plugin_Handled;
}
public Action CommandSync(int client, int args) { RequestSync(g_Config.phaseDelay); ReplyToCommand(client, "[HZSH Environment] synchronization requested"); return Plugin_Handled; }
public Action CommandStatus(int client, int args)
{
    char sky[128], style[64], nightStyle[64];
    if (g_Sky != null) g_Sky.GetString(sky, sizeof(sky));
    GetStringTableData(FindStringTable("lightstyles"), 0, style, sizeof(style));
    GetStringTableData(FindStringTable("lightstyles"), 33, nightStyle, sizeof(nightStyle));
    ReplyToCommand(client, "[HZSH Environment] api=%d enabled=%d owned=%d phase=%d revision=%d sky=%s style0=%s day_relay=%d night_relay=%d drive_lights=%d control_style0=%d epochs=%d/%d/%d",
        APIAvailable(), g_Enabled.BoolValue && g_Config.enabled, g_Owned, g_LastPhase, g_LastRevision, sky, style,
        FindRelay(g_Config.dayRelay), FindRelay(g_Config.nightRelay), g_Config.driveLights, g_Config.controlStyle, g_MapEpoch, g_ConfigEpoch, g_ProviderEpoch);
    bool modeEnabled;
    HZSHPhase gamePhase;
    HZSHEnvironmentPhase phase;
    int day, revision;
    bool ready = APIAvailable() && HZSH_GetEnvironmentState(modeEnabled, gamePhase, phase, day, revision);
    ReplyToCommand(client, "[HZSH Environment] snapshot_ready=%d mode=%d game_phase=%d environment_phase=%d day=%d revision=%d style33=%s swappers=%d/%d pending=%d",
        ready, modeEnabled, gamePhase, phase, day, revision, nightStyle,
        g_Swapper[0], g_Swapper[1], g_Pending != null);
    return Plugin_Handled;
}

