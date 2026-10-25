"""Execute compiled production crafting functions in an offline SourcePawn VM.

This harness never starts a game process. Engine state is supplied by explicit
SourcePawn test doubles; the settlement and menu validation functions are taken
from the production sources, rather than reimplemented in Python.
"""
from __future__ import annotations

import io
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "tests/reports/v082_crafting_20261025"
DEPS = REPORT / "python_deps"
sys.path.insert(0, str(DEPS))
import smx.plugin
from smx.plugin import SourcePawnPlugin
from smx.sourcemod.natives import SourceModNatives
from smx.sourcemod.natives.base import native
from smx.sourcemod.natives.base import WritableString
from smx.sourcemod.natives.base import MethodMap
from smx.rtti import RTTIParser
from smx.definitions import RTTIControlByte
from smx.runtime import PluginFunction
from smx.interfaces import ParamStringFlag

_original_decode_new = RTTIParser.decode_new


def _diagnostic_decode_new(self):
    offset = self.offset
    by_ref = self._match(RTTIControlByte.BY_REF)
    try:
        result = _original_decode_new(self)
        if by_ref:
            # Current compiler emits this marker on debug locals as well as
            # function parameters; pysmx handles only the latter out of box.
            result.is_by_ref = True
        return result
    except AssertionError as error:
        raise RuntimeError(f"Unsupported pysmx RTTI at offset={offset}: {self.data[offset:offset+12].hex()}") from error


RTTIParser.decode_new = _diagnostic_decode_new


def _push_utf8_string(self, value):
    if isinstance(value, bytes):
        return self.push_string_ex(value, len(value) + 1, ParamStringFlag.COPY | ParamStringFlag.BINARY, 0)
    return self.push_string_ex(value, len(value.encode("utf-8")) + 1, ParamStringFlag.COPY | ParamStringFlag.UTF8, 0)


PluginFunction.push_string = _push_utf8_string


def _bounded_stringtable(base: bytes, stringbase: int, size: int) -> dict[int, str]:
    """pysmx 0.4.0's <= boundary reads bytes from the following SMX section."""
    raw = base[stringbase:stringbase + size]
    result: dict[int, str] = {}
    offset = 0
    while offset < len(raw):
        end = raw.find(b"\0", offset)
        if end < 0:
            raise ValueError("Unterminated SMX name-table string")
        try:
            result[offset] = raw[offset:end].decode("utf-8")
        except UnicodeDecodeError:
            # The supplied Windows compiler emits its Chinese include paths
            # using the system codepage in debug metadata. Executed string
            # constants remain untouched in the SMX data section.
            result[offset] = raw[offset:end].decode("cp936")
        offset = end + 1
    return result


smx.plugin.extract_stringtable = _bounded_stringtable

COMPILER = Path(r"D:\css\sp\sourcemod-1.12.0hzs套件\addons\sourcemod\scripting\spcomp64.exe")
INCLUDE = COMPILER.parent / "include"
BUILD = REPORT / "vm"
PRODUCTION = ROOT / "addons/sourcemod/scripting/HanZombieShelter"


class InertHandleMethods(MethodMap):
    @native("cell")
    def Close(self, handle: int) -> None:
        pass


class CraftingVMNatives(SourceModNatives):
    Handle = InertHandleMethods()

    def get_native(self, name):
        if name == "Handle.~Handle":
            return self.CloseHandle
        return super().get_native(name)
    @native("string")
    def strlen(self, value: str) -> int:
        # Match SourceMod byte lengths, rather than Python Unicode lengths.
        return len(value.encode("utf-8"))

    @native("cell")
    def TrimString(self, offset: int) -> int:
        # Upstream WritableString.read addresses the immutable file image;
        # native strings actually reside in the VM's current linear memory.
        original = self.amx._getheapstring(offset)
        trimmed = original.strip(" \t\n\r\v\f")
        WritableString(self.amx, offset, len(original.encode("utf-8")) + 1).write(trimmed, null_terminate=True)
        return len(trimmed.encode("utf-8"))

    @native("string", "string", "writable_string")
    def SplitString(self, source: str, split: str, destination) -> int:
        index = source.find(split)
        if index < 0:
            return -1
        part = source[:index].encode("utf-8")[:max(0, destination.max_length - 1)]
        destination.write(part, null_terminate=True)
        return len((source[:index] + split).encode("utf-8"))

    @native("cell")
    def CloseHandle(self, handle: int) -> None:
        # Menu callback identity tests use inert, distinct handle numbers.
        # No VGUI or SourceMod menu backend exists in this offline VM.
        pass


def compile_source(name: str, source: str) -> Path:
    BUILD.mkdir(parents=True, exist_ok=True)
    source_path = BUILD / f"{name}.sp"
    smx_path = BUILD / f"{name}.smx"
    source_path.write_text(source, encoding="utf-8")
    result = subprocess.run(
        [str(COMPILER), str(source_path), "-i" + str(INCLUDE), "-o" + str(smx_path)],
        text=True, capture_output=True, encoding="utf-8", errors="replace", check=False,
    )
    (BUILD / f"{name}_compile.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return smx_path


def smoke() -> int:
    smx_path = compile_source(
        "smoke", "#pragma newdecls required\npublic int VM_Smoke(int value) { return value + 1; }\n"
    )
    plugin = SourcePawnPlugin(io.BytesIO(smx_path.read_bytes()))
    return plugin.runtime.call_function_by_name("VM_Smoke", 41)


def extract_block(path: Path, marker: str) -> str:
    """Copy a whole production declaration/function without rewriting its body."""
    text = path.read_text(encoding="utf-8")
    start = text.index(marker)
    start = text.rfind("\n", 0, start) + 1
    opening = text.index("{", start)
    depth = 0
    state = "code"
    index = opening
    while index < len(text):
        char = text[index]
        following = text[index:index + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if following == "*/":
                state = "code"
                index += 1
        elif state in ("string", "character"):
            if char == "\\":
                index += 1
            elif char == ('"' if state == "string" else "'"):
                state = "code"
        elif following == "//":
            state = "line"
            index += 1
        elif following == "/*":
            state = "block"
            index += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "character"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1] + "\n"
        index += 1
    raise ValueError(f"Unterminated production block: {path}:{marker}")


PRELUDE = r'''
#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#undef MAXPLAYERS
#define MAXPLAYERS 4
#define MaxClients VM_MaxClients
#define IsClientInGame VM_IsClientInGame
#define IsFakeClient VM_IsFakeClient
#define IsClientSourceTV VM_IsClientSourceTV
#define IsClientReplay VM_IsClientReplay
#define GetClientSerial VM_GetClientSerial
#define GetClientFromSerial VM_GetClientFromSerial
#define CommandExists VM_CommandExists
#define FakeClientCommand VM_FakeClientCommand
#define MATERIALS 64
#define MAX_BUILDINGS 8
#define LEVEL_CELL_LIMIT 2000000000
enum BuildingKind { Kind_Base, Kind_Workbench }
enum ConfigFile { C_Workbenches, C_WeaponCrafting, C_MaterialCrafting, C_Count }
enum struct VMBuilding
{
    bool used;
    bool alive;
    bool constructing;
    int hp;
    BuildingKind kind;
    ConfigFile file;
    int configRevision;
    int ref;
    int buildCore;
    int level;
    int uid;
    int generation;
    char type[64];
}
enum struct VMBuildRecipe
{
    BuildingKind kind;
    ConfigFile file;
    char type[64];
}
enum struct VMMaterial
{
    char label[64];
}
VMBuilding gBuildings[MAX_BUILDINGS];
VMBuildRecipe gRecipes[8];
VMMaterial gMaterials[MATERIALS];
KeyValues gCfg[C_Count];
int gRecipeCount,gMaterialCount,gInventoryCap,gEpoch,gConfigRevision;
int gPlayerInventory[MAXPLAYERS+1][MATERIALS];
int VM_MaxClients=4;
bool vmRunning,vmConnected,vmAlive,vmFake,vmTV,vmReplay,vmBase,vmAimed,vmPowered,vmCommand;
int vmSerial,vmDispatchCount,vmDispatchMode,vmReentrantResult,vmTellCount;
char vmDispatched[128];

bool VM_IsClientInGame(int client) { return client==1 && vmConnected; }
bool VM_IsFakeClient(int client) { return client==1 && vmFake; }
bool VM_IsClientSourceTV(int client) { return client==1 && vmTV; }
bool VM_IsClientReplay(int client) { return client==1 && vmReplay; }
int VM_GetClientSerial(int client) { return client==1 && vmConnected ? vmSerial : 0; }
int VM_GetClientFromSerial(int serial) { return vmConnected && serial==vmSerial ? 1 : 0; }
bool VM_CommandExists(const char[] command) { return vmCommand && StrEqual(command,"sm_fixture_weapon"); }
bool Running() { return vmRunning; }
bool LiveHuman(int client) { return client==1 && vmConnected && vmAlive; }
int Resolve(int ref) { return ref==gBuildings[0].ref ? 81 : -1; }
bool BuildCoreAlive(int core) { return core==0 && vmBase; }
bool BuildingInUseRange(int client,int building) { return client==1 && building==0 && vmAimed; }
bool BuildingPowered(int building) { return building==0 && vmPowered; }
bool Node(ConfigFile file,const char[] path) { return file==C_Count && path[0]==0; }
int FindMaterial(const char[] name)
{
    static const char names[][]={"wood","iron","copper","silver","gold","plague_core","credits"};
    for(int material=0;material<sizeof(names);material++)if(StrEqual(name,names[material]))return material;
    return -1;
}
int ProfileMaximumLevel(ConfigFile file,const char[] path) { return file==C_Workbenches && path[0] ? 10 : 0; }
void Tell(int client,const char[] message,any ...) { if(client==1 && message[0])vmTellCount++; }
void TranslateLabel(int client,const char[] label,char[] output,int length) { if(client==1)strcopy(output,length,label); }
void VM_FakeClientCommand(int client,const char[] format,const char[] command)
{
    if(client!=1 || !StrEqual(format,"%s"))return;
    vmDispatchCount++;strcopy(vmDispatched,sizeof(vmDispatched),command);
    if(vmDispatchMode==1)vmReentrantResult=TryCraftWeapon(client,0,0) ? 1 : 0;
    if(vmDispatchMode==2)
    {
        vmSerial++;
        BeginCraftingTransaction(client,vmSerial);
    }
}
'''


WRAPPERS = r'''
void ShowCraftingMenu(int client,int building) { if(client+building<0)vmTellCount++; }
void ShowWeaponCraftingMenu(int client,int building) { if(client+building<0)vmTellCount++; }
void ShowMaterialCraftingMenu(int client,int building) { if(client+building<0)vmTellCount++; }
void ShowWeaponCraftingDetails(int client,int building,int recipe) { if(client+building+recipe<0)vmTellCount++; }
void ShowMaterialCraftingDetails(int client,int building,int recipe) { if(client+building+recipe<0)vmTellCount++; }
void ShowDefenseMenu(int client,int building) { if(client+building<0)vmTellCount++; }

public int VM_Reset()
{
    vmRunning=true;vmConnected=true;vmAlive=true;vmFake=false;vmTV=false;vmReplay=false;
    vmBase=true;vmAimed=true;vmPowered=true;vmCommand=true;vmSerial=501;
    vmDispatchCount=0;vmDispatchMode=0;vmReentrantResult=-1;vmTellCount=0;vmDispatched[0]=0;
    gMaterialCount=7;gInventoryCap=1000;gEpoch=31;gConfigRevision=17;
    for(int client=0;client<=MAXPLAYERS;client++)
    {
        for(int material=0;material<MATERIALS;material++)gPlayerInventory[client][material]=0;
        gCraftingTransaction[client]=false;gCraftingTransactionSerial[client]=0;
        gCraftingMenuHandle[client]=null;gCraftingMenuNonce[client]=7;
    }
    for(int b=0;b<MAX_BUILDINGS;b++)gBuildings[b].used=false;
    gBuildings[0].used=true;gBuildings[0].alive=true;gBuildings[0].constructing=false;gBuildings[0].hp=500;
    gBuildings[0].kind=Kind_Workbench;gBuildings[0].file=C_Workbenches;gBuildings[0].configRevision=17;
    gBuildings[0].ref=(-2147483647-1);gBuildings[0].buildCore=0;gBuildings[0].level=3;
    gBuildings[0].uid=71;gBuildings[0].generation=4;strcopy(gBuildings[0].type,sizeof(gBuildings[].type),"初级合成台");
    gCraftStationCount=2;strcopy(gCraftStationNames[0],sizeof(gCraftStationNames[]),"初级合成台");
    strcopy(gCraftStationNames[1],sizeof(gCraftStationNames[]),"高级合成台");
    gCraftStationMaximum[0]=10;gCraftStationMaximum[1]=10;
    gRecipeCount=1;gRecipes[0].kind=Kind_Workbench;gRecipes[0].file=C_Workbenches;
    strcopy(gRecipes[0].type,sizeof(gRecipes[].type),"初级合成台");
    gWeaponCraftCount=2;gMaterialCraftCount=3;
    for(int recipe=0;recipe<2;recipe++)
    {
        for(int material=0;material<MATERIALS;material++)gWeaponCrafts[recipe].cost[material]=0;
        strcopy(gWeaponCrafts[recipe].command,sizeof(gWeaponCrafts[].command),"sm_fixture_weapon");
        strcopy(gWeaponCrafts[recipe].label,sizeof(gWeaponCrafts[].label),"fixture weapon");
        strcopy(gWeaponCrafts[recipe].station,sizeof(gWeaponCrafts[].station),recipe==0 ? "初级合成台" : "高级合成台");
        gWeaponCrafts[recipe].requiredLevel=recipe==0 ? 3 : 2;
        gWeaponCrafts[recipe].cost[4]=5;gWeaponCrafts[recipe].cost[5]=1;gWeaponCrafts[recipe].cost[6]=3;
    }
    for(int recipe=0;recipe<3;recipe++)
    {
        for(int material=0;material<MATERIALS;material++)gMaterialCrafts[recipe].cost[material]=0;
        strcopy(gMaterialCrafts[recipe].label,sizeof(gMaterialCrafts[].label),"fixture material");
    }
    gMaterialCrafts[0].cost[0]=100;gMaterialCrafts[0].outputMaterial=1;gMaterialCrafts[0].outputAmount=1;
    gMaterialCrafts[1].cost[0]=100;gMaterialCrafts[1].cost[1]=30;gMaterialCrafts[1].cost[4]=5;
    gMaterialCrafts[1].cost[6]=3;gMaterialCrafts[1].outputMaterial=5;gMaterialCrafts[1].outputAmount=1;
    gMaterialCrafts[2].cost[0]=100;gMaterialCrafts[2].outputMaterial=0;gMaterialCrafts[2].outputAmount=1;
    return 1;
}
public int VM_SetInventory(int material,int amount) { gPlayerInventory[1][material]=amount;return amount; }
public int VM_Inventory(int material) { return gPlayerInventory[1][material]; }
public int VM_SetCap(int amount) { gInventoryCap=amount;return amount; }
public int VM_SetStation(int type,int level)
{
    strcopy(gBuildings[0].type,sizeof(gBuildings[].type),type==0 ? "初级合成台" : "高级合成台");
    gBuildings[0].level=level;return 1;
}
public int VM_SetCondition(int condition,int value)
{
    switch(condition)
    {
        case 0:vmRunning=value!=0;
        case 1:vmConnected=value!=0;
        case 2:vmAlive=value!=0;
        case 3:vmFake=value!=0;
        case 4:vmBase=value!=0;
        case 5:vmAimed=value!=0;
        case 6:vmPowered=value!=0;
        case 7:vmCommand=value!=0;
        case 8:gBuildings[0].constructing=value!=0;
        case 9:gBuildings[0].alive=value!=0;
        case 10:gBuildings[0].configRevision=value;
        case 11:vmSerial=value;
        case 12:gBuildings[0].ref=value;
        case 13:vmDispatchMode=value;
        case 14:vmTV=value!=0;
        case 15:vmReplay=value!=0;
        case 16:gBuildings[0].kind=view_as<BuildingKind>(value);
    }
    return 1;
}
public int VM_SetMaterialRecipe(int recipe,int material,int amount)
{
    gMaterialCrafts[recipe].outputMaterial=material;gMaterialCrafts[recipe].outputAmount=amount;return 1;
}
public int VM_SetCost(int recipe,int material,int amount) { gMaterialCrafts[recipe].cost[material]=amount;return 1; }
public int VM_CraftMaterial(int recipe) { return TryCraftMaterial(1,0,recipe) ? 1 : 0; }
public int VM_CraftWeapon(int recipe) { return TryCraftWeapon(1,0,recipe) ? 1 : 0; }
public int VM_Dispatches() { return vmDispatchCount; }
public int VM_DispatchCorrect() { return StrEqual(vmDispatched,"sm_fixture_weapon") ? 1 : 0; }
public int VM_ReentrantResult() { return vmReentrantResult; }
public int VM_Busy() { return CraftingTransactionBusy(1) ? 1 : 0; }
public int VM_LockSerial() { return gCraftingTransactionSerial[1]; }
public int VM_BeginLock(int serial) { BeginCraftingTransaction(1,serial);return 1; }
public int VM_EndLock(int serial) { EndCraftingTransaction(1,serial);return 1; }
public int VM_Allowed(int recipe) { return WeaponCraftAllowed(0,recipe) ? 1 : 0; }
public int VM_HasRoom(int recipe) { return MaterialCraftHasRoom(1,recipe) ? 1 : 0; }
public int VM_ValidateCommand(const char[] command) { return CraftingCommandValid(command) ? 1 : 0; }
public int VM_ValidateSection(const char[] name) { return CraftingValidSectionName(name) ? 1 : 0; }
public int VM_MenuHandle() { return view_as<int>(gCraftingMenuHandle[1]); }
public int VM_MenuNonce() { return gCraftingMenuNonce[1]; }
public int VM_SetMenuHandle(int handle) { gCraftingMenuHandle[1]=view_as<Menu>(handle);return 1; }
public int VM_ValidateMenu(const char[] token)
{
    int building,recipe;CraftingMenuOperation operation;
    return ValidateCraftingMenu(1,view_as<Menu>(101),token,building,operation,recipe) ? 1 : 0;
}
public int VM_CancelMenu(int handle) { return CraftingMenuHandler(view_as<Menu>(handle),MenuAction_Cancel,1,0); }
public int VM_EndMenu(int handle) { return CraftingMenuHandler(view_as<Menu>(handle),MenuAction_End,0,0); }
public int VM_ResetMenus() { ResetAllCraftingMenus();return 1; }
public int VM_ParseRef(const char[] text)
{
    int value;if(!ParseDefenseEntityRefToken(text,value))return 123;
    return value;
}
public int VM_AnchorOptionalConfig() { return LoadCraftingConfig() ? 1 : 0; }
'''


def build_fixture() -> tuple[str, dict[str, str]]:
    copied: dict[str, str] = {}
    parts = [PRELUDE]
    selections = [
        ("Levels.inc", "bool ParseLevelInteger("),
        ("Construction.inc", "bool BuildingOperational("),
        ("Inventory.inc", "bool Spend("),
    ]
    for filename, marker in selections:
        block = extract_block(PRODUCTION / filename, marker)
        parts.append(block)
        copied[f"{filename}:{marker}"] = hashlib.sha256(block.encode("utf-8")).hexdigest()
    parts.append('#include "' + (PRODUCTION / "Crafting.inc").as_posix() + '"\n')
    parts.append("Menu gCraftingMenuHandle[MAXPLAYERS+1];\nint gCraftingMenuNonce[MAXPLAYERS+1];\n")
    for filename, marker in [
        ("CraftingMenu.inc", "enum CraftingMenuOperation"),
        ("CraftingMenu.inc", "void ResetCraftingMenu("),
        ("CraftingMenu.inc", "void ResetAllCraftingMenus("),
        ("Commands.inc", "bool ParseDefenseEntityRefToken("),
        ("CraftingMenu.inc", "bool ValidateCraftingMenu("),
        ("CraftingMenu.inc", "public int CraftingMenuHandler("),
    ]:
        block = extract_block(PRODUCTION / filename, marker)
        parts.append(block)
        copied[f"{filename}:{marker}"] = hashlib.sha256(block.encode("utf-8")).hexdigest()
    parts.append(WRAPPERS)
    copied["Crafting.inc:whole_include"] = hashlib.sha256((PRODUCTION / "Crafting.inc").read_bytes()).hexdigest()
    return "\n".join(parts), copied


def run_checks() -> dict:
    source, copied = build_fixture()
    smx_path = compile_source("crafting_vm", source)
    plugin = SourcePawnPlugin(io.BytesIO(smx_path.read_bytes()), smsys_options={"natives_cls": CraftingVMNatives})
    vm = plugin.runtime
    vm.console_redirect = None
    checks: list[dict] = []

    def call(name: str, *args):
        try:
            return vm.call_function_by_name("VM_" + name, *args)
        except Exception:
            missing = [item.name for item in plugin.natives if vm.amx.sm_natives.get_native(item.name) is None]
            print("unbound-native-diagnostic:", missing)
            raise

    def check(name: str, actual, expected):
        checks.append({"name": name, "passed": actual == expected, "actual": actual, "expected": expected})

    def reset():
        call("Reset")

    def inventory(*values):
        for material, amount in enumerate(values):
            call("SetInventory", material, amount)

    def balances():
        return [call("Inventory", material) for material in range(7)]

    reset();inventory(200,10)
    check("100 wood to 1 iron commits", call("CraftMaterial",0),1)
    check("wood deducted and full iron credited", balances(),[100,11,0,0,0,0,0])
    for station, label in [(0,"basic"),(1,"advanced")]:
        reset();inventory(200);call("SetStation",station,1)
        check(label+" level1 station shares material recipes",call("CraftMaterial",0),1)
        check(label+" level1 material settlement exact",balances(),[100,1,0,0,0,0,0])
    reset();inventory(200,29,0,0,10,0,5);before=balances()
    check("multi-input missing iron rejected",call("CraftMaterial",1),0)
    check("failed multi-input leaves every material unchanged",balances(),before)
    reset();inventory(200,30,0,0,10,0,3)
    check("multi-input plague-core synthesis commits",call("CraftMaterial",1),1)
    check("multi-input settlement exact",balances(),[100,0,0,0,5,1,0])
    reset();inventory(200,1000);before=balances()
    check("full output stack rejected",call("CraftMaterial",0),0)
    check("full stack does not consume wood",balances(),before)
    reset();inventory(200,999)
    check("exact remaining output space accepted",call("CraftMaterial",0),1)
    check("exact output capacity reached",call("Inventory",1),1000)
    reset();inventory(1000)
    check("same-material input frees capacity",call("CraftMaterial",2),1)
    check("same-material net amount exact",call("Inventory",0),901)
    reset();inventory(950);call("SetCost",2,0,1);call("SetMaterialRecipe",2,0,100)
    check("same-material net overflow rejected",call("CraftMaterial",2),0)
    check("same-material overflow zero loss",call("Inventory",0),950)
    reset();inventory(900);call("SetCost",2,0,1);call("SetMaterialRecipe",2,0,100)
    check("same-material positive net gain fits",call("CraftMaterial",2),1)
    check("same-material gain credited in full",call("Inventory",0),999)
    reset();inventory(200,1);call("SetCap",0);call("SetMaterialRecipe",0,1,2000000000);before=balances()
    check("integer-limit overflow rejected before payment",call("CraftMaterial",0),0)
    check("integer-limit overflow no mutation",balances(),before)
    reset();inventory(200);call("SetCap",0);call("SetMaterialRecipe",0,1,2000000000)
    check("exact integer-limit output accepted",call("CraftMaterial",0),1)
    check("integer-limit output not truncated",call("Inventory",1),2000000000)
    for label, recipe_material, amount in [("unknown output",64,1),("zero output",1,0),("negative output",1,-1)]:
        reset();inventory(200);call("SetMaterialRecipe",0,recipe_material,amount);before=balances()
        check(label+" rejected",call("CraftMaterial",0),0)
        check(label+" no mutation",balances(),before)
    reset();inventory(200);call("SetCost",0,0,-1);before=balances()
    check("negative input rejected",call("CraftMaterial",0),0)
    check("negative input no mutation",balances(),before)
    for label, condition, value in [
        ("campaign stopped",0,0),("disconnected",1,0),("dead player",2,0),("fake player",3,1),
        ("base destroyed",4,0),("aim moved away",5,0),("power unavailable",6,0),
        ("blueprint unfinished",8,1),("station destroyed",9,0),("config revision changed",10,18),
        ("SourceTV client",14,1),("replay client",15,1),("wrong building kind",16,0),
    ]:
        reset();inventory(200);call("SetCondition",condition,value);before=balances()
        check(label+" blocks crafting",call("CraftMaterial",0),0)
        check(label+" no mutation",balances(),before)
    reset();inventory(0,0,0,0,10,2,6);call("SetStation",0,2);before=balances()
    check("weapon minimum level enforced",call("CraftWeapon",0),0)
    check("underlevel weapon no charge",balances(),before)
    check("underlevel weapon no dispatch",call("Dispatches"),0)
    reset();inventory(0,0,0,0,10,2,6)
    check("weapon exact required level accepted",call("CraftWeapon",0),1)
    check("weapon charge exact",balances(),[0,0,0,0,5,1,3])
    check("weapon dispatched once",call("Dispatches"),1)
    check("weapon command dispatched unchanged",call("DispatchCorrect"),1)
    reset();inventory(0,0,0,0,10,2,6);call("SetStation",0,10)
    check("higher level retains same-type weapon recipe",call("CraftWeapon",0),1)
    reset();inventory(0,0,0,0,10,2,6);call("SetStation",1,10);before=balances()
    check("advanced station cannot synthesize basic-only recipe",call("CraftWeapon",0),0)
    check("type mismatch no charge",balances(),before)
    call("SetStation",1,2)
    check("advanced-only level2 recipe accepted",call("CraftWeapon",1),1)
    reset();inventory(0,0,0,0,10,2,6);call("SetCondition",7,0);before=balances()
    check("missing external command blocks weapon",call("CraftWeapon",0),0)
    check("missing command no payment",balances(),before)
    check("missing command no dispatch",call("Dispatches"),0)
    reset();inventory(0,0,0,0,10,2,6);call("SetCondition",13,1)
    check("outer weapon transaction accepted",call("CraftWeapon",0),1)
    check("reentrant weapon attempt rejected",call("ReentrantResult"),0)
    check("reentrant path dispatches only once",call("Dispatches"),1)
    check("reentrant path charges only once",balances(),[0,0,0,0,5,1,3])
    check("outer lock released",call("Busy"),0)
    reset();inventory(0,0,0,0,10,2,6);call("SetCondition",13,2)
    check("dispatch may change caller serial",call("CraftWeapon",0),1)
    check("old transaction leaves replacement lock intact",call("LockSerial"),502)
    check("replacement serial remains locked",call("Busy"),1)
    call("EndLock",501)
    check("explicit old serial cannot unlock replacement",call("Busy"),1)
    call("EndLock",502)
    check("matching replacement serial can unlock",call("Busy"),0)
    reset();inventory(0,0,0,0,10,2,6);call("BeginLock",501);before=balances()
    check("same-serial busy lock rejects transaction",call("CraftWeapon",0),0)
    check("busy transaction no payment",balances(),before)
    for command, expected in [("sm_axe",1),("sm_HGjb6l3RTsMI2ePR",1),("",0),("sm_axe;quit",0),("sm_axe argument",0),("sm_axe\nquit",0),("x"*128,0)]:
        check("command validation "+repr(command),call("ValidateCommand",command),expected)
    for name, expected in [("合成台",1),("高级武器合成台",1),("",0),(" bad",0),("bad ",0),("bad/name",0),("bad:name",0),("台"*22,0)]:
        check("station-name validation "+repr(name),call("ValidateSection",name),expected)

    token=[7,501,31,17,0,71,4,-2147483648,3,0,-1]
    def encoded(fields):
        return ":".join(str(value) for value in fields)
    reset();call("SetMenuHandle",101)
    check("valid menu with INT_MIN EntRef accepted",call("ValidateMenu",encoded(token)),1)
    check("valid menu quote consumed",call("MenuHandle"),0)
    check("valid menu nonce advanced",call("MenuNonce"),8)
    check("duplicate selection rejected",call("ValidateMenu",encoded(token)),0)
    for label, field, value in [
        ("nonce",0,6),("client serial",1,502),("map epoch",2,32),("config revision",3,18),
        ("building index",4,8),("building uid",5,72),("entity generation",6,5),("entity ref",7,-2104229686),
        ("station level",8,4),("operation range",9,8),("navigation recipe",10,0),
    ]:
        reset();call("SetMenuHandle",101);changed=token.copy();changed[field]=value
        check("stale/invalid menu "+label+" rejected",call("ValidateMenu",encoded(changed)),0)
        check("invalid menu "+label+" cannot consume quote",call("MenuNonce"),7)
    reset();call("SetMenuHandle",101);call("SetCondition",12,-2104229686);changed=token.copy();changed[7]=-2104229686
    check("real-world negative EntRef accepted",call("ValidateMenu",encoded(changed)),1)
    for malformed in ["",encoded(token)+":0",encoded(token[:-1]),encoded(token).replace("501","not-a-serial"),encoded(token).replace("-2147483648","-2147483649")]:
        reset();call("SetMenuHandle",101)
        check("malformed menu token "+repr(malformed),call("ValidateMenu",malformed),0)
    reset();call("SetMenuHandle",101);changed=token.copy();changed[9]=3;changed[10]=1
    check("weapon menu cannot select other station recipe",call("ValidateMenu",encoded(changed)),0)
    reset();call("SetMenuHandle",202)
    call("CancelMenu",101)
    check("old Cancel preserves replacement handle",call("MenuHandle"),202)
    check("old Cancel preserves replacement nonce",call("MenuNonce"),7)
    call("EndMenu",101)
    check("old End preserves replacement handle",call("MenuHandle"),202)
    check("old End preserves replacement nonce",call("MenuNonce"),7)
    call("CancelMenu",202)
    check("current Cancel clears quote",call("MenuHandle"),0)
    check("current Cancel advances nonce",call("MenuNonce"),8)
    reset();call("SetMenuHandle",101);call("EndMenu",101)
    check("current End clears quote",call("MenuHandle"),0)
    check("current End advances nonce",call("MenuNonce"),8)
    reset();call("SetMenuHandle",101);call("ResetMenus")
    check("lifecycle reset clears current quote",call("MenuHandle"),0)
    for value in ["-2147483648","-2104229686","2147483647"]:
        check("production full-cell EntRef parser "+value,call("ParseRef",value),int(value))
    for value in ["2147483648","-2147483649","1x"]:
        check("production EntRef parser rejects "+value,call("ParseRef",value),123)
    failed=sum(not item["passed"] for item in checks)
    return {
        "passed": failed==0,
        "checks_passed":len(checks)-failed,
        "checks_failed":failed,
        "engine_started":False,
        "source_mode":"whole production Crafting.inc include plus exact extracted production menu/ledger/parser functions",
        "copied_source_sha256":copied,
        "fixture_smx_sha256":hashlib.sha256(smx_path.read_bytes()).hexdigest(),
        "pysmx_compatibility":"bounded SMX names + CP936 debug path and by-ref debug RTTI decoding; UTF8 byte arguments; byte-length strlen, SplitString and inert menu-handle native adapters",
        "not_tested":["KeyValues configuration loading","actual game entities/raycast/power engine state","native VGUI rendering","external weapon delivery success","real multiplayer load"],
        "checks":checks,
    }


if __name__ == "__main__":
    smoke_result=smoke()
    if smoke_result!=42:
        raise RuntimeError(f"Offline SourcePawn smoke failed: {smoke_result}")
    result=run_checks()
    result["smoke_result"]=smoke_result
    (REPORT / "offline_vm_verification.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
    print(json.dumps({key:result[key] for key in ("passed","checks_passed","checks_failed","smoke_result")},ensure_ascii=False))
    if not result["passed"]:
        print(json.dumps([check for check in result["checks"] if not check["passed"]],ensure_ascii=False,indent=2))
        raise SystemExit(1)
