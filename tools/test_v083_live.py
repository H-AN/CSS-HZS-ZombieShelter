"""Explicit v0.8.3 tests in the one authorized local server, zero fake clients."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time

import test_v082_crafting_live as legacy

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(r'D:\gmod昼夜求生资源\文档\制作计划\v0.8.3 优化计划\implementation_20261030')
legacy.REPORT = legacy.ROOT / 'reports/v083_performance_20261030'
legacy.BUILD = WORK / 'build/live'
legacy.BACKUP = legacy.ROOT / 'backups/v083_performance_20261030'
legacy.PYTHON = legacy.ROOT / 'tools/python/python.exe'
REPORT = legacy.REPORT

def write(name, data):
    legacy.write(REPORT / name, data)

def request(command, label):
    output = legacy.command(command)
    write(label + '.json', {'at': legacy.now(), 'command': command, 'output': output})
    print(output)
    return output

def inventory():
    prefixes = ['addons/sourcemod/plugins', 'addons/sourcemod/configs', 'addons/sourcemod/gamedata', 'addons/sourcemod/scripting', 'addons/sourcemod/translations', 'maps']
    return {p.relative_to(legacy.GAME).as_posix(): legacy.sha(p) for prefix in prefixes for p in (legacy.GAME / prefix).rglob('*') if p.is_file()}

def build(diagnostic):
    output = legacy.BUILD / ('HanZombieShelterDiagnostic.smx' if diagnostic else 'HanZombieShelter.smx')
    original = ROOT / 'addons/sourcemod/scripting/HanZombieShelter.sp'
    source = original
    if diagnostic:
        source = legacy.BUILD / 'HanZombieShelterDiagnostic.sp'
        source.parent.mkdir(parents=True, exist_ok=True)
        text = original.read_text(encoding='utf-8-sig')
        # Expand only local production includes into the diagnostic copy; normal
        # includes remain compiler inputs. Production files are never instrumented.
        def expand_file(file):
            def expand(match):
                included = file.parent / match.group(1)
                if not included.is_file():
                    return match.group(0)
                return '\n// Diagnostic snapshot of ' + included.name + '\n' + expand_file(included)
            return re.sub(r'^#include "([^"\n]+)"\s*$', expand, file.read_text(encoding='utf-8-sig'), flags=re.M)
        text = expand_file(original)
        profiles = [
            ('public Action', 'WorldTick', 'Handle timer', 'timer', 'Action'),
            ('public Action', 'DefenseTimer', 'Handle timer', 'timer', 'Action'),
            ('public Action', 'GrenadeTimer', 'Handle timer', 'timer', 'Action'),
            ('void', 'RecalculatePower', 'int onlyCore=-1', 'onlyCore', 'void'),
            ('void', 'PowerTick', '', '', 'void'),
            ('public Action', 'ResourceDamageHook', 'int entity,int &attacker,int &inflictor,float &damage,int &damageType,int &weapon,float damageForce[3],float damagePosition[3]', 'entity,attacker,inflictor,damage,damageType,weapon,damageForce,damagePosition', 'Action'),
            ('public Action', 'BuildingDamageHook', 'int entity,int &attacker,int &inflictor,float &damage,int &damageType,int &weapon,float damageForce[3],float damagePosition[3]', 'entity,attacker,inflictor,damage,damageType,weapon,damageForce,damagePosition', 'Action'),
            ('void', 'GetProgressionStats', 'bool defense,bool boss,float &multiplier,int &lockedDamage,int &amplifierCount', 'defense,boss,multiplier,lockedDamage,amplifierCount', 'void'),
        ]
        wrappers = []
        for i, (prefix, name, args, values, returns) in enumerate(profiles):
            pattern = r'^' + re.escape(prefix) + ' ' + name + r'\('
            text, count = re.subn(pattern, prefix + ' V083Measured_' + name + '(', text, flags=re.M)
            assert count == 1, (name, count)
            body = 'float begin=GetEngineTime();'
            body += ('' if returns == 'void' else returns + ' result=') + 'V083Measured_' + name + '(' + values + ');'
            body += 'V083Duration(' + str(i) + ',GetEngineTime()-begin);' + ('' if returns == 'void' else 'return result;')
            wrappers.append(prefix + ' ' + name + '(' + args + '){' + body + '}')
        probe = (ROOT / 'tests/V082CraftingLiveProbe.inc').read_text(encoding='utf-8').replace('V082', 'V083Work').replace('v082', 'v083_work')
        probe += '\n' + (ROOT / 'tests/V083PerformanceLiveProbe.inc').read_text(encoding='utf-8')
        # Probe globals must precede any uses. Wrappers are functions and can be forward called.
        text = text.replace('public Plugin myinfo=', probe + '\n' + '\n'.join(wrappers) + '\npublic Plugin myinfo=')
        text = text.replace('public void OnPluginStart()\n{', 'public void OnPluginStart()\n{\n    RegisterV083Probe();')
        source.write_text(text, encoding='utf-8')
    result = legacy.compile_source(source, output)
    result.update(diagnostic=diagnostic, production_main_sha256=legacy.sha(original), production_sources={p.relative_to(ROOT).as_posix(): legacy.sha(p) for p in sorted((ROOT / 'addons/sourcemod/scripting/HanZombieShelter').glob('*.inc'))})
    inputs = {**result['production_sources'], original.relative_to(ROOT).as_posix(): result['production_main_sha256']}
    for name in ['HanZombieScenarioAPI.inc', 'HanZombieShelterAPI.inc']:
        path = ROOT / 'addons/sourcemod/scripting/include' / name
        inputs[path.relative_to(ROOT).as_posix()] = legacy.sha(path)
    result.update(production_inputs=inputs, production_input_count=len(inputs))
    write('diagnostic_build.json' if diagnostic else 'formal_build.json', result)
    print(json.dumps({k: v for k, v in result.items() if k != 'production_sources'}, ensure_ascii=True))

def install(diagnostic):
    assert not any(p['ProcessName'] == 'srcds_win64' for p in legacy.processes()), 'Preserve the running session; do not restart'
    before = REPORT / 'live_before.json'
    if not before.exists():
        write('live_before.json', {'at': legacy.now(), 'files': inventory(), 'processes': legacy.processes()})
    source = legacy.BUILD / ('HanZombieShelterDiagnostic.smx' if diagnostic else 'HanZombieShelter.smx')
    destination = legacy.GAME / 'addons/sourcemod/plugins/HanZombieShelter.smx'
    legacy.backup(destination)
    shutil.copy2(source, destination)
    write('installed_before_start.json', {'at': legacy.now(), 'diagnostic': diagnostic, 'sha256': legacy.sha(destination), 'old_sha256': legacy.sha(legacy.BACKUP / 'addons/sourcemod/plugins/HanZombieShelter.smx')})
    print('Installed only HanZombieShelter.smx before starting the sole server.')

def engine_matrix(vm_report, name):
    assert name in ('power', 'index', 'progression')
    source = Path(vm_report) / (('power' if name == 'index' else name) + '.sp')
    text = source.read_text(encoding='utf-8')
    if name == 'power':
        body = r'''
int savedState[MAX_BUILDINGS+MAX_CORES*4],savedEvents[4096];int failures,checks;
for(int scenario=0;scenario<80;scenario++)for(int scoped=0;scoped<2;scoped++){
    VM_Run(1,scenario,scoped);int states=gStateCount,events=gEventCount;
    for(int i=0;i<states;i++)savedState[i]=gState[i];for(int i=0;i<events;i++)savedEvents[i]=gEvents[i];
    VM_Run(0,scenario,scoped);bool equal=states==gStateCount && events==gEventCount;
    for(int i=0;i<states;i++)if(savedState[i]!=gState[i])equal=false;for(int i=0;i<events;i++)if(savedEvents[i]!=gEvents[i])equal=false;
    checks++;if(!equal){failures++;PrintToServer("[V083Matrix] mismatch scenario=%d scoped=%d",scenario,scoped);}
}
PrintToServer("[V083Matrix] power checks=%d failures=%d; isolated engine state doubles, not game-world or multiplayer acceptance",checks,failures);
float oldTimes[9],newTimes[9];
for(int sample=0;sample<9;sample++){
    VM_Setup(23);gReference=true;ReferenceRecalculatePower();float begin=GetEngineTime();
    for(int pass=0;pass<40;pass++){gEventCount=0;ReferenceRecalculatePower();}oldTimes[sample]=(GetEngineTime()-begin)*1000.0/40.0;
    VM_Setup(23);gReference=false;RecalculatePower();begin=GetEngineTime();
    for(int pass=0;pass<40;pass++){gEventCount=0;RecalculatePower();}newTimes[sample]=(GetEngineTime()-begin)*1000.0/40.0;
}
for(int sample=0;sample<9;sample++)PrintToServer("[V083Matrix] microbench sample=%d old_ms=%.6f new_ms=%.6f; fake entity/effect natives",sample,oldTimes[sample],newTimes[sample]);
'''
    elif name == 'index':
        text += '\n' + (ROOT / 'tests/V083IndexChecks.inc').read_text(encoding='utf-8')
        body = 'V083IndexMatrix();'
    else:
        body = 'VM_Setup();int failures;for(int step=0;step<14;step++)failures+=VM_Step(step);PrintToServer("[V083Progression] checks=%d failures=%d; isolated configuration/power doubles",VM_Checks(),failures);'
    text += '\n#pragma dynamic 65536\npublic void OnPluginStart(){RegServerCmd("sm_v083_matrix_' + name + '",V083MatrixCommand);}\npublic Action V083MatrixCommand(int args){' + body + 'return Plugin_Handled;}\n'
    label = 'matrix_' + name + '_' + str(time.time_ns())
    generated = legacy.BUILD / (label + '.sp')
    generated.write_text(text, encoding='utf-8')
    output = generated.with_suffix('.smx')
    receipt = legacy.compile_source(generated, output)
    destination = legacy.GAME / 'addons/sourcemod/plugins/tests' / output.name
    destination.parent.mkdir(exist_ok=True)
    assert not destination.exists(), 'Never replace an unidentified helper'
    shutil.copy2(output, destination)
    write(label + '_build.json', receipt)
    request('sm plugins load tests/' + output.name, label + '_load')
    try:
        result = request('sm_v083_matrix_' + name, label + '_run')
        assert 'failures=0' in result and 'Exception' not in result, result
    finally:
        request('sm plugins unload tests/' + output.name, label + '_unload')
        destination.unlink()

def entity_checks():
    sys.path.insert(0, str(ROOT / 'tests'))
    # Importing the VM test would create its report directory; extract declarations here instead.
    def function(path, name):
        text = path.read_text(encoding='utf-8-sig')
        start = re.search(r'^void ' + name + r'\([^\n]*', text, re.M).start()
        left = text.index('{', start);depth=1;at=left+1
        while depth:
            depth += (text[at] == '{') - (text[at] == '}');at+=1
        return text[start:at]
    production = ROOT / 'addons/sourcemod/scripting/HanZombieShelter'
    funcs = function(production / 'Power.inc', 'UpdateBuildingLight') + '\n' + function(production / 'World.inc', 'UpdateResourceVisual')
    source = legacy.BUILD / ('V083Entities_' + str(time.time_ns()) + '.sp')
    source.write_text((ROOT / 'tests/V083EntityMaintenanceProbe.sp.in').read_text(encoding='utf-8').replace('// @PRODUCTION_FUNCTIONS@', funcs), encoding='utf-8')
    receipt = legacy.compile_source(source, source.with_suffix('.smx'))
    destination = legacy.GAME / 'addons/sourcemod/plugins/tests' / source.with_suffix('.smx').name
    shutil.copy2(source.with_suffix('.smx'), destination)
    label = source.stem.lower()
    write(label + '_build.json', receipt)
    request('sm plugins load tests/' + destination.name, label + '_load')
    try:
        output = request('sm_v083_entity_checks', label + '_run')
        assert 'Exception' not in output, output
        time.sleep(.3)
        output = request('sm_v083_entity_status', label + '_status')
        assert 'checks=18 failures=0' in output, output
    finally:
        request('sm plugins unload tests/' + destination.name, label + '_unload')
        destination.unlink()

if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'install', 'command', 'capture', 'client', 'tap', 'matrix', 'status', 'aim-helper', 'entities', 'snapshot-helper'])
    parser.add_argument('--diagnostic', action='store_true')
    parser.add_argument('--text')
    parser.add_argument('--label', default='status')
    parser.add_argument('--bootstrap', action='store_true')
    parser.add_argument('--vm-report')
    args = parser.parse_args()
    if args.action == 'build': build(args.diagnostic)
    elif args.action == 'install': install(args.diagnostic)
    elif args.action == 'command': request(args.text, args.label)
    elif args.action == 'capture': print(json.dumps(legacy.capture(args.label, args.bootstrap), ensure_ascii=True))
    elif args.action == 'client': legacy.client(args.text, args.label, args.bootstrap)
    elif args.action == 'tap': print(legacy.tap(args.text, args.label))
    elif args.action == 'matrix': engine_matrix(args.vm_report, args.text)
    elif args.action == 'status': print(json.dumps(legacy.controller('--status'), ensure_ascii=True))
    elif args.action == 'entities': entity_checks()
    elif args.action == 'snapshot-helper':
        output = legacy.BUILD / 'V083FinalSnapshot.smx'
        receipt = legacy.compile_source(ROOT / 'tests/V083FinalSnapshot.sp', output)
        dest = legacy.GAME / 'addons/sourcemod/plugins/tests' / output.name
        assert not dest.exists()
        shutil.copy2(output, dest);write('final_snapshot_build.json', receipt)
        request('sm plugins load tests/' + output.name, 'final_snapshot_load')
    elif args.action == 'aim-helper':
        output = legacy.BUILD / 'V083CloseAim.smx'
        receipt = legacy.compile_source(ROOT / 'tests/V082CloseAim.sp', output)
        dest = legacy.GAME / 'addons/sourcemod/plugins/tests/V083CloseAim.smx'
        assert not dest.exists()
        shutil.copy2(output, dest)
        write('close_aim_build.json', receipt)
        request('sm plugins load tests/V083CloseAim.smx', 'close_aim_load')
