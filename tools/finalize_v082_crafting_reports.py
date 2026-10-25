"""Read-only final sampling and evidence summary for the retained v0.8.2 session."""
from __future__ import annotations

import json
import re
import runpy
import shutil
import subprocess
from pathlib import Path

import test_v082_crafting_live as t


def read(path):
    return json.loads(path.read_text('utf-8-sig'))


def main():
    expected = '3920d6fcc9a5f4a490255153dd91a044ce532f637c3c6b066c050945d2dcfab1'
    build = read(t.REPORT / 'formal_build.json')
    assert build['sha256'] == expected and not build['diagnostic']
    assert t.sha(t.REPO / 'addons/sourcemod/scripting/HanZombieShelter.sp') == build['production_main_sha256']
    assert all(t.sha(t.REPO / name) == digest for name, digest in build['production_sources'].items())
    binaries = [t.BUILD / 'HanZombieShelter.smx', t.REPO / 'addons/sourcemod/plugins/HanZombieShelter.smx',
                t.GAME / 'addons/sourcemod/plugins/HanZombieShelter.smx']
    assert all(t.sha(p) == expected for p in binaries)
    baseline = read(t.BUILD.parent / 'baseline_hashes.json')
    changes = [{'path': row['path'], 'before': row['sha256'], 'after': t.sha(t.REPO / row['path'])}
               for row in baseline if t.sha(t.REPO / row['path']) != row['sha256']]
    allowed = {'addons/sourcemod/scripting/HanZombieShelter.sp',
               'addons/sourcemod/configs/HanZombieShelter/weapons.cfg',
               'addons/sourcemod/translations/HanZombieShelter.phrases.txt',
               'addons/sourcemod/plugins/HanZombieShelter.smx'}
    allowed.update('addons/sourcemod/scripting/HanZombieShelter/' + name + '.inc'
                   for name in ('Types', 'Config', 'Levels', 'Commands', 'World', 'HUD'))
    assert {row['path'] for row in changes} <= allowed
    preservation = {'at': t.now(), 'baseline_files': len(baseline), 'changed_existing': changes,
                    'unchanged_existing_count': len(baseline) - len(changes),
                    'scope': 'Repository pre-edit snapshot; live deployment used leaf merges and separate backups.'}
    configs = []
    for name in ('workbenches.cfg', 'weapon_crafting.cfg', 'material_crafting.cfg', 'weapons.cfg'):
        rel = 'addons/sourcemod/configs/HanZombieShelter/' + name
        repo, live = t.REPO / rel, t.GAME / rel
        byte_equal = t.sha(repo) == t.sha(live)
        if not byte_equal:
            assert name == 'weapons.cfg', name
            parse = runpy.run_path(str(t.REPO / 'tools/migrate_v05_materials.py'))['parse']
            assert parse(repo.read_text('utf-8-sig')) == parse(live.read_text('utf-8-sig'))
        configs.append({'name': name, 'live_sha256': t.sha(live), 'repo_sha256': t.sha(repo),
                        'byte_equal': byte_equal, 'parsed_values_equal': True})
    t.write(t.REPORT / 'final_preservation.json', preservation)
    t.write(t.REPORT / 'final_config_deployment.json', {'at': t.now(), 'files': configs,
            'note': 'Final identities supersede initial zero-power workbenches hash in config_deployment.json; history retained.'})
    probe = read(t.REPORT / 'final_fixture_cleanup.json')['output']
    assert '"passed":75,"failed":0' in probe and '"active":0' in probe
    assert probe.count('"test_delta":0') == 7
    knife = read(t.REPORT / 'real_knife_work_verification.json')
    assert knife['passed'] == 20 and knife['failed'] == 0
    vm = read(t.BUILD.parent / 'offline_vm_verification.json')
    assert vm['checks_passed'] == 147 and vm['checks_failed'] == 0
    plugins = t.command('sm plugins list')
    assert 'Listing 28 plugins' in plugins and '"HZS Zombie Shelter" (0.8.2)' in plugins
    assert 'CSS test native panel closer' not in plugins and 'temporary close-aim' not in plugins
    assert not list((t.GAME / 'addons/sourcemod/plugins').rglob('*v082*.smx'))
    status = t.command('sm_zsh_status')
    assert 'version=0.8.2 enabled=1 ready=1' in status
    processes = t.processes()
    session = t.owned_server()
    clients = [row for row in processes if row['ProcessName'] == 'cstrike_win64']
    assert len(clients) == 1 and clients[0]['Id'] == 13940
    native_status = t.command('status')
    assert '1 humans, 0 bots' in native_status, native_status
    endpoint_query = "@{udp=@(Get-NetUDPEndpoint -LocalPort 27169 | Select-Object LocalAddress,LocalPort,OwningProcess);tcp=@(Get-NetTCPConnection -LocalPort 27169 -State Listen | Select-Object LocalAddress,LocalPort,OwningProcess)} | ConvertTo-Json -Depth 4 -Compress"
    result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', endpoint_query],
                            stdin=subprocess.DEVNULL, capture_output=True, encoding='utf-8', check=True)
    endpoints = json.loads(result.stdout)
    assert all(row['OwningProcess'] == session['pid'] and row['LocalAddress'] == '0.0.0.0'
               for rows in endpoints.values() for row in rows)
    error_path = t.GAME / 'addons/sourcemod/logs/errors_20261025.log'
    error_text = error_path.read_text('utf-8', errors='replace') if error_path.exists() else ''
    errors = [line for line in error_text.splitlines() if '[HanZombieShelter.smx]' in line]
    runtime_log = t.GAME / 'local_test_runtime.log'
    log_text = runtime_log.read_text('utf-8', errors='replace') if runtime_log.exists() else ''
    animations = [line for line in log_text.splitlines()
                  if re.search(r'\b(?:bad sequence|invalid sequence|bad sequence number|no sequences)\b', line, re.I)]
    visual = read(t.REPORT / 'client_visual_gate.json')
    assert visual['clear_confirmed'] and t.sha(Path(visual['path'])) == visual['sha256']
    final = {'at': t.now(), 'status': 'formal_crafting_loaded_retained_with_boss_binding_warnings',
             'root': str(t.ROOT), 'game': str(t.GAME), 'address': '192.168.0.102:27169',
             'server_pid': session['pid'], 'controller_pid': session['controller_pid'], 'client_pid': clients[0]['Id'],
             'processes': processes, 'endpoints': endpoints, 'ready': True, 'kept_running': True,
             'window': t.minimize(), 'formal_sha256': expected, 'plugin_count': 28,
             'humans': 1, 'bots': 0, 'plugins': plugins, 'shelter_status': status,
             'native_status': native_status, 'formal_load_receipt': 'formal_hotload.json',
             'startup_session_hash_is_historical': session['main_sha256'],
             'visual': visual, 'stop_restore': read(t.REPORT / 'final_stop_restored.json'),
             'fixture_cleanup': 'final_fixture_cleanup.json', 'test_tools_cleanup': 'final_test_weapons_cleanup.json',
             'helper_cleanup': 'test_helpers_removed.json', 'sourcemod_error_entries': errors,
             'sourcemod_error_count': len(errors), 'sourcepawn_stack_trace_present': 'Exception reported:' in error_text,
             'animation_warning_count': len(animations),
             'limitation': 'Formal hotload replayed Han_OnGameStart once into the existing HZS round; native NPC/Boss rebinding is not natural-start acceptance. Three missing Boss logs are retained. No server/client restart, changelevel or NPC respawn was used for finalization.'}
    t.write(t.REPORT / 'final_session.json', final)
    summary = {'at': t.now(), 'formal_sha256': expected, 'production_inputs': 50, 'compiler_warnings': 0,
               'offline_vm': {'passed': 147, 'failed': 0},
               'knife_config_static': {'passed': 86, 'failed': 0},
               'power_config_static': {'passed': 59, 'failed': 0},
               'final_power_engine_diagnostic': {'passed': 75, 'failed': 0,
                   'includes': '73 accumulated business assertions plus 2 cleanup assertions; do not add nested subgroups again'},
               'real_knife_work': {'passed': 20, 'failed': 0, 'synthetic_damage': False},
               'real_ui': ['aimed E root', 'axe wood50 iron20', 'wood100 -> iron1',
                           'unpowered enabled-menu confirmation refuses without debit', 'powered Lv1->2 costs and power50->80'],
               'old_zero_power_ui_history': 'Nine real UI upgrades Lv1->10; preserved as historical configuration, not final power acceptance.',
               'preservation': preservation, 'final_session': 'final_session.json',
               'retained_runtime_warnings': errors,
               'scope': 'One real human; controlled diagnostic states and actual production functions. No multiplayer economy/balance or full natural lifecycle claim.',
               'early_failures_preserved': ['zero-offset placement', 'expired UI confirmation/record guard',
                    'wrong active custom weapon and coalesced attack input', 'initial client launch timeout',
                    'formal hotload Boss missing warnings'],
               'package_report': str(t.BUILD.parent / 'package_verification.json')}
    t.write(t.REPORT / 'verification_summary.json', summary)
    for name in ('final_session.json', 'verification_summary.json', 'final_preservation.json', 'final_config_deployment.json'):
        shutil.copy2(t.REPORT / name, t.BUILD.parent / name)
    print(json.dumps({'at': final['at'], 'formal_sha256': expected, 'server': final['server_pid'],
                      'client': final['client_pid'], 'preserved_baseline': preservation['unchanged_existing_count'],
                      'changed_existing': len(changes), 'boss_warning_count': len(errors)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
