"""Start the exact v0.5 follow-up probe without engine patches and keep it live.

The one existing fixed-path Steam client is reused. This profile is explicitly
integration_probe; the formal manual-review launcher's default remains formal.
"""
from __future__ import annotations

import argparse
import sys

from start_v05_manual_review import ManualReviewSession
from test_v05_real_client import REPO, load_steps


class FollowupProbeSession(ManualReviewSession):
    def __init__(self, options):
        super().__init__(options)
        self.results.update({'purpose': 'v0.5 follow-up integration probe; preserve server and client',
                             'scope': 'integration_probe, one existing ordinary Steam client, no BOTs',
                             'main_profile': 'integration_probe', 'formal_build': False,
                             'deferred_api_travel': 'API consumer is loaded separately after the two-plugin startup profile'})

    def expected_main_source(self):
        return REPO / 'tests/compiled/ShelterIntegrationProbe.smx'

    def profile_label(self):
        return 'integration_probe'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-dir', default=str(REPO / 'tests/reports/v05/followup_probe'))
    parser.add_argument('--connect-timeout', type=float, default=180)
    parser.add_argument('--observe-seconds', type=float, default=3)
    parser.add_argument('--rcon-script', help='Optional additional validated v0.5 JSON or newline script steps')
    options = parser.parse_args(argv)
    if not 20 <= options.connect_timeout <= 600 or not 0 <= options.observe_seconds <= 120:
        parser.error('Connect timeout 20..600 seconds; ready observation 0..120 seconds')
    options.reuse_client = True
    options.supporting_fixes = False
    extra = load_steps(options.rcon_script)
    steps = [{'rcon': 'zsh_test ' + case} for case in (
        'v05_rotation_math', 'v05_fence_logic', 'v05_fence_real',
        'v05_npc_followup_logic', 'v05_npc_followup_real')]
    steps += extra
    steps.append({'rcon': 'zsh_test summary', 'contains': 'Failed=0'})
    session = FollowupProbeSession(options)
    code = 0
    try:
        session.start_and_leave_running()
        session.record('followup_scope', main_profile='integration_probe', requested_steps=steps)
        session.execute(steps)
        session.screenshot('followup_probe_completed')
        session.results['status'] = 'followup_probe_completed; integration_probe_server_and_client_kept_running'
        session.results['followup_cases_completed'] = True
        session.record('followup_completed', kept_running=True)
    except Exception as error:
        code = 1
        session.results['status'] = 'followup_not_complete; any_started_processes_kept_for_diagnosis'
        session.results['followup_cases_completed'] = False
        message = str(error).replace(session.secret, '[redacted]')
        session.record('error', error_type=type(error).__name__, message=message)
        print('[V05 follow-up probe]', type(error).__name__, message, flush=True)
    finally:
        session.release_python_only()
    print('[V05 follow-up probe] Report:', session.report / 'session.json', flush=True)
    print('[V05 follow-up probe] Integration profile kept running:', session.results['kept_running'],
          'server PID:', session.results['startupPID'], 'client PIDs:', session.results['clientPIDs'], flush=True)
    return code


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    raise SystemExit(main())
