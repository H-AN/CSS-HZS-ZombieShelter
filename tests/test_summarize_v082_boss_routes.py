"""Negative evidence tests for the actual route-audit implementation; no engine."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location('route_summary', TOOLS / 'summarize_v082_boss_routes.py')
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


def fixture():
    run = {'begin': {'_epoch': 2}, 'initial_state': {'actor_serial': 7, 'actor_userid': 3},
           'configured_points': {1: {'origin': [0, 0, 0]}}}
    common = {'scene': 2, '_epoch': 2, 'boss_id': 'boss_01', 'point': 1, 'body_ref': 123}
    rows = [
        {'kind': 'point_geometry', 'real_hull_clear': 1, 'real_supported_floor': 1, 'point_was_adjusted': False},
        {'kind': 'prepared', 'origin': [0, 0, 0], 'health': 100},
        {'kind': 'actor_damage_observed', 'actor_serial': 7, 'actor_userid': 3, 'boss_ref': 123,
         'requested_damage': 1, 'nonlethal_damage': 1, 'buttons': 1},
        {'kind': 'awakened', 'reason': 'actual_actor_damage_hp_drop_and_production_awakened_observed'},
        {'kind': 'awakened', 'awakened': 1},
        {'kind': 'return_to_base', 'reason': 'only_real_player_teleported'},
        {'kind': 'chase_started', 'stage': 3, 'game_time': 10, 'origin': [0, 0, 0], 'health': 99,
         'returned_core': 0, 'player_safe_core': 0, 'move_type': 0},
        {'kind': 'sample', 'stage': 3, 'game_time': 11, 'origin': [100, 0, 0]},
        {'kind': 'sample', 'stage': 3, 'game_time': 12, 'origin': [200, 0, 0]},
        {'kind': 'arrived_at_any_base', 'stage': 3, 'game_time': 13, 'origin': [300, 0, 0],
         'safe_core': 0, 'returned_core': 0, 'player_safe_core': 0},
        {'kind': 'scene_result', 'game_time': 13, 'boss_ref': 123, 'passed': True,
         'travel': 300, 'chase_seconds': 3, 'hit_count': 1, 'reason': 'entered_actual_base'},
    ]
    return [{**common, **row, '_line': i} for i, row in enumerate(rows, 1)], run


class RouteEvidenceTests(unittest.TestCase):
    def audit(self, rows, run):
        return summary.audit_scene(rows, run)

    def test_actual_sequence_and_positions_allow_initial_movetype_zero(self):
        rows, run = fixture()
        audited = self.audit(rows, run)
        self.assertTrue(audited['audited_pass'], audited['failed_checks'])
        self.assertTrue(audited['warnings'])

    def test_wrong_base_is_not_counted(self):
        rows, run = fixture()
        rows[-2]['safe_core'] = 1
        audited = self.audit(rows, run)
        self.assertFalse(audited['audited_pass'])
        self.assertIn('arrived_in_returned_player_base', audited['failed_checks'])

    def test_missing_hit_is_not_counted(self):
        rows, run = fixture()
        rows = [r for r in rows if r['kind'] != 'actor_damage_observed']
        self.assertFalse(self.audit(rows, run)['audited_pass'])

    def test_wrong_human_is_not_counted(self):
        rows, run = fixture()
        rows[2]['actor_serial'] = 8
        self.assertIn('real_actor_hit', self.audit(rows, run)['failed_checks'])

    def test_no_hit_wait_is_separate_from_walking_failure(self):
        rows, run = fixture()
        rows = rows[:2] + [{**rows[-1], 'passed': False, 'travel': 0, 'hit_count': 0,
                            'reason': 'no_real_hit_awaken_within_60_seconds'}]
        self.assertEqual(self.audit(rows, run)['classification'], 'operator_no_hit_wait_failure_not_a_walking_attempt')

    def test_timeout_remains_failure_with_positive_motion(self):
        rows, run = fixture()
        rows[-2]['kind'] = 'timed_out'
        rows[-1].update(passed=False, reason='timeout', chase_seconds=180)
        self.assertEqual(self.audit(rows, run)['classification'], 'walking_attempt_failed')

    def test_late_status_does_not_inflate_scene_duration_or_endpoint(self):
        rows, run = fixture()
        rows.append({**rows[-2], 'kind': 'status', '_line': 100, 'game_time': 30, 'origin': [900, 0, 0]})
        audited = self.audit(rows, run)
        self.assertTrue(audited['audited_pass'])
        self.assertEqual(audited['chase_seconds'], 3)
        self.assertEqual(audited['end_origin'], [300, 0, 0])

    def test_missing_trajectory_interval_is_rejected(self):
        rows, run = fixture()
        rows[8]['game_time'] = 12.9
        rows[7]['game_time'] = 10.1
        self.assertIn('continuous_bounded_samples', self.audit(rows, run)['failed_checks'])

    def test_wrong_body_reference_is_rejected(self):
        rows, run = fixture()
        rows[8]['body_ref'] = 456
        self.assertIn('one_body_reference', self.audit(rows, run)['failed_checks'])

    def test_same_time_different_position_is_rejected(self):
        rows, run = fixture()
        rows[8]['game_time'] = 11
        self.assertIn('continuous_bounded_samples', self.audit(rows, run)['failed_checks'])

    def test_same_epoch_and_scene_after_new_begin_are_distinct_runs(self):
        rows = [
            {'kind': 'state', 'time': 100, 'epoch': 2, 'actor_serial': 7},
            {'kind': 'begin', 'time': 101, 'epoch': 2, 'scene': 0},
            {'kind': 'scene_result', 'time': 102, 'scene': 1},
            {'kind': 'begin', 'time': 201, 'epoch': 2, 'scene': 0},
            {'kind': 'scene_result', 'time': 202, 'scene': 1},
        ]
        parsed, _ = summary.read_log(('\n'.join(json.dumps(r) for r in rows) + '\n').encode())
        runs = summary.split_runs(parsed, {'session_id': 'server', 'started_at': 90}, 90, None)
        self.assertEqual(len(runs), 2)
        self.assertNotEqual(runs[0]['run_id'], runs[1]['run_id'])
        self.assertEqual([r['time'] for r in runs[0]['rows'] if r['kind'] == 'scene_result'], [102])
        self.assertEqual([r['time'] for r in runs[1]['rows'] if r['kind'] == 'scene_result'], [202])

    def test_old_server_rows_do_not_join_current_run(self):
        raw = b'{"kind":"begin","time":1,"epoch":2}\n{"kind":"begin","time":101,"epoch":2}\n'
        parsed, _ = summary.read_log(raw)
        runs = summary.split_runs(parsed, {'session_id': 'new', 'started_at': 100}, 100, None)
        self.assertEqual(len(runs), 1)
        self.assertEqual(runs[0]['begin']['time'], 101)

    def test_incomplete_tail_is_explicit(self):
        parsed, errors = summary.read_log(b'{"kind":"begin","time":101}\n{"kind":')
        self.assertEqual(len(parsed), 1)
        self.assertTrue(errors[0]['incomplete_tail'])

    def test_later_batch_with_reused_epoch_does_not_match_old_run(self):
        run = {'begin': {'_epoch': 2, '_wall_time': 101}, 'initial_state': {'actor_serial': 7}}
        batch = {'epoch': 2, 'actor_serial': 7, 'started': 205}
        self.assertFalse(summary.batch_belongs_to_run(batch, run, 201))
        batch['started'] = 105
        self.assertTrue(summary.batch_belongs_to_run(batch, run, 201))

    def test_partial_utf8_tail_is_reported_without_mixing_records(self):
        parsed, errors = summary.read_log(b'{"kind":"begin","time":101}\n{"text":"\xe4\xb8')
        self.assertEqual(len(parsed), 1)
        self.assertTrue(all(e['incomplete_tail'] for e in errors))

    def test_flat_and_nested_restart_receipts_have_same_identity(self):
        session = {'session_id': 'deadcity-new-server', 'started_at': 200,
                   'pid': 31944, 'map': 'zsh_deadcity_han_dn'}
        self.assertEqual(summary.session_from_receipt(session), session)
        self.assertEqual(summary.session_from_receipt({'session': session, 'environment': {}}), session)

    def test_missing_restart_identity_is_rejected(self):
        for receipt in ({}, {'session': {}}, {'started_at': 100},
                        {'session_id': 'x', 'started_at': float('nan')}):
            with self.subTest(receipt=receipt), self.assertRaises(ValueError):
                summary.session_from_receipt(receipt)

    def test_another_report_directory_has_its_own_default_output(self):
        report = Path('tests/reports/new_deadcity')
        output, map_path, batch = summary.report_paths(report)
        self.assertEqual(output, (report / 'route_audit').resolve())
        self.assertNotEqual(output, (summary.REPORTS / 'route_audit').resolve())
        self.assertEqual(map_path, (report / 'baseline_live_map.cfg').resolve())
        self.assertEqual(batch, (report / 'real_route_batch.json').resolve())

    def test_explicit_map_and_batch_paths_are_supported(self):
        report, map_path, batch = Path('report'), Path('actual_deadcity.cfg'), Path('deadcity_route_batch.json')
        output, actual_map, actual_batch = summary.report_paths(report, map_config=map_path, batch_receipt=batch)
        self.assertEqual(actual_map, map_path.resolve())
        self.assertEqual(actual_batch, batch.resolve())
        self.assertEqual(output, (report / 'route_audit').resolve())

    def test_six_bases_and_disabled_core_use_actual_contiguous_indices(self):
        cores = ['"disabled" { "enabled" "0" }']
        zones = []
        for core in range(6):
            cores.append(f'"core{core}" {{ "origin" "{core * 100} 0 0" "angles" "0 90 0" "safe_zone" "safe{core}" }}')
            zones.append(f'"safe{core}" {{ "Boxes" {{ "inside" {{ "mins" "-10 -2 -5" "maxs" "10 2 100" }} }} }}')
        source = '"ShelterMap" { "Cores" { ' + ' '.join(cores) + ' } "SafeZones" { ' + ' '.join(zones) + ' } }'
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'six_bases.cfg'
            path.write_text(source, encoding='utf-8')
            boxes = summary.base_boxes(path)
        self.assertEqual([b['core'] for b in boxes], list(range(6)))
        self.assertEqual(boxes[-1]['id'], 'core5')
        self.assertTrue(summary.inside_safe_box([500, 9, 0], 5, boxes))
        self.assertFalse(summary.inside_safe_box([509, 0, 0], 5, boxes))
        self.assertFalse(summary.inside_safe_box([500, 9, 101], 5, boxes))

    def test_deadcity_ids_and_base_f_do_not_require_desert_names(self):
        rows, run = fixture()
        for row in rows:
            row['boss_id'] = 'diag_yixing'
        rows[6].update(returned_core=5, player_safe_core=5, player_origin=[300, 0, 0])
        rows[9].update(safe_core=5, returned_core=5, player_safe_core=5, player_origin=[300, 0, 0])
        scene = self.audit(rows, run)
        boxes = [{'core': 5, 'origin': [300, 0, 0], 'yaw': 0,
                  'local_mins': [-20, -20, -5], 'local_maxs': [20, 20, 100]}]
        summary.audit_safe_box_positions(scene, boxes)
        self.assertTrue(scene['audited_pass'], scene['failed_checks'])
        self.assertEqual(scene['boss'], 'diag_yixing')
        self.assertEqual(scene['best_base'], 'F')

    def test_logged_core_equality_does_not_override_actual_geometry(self):
        rows, run = fixture()
        rows[6]['player_origin'] = [300, 0, 0]
        rows[9]['player_origin'] = [300, 0, 0]
        scene = self.audit(rows, run)
        boxes = [{'core': 0, 'origin': [900, 0, 0], 'yaw': 0,
                  'local_mins': [-20, -20, -5], 'local_maxs': [20, 20, 100]}]
        summary.audit_safe_box_positions(scene, boxes)
        self.assertFalse(scene['audited_pass'])
        self.assertEqual(scene['classification'], 'raw_pass_with_missing_or_invalid_evidence')
        self.assertIn('boss_endpoint_in_actual_base_box', scene['failed_checks'])

    def test_changed_player_endpoint_outside_same_logged_base_is_rejected(self):
        rows, run = fixture()
        rows[6]['player_origin'] = [300, 0, 0]
        rows[9]['player_origin'] = [900, 0, 0]
        scene = self.audit(rows, run)
        boxes = [{'core': 0, 'origin': [300, 0, 0], 'yaw': 0,
                  'local_mins': [-20, -20, -5], 'local_maxs': [20, 20, 100]}]
        summary.audit_safe_box_positions(scene, boxes)
        self.assertFalse(scene['audited_pass'])
        self.assertIn('player_arrival_in_actual_base_box', scene['failed_checks'])


if __name__ == '__main__':
    unittest.main()
