"""Offline migration preservation tests; no game processes or network commands."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from migrate_v05_materials import emit, load, migrate_materials, parse, save


class MaterialMigrationTests(unittest.TestCase):
    def fixture(self, source):
        definitions = {material: {'display_name': f'Material_{material}', 'model': f'models/{material}.mdl',
            'hits_per_cycle': '3', 'yield_per_successful_hit': '7', 'grow_seconds': '10',
            'hold_seconds': '0', 'model_scale': '0.6', 'render_rgb': '1 2 3'}
            for material in ('wood', 'iron', 'copper', 'silver', 'gold')}
        resources = {'ShelterResources': {'schema_version': '1', 'Inventory': {'capacity': '91'},
            'BaseStorage': {'capacity': '123'}, 'Gathering': {'cooldown_seconds': '0.1'},
            'Materials': definitions, 'Pools': {'local': {'reroll_on_regrow': '0', 'Weights': {'wood': '45', 'iron': '40'}}}}}
        days = {'ShelterDays': {'schema_version': '1', 'Stages': {'all': {'from_day': '1', 'to_day': '0',
            'day_target_alive': '20', 'night_target_alive': '60', 'max_raider_hp': '100', 'drop_profile': 'old', 'drop_multiplier': '3'}}}}
        save(source / 'resources.cfg', resources)
        save(source / 'days.cfg', days)
        save(source / 'drops.cfg', {'ShelterDrops': {'schema_version': '1', 'Profiles': {'old': {'drop_chance': '0.5'}}}})
        return resources, days

    def test_original_low_cost_gathering_and_day_values_survive(self):
        with tempfile.TemporaryDirectory() as temp:
            source, target = Path(temp) / 'source', Path(temp) / 'output'
            original, days = self.fixture(source)
            before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir()}
            report = migrate_materials(source, target, Path(temp) / 'absent_zombies.cfg')
            migrated = load(target / 'resources.cfg')['ShelterResources']
            for material, values in original['ShelterResources']['Materials'].items():
                self.assertEqual(migrated['Gatherables'][material], {k: v for k, v in values.items() if k != 'display_name'})
            for field in ('Inventory', 'BaseStorage', 'Gathering', 'Pools'):
                self.assertEqual(migrated[field], original['ShelterResources'][field])
            day = load(target / 'days.cfg')['ShelterDays']['Stages']['all']
            self.assertEqual(day, {k: v for k, v in days['ShelterDays']['Stages']['all'].items() if not k.startswith('drop_')})
            self.assertNotIn('plague_core', migrated['Gatherables'])
            self.assertEqual(len(report['materials']), 7)
            self.assertEqual(before, {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir()})
            self.assertTrue((target / 'migration_reference/drops.v04.cfg').exists())
            self.assertEqual(load(target / 'zombie_drops.cfg')['ShelterZombieDrops']['Rules'], {})

    def test_custom_v2_rule_is_preserved_byte_for_byte_on_repeat(self):
        with tempfile.TemporaryDirectory() as temp:
            source, first, second = [Path(temp) / name for name in ('source', 'first', 'second')]
            self.fixture(source)
            migrate_materials(source, first, Path(temp) / 'absent.cfg')
            custom = '// user custom policy\n"ShelterZombieDrops" { "schema_version" "2" "Rules" { "custom" { "zombie_name" "本地NPC" "materials" "wood,,plague_core" "chances" "1,0,0.1" "amounts" "1" } } }\n'
            (first / 'zombie_drops.cfg').write_text(custom, encoding='utf-8')
            migrate_materials(first, second)
            self.assertEqual((second / 'zombie_drops.cfg').read_bytes(), (first / 'zombie_drops.cfg').read_bytes())
            self.assertEqual(load(first / 'resources.cfg'), load(second / 'resources.cfg'))

    def test_output_cannot_overwrite_or_nest_under_source(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'source'
            self.fixture(source)
            with self.assertRaises(ValueError):
                migrate_materials(source, source)
            with self.assertRaises(ValueError):
                migrate_materials(source, source / 'output')

    def test_keyvalues_preserves_empty_labels_and_unicode(self):
        value = {'root': {'empty': '', 'name': '瘟疫核心', 'description': '中文 "quote"'}}
        self.assertEqual(parse(emit(value)), value)


if __name__ == '__main__':
    unittest.main()
