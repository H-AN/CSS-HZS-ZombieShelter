#define SHELTER_VERSION "0.7.0"
#define MAX_BUILDINGS 1024 // Storage ceiling; configured quotas and edict budget still apply.
#define MAX_NODES 80
#define MAX_NPCS 512 // Includes the configured 250 ordinary pool, Bosses and registration headroom.
#define MAX_CORES 8
#define MAX_BOSSES 8
#define MAX_BOXES 8
#define MAX_POINTS 32
#define MAX_RECIPES 256 // Configuration catalogue, independent of live building capacity.
#define MATERIALS 64 // Bounded storage; enumerate only gMaterialCount registered IDs.
#define LEGACY_MATERIALS 5

enum Phase { Phase_Waiting, Phase_Day, Phase_Night, Phase_Ending }
enum BuildingKind { Kind_Base, Kind_Turret, Kind_Flame, Kind_Fence, Kind_Mine, Kind_Amplifier, Kind_Generator, Kind_Light, Kind_Grenade, Kind_Expander,
    Kind_AutoRepair, Kind_Heal, Kind_Armor, Kind_Beacon, Kind_Campfire }
enum ConfigFile { C_Mode, C_Lifecycle, C_Resources, C_Zombies, C_Days, C_Bosses, C_Bases, C_Turrets, C_Flames, C_Fences, C_Mines, C_Drops, C_Map, C_Sounds, C_Amplifiers, C_Progression, C_Entities, C_Management, C_Power, C_Generators, C_Lights, C_Grenades, C_Expanders, C_Materials, C_ZombieDrops,
    C_Repair, C_Heal, C_Armor, C_Beacons, C_Campfires, C_Shelters, C_Count }

// Reward identity is independent of the living client required by the HZS
// damage API. Delayed hits retain this snapshot after a facility is removed.
enum struct RewardContext
{
    bool valid;
    bool facility;
    int epoch;
    int sourceUID;
    int sourceRef;
    int buildCore;
    int ownerSerial;
    char ownerKey[64];
}

// A real NPC strike owns one immutable reply, even if it destroys the fence.
enum struct FenceRetaliationEvent
{
    bool pending;
    int epoch;
    int npc;
    int npcRef;
    int damage;
    RewardContext reward;
}

enum struct MaterialDef
{
    char id[32];
    char label[64];
    char description[256];
    bool currency;
    bool gatherable;
    char model[PLATFORM_MAX_PATH];
    int quota;
    int yield;
    int rgb[3];
    float scale;
    float hold;
    float grow;
    bool ease;
}

enum struct CoreDef
{
    bool dynamic;
    char id[64];
    char profile[64];
    char model[PLATFORM_MAX_PATH];
    float origin[3];
    float angles[3];
    int boxCount;
    int pointCount;
    int building;
    float buildCenter[3];
    float buildAngles[3];
    float buildZMin;
    float buildZMax;
    int buildHardLimit;
}

enum struct BossDef
{
    char id[64];
    char publicName[64];
    char key[64];
    char marker[64];
    char idle[64];
    char attack[64];
    float origin[3];
    float angles[3];
    int maxHP;
    int savedHP;
    int positionedHP;
    bool awakened;
    bool defeated;
    bool required;
    bool boundThisRound;
    float damage;
    float interval;
}

enum struct BuildingMetadata
{
    char label[128];
    char description[256];
    int render[4];
    int skin;
    char placementMode[16];
    float originOffset[3];
    float angleOffset[3];
    bool allowOverlap;
    float maxDistance;
    float muzzleOffset[3];
    float lightOffset[3];
    char buildSound[PLATFORM_MAX_PATH];
    char attackSound[PLATFORM_MAX_PATH];
    char upgradeSound[PLATFORM_MAX_PATH];
    char destroySound[PLATFORM_MAX_PATH];
    char pulseSound[PLATFORM_MAX_PATH];
}

enum struct Building
{
    bool used;
    bool alive;
    bool constructing;
    int constructionTarget;
    BuildingKind kind;
    int uid;
    int generation;
    int core;
    int buildCore;
    int configRevision;
    float placementAnchor[3];
    ConfigFile file;
    char type[64];
    char model[PLATFORM_MAX_PATH];
    BuildingMetadata meta;
    int level;
    int hp;
    int maxHP;
    int ref;
    float origin[3];
    float angles[3];
    float nextFire;
    float nextSearch;
    int targetRef;
    int targetNPC;
    int targetEpoch;
    int targetBuildingUID;
    int targetBuildingRef;
    float range;
    float damage;
    float interval;
    int restoreAmount;
    int maxPerBase;
    float restoreInterval;
    float nextRestore;
    float halfAngle;
    float burnDuration;
    int burnDamage;
    float burnInterval;
    bool upgraded;
    int ownerSerial;
    char ownerKey[64];
    int invested[MATERIALS];
    float lastAttackAt;
    float restoreMins[3];
    float restoreMaxs[3];
    bool boundsKnown;
    int powerCore;
    bool gridAssigned;
    bool powered;
    float powerOutput;
    float powerDemand;
    int powerPriority;
    int effectRef;
    float lightRadius;
    int lightBrightness;
    float projectileSpeed;
    float blastRadius;
    float buildRangeBonus;
    int buildLimitBonus;
    int baseBuildLimit;
    float buildHalfExtents[3];
    float restoreRetryAt;
}

enum struct ResourceNode
{
    bool used;
    char id[64];
    char pool[64];
    int material;
    int remaining;
    int ref;
    float origin[3];
    float angles[3];
    bool growing;
    bool rerollPending;
    float elapsed;
}

enum struct NPC
{
    bool used;
    bool settled;
    int registrationEpoch;
    char publicName[128];
    bool ready;
    bool active;
    bool dead;
    bool paused;
    int ref;
    int visualRef;
    int boss;
    int bodyColor[4];
    int visualColor[4];
    int takeDamage;
    int solidFlags;
    MoveType moveType;
    float originalOrigin[3];
    float originalAngles[3];
    float resumeOrigin[3];
    float resumeAngles[3];
    bool savedPosition;
    int lastHP;
    int baseHP;
    char idle[64];
    char attack[64];
    int touchedUID;
    float touchedUntil;
    int attackingUID;
    float nextAttack;
    int targetClientSerial;
    float burnUntil;
    float nextBurn;
    int burnDamage;
    float burnInterval;
    int burnClientSerial;
}

enum struct Recipe
{
    char id[64];
    char type[64];
    char label[128];
    BuildingMetadata meta;
    ConfigFile file;
    BuildingKind kind;
    int cost[MATERIALS];
}

KeyValues gCfg[C_Count];
KeyValues gHanData;
MaterialDef gMaterials[MATERIALS];
int gMaterialCount;
CoreDef gCores[MAX_CORES];
float gCoreMins[MAX_CORES][MAX_BOXES][3], gCoreMaxs[MAX_CORES][MAX_BOXES][3];
float gCorePoints[MAX_CORES][MAX_POINTS][3], gCorePointAngles[MAX_CORES][MAX_POINTS][3];
BossDef gBosses[MAX_BOSSES];
Building gBuildings[MAX_BUILDINGS];
ResourceNode gNodes[MAX_NODES];
NPC gNPCs[MAX_NPCS];
Recipe gRecipes[MAX_RECIPES];
int gCoreCount, gBossCount, gNodeCount, gRecipeCount;
int gPlayerInventory[MAXPLAYERS+1][MATERIALS];
int gBaseInventory[MAX_CORES][MATERIALS];
int gEpoch, gUID, gConfigRevision;
int gDay=1, gNativeKills;
Phase gPhase=Phase_Waiting;
bool gEnabled, gCoreReady, gSnapshot, gRollback, gTerminal, gFailureCandidate, gVictoryCandidate;
bool gRoundPrepared, gRoundRestored, gWarningSent, gRebuilding;
bool gOutcomeQueued, gTerminationExpected;
float gDeadline, gNextHUD, gNextExposure, gNextAdmissions, gPreviousTick;
float gDaySeconds, gNightSeconds, gWarningSeconds, gExposureInterval, gRecoveryFraction, gOutcomeSeconds;
// gRepairCooldown/gNextRepair remain for older probe source compatibility.
// Manual building hits use weapon rules without an independent rate limit.
float gGatherCooldown, gRepairCooldown, gUseDistance, gUseCooldown;
#pragma unused gRepairCooldown
int gExposureDamage, gSummonLimit, gPopulationLimit, gNativePoolSize, gInventoryCap;
int gRollbackDays, gMinimumDay;
bool gDestroySummons, gDirectLoss, gExposureEnabled;
char gSpawnPolicy[32], gMap[64], gFailureText[256], gVictoryText[256];
float gParking[3];
float gNextGather[MAXPLAYERS+1], gNextRepair[MAXPLAYERS+1], gNextUse[MAXPLAYERS+1];
int gLastButtons[MAXPLAYERS+1];
int gDamageNPCRef=INVALID_ENT_REFERENCE, gDamageValue;
int gNPCBinding[2049], gNPCBindingRef[2049];
int gWorldBinding[2049], gWorldBindingRef[2049];
float gNextBossAudit;
int gLeaderOffset=-1, gBeamSprite, gFireSprite, gExplosionSprite;
Handle gTick,gProjectileTimer,gDefenseTimer;
ConVar gCorePoolCvar;
int gOriginalPool;
bool gPoolOverridden;
ConVar gPolicyCVars[7];
char gPolicyOriginal[7][64];
bool gPolicyOverridden;

bool LiveHuman(int client)
{
    return client>0 && client<=MaxClients && IsClientInGame(client) && IsPlayerAlive(client)
        && (GetClientTeam(client)==CS_TEAM_T || GetClientTeam(client)==CS_TEAM_CT);
}

bool Running()
{
    return gEnabled && gCoreReady && (gPhase==Phase_Day || gPhase==Phase_Night);
}

int Resolve(int ref)
{
    if(ref==INVALID_ENT_REFERENCE)return -1;
    int entity=EntRefToEntIndex(ref);
    return entity>MaxClients && IsValidEntity(entity) ? entity : -1;
}

int FindBuilding(int entity)
{
    if(entity<=MaxClients || !IsValidEntity(entity))return -1;
    int ref=EntIndexToEntRef(entity);
    for(int i=0;i<MAX_BUILDINGS;i++)
        if(gBuildings[i].used && gBuildings[i].ref==ref)return i;
    return -1;
}

int FindNPC(int entity)
{
    if(entity<=MaxClients || !IsValidEntity(entity))return -1;
    int ref=EntIndexToEntRef(entity);
    for(int i=0;i<MAX_NPCS;i++)if(gNPCs[i].used && gNPCs[i].ref==ref)return i;
    return -1;
}

int FindNode(int entity)
{
    if(entity<=MaxClients || !IsValidEntity(entity))return -1;
    int ref=EntIndexToEntRef(entity);
    for(int i=0;i<gNodeCount;i++)if(gNodes[i].used && gNodes[i].ref==ref)return i;
    return -1;
}

int FindMaterial(const char[] id)
{
    for(int i=0;i<gMaterialCount;i++)if(StrEqual(id,gMaterials[i].id))return i;
    return -1;
}

void Tell(int client, const char[] format, any ...)
{
    SetGlobalTransTarget(client>0 && client<=MaxClients && IsClientInGame(client) ? client : LANG_SERVER);
    char text[512];VFormat(text,sizeof(text),format,3);
    if(client>0 && IsClientInGame(client))PrintToChat(client,"\x04[HZS Shelter]\x01 %s",text);
    else PrintToServer("[Shelter] %s",text);
}

void AnnouncePhrase(const char[] key,any ...)
{
    if(!key[0])return;
    char text[512];SetGlobalTransTarget(LANG_SERVER);VFormat(text,sizeof(text),"%t",1);PrintToServer("[Shelter] %s",text);
    for(int client=1;client<=MaxClients;client++)if(IsClientInGame(client) && !IsFakeClient(client))
    {
        SetGlobalTransTarget(client);VFormat(text,sizeof(text),"%t",1);PrintToChat(client,"\x04[HZS Shelter]\x01 %s",text);
    }
}

void TranslateLabel(int client,const char[] key,char[] buffer,int size)
{
    if(TranslationPhraseExists(key))Format(buffer,size,"%T",key,client);
    else strcopy(buffer,size,key); // Administrators may supply a literal custom label in config.
}
