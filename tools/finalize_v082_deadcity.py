"""Read-only evidence consolidation after deadcity route tests and formal restart."""
import json
import re
import runpy

import run_v082_deadcity_routes as driver

r = driver.r


def load(name):
    return json.loads((r.LOCAL / name).read_text('utf-8'))


def main():
    damage = r.REPO / 'tests/reports/v082_boss_damage_20261025'
    build = json.loads((damage / 'formal_build.json').read_text('utf-8'))
    identity = build['sha256']
    assert identity == 'a02f00dcbe0ae4c054d83bda3b018da65ca1671ba3a9035783be5e1b67e06ae0'
    assert len(build['production_inputs']) == 52
    assert all(r.t.sha(r.REPO / name) == sha for name, sha in build['production_inputs'].items())
    assert all(r.t.sha(path) == identity for path in (
        damage / 'build/HanZombieShelter.smx', r.REPO / 'addons/sourcemod/plugins/HanZombieShelter.smx',
        r.t.GAME / 'addons/sourcemod/plugins/HanZombieShelter.smx'))
    baseline = json.loads((r.REPO / 'tests/reports/v082_deadcity_setup_20261025/before_switch.json').read_text('utf-8'))
    changes = []
    for row in baseline['live_files']:
        path = r.t.ROOT / row['path']
        current = r.t.sha(path) if path.exists() else None
        if current != row['sha256']:
            changes.append({**row, 'new_sha256': current})
    assert {x['path'] for x in changes} == {
        'cstrike/addons/sourcemod/plugins/HanZombieShelter.smx',
        'cstrike/addons/sourcemod/configs/HanZombieShelter/maps/zsh_deadcity_han_dn.cfg',
        'cstrike/addons/sourcemod/configs/HanZombieScenario/HanZombieScenarioZombieData.cfg'}
    old_build = json.loads((r.REPO / 'tests/reports/v082_boss_routes_20261025/formal_build.json').read_text('utf-8'))
    changed_sources = [name for name, sha in old_build['production_inputs'].items() if build['production_inputs'][name] != sha]
    assert set(changed_sources) == {'addons/sourcemod/scripting/HanZombieShelter/Combat.inc', 'addons/sourcemod/scripting/HanZombieShelter/NPC.inc'}
    actual_map = r.t.GAME / 'addons/sourcemod/configs/HanZombieShelter/maps/zsh_deadcity_han_dn.cfg'
    stage = r.t.GAME / 'addons/sourcemod/configs/HanZombieScenario/zsh_deadcity_han_dn/HanZombieScenarioStage.cfg'
    assert r.t.sha(actual_map) == r.t.sha(r.LOCAL / 'config/formal/zsh_deadcity_han_dn.cfg')
    assert r.t.sha(stage) == r.t.sha(r.LOCAL / 'config/source_stage_before.cfg')
    capture = r.LOCAL / 'user_captured_points.cfg'
    assert r.t.sha(capture) == r.t.sha(r.t.GAME / 'addons/sourcemod/data/zsh_boss_points/zsh_deadcity_han_dn.cfg')
    assert r.t.sha(capture) == r.t.sha(r.REPO / 'server-support/map-point-updates/zsh_deadcity_han_dn/BossSpawnPoints.cfg')
    assert not list((r.t.GAME / 'addons/sourcemod/plugins/tests').glob('*.smx'))
    preservation = {'at': r.t.now(), 'audited_live_files': len(baseline['live_files']),
        'changed': changes, 'unchanged': len(baseline['live_files']) - len(changes),
        'changed_production_inputs_since_374': changed_sources, 'unchanged_production_inputs': 50,
        'native_stage_original_bytes_restored': True, 'original_single_boss_restored': True,
        'user_point_coordinates_changed': False, 'HZS_source_API_gamedata_other_plugins_unchanged': True,
        'native_damage_config_changes_authorized': 'Only boss_pangzi/boss_angela/boss_yixing damage 10 -> 30',
        'local_startup_tools_audited_separately': '../v082_deadcity_setup_20261025/startup_tools_update.json'}
    r.record('preservation_verification', preservation)
    parse = runpy.run_path(str(r.REPO / 'tools/migrate_v05_materials.py'))['parse']
    snapshot = parse((r.LOCAL / 'formal_user_snapshot.cfg').read_text('utf-8-sig'))['V082UserSnapshot']
    player = snapshot['player']
    assert player['takedamage'] == '2' and int(player['flags']) & 16384 == 0 and player['move_type'] == '2'
    assert all(row['amount'] == '0' for row in player['Materials'].values())
    config = parse((r.t.GAME / 'addons/sourcemod/configs/HanZombieScenario/HanZombieScenarioZombieData.cfg').read_text('utf-8-sig'))['zombies']
    assert all(config[key]['damage'] == '30' for key in ('boss_pangzi', 'boss_angela', 'boss_yixing'))
    engine = load('final_engine_status.json')
    shelter = load('final_shelter_status.json')
    plugins = load('final_plugins.json')
    assert '1 humans, 0 bots' in engine['output'] and 'zsh_deadcity_han_dn' in engine['output']
    assert 'enabled=1 ready=1' in shelter['output'] and 'bases=6/6' in shelter['output']
    assert 'guard_01 hp=50000 awake=0 defeated=0 bound=1 positioned_hp=50000' in shelter['output']
    assert 'Listing 28 plugins' in plugins['output']
    control = r.t.controller('--status')
    processes = r.t.processes()
    assert control['server_running'] and control['controller_running']
    assert len([p for p in processes if p['ProcessName'] == 'srcds_win64']) == 1
    assert len([p for p in processes if p['ProcessName'] == 'cstrike_win64']) == 1
    window = r.t.minimize()
    errors = (r.t.GAME / 'addons/sourcemod/logs/errors_20261025.log').read_text('utf-8', errors='replace')
    recent = [line for line in errors.splitlines() if (m := re.match(r'L 10/25/2026 - (\d\d:\d\d:\d\d):', line)) and m[1] >= '20:18:35']
    assert not recent, recent
    runtime = (r.t.GAME / 'local_test_runtime.log').read_text('utf-8', errors='replace')
    formal_tail = runtime[runtime.index('L 10/25/2026 - 20:18:36:'):]
    assert 'Boss positioned guard_01 hp=50000 dormant=1 attempts=2' in formal_tail
    empty = load('formal_empty_pending_after_30s.json')
    joined = load('formal_after_client_join.json')
    assert 'epoch=2' in empty['output'] and 'positioned_hp=0' in empty['output']
    assert 'epoch=2' in joined['output'] and 'positioned_hp=50000' in joined['output']
    r.record('cold_start_runtime_verification', {'at': r.t.now(), 'passed': True, 'formal_sha256': identity,
        'session_id': control['session']['session_id'], 'same_epoch_before_and_after_client_join': 2,
        'empty_server_receipt': 'formal_empty_engine_status.json',
        'empty_after_30_seconds_receipt': 'formal_empty_pending_after_30s.json',
        'after_client_join_receipt': 'formal_after_client_join.json',
        'native_log_success': 'Boss positioned guard_01 hp=50000 dormant=1 attempts=2',
        'new_errors': 0, 'old_50_attempts_at_zero_game_time_failures_preserved': True,
        'boss_created_by_natural_HZS_start': True, 'test_command_created_or_revived_NPC': False})
    final = {'at': r.t.now(), 'main_sha256': identity, 'production_inputs': 52,
        'server_pid': control['session']['pid'], 'controller_pid': control['session']['controller_pid'],
        'client_pid': next(p['Id'] for p in processes if p['ProcessName'] == 'cstrike_win64'),
        'session_id': control['session']['session_id'], 'map': 'zsh_deadcity_han_dn',
        'address': control['environment']['address'], 'ready': True, 'kept_running': True,
        'window_minimized': window['minimized_or_hidden'], 'humans': 1, 'bots': 0, 'plugins': 28,
        'bases': 6, 'bosses': 1, 'formal_boss_id': 'guard_01', 'boss_hp': 50000, 'boss_awakened': False,
        'game_state_sample_at': shelter['at'], 'natural_state_may_change_after_sample': True,
        'player_snapshot_unix_time': int(snapshot['unix_time']),
        'player_sample': {key: player[key] for key in ('alive','hp','takedamage','flags','move_type','gravity_raw')},
        'temporary_materials_remaining_at_player_sample': 0, 'buildings_at_player_sample': len(snapshot['Buildings']),
        'helpers_remaining': [], 'zombies_restored_receipt': 'formal_zombies_restored.json',
        'visual_reviewed_clear': 'deadcity_formal_retained_01_capture.json',
        'new_SM_error_entries_since_formal_start': 0,
        'new_animation_warning_count': len(re.findall(r'(?i)bad sequence|invalid sequence|animation error', formal_tail)),
        'historical_errors_retained': True, 'other_performance_or_precache_warnings_not_claimed_fixed': True,
        'three_native_Boss_base_melee_config_values': 30, 'native_config_read_on_new_map_start': True,
        'Boss_native_melee_day_scaling_deployed': True, 'Boss_building_damage_fixed': 120,
        'Boss_skill_damage_changed': False, 'new_melee_day_damage_test_scope': '298 offline production-function checks; not a new actual-AI day-by-day damage measurement',
        'new_damage_formal_three_way_identity_verified': True}
    r.record('final_session', final)
    for folder in (damage, r.t.ROOT / 'reports/v082_boss_damage_20261025'):
        r.t.write(folder / 'final_application.json', final)
    print(json.dumps(final, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
