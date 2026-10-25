"""Execute production Boss outcome and new-round logic in an offline Pawn VM.

World creation, entity lookup, reward effects and frame scheduling are test
adapters. Boss selection, death settlement, victory gates and round reset are
the actual production functions, extracted without rewriting their bodies.
"""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ROOT / "addons/sourcemod/scripting/HanZombieShelter"
REPORT = ROOT / "tests/reports/v082_boss_routes_20261025/outcomes"
BUILD = REPORT / "vm"
spec = importlib.util.spec_from_file_location("boss_outcome_vm_support", ROOT / "tests/test_v082_boss_spawns_vm.py")
support = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = support
spec.loader.exec_module(support)
support.compat.BUILD = BUILD


class OutcomeNatives(support.BossVMNatives):
    frames: list[tuple[int, int]] = []
    errors: list[str] = []
    now = 100.0

    @support.native("cell", "cell")
    def RequestFrame(self, callback: int, data: int) -> None:
        self.frames.append((callback, data))

    @support.native("string", "...")
    def LogError(self, fmt: str, *args) -> None:
        self.errors.append(support.atcprintf(self.amx, fmt, args))

    @support.native
    def GetGameTime(self) -> float:
        return self.now

    @support.native("float", "float")
    def __FLOAT_GE__(self, left: float, right: float) -> bool:
        return left >= right

    @support.native("float", "float")
    def __FLOAT_LE__(self, left: float, right: float) -> bool:
        return left <= right

    @support.native("float", "float")
    def __FLOAT_SUB__(self, left: float, right: float) -> float:
        return left - right


PRELUDE = r'''
#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#define MAX_NPCS 512
#define MAX_BOSSES MAX_NPCS
#define BOSS_SPAWN_POINT_COUNT 5
#define HZSHEnvironment_Day 0
enum ConfigFile { C_Map, C_Bosses, C_Count }
KeyValues gCfg[C_Count];
int gBossCount,gEpoch,gDay,gEnvironmentPhase,gNativeKills,gSurvivalDayLimit;
bool gEnabled,gTerminal,gRoundPrepared,gWarningSent,gFailureCandidate,gVictoryCandidate;
bool gOutcomeQueued,gTerminationExpected,gRoundRestored,gSnapshot,gRollback,gCoreReady;
float gPreviousTick,gDeadline,gNextBossAudit,gWarningSeconds;
int vmRewards,vmWorldClears,vmWorldCreates,vmDayAdvances,vmMapChanges;
bool vmWorldRestored;
enum struct VMNPC
{
    bool used;
    int boss;
    int ref;
    int visualRef;
    int registrationEpoch;
    bool dead;
    bool settled;
    bool active;
    float burnUntil;
    char publicName[128];
}
VMNPC gNPCs[MAX_NPCS];
int Resolve(int ref) { return ref; }
bool ConfigFailure(const char[] reason) { LogError("%s",reason);return false; }
int FindNPC(int entity) { return entity>=0 && entity<MAX_NPCS && gNPCs[entity].used ? entity : -1; }
void CancelAllBaseTeleportSelections() {}
void CleanWorld() { vmWorldClears++; }
void ApplyMapAdjustments() {}
bool CreateWorld(bool snapshot) { vmWorldCreates++;vmWorldRestored=snapshot;return true; }
void RestoreCorePool() {}
void RestoreServerPolicy() {}
void PublishShelterState(bool force=false) { if(force)return; }
void NotifyPhaseChanged(Phase previous,float duration) { }
void OverrideCorePool() {}
void ClearNPCBurnVisual(int npc) { }
void ExpansionClearNPCFreeze(int npc) { }
void SettleZombieDrops(int npc,int killer) { vmRewards++; }
bool Han_GetZombieName(int npc,char[] name,int length) { strcopy(name,length,"Fixture Boss");return true; }
void CacheNPCDrops(int npc,const char[] name) { }
public void ResolveOutcome(any epoch) { }
bool APIAvailable() { return true; }
void BurnVisualTick(float now) { }
void SkillsTick(float now) { }
void StopPowerEffects() { }
void ClearGrenades() { }
void CampaignHUD() { }
void ChangeCampaignMap() { vmMapChanges++; }
void SuspendCampfires() { }
void StartNight() { gPhase=Phase_Night; }
void StartDay(bool advance) { if(advance){vmDayAdvances++;gDay++;}gPhase=Phase_Day; }
void AnnouncePhrase(const char[] name) { }
void EntityBudgetTick(float now) { }
void FlushSharedOverflow() { }
void RestoreDeferredBuildings() { }
void SyncDynamicBasesGeometry() { }
void ResourceTick(float elapsed) { }
void CampfireTick(float elapsed) { }
void NPCTick(float now) { }
void PowerTick() { }
void RestorationTick(float now) { }
void BeaconTick(float now) { }
void MineWorldTick() { }
void ExpansionWorldTick(float now) { }
void ExposureTick(float now) { }
'''


WRAPPERS = r'''
public int VM_Reset()
{
    gCfg[C_Map]=view_as<KeyValues>(100);gCfg[C_Bosses]=view_as<KeyValues>(101);
    gBossCount=0;gEpoch=7;gDay=6;gEnvironmentPhase=0;gNativeKills=0;
    gEnabled=true;gTerminal=false;gRoundPrepared=true;gWarningSent=false;
    gFailureCandidate=false;gVictoryCandidate=false;gOutcomeQueued=false;gTerminationExpected=false;
    gRoundRestored=false;gSnapshot=true;gRollback=false;gCoreReady=true;
    gPhase=Phase_Day;gVictoryReason=Victory_None;
    gPreviousTick=99.0;gDeadline=200.0;gNextBossAudit=0.0;gWarningSeconds=30.0;gSurvivalDayLimit=30;
    vmRewards=0;vmWorldClears=0;vmWorldCreates=0;vmDayAdvances=0;vmMapChanges=0;vmWorldRestored=false;
    VMNPC blank;for(int i=0;i<MAX_NPCS;i++)gNPCs[i]=blank;
    return 1;
}
public int VM_Read() { return ReadBossSpawnPoints() && ReadBossDefinitions() ? 1 : 0; }
public int VM_ArmNPCs()
{
    for(int i=0;i<gBossCount;i++)
    {
        gNPCs[i].used=true;gNPCs[i].boss=i;gNPCs[i].ref=i;gNPCs[i].registrationEpoch=gEpoch;
        gNPCs[i].active=true;gNPCs[i].dead=false;gNPCs[i].settled=false;
        strcopy(gNPCs[i].publicName,sizeof(gNPCs[].publicName),gBosses[i].publicName);
    }
    return 1;
}
public int VM_Count() { return gBossCount; }
public int VM_SetCount(int count) { gBossCount=count;return 1; }
public int VM_Required(int index) { return gBosses[index].required; }
public int VM_SetRequired(int index,int required) { gBosses[index].required=required!=0;return 1; }
public int VM_SetBossState(int index,int hp,int defeated,int awakened,int bound,int positioned)
{
    gBosses[index].savedHP=hp;gBosses[index].defeated=defeated!=0;gBosses[index].awakened=awakened!=0;
    gBosses[index].boundThisRound=bound!=0;gBosses[index].positionedHP=positioned;return 1;
}
public int VM_BossState(int index,int field)
{
    switch(field)
    {
        case 0:return gBosses[index].savedHP;
        case 1:return gBosses[index].defeated;
        case 2:return gBosses[index].awakened;
        case 3:return gBosses[index].boundThisRound;
        case 4:return gBosses[index].positionedHP;
        case 5:return gBosses[index].maxHP;
        case 6:return gBosses[index].spawnPoint;
        case 7:return view_as<int>(gBosses[index].origin[0]);
    }
    return -1;
}
public int VM_AllDefeated() { return AllBossesDefeated(); }
public int VM_Die(int index) { Han_OnZombieDeath(index,1);return 1; }
public int VM_QueueVictory() { return gVictoryCandidate; }
public int VM_Reason() { return view_as<int>(gVictoryReason); }
public int VM_Queued() { return gOutcomeQueued; }
public int VM_Rewards() { return vmRewards; }
public int VM_HumanWin() { Han_OnHumanWin();return 1; }
public int VM_NewRound() { RoundStart(null,"round_start",false);return 1; }
public int VM_PrepareRound() { PrepareRound();return 1; }
public int VM_ResetRoundState() { ResetBossRoundState();return 1; }
public int VM_PrepareState(int field)
{
    switch(field)
    {
        case 0:return gEpoch;
        case 1:return gRoundPrepared;
        case 2:return gRoundRestored;
        case 3:return vmWorldClears;
        case 4:return vmWorldCreates;
        case 5:return vmWorldRestored;
        case 6:return gDay;
        case 7:return gBossCount;
        case 8:return gNPCs[0].used;
    }
    return -1;
}
public int VM_SetGate(int enabled,int terminal) { gEnabled=enabled!=0;gTerminal=terminal!=0;return 1; }
public int VM_SetNPCEpoch(int index,int epoch) { gNPCs[index].registrationEpoch=epoch;return 1; }
public int VM_SetNPCActive(int index,int active) { gNPCs[index].active=active!=0;return 1; }
public int VM_SetNPCBoss(int index,int boss) { gNPCs[index].boss=boss;return 1; }
public int VM_NativeKills() { return gNativeKills; }
public int VM_SetPhase(int phase) { gPhase=view_as<Phase>(phase);return 1; }
public int VM_ConfigureNightExpiry(int day,int limit)
{
    gPhase=Phase_Night;gDay=day;gSurvivalDayLimit=limit;gDeadline=99.0;return 1;
}
public int VM_Tick() { return view_as<int>(WorldTick(null)); }
public int VM_DayAdvances() { return vmDayAdvances; }
public int VM_UpgradeVictoryReason() { QueueOutcome(true,Victory_Bosses);return 1; }
public int VM_Claim(const char[] name) { return ClaimBossDefinition(name); }
public int VM_UnusedOutcomeState()
{ return gEnvironmentPhase+view_as<int>(gFailureCandidate)+view_as<int>(gTerminationExpected)+view_as<int>(gRollback)+vmMapChanges; }
'''


def production_source() -> tuple[str, dict[str, str]]:
    copied = {}
    types = []
    for marker in ("enum Phase", "enum VictoryReason", "enum struct BossDef"):
        block = support.compat.extract_block(PRODUCTION / "Types.inc", marker)
        types.append(block)
        copied["Types.inc:" + marker] = hashlib.sha256(block.encode("utf-8")).hexdigest()
    prefix, harness = PRELUDE.split("KeyValues gCfg", 1)
    # Public test adapters may intentionally ignore engine-only parameters.
    harness = re.sub(r"(?m)^(int|bool|void) (\w+\()", r"public \1 \2", harness)
    harness = harness.replace("public void PublishShelterState", "void PublishShelterState")
    parts = [prefix, *types, "Phase gPhase;VictoryReason gVictoryReason;BossDef gBosses[MAX_BOSSES];", "KeyValues gCfg" + harness]
    for filename, marker in (
        ("Config.inc", "bool Node("), ("Config.inc", "int Number("),
        ("Config.inc", "float Decimal("), ("Config.inc", "void Text("),
        ("Types.inc", "bool Running("),
        ("Lifecycle.inc", "void ResetBossRoundState("),
        ("Lifecycle.inc", "void PrepareRound("),
        ("Lifecycle.inc", "public void RoundStart("),
        ("Lifecycle.inc", "public void Han_OnHumanWin("),
        ("Lifecycle.inc", "void QueueOutcome("),
        ("Lifecycle.inc", "public Action WorldTick("),
        ("NPC.inc", "public void Han_OnZombieDeath("),
        ("NPC.inc", "bool AllBossesDefeated("),
    ):
        block = support.compat.extract_block(PRODUCTION / filename, marker)
        copied[filename + ":" + marker] = hashlib.sha256(block.encode("utf-8")).hexdigest()
        parts.append(block)
    parts.append("float gBossSpawnOrigins[BOSS_SPAWN_POINT_COUNT][3];float gBossSpawnAngles[BOSS_SPAWN_POINT_COUNT][3];")
    for marker in ("int BossSpawnPointForOrdinal(", "bool BossReadPointVector(", "bool BossPointFieldsValid(",
                   "bool ReadBossSpawnPoints(", "bool BossConfigIdentifierValid(", "bool ReadBossDefinitions(",
                   "int ClaimBossDefinition("):
        block = support.compat.extract_block(PRODUCTION / "BossSpawns.inc", marker)
        copied["BossSpawns.inc:" + marker] = hashlib.sha256(block.encode("utf-8")).hexdigest()
        parts.append(block)
    parts.append(WRAPPERS)
    return "\n".join(parts), copied


def run() -> dict:
    REPORT.mkdir(parents=True, exist_ok=True)
    source, copied = production_source()
    smx_path = support.compat.compile_source("boss_outcomes_vm", source)
    plugin = support.SourcePawnPlugin(io.BytesIO(smx_path.read_bytes()), smsys_options={"natives_cls": OutcomeNatives})
    vm = plugin.runtime
    vm.console_redirect = None
    checks = []

    def call(name, *args):
        return vm.call_function_by_name("VM_" + name, *args)

    def check(name, actual, expected):
        checks.append({"check": name, "passed": actual == expected, "actual": actual, "expected": expected})

    def install(count, required="boss_001"):
        support.BossVMNatives.trees = {
            100: support.KVCursor(support.map_config(count, required=required)),
            101: support.KVCursor(support.profiles()),
        }
        OutcomeNatives.frames = []
        OutcomeNatives.errors = []
        call("Reset")
        vm.amx._local_to_string = vm.amx._getheapstring
        assert call("Read") == 1
        call("ArmNPCs")

    for count in (1, 3, 5):
        install(count)
        check(f"{count} enabled Bosses all required despite legacy single-ID list", [call("Required", i) for i in range(count)], [1] * count)
    install(3, required="")
    check("empty legacy required list cannot disable Boss victory members", [call("Required", i) for i in range(3)], [1, 1, 1])
    install(3, required="obsolete_missing_id")
    check("obsolete legacy required list is ignored", [call("Required", i) for i in range(3)], [1, 1, 1])

    orders = {1: [(0,)], 3: [(0, 1, 2), (2, 1, 0), (1, 2, 0), (2, 0, 1)],
              5: [(0, 1, 2, 3, 4), (4, 3, 2, 1, 0), (2, 4, 1, 3, 0), (0, 4, 3, 1, 2)]}
    for count, sequences in orders.items():
        for order in sequences:
            install(count)
            for i in range(count):
                call("SetRequired", i, int(i == 0))
            check(f"{count}/{order}: no initial victory", call("AllDefeated"), 0)
            for step, index in enumerate(order):
                call("Die", index)
                last = int(step == count - 1)
                prefix = f"{count}/{order}: death {step + 1}"
                check(prefix + " settles exact Boss", call("BossState", index, 1), 1)
                check(prefix + " records zero saved HP", call("BossState", index, 0), 0)
                check(prefix + " all-Boss gate", call("AllDefeated"), last)
                check(prefix + " queues victory only at final death", call("QueueVictory"), last)
                check(prefix + " one deferred outcome only at final death", len(OutcomeNatives.frames), last)
            check(f"{count}/{order}: exactly one reward settlement per Boss", call("Rewards"), count)
            call("Die", order[-1])
            check(f"{count}/{order}: repeated death cannot double settle", call("Rewards"), count)
            check(f"{count}/{order}: repeated death cannot queue another outcome", len(OutcomeNatives.frames), 1)
            check(f"{count}/{order}: Boss completion reason", call("Reason"), 2)

    install(3)
    call("Die", 0)
    call("HumanWin")
    check("premature native human win cannot bypass remaining Bosses", call("QueueVictory"), 0)
    check("premature native human win emits one diagnostic", len(OutcomeNatives.errors), 1)
    call("Die", 1)
    call("Die", 2)
    call("HumanWin")
    check("native human win after full completion preserves single queued outcome", len(OutcomeNatives.frames), 1)

    install(1)
    call("SetNPCEpoch", 0, 6)
    call("Die", 0)
    check("old-epoch death cannot settle current Boss", [call("BossState", 0, 1), call("QueueVictory"), call("Rewards")], [0, 0, 0])
    install(1)
    call("SetNPCActive", 0, 0)
    call("Die", 0)
    check("parked Boss death cannot grant victory", [call("BossState", 0, 1), call("QueueVictory"), call("Rewards")], [0, 0, 0])
    install(1)
    call("SetPhase", 0)
    call("Die", 0)
    check("waiting-phase death cannot grant victory", [call("BossState", 0, 1), call("QueueVictory")], [0, 0])
    install(1)
    call("SetNPCBoss", 0, -1)
    call("Die", 0)
    check("ordinary death increments native kills without completing Boss", [call("NativeKills"), call("BossState", 0, 1), call("QueueVictory")], [1, 0, 0])
    call("SetCount", 0)
    check("empty Boss catalogue never grants vacuous victory", call("AllDefeated"), 0)

    install(5)
    before = []
    for i in range(5):
        defeated = int(i == 2)
        call("SetBossState", i, 0 if defeated else 49000 - i * 100, defeated, 1, 1, 50000)
        before.append([call("BossState", i, field) for field in (0, 1, 5, 6, 7)])
    call("NewRound")
    check("real RoundStart -> PrepareRound increments epoch once and preserves campaign day", [call("PrepareState", i) for i in (0, 6)], [8, 6])
    check("real PrepareRound recreates snapshot once and clears old NPC registry", [call("PrepareState", i) for i in (1, 2, 3, 4, 5, 7, 8)], [1, 1, 1, 1, 1, 5, 0])
    for i in range(5):
        check(f"new round Boss {i}: awakened/bound/positioning reset", [call("BossState", i, field) for field in (2, 3, 4)], [0, 0, 0])
        check(f"new round Boss {i}: HP/defeat/maxHP/point/origin retained", [call("BossState", i, field) for field in (0, 1, 5, 6, 7)], before[i])
    call("ResetRoundState")
    check("round reset helper is idempotent and keeps defeated progress", [call("BossState", 2, 0), call("BossState", 2, 1)], [0, 1])
    for enabled, terminal in ((0, 0), (1, 1)):
        install(1)
        call("SetBossState", 0, 42000, 0, 1, 1, 50000)
        call("SetGate", enabled, terminal)
        call("PrepareRound")
        check(f"PrepareRound guard enabled={enabled}/terminal={terminal} does not reset", [call("BossState", 0, 2), call("BossState", 0, 3), call("PrepareState", 0)], [1, 1, 7])

    install(3)
    call("ConfigureNightExpiry", 30, 30)
    call("Tick")
    check("actual WorldTick retains configured day30 rescue with living Bosses", [call("AllDefeated"), call("QueueVictory"), call("Reason"), len(OutcomeNatives.frames)], [0, 1, 1, 1])
    call("UpgradeVictoryReason")
    check("Boss completion priority remains above rescue without second queued frame", [call("Reason"), len(OutcomeNatives.frames)], [2, 1])
    for day, limit in ((29, 30), (30, 0)):
        install(3)
        call("SetBossState", 0, 42000, 0, 1, 1, 50000)
        call("ConfigureNightExpiry", day, limit)
        call("Tick")
        check(f"night expiry day={day}/limit={limit} advances normally without victory", [call("QueueVictory"), call("DayAdvances")], [0, 1])
        check(f"ordinary day transition day={day}/limit={limit} does not reset awake Boss", call("BossState", 0, 2), 1)

    failed = sum(not item["passed"] for item in checks)
    log = (BUILD / "boss_outcomes_vm_compile.txt").read_text("utf-8")
    return {"generated_at": datetime.now(timezone(timedelta(hours=8))).isoformat(),
            "passed": failed == 0, "checks_passed": len(checks) - failed, "checks_failed": failed,
            "production_source_sha256": copied, "fixture_smx_sha256": hashlib.sha256(smx_path.read_bytes()).hexdigest(),
            "source_mode": "exact production round/death/outcome/WorldTick functions and BossSpawns parsing/assignment functions",
            "test_harness_compile_warning_count": len(re.findall(r"warning \d+", log)),
            "game_commands_sent": False, "game_processes_started": False,
            "scope": "Offline VM; engine entity lookup/world creation/rewards/frame scheduling are adapters. No actual NPC respawn, map change or player reset.",
            "rescue_policy": "Existing configured survival-day rescue remains an independent victory path; Boss-driven victory requires every enabled definition defeated.",
            "checks": checks}


if __name__ == "__main__":
    result = run()
    output = REPORT / "boss_outcomes_vm_verification.json"
    if output.exists():
        raw = output.read_bytes()
        history = REPORT / "history" / ("boss_outcomes_vm_" + hashlib.sha256(raw).hexdigest() + ".json")
        history.parent.mkdir(parents=True, exist_ok=True)
        if not history.exists():
            history.write_bytes(raw)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in ("passed", "checks_passed", "checks_failed", "test_harness_compile_warning_count")}, ensure_ascii=False))
    if not result["passed"]:
        print(json.dumps([row for row in result["checks"] if not row["passed"]], ensure_ascii=False, indent=2))
        raise SystemExit(1)
