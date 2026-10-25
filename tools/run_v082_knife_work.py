"""Actual client melee against the exact diagnostic bench, never SDK damage.

Weapon delivery/charges are tested separately through production crafting.
Here the existing external commands supply each test tool, then a real client
use-command selects it. The fixture only prepares logical HP and construction
state; client attack input must cause the exact configured change.
"""
from __future__ import annotations

import json
from pathlib import Path
import sys

import test_v082_crafting_live as live

KNIVES = [('beijixing', 340), ('beijixing2', 340), ('chongsu', 340),
          ('xintiao', 250), ('xintiao2', 250), ('lianming', 320),
          ('longya', 320), ('zhuixing', 330), ('wange', 370), ('apexshuangren', 500)]


def request(text, label):
    output = live.command(text)
    live.write(live.REPORT / (label + '.json'), {'at': live.now(), 'command': text, 'output': output})
    return output


def status(label):
    rows = []
    for line in request('sm_v082_probe status', label).splitlines():
        if line.startswith('[V082Probe] {'):
            rows.append(json.loads(line[len('[V082Probe] '):]))
    return next(row for row in rows if row['kind'] == 'status')


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    report = live.REPORT / 'real_knife_work_verification.json'
    assert not report.exists(), 'Never overwrite the first real work session'
    initial = status('knife_work_initial_state')
    assert initial['active'] and initial['bench_uid'] > 0
    results = []
    summary = {'started': live.now(), 'scope': 'One real client; configured tools; explicit diagnostic HP1/max-level states; real attack input',
               'synthetic_damage': False, 'server_pid': live.owned_server()['pid'], 'checks': results}
    live.write(report, summary)
    for name, expected in KNIVES:
        give = 'sm_shuangren' if name == 'apexshuangren' else 'sm_' + name
        live.client(give, 'work_' + name + '_tool')
        live.client('use weapon_' + name, 'work_' + name + '_select')
        selected = status('work_' + name + '_selected')
        assert selected['classname'] == 'weapon_' + name and selected['active_weapon'] == selected['slot2'], selected
        for mode in ('construction', 'repair'):
            label = 'work_' + name + '_' + mode
            prepared = False
            try:
                output = request('sm_v082_probe work_prepare ' + mode, label + '_prepare')
                prepared = '"synthetic_damage_or_repair_invoked":false' in output
                assert prepared, output
                aimed = request('sm_v082_close_aim ' + str(initial['bench_uid']), label + '_aim')
                assert 'real_eye_hit_distance=' in aimed and 'no damage or repair invoked' in aimed, aimed
                live.tap('attack', label + '_tap')
                actual = status(label + '_result')
                passed = (actual['bench_uid'] == initial['bench_uid'] and actual['hp'] == 1 + expected
                          and actual['constructing'] == (1 if mode == 'construction' else 0)
                          and actual['classname'] == 'weapon_' + name and actual['active_weapon'] == actual['slot2'])
                row = {'knife': name, 'mode': mode, 'expected_gain': expected, 'actual_gain': actual['hp'] - 1, 'passed': passed}
                results.append(row)
                live.write(report, summary)
                print(json.dumps(row, ensure_ascii=False), flush=True)
                assert passed, row
            finally:
                if prepared:
                    restored = request('sm_v082_probe work_restore', label + '_restore')
                    assert 'restored the exact saved Building state' in restored, restored
    summary.update(completed=live.now(), passed=sum(row['passed'] for row in results),
                   failed=sum(not row['passed'] for row in results), all_passed=all(row['passed'] for row in results))
    live.write(report, summary)
    print(json.dumps({'report': str(report), 'passed': summary['passed'], 'failed': summary['failed']}), flush=True)


if __name__ == '__main__':
    main()
