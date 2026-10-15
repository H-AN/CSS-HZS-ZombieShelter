"""Build an offline v0.5 configuration tree while preserving existing values.

No deployment mode. Input must be the HanZombieShelter config directory.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil

from migrate_v05_materials import load, save, migrate_materials

ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = ROOT / 'addons/sourcemod/configs/HanZombieShelter'
MODEL_CONTACT_Z = {
    'models/zshelter/barricade_wire.mdl': '3.570',
    'models/zshelter/shelter_b_claymore01.mdl': '0.310',
    'models/zshelter/shelter_b_firegun01.mdl': '0.250',
    'models/zshelter/shelter_b_turret01.mdl': '2.717',
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def migrate(source: Path, output: Path, zombie_data: Path | None = None):
    source, output = source.resolve(), output.resolve()
    if source == output or source in output.parents or output in source.parents:
        raise ValueError('Input and output must be separate, non-containing directories')
    if output.exists() and any(output.iterdir()):
        raise ValueError('Output must be empty; existing migration results are never overwritten')
    originals = {p.relative_to(source).as_posix(): digest(p) for p in source.rglob('*.cfg')}
    output.mkdir(parents=True, exist_ok=True)
    for path in source.rglob('*.cfg'):
        target = output / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    if zombie_data is None:
        active_data = source.parent / 'HanZombieScenario' / 'HanZombieScenarioZombieData.cfg'
        if active_data.is_file():
            zombie_data = active_data
    material_report = migrate_materials(source, output, zombie_data)
    if (output / 'drops.cfg').exists():
        (output / 'drops.cfg').unlink()  # The copy is retained under migration_reference.
    changes = []
    base_data = load(source / 'bases.cfg')
    for profile_id, profile in base_data['ShelterBases']['Profiles'].items():
        profile.setdefault('build_limit_max', '128')
        for level, row in profile['Levels'].items():
            number = int(level)
            row.setdefault('build_limit', str(min(1024 - 8, number * 24)))
            row.setdefault('build_half_extents', f'{min(8192, 600 + (number - 1) * 50)} {min(8192, 500 + (number - 1) * 50)} 0')
        changes.append(f'bases.cfg Profiles/{profile_id}: add absent range/quota fields, retain every original HP/cost/Growth value')
    save(output / 'bases.cfg', base_data, 'v0.5 offline migration: existing balance retained')
    for map_path in (source / 'maps').glob('*.cfg'):
        data = load(map_path)
        for core_id, core in data['ShelterMap'].get('Cores', {}).items():
            if 'BuildArea' not in core:
                angles = core.get('angles', '0 0 0').split()
                core['BuildArea'] = {'center': core['origin'], 'angles': f'0 {angles[1]} 0', 'z_min': '-128', 'z_max': '512'}
                changes.append(f'maps/{map_path.name} Cores/{core_id}: fixed center from original core origin; original yaw retained')
        save(output / 'maps' / map_path.name, data)
    management = load(source / 'management.cfg')
    limits = management['ShelterManagement'].setdefault('Limits', {})
    if 'max_total_buildings' not in limits:
        old = limits.pop('max_managed_defenses', None)
        # Deliberate v0.5 capacity migration; do not silently retain the obsolete 96 ceiling.
        limits['max_total_buildings'] = '768'
        changes.append(f'management.cfg Limits/max_managed_defenses={old!r} -> max_total_buildings=768 (global, excludes bases)')
    save(output / 'management.cfg', management)
    mode = load(source / 'mode.cfg')
    mode['ShelterMode'].setdefault('BuildZoneDisplay', {
        'enabled': '1', 'refresh_interval': '1.0', 'effect_lifetime': '1.2',
        'color': '255 40 40 220', 'line_width': '3.0', 'height_offset': '4.0'})
    save(output / 'mode.cfg', mode)
    if not (source / 'expanders.cfg').exists():
        shutil.copy2(DEFAULTS / 'expanders.cfg', output / 'expanders.cfg')
        changes.append('expanders.cfg: add stock-model basic/advanced variants; existing recipes untouched')
    # Make the placement policy visible in migrated configs without changing any
    # previously configured model offsets, sounds, attachments or balance.
    for path in output.glob('*.cfg'):
        data = load(path)
        root = next(iter(data.values()))
        if not isinstance(root, dict) or 'Types' not in root:
            continue
        for row in root['Types'].values():
            contact = MODEL_CONTACT_Z.get(row.get('model', '').lower(), '0')
            row.setdefault('Placement', {'mode': 'upright', 'origin_offset': f'0 0 {contact}',
                'angle_offset': '0 0 0', 'allow_overlap': '1', 'max_distance': '512'})
        save(path, data)
    unchanged_source = all(digest(source / name) == value for name, value in originals.items())
    if not unchanged_source:
        raise RuntimeError('Source changed concurrently; migration snapshot needs review')
    report = {'version': '0.5.0-dev', 'source': str(source), 'output': str(output),
        'source_unchanged': unchanged_source, 'deployed': False, 'changes': changes,
        'materials': material_report, 'source_sha256': originals,
        'output_sha256': {p.relative_to(output).as_posix(): digest(p) for p in output.rglob('*.cfg')},
        'retained_values': {'mode_rules': copy.deepcopy(load(source / 'mode.cfg')['ShelterMode']['Rules']),
            'day_stages_without_retired_drop_keys': {key: {k: v for k, v in value.items() if k not in ('drop_profile', 'drop_multiplier')}
                for key, value in load(source / 'days.cfg')['ShelterDays']['Stages'].items()}},
        'limitations': ['No in-place deployment. Review new drop probabilities/public names and model offsets before installation.',
            'A 256 per-base ceiling is configurable; actual admission still respects global logical and edict budgets.']}
    (output / 'migration_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=DEFAULTS)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--zombie-data', type=Path)
    args = parser.parse_args()
    result = migrate(args.source, args.output, args.zombie_data)
    print(json.dumps({'output': result['output'], 'source_unchanged': result['source_unchanged'],
        'deployed': False, 'changes': result['changes']}, ensure_ascii=False, indent=2))
