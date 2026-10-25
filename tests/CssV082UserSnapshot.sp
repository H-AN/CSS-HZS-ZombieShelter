#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <sdktools>
#include <HanZombieShelterAPI>

// Diagnostic export only. This plugin has no restore command, timers, client
// commands, entity setters, material setters, teleport or round-control calls.
public Plugin myinfo =
{
    name = "CSS v0.8.2 user preservation snapshot",
    author = "Local test tooling",
    description = "Export the sole real user's current public API/entity state",
    version = "0.1"
};

int gSnapshotSequence;

bool SnapshotNativesReady()
{
    static const char names[][] =
    {
        "HZSH_GetAPIVersion", "HZSH_GetEnvironmentState", "HZSH_IsRunning",
        "HZSH_GetPhaseTimeRemaining", "HZSH_GetBaseCount", "HZSH_GetBaseInfo",
        "HZSH_GetBasePower", "HZSH_GetBuildingInfo", "HZSH_GetMaterialCount",
        "HZSH_GetMaterialId", "HZSH_GetPlayerMaterialAmount", "HZSH_GetBaseMaterialAmount"
    };
    for (int i = 0; i < sizeof(names); i++)
        if (GetFeatureStatus(FeatureType_Native, names[i]) != FeatureStatus_Available)
            return false;
    return HZSH_GetAPIVersion() >= 2;
}

void SnapshotInt(KeyValues kv, int entity, const char[] property, const char[] field)
{
    char available[96];
    Format(available, sizeof(available), "%s_available", field);
    if (HasEntProp(entity, Prop_Send, property))
    {
        kv.SetNum(available, 1);
        kv.SetNum(field, GetEntProp(entity, Prop_Send, property));
    }
    else if (HasEntProp(entity, Prop_Data, property))
    {
        kv.SetNum(available, 1);
        kv.SetNum(field, GetEntProp(entity, Prop_Data, property));
    }
    else kv.SetNum(available, 0);
}

void SnapshotFloat(KeyValues kv, int entity, const char[] property, const char[] field)
{
    char available[96];
    Format(available, sizeof(available), "%s_available", field);
    if (HasEntProp(entity, Prop_Send, property))
    {
        kv.SetNum(available, 1);
        kv.SetFloat(field, GetEntPropFloat(entity, Prop_Send, property));
    }
    else if (HasEntProp(entity, Prop_Data, property))
    {
        kv.SetNum(available, 1);
        kv.SetFloat(field, GetEntPropFloat(entity, Prop_Data, property));
    }
    else kv.SetNum(available, 0);
}

void SnapshotVector(KeyValues kv, int entity, const char[] property, const char[] field)
{
    char available[96];
    Format(available, sizeof(available), "%s_available", field);
    float value[3];
    if (HasEntProp(entity, Prop_Data, property))
        GetEntPropVector(entity, Prop_Data, property, value);
    else if (HasEntProp(entity, Prop_Send, property))
        GetEntPropVector(entity, Prop_Send, property, value);
    else
    {
        kv.SetNum(available, 0);
        return;
    }
    kv.SetNum(available, 1);
    kv.SetVector(field, value);
}

void SnapshotEntity(KeyValues kv, int entity)
{
    kv.SetNum("entity_index", entity);
    kv.SetNum("entity_ref", EntIndexToEntRef(entity));
    char value[PLATFORM_MAX_PATH];
    GetEntityClassname(entity, value, sizeof(value));
    kv.SetString("classname", value);
    if (HasEntProp(entity, Prop_Data, "m_iName"))
    {
        GetEntPropString(entity, Prop_Data, "m_iName", value, sizeof(value));
        kv.SetString("targetname", value);
    }
    // SDKTools exposes SetEntityModel; model reads use the existing property.
    if (HasEntProp(entity, Prop_Data, "m_ModelName"))
    {
        GetEntPropString(entity, Prop_Data, "m_ModelName", value, sizeof(value));
        kv.SetString("model", value);
    }
    SnapshotVector(kv, entity, "m_vecOrigin", "origin");
    SnapshotVector(kv, entity, "m_vecAbsOrigin", "absolute_origin");
    SnapshotVector(kv, entity, "m_angRotation", "angles");
    SnapshotVector(kv, entity, "m_angAbsRotation", "absolute_angles");
    SnapshotVector(kv, entity, "m_vecAbsVelocity", "velocity");
    SnapshotVector(kv, entity, "m_vecMins", "collision_mins");
    SnapshotVector(kv, entity, "m_vecMaxs", "collision_maxs");
    SnapshotInt(kv, entity, "m_nSkin", "skin");
    SnapshotInt(kv, entity, "m_nBody", "body");
    SnapshotInt(kv, entity, "m_CollisionGroup", "collision_group");
    SnapshotInt(kv, entity, "m_usSolidFlags", "solid_flags");
    SnapshotInt(kv, entity, "m_iHealth", "entity_health");
    SnapshotInt(kv, entity, "m_takedamage", "takedamage");
    SnapshotFloat(kv, entity, "m_flModelScale", "model_scale");
    kv.SetNum("move_type", view_as<int>(GetEntityMoveType(entity)));
    kv.SetNum("render_mode", view_as<int>(GetEntityRenderMode(entity)));
    int red, green, blue, alpha;
    GetEntityRenderColor(entity, red, green, blue, alpha);
    Format(value, sizeof(value), "%d %d %d %d", red, green, blue, alpha);
    kv.SetString("render_rgba", value);
}

int SnapshotBuildingUID(const char[] name)
{
    if (StrContains(name, "hzsh_build_") != 0) return 0;
    int at = 11, uid;
    if (name[at] < '0' || name[at] > '9') return 0;
    while (name[at] >= '0' && name[at] <= '9')
    {
        int digit = name[at++] - '0';
        if (uid > 214748364 || (uid == 214748364 && digit > 7)) return 0;
        uid = uid * 10 + digit;
    }
    if (uid < 1 || name[at++] != '_' || name[at] < '0' || name[at] > '9') return 0;
    while (name[at] >= '0' && name[at] <= '9') at++;
    return name[at] == '\0' ? uid : 0;
}

void SnapshotAddUID(ArrayList uids, int uid)
{
    if (uid > 0 && uids.FindValue(uid) == -1) uids.Push(uid);
}

bool SnapshotCommit(KeyValues kv, const char[] path)
{
    char pending[PLATFORM_MAX_PATH], backup[PLATFORM_MAX_PATH];
    int timestamp = GetTime(), sequence;
    do
    {
        sequence = ++gSnapshotSequence;
        Format(pending, sizeof(pending), "%s.pending.%d.%d", path, timestamp, sequence);
        Format(backup, sizeof(backup), "%s.backup.%d.%d", path, timestamp, sequence);
    }
    while (FileExists(pending) || FileExists(backup));
    kv.Rewind();
    if (!kv.ExportToFile(pending)) return false;
    bool existed = FileExists(path);
    if (existed && !RenameFile(backup, path)) return false;
    if (RenameFile(path, pending)) return true;
    if (existed && !RenameFile(path, backup))
        LogError("[V082UserSnapshot] Could not restore old snapshot; retained backup: %s", backup);
    return false;
}

public Action ExportUserSnapshot(int args)
{
    if (args != 0 || !SnapshotNativesReady())
    {
        PrintToServer("[V082UserSnapshot] REFUSED: no arguments permitted; public Shelter API v2 must be available");
        return Plugin_Handled;
    }
    int client, humans;
    for (int i = 1; i <= MaxClients; i++)
        if (IsClientInGame(i) && !IsFakeClient(i) && !IsClientSourceTV(i) && !IsClientReplay(i))
        {
            client = i;
            humans++;
        }
    char auth[64];
    if (humans != 1 || !GetClientAuthId(client, AuthId_Steam2, auth, sizeof(auth), true))
    {
        PrintToServer("[V082UserSnapshot] REFUSED: exactly one in-game real user with verified Steam2 auth is required (humans=%d)", humans);
        return Plugin_Handled;
    }
    bool enabled;
    HZSHPhase phase;
    HZSHEnvironmentPhase environment;
    int day, revision, materialCount = HZSH_GetMaterialCount(), baseCount = HZSH_GetBaseCount();
    if (!HZSH_GetEnvironmentState(enabled, phase, environment, day, revision)
        || materialCount < 1 || materialCount > 1024 || baseCount < 0 || baseCount > 1024)
    {
        PrintToServer("[V082UserSnapshot] REFUSED: environment or enumeration bounds unavailable");
        return Plugin_Handled;
    }
    int serial = GetClientSerial(client);
    KeyValues kv = new KeyValues("V082UserSnapshot");
    kv.SetNum("schema_version", 1);
    kv.SetNum("unix_time", GetTime());
    kv.SetFloat("engine_time", GetGameTime());
    kv.SetString("scope", "read-only public API + existing entities; no game-state setters");
    kv.SetString("uid_scope", "base UIDs plus existing hzsh_build_UID_generation bodies; no API enumerates removed/bodyless non-base UIDs");
    kv.SetString("construction_state", "not exposed by public API; alive=false alone cannot distinguish blueprint from destroyed/incomplete");
    kv.SetString("recipe_profile", "not exposed; model/kind/level are evidence, not an invented recipe/profile");
    kv.SetString("skills", "saved separately by root via sm_zsh_skills_status");
    kv.SetString("boss_state", "saved separately by root via sm_zsh_status");
    char text[PLATFORM_MAX_PATH];
    GetCurrentMap(text, sizeof(text));
    kv.SetString("map", text);
    kv.SetNum("material_count", materialCount);
    kv.SetNum("base_count", baseCount);
    kv.JumpToKey("Environment", true);
    kv.SetNum("enabled", enabled);
    kv.SetNum("running", HZSH_IsRunning());
    kv.SetNum("phase", view_as<int>(phase));
    kv.SetNum("environment_phase", view_as<int>(environment));
    kv.SetNum("day", day);
    kv.SetNum("api_revision", revision);
    kv.SetFloat("phase_remaining", HZSH_GetPhaseTimeRemaining());
    kv.GoBack();
    kv.JumpToKey("Player", true);
    kv.SetNum("client", client);
    kv.SetNum("serial", serial);
    kv.SetNum("userid", GetClientUserId(client));
    kv.SetString("verified_steam2", auth);
    kv.SetNum("steam_account_id", GetSteamAccountID(client, true));
    GetClientName(client, text, sizeof(text));
    kv.SetString("name", text);
    kv.SetNum("team", GetClientTeam(client));
    kv.SetNum("alive", IsPlayerAlive(client));
    kv.SetNum("hp", GetClientHealth(client));
    kv.SetNum("armor", GetClientArmor(client));
    SnapshotInt(kv, client, "m_iMaxHealth", "max_hp");
    SnapshotInt(kv, client, "m_takedamage", "takedamage");
    if (HasEntProp(client, Prop_Data, "m_takedamage"))
    {
        kv.SetNum("god", GetEntProp(client, Prop_Data, "m_takedamage") == 0);
        kv.SetString("god_meaning", "inferred from takedamage==0; not a separate plugin/god-mode flag");
    }
    kv.SetNum("move_type", view_as<int>(GetEntityMoveType(client)));
    kv.SetNum("flags", GetEntityFlags(client));
    SnapshotFloat(kv, client, "m_flLaggedMovementValue", "speed_multiplier");
    SnapshotFloat(kv, client, "m_flMaxspeed", "max_speed");
    kv.SetFloat("gravity_raw", GetEntityGravity(client));
    float vector[3];
    GetClientAbsOrigin(client, vector);
    kv.SetVector("origin", vector);
    GetClientAbsAngles(client, vector);
    kv.SetVector("absolute_angles", vector);
    GetClientEyeAngles(client, vector);
    kv.SetVector("eye_angles", vector);
    SnapshotVector(kv, client, "m_vecAbsVelocity", "velocity");
    kv.JumpToKey("Materials", true);
    bool allReadsValid = true;
    for (int m = 0; m < materialCount; m++)
    {
        char id[128];
        int amount;
        bool idValid = HZSH_GetMaterialId(m, id, sizeof(id));
        Format(text, sizeof(text), "%d", m);
        kv.JumpToKey(text, true);
        kv.SetNum("index", m);
        kv.SetNum("id_valid", idValid);
        kv.SetString("id", id);
        bool valid = idValid && HZSH_GetPlayerMaterialAmount(client, id, amount);
        kv.SetNum("amount_valid", valid);
        if (valid) kv.SetNum("amount", amount);
        else allReadsValid = false;
        kv.GoBack();
    }
    kv.GoBack();
    kv.GoBack();

    ArrayList uids = new ArrayList();
    kv.JumpToKey("Bases", true);
    for (int core = 0; core < baseCount; core++)
    {
        int uid, level, hp, maxHP;
        bool alive;
        bool valid = HZSH_GetBaseInfo(core, uid, level, hp, maxHP, alive);
        Format(text, sizeof(text), "%d", core);
        kv.JumpToKey(text, true);
        kv.SetNum("core", core);
        kv.SetNum("api_valid", valid);
        if (valid)
        {
            SnapshotAddUID(uids, uid);
            kv.SetNum("uid", uid);
            kv.SetNum("level", level);
            kv.SetNum("hp", hp);
            kv.SetNum("max_hp", maxHP);
            kv.SetNum("api_alive", alive);
        }
        else allReadsValid = false;
        float production, demand, allocated;
        int offline;
        bool powerValid = HZSH_GetBasePower(core, production, demand, allocated, offline);
        kv.SetNum("power_valid", powerValid);
        if (powerValid)
        {
            kv.SetFloat("production", production);
            kv.SetFloat("demand", demand);
            kv.SetFloat("allocated", allocated);
            kv.SetNum("offline", offline);
        }
        else allReadsValid = false;
        kv.JumpToKey("Materials", true);
        for (int m = 0; m < materialCount; m++)
        {
            char id[128];
            int amount;
            bool idValid = HZSH_GetMaterialId(m, id, sizeof(id));
            Format(text, sizeof(text), "%d", m);
            kv.JumpToKey(text, true);
            kv.SetString("id", id);
            bool amountValid = idValid && HZSH_GetBaseMaterialAmount(core, id, amount);
            kv.SetNum("amount_valid", amountValid);
            if (amountValid) kv.SetNum("amount", amount);
            else allReadsValid = false;
            kv.GoBack();
        }
        kv.GoBack();
        kv.GoBack();
    }
    kv.GoBack();

    int bodyCount;
    kv.JumpToKey("BuildingBodies", true);
    for (int entity = MaxClients + 1; entity < GetMaxEntities(); entity++)
    {
        if (!IsValidEntity(entity) || !HasEntProp(entity, Prop_Data, "m_iName")) continue;
        char name[128];
        GetEntPropString(entity, Prop_Data, "m_iName", name, sizeof(name));
        int uid = SnapshotBuildingUID(name);
        if (uid < 1) continue;
        SnapshotAddUID(uids, uid);
        Format(text, sizeof(text), "%d", entity);
        kv.JumpToKey(text, true);
        kv.SetNum("uid", uid);
        SnapshotEntity(kv, entity);
        kv.GoBack();
        bodyCount++;
    }
    kv.GoBack();
    int buildingCount = uids.Length, validBuildings;
    kv.JumpToKey("Buildings", true);
    for (int row = 0; row < buildingCount; row++)
    {
        int uid = uids.Get(row), core, level, hp, maxHP;
        HZSHBuildingKind kind;
        bool alive, powered;
        float load, output;
        bool valid = HZSH_GetBuildingInfo(uid, kind, core, level, hp, maxHP, alive, powered, load, output);
        Format(text, sizeof(text), "%d", uid);
        kv.JumpToKey(text, true);
        kv.SetNum("uid", uid);
        kv.SetNum("api_valid", valid);
        if (valid)
        {
            validBuildings++;
            kv.SetNum("kind", view_as<int>(kind));
            kv.SetNum("api_core", core);
            kv.SetString("api_core_meaning", "base: core; non-base: current powerCore, not placement/buildCore");
            kv.SetNum("level", level);
            kv.SetNum("hp", hp);
            kv.SetNum("max_hp", maxHP);
            kv.SetNum("api_alive", alive);
            kv.SetNum("powered", powered);
            kv.SetFloat("power_required", load);
            kv.SetFloat("power_output", output);
        }
        else allReadsValid = false;
        kv.GoBack();
    }
    kv.GoBack();
    delete uids;
    kv.SetNum("building_uid_count", buildingCount);
    kv.SetNum("building_api_valid_count", validBuildings);
    kv.SetNum("building_body_count", bodyCount);
    kv.SetNum("all_api_reads_valid", allReadsValid);
    kv.SetNum("same_client_serial_at_end", GetClientFromSerial(serial) == client);
    kv.SetFloat("capture_finished_engine_time", GetGameTime());
    char path[PLATFORM_MAX_PATH];
    BuildPath(Path_SM, path, sizeof(path), "data/v082_routes_user_snapshot.cfg");
    bool committed = SnapshotCommit(kv, path);
    delete kv;
    PrintToServer("[V082UserSnapshot] committed=%d path=%s client_serial=%d materials=%d bases=%d building_uids=%d valid_buildings=%d bodies=%d all_api_reads_valid=%d",
        committed, path, serial, materialCount, baseCount, buildingCount, validBuildings, bodyCount, allReadsValid);
    return Plugin_Handled;
}

public void OnPluginStart()
{
    if (CommandExists("sm_v082_usersnapshot"))
    {
        SetFailState("sm_v082_usersnapshot already exists; refusing to override another command");
        return;
    }
    RegServerCmd("sm_v082_usersnapshot", ExportUserSnapshot, "Read-only export of the sole real user and current public Shelter state");
}
