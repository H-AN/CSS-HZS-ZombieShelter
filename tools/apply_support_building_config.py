"""Merge this expansion into the marked 27139 tree, preserving site values.

Only the four new files are copied when absent. The explicitly requested test
population is the only change to existing configurations. Recording paths and
the repository's release population profile are rejected.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

from migrate_v05_materials import load, save
from local_server_runtime import retired_local_runner

ROOT = Path(__file__).resolve().parents[1]
ISOLATED = ROOT / 'tests/runtime/v05_server'
TARGET = ISOLATED / 'cstrike/addons/sourcemod/configs/HanZombieShelter'
NEW_FILES = ('repair_stations.cfg', 'heal_stations.cfg', 'armor_stations.cfg', 'beacons.cfg')


def apply(target: Path, report: Path):
    retired_local_runner()
    target = target.resolve()
    if target != TARGET or not (ISOLATED / '.shelter-v05-isolated.json').is_file():
        raise ValueError('Only the marked 27139 isolated configuration is an allowed target')
    report = report.resolve()
    report.mkdir(parents=True, exist_ok=True)
    before = {p.relative_to(target).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in target.rglob('*.cfg')}
    for name in ('days.cfg', 'zombies.cfg'):
        backup = report / 'before_configs' / name
        if not backup.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target / name, backup)
    copied, preserved = [], []
    for name in NEW_FILES:
        if (target / name).exists():
            preserved.append(name)
        else:
            shutil.copy2(ROOT / 'addons/sourcemod/configs/HanZombieShelter' / name, target / name)
            copied.append(name)
    days = load(target / 'days.cfg')
    for stage in days['ShelterDays']['Stages'].values():
        stage['day_target_alive'] = '100'
        stage['night_target_alive'] = '250'
        stage.setdefault('Cvars', {})['hzs_maxzombiespawn'] = '250'
        if 'night_target_cap' in stage:
            stage['night_target_cap'] = '250'
    save(target / 'days.cfg', days, 'Requested test population only: day 100 / night 250; all other values retained')
    zombies = load(target / 'zombies.cfg')
    zombies['ShelterZombies']['NativePool']['initial_ordinary_count'] = '250'
    zombies['ShelterZombies']['Population']['raider_alive_limit'] = '250'
    save(target / 'zombies.cfg', zombies, 'Native ordinary pool 250; existing HP and building attacks retained')
    after = {p.relative_to(target).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in target.rglob('*.cfg')}
    changed = [name for name in before if before[name] != after.get(name)]
    if any(name not in ('days.cfg', 'zombies.cfg') for name in changed):
        raise AssertionError('Unexpected existing configuration mutation: ' + repr(changed))
    result = {'target': str(target), 'day_target': 100, 'night_target': 250, 'ordinary_pool': 250,
              'before_sha256': before, 'after_sha256': after, 'changed_existing_files': changed,
              'new_files_copied': copied, 'existing_new_category_files_preserved': preserved,
              'recording_server_touched': False, 'other_values_preserved': True}
    (report / 'configuration_merge.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', type=Path, default=TARGET)
    parser.add_argument('--report', type=Path, required=True)
    options = parser.parse_args()
    print(json.dumps(apply(options.target, options.report), ensure_ascii=False, indent=2))
