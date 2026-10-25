"""Run the production Boss spawn/bridge code in an isolated SourcePawn VM.

The Python layer supplies KeyValues and basic native API behavior, not the
Boss parsing, assignment, claiming or native-budget rules being verified.
No server, client, map trace, NPC generation or game command is used.
"""
from __future__ import annotations

import ctypes
import argparse
from dataclasses import dataclass, field
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
DEFAULT_REPORT = ROOT / "tests/reports/v082_boss_spawnpoints_20261025"
REPORT = DEFAULT_REPORT
# Historical candidate inputs stay fixed when output reports are redirected.
# A new regression must not rewrite the frozen delivery's reports or sources.
CANDIDATE_MAPS = DEFAULT_REPORT / "candidate_maps"
BUILD = REPORT / "vm"
PRODUCTION = ROOT / "addons/sourcemod/scripting/HanZombieShelter"
LOCAL_ROOT = Path(r"D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器")
LOCAL_GAME = LOCAL_ROOT / "cstrike"

# Reuse only the already-installed VM/encoding compatibility adapters. Loading
# this module does not execute its __main__ block or write any crafting report.
spec = importlib.util.spec_from_file_location("v082_crafting_vm_compat", ROOT / "tests/test_v082_crafting_vm.py")
compat = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = compat
spec.loader.exec_module(compat)
compat.BUILD = BUILD
migration_spec = importlib.util.spec_from_file_location("v05_keyvalues_syntax_parser", ROOT / "tools/migrate_v05_materials.py")
migration = importlib.util.module_from_spec(migration_spec)
sys.modules[migration_spec.name] = migration
migration_spec.loader.exec_module(migration)
from smx.plugin import SourcePawnPlugin
from smx.sourcemod.natives.base import MethodMap, WritableString, native
from smx.sourcemod.printf import atcprintf
from smx.vm import SourcePawnAbstractMachine


_original_enter_scope = SourcePawnAbstractMachine._enter_heap_scope


def _linked_enter_heap_scope(self):
    # pysmx 0.4.0 writes the prior scope pointer but fails to publish the new
    # scope, so heap.restore on compiler-generated multidimensional arrays
    # can dereference unrelated data. Match the VM heap-scope chain semantics.
    scope = _original_enter_scope(self)
    self._hp_scope = scope
    return scope


SourcePawnAbstractMachine._enter_heap_scope = _linked_enter_heap_scope


@dataclass
class KVNode:
    name: str
    value: str | None = None
    children: list["KVNode"] = field(default_factory=list)


def scalar(name: str, value: object) -> KVNode:
    return KVNode(name, str(value))


def section(name: str, *children: KVNode) -> KVNode:
    return KVNode(name, children=list(children))


@dataclass
class KVCursor:
    root: KVNode
    current: KVNode | None = None
    history: list[tuple[KVNode, int]] = field(default_factory=list)

    def __post_init__(self):
        self.current = self.root

    def find(self, key: str) -> KVNode | None:
        if not key:
            return self.current
        return next((child for child in self.current.children if child.name == key), None)

    def enter(self, index: int):
        parent = self.current
        self.history.append((parent, index))
        self.current = parent.children[index]


def write_string(destination: WritableString, value: str) -> int:
    raw = value.encode("utf-8")[:max(0, destination.max_length - 1)]
    destination.write(raw, null_terminate=True)
    return len(raw)


def c_integer(text: str) -> int:
    match = re.match(r"^[\t\r\n ]*([+-]?\d+)", text)
    return ctypes.c_int32(int(match.group(1))).value if match else 0


FLOAT_PREFIX = re.compile(r"^[\t\r\n ]*[+-]?(?:nan(?:\([^)]*\))?|inf(?:inity)?|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)", re.IGNORECASE)


def c_float_prefix(text: str) -> tuple[float, int]:
    match = FLOAT_PREFIX.match(text)
    if not match:
        return 0.0, 0
    token = match.group(0)
    normalized = re.sub(r"nan\([^)]*\)", "nan", token, flags=re.IGNORECASE)
    return float(normalized), len(token.encode("utf-8"))


class KeyValuesMethods(MethodMap):
    @native("cell", "bool")
    def Rewind(self, handle: int, clear_history: bool) -> None:
        cursor = BossVMNatives.trees[handle]
        cursor.current = cursor.root
        cursor.history.clear()

    @native("cell", "string", "bool")
    def JumpToKey(self, handle: int, key: str, create: bool) -> bool:
        cursor = BossVMNatives.trees[handle]
        for index, child in enumerate(cursor.current.children):
            if child.name == key:
                cursor.enter(index)
                return True
        if create:
            cursor.current.children.append(section(key))
            cursor.enter(len(cursor.current.children) - 1)
            return True
        return False

    @native("cell", "bool")
    def GotoFirstSubKey(self, handle: int, key_only: bool) -> bool:
        cursor = BossVMNatives.trees[handle]
        for index, child in enumerate(cursor.current.children):
            if not key_only or child.value is None:
                cursor.enter(index)
                return True
        return False

    @native("cell", "bool")
    def GotoNextKey(self, handle: int, key_only: bool) -> bool:
        cursor = BossVMNatives.trees[handle]
        if not cursor.history:
            return False
        parent, previous = cursor.history[-1]
        for index in range(previous + 1, len(parent.children)):
            if not key_only or parent.children[index].value is None:
                cursor.history[-1] = (parent, index)
                cursor.current = parent.children[index]
                return True
        return False

    @native("cell")
    def GoBack(self, handle: int) -> bool:
        cursor = BossVMNatives.trees[handle]
        if not cursor.history:
            return False
        cursor.current = cursor.history.pop()[0]
        return True

    @native("cell", "writable_string")
    def GetSectionName(self, handle: int, destination) -> bool:
        write_string(destination, BossVMNatives.trees[handle].current.name)
        return True

    @native("cell", "string")
    def GetDataType(self, handle: int, key: str) -> int:
        node = BossVMNatives.trees[handle].find(key)
        return 1 if node is not None and node.value is not None else 0

    @native("cell", "string", "writable_string", "string")
    def GetString(self, handle: int, key: str, destination, fallback: str) -> None:
        node = BossVMNatives.trees[handle].find(key)
        write_string(destination, node.value if node is not None and node.value is not None else fallback)

    @native("cell", "string", "cell")
    def GetNum(self, handle: int, key: str, fallback: int) -> int:
        node = BossVMNatives.trees[handle].find(key)
        return c_integer(node.value) if node is not None and node.value is not None else fallback

    @native("cell", "string", "float")
    def GetFloat(self, handle: int, key: str, fallback: float) -> float:
        node = BossVMNatives.trees[handle].find(key)
        return c_float_prefix(node.value)[0] if node is not None and node.value is not None else fallback


class BossVMNatives(compat.CraftingVMNatives):
    KeyValues = KeyValuesMethods()
    trees: dict[int, KVCursor] = {}

    @native("float", "float")
    def __FLOAT_GT__(self, left: float, right: float) -> bool:
        return left > right

    @native("float", "float")
    def __FLOAT_LT__(self, left: float, right: float) -> bool:
        return left < right

    @native("writable_string", "string", "...")
    def Format(self, destination, fmt: str, *args) -> int:
        return write_string(destination, atcprintf(self.amx, fmt, args))

    @native("string", "cell")
    def StringToFloatEx(self, text: str, destination: int) -> int:
        value, consumed = c_float_prefix(text)
        try:
            number = ctypes.c_float(value)
        except OverflowError:
            number = ctypes.c_float(float("-inf") if value < 0 else float("inf"))
        self.amx._writeheap(destination, number)
        return consumed


PRELUDE = r'''
#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>
#define MAX_NPCS 512
#define MAX_BOSSES MAX_NPCS
#define BOSS_SPAWN_POINT_COUNT 5
enum ConfigFile { C_Map, C_Bosses, C_Count }
KeyValues gCfg[C_Count];
int gBossCount,gNativePoolSize,gPopulationLimit;
char vmLastFailure[256];
int vmFailureCount;
enum struct VMNPC
{
    bool used;
    int boss;
    int ref;
    int visualRef;
}
VMNPC gNPCs[MAX_NPCS];
int Resolve(int ref) { return ref; }
bool ConfigFailure(const char[] reason)
{
    vmFailureCount++;strcopy(vmLastFailure,sizeof(vmLastFailure),reason);return false;
}
'''


WRAPPERS = r'''
public int VM_Reset()
{
    gCfg[C_Map]=view_as<KeyValues>(100);gCfg[C_Bosses]=view_as<KeyValues>(101);
    gBossCount=0;gNativePoolSize=100;gPopulationLimit=100;vmLastFailure[0]=0;vmFailureCount=0;
    for(int n=0;n<MAX_NPCS;n++)gNPCs[n].used=false;
    return 1;
}
public int VM_ReadPoints() { return ReadBossSpawnPoints() ? 1 : 0; }
public int VM_ReadDefinitions() { return ReadBossDefinitions() ? 1 : 0; }
public int VM_Read() { return ReadBossSpawnPoints() && ReadBossDefinitions() ? 1 : 0; }
public int VM_Count() { return gBossCount; }
public int VM_SpawnPoint(int boss) { return gBosses[boss].spawnPoint; }
public int VM_Ordinal(int ordinal) { return BossSpawnPointForOrdinal(ordinal); }
public int VM_Origin(int boss,int axis) { return view_as<int>(gBosses[boss].origin[axis]); }
public int VM_Angle(int boss,int axis) { return view_as<int>(gBosses[boss].angles[axis]); }
public int VM_PointOrigin(int point,int axis) { return view_as<int>(gBossSpawnOrigins[point][axis]); }
public int VM_PointAngle(int point,int axis) { return view_as<int>(gBossSpawnAngles[point][axis]); }
public int VM_Required(int boss) { return gBosses[boss].required ? 1 : 0; }
public int VM_Bound(int boss) { return gBosses[boss].boundThisRound ? 1 : 0; }
public int VM_Defeated(int boss) { return gBosses[boss].defeated ? 1 : 0; }
public int VM_SetDefeated(int boss,int value) { gBosses[boss].defeated=value!=0;return 1; }
public int VM_SetRequired(int boss,int value) { gBosses[boss].required=value!=0;return 1; }
public int VM_AllBossesDefeated() { return AllBossesDefeated() ? 1 : 0; }
public int VM_SetOrigin(int boss,int axis,float value) { gBosses[boss].origin[axis]=value;return 1; }
public int VM_SetSavedHP(int boss,int value) { gBosses[boss].savedHP=value;return 1; }
public int VM_SavedHP(int boss) { return gBosses[boss].savedHP; }
public int VM_Claim(const char[] name) { return ClaimBossDefinition(name); }
public int VM_ClaimFat() { return ClaimBossDefinition("Fat Boss"); }
public int VM_ParseCount(const char[] text) { int count;return BossNativeParseCount(text,count) ? count : -1; }
public int VM_ParseCustom(const char[] custom)
{
    char keys[BOSS_NATIVE_CUSTOM_ENTRIES][BOSS_NATIVE_CUSTOM_KEY_BYTES];
    int counts[BOSS_NATIVE_CUSTOM_ENTRIES],entryCount,totalCount;
    return view_as<int>(BossNativeParseCustom(custom,keys,counts,entryCount,totalCount));
}
public int VM_ParseCustomCount(const char[] custom)
{
    char keys[BOSS_NATIVE_CUSTOM_ENTRIES][BOSS_NATIVE_CUSTOM_KEY_BYTES];
    int counts[BOSS_NATIVE_CUSTOM_ENTRIES],entryCount,totalCount;
    BossNativeParseCustom(custom,keys,counts,entryCount,totalCount);return entryCount;
}
public int VM_ParseCustomTotal(const char[] custom)
{
    char keys[BOSS_NATIVE_CUSTOM_ENTRIES][BOSS_NATIVE_CUSTOM_KEY_BYTES];
    int counts[BOSS_NATIVE_CUSTOM_ENTRIES],entryCount,totalCount;
    BossNativeParseCustom(custom,keys,counts,entryCount,totalCount);return totalCount;
}
public int VM_ValidateBridge(const char[] ordinary,const char[] custom) { return ValidateBossNativeCustomList(ordinary,custom) ? 1 : 0; }
public int VM_OrdinaryContains(const char[] ordinary,const char[] key) { return BossNativeCSVContainsKey(ordinary,key) ? 1 : 0; }
public int VM_SetPool(int pool,int population) { gNativePoolSize=pool;gPopulationLimit=population;return 1; }
public int VM_SetNPC(int slot,int used,int boss,int body,int visual)
{
    gNPCs[slot].used=used!=0;gNPCs[slot].boss=boss;gNPCs[slot].ref=body;gNPCs[slot].visualRef=visual;return 1;
}
public int VM_Filter(int entity,int ignored) { return BossPlacementTraceFilter(entity,0,ignored) ? 1 : 0; }
public int VM_Failures() { return vmFailureCount; }
public int VM_RequiredToken(const char[] tokens,const char[] word) { return TokenPresent(tokens,word) ? 1 : 0; }
'''


def production_source() -> tuple[str, dict[str, str]]:
    copied = {}
    parts = [PRELUDE]
    for filename, marker in [
        ("Types.inc", "enum struct BossDef"),
        ("Config.inc", "bool Node("),
        ("Config.inc", "int Number("),
        ("Config.inc", "float Decimal("),
        ("Config.inc", "void Text("),
        ("Config.inc", "bool TokenPresent("),
    ]:
        block = compat.extract_block(PRODUCTION / filename, marker)
        parts.append(block)
        copied[f"{filename}:{marker}"] = hashlib.sha256(block.encode("utf-8")).hexdigest()
    parts.append("BossDef gBosses[MAX_BOSSES];\n")
    for filename in ["BossSpawns.inc", "BossBridge.inc"]:
        path = PRODUCTION / filename
        parts.append('#include "' + path.as_posix() + '"\n')
        copied[filename + ":whole_include"] = hashlib.sha256(path.read_bytes()).hexdigest()
    victory = compat.extract_block(PRODUCTION / "NPC.inc", "bool AllBossesDefeated(")
    parts.append(victory)
    copied["NPC.inc:bool AllBossesDefeated("] = hashlib.sha256(victory.encode("utf-8")).hexdigest()
    parts.append(WRAPPERS)
    return "\n".join(parts), copied


def points() -> KVNode:
    return section("BossSpawnPoints", *[
        section(f"boss{index}", scalar("origin", f"{index*100} {index*100+1} {index*100+2}"), scalar("angles", f"0 {index*10} 0"))
        for index in range(1, 6)
    ])


def map_config(count: int, *, required: str = "", ids: list[str] | None = None) -> KVNode:
    ids = ids or [f"boss_{index:03}" for index in range(1, count + 1)]
    return section("ShelterMap", scalar("required_boss_ids", required), points(),
        section("Bosses", *[section(identifier, scalar("boss_profile", "fat")) for identifier in ids]))


def profiles() -> KVNode:
    return section("ShelterBosses", section("BossTypes",
        section("fat", scalar("han_config_key", "boss_fat"), scalar("public_name", "Fat Boss"), scalar("max_hp", "50000")),
        section("other", scalar("han_config_key", "boss_other"), scalar("public_name", "Other Boss"), scalar("max_hp", "60000"))))


def parsed_tree(path: Path) -> tuple[KVNode, dict]:
    document = migration.parse(path.read_text(encoding="utf-8-sig"))
    if len(document) != 1:
        raise ValueError(f"Expected one KeyValues root in candidate: {path}")

    def convert(name: str, value) -> KVNode:
        if isinstance(value, dict):
            return section(name, *[convert(key, child) for key, child in value.items()])
        return scalar(name, value)

    name, value = next(iter(document.items()))
    return convert(name, value), value


def float_cell(value: int) -> float:
    return struct.unpack("<f", struct.pack("<I", value & 0xFFFFFFFF))[0]


def run() -> dict:
    REPORT.mkdir(parents=True, exist_ok=True)
    source, copied = production_source()
    smx_path = compat.compile_source("boss_spawns_vm", source)
    plugin = SourcePawnPlugin(io.BytesIO(smx_path.read_bytes()), smsys_options={"natives_cls": BossVMNatives})
    vm = plugin.runtime
    vm.console_redirect = None
    checks = []
    candidate_checks = []

    def call(name, *args):
        try:
            return vm.call_function_by_name("VM_" + name, *args)
        except Exception:
            print("unbound-native-diagnostic:",[item.name for item in plugin.natives if vm.amx.sm_natives.get_native(item.name) is None])
            raise

    def check(name, actual, expected):
        checks.append({"name": name, "passed": actual == expected, "actual": actual, "expected": expected})
        if len(checks) % 40 == 0:
            print(json.dumps({"progress_checks":len(checks),"latest":name},ensure_ascii=False),flush=True)

    def install(map_tree=None, boss_tree=None):
        BossVMNatives.trees = {100: KVCursor(map_tree or map_config(1)), 101: KVCursor(boss_tree or profiles())}
        call("Reset")
        # Fix only the VM helper's string-address conversion; executable and
        # production string data remain the compiler's original bytes.
        vm.amx._local_to_string = vm.amx._getheapstring

    install()
    check("zero configured Bosses cannot satisfy the Boss victory objective", call("AllBossesDefeated"), 0)
    check("production parser smoke", call("Read"), 1)
    check("single Boss assigned first point", call("SpawnPoint", 0), 0)
    check("empty legacy required list still makes the configured Boss required", call("Required", 0), 1)
    check("single living configured Boss blocks victory", call("AllBossesDefeated"), 0)
    call("SetDefeated", 0, 1)
    check("single configured Boss death satisfies Boss victory", call("AllBossesDefeated"), 1)

    for count in [1, 5, 6, 10, 20]:
        install(map_config(count))
        check(f"{count} configured Boss definitions accepted", call("Read"), 1)
        check(f"{count} independent definitions retained", call("Count"), count)
        for index in range(count):
            check(f"ordinal {index} of {count} wraps through five slots", call("SpawnPoint", index), index % 5)
        last = (count - 1) % 5 + 1
        check(f"{count} final Boss coordinate copied from assigned point", float_cell(call("Origin", count-1, 0)), float(last*100))
    for ordinal in [-1, 0, 4, 5, 9, 10, 511]:
        check("pure ordinal assignment " + str(ordinal), call("Ordinal", ordinal), -1 if ordinal < 0 else ordinal % 5)

    # KeyValues roots preserve duplicate names and scalar/section distinctions.
    # The production parser, not the Python fixture, decides validity.
    malformed = []
    tree = map_config(1);tree.children[1].children.pop();malformed.append(("missing fifth point",tree))
    tree = map_config(1);tree.children[1].children.append(section("boss6",scalar("origin","0 0 0")));malformed.append(("extra sixth point",tree))
    tree = map_config(1);tree.children[1].children.append(section("boss1",scalar("origin","0 0 0")));malformed.append(("duplicate point name",tree))
    tree = map_config(1);tree.children[1].children[0]=scalar("boss1","0 0 0");malformed.append(("point supplied as scalar",tree))
    tree = map_config(1);tree.children[1].children[0].children=[scalar("angles","0 0 0")];malformed.append(("point missing origin",tree))
    tree = map_config(1);tree.children[1].children[0].children.append(scalar("origin","0 0 0"));malformed.append(("duplicate origin field",tree))
    tree = map_config(1);tree.children[1].children[0].children.append(scalar("unknown","0"));malformed.append(("unknown point field",tree))
    tree = map_config(1);tree.children[1].children[0].children[0]=section("origin",scalar("x","0"));malformed.append(("nested origin section",tree))
    for label, tree in malformed:
        install(tree)
        check(label + " rejected", call("ReadPoints"), 0)
        check(label + " reports configuration failure", call("Failures") > 0, True)
    for value in ["nan 0 0", "NaN 0 0", "inf 0 0", "-infinity 0 0", "1e999 0 0", "1e 0 0", "1.5x 0 0", "0 0", "0 0 0 0", "15000.01 0 0", "-15000.01 0 0", "", "x"*200]:
        tree=map_config(1);tree.children[1].children[0].children[0].value=value
        install(tree);check("invalid origin " + repr(value), call("ReadPoints"), 0)
    for value in ["0 361 0", "0 -361 0", "0 nan 0", "0 0", "0 0 0 trailing"]:
        tree=map_config(1);tree.children[1].children[0].children[1].value=value
        install(tree);check("invalid angles " + repr(value), call("ReadPoints"), 0)
    tree=map_config(1);tree.children[1].children[0].children=[scalar("origin","-15000 15000 0")]
    install(tree);check("coordinate boundary and omitted angles accepted", call("Read"), 1)
    check("omitted angles default to zero", [float_cell(call("Angle",0,axis)) for axis in range(3)], [0.0,0.0,0.0])
    tree=map_config(1);tree.children[1].children.reverse()
    install(tree);check("point section ordering does not change named slots", call("Read"), 1)
    check("boss1 remains first point after reverse KV order", float_cell(call("Origin",0,0)),100.0)
    tree=map_config(6);tree.children[2].children.insert(0,section("disabled",scalar("enabled","0")))
    install(tree);check("disabled definition skipped before ordinal allocation",call("Read"),1)
    check("enabled definitions still number six",call("Count"),6)
    check("disabled row does not consume first slot",call("SpawnPoint",0),0)
    check("disabled row does not shift wrapped slot",call("SpawnPoint",5),0)
    for index in range(6):call("SetDefeated",index,1)
    check("disabled definition is outside the enabled Boss victory objective",call("AllBossesDefeated"),1)
    for value in ["2", "false", "", "1 trailing"]:
        tree=map_config(1);tree.children[2].children[0].children.append(scalar("enabled",value))
        install(tree);check("invalid enabled value " + repr(value),call("Read"),0)
    tree=map_config(1);tree.children[2].children.append(section("boss_001",scalar("boss_profile","fat")))
    install(tree);check("duplicate enabled Boss ID rejected",call("Read"),0)
    tree=map_config(1);tree.children[2].children[0].children[0].value="missing_profile"
    install(tree);check("missing Boss profile rejected",call("Read"),0)
    ids=[f"long_boss_identifier_{index:03}" for index in range(1,25)]
    required=" ".join(ids)
    install(map_config(len(ids),required=required,ids=ids))
    check("legacy required list longer than 512 bytes does not prevent parsing",len(required)>512 and call("Read")==1,True)
    check("all 24 enabled definitions required regardless of legacy list",[call("Required",index) for index in range(24)],[1]*24)
    install(map_config(10,required="boss_010"));check("legacy subset selector map still loads",call("Read"),1)
    check("legacy subset does not exclude first Boss",call("Required",0),1)
    check("legacy subset does not exclude any enabled Boss",[call("Required",index) for index in range(10)],[1]*10)
    call("SetDefeated",9,1)
    check("only legacy-selected tenth Boss dead cannot win",call("AllBossesDefeated"),0)
    for index in range(1,9):call("SetDefeated",index,1)
    check("one remaining enabled Boss still blocks victory",call("AllBossesDefeated"),0)
    call("SetRequired",0,0)
    check("stale false required flag cannot bypass a remaining Boss",call("AllBossesDefeated"),0)
    call("SetDefeated",0,1)
    check("all ten configured Bosses dead satisfy production victory",call("AllBossesDefeated"),1)
    tree=map_config(3);tree.children.pop(0)
    install(tree);check("omitted legacy required selector map loads",call("Read"),1)
    check("omitted selector makes all enabled Bosses required",[call("Required",index) for index in range(3)],[1]*3)
    install(map_config(3,required="not_a_configured_boss"));check("unknown legacy selector is ignored",call("Read"),1)
    check("unknown legacy selector cannot produce empty victory objective",call("AllBossesDefeated"),0)
    check("unknown legacy selector still requires all enabled Bosses",[call("Required",index) for index in range(3)],[1]*3)
    check("independent legacy TokenPresent helper still scans beyond sixteen tokens",call("RequiredToken"," ".join(f"id{i}" for i in range(30)),"id29"),1)

    install(map_config(6));check("same-name lifecycle fixture loads",call("Read"),1)
    check("first same-name claim chooses first free instance",call("ClaimFat"),0)
    check("second same-name claim chooses next instance",call("ClaimFat"),1)
    check("bound state belongs to first two definitions",[call("Bound",index) for index in range(6)],[1,1,0,0,0,0])
    call("SetDefeated",0,1);call("SetSavedHP",0,123);call("SetOrigin",0,0,321.0)
    check("defeated state independent at shared position",call("Defeated",5),0)
    check("saved HP independent at shared position",call("SavedHP",5),50000)
    check("coordinate arrays copied independently for shared slot",float_cell(call("Origin",5,0)),100.0)
    check("changing one Boss does not mutate map point",float_cell(call("PointOrigin",0,0)),100.0)
    check("third claim proceeds across position allocation",call("ClaimFat"),2)
    for expected in [3,4,5]:check("subsequent same-name claim " + str(expected),call("ClaimFat"),expected)
    check("same-name claim exhausts independent definitions",call("ClaimFat"),-1)
    check("unconfigured public name not claimed",call("Claim","Other Name"),-1)

    for entity, used, boss, body, visual in [(0,1,0,101,201),(1,1,5,102,202),(2,1,-1,103,203),(3,0,0,104,204)]:
        call("SetNPC",entity,used,boss,body,visual)
    for entity, expected in [(101,0),(201,0),(102,0),(202,0),(103,1),(203,1),(104,1),(301,1),(1,1),(0,1)]:
        check("Boss placement filter for entity " + str(entity),call("Filter",entity,999),expected)
    check("placement filter excludes requested current entity",call("Filter",999,999),0)

    for value, expected in [("1",1),("512",512),("0",-1),("513",-1),("-1",-1),("+1",-1),("1.0",-1),("1e1",-1),("1 ",-1),("9"*80,-1)]:
        check("native count parser " + repr(value),call("ParseCount",value),expected)
    for value, expected in [("",0),("boss_fat:1",0),(",boss_fat:1",2),("boss_fat:1,",2),("a:1,,b:1",2),
        ("a:1,a:2",6),("a:0",5),("a:513",5),("a:+1",5),("a:1.0",5),("a: 1",5),("a:b:1",5),
        ("a:300,b:213",7),("a"*61+":1",0),("a"*62+":1",4)]:
        check("native custom parser " + repr(value),call("ParseCustom",value),expected)
    csv32=",".join(f"k{index:02}:1" for index in range(32))
    check("native custom 32-entry boundary accepted",call("ParseCustom",csv32),0)
    check("native custom count is exactly 32",call("ParseCustomCount",csv32),32)
    check("native custom total is exactly 32",call("ParseCustomTotal",csv32),32)
    check("native custom 33 entries rejected",call("ParseCustom",csv32+",extra:1"),3)
    csv511=",".join(f"k{index:02}"+"x"*58+":1" for index in range(8))
    check("511-byte custom fixture exact length",len(csv511),511)
    check("511-byte custom boundary accepted",call("ParseCustom",csv511),0)
    check("512-byte custom boundary rejected",call("ParseCustom",csv511+"x"),1)
    install(map_config(6));check("native bridge aggregate fixture loads",call("Read"),1)
    check("native custom count covers six same-key definitions",call("ValidateBridge","ordinary","boss_fat:6"),1)
    check("native count five cannot cover six definitions",call("ValidateBridge","ordinary","boss_fat:5"),0)
    check("native surplus seven cannot become an unclaimed ordinary Boss",call("ValidateBridge","ordinary","boss_fat:7"),0)
    check("empty native custom cannot cover configured Boss",call("ValidateBridge","ordinary",""),0)
    check("fixed Boss key excluded from ordinary pool",call("ValidateBridge","ordinary,boss_fat","boss_fat:6"),0)
    check("ordinary prefix resemblance does not match Boss key",call("ValidateBridge","boss_fat_small","boss_fat:6"),1)
    check("ordinary suffix resemblance does not match Boss key",call("ValidateBridge","small_boss_fat","boss_fat:6"),1)
    call("SetPool",250,250)
    check("2*pool plus all custom equals512 accepted",call("ValidateBridge","ordinary","boss_fat:6,extra:6"),1)
    check("unrelated custom count also reserves tracking capacity",call("ValidateBridge","ordinary","boss_fat:6,extra:7"),0)
    for pool, population in [(0,0),(100,0),(100,101)]:
        call("SetPool",pool,population)
        check(f"invalid native pool/population {pool}/{population}",call("ValidateBridge","ordinary","boss_fat:6"),0)
    call("SetPool",100,100)
    check("511-byte ordinary CSV accepted by byte guard",call("ValidateBridge","x"*511,"boss_fat:6"),1)
    check("512-byte ordinary CSV rejected",call("ValidateBridge","x"*512,"boss_fat:6"),0)
    tree=map_config(2);tree.children[2].children[1].children[0].value="other"
    boss_tree=profiles();boss_tree.children[0].children[1].children[1].value="Fat Boss"
    install(tree,boss_tree);check("ambiguous-name fixture parses definitions",call("Read"),1)
    check("one public name cannot map to different native keys",call("ValidateBridge","ordinary","boss_fat:1,boss_other:1"),0)
    boss_tree=profiles();boss_tree.children[0].children[1].children[0].value="boss_fat"
    install(tree,boss_tree);check("ambiguous-key fixture parses definitions",call("Read"),1)
    check("one native key cannot map to different public names",call("ValidateBridge","ordinary","boss_fat:2"),0)

    # Feed complete candidate map trees through the actual production readers.
    # The syntax parser only turns text into ordered nodes; it does not resolve
    # slots, profiles, required flags, counts or native capacity decisions.
    candidates=sorted(CANDIDATE_MAPS.glob("*/*.cfg"))
    check("seven actual candidate maps available",len(candidates),7)
    local_bosses=LOCAL_GAME/"addons/sourcemod/configs/HanZombieShelter/bosses.cfg"
    repo_bosses=ROOT/"addons/sourcemod/configs/HanZombieShelter/bosses.cfg"
    local_zombies=LOCAL_GAME/"addons/sourcemod/configs/HanZombieShelter/zombies.cfg"
    local_zombie_document=migration.parse(local_zombies.read_text(encoding="utf-8-sig"))
    local_zombie_root=next(iter(local_zombie_document.values()))
    for candidate in candidates:
        group=candidate.parent.name
        boss_path=local_bosses if group=="local" else repo_bosses
        map_tree,map_values=parsed_tree(candidate)
        boss_tree,boss_values=parsed_tree(boss_path)
        install(map_tree,boss_tree)
        label=group+"/"+candidate.name
        loaded=call("Read")
        check("actual candidate production parse "+label,loaded,1)
        enabled=[(identifier,row) for identifier,row in map_values["Bosses"].items() if row.get("enabled","1")=="1"]
        check("actual candidate definition count "+label,call("Count"),len(enabled))
        expected_points=map_values["BossSpawnPoints"]
        for ordinal,(identifier,row) in enumerate(enabled):
            point=expected_points[f"boss{ordinal%5+1}"]
            expected_origin=[ctypes.c_float(float(value)).value for value in point["origin"].split()]
            expected_angles=[ctypes.c_float(float(value)).value for value in point.get("angles","0 0 0").split()]
            check("actual candidate assigned slot "+label+"/"+identifier,call("SpawnPoint",ordinal),ordinal%5)
            check("actual candidate exact origin "+label+"/"+identifier,[float_cell(call("Origin",ordinal,axis)) for axis in range(3)],expected_origin)
            check("actual candidate exact angles "+label+"/"+identifier,[float_cell(call("Angle",ordinal,axis)) for axis in range(3)],expected_angles)
            check("actual candidate enabled Boss always required "+label+"/"+identifier,call("Required",ordinal),1)
            public_name=boss_values["BossTypes"][row["boss_profile"]]["public_name"]
            check("actual candidate UTF8 public-name claim "+label+"/"+identifier,call("Claim",public_name),ordinal)
        record={
            "candidate":str(candidate.relative_to(ROOT)),"candidate_sha256":hashlib.sha256(candidate.read_bytes()).hexdigest(),
            "boss_profiles":str(boss_path),"boss_profiles_sha256":hashlib.sha256(boss_path.read_bytes()).hexdigest(),
            "enabled_boss_count":len(enabled),"production_parse_passed":loaded==1,
            "legacy_required_boss_ids_ignored":True,
            "world_coordinate_safety_claimed":False,
        }
        if group=="local":
            stage_path=LOCAL_GAME/"addons/sourcemod/configs/HanZombieScenario"/candidate.stem/"HanZombieScenarioStage.cfg"
            stage_document=migration.parse(stage_path.read_text(encoding="utf-8-sig"))
            stage_root=next(iter(stage_document.values()))
            stage_values=stage_root.get("Shelter",next(iter(stage_root.values())))
            pool=int(local_zombie_root["NativePool"]["initial_ordinary_count"])
            population=int(local_zombie_root["Population"]["raider_alive_limit"])
            call("SetPool",pool,population)
            bridge=call("ValidateBridge",stage_values["zombie"],stage_values["customzombies"])
            check("actual local Stage and pool production bridge "+label,bridge,1)
            record.update({"stage":str(stage_path),"stage_sha256":hashlib.sha256(stage_path.read_bytes()).hexdigest(),
                "zombie_config_sha256":hashlib.sha256(local_zombies.read_bytes()).hexdigest(),
                "native_pool":pool,"population_limit":population,"production_stage_bridge_passed":bridge==1})
        candidate_checks.append(record)

    install(map_config(512));check("tracking-capacity 512 definitions accepted by map parser",call("Read"),1)
    check("512 definitions retained without old eight-entry cap",call("Count"),512)
    check("512th definition maps to second point",call("SpawnPoint",511),1)
    install(map_config(513));check("513 definitions rejected before tracking overflow",call("Read"),0)
    check("overflow never writes beyond512 definitions",call("Count"),512)
    failed=sum(not check["passed"] for check in checks)
    return {
        "generated_at":datetime.now(timezone(timedelta(hours=8))).isoformat(),
        "passed":failed==0,"checks_passed":len(checks)-failed,"checks_failed":failed,
        "source_mode":"whole production BossSpawns.inc/BossBridge.inc includes plus exact Config helpers, BossDef declaration and production AllBossesDefeated",
        "required_semantics":"every enabled configured Boss belongs to victory; legacy required_boss_ids is ignored and all configured Bosses must be defeated",
        "report_directory":str(REPORT),
        "candidate_input_directory":str(CANDIDATE_MAPS),
        "production_source_sha256":copied,
        "actual_candidate_config_checks":candidate_checks,
        "fixture_smx_sha256":hashlib.sha256(smx_path.read_bytes()).hexdigest(),
        "keyvalues_emulation":"ordered nodes retain duplicate/scalar/section distinctions; actual production functions make validation decisions",
        "vm_compatibility_adapters":["inherited bounded SMX/Windows debug metadata and UTF8 adapters","KeyValues cursor operations and native GetString/GetNum/GetFloat","StringToFloatEx prefix-conversion native","Format and float comparison natives","heap-save updates the VM heap-scope chain"],
        "world_coordinates_safety_or_route_acceptance_claimed":False,
        "game_processes_started":False,"game_commands_sent":False,
        "not_tested":["Source engine entity hulls or actual world traces","world coordinate safety or traversable routes","actual HZS NPC registration/spawning","real Boss combat/lifecycle/multiplayer load"],
        "checks":checks,
    }


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report-dir",type=Path,default=DEFAULT_REPORT,help="Output report/VM directory; fixed historical candidate inputs are read separately")
    parser.add_argument("--verify-source-snapshot",action="store_true",help="Compare current production sources with the report in --report-dir without rewriting it")
    args=parser.parse_args()
    REPORT=args.report_dir.resolve()
    BUILD=REPORT/"vm"
    compat.BUILD=BUILD
    if args.verify_source_snapshot:
        recorded=json.loads((REPORT/"boss_spawnpoints_vm_verification.json").read_text(encoding="utf-8"))
        _,current=production_source()
        matches=current==recorded["production_source_sha256"]
        print(json.dumps({"production_source_snapshot_matches":matches,"production_blocks_checked":len(current)},ensure_ascii=False))
        raise SystemExit(0 if matches else 1)
    result=run()
    output=REPORT/"boss_spawnpoints_vm_verification.json"
    if output.exists():
        previous=output.read_bytes()
        history=REPORT/"history"/("boss_spawnpoints_vm_verification_"+hashlib.sha256(previous).hexdigest()+".json")
        history.parent.mkdir(parents=True,exist_ok=True)
        if not history.exists():
            history.write_bytes(previous)
    compatibility_history={
        "tool":"pysmx 0.4.0 offline Python SourcePawn VM",
        "production_or_game_crash_claimed":False,
        "initial_tool_failures_preserved":[
            {"stage":"first production KV smoke","failure":"unbound __FLOAT_GT__/__FLOAT_LT__ compiler float comparison natives","resolution":"test-only standard float comparison native adapters"},
            {"stage":"first Bridge multidimensional local array execution","failure":"Windows Python process access violation in _leave_heap_scope/_getheapcell","observed_scope":"pysmx _enter_heap_scope wrote the previous scope link but did not publish the new scope pointer","resolution":"test-only heap-save scope chain adapter; subsequent complete suite executed successfully"}
        ],
        "game_processes_started":False,
        "game_commands_sent":False,
    }
    (REPORT/"tool_compatibility_history.json").write_text(json.dumps(compatibility_history,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({key:result[key] for key in ["passed","checks_passed","checks_failed"]},ensure_ascii=False))
    if not result["passed"]:
        print(json.dumps([check for check in result["checks"] if not check["passed"]],ensure_ascii=False,indent=2))
        raise SystemExit(1)
