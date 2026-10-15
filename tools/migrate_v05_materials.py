"""Offline material/gathering migration. Writes only an explicit output directory.

Import migrate_materials(source, target, zombie_data=None) to compose a full upgrade.
Kill probabilities are an explicit v0.5 policy, not a lossless old-profile conversion.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil


def parse(text: str) -> dict:
    pattern = re.compile(r'\s+|//[^\n]*|"((?:\\.|[^"\\])*)"|([{}])')
    tokens = []
    position = 0
    while position < len(text):
        found = pattern.match(text, position)
        if not found:
            raise ValueError(f"Invalid KeyValues input at character {position}")
        position = found.end()
        if found.group(1) is not None:
            tokens.append(found.group(1).replace(r'\"', '"').replace(r'\\', '\\'))
        elif found.group(2):
            tokens.append(found.group(2))
    cursor = 0

    def block(nested=False):
        nonlocal cursor
        result = {}
        while cursor < len(tokens):
            key = tokens[cursor]
            cursor += 1
            if key == '}':
                if not nested:
                    raise ValueError('Unexpected closing brace')
                return result
            if key == '{' or cursor >= len(tokens):
                raise ValueError('Missing key/value')
            value = tokens[cursor]
            cursor += 1
            if value == '{':
                value = block(True)
            elif value == '}':
                raise ValueError('Missing value')
            if key in result:
                raise ValueError(f'Duplicate key: {key}')
            result[key] = value
        if nested:
            raise ValueError('Missing closing brace')
        return result

    return block()


def emit(data: dict, level=0) -> str:
    def quote(value):
        return '"' + str(value).replace('\\', '\\\\').replace('"', r'\"') + '"'
    lines = []
    indent = '    ' * level
    for key, value in data.items():
        if isinstance(value, dict):
            lines.extend([indent + quote(key), indent + '{', emit(value, level + 1), indent + '}'])
        else:
            lines.append(f'{indent}{quote(key)} {quote(value)}')
    return '\n'.join(lines)


def load(path: Path) -> dict:
    return parse(path.read_text(encoding='utf-8-sig'))


def save(path: Path, data: dict, comment=''):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text((f'// {comment}\n' if comment else '') + emit(data) + '\n', encoding='utf-8')


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def migrate_materials(source: Path, target: Path, zombie_data: Path | None = None) -> dict:
    source, target = Path(source).resolve(), Path(target).resolve()
    if source == target or source in target.parents:
        raise ValueError('Output must be a separate directory outside the input config tree')
    target.mkdir(parents=True, exist_ok=True)
    report = {'source': str(source), 'target': str(target), 'writes_live_server': False,
              'material_limit': 64, 'preserved': [], 'retired': [], 'warnings': [], 'source_sha256': {}}
    resource_path = source / 'resources.cfg'
    report['source_sha256']['resources.cfg'] = sha(resource_path)
    resources = load(resource_path)
    root = resources['ShelterResources']
    old = root.pop('Materials', None)
    if old is not None:
        gatherables = copy.deepcopy(old)
        registry = {}
        for material_id, values in gatherables.items():
            label = values.pop('display_name', material_id)
            registry[material_id] = {'display_name': label, 'description': '', 'kind': 'material'}
        root['Gatherables'] = gatherables
        materials = {'ShelterMaterials': {'schema_version': '2', 'Materials': registry}}
    else:
        materials = load(source / 'materials.cfg')
        registry = materials['ShelterMaterials']['Materials']
    for old_id in ('wood', 'iron', 'copper', 'silver', 'gold'):
        if old_id not in registry:
            raise ValueError(f'Missing required legacy material {old_id}')
    registry.setdefault('plague_core', {'display_name': '瘟疫核心', 'description': '击杀指定丧尸获得，可支付特定升级费用', 'kind': 'material'})
    registry.setdefault('credits', {'display_name': '生存币', 'description': '仅为本插件内部材料账本，不关联CS现金或HZS金钱', 'kind': 'currency'})
    if len(registry) > 64:
        raise ValueError('Material registry exceeds the documented 64-ID capacity')
    root['schema_version'] = '2'
    report['preserved'].extend(['Inventory', 'BaseStorage', 'Gathering', 'Pools', 'all original gatherable growth/model/yield fields'])
    save(target / 'resources.cfg', resources, 'v0.5: gathering only; original numerical values preserved')
    save(target / 'materials.cfg', materials, 'v0.5: material IDs, display metadata and currency classification only')
    if (source / 'zombie_drops.cfg').exists():
        # Existing user policy wins; never regenerate it during a repeated upgrade.
        shutil.copy2(source / 'zombie_drops.cfg', target / 'zombie_drops.cfg')
        report['preserved'].append('existing zombie_drops.cfg copied unchanged')
    else:
        if zombie_data is None:
            zombie_data = Path(__file__).resolve().parents[2] / 'H-AN-CSS-ZombieScenario' / '示例配置文件' / 'configs' / 'HanZombieScenario' / 'HanZombieScenarioZombieData.cfg'
        names = []
        if zombie_data.exists():
            zombie_root = next(iter(load(zombie_data).values()))
            names = [row['name'] for row in zombie_root.values() if isinstance(row, dict) and row.get('name')]
            report['zombie_name_source'] = str(zombie_data)
            report['zombie_name_source_sha256'] = sha(zombie_data)
        else:
            report['warnings'].append('Zombie public-name source missing: new drop table has no enabled rules; no wildcard/fallback used')
        # Deliberate fixed v0.5 policy; each material gets an independent roll.
        rules = {}
        for index, name in enumerate(names):
            rules[f'base_materials_{index + 1}'] = {'enabled': '1', 'zombie_name': name,
                'materials': 'wood,iron,copper,silver,gold', 'chances': '0.225,0.09375,0.0375,0.015,0.00375', 'amounts': '1'}
            if index > 0:
                rules[f'special_core_{index + 1}'] = {'enabled': '1', 'zombie_name': name,
                    'materials': 'plague_core,credits', 'chances': '0.1,1', 'amounts': '1'}
        drops = {'ShelterZombieDrops': {'schema_version': '2', 'Settings': {'reward_defense_kills': '1'}, 'Rules': rules}}
        save(target / 'zombie_drops.cfg', drops, 'Explicit v0.5 fixed policy; independently rolled, no day/profile multiplier and no old-table fallback')
        report['warnings'].append('New base fixed chances 0.225/0.09375/0.0375/0.015/0.00375, amount 1; they match only the old baseline expected amount, not its distribution or later-stage multipliers. Specials also gain core 10% and credits 100%, amount 1. Review names against the active HZS config.')
    if (source / 'days.cfg').exists():
        days = load(source / 'days.cfg')
        for stage_id, stage in days['ShelterDays'].get('Stages', {}).items():
            for key in ('drop_profile', 'drop_multiplier'):
                if key in stage:
                    report['retired'].append({'file': 'days.cfg', 'stage': stage_id, 'key': key, 'old_value': stage.pop(key)})
        save(target / 'days.cfg', days, 'v0.5: original day/NPC numerical values retained; legacy kill-drop controls removed')
        report['preserved'].append('all non-drop days.cfg fields')
    if (source / 'drops.cfg').exists():
        (target / 'migration_reference').mkdir(exist_ok=True)
        shutil.copy2(source / 'drops.cfg', target / 'migration_reference' / 'drops.v04.cfg')
        report['retired'].append({'file': 'drops.cfg', 'runtime': 'not loaded', 'backup': 'migration_reference/drops.v04.cfg'})
    report['materials'] = list(registry)
    report['gatherable_ids'] = list(root.get('Gatherables', {}))
    report['output_sha256'] = {name: sha(target / name) for name in ('materials.cfg', 'resources.cfg', 'zombie_drops.cfg')}
    (target / 'materials_migration_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'addons/sourcemod/configs/HanZombieShelter')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--zombie-data', type=Path)
    args = parser.parse_args()
    report = migrate_materials(args.source, args.output, args.zombie_data)
    print(json.dumps({'target': report['target'], 'materials': len(report['materials']), 'gatherables': len(report['gatherable_ids']), 'warnings': report['warnings']}, ensure_ascii=False))


if __name__ == '__main__':
    main()
