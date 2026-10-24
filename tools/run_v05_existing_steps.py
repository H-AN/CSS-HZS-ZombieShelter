"""Run validated hijack/RCON steps on the existing 27139 review session.

Never starts, stops, reconnects or changes maps; credentials stay in the client.
"""
from __future__ import annotations

import argparse
import re
import sys
import time

from control_v05_running import control
from local_server_runtime import retired_local_runner
from test_v05_real_client import (
    CLIENT, ISOLATED, RealClientSession, load_steps, matching_processes,
    validate_rcon, windows_processes,
)


class ExistingSession(RealClientSession):
    def wait(self, seconds):
        time.sleep(seconds)
        rows = matching_processes(windows_processes(), ISOLATED / 'srcds_win64.exe')
        if len(rows) != 1 or rows[0]['ProcessId'] != self.results['startupPID']:
            raise RuntimeError('The original isolated server exited')

    def preflight(self):
        rows = windows_processes()
        servers = matching_processes(rows, ISOLATED / 'srcds_win64.exe')
        clients = matching_processes(rows, CLIENT / 'cstrike_win64.exe')
        if len(servers) != 1 or len(clients) != 1:
            raise RuntimeError('Exactly one fixed isolated server and normal client must already be running')
        self.results.update(startupPID=servers[0]['ProcessId'],
                            clientPIDs=[clients[0]['ProcessId']],
                            scope='existing isolated session, one real client, zero BOTs',
                            main_profile='integration_probe', kept_running=True)
        output = control(['status'])['output']
        if not re.search(r'players\s*:\s*1 humans, 0 bots', output):
            raise RuntimeError('The existing session must contain one real client and zero BOTs')
        self.record('existing_session_verified', output=output)

    def rcon(self, command, *, cleanup=False):
        validate_rcon(command)
        output = control([command])['output']
        self.record('rcon', command=command, output=output)
        print('[V05 existing]', command, output[:500], flush=True)
        if re.search(r'\b(?:FAIL|SKIP)\b', output) or 'Unknown command' in output:
            raise AssertionError('A fixture failed; see the recorded output')
        return output


def main():
    retired_local_runner()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-dir', required=True)
    parser.add_argument('--rcon-script', required=True)
    options = parser.parse_args()
    steps = load_steps(options.rcon_script)
    session = ExistingSession(options)
    code = 0
    try:
        session.preflight()
        session.execute(steps)
        session.results['status'] = 'completed; existing server and client kept running'
        session.record('steps_completed')
    except Exception as error:
        code = 1
        session.results['status'] = 'not_complete; existing server and client kept running'
        session.record('error', error_type=type(error).__name__, message=str(error))
        print(type(error).__name__, str(error), flush=True)
    finally:
        if any(step.get('client') == '+use' for step in steps):
            session.client_command('-use')
        for path in session.cfg_paths:
            if path.is_file():
                path.unlink()
        session.record('finished_without_process_or_connection_changes')
    return code


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    raise SystemExit(main())
