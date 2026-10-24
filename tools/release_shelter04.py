"""Package 0.4 and optionally install the validated production files locally."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import argparse, hashlib, json, shutil, zipfile
from local_server_runtime import retired_local_runner

ROOT = Path(__file__).resolve().parents[1]
SERVER = Path(r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')
DELIVERY = Path(r'D:\gmod昼夜求生资源\2026年10月15日\昼夜求生外部插件0.4')
MAP = Path(r'D:\gmod昼夜求生资源\2026年10月14日\昼夜求生地图版_0.3_出生点优化\cstrike\maps\zsh_desertplant_cso_v1_d.bsp')
REPORT = ROOT / 'tests/reports/v04'
parser = argparse.ArgumentParser()
parser.add_argument('--deploy', action='store_true')
options = parser.parse_args()
if options.deploy:
    retired_local_runner()
def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')

# Do not publish a package on the strength of old-version checks.
for group in ('integration_final', 'production_final'):
    errors = json.loads((REPORT / group / '原生主体联合错误分类.json').read_text(encoding='utf-8-sig'))
    if not errors['runtime_check_passed'] or errors['updater_block_count']:
        raise SystemExit('Final error classification failed: ' + group)
integration = json.dumps(json.loads((REPORT / 'integration_final/原生主体联合RCON.json').read_text(encoding='utf-8-sig')),ensure_ascii=False)
if '[ShelterTestSummary] Passed=409 Failed=0' not in integration:
    raise SystemExit('Final 0.4 integration report is missing')
production = json.dumps(json.loads((REPORT / 'production_final/原生主体联合RCON.json').read_text(encoding='utf-8-sig')),ensure_ascii=False)
if 'Unknown command' not in production or '[ShelterAPI] PASS optional consumer survives provider absence' not in production:
    raise SystemExit('Final production/API lifecycle evidence is missing')

DELIVERY.mkdir(parents=True, exist_ok=True)
for part in ('addons', 'sound', 'docs', 'server-support'):
    shutil.copytree(ROOT / part, DELIVERY / part, dirs_exist_ok=True)
for name in ('README.md', 'build.ps1', 'LICENSE'):
    shutil.copy2(ROOT / name, DELIVERY / name)
for part in ('tools', 'tests'):
    for source in (ROOT / part).iterdir():
        if source.is_file() and source.suffix in ('.py', '.ps1', '.sp', '.inc', '.json'):
            dest = DELIVERY / part / source.name; dest.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, dest)
(DELIVERY / 'maps').mkdir(exist_ok=True)
shutil.copy2(MAP, DELIVERY / 'maps' / MAP.name)
plugins = list((DELIVERY / 'addons/sourcemod/plugins').glob('*.smx'))
if [p.name for p in plugins] != ['HanZombieShelter.smx']:
    raise SystemExit('Release must contain only the production shelter SMX')
manifest = {p.relative_to(DELIVERY).as_posix(): sha(p) for p in DELIVERY.rglob('*') if p.is_file() and p.name != 'manifest.json'}
write_json(DELIVERY / 'manifest.json', {'version':'0.4.0', 'api_version':1, 'files':manifest})
archive = DELIVERY.parent / 'CSS-HZS-ZombieShelter-0.4.0.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
    for path in sorted(DELIVERY.rglob('*')):
        if path.is_file():bundle.write(path, path.relative_to(DELIVERY).as_posix())
with zipfile.ZipFile(archive) as bundle:
    bad = bundle.testzip()
    if bad:raise SystemExit('ZIP CRC failed: ' + bad)
write_json(REPORT / 'package.json', {'version':'0.4.0','directory':str(DELIVERY),'archive':str(archive),'sha256':sha(archive),
    'crc_passed':True,'files':len(manifest)+1,'archive_bytes':archive.stat().st_size,'map_sha256':sha(MAP)})
print('Package ready:', archive)

if options.deploy:
    # Replace only the three existing configurations with an unchanged baseline.
    # Customized live configs must be merged explicitly, never overwritten here.
    before = json.loads(Path(r'D:\gmod昼夜求生资源\文档\2026年10月15日昼夜求生进度文档\04_验收证据与现场快照.json').read_text(encoding='utf-8-sig'))
    baseline = {row['relative_path']: row['server_sha256'] for row in before['external_configs']['files']}
    config_names = ('turrets.cfg','flame_turrets.cfg','amplifiers.cfg','power.cfg','generators.cfg','lights.cfg','grenade_turrets.cfg')
    files = [Path('addons/sourcemod/plugins/HanZombieShelter.smx'),Path('addons/sourcemod/translations/HanZombieShelter.phrases.txt'),
        Path('addons/sourcemod/scripting/HanZombieShelter.sp'),Path('addons/sourcemod/scripting/include/HanZombieShelterAPI.inc')]
    files += [p.relative_to(ROOT) for p in (ROOT / 'addons/sourcemod/scripting/HanZombieShelter').glob('*.inc')]
    files += [Path('addons/sourcemod/configs/HanZombieShelter') / name for name in config_names]
    game = SERVER / 'cstrike'
    for rel in files:
        live = game / rel
        if rel.as_posix() in baseline and sha(live) != baseline[rel.as_posix()]:
            raise SystemExit('Live config was customized; merge required: ' + str(rel))
        if rel.name in config_names[3:] and live.exists():
            raise SystemExit('New configuration already exists; merge required: ' + str(rel))
    stamp = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d_%H%M%S')
    backup = SERVER / 'zsh_test_backup' / ('v04_' + stamp)
    records = []
    for rel in files:
        live = game / rel; record = {'relative_path':rel.as_posix(),'existed':live.exists()}
        if live.exists():
            saved = backup / rel; saved.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(live,saved); record['before_sha256']=sha(saved)
        records.append(record)
    write_json(backup / 'manifest.json', {'version':'before-0.4.0','files':records})
    for rel in files:
        live = game / rel; live.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(ROOT / rel,live)
    installed = [{'relative_path':rel.as_posix(),'sha256':sha(game/rel),'matches_repository':sha(game/rel)==sha(ROOT/rel)} for rel in files]
    if not all(row['matches_repository'] for row in installed):raise SystemExit('Installed hash mismatch')
    write_json(REPORT / 'deployment.json', {'version':'0.4.0','backup':str(backup),'installed':installed,
        'note':'Files installed; reload the map and inspect the live plugin/status separately.'})
    print('Production files installed; backup:', backup)
