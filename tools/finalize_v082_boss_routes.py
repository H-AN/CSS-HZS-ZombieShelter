"""Read-only final audit of the completed local Boss tests and deployment."""
from __future__ import annotations

import json
import re
import runpy
import shutil

import test_v082_boss_routes as r


def read(name):
    return json.loads((r.LOCAL / (name + '.json')).read_text('utf-8'))


def main():
    build = read('formal_build')
    identity = build['sha256']
    assert not build['diagnostic']
    assert len(build['production_inputs']) == 52
    assert all(r.t.sha(r.REPO / path) == sha for path, sha in build['production_inputs'].items())
    formal_paths = [r.BUILD / 'HanZombieShelter.smx',
                    r.REPO / 'addons/sourcemod/plugins/HanZombieShelter.smx',
                    r.t.GAME / 'addons/sourcemod/plugins/HanZombieShelter.smx']
    assert all(r.t.sha(path) == identity for path in formal_paths)
    changes = {}
    baseline = read('baseline')
    for key, root in [('repo_files', r.REPO), ('live_files', r.t.ROOT)]:
        changed = []
        for old in baseline[key]:
            path = root / old['path']
            current = r.t.sha(path) if path.exists() else None
            if current != old['sha256']:
                changed.append({**old, 'new_sha256': current})
        changes[key] = {'audited': len(baseline[key]), 'unchanged': len(baseline[key]) - len(changed), 'changed': changed}
    assert {x['path'] for x in changes['repo_files']['changed']} == {
        'addons/sourcemod/scripting/HanZombieShelter/' + name + '.inc'
        for name in ('BossSpawns', 'Config', 'Lifecycle', 'NPC')
    } | {'addons/sourcemod/plugins/HanZombieShelter.smx'}
    assert {x['path'] for x in changes['live_files']['changed']} == {
        'cstrike/addons/sourcemod/configs/HanZombieShelter/maps/zsh_desert_han_dn.cfg',
        'cstrike/addons/sourcemod/plugins/HanZombieShelter.smx',
        'cstrike/addons/sourcemod/plugins/tests/CssBossPointCapture.smx'}
    r.record('preservation_verification', {'at': r.t.now(), **changes,
        'scope': 'Existing tracked product files only; new tools, reports and standalone map-point fragment are separate additions.'})

    log = r.t.GAME / 'addons/sourcemod/logs/v082_boss_path_probe.jsonl'
    frozen = r.LOCAL / 'completed_diagnostic_log.jsonl'
    shutil.copy2(log, frozen)
    rows = [json.loads(line) for line in frozen.read_text('utf-8').splitlines() if line.strip()]
    goals = [read('victory_boss%d_result' % i)['rows'][0] for i in (1, 2, 3)]
    assert [x['defeated'] for x in goals] == [1, 2, 3]
    assert [x['terminal'] for x in goals] == [0, 0, 1]
    assert [x['all_bosses_defeated'] for x in goals] == [0, 0, 1]
    assert {x['epoch'] for x in goals} == {9}
    deaths = [x for x in rows if x.get('kind') == 'native_boss_death_forward' and x['epoch'] == 9]
    assert {x['boss_id'] for x in deaths} == {'boss_01', 'boss_02', 'boss_03'}
    assert len(deaths) == 3 and all(x['real_human_killer'] == 1 and x['killer_userid'] == 2 for x in deaths)
    guard = read('victory_after_map_guard')['rows'][0]
    assert guard['map_change_intercepted'] == 1 and guard['all_bosses_defeated'] == 1
    r.record('victory_runtime_verification', {'at': r.t.now(), 'passed': True,
        'diagnostic_sha256': read('diagnostic_build')['sha256'], 'epoch': 9,
        'real_player_death_forwards': deaths, 'successive_results': goals,
        'map_change_guard': guard, 'log_sha256': r.t.sha(frozen),
        'method': 'Each existing Boss lowered to 1 HP, then killed by an actual ordinary-client USP shot. Production death hooks and win gates executed. No NPC spawned/revived by test command.',
        'limits': 'Diagnostic player/building protection and paused admissions/exposure/clock. Real changelevel intercepted due known local crash; not a full-HP combat balance test.',
        'visual': 'victory_all_three_01_capture.json: reviewed client JPEG shows victory message.'})

    cleanup = read('helpers_cleanup')
    assert len(cleanup['helpers']) == 4 and all(x['unloaded'] and x['removed'] for x in cleanup['helpers'])
    assert not list((r.t.GAME / 'addons/sourcemod/plugins/tests').glob('*.smx'))
    parse = runpy.run_path(str(r.REPO / 'tools/migrate_v05_materials.py'))['parse']
    snap = parse((r.LOCAL / 'formal_user_snapshot.cfg').read_text('utf-8-sig'))['V082UserSnapshot']
    player = snap['player']
    assert player['takedamage'] == '2' and player['move_type'] == '2'
    assert int(player['flags']) & 16384 == 0
    assert all(row['amount'] == '0' for row in player['Materials'].values())
    controller = r.t.controller('--status')
    processes = r.t.processes()
    assert controller['server_running'] and controller['controller_running']
    assert len([p for p in processes if p['ProcessName'] == 'srcds_win64']) == 1
    assert len([p for p in processes if p['ProcessName'] == 'cstrike_win64']) == 1
    window = r.t.minimize()
    status = r.command('status', 'final_readonly_engine_status')
    shelter = r.command('sm_zsh_status', 'final_readonly_shelter_status')
    plugins = r.command('sm plugins list', 'final_readonly_plugins')
    skills = r.command('sm_zsh_skills_status', 'final_readonly_skills')
    assert '1 humans, 0 bots' in status['output'] and 'Listing 28 plugins' in plugins['output']
    assert 'ready=1' in shelter['output']
    assert all(word not in plugins['output'] for word in ('CSS temporary', 'CSS v0.8.2', 'CSS test native', 'Diagnostic'))
    errors = r.t.GAME / 'addons/sourcemod/logs/errors_20261025.log'
    error_text = errors.read_text('utf-8', errors='replace') if errors.exists() else ''
    entries = re.split(r'(?=^L 10/25/2026 - \d\d:\d\d:\d\d:)', error_text, flags=re.M)
    def recent_errors(since):
        return [x for x in entries if (m := re.match(r'L 10/25/2026 - (\d\d:\d\d:\d\d):', x)) and m[1] >= since]
    runtime = (r.t.GAME / 'local_test_runtime.log').read_text('utf-8', errors='replace')
    formal_tail = runtime[runtime.find('L 10/25/2026 - 19:18:17:'):]
    recent = recent_errors('19:18:17')
    assert not recent, recent
    audit = {'at': r.t.now(), 'main_sha256': identity, 'source_inputs': 52,
        'server_pid': controller['session']['pid'], 'controller_pid': controller['session']['controller_pid'],
        'client_pid': next(p['Id'] for p in processes if p['ProcessName'] == 'cstrike_win64'),
        'address': controller['environment']['address'], 'ready': True, 'kept_running': True,
        'window_minimized': window['minimized_or_hidden'], 'humans': 1, 'bots': 0, 'plugins': 28,
        'controller_startup_hash_is_historical': controller['session']['main_sha256'],
        'current_formal_identity_receipt': 'formal_deployment.json',
        'shelter_state_receipt': 'final_readonly_shelter_status.json',
        'formal_bound_evidence': 'formal_bound_status.json',
        'formal_bound_initial_hp': [50000, 50000, 50000],
        'formal_player_sample_unix_time': int(snap['unix_time']),
        'formal_player_sample': {k: player[k] for k in ('alive','hp','takedamage','flags','move_type','gravity_raw')},
        'temporary_materials_remaining_at_sample': 0,
        'formal_building_count_at_sample': len(snap.get('Buildings', {})),
        'old_pre_user_shutdown_snapshot_replayed': False,
        'zombies_resumed_receipt': 'formal_restore_zombies.json',
        'helpers_removed': [x['name'] for x in cleanup['helpers']],
        'final_visual': {'path': str(r.REPORT / 'screenshots/hzsh_v082_formal_retained_01.jpg'), 'reviewed_clear': True},
        'new_sm_error_entries_since_formal_load': len(recent),
        'new_sm_error_entries_since_retry_diagnostic_load': len(recent_errors('19:05:53')),
        'historical_errors_retained': True,
        'formal_animation_warning_count': len(re.findall(r'(?i)bad sequence|invalid sequence|animation error', formal_tail)),
        'runtime_audio_and_client_delta_warnings_not_claimed_fixed': True,
        'natural_state_may_change_after_sample': True}
    r.record('final_session', audit)
    print(json.dumps(audit, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
