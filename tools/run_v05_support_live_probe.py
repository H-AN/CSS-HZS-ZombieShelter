"""Observe existing native-pool admission, then test one disposable base explosion.

Reuses the fixed isolated startup, normal Steam client, hijack and RCON workflow.
Never adds or revives a BOT, fake client, or NPC. Leaves both processes open.
"""
from types import SimpleNamespace
from pathlib import Path
import argparse
import sys

from run_v05_followup_probe import FollowupProbeSession


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-dir', required=True)
    options = parser.parse_args()
    session = FollowupProbeSession(SimpleNamespace(
        report_dir=options.report_dir, connect_timeout=180, observe_seconds=3,
        reuse_client=True, supporting_fixes=False))
    code = 0
    population_started = False
    try:
        session.start_and_leave_running()
        output = session.rcon('zsh_test v05_population_live_start')
        if 'SKIP' in output:
            raise AssertionError('Population fixture did not start; see report')
        population_started = True
        reached = False
        for attempt in range(25):
            session.wait(10)
            output = session.rcon('zsh_test v05_population_live_poll')
            if 'FAILED' in output:
                raise AssertionError('Natural admission fixture failed; see report')
            if 'HOLD_REACHED' in output:
                reached = True
                break
        if not reached:
            raise AssertionError('Both natural active population targets were not observed')
        session.screenshot('night_250_observed')
        session.rcon('zsh_test v05_population_live_cleanup')
        population_started = False
        session.rcon('zsh_test v05_base_explosion_live')
        session.wait(1)
        session.screenshot('base_a_exploded_grid_off')
        output = session.rcon('zsh_test summary')
        if 'Failed=0' not in output or 'Skipped=0' not in output:
            raise AssertionError('Live supplement did not finish without failures/skips')
        session.results['status'] = 'live_population_and_real_base_explosion_completed'
        session.results['supplement_completed'] = True
        session.record('support_live_completed', kept_running=True)
    except Exception as error:
        code = 1
        session.results['status'] = 'support_live_not_complete; processes retained'
        session.results['supplement_completed'] = False
        session.record('error', error_type=type(error).__name__, message=str(error))
        print(type(error).__name__, str(error), flush=True)
    finally:
        if population_started:
            try:
                session.rcon('zsh_test v05_population_live_cleanup')
            except Exception as error:
                session.record('population_cleanup_error', message=str(error))
        session.release_python_only()
    print('Report:', Path(options.report_dir) / 'session.json', flush=True)
    return code


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    raise SystemExit(main())
