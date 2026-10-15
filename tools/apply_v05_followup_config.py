"""Merge only the reviewed map repairs and wire reflection default.

Never copies a release profile over the site profile. Existing explicit values,
including damage=0, are preserved. Targets are restricted to this repository or
its marked isolated test tree; the recording server is not a valid target.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from migrate_v05_materials import load, save

ROOT = Path(__file__).resolve().parents[1]
POINTS = {
    'hzsh_t_003': (2176, -2528, 68.5), 'hzsh_t_004': (2048, -2528, 62.7),
    'hzsh_t_005': (1920, -2528, 57.5), 'hzsh_t_006': (2176, -2400, 70.6),
    'hzsh_t_007': (2048, -2400, 65.3), 'hzsh_t_008': (1792, -2528, 52.9),
}


def apply(target, report):
    target = target.resolve()
    allowed = (ROOT / 'addons/sourcemod/configs/HanZombieShelter',
               ROOT / 'tests/runtime/v05_server/cstrike/addons/sourcemod/configs/HanZombieShelter')
    if target not in allowed:
        raise ValueError('Only repository defaults or the fixed isolated configuration may be patched')
    if target == allowed[1] and not (ROOT / 'tests/runtime/v05_server/.shelter-v05-isolated.json').is_file():
        raise ValueError('Isolation marker missing')
    files = ('maps/zsh_desertplant_cso_v1_d.cfg', 'fences.cfg')
    hashes = {name: hashlib.sha256((target / name).read_bytes()).hexdigest() for name in files}
    report = report.resolve()
    report.mkdir(parents=True, exist_ok=True)
    backup = report / 'before_configs'
    for name in files:
        dest = backup / name
        if not dest.exists():
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target / name, dest)
    map_data = load(target / files[0])
    cfg = map_data['ShelterMap']
    repairs = cfg.setdefault('MapAdjustments', {})
    brushes = repairs.setdefault('RemoveDecorativeBrushes', {})
    brushes.setdefault('infection_sheet_01', {'model': '*3', 'origin': '-2879.5 -1760 76'})
    brushes.setdefault('infection_sheet_02', {'model': '*7', 'origin': '-3843.5 -1946 88'})
    points = repairs.setdefault('NPCSpawnOverrides', {})
    for name, (x, y, yaw) in POINTS.items():
        row = points.setdefault(name, {'origin': f'{x} {y} 36.031', 'angles': f'0 {yaw} 0'})
        actual = [float(v) for v in row['origin'].split()]
        column = cfg['BuildRestrictions']['NPCSpawnColumns'][name]
        column['mins'] = f'{actual[0]-64:.3f} {actual[1]-64:.3f} {actual[2]-40:.3f}'
        column['maxs'] = f'{actual[0]+64:.3f} {actual[1]+64:.3f} {actual[2]+124:.3f}'
    save(target / files[0], map_data, 'Explicit runtime repairs; BSP and unrelated scene collisions retained')
    fence_data = load(target / files[1])
    wire = fence_data['ShelterFences']['Types'].get('wood_fence')
    if wire is not None:
        wire['Levels']['1'].setdefault('damage', '10')
    save(target / files[1], fence_data, 'Optional damage is retaliation for defensive structures; omitted or 0 disables it')
    result = {'target': str(target), 'before_sha256': hashes,
              'after_sha256': {name: hashlib.sha256((target / name).read_bytes()).hexdigest() for name in files},
              'existing_explicit_values_preserved': True, 'recording_server_modified': False,
              'spawn_override_names': list(points)}
    (report / 'config_patch.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    apply(args.target, args.report)
