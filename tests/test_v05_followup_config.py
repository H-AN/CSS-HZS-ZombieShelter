import contextlib
import io
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import apply_v05_followup_config as patcher
from migrate_v05_materials import load, save


class FollowupPreservationTests(unittest.TestCase):
    def test_explicit_disabled_reflection_and_custom_map_values_survive(self):
        with tempfile.TemporaryDirectory() as folder:
            original_root = patcher.ROOT
            try:
                patcher.ROOT = Path(folder)
                target = patcher.ROOT / 'addons/sourcemod/configs/HanZombieShelter'
                (target / 'maps').mkdir(parents=True)
                source = ROOT / 'addons/sourcemod/configs/HanZombieShelter'
                for name in ('fences.cfg', 'maps/zsh_desertplant_cso_v1_d.cfg'):
                    shutil.copy2(source / name, target / name)
                fence = load(target / 'fences.cfg')
                wire = fence['ShelterFences']['Types']['wood_fence']['Levels']['1']
                wire.update({'damage': '0', 'max_hp': '444'})
                fence['ShelterFences']['BuildRecipes']['build_fence']['cost']['wood'] = '3'
                save(target / 'fences.cfg', fence)
                map_cfg = load(target / 'maps/zsh_desertplant_cso_v1_d.cfg')
                row = map_cfg['ShelterMap']['MapAdjustments']['NPCSpawnOverrides']['hzsh_t_003']
                row['origin'] = '2000 -2500 40'
                save(target / 'maps/zsh_desertplant_cso_v1_d.cfg', map_cfg)
                unrelated = target / 'days.cfg'
                unrelated.write_bytes(b'custom site day values stay byte-identical')
                with contextlib.redirect_stdout(io.StringIO()):
                    patcher.apply(target, patcher.ROOT / 'report')
                self.assertEqual(load(target / 'fences.cfg'), fence)
                result = load(target / 'maps/zsh_desertplant_cso_v1_d.cfg')['ShelterMap']
                self.assertEqual(result['MapAdjustments']['NPCSpawnOverrides']['hzsh_t_003']['origin'], '2000 -2500 40')
                self.assertEqual(result['BuildRestrictions']['NPCSpawnColumns']['hzsh_t_003'],
                                 {'mins': '1936.000 -2564.000 0.000', 'maxs': '2064.000 -2436.000 164.000'})
                self.assertEqual(unrelated.read_bytes(), b'custom site day values stay byte-identical')
                before = {p.relative_to(target): p.read_bytes() for p in target.rglob('*.cfg')}
                with contextlib.redirect_stdout(io.StringIO()):
                    patcher.apply(target, patcher.ROOT / 'report')
                self.assertEqual(before, {p.relative_to(target): p.read_bytes() for p in target.rglob('*.cfg')})
            finally:
                patcher.ROOT = original_root

    def test_recording_tree_and_arbitrary_targets_are_refused(self):
        with self.assertRaises(ValueError):
            patcher.apply(Path(r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\cstrike\addons\sourcemod\configs\HanZombieShelter'),
                          ROOT / 'tests/reports/v05/should_not_be_created')


if __name__ == '__main__':
    unittest.main()
