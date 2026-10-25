"""Offline execution of the production Boss melee ticket and damage hook.

Uses real live days/zombies/bosses KeyValues inputs and exact production
functions. Entity/client/clock/wall natives are fixture adapters. No server
commands, plugin loads, damage, player input, or live-file writes occur here.
"""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
import ctypes
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ROOT / "addons/sourcemod/scripting/HanZombieShelter"
REPORT = ROOT / "tests/reports/v082_boss_damage_20261025"
BUILD = REPORT / "vm"
spec = importlib.util.spec_from_file_location("boss_damage_support", ROOT / "tests/test_v082_boss_spawns_vm.py")
support = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = support
spec.loader.exec_module(support)
support.compat.BUILD = BUILD


class DamageNatives(support.BossVMNatives):
    tick = 100
    serials = {1: 101, 2: 102}
    hp = 50000
    errors = []

    @support.native
    def GetGameTickCount(self) -> int:
        return self.tick

    @support.native("cell")
    def GetClientSerial(self, client: int) -> int:
        return self.serials.get(client, 0)

    @support.native("cell")
    def IsClientInGame(self, client: int) -> bool:
        return client in self.serials

    @support.native("cell", "cell", "string", "cell", "cell")
    def GetEntProp(self, entity: int, prop_type: int, key: str, size: int, element: int) -> int:
        assert entity > 4 and key == "m_iHealth", (entity, key)
        return self.hp

    @support.native("string", "...")
    def LogError(self, fmt: str, *args) -> None:
        self.errors.append(support.atcprintf(self.amx, fmt, args))

    @support.native("float", "float")
    def __FLOAT_GE__(self, left: float, right: float) -> bool:
        return left >= right

    @support.native("float", "float")
    def __FLOAT_LE__(self, left: float, right: float) -> bool:
        return left <= right

    @support.native("float", "float")
    def __FLOAT_SUB__(self, left: float, right: float) -> float:
        return left - right

    @support.native("float", "float")
    def __FLOAT_ADD__(self, left: float, right: float) -> float:
        return left + right

    @support.native("float", "float")
    def __FLOAT_MUL__(self, left: float, right: float) -> float:
        return ctypes.c_float(left * right).value

    @support.native("float", "float")
    def __FLOAT_DIV__(self, left: float, right: float) -> float:
        return left / right

    @support.native("float", "float")
    def __FLOAT_NE__(self, left: float, right: float) -> bool:
        return left != right

    @support.native("cell")
    def float(self, value: int) -> float:
        return float(value)


PRELUDE = r'''
#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#include <sdkhooks>
#define MAX_NPCS 512
#define MaxClients 4
enum ConfigFile { C_Days,C_Zombies,C_Count }
enum Phase { Phase_Waiting,Phase_Day,Phase_Night,Phase_Ending }
KeyValues gCfg[C_Count];
int gDay,gEpoch;
bool gEnabled,gCoreReady;
Phase gPhase;
enum struct VMNPC
{
    bool used;
    int ref;
    int boss;
    bool active;
    bool ready;
    bool dead;
    bool settled;
    bool paused;
    int registrationEpoch;
    int attackingUID;
}
enum struct VMBoss
{
    bool awakened;
    bool defeated;
    float damage;
}
VMNPC gNPCs[MAX_NPCS];
VMBoss gBosses[MAX_NPCS];
int vmWall;
float vmDamage;
public bool LiveHuman(int client) { return client==1 || client==2; }
public int Resolve(int ref) { return ref>4 ? ref%1000 : -1; }
public int FindNPC(int entity)
{
    for(int n=0;n<MAX_NPCS;n++)if(gNPCs[n].used && Resolve(gNPCs[n].ref)==entity)return n;
    return -1;
}
public int BlockingBuilding(int npc,int client) { return vmWall; }
'''

WRAPPERS = r'''
public int VM_Reset(int boss,int day)
{
    gEnabled=true;gCoreReady=true;gPhase=Phase_Day;gEpoch=7;gDay=day;vmWall=-1;
    gCfg[C_Days]=view_as<KeyValues>(100);gCfg[C_Zombies]=view_as<KeyValues>(101);
    VMNPC blank;BossMeleeTicket ticket;
    for(int n=0;n<MAX_NPCS;n++){gNPCs[n]=blank;gBossMeleeTickets[n]=ticket;}
    gNPCs[0].used=true;gNPCs[0].ref=1020;gNPCs[0].boss=boss;
    gNPCs[0].active=true;gNPCs[0].ready=true;gNPCs[0].registrationEpoch=gEpoch;
    for(int b=0;b<3;b++){gBosses[b].awakened=true;gBosses[b].defeated=false;gBosses[b].damage=120.0;}
    return 1;
}
public int VM_Arm(int victim) { Han_OnZombieAttack(20,victim);return gBossMeleeTickets[0].pending; }
public int VM_ArmEntity(int entity,int victim) { Han_OnZombieAttack(entity,victim);return gBossMeleeTickets[0].pending; }
public int VM_Hit(int victim,int attacker,int inflictor,float amount,int kind)
{
    int result=view_as<int>(HumanDamageHook(victim,attacker,inflictor,amount,kind));vmDamage=amount;return result;
}
public int VM_Damage() { return view_as<int>(vmDamage); }
public int VM_Pending() { return gBossMeleeTickets[0].pending; }
public int VM_Set(int field,int value)
{
    switch(field)
    {
        case 0:gEnabled=value!=0;
        case 1:gCoreReady=value!=0;
        case 2:gNPCs[0].active=value!=0;
        case 3:gNPCs[0].ready=value!=0;
        case 4:gNPCs[0].dead=value!=0;
        case 5:gNPCs[0].settled=value!=0;
        case 6:gNPCs[0].paused=value!=0;
        case 7:gNPCs[0].registrationEpoch=value;
        case 8:gNPCs[0].attackingUID=value;
        case 9:gBosses[0].awakened=value!=0;
        case 10:vmWall=value;
        case 11:gEpoch=value;
        case 12:gNPCs[0].ref=value;
        case 13:gBosses[0].defeated=value!=0;
        case 14:gPhase=view_as<Phase>(value);
    }
    return 1;
}
public int VM_BuildingDamage(int boss,float configured)
{
    if(boss>=0)gBosses[boss].damage=configured;
    __PRODUCTION_BUILDING_EXPRESSION__
    return view_as<int>(damage);
}
'''


def production_source():
    parts = [PRELUDE]
    snapshots = {}
    for filename, marker in (
        ("Combat.inc", "enum struct BossMeleeTicket"),
        ("Combat.inc", "public void Han_OnZombieAttack("),
        ("Combat.inc", "bool ConsumeBossMeleeTicket("),
        ("Combat.inc", "public Action HumanDamageHook("),
        ("Types.inc", "bool Running("),
        ("Config.inc", "bool Node("),
        ("Config.inc", "float Decimal("),
        ("Config.inc", "bool SelectDay("),
        ("Config.inc", "float HPScale("),
        ("Config.inc", "float DayPlayerDamageScale("),
        ("Config.inc", "float PlayerDamageScale("),
    ):
        block = support.compat.extract_block(PRODUCTION / filename, marker)
        snapshots[filename + ":" + marker] = hashlib.sha256(block.encode()).hexdigest()
        parts.append(block)
        if marker.startswith("enum"):
            parts.append("BossMeleeTicket gBossMeleeTickets[MAX_NPCS];")
    line = next(line.strip() for line in (PRODUCTION / "NPC.inc").read_text("utf-8-sig").splitlines()
                if "float damage=boss>=0 ? gBosses[boss].damage" in line)
    snapshots["NPC.inc:building damage expression"] = hashlib.sha256(line.encode()).hexdigest()
    parts.append(WRAPPERS.replace("__PRODUCTION_BUILDING_EXPRESSION__", line))
    return "\n".join(parts), snapshots


def run():
    REPORT.mkdir(parents=True, exist_ok=True)
    source, copied = production_source()
    output = support.compat.compile_source("boss_damage_vm", source)
    log = (BUILD / "boss_damage_vm_compile.txt").read_text("utf-8")
    assert not re.search(r"\b(?:error|warning)\s+\d+", log, re.I), log
    plugin = support.SourcePawnPlugin(io.BytesIO(output.read_bytes()), smsys_options={"natives_cls": DamageNatives})
    vm = plugin.runtime
    vm.console_redirect = None
    checks = []
    cfg = support.LOCAL_GAME / "addons/sourcemod/configs/HanZombieShelter"
    day_tree, days = support.parsed_tree(cfg / "days.cfg")
    zombie_tree, zombies = support.parsed_tree(cfg / "zombies.cfg")
    _, bosses = support.parsed_tree(cfg / "bosses.cfg")
    support.BossVMNatives.trees = {100: support.KVCursor(day_tree), 101: support.KVCursor(zombie_tree)}

    def call(name, *args):
        return vm.call_function_by_name("VM_" + name, *args)

    def check(name, actual, expected):
        passed = abs(actual - expected) <= 0.0001 if isinstance(expected, float) else actual == expected
        checks.append(dict(check=name, passed=passed, actual=actual, expected=expected))

    def reset(boss=0, day=45):
        DamageNatives.tick = 100
        DamageNatives.hp = 50000
        DamageNatives.serials = {1: 101, 2: 102}
        DamageNatives.errors = []
        call("Reset", boss, day)
        vm.amx._local_to_string = vm.amx._getheapstring

    def hit(amount=30.0, victim=1, attacker=20, inflictor=20, kind=4):
        action = call("Hit", victim, attacker, inflictor, amount, kind)
        return action, support.float_cell(call("Damage"))

    progression = [(1, 30.0), (2, 31.5), (4, 31.5), (5, 33.0), (9, 33.0),
                   (10, 34.5), (30, 40.5), (44, 44.7), (45, 45.0), (1000, 45.0), (2147483647, 45.0)]
    for boss in range(3):
        for day, expected in progression:
            reset(boss, day)
            check(f"Boss {boss} day {day} native forward arms one ticket", call("Arm", 1), 1)
            action, amount = hit()
            check(f"Boss {boss} day {day} base30 native melee", amount, expected)
            check(f"Boss {boss} day {day} SDK action", action, 0 if day == 1 else 1)
            check(f"Boss {boss} day {day} ticket consumed", call("Pending"), 0)
    for day, expected in progression:
        reset(-1, day)
        check(f"Ordinary day {day} no Boss ticket", call("Arm", 1), 0)
        check(f"Ordinary day {day} existing base10 scaling", hit(10.0)[1], expected / 3.0)

    for boss in range(3):
        for skill_damage in (3.0, 10.0, 20.0):
            reset(boss)
            check(f"Boss {boss} direct slash skill {skill_damage} without native event", hit(skill_damage)[1], skill_damage)
            call("Arm", 1)
            hit()
            check(f"Boss {boss} same-tick skill {skill_damage} after native strike", hit(skill_damage)[1], skill_damage)
        reset(boss)
        call("Arm", 1)
        DamageNatives.tick += 1
        check(f"Boss {boss} delayed skill expires unused ticket", hit(20.0)[1], 20.0)
        check(f"Boss {boss} stale ticket cleared", call("Pending"), 0)
        reset(boss)
        call("Arm", 1)
        check(f"Boss {boss} projectile not scaled", hit(20.0, inflictor=21)[1], 20.0)
        check(f"Boss {boss} wrong victim skill not scaled", hit(20.0, victim=2)[1], 20.0)
        check(f"Boss {boss} other damage type not scaled", hit(20.0, kind=8)[1], 20.0)
        check(f"Boss {boss} matching native hit remains available", hit()[1], 45.0)

    # State can change both before the native forward and before its damage.
    gates = [("not ready", 1, 0), ("inactive", 2, 0), ("NPC not ready", 3, 0),
             ("dead", 4, 1), ("settled", 5, 1), ("paused", 6, 1),
             ("old registration epoch", 7, 6), ("attacking building", 8, 99),
             ("sleeping", 9, 0), ("waiting phase", 14, 0), ("ending phase", 14, 3)]
    for name, field, value in gates:
        reset()
        call("Set", field, value)
        check(name + " does not arm", call("Arm", 1), 0)
        check(name + " retains damage denial", hit(), (3, 0.0))
        reset()
        call("Arm", 1)
        call("Set", field, value)
        check(name + " after arm denies hit", hit(), (3, 0.0))
        check(name + " denied hit consumes ticket", call("Pending"), 0)
    reset()
    call("Arm", 1)
    call("Set", 10, 0)
    check("Owned wall blocks native strike", hit(), (3, 0.0))
    call("Set", 10, -1)
    check("Blocked strike cannot scale same-tick later skill", hit(20.0)[1], 20.0)
    reset()
    call("Set", 0, 0)
    check("Global disabled does not arm", call("Arm", 1), 0)
    check("Global disabled keeps original pass-through", hit(), (0, 30.0))
    reset()
    call("Set", 13, 1)
    check("Defeated Boss does not arm", call("Arm", 1), 0)
    reset()
    DamageNatives.hp = 0
    check("Dead entity health does not arm", call("Arm", 1), 0)
    reset()
    call("Arm", 1)
    call("Set", 11, 8)
    call("Set", 7, 8)
    check("New epoch cannot reuse otherwise-valid ticket", hit()[1], 30.0)
    check("New epoch clears ticket", call("Pending"), 0)
    reset()
    call("Arm", 1)
    call("Set", 12, 2020)
    check("New entref at same entity index cannot reuse ticket", hit()[1], 30.0)
    check("New entref clears ticket", call("Pending"), 0)
    reset()
    call("Arm", 1)
    DamageNatives.serials[1] = 201
    check("Reused client slot cannot consume old player's ticket", hit()[1], 30.0)
    reset()
    check("Untracked attacker cannot arm", call("ArmEntity", 30, 1), 0)
    check("Nonhuman target cannot arm", call("Arm", 50), 0)
    check("Untracked slash attacker unchanged", hit(20.0, attacker=30, inflictor=30)[1], 20.0)
    check("World poison unchanged", hit(7.0, attacker=0, inflictor=0, kind=131072)[1], 7.0)
    check("Existing PvP prevention unchanged", hit(20.0, attacker=2, inflictor=2), (3, 0.0))
    reset()
    call("Arm", 1)
    call("Arm", 2)
    check("New target replaces prior ticket", hit()[1], 30.0)
    check("Latest target native melee scales", hit(victim=2)[1], 45.0)
    reset()
    call("Arm", 1)
    check("Zero damage stays zero and consumes", hit(0.0)[1], 0.0)
    check("Zero damage consumes ticket", call("Pending"), 0)
    reset()
    call("Arm", 1)
    check("Overflow preserves original finite input", hit(3.0e38)[0], 0)
    check("Overflow logs exactly once", len(DamageNatives.errors), 1)

    for day in (1, 2, 5, 10, 30, 45):
        reset(day=day)
        for index, (name, profile) in enumerate(bosses["BossTypes"].items()):
            configured = float(profile["building_damage"])
            check(f"Live {name} configured building damage", configured, 120.0)
            check(f"{name} building damage stays fixed day {day}", support.float_cell(call("BuildingDamage", index, configured)), 120.0)
    for day, expected in ((1, 20.0), (2, 24.0), (5, 32.0), (10, 40.0), (30, 80.0), (45, 100.0)):
        reset(-1, day)
        check(f"Ordinary building formula unchanged day {day}", support.float_cell(call("BuildingDamage", -1, 0.0)), expected)

    report = dict(at=datetime.now(timezone(timedelta(hours=8))).isoformat(),
                  scope="Offline exact production functions with engine/client/wall adapters; no live damage or AI test",
                  passed=sum(row["passed"] for row in checks), failed=sum(not row["passed"] for row in checks),
                  production_blocks=copied,
                  live_config_inputs={str(cfg / name): hashlib.sha256((cfg / name).read_bytes()).hexdigest()
                                      for name in ("days.cfg", "zombies.cfg", "bosses.cfg")},
                  native_base_damage_dependency="Root must set HZS ZombieData boss_pangzi/boss_angela/boss_yixing damage=30; native config is loaded at OnMapStart. This VM injects 30 as engine damage input.",
                  skill_isolation="Existing 3 Boss skills do not synchronously apply matching health damage inside Han_OnZombieAttack; delayed or unrelated slash skills lack a current one-use ticket.",
                  vm_build_sha256=hashlib.sha256(output.read_bytes()).hexdigest(), checks=checks)
    (REPORT / "production_vm.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "failed": report["failed"], "report": str(REPORT / "production_vm.json")}, ensure_ascii=False))
    for row in checks:
        if not row["passed"]:
            print(json.dumps(row, ensure_ascii=False))
    return 1 if report["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(run())
