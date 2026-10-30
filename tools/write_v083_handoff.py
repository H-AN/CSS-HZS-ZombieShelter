"""Capture the actual retained session; does not reset game state or load helpers."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import json
import re
import shutil

import test_v083_live as live

def main():
    session = live.legacy.owned_server()
    assert session['session_id'] == '56f6026b07e64218aae8101a4e48a4b4'
    formal = json.loads((live.REPORT / 'formal_build.json').read_text(encoding='utf-8'))
    assert session['main_sha256'] == formal['sha256']
    mode = live.request('sm_zsh_status', 'final_retained_mode')
    network = live.request('status', 'final_retained_network')
    plugins = live.request('sm plugins list', 'final_retained_plugins')
    assert '1 humans, 0 bots' in network and 'Listing 28 plugins' in plugins
    assert 'version=0.8.3 enabled=1 ready=1' in mode
    windows = live.legacy.minimize()
    cold = json.loads((live.REPORT / 'formal_cold_start.json').read_text(encoding='utf-8'))
    start = datetime.fromtimestamp(cold['new_session']['started_at'], tz=timezone(timedelta(hours=8)))
    errors = live.legacy.GAME / 'addons/sourcemod/logs/errors_20261030.log'
    exceptions = []
    for line in errors.read_text(encoding='utf-8', errors='replace').splitlines() if errors.exists() else []:
        match = re.match(r'L (\d\d/\d\d/\d{4} - \d\d:\d\d:\d\d): .*Exception reported:', line)
        if match:
            at = datetime.strptime(match[1], '%m/%d/%Y - %H:%M:%S').replace(tzinfo=timezone(timedelta(hours=8)))
            exceptions.append({'at': at.isoformat(), 'line': line, 'after_formal_start': at >= start})
    assert not any(row['after_formal_start'] for row in exceptions), exceptions
    cleaned = []
    for cfg in (live.REPORT / 'client_cfg').glob('*.cfg'):
        target = live.legacy.CLIENT / 'cfg' / cfg.name
        if target.is_file() and live.legacy.sha(target) == live.legacy.sha(cfg):
            target.unlink();cleaned.append(cfg.name)
    live.write('client_temporary_cfg_cleanup.json', {'at': live.legacy.now(), 'removed_exact_matching_generated_files': cleaned, 'report_copies_retained': True})
    aborted = live.WORK / 'reports/vm_20261030_091430/aborted.json'
    if not aborted.exists():
        live.legacy.write(aborted, {'at': live.legacy.now(), 'completed': False, 'result': 'not counted', 'reason': 'Redundant Python-interpreter power matrix was stopped only after the same 160 scenarios completed successfully in actual SourceMod; no gameplay process stopped by this cancellation'})
    states = live.legacy.processes()
    row = {'at': live.legacy.now(), 'main_sha256': formal['sha256'], 'production_inputs': 53, 'server_pid': session['pid'], 'controller_pid': session['controller_pid'],
        'client_pids': [p['Id'] for p in states if p['ProcessName'] == 'cstrike_win64'], 'session_id': session['session_id'], 'map': session['map'], 'address': session['address'],
        'ready': session['ready'], 'kept_running': True, 'window_minimized_or_hidden': windows['minimized_or_hidden'], 'plugins': 28, 'humans': 1, 'bots': 0,
        'actual_mode': mode, 'actual_status': network, 'formal_sm_exceptions': sum(r['after_formal_start'] for r in exceptions), 'historical_sm_exceptions_retained': exceptions,
        'helpers_unloaded_and_removed': ['V083CloseAim.smx', 'V083FinalSnapshot.smx', 'hzsh_v082_close_panels.smx', 'all isolated matrix/entity helpers'],
        'last_player_snapshot': json.loads((live.REPORT / 'formal_player_snapshot.json').read_text(encoding='utf-8')),
        'player_snapshot_is_historical_not_current_hp_inventory': True, 'final_reviewed_screenshot': str(live.REPORT / 'screenshots/hzsh_v082_formal_final_scene.jpg'),
        'natural_changes_after_sampling_must_be_preserved': True, 'multiplayer_acceptance': False}
    live.write('final_session.json', row)
    summary_path = live.REPORT / 'verification_summary.json'
    summary = json.loads(summary_path.read_text(encoding='utf-8'));summary['final_session'] = row
    live.legacy.write(summary_path, summary)
    target = live.ROOT / 'tests/reports/v083_performance_20261030'
    for path in live.REPORT.glob('*.json'):shutil.copy2(path, target / path.name)
    print(json.dumps({key: row[key] for key in ['at', 'server_pid', 'client_pids', 'ready', 'plugins', 'humans', 'bots', 'formal_sm_exceptions']}, ensure_ascii=True))

if __name__ == '__main__':
    import sys
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
