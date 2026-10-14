#if defined _HanZombieShelterAPI_included
    #endinput
#endif
#define _HanZombieShelterAPI_included
#define HZSH_API_VERSION 1

enum HZSHPhase { HZSHPhase_Waiting, HZSHPhase_Day, HZSHPhase_Night, HZSHPhase_Ending }
enum HZSHEnvironmentPhase { HZSHEnvironment_Day, HZSHEnvironment_Night }
enum HZSHBuildingKind { HZSHBuilding_Base, HZSHBuilding_Turret, HZSHBuilding_Flame, HZSHBuilding_Fence,
    HZSHBuilding_Mine, HZSHBuilding_Amplifier, HZSHBuilding_Generator, HZSHBuilding_Light, HZSHBuilding_Grenade }

native int HZSH_GetAPIVersion();
native int HZSH_GetEnvironmentAPIVersion();
// Valid disabled snapshots return true. False means no map state is ready yet.
// Ending preserves the last environment; new rounds explicitly reset it to Day.
native bool HZSH_GetEnvironmentState(bool &modeEnabled, HZSHPhase &gamePhase,
    HZSHEnvironmentPhase &environmentPhase, int &day, int &revision);
native bool HZSH_IsRunning();
native float HZSH_GetPhaseTimeRemaining();
native int HZSH_GetBaseCount();
// Core indices are 0-based and valid only for this map. False on invalid index.
native bool HZSH_GetBaseInfo(int core, int &uid, int &level, int &hp, int &maxHP, bool &alive);
native bool HZSH_GetBasePower(int core, float &production, float &demand, float &allocated, int &offline);
// Stable building UIDs are scoped to a provider/map session, never entity indices.
native bool HZSH_GetBuildingInfo(int uid, HZSHBuildingKind &kind, int &core, int &level,
    int &hp, int &maxHP, bool &alive, bool &powered, float &powerRequired, float &powerOutput);
native bool HZSH_GetPlayerMaterials(int client, int materials[5]);

// Notifications run after committed state. Read getters again in delayed work.
forward void HZSH_OnEnvironmentStateChanged(int revision);
// Includes same-phase countdown resets and day changes; old/new may be equal.
forward void HZSH_OnPhaseChanged(HZSHPhase previous, HZSHPhase current, int day, float duration);
forward void HZSH_OnRoundStarted(int day, bool restored);
forward void HZSH_OnOutcome(bool victory, int previousDay, int nextDay, bool terminal);
forward void HZSH_OnBasePowerChanged(int core, float production, float demand, float allocated, int offline);

#if !defined HZSH_API_PROVIDER
public SharedPlugin __pl_HanZombieShelter =
{
    name = "HanZombieShelter",
    file = "HanZombieShelter.smx",
#if defined REQUIRE_PLUGIN
    required = 1,
#else
    required = 0,
#endif
};
#if !defined REQUIRE_PLUGIN
public void __pl_HanZombieShelter_SetNTVOptional()
{
    MarkNativeAsOptional("HZSH_GetAPIVersion");MarkNativeAsOptional("HZSH_GetEnvironmentAPIVersion");
    MarkNativeAsOptional("HZSH_GetEnvironmentState");MarkNativeAsOptional("HZSH_IsRunning");
    MarkNativeAsOptional("HZSH_GetPhaseTimeRemaining");MarkNativeAsOptional("HZSH_GetBaseCount");
    MarkNativeAsOptional("HZSH_GetBaseInfo");MarkNativeAsOptional("HZSH_GetBasePower");
    MarkNativeAsOptional("HZSH_GetBuildingInfo");MarkNativeAsOptional("HZSH_GetPlayerMaterials");
}
#endif
#endif
