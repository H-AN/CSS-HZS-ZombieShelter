"""Real-client hit and natural NPC motion for the remaining 5x3 route cases."""
import json
import time

import test_v082_boss_routes as r


def status(label):
    row = r.command('sm_v082_bosspath status', label)
    state = next(x for x in row['rows'] if x.get('kind') == 'state')
    assert state['active'] and state['running'] and not state['terminal'], state
    return row, state


def run():
    initial, start = status('batch_initial_status')
    assert start['stage'] in (0, 4)
    epoch, serial = start['epoch'], start['actor_serial']
    completed = {(x['id'], i+1) for x in initial['rows'] if x.get('kind') == 'boss_state'
                 for i, value in enumerate(x['point_results']) if value == 1}
    report = {'started': r.t.now(), 'epoch': epoch, 'actor_serial': serial,
              'preexisting_passed': sorted(completed), 'attempts': [], 'npc_motion_written_during_chase': False}
    r.record('real_route_batch', report)
    shot_count = 0
    for boss in ('boss_01', 'boss_02', 'boss_03'):
        for point in range(1, 6):
            if (boss, point) in completed:
                continue
            # Nearest base first; either base is sufficient for the user's goal.
            bases = ('B','A') if point == 3 else ('A','B')
            for attempt, base in enumerate(bases, 1):
                tag = f'route_{boss}_p{point}_{base.lower()}_{attempt}'
                print(json.dumps({'starting': tag}, ensure_ascii=False), flush=True)
                if shot_count % 5 == 0:
                    r.t.client('+reload', tag + '_reload_down')
                    time.sleep(.3)
                    r.t.client('-reload', tag + '_reload_up')
                prepared = r.command(f'sm_v082_bosspath prepare {boss} {point} 180', tag + '_prepare')
                failure = next((x for x in prepared['rows'] if x.get('kind') == 'scene_result'), None)
                if failure is not None:
                    report['attempts'].append(failure)
                    r.record('real_route_batch', report)
                    print(json.dumps(failure, ensure_ascii=False), flush=True)
                    break
                assert any(x.get('kind') == 'prepared' for x in prepared['rows']), prepared['output'][-1200:]
                for shot in range(1, 4):
                    r.t.tap('attack', tag + f'_hit_{shot}')
                    shot_count += 1
                    time.sleep(.35)
                    current, state = status(tag + f'_after_hit_{shot}')
                    assert state['epoch'] == epoch and state['actor_serial'] == serial
                    if state['stage'] == 2:
                        break
                    assert state['stage'] == 1, current['output'][-1600:]
                assert state['stage'] == 2, 'No observed real hit; stop batch without faking wake'
                returned = r.command('sm_v082_bosspath return_to_base ' + base, tag + '_return')
                assert any(x.get('kind') == 'chase_started' for x in returned['rows']), returned['output'][-1500:]
                deadline = time.monotonic() + 195
                poll = 0
                while time.monotonic() < deadline:
                    time.sleep(4)
                    poll += 1
                    current, state = status(tag + f'_poll_{poll:02}')
                    assert state['epoch'] == epoch and state['actor_serial'] == serial
                    if state['stage'] == 4:
                        break
                assert state['stage'] == 4, 'Fixture did not finish its bounded route'
                boss_state = next(x for x in current['rows'] if x.get('kind') == 'boss_state' and x['id'] == boss)
                sample = next(x for x in current['rows'] if x.get('kind') == 'status')
                passed = boss_state['point_results'][point-1] == 1
                result = {'boss': boss, 'point': point, 'return_base': base, 'passed': passed,
                          'arrived_core': sample['safe_core'], 'travel': sample['travel'],
                          'chase_seconds_at_poll': sample['chase_seconds'], 'body_ref': sample['body_ref'],
                          'scene': sample['scene'], 'status_receipt': tag + f'_poll_{poll:02}.json'}
                report['attempts'].append(result)
                r.record('real_route_batch', report)
                print(json.dumps(result, ensure_ascii=False), flush=True)
                if passed:
                    completed.add((boss, point))
                    break
    final, state = status('batch_final_status')
    report['completed'] = r.t.now()
    report['matrix'] = {x['id']: x['point_results'] for x in final['rows'] if x.get('kind') == 'boss_state'}
    report['combinations_passed'] = sum(value == 1 for values in report['matrix'].values() for value in values)
    report['combinations_not_passed'] = 15 - report['combinations_passed']
    r.record('real_route_batch', report)
    print(json.dumps({'completed': True, 'matrix': report['matrix'], 'passed': report['combinations_passed']}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    run()
