"""Read-only patch-address diagnostic on the empty isolated server.

No client connection, BOT, NPC creation, byte patch or detour. This temporary
owned diagnostic process exits; the ordinary user's Steam client stays open.
"""
import json
from pathlib import Path
import secrets
import subprocess
import sys
import time
from local_server_runtime import startup_arguments, retired_local_runner
from test_v05_real_client import ISOLATED, REPO, RCON, matching_processes, windows_processes

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
retired_local_runner()

report = REPO / 'tests/reports/v05/followup/patch_diagnostic'
report.mkdir(parents=True, exist_ok=True)
if matching_processes(windows_processes(), ISOLATED / 'srcds_win64.exe'):
    raise SystemExit('Isolated server is already running')
plugins = sorted(p.name for p in (ISOLATED / 'cstrike/addons/sourcemod/plugins').glob('*.smx'))
if plugins != ['UnmodifiedHZS.smx', 'hzsh_patch_diagnostic.smx']:
    raise SystemExit('Diagnostic requires exactly native HZS and the read-only diagnostic SMX')
secret = secrets.token_hex(24)
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
process = subprocess.Popen(startup_arguments(ISOLATED, 27139, 'zsh_codex_test.cfg', secret),
                           cwd=ISOLATED, startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE)
result = {'pid': process.pid, 'client_connected_by_runner': False, 'bots_added': 0,
          'npc_or_fake_client_created': False, 'records': []}
connection = None
try:
    for _ in range(50):
        if process.poll() is not None:
            break
        try:
            connection = RCON(27139, secret)
            break
        except (OSError, ConnectionError):
            time.sleep(0.4)
    if connection:
        for command in ('sm plugins list', 'status'):
            result['records'].append({'command': command, 'output': connection.command(command)})
        time.sleep(1)
        connection.command('quit')
        process.wait(timeout=10)
finally:
    if connection:
        connection.close()
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=10)
    result['exit_code'] = process.poll()
    source = ISOLATED / 'cstrike/addons/sourcemod/logs/hzsh_patch_diagnostic.log'
    if source.is_file():
        result['steps'] = source.read_text(encoding='utf-8', errors='replace')
    (report / 'session.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))
