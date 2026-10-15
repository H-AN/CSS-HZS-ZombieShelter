"""Prepare a separate writable game tree; only immutable assets use hard links."""
from pathlib import Path
import argparse, shutil, os, json, subprocess, base64

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--source', default=r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')
parser.add_argument('--target', default=str(ROOT / 'tests/runtime/v05_server'))
parser.add_argument('--production', action='store_true')
args = parser.parse_args()
source, target = Path(args.source).resolve(), Path(args.target).resolve()
if not (source/'srcds_win64.exe').is_file():raise SystemExit('Source server executable missing')
if source == target or source in target.parents or target in source.parents:
    raise SystemExit('Source and isolated target must be separate trees')
if not target.is_relative_to(ROOT / 'tests/runtime'):
    raise SystemExit('Isolated target must stay in tests/runtime')
marker=target/'.shelter-v05-isolated.json'
if target.exists() and not marker.is_file():
    raise SystemExit('Existing target has no v0.5 isolation marker; select a new directory')
if os.name=='nt':
    # Query only a boolean; never print process command lines or RCON secrets.
    quoted=str(target/'srcds_win64.exe').replace("'","''")
    script="$exe='"+quoted+"'; [bool](Get-CimInstance Win32_Process -Filter \"Name = 'srcds_win64.exe'\" | Where-Object { $_.ExecutablePath -eq $exe })"
    encoded=base64.b64encode(script.encode('utf-16le')).decode('ascii')
    running=subprocess.run(['powershell','-NoProfile','-EncodedCommand',encoded],capture_output=True,text=True,check=True)
    if running.stdout.strip().lower()=='true':raise SystemExit('Isolated target is running; do not prepare over a live server')
plugin = ROOT / ('addons/sourcemod/plugins/HanZombieShelter.smx' if args.production else 'tests/compiled/ShelterIntegrationProbe.smx')
if not plugin.is_file():raise SystemExit('Build the selected plugin before preparing an isolated server')
target.mkdir(parents=True, exist_ok=True)
linked = copied = 0
for item in source.rglob('*'):
    rel = item.relative_to(source)
    if any(part in ('logs', 'downloads', 'download', 'zsh_test_backup') for part in rel.parts):
        continue
    if rel.as_posix().lower() in ('cstrike/cfg/autoexec.cfg','cstrike/cfg/server.cfg'):
        continue
    if item.is_dir():
        (target / rel).mkdir(parents=True, exist_ok=True)
        continue
    if rel.parts[0].startswith('.') or item.suffix.lower() in ('.log', '.bat', '.ps1'):
        continue
    # Copy only the known original HZS dependency. Do not import test probes,
    # engine patch plugins, or arbitrary active plugins from another server.
    if 'plugins' in rel.parts and item.suffix.lower()=='.smx' and item.name!='UnmodifiedHZS.smx':
        continue
    dest = target / rel
    if dest.exists():
        continue
    dest.parent.mkdir(parents=True, exist_ok=True)
    # All plugin files and config files remain independent, including logs that
    # will later be written below addons. Asset files and engine binaries are read-only.
    immutable_suffixes={'.bsp','.vpk','.mdl','.vtx','.vvd','.phy','.vmt','.vtf','.wav','.mp3','.pcf','.spr','.dll','.exe','.so'}
    writable = ('addons' in rel.parts or 'cfg' in rel.parts or item.suffix.lower() not in immutable_suffixes)
    if writable:
        shutil.copy2(item, dest); copied += 1
    else:
        os.link(item, dest); linked += 1
game = target / 'cstrike'
for part in ('configs', 'translations', 'scripting'):
    shutil.copytree(ROOT / 'addons/sourcemod' / part, game / 'addons/sourcemod' / part, dirs_exist_ok=True)
for part in ('sound',):
    shutil.copytree(ROOT / part, game / part, dirs_exist_ok=True)
shutil.copy2(plugin, game / 'addons/sourcemod/plugins/HanZombieShelter.smx')
consumer = ROOT / 'tests/compiled/ShelterAPIConsumer.smx'
if consumer.exists() and not args.production:
    shutil.copy2(consumer, game / 'addons/sourcemod/plugins/000_ShelterAPIConsumer.smx')
if args.production:
    for old in (game/'addons/sourcemod/plugins').glob('*.smx'):
        if old.name not in ('HanZombieShelter.smx','UnmodifiedHZS.smx'):old.unlink()
# The runner uses this independent cfg instead of inherited startup defaults.
# Atomic replacement also prevents writes through an accidental old hard link.
for name in ('zsh_codex_test.cfg','autoexec.cfg','server.cfg'):
    cfg=game/'cfg'/name;cfg.parent.mkdir(parents=True,exist_ok=True)
    temporary=cfg.with_suffix('.cfg.tmp')
    temporary.write_text('// Isolated v0.5 test startup; no inherited startup commands.\nbot_quota 0\nbot_join_after_player 1\nsv_hibernate_when_empty 0\n',encoding='utf-8')
    os.replace(temporary,cfg)
logs = game / 'addons/sourcemod/logs'; logs.mkdir(parents=True, exist_ok=True)
record={'target':str(target),'source':str(source),'linked_assets':linked,'copied_mutable_files':copied,'production':args.production,'bots_enabled':False,'engine_patches_installed':False}
marker.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(record,ensure_ascii=False))
