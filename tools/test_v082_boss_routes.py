"""Explicit Boss-route actions on the sole fixed server; no automatic restart."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import runpy
import shutil

import test_v082_crafting_live as t

REPO = t.REPO
REPORT = t.ROOT / 'reports/v082_boss_routes_20261025'
LOCAL = REPO / 'tests/reports/v082_boss_routes_20261025'
BUILD = LOCAL / 'build'
t.REPORT = REPORT
t.BUILD = BUILD


def record(name, value):
    for folder in (REPORT, LOCAL):
        destination = folder / (name + '.json')
        if destination.exists():
            previous = json.loads(destination.read_text('utf-8'))
            if previous != value:
                history = folder / 'history' / (name + '_' + t.sha(destination) + '.json')
                if not history.exists():
                    t.write(history, previous)
        t.write(destination, value)


def parse_output(output):
    rows = []
    for line in output.splitlines():
        marker = line.find('{')
        if marker >= 0:
            try:
                rows.append(json.loads(line[marker:]))
            except json.JSONDecodeError:
                pass
    return rows


def command(text, label):
    result = {'at': t.now(), 'command': text, 'output': t.command(text)}
    result['rows'] = parse_output(result['output'])
    if label:
        record(label, result)
    return result


def kv_text(data, indent=0):
    lines = []
    def quote(value):
        return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'
    for key, value in data.items():
        prefix = '    ' * indent
        if isinstance(value, dict):
            lines.extend((prefix + quote(key), prefix + '{', kv_text(value, indent + 1).rstrip('\n'), prefix + '}'))
        else:
            lines.append(prefix + quote(key) + ' ' + quote(value))
    return '\n'.join(lines) + '\n'


def snapshot():
    result = command('sm_v082_usersnapshot', 'user_snapshot_export_latest')
    assert 'committed=1' in result['output'], result['output']
    raw = t.GAME / 'addons/sourcemod/data/v082_routes_user_snapshot.cfg'
    original = raw.read_bytes()
    archived = LOCAL / ('user_snapshot_' + t.sha(raw) + '.cfg')
    if not archived.exists():
        archived.write_bytes(original)
    skills = command('sm_zsh_skills_status', 'skills_before_application')
    status = command('sm_zsh_status', 'status_before_application')
    parse = runpy.run_path(str(REPO / 'tools/migrate_v05_materials.py'))['parse']
    data = parse(original.decode('utf-8-sig'))
    state = data['V082UserSnapshot']
    text = skills['output']
    player = re.search(r'\[ShelterSkillsPlayer\] userid=(\d+) account=\d+ revision=\d+ spent=(\d+) balance=(\d+)', text)
    budget = re.search(r'\[ShelterSkillsBudget\] userid=(\d+) earned=(\d+) adjustment=(-?\d+)', text)
    highest = re.search(r'dmax=(\d+) quota=(\d+)', text)
    rate = re.search(r'configured=(\d+) effective=(\d+) override=(-?\d+)', text)
    assert player and budget and highest and rate and player[1] == budget[1] == state['player']['userid']
    ids = ['gather','repair','construct','speed','gravity','immunity','medic','logistics','artisan','selfheal','selfarmor','maxhp','maxarmor']
    levels = {ids[int(index)]: value for userid, index, value in re.findall(r'\[ShelterSkill\] userid=(\d+) id=(\d+) level=(\d+)', text) if userid == player[1]}
    assert len(levels) == 13
    state['Skills'] = {'highest_day': highest[1], 'earned': budget[2], 'spent': player[2],
                       'adjustment': budget[3], 'daily_rate': rate[2], 'rate_override': rate[3], 'Levels': levels,
                       'schedule_note': 'Public snapshot contains current rate and earned/highest; earlier private rate schedule was not exported.'}
    state['Bosses'] = {identifier: {'saved_hp': hp, 'defeated': defeated, 'observed_awake': awake}
        for identifier, hp, awake, defeated in re.findall(r'\[ShelterBoss\] (\S+) hp=(\d+) awake=(\d+) defeated=(\d+)', status['output'])}
    assert len(state['Bosses']) == 3
    payload = kv_text(data).encode('utf-8')
    destination = t.GAME / 'addons/sourcemod/data/v082_routes_restore_input.cfg'
    if destination.exists():
        backup = LOCAL / ('restore_input_' + t.sha(destination) + '.cfg')
        if not backup.exists():
            shutil.copy2(destination, backup)
    destination.write_bytes(payload)
    (LOCAL / 'restore_input.cfg').write_bytes(payload)
    return {'path': str(destination), 'sha256': t.sha(destination), 'day': state['Environment']['day'],
            'skills': state['Skills'], 'bosses': state['Bosses'],
            'player_materials': {row['id']: row['amount'] for row in state['player']['Materials'].values()}}


def build(diagnostic=False):
    source = REPO / 'addons/sourcemod/scripting/HanZombieShelter.sp'
    inputs = [source, *sorted((source.parent / 'HanZombieShelter').glob('*.inc')),
              source.parent / 'include/HanZombieScenarioAPI.inc', source.parent / 'include/HanZombieShelterAPI.inc']
    identities = {p.relative_to(REPO).as_posix(): t.sha(p) for p in inputs}
    changes = []
    if diagnostic:
        folder = BUILD / 'diagnostic_source'
        folder.mkdir(parents=True, exist_ok=True)
        for p in inputs:
            relative = p.relative_to(source.parent)
            destination = folder / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, destination)
        source = folder / source.name
        additions = ['V082BossPathProbe.inc', 'V082UserStateRestore.inc', 'V082BossRoutesControl.inc']
        for name in additions:
            assert (REPO / 'tests' / name).is_file(), name
        text = source.read_text('utf-8-sig')
        marker = 'public Plugin myinfo='
        assert text.count(marker) == 1
        includes = ''.join('#include "' + (REPO / 'tests' / name).as_posix() + '"\n' for name in additions)
        text = text.replace(marker, includes + '\n' + marker)
        marker = 'public void OnPluginStart()\n{'
        assert text.count(marker) == 1
        text = text.replace(marker, marker + '\n    RegisterV082BossPathProbe(true);RegisterV082UserStateRestore();RegisterV082BossRoutesControl();')
        marker = 'public void OnPluginEnd()\n{'
        assert text.count(marker) == 1
        text = text.replace(marker, marker + '\n    V082BossPathShutdown();')
        source.write_text(text, encoding='utf-8')
        patches = {
            'World.inc': [('void DamageBuilding(int b,int damage,bool attacked=false,int attackerNPC=-1)\n{',
                'void DamageBuilding(int b,int damage,bool attacked=false,int attackerNPC=-1)\n{\n    if(V082BossPathBlockBuildingDamage(b))return;')],
            'NPC.inc': [('void NPCTick(float now)\n{', 'void NPCTick(float now)\n{\n    V082BossPathBeforeNPCTick(now);'),
                ('    ApplyProgressionDamage(n,attacker,damage,external && gDamageIsDefense);',
                 '    ApplyProgressionDamage(n,attacker,damage,external && gDamageIsDefense);\n    if(!V082RouteAllowsLethal())V082BossPathFilterDamage(zombie,attacker,damage);'),
                ('public void Han_OnZombieDeath(int zombie,int killer)\n{',
                 'public void Han_OnZombieDeath(int zombie,int killer)\n{\n    V082RouteRecordBossDeath(zombie,killer);')],
            'Lifecycle.inc': [('void ChangeCampaignMap()\n{',
                              'void ChangeCampaignMap()\n{\n    if(V082RouteInterceptMapChange())return;')],
        }
        for name, replacements in patches.items():
            path = folder / 'HanZombieShelter' / name
            data = path.read_text('utf-8-sig')
            for old, new in replacements:
                assert data.count(old) == 1, (name, old)
                data = data.replace(old, new)
            path.write_text(data, encoding='utf-8')
            changes.append({'file': name, 'sha256': t.sha(path), 'diagnostic_insertions': len(replacements)})
    output = BUILD / ('HanZombieShelterBossRoutesDiagnostic.smx' if diagnostic else 'HanZombieShelter.smx')
    if output.exists():
        history = BUILD / 'history' / (output.stem + '_' + t.sha(output) + '.smx')
        history.parent.mkdir(parents=True, exist_ok=True)
        if not history.exists():
            shutil.copy2(output, history)
    result = t.compile_source(source, output)
    assert all(t.sha(REPO / name) == identity for name, identity in identities.items())
    result.update(diagnostic=diagnostic, production_inputs=identities, diagnostic_source_changes=changes)
    record('diagnostic_build' if diagnostic else 'formal_build', result)
    return {key: result[key] for key in ('sha256','diagnostic','log','output')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['command','client','tap','capture','confirm-visual','snapshot','build','build-diagnostic','processes'])
    parser.add_argument('--text', default='')
    parser.add_argument('--label', default='')
    parser.add_argument('--bootstrap', action='store_true')
    args = parser.parse_args()
    if args.action == 'command':
        result = command(args.text, args.label)
    elif args.action == 'client':
        t.client(args.text, args.label, args.bootstrap)
        result = {'client_input_sent': True, 'label': args.label}
    elif args.action == 'tap':
        result = t.tap(args.text, args.label)
    elif args.action == 'capture':
        result = t.capture(args.label, args.bootstrap)
    elif args.action == 'confirm-visual':
        captured = json.loads((REPORT / (args.label + '_capture.json')).read_text('utf-8'))
        assert t.sha(Path(captured['path'])) == captured['sha256']
        result = {**captured, 'clear_confirmed': True, 'review': 'Reviewed with view_image, no native page obstruction'}
        t.write(REPORT / 'client_visual_gate.json', result)
    elif args.action == 'snapshot':
        result = snapshot()
    elif args.action in ('build','build-diagnostic'):
        result = build(args.action == 'build-diagnostic')
    else:
        result = {'at': t.now(), 'processes': t.processes()}
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
