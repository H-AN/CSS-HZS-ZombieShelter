"""Run the exact Boss placement queue/retry callbacks in an offline Pawn VM.

Timer scheduling, entity properties and hull queries are adapters. Production
guards, retries, timeout and successful placement sequencing execute unchanged.
No game process, game command, entity creation or NPC respawn is performed.
"""
from __future__ import annotations

import ctypes
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ROOT / "addons/sourcemod/scripting/HanZombieShelter"
REPORT = ROOT / "tests/reports/v082_boss_routes_20261025/startup_retry"
BUILD = REPORT / "vm"
spec = importlib.util.spec_from_file_location("boss_retry_vm_support", ROOT / "tests/test_v082_boss_outcomes_vm.py")
support = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = support
spec.loader.exec_module(support)
base = support.support
base.compat.BUILD = BUILD


class PackMethods(base.MethodMap):
    @base.native("cell", "bool")
    def Reset(self, handle: int, clear: bool) -> None:
        pack = RetryNatives.packs[handle]
        pack["cursor"] = 0
        if clear:
            pack["cells"].clear()

    @base.native("cell", "cell", "bool")
    def WriteCell(self, handle: int, value: int, insert: bool) -> None:
        pack = RetryNatives.packs[handle]
        cursor = pack["cursor"]
        if insert:
            pack["cells"].insert(cursor, value)
        elif cursor < len(pack["cells"]):
            pack["cells"][cursor] = value
        else:
            pack["cells"].append(value)
        pack["cursor"] += 1

    @base.native("cell")
    def ReadCell(self, handle: int) -> int:
        pack = RetryNatives.packs[handle]
        result = pack["cells"][pack["cursor"]]
        pack["cursor"] += 1
        return result


class RetryNatives(support.OutcomeNatives):
    DataPack = PackMethods()
    packs: dict[int, dict] = {}
    timers: list[dict] = []
    teleports: list[dict] = []
    messages: list[str] = []
    health: dict[int, int] = {}

    @base.native("float", "cell", "cell", "cell")
    def VMCreateDataTimer(self, interval: float, callback: int, destination: int, flags: int) -> int:
        handle = 1000 + len(self.timers)
        self.packs[handle] = {"cells": [], "cursor": 0}
        self.timers.append({"pack": handle, "interval": interval, "callback": callback, "flags": flags})
        self.amx._writeheap(destination, ctypes.c_int32(handle))
        return handle + 1000

    @base.native("string", "...")
    def PrintToServer(self, fmt: str, *args) -> None:
        self.messages.append(base.atcprintf(self.amx, fmt, args))

    @base.native("cell", "cell", "string", "cell", "cell", "cell")
    def SetEntProp(self, entity: int, prop_type: int, prop: str, value: int, size: int, element: int) -> None:
        assert prop == "m_iHealth"
        self.health[entity] = value

    @base.native("cell", "cell", "string", "cell", "cell")
    def GetEntProp(self, entity: int, prop_type: int, prop: str, size: int, element: int) -> int:
        assert prop == "m_iHealth"
        return self.health[entity]

    @base.native("cell", "cell", "cell", "cell")
    def TeleportEntity(self, entity: int, origin: int, angles: int, velocity: int) -> None:
        def vector(pointer):
            return [struct.unpack("<f", struct.pack("<I", self.amx._getheapcell(pointer + offset * 4) & 0xFFFFFFFF))[0]
                    for offset in range(3)]
        self.teleports.append({"entity": entity, "origin": vector(origin), "angles": vector(angles), "velocity": vector(velocity)})


PRELUDE = r'''
#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#define MAX_NPCS 512
#define MAX_BOSSES MAX_NPCS
// Test binding for the timer constructor; the production Queue body remains
// exact, including its interval, repeat flags and two-cell DataPack contract.
native Handle VMCreateDataTimer(float interval,Function callback,DataPack &pack,int flags);
#define CreateDataTimer VMCreateDataTimer
bool gEnabled,gCoreReady;
int gEpoch,gBossCount;
enum struct VMNPC
{
    bool used;
    bool ready;
    bool dead;
    bool active;
    bool paused;
    int ref;
    int registrationEpoch;
    int boss;
    int lastHP;
}
VMNPC gNPCs[MAX_NPCS];
int vmWorldRef,vmTraceCalls,vmRestoreCalls,vmFreezeCalls,vmParkCalls;
bool vmEntityValid,vmHullClear;
float vmMins[3],vmMaxs[3];
public int Resolve(int ref) { return vmEntityValid && ref==vmWorldRef ? 100 : -1; }
public int FindNPC(int entity) { return entity==100 ? 0 : -1; }
public void ParkNPC(int n) { vmParkCalls++;gNPCs[n].active=false;gNPCs[n].paused=true; }
public void RestoreNPCBody(int n) { vmRestoreCalls++;gNPCs[n].paused=false; }
public void FreezeNPC(int n) { vmFreezeCalls++; }
public void NPCBounds(int entity,float mins[3],float maxs[3]) { mins=vmMins;maxs=vmMaxs; }
public bool BossSpawnHullClear(const float position[3],const float mins[3],const float maxs[3],int entity)
{ vmTraceCalls++;return vmHullClear; }
'''


WRAPPERS = r'''
public int VM_Reset()
{
    gEnabled=true;gCoreReady=true;gEpoch=7;gBossCount=1;
    vmWorldRef=-123456789;vmEntityValid=true;vmHullClear=false;
    vmTraceCalls=0;vmRestoreCalls=0;vmFreezeCalls=0;vmParkCalls=0;
    VMNPC blank;BossDef blankBoss;
    for(int i=0;i<MAX_NPCS;i++)
    {gNPCs[i]=blank;gBosses[i]=blankBoss;gBossPositionPending[i]=false;gBossPositionRef[i]=0;gBossPositionEpoch[i]=0;gBossPositionAttempts[i]=0;gBossPositionStarted[i]=0.0;}
    gNPCs[0].used=true;gNPCs[0].ready=true;gNPCs[0].ref=vmWorldRef;gNPCs[0].registrationEpoch=gEpoch;
    gNPCs[0].boss=0;gNPCs[0].paused=true;gNPCs[0].active=false;gNPCs[0].lastHP=50000;
    gBosses[0].savedHP=50000;gBosses[0].maxHP=50000;gBosses[0].boundThisRound=true;
    gBosses[0].origin=view_as<float>({100.25,200.5,-1535.96875});gBosses[0].angles=view_as<float>({0.0,180.0,0.0});
    strcopy(gBosses[0].id,sizeof(gBosses[].id),"boss_01");
    vmMins=view_as<float>({-1.5,-125.8,-1.5});vmMaxs=view_as<float>({1.5,1.5,20.5});return 1;
}
public int VM_Queue() { QueueBossPosition(0);return 1; }
public int VM_Callback(int pack) { return view_as<int>(BossPosition(null,view_as<DataPack>(pack))); }
public int VM_Continue() { return view_as<int>(Plugin_Continue); }
public int VM_Stop() { return view_as<int>(Plugin_Stop); }
public int VM_TimerFlags(int flags) { return (flags&TIMER_REPEAT)!=0 && (flags&TIMER_FLAG_NO_MAPCHANGE)!=0; }
public int VM_Allowed(int attempts,float elapsed) { return BossPositionRetryAllowed(attempts,elapsed); }
public int VM_SetClear(int value) { vmHullClear=value!=0;return 1; }
public int VM_FinalBounds()
{ vmMins=view_as<float>({-26.0,-26.0,0.0});vmMaxs=view_as<float>({26.0,26.0,144.0});return 1; }
public int VM_SetSavedHP(int value) { gBosses[0].savedHP=value;return 1; }
public int VM_State(int field)
{
    switch(field)
    {
        case 0:return gBossPositionPending[0];
        case 1:return gBossPositionAttempts[0];
        case 2:return vmTraceCalls;
        case 3:return vmRestoreCalls;
        case 4:return vmFreezeCalls;
        case 5:return vmParkCalls;
        case 6:return gBosses[0].positionedHP;
        case 7:return gBosses[0].savedHP;
        case 8:return gNPCs[0].lastHP;
        case 9:return gNPCs[0].active;
        case 10:return gNPCs[0].paused;
        case 11:return gBosses[0].awakened;
        case 12:return gBosses[0].defeated;
    }
    return -1;
}
public int VM_Guard(int field,int value)
{
    switch(field)
    {
        case 0:vmEntityValid=value!=0;
        case 1:gEpoch=value;
        case 2:gEnabled=value!=0;
        case 3:gCoreReady=value!=0;
        case 4:gNPCs[0].used=value!=0;
        case 5:gNPCs[0].ready=value!=0;
        case 6:gNPCs[0].dead=value!=0;
        case 7:gNPCs[0].registrationEpoch=value;
        case 8:gNPCs[0].boss=value;
        case 9:gBosses[0].defeated=value!=0;
        case 10:gBosses[0].positionedHP=value;
        case 11:gBosses[0].awakened=value!=0;
        case 12:gNPCs[0].ref=value;
    }
    return 1;
}
public int VM_NewTask(int ref,int epoch)
{gBossPositionRef[0]=ref;gBossPositionEpoch[0]=epoch;gBossPositionPending[0]=true;return 1;}
'''


def production_source():
    copied = {}
    boss = base.compat.extract_block(PRODUCTION / "Types.inc", "enum struct BossDef")
    copied["Types.inc:BossDef"] = hashlib.sha256(boss.encode("utf-8")).hexdigest()
    parts = [PRELUDE, boss, "BossDef gBosses[MAX_BOSSES];"]
    npc_text = (PRODUCTION / "NPC.inc").read_text("utf-8-sig")
    definitions = npc_text[npc_text.index("#define BOSS_POSITION_RETRY_INTERVAL"):npc_text.index("bool BossPositionRetryAllowed(")]
    parts.append(definitions)
    copied["NPC.inc:retry_globals"] = hashlib.sha256(definitions.encode("utf-8")).hexdigest()
    for marker in ("bool BossPositionRetryAllowed(", "bool BossPositionContextValid(", "void FinishBossPositionRetry(",
                   "void QueueBossPosition(", "public Action BossPosition("):
        block = base.compat.extract_block(PRODUCTION / "NPC.inc", marker)
        copied["NPC.inc:" + marker] = hashlib.sha256(block.encode("utf-8")).hexdigest()
        parts.append(block)
    parts.append(WRAPPERS)
    return "\n".join(parts), copied


def run():
    REPORT.mkdir(parents=True, exist_ok=True)
    source, copied = production_source()
    smx = base.compat.compile_source("boss_position_retry_vm", source)
    plugin = base.SourcePawnPlugin(io.BytesIO(smx.read_bytes()), smsys_options={"natives_cls": RetryNatives})
    vm = plugin.runtime
    vm.console_redirect = None
    checks = []

    def call(name, *args):
        return vm.call_function_by_name("VM_" + name, *args)

    def check(name, actual, expected):
        checks.append({"name": name, "passed": actual == expected, "actual": actual, "expected": expected})

    def reset(queue=True):
        RetryNatives.now = 0.0
        RetryNatives.packs = {}
        RetryNatives.timers = []
        RetryNatives.teleports = []
        RetryNatives.messages = []
        RetryNatives.errors = []
        RetryNatives.health = {}
        call("Reset")
        vm.amx._local_to_string = vm.amx._getheapstring
        if queue:
            call("Queue")
            assert len(RetryNatives.timers) == 1
            return RetryNatives.timers[0]["pack"]
        return -1

    pack = reset()
    continue_value, stop_value = call("Continue"), call("Stop")
    check("queue uses repeat and no-mapchange flags", call("TimerFlags", RetryNatives.timers[0]["flags"]), 1)
    check("queue interval is 0.1 seconds", round(RetryNatives.timers[0]["interval"], 6), 0.1)
    check("DataPack retains full signed ref plus epoch only", RetryNatives.packs[pack]["cells"], [-123456789, 7])
    call("Queue")
    check("duplicate queue does not create a second placement timer", len(RetryNatives.timers), 1)
    for attempt in (1, 2):
        RetryNatives.now = attempt / 10
        check(f"temporary starting hull failure {attempt} requests retry", call("Callback", pack), continue_value)
        check(f"temporary starting hull failure {attempt} leaves body hidden/parked", [call("State", field) for field in (9, 10, 3, 4)], [0, 1, 0, 0])
        check(f"temporary starting hull failure {attempt} does not log terminal failure", len(RetryNatives.errors), 0)
        check(f"temporary starting hull failure {attempt} does not teleport", len(RetryNatives.teleports), 0)
    call("FinalBounds")
    call("SetClear", 1)
    RetryNatives.now = 0.3
    check("final actual hull permits original point on third attempt", call("Callback", pack), stop_value)
    check("successful placement restores exactly 50000 HP once", [RetryNatives.health[100], call("State", 6), call("State", 8), call("State", 3)], [50000, 50000, 50000, 1])
    check("success consumes pending task and preserves dormant flag", [call("State", field) for field in (0, 1, 4, 9, 10, 11)], [0, 3, 1, 1, 0, 0])
    check("exact configured origin/angles are used without height changes", RetryNatives.teleports,
          [{"entity": 100, "origin": [100.25, 200.5, -1535.96875], "angles": [0.0, 180.0, 0.0], "velocity": [0.0, 0.0, 0.0]}])
    check("success log records actual attempt count", any("attempts=3" in row for row in RetryNatives.messages), True)
    check("late callback after success stops", call("Callback", pack), stop_value)
    call("Queue")
    check("success cannot be queued again into a combat teleport", [len(RetryNatives.timers), len(RetryNatives.teleports), call("State", 3)], [1, 1, 1])
    call("Guard", 10, 0)
    check("consumed callback stays stopped even if positioning receipt is manually cleared", call("Callback", pack), stop_value)
    check("consumed callback never teleports twice", len(RetryNatives.teleports), 1)

    for attempts, elapsed, expected in ((1, 0.1, 1), (49, 4.9, 1), (50, 4.9, 0), (1, 5.0, 0), (0, 0.1, 0), (-1, 0.1, 0), (1, -0.1, 0)):
        check(f"retry budget attempts={attempts}/elapsed={elapsed}", call("Allowed", attempts, elapsed), expected)

    pack = reset()
    for attempt in range(1, 51):
        RetryNatives.now = attempt / 10
        result = call("Callback", pack)
        check(f"blocked point attempt {attempt} is bounded", result, continue_value if attempt < 50 else stop_value)
    check("permanently blocked point logs exactly one final failure", len(RetryNatives.errors), 1)
    check("timeout reports final sampled bounds and attempts", "after 50 attempts" in RetryNatives.errors[0] and "mins=-1.5 -125.8 -1.5" in RetryNatives.errors[0], True)
    check("timeout leaves HP untouched and NPC parked", [len(RetryNatives.teleports), len(RetryNatives.health), call("State", 7), call("State", 9), call("State", 10), call("State", 0)], [0, 0, 50000, 0, 1, 0])
    check("callback replay after timeout stops without another error", call("Callback", pack), stop_value)
    check("terminal error count remains one", len(RetryNatives.errors), 1)
    pack = reset()
    RetryNatives.now = 5.0
    check("elapsed deadline stops a stalled first failed callback", call("Callback", pack), stop_value)
    check("elapsed deadline does not need fifty attempts", [call("State", 1), len(RetryNatives.errors)], [1, 1])

    guards = [("lost entity", 0, 0), ("old map epoch", 1, 8), ("disabled main", 2, 0),
              ("unavailable core API", 3, 0), ("unused slot", 4, 0), ("not ready", 5, 0),
              ("dead NPC", 6, 1), ("stale registration epoch", 7, 6), ("ordinary NPC", 8, -1),
              ("invalid Boss index", 8, 1), ("defeated Boss", 9, 1), ("already positioned", 10, 50000),
              ("reused slot ref", 12, -22222222)]
    for name, field, value in guards:
        pack = reset()
        call("SetClear", 1)
        call("Guard", field, value)
        check(name + " stops the placement callback", call("Callback", pack), stop_value)
        check(name + " does not trace/teleport/write HP", [call("State", 2), len(RetryNatives.teleports), len(RetryNatives.health)], [0, 0, 0])
        check(name + " does not emit false geometry failure", len(RetryNatives.errors), 0)
    pack = reset()
    call("Guard", 12, -22222222)
    call("NewTask", -22222222, 7)
    check("old-ref callback cannot cancel replacement task", [call("Callback", pack), call("State", 0)], [stop_value, 1])
    pack = reset()
    call("Guard", 1, 8)
    call("NewTask", -123456789, 8)
    check("old-epoch callback cannot cancel replacement task", [call("Callback", pack), call("State", 0)], [stop_value, 1])

    reset(queue=False)
    call("Guard", 9, 1)
    call("Queue")
    check("defeated Boss is parked without starting a timer", [len(RetryNatives.timers), call("State", 5)], [0, 1])
    reset(queue=False)
    call("Guard", 10, 50000)
    call("Queue")
    check("already positioned Boss does not start another timer", len(RetryNatives.timers), 0)
    pack = reset()
    call("SetSavedHP", 43123)
    call("SetClear", 1)
    RetryNatives.now = 0.1
    call("Callback", pack)
    check("saved campaign HP is retained rather than refilled", [RetryNatives.health[100], call("State", 7), call("State", 8)], [43123, 43123, 43123])

    failed = sum(not row["passed"] for row in checks)
    log = (BUILD / "boss_position_retry_vm_compile.txt").read_text("utf-8")
    return {"generated_at": datetime.now(timezone(timedelta(hours=8))).isoformat(),
            "passed": failed == 0, "checks_passed": len(checks) - failed, "checks_failed": failed,
            "production_source_sha256": copied, "fixture_smx_sha256": hashlib.sha256(smx.read_bytes()).hexdigest(),
            "fixture_compile_warning_count": len(re.findall(r"warning \d+", log)),
            "source_mode": "exact production retry globals, QueueBossPosition and BossPosition plus their guard helpers",
            "timer_and_engine_adapters": "DataPack cells, timer constructor, current time, body properties, hull results, body freeze/restore and teleport capture",
            "actual_engine_startup_timing_proven": False, "game_commands_sent": False, "npc_or_player_created": False,
            "limits": "The 5-second failure window is checked on the timer callback; real scheduler tick latency remains an engine property. Actual HZS transient-to-final bounds and first-round success require separate engine observation.",
            "checks": checks}


if __name__ == "__main__":
    result = run()
    output = REPORT / "boss_position_retry_vm_verification.json"
    if output.exists():
        raw = output.read_bytes()
        history = REPORT / "history" / ("boss_position_retry_vm_" + hashlib.sha256(raw).hexdigest() + ".json")
        history.parent.mkdir(parents=True, exist_ok=True)
        if not history.exists():
            history.write_bytes(raw)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in ("passed", "checks_passed", "checks_failed", "fixture_compile_warning_count")}, ensure_ascii=False))
    if not result["passed"]:
        print(json.dumps([row for row in result["checks"] if not row["passed"]], ensure_ascii=False, indent=2))
        raise SystemExit(1)
