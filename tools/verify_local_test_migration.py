"""Read-only migration acceptance plus the required normal zombie-resume command."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import contextlib
import hashlib
import importlib.util
import io
import json
import re
import subprocess
import time
from local_test_environment import TEST_ROOT, GAME_ROOT, CONTROLLER, SERVER_EXE


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    report_dir = TEST_ROOT / 'reports/migration_20261024'
    spec = importlib.util.spec_from_file_location('canonical_migration_runtime', CONTROLLER)
    runtime = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runtime)
    state = runtime.session()
    if not state or not state.get('ready'):
        raise RuntimeError('The canonical session has not become ready.')
    checks = []

    def check(name, passed, detail=None):
        checks.append({'name': name, 'passed': bool(passed), 'detail': detail})

    def command(text):
        with contextlib.redirect_stdout(io.StringIO()) as capture:
            runtime.command(text)
        result = json.loads(capture.getvalue())
        if not result.get('ok'):
            raise RuntimeError(result)
        return result['output']

    replies = {text: command(text) for text in ['status', 'sm plugins list', 'meta list', 'sm_zsh_status',
                                               'sm_zsh_skills_status', 'sm_zsh_recording_status', 'path']}
    manifest_path = report_dir / 'migration_manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    manifest['controller_sha256'] = sha(CONTROLLER)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    processes = runtime.server_processes()
    check('one_owned_server', len(processes) == 1 and processes[0]['Id'] == state['pid']
          and Path(processes[0]['Path']).resolve() == SERVER_EXE.resolve(), processes)
    endpoint_result = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
        '@(Get-NetUDPEndpoint -LocalPort 27169 -ErrorAction Stop | Select-Object LocalAddress,LocalPort,OwningProcess) | ConvertTo-Json -Compress'],
        capture_output=True, text=True, encoding='utf-8', check=True)
    endpoints = json.loads(endpoint_result.stdout)
    if not isinstance(endpoints, list):
        endpoints = [endpoints]
    check('sole_27169_listener_owned', len(endpoints) == 1 and endpoints[0]['LocalAddress'] == '0.0.0.0'
          and endpoints[0]['OwningProcess'] == state['pid'], endpoints)
    check('fixed_game_directory', Path(state['game']).resolve() == GAME_ROOT.resolve())
    check('ready_and_kept_running', state.get('ready') and state.get('kept_running'))
    check('correct_map', 'zsh_desert_han_dn' in replies['status'])
    check('zero_bots', bool(re.search(r'\b0 bots\b', replies['status'])), replies['status'])
    check('five_formal_plugins', 'Listing 5 plugins' in replies['sm plugins list'], replies['sm plugins list'])
    expected = {'HanWeaponSystem.smx', 'HanZombieShelter.smx', 'HanZombieShelterEnvironment.smx',
                'UnmodifiedHZS.smx', 'hzsh_v07_recording_access.smx'}
    active = {p.name for p in (GAME_ROOT / 'addons/sourcemod/plugins').glob('*.smx')}
    check('five_formal_smx_files', active == expected, sorted(active))
    helpers = list((GAME_ROOT / 'addons/sourcemod/plugins/tests').glob('*.smx'))
    check('no_test_helpers', not helpers, [str(p) for p in helpers])
    check('main_plugin_identity', sha(GAME_ROOT / 'addons/sourcemod/plugins/HanZombieShelter.smx') ==
          '59218e6e94f1df6bf12b1eb26075fee894a6d4aa05ed91b710a194bf44aa1e3f')
    check('shelter_ready', 'enabled=1 ready=1' in replies['sm_zsh_status'])
    check('current_skill_defaults_loaded', 'configured=2 effective=2 override=-1' in replies['sm_zsh_skills_status'])
    config = [row for row in manifest['original_files']
              if '/cfg/' in '/' + row['relative'] or '/configs/' in '/' + row['relative']]
    changed = [row['relative'] for row in config if sha(TEST_ROOT / row['relative']) != row['sha256']]
    check('all_original_configuration_bytes_preserved', len(config) == 68 and not changed,
          {'count': len(config), 'changed': changed})
    plugin_rows = [row for row in manifest['original_files'] if row['relative'].endswith('.smx')]
    check('all_existing_plugin_binaries_preserved', all(sha(TEST_ROOT / row['relative']) == row['sha256'] for row in plugin_rows))
    gameinfo = (GAME_ROOT / 'gameinfo.txt').read_text(encoding='utf-8')
    check('no_legacy_or_client_search_paths', not any(text in gameinfo for text in
          ['preview_update', '制作计划', 'SteamLibrary', 'release/cstrike', 'official_support']))
    paths = replies['path'].replace('\\', '/').lower()
    check('engine_mounts_canonical_environment', str(GAME_ROOT).replace('\\', '/').lower() in paths)
    check('engine_has_no_retired_game_search_path', 'preview_update/runtime/server' not in paths)
    with contextlib.redirect_stdout(io.StringIO()) as capture:
        runtime.start()
    repeated = json.loads(capture.getvalue())
    check('repeat_start_reuses_same_server', repeated.get('already_running') and repeated['pid'] == state['pid'])
    minimized = runtime.minimize_owned_server_window(state['pid'])
    check('owned_server_minimized', minimized)
    restored = command('hzs_stopzombie 0')
    log = GAME_ROOT / 'local_test_runtime.log'
    check('runtime_log_is_in_new_game', log.is_file(), str(log))
    animation = []
    if log.exists():
        for line in log.read_text(encoding='utf-8', errors='replace').splitlines():
            if re.search(r'(?:invalid|unknown|missing|not found).*\b(?:sequence|animation)\b|\b(?:sequence|animation)\b.*(?:invalid|unknown|not found)', line, re.I):
                if not re.search('password|rcon', line, re.I):
                    animation.append(line)
    check('no_new_animation_warnings', not animation, animation)
    today = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d')
    errors = []
    for path in (GAME_ROOT / 'addons/sourcemod/logs').glob('errors_' + today + '*.log'):
        errors.append({'name': path.name, 'bytes': path.stat().st_size})
    check('no_current_sourcemod_error_log', not errors, errors)
    for name in ['tool_entry_verification.json', 'retired_legacy_tools.json', 'independent_file_audit.json']:
        path = report_dir / name
        report = json.loads(path.read_text(encoding='utf-8-sig')) if path.exists() else {}
        supporting_passed = report.get('passed', report.get('all_guards_verified'))
        check('supporting_audit:' + name, supporting_passed is True,
              {'path': str(path), 'passed': supporting_passed})
    result = {'at': datetime.now(timezone(timedelta(hours=8))).isoformat(), 'session': state,
              'scope': 'File migration, engine path and formal-plugin startup only; no ordinary client or gameplay regression performed.',
              'checks': checks, 'counts': {'passed': sum(x['passed'] for x in checks), 'failed': sum(not x['passed'] for x in checks)},
              'replies': replies, 'official_zombies_resumed': {'command': 'hzs_stopzombie 0', 'receipt': restored},
              'kept_running': True, 'passed': all(x['passed'] for x in checks),
              'initial_failed_attempt': 'initial_boot_missing_valve_rc.json retained. The stock cstrike VPK GAME+MOD path ID was restored before a new process launch; no loose valve.rc was added.'}
    (report_dir / 'verification_summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'passed': result['passed'], 'counts': result['counts'], 'server_pid': state['pid'],
                      'controller_pid': state['controller_pid'], 'game': state['game'],
                      'failed_checks': [x for x in checks if not x['passed']]}, ensure_ascii=False))
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
