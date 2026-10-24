"""Control the already running 27139 review session through client hijack/RCON.

Uses the client's existing in-memory credentials, never password files. Leaves
the server and the ordinary Steam client running after each operation.
"""
from pathlib import Path
import argparse
import json
import re
import subprocess
import time
import uuid

from test_v05_real_client import CLIENT, ISOLATED, REPO, matching_processes, windows_processes
from local_server_runtime import retired_local_runner


def control(commands, report=None, authorized_restart=False):
    retired_local_runner()
    for command in commands:
        if authorized_restart and command in ('quit','sm plugins unload HanZombieShelter','sm plugins unload UnmodifiedHZS'):
            continue
        if any(c in command for c in ';\n\r\x00') or re.search(
                r'(?i)(?:\bbot_|fakeclient|respawn|password|\bquit\b|\bdisconnect\b|\bchangelevel\b|^map\b|sm_zsh_fail)', command):
            raise ValueError('This live-review helper refuses shutdown, BOT, respawn, password and map commands')
        if not re.match(r'^(?:status\b|sm\s|sm_zsh_\w+\b|sm_hzsh_\w+\b|hzsh_\w+\b|zsh_\w+\b)', command):
            raise ValueError('Expected a review status, plugin-management or explicit shelter command')
    rows = windows_processes()
    if len(matching_processes(rows, ISOLATED / 'srcds_win64.exe')) != 1:
        raise RuntimeError('Exactly one owned isolated server must already be running')
    if len(matching_processes(rows, CLIENT / 'cstrike_win64.exe')) != 1:
        raise RuntimeError('Exactly one normal CSS client must already be running')
    run = uuid.uuid4().hex[:12]
    basename = 'zsh05_control_' + run
    paths = []

    def hijack(label, body):
        path = CLIENT / 'cstrike/cfg' / (basename + '_' + label + '.cfg')
        if path.exists():
            raise FileExistsError(path)
        path.write_text(body, encoding='utf-8')
        paths.append(path)
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        subprocess.run([str(CLIENT / 'cstrike_win64.exe'), '-hijack', '-game', 'cstrike', '+exec', path.name],
                       cwd=CLIENT, startupinfo=startup, capture_output=True, check=True, timeout=20)

    log = CLIENT / 'cstrike' / (basename + '.log')
    try:
        hijack('commands', 'con_logfile "' + log.name + '"\n' +
               '\n'.join('rcon ' + command for command in commands) + '\nhideconsole\n')
        time.sleep(4)
        hijack('finish', 'con_logfile ""\nhideconsole\n')
        time.sleep(1)
        output = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
        if not output:
            raise RuntimeError('No client RCON console log was captured')
        result = {'commands': commands, 'output': output,
                  'server_and_client_kept_running': not authorized_restart,
                  'client_kept_running': True, 'user_authorized_server_restart': authorized_restart,
                  'client_console_log': str(log)}
        if report:
            report = Path(report)
            report.parent.mkdir(parents=True, exist_ok=True)
            report.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        return result
    finally:
        for path in paths:
            if path.exists():
                path.unlink()


if __name__ == '__main__':
    import sys
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rcon', action='append', required=True)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--authorized-restart', action='store_true',
                        help='Use only after explicit user authorization to restart this test server; keeps the client open')
    options = parser.parse_args()
    print(json.dumps(control(options.rcon, options.report, options.authorized_restart), ensure_ascii=False, indent=2))
