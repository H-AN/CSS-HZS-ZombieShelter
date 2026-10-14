"""Prepare a separate writable game tree; only immutable assets use hard links."""
from pathlib import Path
import argparse, shutil, os, json

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--source', default=r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')
parser.add_argument('--target', default=str(ROOT / 'tests/runtime/v04_server'))
parser.add_argument('--production', action='store_true')
args = parser.parse_args()
source, target = Path(args.source).resolve(), Path(args.target).resolve()
if source == target or source in target.parents or target in source.parents:
    raise SystemExit('Source and isolated target must be separate trees')
if not target.is_relative_to(ROOT / 'tests/runtime'):
    raise SystemExit('Isolated target must stay in tests/runtime')
target.mkdir(parents=True, exist_ok=True)
linked = copied = 0
for item in source.rglob('*'):
    rel = item.relative_to(source)
    if any(part in ('logs', 'downloads', 'download', 'zsh_test_backup') for part in rel.parts):
        continue
    if item.is_dir():
        (target / rel).mkdir(parents=True, exist_ok=True)
        continue
    if rel.parts[0].startswith('.') or item.suffix.lower() in ('.log', '.bat', '.ps1'):
        continue
    dest = target / rel
    if dest.exists():
        continue
    dest.parent.mkdir(parents=True, exist_ok=True)
    # All plugin files and config files remain independent, including logs that
    # will later be written below addons. Asset files and engine binaries are read-only.
    writable = ('addons' in rel.parts or 'cfg' in rel.parts or item.suffix.lower() in ('.cfg', '.txt'))
    if writable:
        shutil.copy2(item, dest); copied += 1
    else:
        os.link(item, dest); linked += 1
game = target / 'cstrike'
for part in ('configs', 'translations', 'scripting'):
    shutil.copytree(ROOT / 'addons/sourcemod' / part, game / 'addons/sourcemod' / part, dirs_exist_ok=True)
for part in ('sound',):
    shutil.copytree(ROOT / part, game / part, dirs_exist_ok=True)
plugin = ROOT / ('addons/sourcemod/plugins/HanZombieShelter.smx' if args.production else 'tests/compiled/ShelterIntegrationProbe.smx')
shutil.copy2(plugin, game / 'addons/sourcemod/plugins/HanZombieShelter.smx')
consumer = ROOT / 'tests/compiled/ShelterAPIConsumer.smx'
if consumer.exists():
    shutil.copy2(consumer, game / 'addons/sourcemod/plugins/000_ShelterAPIConsumer.smx')
logs = game / 'addons/sourcemod/logs'; logs.mkdir(parents=True, exist_ok=True)
print(json.dumps({'target': str(target), 'linked_assets': linked, 'copied_mutable_files': copied, 'production': args.production}))
