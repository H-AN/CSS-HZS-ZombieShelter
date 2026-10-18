"""Temporary-tree regression checks; never apply a patch to a real server."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

REPOSITORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY / 'tools'))
import apply_support_building_config as patcher
from migrate_v05_materials import load, save


class SupportConfigurationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve() / 'repository'
        self.isolated = self.root / 'tests/runtime/v05_server'
        self.target = self.isolated / 'cstrike/addons/sourcemod/configs/HanZombieShelter'
        self.defaults = self.root / 'addons/sourcemod/configs/HanZombieShelter'
        self.target.mkdir(parents=True)
        self.defaults.mkdir(parents=True)
        (self.isolated / '.shelter-v05-isolated.json').write_text('{}', encoding='utf-8')
        self.report = self.root / 'report'
        constants = patch.multiple(patcher, ROOT=self.root, ISOLATED=self.isolated, TARGET=self.target)
        constants.start()
        self.addCleanup(constants.stop)
        self.days = {'ShelterDays': {'schema_version': 'site-custom', 'Stages': {}}}
        for index, name in enumerate(('day_01', 'day_02_04', 'day_05_09', 'day_10_plus')):
            self.days['ShelterDays']['Stages'][name] = {
                'from_day': str((1, 2, 5, 10)[index]), 'to_day': str((1, 4, 9, 0)[index]),
                'day_target_alive': '20', 'night_target_alive': '60',
                'day_spawn_batch': '3', 'day_spawn_interval_seconds': '2.75',
                'spawn_batch': '6', 'spawn_interval_seconds': '0.625',
                'raider_hp_multiplier': '1.0', 'max_raider_hp': '100',
                'extra_hp_multiplier_per_day': '0.0', 'hp_multiplier_cap': '1.0',
                'extra_alive_per_day_after_from': '0', 'night_target_cap': '60',
                'Cvars': {'hzs_maxzombiespawn': '60', 'hzs_killmoney': '987', 'hzs_playerreborn': '7.5'},
                'user_metadata': {'retain_unknown': 'value'},
            }
        self.zombies = {'ShelterZombies': {
            'NativePool': {'initial_ordinary_count': '100', 'unrelated_custom': 'keep'},
            'Population': {'raider_alive_limit': '100', 'custom_admission_policy': 'keep'},
            'Ordinary': {'max_hp': '100', 'scale': '1.125'},
            'BuildingAttack': {'interval_seconds': '1.375', 'damage': '11'},
        }}
        save(self.target / 'days.cfg', self.days)
        save(self.target / 'zombies.cfg', self.zombies)
        self.unrelated = {
            'lifecycle.cfg': b'"ShelterLifecycle" { "day_seconds" "20" "night_seconds" "60" }\r\n',
            'progression.cfg': b'"ShelterProgression" { "Weapons" { "m4a1" { "bonus" "0.0" } } }\r\n',
            'fences.cfg': b'// site values\r\n"ShelterFences" { "max_hp" "444" "wood_cost" "1" "damage" "0" }\r\n',
            'maps/custom.cfg': b'"ShelterMap" { "custom_origin" "1 2 3" }\r\n',
        }
        for name, data in self.unrelated.items():
            path = self.target / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        for name in patcher.NEW_FILES:
            (self.defaults / name).write_bytes(('// new default ' + name + '\r\n"Defaults" { "amount" "20" }\r\n').encode())

    def apply(self):
        return patcher.apply(self.target, self.report)

    def snapshot(self):
        return {p.relative_to(self.target).as_posix(): p.read_bytes() for p in self.target.rglob('*.cfg')}

    def test_only_authorized_population_fields_change(self):
        expected_days = copy.deepcopy(self.days)
        for stage in expected_days['ShelterDays']['Stages'].values():
            stage.update(day_target_alive='100', night_target_alive='250', night_target_cap='250')
            stage['Cvars']['hzs_maxzombiespawn'] = '250'
        expected_zombies = copy.deepcopy(self.zombies)
        expected_zombies['ShelterZombies']['NativePool']['initial_ordinary_count'] = '250'
        expected_zombies['ShelterZombies']['Population']['raider_alive_limit'] = '250'
        result = self.apply()
        self.assertEqual(load(self.target / 'days.cfg'), expected_days)
        self.assertEqual(load(self.target / 'zombies.cfg'), expected_zombies)
        self.assertCountEqual(result['changed_existing_files'], ('days.cfg', 'zombies.cfg'))
        for name, data in self.unrelated.items():
            self.assertEqual((self.target / name).read_bytes(), data, name)
        self.assertEqual((self.report / 'before_configs/days.cfg').read_bytes(),
                         self.original_days_bytes)

    @property
    def original_days_bytes(self):
        # Serialize a reference only inside this test's temporary root.
        path = self.root / 'reference-days.cfg'
        save(path, self.days)
        return path.read_bytes()

    def test_existing_new_category_files_are_byte_identical(self):
        original = {}
        for name in patcher.NEW_FILES:
            original[name] = ('// custom existing bytes: ' + name + '\r\n"Custom" { "restore_amount" "7" "wood" "1" }\r\n').encode()
            (self.target / name).write_bytes(original[name])
        result = self.apply()
        self.assertEqual(result['new_files_copied'], [])
        self.assertCountEqual(result['existing_new_category_files_preserved'], patcher.NEW_FILES)
        for name, data in original.items():
            self.assertEqual((self.target / name).read_bytes(), data)

    def test_missing_new_files_copy_defaults_exactly(self):
        result = self.apply()
        self.assertCountEqual(result['new_files_copied'], patcher.NEW_FILES)
        for name in patcher.NEW_FILES:
            self.assertEqual((self.target / name).read_bytes(), (self.defaults / name).read_bytes())

    def test_reapplication_is_byte_stable_and_keeps_first_backup(self):
        first_backup = {name: (self.target / name).read_bytes() for name in ('days.cfg', 'zombies.cfg')}
        self.apply()
        after_first = self.snapshot()
        second = self.apply()
        self.assertEqual(after_first, self.snapshot())
        self.assertEqual(second['changed_existing_files'], [])
        self.assertEqual(second['new_files_copied'], [])
        for name, data in first_backup.items():
            self.assertEqual((self.report / 'before_configs' / name).read_bytes(), data)

    def test_missing_optional_stage_fields_do_not_invent_growth_or_cap(self):
        stage = self.days['ShelterDays']['Stages']['day_01']
        for key in ('Cvars', 'night_target_cap', 'extra_alive_per_day_after_from'):
            stage.pop(key)
        save(self.target / 'days.cfg', self.days)
        self.apply()
        final = load(self.target / 'days.cfg')['ShelterDays']['Stages']['day_01']
        self.assertEqual(final['Cvars'], {'hzs_maxzombiespawn': '250'})
        self.assertNotIn('night_target_cap', final)
        self.assertNotIn('extra_alive_per_day_after_from', final)

    def test_custom_population_growth_parameter_is_preserved(self):
        self.days['ShelterDays']['Stages']['day_10_plus']['extra_alive_per_day_after_from'] = '14'
        save(self.target / 'days.cfg', self.days)
        self.apply()
        actual = load(self.target / 'days.cfg')['ShelterDays']['Stages']['day_10_plus']
        self.assertEqual(actual['extra_alive_per_day_after_from'], '14')
        self.assertEqual(actual['night_target_alive'], '250')
        self.assertEqual(actual['night_target_cap'], '250')

    def test_unmarked_isolated_tree_is_rejected_before_writing(self):
        before = self.snapshot()
        (self.isolated / '.shelter-v05-isolated.json').unlink()
        with self.assertRaises(ValueError):
            self.apply()
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.report.exists())

    def test_release_recording_named_and_arbitrary_trees_are_rejected(self):
        for candidate in (self.defaults, self.root / 'recording/cstrike/addons/sourcemod/configs/HanZombieShelter', self.root / 'other'):
            with self.subTest(candidate=candidate):
                candidate.mkdir(parents=True, exist_ok=True)
                sentinel = candidate / 'sentinel.cfg'
                sentinel.write_bytes(b'must remain byte identical')
                with self.assertRaises(ValueError):
                    patcher.apply(candidate, self.report)
                self.assertEqual(sentinel.read_bytes(), b'must remain byte identical')
                self.assertFalse(self.report.exists())


if __name__ == '__main__':
    unittest.main()
