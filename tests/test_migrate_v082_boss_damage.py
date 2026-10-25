"""Exercise the actual byte-preserving damage migrator; never use a live file."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import migrate_v082_boss_damage as migration


def config(values=('10', '10', '10')):
    rows = ['// UTF8 original: \u4fdd\u7559\u6ce8\u91ca', '"zombies"', '{']
    for key, value in zip(migration.TARGETS, values):
        rows += ['    "' + key + '"', '    {', '        "name" "\u539f\u540d\\"10\\""',
                 '        "damage"          "' + value + '" // damage 10 in comment',
                 '        "building_damage" "120"', '        "health" "50000"', '    }']
    rows += ['    "ordinary" { "damage" "10" }', '}']
    return b'\xef\xbb\xbf' + ('\r\n'.join(rows) + '\r\n').encode('utf-8')


class DamageMigrationTests(unittest.TestCase):
    def test_only_three_values_change_with_bom_crlf_unicode_and_comments_retained(self):
        raw = config()
        candidate, report = migration.build_candidate(raw)
        self.assertEqual(report['changed_leaf_count'], 3)
        self.assertEqual(len(raw), len(candidate))
        self.assertEqual(sum(a != b for a, b in zip(raw, candidate)), 3)
        self.assertTrue(candidate.startswith(b'\xef\xbb\xbf'))
        self.assertEqual(candidate.count(b'\r\n'), raw.count(b'\r\n'))
        self.assertIn(b'"ordinary" { "damage" "10" }', candidate)
        self.assertEqual(candidate.count(b'"building_damage" "120"'), 3)
        self.assertEqual(candidate.count(b'// damage 10 in comment'), 3)

    def test_second_pass_is_byte_identical(self):
        once, _ = migration.build_candidate(config())
        twice, report = migration.build_candidate(once)
        self.assertEqual(once, twice)
        self.assertEqual(report['changed_leaf_count'], 0)
        self.assertTrue(report['idempotent_noop'])

    def test_mixed_10_and_30_changes_only_remaining_values(self):
        _, report = migration.build_candidate(config(('30', '10', '30')))
        self.assertEqual(report['changed_leaf_count'], 1)
        self.assertEqual(set(report['leaf_diff']['changed']), {'zombies/boss_angela/damage'})

    def test_unknown_numbers_and_missing_values_fail(self):
        for value in ('0', '-10', '20', '10.0', '010', '+10', 'NaN', 'Infinity', '', '30;quit'):
            with self.subTest(value=value), self.assertRaises(migration.MigrationError):
                migration.build_candidate(config((value, '10', '10')))

    def test_duplicate_target_section_is_rejected(self):
        raw = config().replace(b'"ordinary"', b'"boss_angela"')
        with self.assertRaisesRegex(migration.MigrationError, 'Duplicate'):
            migration.build_candidate(raw)

    def test_duplicate_damage_and_case_alias_are_rejected(self):
        for key in (b'damage', b'DAMAGE'):
            raw = config().replace(b'"health" "50000"', b'"' + key + b'" "10"', 1)
            with self.subTest(key=key), self.assertRaisesRegex(migration.MigrationError, 'Duplicate'):
                migration.build_candidate(raw)

    def test_duplicate_unrelated_keys_are_rejected_instead_of_silently_flattened(self):
        raw = config().replace(b'"health" "50000"', b'"NAME" "duplicate"', 1)
        with self.assertRaisesRegex(migration.MigrationError, 'Duplicate'):
            migration.build_candidate(raw)

    def test_missing_target_or_damage_is_rejected(self):
        for raw in (config().replace(b'boss_angela', b'boss_other'),
                    config().replace(b'"damage"          ', b'"old_damage"          ', 1)):
            with self.assertRaisesRegex(migration.MigrationError, 'Missing scalar'):
                migration.build_candidate(raw)

    def test_wrong_root_and_scalar_target_are_rejected(self):
        for raw in (config().replace(b'"zombies"', b'"other"', 1),
                    b'"zombies" { "boss_pangzi" "10" }'):
            with self.assertRaises(migration.MigrationError):
                migration.build_candidate(raw)

    def test_malformed_kv_and_bare_tokens_are_rejected(self):
        for raw in (config()[:-3], b'"zombies" { damage 10 }', b'"zombies" { "a" "bad', b'/* missing end'):
            with self.subTest(raw=raw[-25:]), self.assertRaises(migration.MigrationError):
                migration.build_candidate(raw)

    def test_braces_and_damage_text_inside_comments_do_not_become_nodes(self):
        raw = config().replace(b'"zombies"', b'/* { "damage" "10" } */\r\n"zombies"', 1)
        candidate, report = migration.build_candidate(raw)
        self.assertEqual(report['changed_leaf_count'], 3)
        self.assertIn(b'/* { "damage" "10" } */', candidate)

    def test_identical_artifacts_are_repeatable_and_check_only_is_read_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output = root / 'source.cfg', root / 'candidate.cfg'
            source.write_bytes(config())
            first = migration.migrate(source, output)
            files = {p.name: p.read_bytes() for p in root.iterdir()}
            second = migration.migrate(source, output)
            checked = migration.migrate(source, output, check_only=True)
            self.assertEqual(first, second)
            self.assertEqual(second, checked)
            self.assertEqual(files, {p.name: p.read_bytes() for p in root.iterdir()})
            self.assertEqual(source.read_bytes(), config())

    def test_input_and_artifact_path_collisions_are_rejected(self):
        source = Path('source.cfg').resolve()
        for paths in ((source, Path('backup.cfg').resolve(), Path('receipt.json').resolve()),
                      (Path('a.cfg').resolve(), Path('a.cfg').resolve(), Path('r.json').resolve())):
            with self.assertRaises(migration.MigrationError):
                migration.validate_destinations(source, paths)

    def test_fixed_live_output_is_rejected_before_writing(self):
        with self.assertRaisesRegex(migration.MigrationError, 'fixed live ROOT'):
            migration.validate_destinations(Path('source.cfg').resolve(), (
                (migration.FIXED_ROOT / 'cstrike/test.cfg').resolve(),
                Path('backup.cfg').resolve(), Path('receipt.json').resolve()))

    def test_existing_different_candidate_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output = root / 'source.cfg', root / 'candidate.cfg'
            source.write_bytes(config())
            output.write_bytes(b'keep this existing candidate')
            with self.assertRaisesRegex(migration.MigrationError, 'Different artifact'):
                migration.migrate(source, output)
            self.assertEqual(output.read_bytes(), b'keep this existing candidate')
            self.assertEqual(len(list(root.iterdir())), 2)

    def test_map_override_is_rejected_before_candidate_or_backup_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scope = root / 'HanZombieScenario'
            scope.mkdir()
            source = scope / 'HanZombieScenarioZombieData.cfg'
            source.write_bytes(config())
            per_map = scope / 'map'
            per_map.mkdir()
            (per_map / source.name).write_bytes(config())
            output = root / 'candidate.cfg'
            with self.assertRaisesRegex(migration.MigrationError, 'separate review'):
                migration.migrate(source, output)
            self.assertFalse(output.exists())
            self.assertFalse((root / 'candidate.source_before.cfg').exists())


if __name__ == '__main__':
    unittest.main()
