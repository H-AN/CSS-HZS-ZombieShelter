"""Offline differential execution of v0.8.2/v0.8.3 production functions.

No game process or live directory writes. Engine behavior uses explicit doubles;
these checks prove the selected function contracts, not engine performance.
"""
from pathlib import Path
import importlib.util
import io
import json
import hashlib
import random
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(r"D:\gmod昼夜求生资源\文档\制作计划\v0.8.3 优化计划\implementation_20261030")
OLD = WORK / "checkpoints/00_baseline/files/addons/sourcemod/scripting/HanZombieShelter"
NEW = ROOT / "addons/sourcemod/scripting/HanZombieShelter"
spec = importlib.util.spec_from_file_location("v083_vm_support", ROOT / "tests/test_v082_boss_damage_vm.py")
support = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = support
spec.loader.exec_module(support)
compat = support.support.compat
REPORT = WORK / "reports" / ("vm_" + time.strftime("%Y%m%d_%H%M%S"))
REPORT.mkdir(parents=True, exist_ok=False)
compat.BUILD = REPORT


def block(base, file, name):
    path = base / file
    declaration = re.search(r'^(?:stock )?(?:public )?[A-Za-z_][A-Za-z_0-9]* ' + re.escape(name) + r'\([^\n]*', path.read_text(encoding='utf-8-sig'), re.M)
    assert declaration, (file, name)
    return compat.extract_block(path, declaration.group())


def execute(name, source):
    path = compat.compile_source(name, source)
    log = (REPORT / (name + "_compile.txt")).read_text(encoding="utf-8")
    if "warning " in log.lower():
        raise AssertionError(log)
    plugin = support.support.SourcePawnPlugin(io.BytesIO(path.read_bytes()), smsys_options={"natives_cls": support.DamageNatives})
    vm = plugin.runtime
    return lambda function, *args: vm.call_function_by_name("VM_" + function, *args)


def lookup_checks():
    prelude = r'''
#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#define MaxClients 4
#define MAX_BUILDINGS 1024
#define MAX_NPCS 512
enum struct Record
{
    bool used;
    int ref;
}
Record gBuildings[MAX_BUILDINGS],gNPCs[MAX_NPCS],gNodes[80];
int gNodeCount=80,gWorldBinding[2049],gWorldBindingRef[2049],gNPCBinding[2049],gNPCBindingRef[2049];
bool gValid[2049];int gRef[2049];
#define IsValidEntity FixtureValid
#define EntIndexToEntRef FixtureRef
bool FixtureValid(int entity){return entity>=0 && entity<2049 && gValid[entity];}
int FixtureRef(int entity){return gRef[entity];}
public void VM_Reset(){
    Record empty;
    for(int i=0;i<1024;i++)gBuildings[i]=empty;
    for(int i=0;i<512;i++)gNPCs[i]=empty;
    for(int i=0;i<80;i++)gNodes[i]=empty;
    for(int e=0;e<2049;e++){gValid[e]=false;gRef[e]=0;gWorldBinding[e]=0;gNPCBinding[e]=0;gWorldBindingRef[e]=0;gNPCBindingRef[e]=0;}
}
public void VM_Put(int kind,int slot,int entity,int ref){
    gValid[entity]=true;gRef[entity]=ref;
    if(kind==0){gBuildings[slot].used=true;gBuildings[slot].ref=ref;gWorldBinding[entity]=slot+1;gWorldBindingRef[entity]=ref;}
    else if(kind==1){gNPCs[slot].used=true;gNPCs[slot].ref=ref;gNPCBinding[entity]=slot+1;gNPCBindingRef[entity]=ref;}
    else{gNodes[slot].used=true;gNodes[slot].ref=ref;gWorldBinding[entity]=-(slot+1);gWorldBindingRef[entity]=ref;}
}
public void VM_Reuse(int entity,int ref){gRef[entity]=ref;}
public void VM_Delete(int entity){gValid[entity]=false;}
public void VM_Unuse(int kind,int slot){if(kind==0)gBuildings[slot].used=false;else if(kind==1)gNPCs[slot].used=false;else gNodes[slot].used=false;}
'''
    functions = []
    for name in ["FindBuilding", "FindNPC", "FindNode"]:
        functions += [block(OLD, "Types.inc", name).replace(name + "(", "Old" + name + "("), block(NEW, "Types.inc", name)]
    wrappers = r'''
public int VM_Query(int old,int kind,int entity){
    if(kind==0)return old ? OldFindBuilding(entity) : FindBuilding(entity);
    if(kind==1)return old ? OldFindNPC(entity) : FindNPC(entity);
    return old ? OldFindNode(entity) : FindNode(entity);
}
'''
    call = execute("lookup", prelude + "\n".join(functions) + wrappers)
    call("Reset")
    rng = random.Random(8301)
    for kind, limit in [(0, 1024), (1, 512), (2, 80)]:
        call("Put", kind, limit - 1, 100 + kind, -2147483648 + kind)
    checks = 0
    for entity in [-1, 0, 1, 4, 5, 100, 101, 102, 2048, 2049, 3000]:
        for kind in range(3):
            assert call("Query", 0, kind, entity) == call("Query", 1, kind, entity)
            checks += 1
    for i in range(240):
        kind = i % 3
        slot = rng.randrange([1023, 511, 79][kind])
        entity = 200 + i
        ref = 400000 + i
        call("Put", kind, slot, entity, ref)
        for query_kind in range(3):
            assert call("Query", 0, query_kind, entity) == call("Query", 1, query_kind, entity)
            checks += 1
        call("Reuse", entity, ref + 1000000)
        assert call("Query", 0, kind, entity) == call("Query", 1, kind, entity) == -1
        checks += 1
        call("Put", kind, slot, entity, ref + 1000000)
        call("Unuse", kind, slot)
        assert call("Query", 0, kind, entity) == call("Query", 1, kind, entity) == -1
        checks += 1
        call("Delete", entity)
    return checks


def power_checks():
    prelude = r'''
#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#define MAX_BUILDINGS 1024
#define MAX_CORES 8
enum BuildingKind {Kind_Base,Kind_Generator,Kind_Light,Kind_Campfire,Kind_Expander,Kind_Beacon,Kind_Other,Kind_Support}
enum struct Building
{
    bool used;
    bool alive;
    bool constructing;
    bool gridAssigned;
    bool powered;
    bool lightManualOff;
    int ref;
    int uid;
    int hp;
    int maxHP;
    int powerPriority;
    int powerCore;
    int buildCore;
    int core;
    BuildingKind kind;
    float powerDemand;
    float powerOutput;
    float origin[3];
}
Building gBuildings[MAX_BUILDINGS];
float gGridProduction[MAX_CORES],gGridDemand[MAX_CORES],gGridAllocated[MAX_CORES];
int gGridOffline[MAX_CORES],gCoreCount=3;
int gPowerLoadOrder[MAX_BUILDINGS],gPowerLoadCount,gPowerOrderRevision;
bool gPowerEnabled,gRun,gNight,gPenalty,gCoreAlive[MAX_CORES];
int gEvents[4096],gEventCount,gState[MAX_BUILDINGS+MAX_CORES*4],gStateCount;
bool gReference,gReenter,gReentered;
bool BuildingOperational(int b){return b>=0 && b<MAX_BUILDINGS && gBuildings[b].used && gBuildings[b].alive && !gBuildings[b].constructing && gBuildings[b].hp>0;}
int Resolve(int ref){return ref>0 ? ref:-1;}
bool Running(){return gRun;}
bool LampNightPowerActive(){return gRun && gNight;}
bool BuildCoreAlive(int core){return core>=0 && core<gCoreCount && gCoreAlive[core];}
bool PowerOverloadPenaltyEnabled(){return gPenalty;}
bool IsBaseSupportKind(BuildingKind kind){return kind==Kind_Support;}
int GridAt(const float origin[3]){return RoundToFloor(origin[0]);}
void InvalidateDefenseCache(int b){if(gEventCount<4096)gEvents[gEventCount++]=10000+b;}
void MarkBuildAreasDirty(bool structure=true){if(structure)InvalidateRuntimeBuildings();}
void UpdateBuildingLight(int b){if(b<0)ThrowError("bad light");}
void SyncBeaconMarker(int b){if(b<0)ThrowError("bad beacon");}
void RefreshBuildAreas(){}
void RecordEvent(int value){if(gEventCount>=4096)ThrowError("event overflow");gEvents[gEventCount++]=value;}
void PlayCoreInteractionSound(int core,const char[] name){if(name[0]==0)ThrowError("sound");RecordEvent(20000+core);}
void NotifyPowerChanged(int core){
    RecordEvent(30000+core);
    // A subscriber must see the allocation fully committed, including other grids.
    for(int c=0;c<gCoreCount;c++){RecordEvent(view_as<int>(gGridProduction[c]));RecordEvent(view_as<int>(gGridDemand[c]));RecordEvent(view_as<int>(gGridAllocated[c]));RecordEvent(gGridOffline[c]);}
    if(gReenter && !gReentered){gReentered=true;if(gReference)ReferenceRecalculatePower();else RecalculatePower();}
}
public void VM_Setup(int scenario){
    Building empty;
    for(int b=0;b<MAX_BUILDINGS;b++)gBuildings[b]=empty;
    gCoreCount=3;for(int c=0;c<MAX_CORES;c++)gCoreAlive[c]=true;
    gRun=scenario%13!=1;gNight=scenario%3!=0;gPowerEnabled=scenario%7!=0;gPenalty=scenario%5==0;
    gReenter=scenario%11==0;gReentered=false;
    ResetPowerState();InvalidateRuntimeBuildings();gPowerOrderRevision=0;
    for(int b=0;b<64;b++){
        int slot=(b*37)%1024;
        gBuildings[slot].used=true;gBuildings[slot].alive=(b+scenario)%17!=0;
        gBuildings[slot].constructing=(b+scenario)%19==0;
        gBuildings[slot].uid=1000-b*3;gBuildings[slot].kind=view_as<BuildingKind>((b+scenario)%8);
        gBuildings[slot].ref=(b+scenario)%23==0 ? -1:slot+100;
        gBuildings[slot].hp=1+(b*29+scenario*11)%100;gBuildings[slot].maxHP=100;
        gBuildings[slot].powerDemand=float((b*13+scenario)%31)*0.25;
        gBuildings[slot].powerOutput=gBuildings[slot].kind==Kind_Generator ? float(4+(b*17+scenario)%90)*0.25:0.0;
        gBuildings[slot].powerPriority=(b+scenario)%4;gBuildings[slot].powerCore=b%3;
        gBuildings[slot].buildCore=(b+1)%3;gBuildings[slot].core=b%3;
        gBuildings[slot].origin[0]=float(b%3);gBuildings[slot].gridAssigned=(b+scenario)%9!=0;
        gBuildings[slot].lightManualOff=(b+scenario)%7==0;
    }
    if(scenario%6==0)gCoreAlive[1]=false;
    // Known generators, including an exact binary fractional threshold.
    for(int c=0;c<3;c++){
        int slot=1000+c;gBuildings[slot]=empty;
        gBuildings[slot].used=true;gBuildings[slot].alive=true;gBuildings[slot].kind=Kind_Generator;
        gBuildings[slot].ref=1700+c;gBuildings[slot].uid=2000+c;gBuildings[slot].hp=50;gBuildings[slot].maxHP=100;
        gBuildings[slot].powerOutput=23.5;gBuildings[slot].powerCore=c;gBuildings[slot].gridAssigned=true;
    }
    gEventCount=0;
}
public void VM_Run(int reference,int scenario,int scoped){
    VM_Setup(scenario);gReference=reference!=0;
    if(gReference)ReferenceRecalculatePower();else RecalculatePower();
    if(scoped){
        gEventCount=0;gReentered=false;
        // One HP-only transaction after a fully committed initial allocation.
        gBuildings[1000+scenario%3].hp=1+scenario%100;
        if(gReference)ReferenceRecalculatePower();else RecalculateBuildingPower(1000+scenario%3);
    }
    gStateCount=0;
    for(int b=0;b<MAX_BUILDINGS;b++)gState[gStateCount++]=view_as<int>(gBuildings[b].powered)+(gBuildings[b].powerCore+1)*2+view_as<int>(gBuildings[b].gridAssigned)*32;
    for(int c=0;c<gCoreCount;c++){
        gState[gStateCount++]=view_as<int>(gGridProduction[c]);gState[gStateCount++]=view_as<int>(gGridDemand[c]);
        gState[gStateCount++]=view_as<int>(gGridAllocated[c]);gState[gStateCount++]=gGridOffline[c];
    }
}
public int VM_StateCount(){return gStateCount;}
public int VM_State(int at){return gState[at];}
public int VM_EventCount(){return gEventCount;}
public int VM_EventAt(int at){return gEvents[at];}
public int VM_UID(int uid){return RuntimeFindBuildingUID(uid);}
'''
    functions = [
        (NEW / 'RuntimeIndex.inc').read_text(encoding='utf-8'),
        block(OLD, 'Power.inc', 'RecalculatePower').replace('RecalculatePower(', 'ReferenceRecalculatePower('),
    ]
    for name in ['ResetPowerState', 'GeneratorOutput', 'EnsurePowerLoadOrder', 'RecalculateBuildingPower', 'RecalculatePower']:
        functions.append(block(NEW, 'Power.inc', name))
    call = execute('power', prelude + '\n'.join(functions))
    if '--compile-only' in sys.argv:
        return 0
    assertions = 0
    for scenario in range(80):
        for scoped in range(2):
            snapshots = []
            for reference in [1, 0]:
                call('Run', reference, scenario, scoped)
                snapshots.append(([call('State', i) for i in range(call('StateCount'))], [call('EventAt', i) for i in range(call('EventCount'))]))
            assert snapshots[0] == snapshots[1], ('power mismatch', scenario, scoped, [(i, a, b) for i, (a, b) in enumerate(zip(snapshots[0][0], snapshots[1][0])) if a != b][:12], snapshots[0][1], snapshots[1][1])
            assertions += 1
    return assertions


def resource_checks():
    prelude = r'''
#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
enum struct Resource
{
    int ref;
    int material;
    bool growing;
    float elapsed;
}
enum struct Material
{
    float hold;
    float grow;
    bool ease;
    int rgb[3];
}
Resource gNodes[1];Material gMaterials[1];
int gRGBA[4],gFlags,gGroup,gInputs,gColorWrites,gGroupWrites;
int Resolve(int ref){return ref;}
#define GetEntityRenderColor FixtureGetColor
#define SetEntityRenderColor FixtureSetColor
#define AcceptEntityInput FixtureInput
#define GetEntProp FixtureProp
#define SetEntityCollisionGroup FixtureGroup
void FixtureGetColor(int e,int &r,int &g,int &b,int &a){if(e!=100)ThrowError("entity");r=gRGBA[0];g=gRGBA[1];b=gRGBA[2];a=gRGBA[3];}
void FixtureSetColor(int e,int r,int g,int b,int a){if(e!=100)ThrowError("entity");gRGBA[0]=r;gRGBA[1]=g;gRGBA[2]=b;gRGBA[3]=a;gColorWrites++;}
bool FixtureInput(int e,const char[] input){if(e!=100)ThrowError("entity");if(StrEqual(input,"DisableCollision"))gFlags|=4;else if(StrEqual(input,"EnableCollision"))gFlags&=~4;else ThrowError("input");gInputs++;return true;}
int FixtureProp(int e,PropType type,const char[] name){if(e!=100 || type!=Prop_Send)ThrowError("prop");return StrEqual(name,"m_usSolidFlags") ? gFlags:gGroup;}
void FixtureGroup(int e,int group){if(e!=100)ThrowError("entity");gGroup=group;gGroupWrites++;}
public void VM_Setup(int scenario){
    gNodes[0].ref=scenario%17==0 ? -1:100;gNodes[0].material=0;gNodes[0].growing=scenario%3!=0;gNodes[0].elapsed=float(scenario)*0.25;
    gMaterials[0].hold=4.0;gMaterials[0].grow=6.0;gMaterials[0].ease=scenario%2==0;
    gMaterials[0].rgb[0]=50;gMaterials[0].rgb[1]=100;gMaterials[0].rgb[2]=200;
    for(int i=0;i<4;i++)gRGBA[i]=123;
    gFlags=scenario%2==0 ? 0:4;gGroup=0;gInputs=0;gColorWrites=0;gGroupWrites=0;
}
public void VM_Run(int reference,int scenario,int force){VM_Setup(scenario);if(reference)ReferenceUpdateResourceVisual(0);else UpdateResourceVisual(0,force!=0);}
public void VM_Again(int force){UpdateResourceVisual(0,force!=0);}
public int VM_State(int i){if(i<4)return gRGBA[i];if(i==4)return gFlags;if(i==5)return gGroup;if(i==6)return gInputs;if(i==7)return gColorWrites;return gGroupWrites;}
public void VM_Corrupt(){gFlags^=4;gGroup=5;gRGBA[0]=1;}
'''
    call = execute('resource', prelude + block(OLD, 'World.inc', 'UpdateResourceVisual').replace('UpdateResourceVisual(', 'ReferenceUpdateResourceVisual(') + block(NEW, 'World.inc', 'UpdateResourceVisual'))
    checks = 0
    for scenario in range(60):
        for force in range(2):
            call('Run', 1, scenario, force)
            expected = [call('State', i) for i in range(6)]
            call('Run', 0, scenario, force)
            assert expected == [call('State', i) for i in range(6)]
            checks += 1
            before = [call('State', i) for i in range(9)]
            call('Again', 0)
            assert before == [call('State', i) for i in range(9)]
            checks += 1
            if scenario % 17:
                call('Corrupt');call('Again', 0)
                assert expected == [call('State', i) for i in range(6)]
                checks += 1
    return checks


def progression_checks():
    prelude = r'''
#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#define MAX_BUILDINGS 1024
#define MAX_CORES 8
#define PROGRESSION_CHANNELS 4
enum BuildingKind {Kind_Base,Kind_Other,Kind_Amplifier}
enum struct Building
{
    bool used;
    bool alive;
    bool powered;
    int uid;
    int hp;
    int ref;
    int powerCore;
    int level;
    BuildingKind kind;
    char type[64];
}
enum struct Rule
{
    int unlockLevel;
    int lockedDamage;
    float maxMultiplier;
    float maxAmplifierBonus;
}
Building gBuildings[MAX_BUILDINGS];Rule gProgressionRules[4];
bool gProgressionEnabled=true;int gProgressionLevels=4,gConfigRevision=1,gBaseLevel=2,gReads,gChecks;
bool BuildingPowered(int b){return gBuildings[b].powered && gBuildings[b].ref>0;}
int ProgressionBaseLevel(){return gBaseLevel;}
bool ReadProgressionBaseMultiplier(int channel,int level,float &value){gReads++;value=float(level)*0.5+float(channel)*0.25+float(gConfigRevision)*0.125;return level>0 && level<=gProgressionLevels;}
bool AmplifierLevelInfo(const char[] type,int level,int &required,float bonuses[4]){
    gReads++;required=1;for(int c=0;c<4;c++)bonuses[c]=0.0;
    if(!StrEqual(type,"valid") || level<1 || level>10)return false;
    for(int c=0;c<4;c++)bonuses[c]=float(level+c)*0.125+float(gConfigRevision)*0.25;return true;
}
public void VM_Setup(){
    Building empty;for(int b=0;b<MAX_BUILDINGS;b++)gBuildings[b]=empty;
    gConfigRevision=1;gBaseLevel=2;gReads=0;gChecks=0;gProgressionEnabled=true;
    for(int c=0;c<4;c++){
        gProgressionRules[c].unlockLevel=c==1 || c==3 ? 2:1;gProgressionRules[c].lockedDamage=1;
        gProgressionRules[c].maxMultiplier=8.0;gProgressionRules[c].maxAmplifierBonus=6.0;
        gProgressionCacheLevel[c]=0;gProgressionCacheRevision[c]=0;
    }
    for(int i=0;i<3;i++){
        int b=9+i*500;gBuildings[b].used=true;gBuildings[b].alive=true;gBuildings[b].powered=true;gBuildings[b].hp=100;
        gBuildings[b].uid=1+i;gBuildings[b].ref=100+i;gBuildings[b].level=1;gBuildings[b].kind=Kind_Amplifier;
        strcopy(gBuildings[b].type,64,"valid");gAmplifierCacheUID[b]=0;
    }
    InvalidateRuntimeBuildings();
}
public int VM_Step(int step){
    if(step==1){gBuildings[9].level=3;InvalidateRuntimeBuildings();}
    if(step==2)gBuildings[9].powered=false;
    if(step==3)gBuildings[509].hp=0;
    if(step==4)gBuildings[9].powered=true;
    if(step==5){gBuildings[509].hp=100;gBuildings[509].ref=-1;}
    if(step==6){gBuildings[1009].used=false;InvalidateRuntimeBuildings();}
    if(step==7){gBuildings[1009].used=true;gBuildings[1009].uid=999;gBuildings[1009].level=10;InvalidateRuntimeBuildings();}
    if(step==8){gConfigRevision++;}
    if(step==9)gBaseLevel=0;
    if(step==10)gBaseLevel=100;
    if(step==11)gProgressionEnabled=false;
    if(step==12){gProgressionEnabled=true;gBuildings[9].uid=444;strcopy(gBuildings[9].type,64,"invalid");InvalidateRuntimeBuildings();}
    if(step==13){gBuildings[9].uid=445;strcopy(gBuildings[9].type,64,"valid");InvalidateRuntimeBuildings();}
    int failures;
    for(int channel=0;channel<4;channel++){
        float a,b;int al,bl,ac,bc;bool defense=channel>=2,boss=channel%2!=0;
        ReferenceGetProgressionStats(defense,boss,a,al,ac);GetProgressionStats(defense,boss,b,bl,bc);
        if(view_as<int>(a)!=view_as<int>(b) || al!=bl || ac!=bc)failures++;gChecks++;
        int before=gReads;GetProgressionStats(defense,boss,b,bl,bc);
        if(before!=gReads)failures++;gChecks++;
    }
    return failures;
}
public int VM_Checks(){return gChecks;}
public int VM_UID(int uid){return RuntimeFindBuildingUID(uid);}
public int VM_Core(int after,int core){return RuntimeNextPowerBuilding(after,core);}
'''
    cache_text = (NEW / 'Progression.inc').read_text(encoding='utf-8')
    cache_text = cache_text[cache_text.index('// Fixed-size current-level'):cache_text.index('bool LoadProgression()')]
    # Declarations must precede their fixture uses.
    prelude = prelude.replace('Building gBuildings[MAX_BUILDINGS];Rule', cache_text + '\nBuilding gBuildings[MAX_BUILDINGS];Rule')
    functions = [(NEW / 'RuntimeIndex.inc').read_text(encoding='utf-8')]
    functions += [block(OLD, 'Progression.inc', 'AmplifierBonus').replace('AmplifierBonus(', 'ReferenceAmplifierBonus(')]
    functions += [block(OLD, 'Progression.inc', 'GetProgressionStats').replace('GetProgressionStats(', 'ReferenceGetProgressionStats(').replace('AmplifierBonus(', 'ReferenceAmplifierBonus(')]
    functions += [block(NEW, 'Progression.inc', name) for name in ['ProgressionChannel', 'CachedAmplifierBonuses', 'CachedProgressionBaseMultiplier', 'AmplifierBonus', 'GetProgressionStats']]
    call = execute('progression', prelude + '\n'.join(functions))
    if '--compile-only' in sys.argv:
        return 0
    call('Setup')
    for step in range(14):
        assert call('Step', step) == 0, ('progression cache mismatch', step)
    return call('Checks')


def main():
    result = {"scope": "compile only" if '--compile-only' in sys.argv else "offline production-function differential VM; no engine or multiplayer acceptance", "passed": False}
    try:
        tests = {'lookup': lookup_checks, 'power': power_checks, 'resource': resource_checks, 'progression': progression_checks}
        selected = next((name for name in tests if '--only-' + name in sys.argv), None)
        for name, test in tests.items():
            if selected is None or selected == name:
                result[name + '_assertions'] = test()
        result["passed"] = True
    finally:
        result["production_hashes"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in NEW.glob("*.inc")}
        (REPORT / "verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps({"report": str(REPORT), **{k: v for k, v in result.items() if k != "production_hashes"}}, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
