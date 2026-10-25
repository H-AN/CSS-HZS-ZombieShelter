"""Read-only final preservation audit; writes receipts, never game files."""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import subprocess

from local_test_environment import TEST_ROOT
from package_v082_boss_spawnpoints import ROOT, REPORT, FORMAL, PLUGIN, sources


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text('utf-8-sig'))


def write(path, row):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(row, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    baseline = read(REPORT / 'baseline.json')
    build = read(REPORT / 'build/build_receipt.json')
    vm = read(REPORT / 'boss_spawnpoints_vm_verification.json')
    maps = read(REPORT / 'map_migration_candidates.json')
    package = read(REPORT / 'package_verification.json')
    assert vm['passed'] and vm['checks_failed'] == 0
    assert package['passed'] and package['formal_sha256'] == build['formal_sha256']
    assert sha(ROOT / package['zip']) == package['zip_sha256']
    assert sha(FORMAL) == sha(ROOT / PLUGIN) == build['formal_sha256']
    assert {name: hashlib.sha256(blob).hexdigest() for name, blob in sources().items()} == build['production_inputs']
    for name in ('BossSpawns.inc', 'BossBridge.inc'):
        assert sha(ROOT / 'addons/sourcemod/scripting/HanZombieShelter' / name) == vm['production_source_sha256'][name + ':whole_include']
    changed = [row['path'] for row in baseline['repo_files'] if sha(ROOT / row['path']) != row['sha256']]
    allowed = {PLUGIN, 'addons/sourcemod/scripting/HanZombieShelter.sp',
               'addons/sourcemod/configs/HanZombieShelter/maps/zsh_desertplant_cso_v1_d.cfg'}
    allowed.update('addons/sourcemod/scripting/HanZombieShelter/' + name + '.inc'
                   for name in ('Types', 'Config', 'Commands', 'NPC'))
    assert set(changed) <= allowed, changed
    live_changes = [row['path'] for row in baseline['live_files'] if sha(TEST_ROOT / row['path']) != row['sha256']]
    assert not live_changes, live_changes
    live_main = TEST_ROOT / 'cstrike' / PLUGIN
    assert sha(live_main) == '3920d6fcc9a5f4a490255153dd91a044ce532f637c3c6b066c050945d2dcfab1'
    query = '@(Get-Process srcds_win64,cstrike_win64 -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,Path) | ConvertTo-Json -Compress'
    result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', query], stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, encoding='utf-8', check=True)
    processes = json.loads(result.stdout or '[]')
    if isinstance(processes, dict):
        processes = [processes]
    timestamp = datetime.now(timezone(timedelta(hours=8))).isoformat()
    summary = {'at': timestamp, 'status': 'implemented_and_offline_verified_pending_position_selection_and_live_application',
               'version': '0.8.2', 'revision': 'boss_spawnpoints', 'formal_sha256': build['formal_sha256'],
               'production_inputs': build['production_input_count'], 'compiler_warnings': build['warnings'],
               'offline_production_vm': {'passed': vm['checks_passed'], 'failed': vm['checks_failed'],
                   'fixture_warning': '203: unused BossSpawnHullClear; no physics or live-engine test was performed'},
               'migration_tool_tests': {'passed': maps['tool_unit_tests_passed'], 'failed': maps['tool_unit_tests_failed']},
               'candidate_maps': maps['candidate_count'], 'candidate_maps_verified': maps['candidate_check_only_passed'],
               'repo_baseline_files': len(baseline['repo_files']), 'changed_existing_repo_files': changed,
               'unchanged_repo_baseline_files': len(baseline['repo_files'])-len(changed),
               'new_production_modules': ['BossSpawns.inc', 'BossBridge.inc'],
               'live_baseline_files': len(baseline['live_files']), 'live_changed_files': live_changes,
               'live_formal_sha256_unchanged': sha(live_main), 'package': package,
               'package_tool_smoke': read(REPORT / 'package_tool_smoke.json'),
               'game_commands_sent': False, 'live_deployed': False, 'new_safe_positions_selected': False,
               'limits': ['Old coordinates and duplicate placeholders only; no five-safe-point claim.',
                          'VM uses emulated KeyValues and engine natives; no physical overlap, pathfinding or live spawning acceptance.',
                          'Native HZS CSV and shared NPC budget still constrain counts; five points are not a five-Boss cap.']}
    state = {'at': timestamp, 'scope': 'Read-only process/file snapshot, no RCON or gameplay commands',
             'processes': processes, 'live_main_sha256': sha(live_main),
             'repo_new_main_sha256': build['formal_sha256'], 'gameplay_stage_sampled': False,
             'live_deployed': False, 'new_safe_positions_selected': False,
             'note': 'User-owned processes and current gameplay were preserved. Do not infer current day, Boss state or inventory from previous reports.'}
    write(REPORT / 'verification_summary.json', summary)
    write(REPORT / 'final_session.json', state)
    for name, row in (('verification_summary.json', summary), ('final_session.json', state)):
        write(TEST_ROOT / 'reports/v082_boss_spawnpoints_20261025' / name, row)
    print(json.dumps({'source': build['formal_sha256'], 'vm_passed': vm['checks_passed'],
                      'unchanged_live_files': len(baseline['live_files']), 'changed_repo_files': len(changed),
                      'processes': processes, 'live_deployed': False}, ensure_ascii=False))


if __name__ == '__main__':
    main()
