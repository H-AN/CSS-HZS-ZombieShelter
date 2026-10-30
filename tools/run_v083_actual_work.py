"""Real client melee assertions; diagnostic preparation is labelled separately."""
import json
from pathlib import Path
import test_v083_live as live

def status(label):
    text = live.request('sm_v083_work_probe status', label)
    rows = [json.loads(line.split('] ', 1)[1]) for line in text.splitlines() if line.startswith('[V083WorkProbe] {')]
    return next(row for row in rows if row['kind'] == 'status')

def tap(label):
    result = live.legacy.tap('attack', label)
    live.write(label + '_tap.json', result)

def main():
    import sys
    import time
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    results = []
    initial = status('actual_work_initial')
    assert initial['classname'] == 'weapon_axe'
    stamp = str(time.time_ns())
    if '--resume-generator' not in sys.argv:
        assert initial['hp'] == 201 and initial['constructing']
        for expected, label in [(401, 'bench_second'), (500, 'bench_complete')]:
            tap(label)
            result = status(label + '_result')
            assert result['hp'] == expected and bool(result['constructing']) == (expected < 500), result
            results.append({'case': label, 'passed': True, 'hp': expected, 'actual_client_attack': True})
        live.request('sm_v083_work_probe power_setup', 'actual_power_setup')
    else:
        assert initial['hp'] == 500 and not initial['constructing']
    # HWS may put a newly crafted knife into slot2 while leaving a pistol active.
    live.legacy.client('use weapon_axe', 'generator_selected_' + stamp)
    selected = status('generator_selected_' + stamp)
    assert selected['active_weapon'] == selected['slot2']
    live.request('sm_v083_perf reset', 'actual_generator_perf_reset')
    for mode, amount in [('construction', 200), ('repair', 150)]:
        label = 'generator_' + mode + '_' + stamp
        prepared = live.request('sm_v083_probe generator_prepare ' + mode, label + '_prepare')
        assert 'hp=1' in prepared and 'no attack invoked' in prepared, prepared
        try:
            tap(label)
            result = live.request('sm_v083_probe generator_status', label + '_result')
            assert 'hp=' + str(1 + amount) + ' ' in result, result
            assert 'constructing=' + str(int(mode == 'construction')) in result, result
            results.append({'case': 'generator_' + mode, 'passed': True, 'actual_gain': amount, 'actual_client_attack': True})
        finally:
            live.request('sm_v083_probe generator_restore', label + '_restore')
    live.request('sm_v083_perf report', 'actual_generator_perf')
    live.request('sm_v083_probe indices', 'actual_indices_after_generator')
    live.write('actual_work_verification_' + stamp + '.json', {'at': live.legacy.now(), 'checks': results, 'passed': len(results), 'scope': 'one real client, protected fixture, real axe input; no multiplayer acceptance'})

if __name__ == '__main__':
    main()
