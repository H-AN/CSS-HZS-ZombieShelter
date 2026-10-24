"""One-time, non-destructive migration of the accepted local test environment."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import hashlib
import json
import shutil
import subprocess

from local_test_environment import INSTALL_ROOT, TEST_ROOT, GAME_ROOT, SERVER_EXE

REPO = Path(__file__).resolve().parents[1]
SOURCE = Path(r'D:\gmod昼夜求生资源\文档\制作计划\v0.7.0\preview_update\runtime\server')
CLIENT = Path(r'D:\SteamLibrary\steamapps\common\Counter-Strike Source')
RESOURCE_ROOTS = [Path(r'D:\金源地图移植cs起源\csol地图移植\移植\模型资源\release\cstrike'),
                  Path(r'D:\金源地图移植cs起源\csol地图移植\移植\模型资源\runtime_test\official_support'),
                  INSTALL_ROOT / 'cstrike']
REPORTS = TEST_ROOT / 'reports/migration_20261024'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def files(root):
    return sorted(p for p in root.rglob('*') if p.is_file())


def main():
    running = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
                              "@(Get-Process srcds_win64,srcds -ErrorAction SilentlyContinue | Select-Object Id,ProcessName) | ConvertTo-Json -Compress"],
                             capture_output=True, text=True, encoding='utf-8', check=True)
    if json.loads(running.stdout or '[]'):
        raise RuntimeError('A dedicated server is running. Refuse to migrate live data.')
    if TEST_ROOT.exists() and any(TEST_ROOT.iterdir()):
        raise RuntimeError('Destination already exists; refuse to overwrite or merge unknown files.')
    if not SERVER_EXE.is_file() or not (SOURCE / 'cstrike/gameinfo.txt').is_file():
        raise RuntimeError('Source prerequisites missing.')
    reparse = [str(p) for p in SOURCE.rglob('*') if p.is_symlink() or p.is_junction()]
    if reparse:
        raise RuntimeError('Source contains links requiring explicit review: ' + repr(reparse))
    TEST_ROOT.mkdir(parents=True, exist_ok=True)
    inventory = []
    for source in files(SOURCE):
        relative = source.relative_to(SOURCE)
        target = TEST_ROOT / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        original = sha(source)
        if sha(target) != original:
            raise RuntimeError('Migration hash mismatch: ' + str(relative))
        inventory.append({'relative': relative.as_posix(), 'bytes': source.stat().st_size, 'sha256': original})
    # Keep the runtime overlay first, then fill external custom resources in the
    # exact priority order in the original gameinfo. Never replace live settings.
    resources = []
    for root in RESOURCE_ROOTS:
        for source in files(root):
            relative = source.relative_to(root)
            if relative.parts[0].lower() not in {'cfg', 'maps', 'materials', 'models', 'sound', 'scripts', 'resource', 'particles', 'custom'}:
                continue
            target = GAME_ROOT / relative
            if target.exists():
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            original = sha(source)
            if sha(target) != original:
                raise RuntimeError('Custom resource mismatch: ' + str(relative))
            resources.append({'source': str(source), 'relative': relative.as_posix(), 'sha256': original})
    # Include engine game DLLs and the stock packages, without importing the
    # obsolete installation's addons or cfg. All game search paths are local.
    shutil.copytree(INSTALL_ROOT / 'cstrike/bin', GAME_ROOT / 'bin', dirs_exist_ok=True)
    stock = []
    prefixes = {'cstrike': ['cstrike_pak'], 'hl2': ['hl2_textures', 'hl2_misc', 'hl2_sound_misc'],
                'platform': ['platform_misc']}
    for folder, packages in prefixes.items():
        destination = TEST_ROOT / 'base_content' / folder
        destination.mkdir(parents=True, exist_ok=True)
        for prefix in packages:
            candidate = INSTALL_ROOT / folder
            if not (candidate / (prefix + '_dir.vpk')).is_file():
                candidate = CLIENT / folder
            sources = sorted(candidate.glob(prefix + '*.vpk'))
            if not sources:
                raise RuntimeError('Stock package missing: ' + prefix)
            for source in sources:
                target = destination / source.name
                shutil.copy2(source, target)
                original = sha(source)
                if sha(target) != original:
                    raise RuntimeError('Stock package mismatch: ' + source.name)
                stock.append({'source': str(source), 'relative': target.relative_to(TEST_ROOT).as_posix(),
                              'bytes': target.stat().st_size, 'sha256': original})
    # Loose platform UI files accompany the packaged content; no client addon
    # or account data is imported.
    for name in ['cfg', 'resource', 'scripts', 'materials']:
        source = INSTALL_ROOT / 'platform' / name
        if source.is_dir():
            shutil.copytree(source, TEST_ROOT / 'base_content/platform' / name, dirs_exist_ok=True)
    for directory in ['tools', 'control', 'temp', 'backups/migration_20261024', 'reports/migration_20261024']:
        (TEST_ROOT / directory).mkdir(parents=True, exist_ok=True)
    shutil.copy2(GAME_ROOT / 'gameinfo.txt', TEST_ROOT / 'backups/migration_20261024/gameinfo.before.txt')
    (GAME_ROOT / 'gameinfo.txt').write_text('''"GameInfo"
{
 "game" "HZS Local Test 27169"
 "type" "multiplayer_only"
 "FileSystem"
 {
  "SteamAppId" "240"
  "ToolsAppId" "211"
  "SearchPaths"
  {
   "gamebin" "|gameinfo_path|bin"
   "game+mod+mod_write+default_write_path" "|gameinfo_path|."
   "game+mod" "|gameinfo_path|../base_content/cstrike/cstrike_pak.vpk"
   "game" "|gameinfo_path|../base_content/hl2/hl2_textures.vpk"
   "game" "|gameinfo_path|../base_content/hl2/hl2_misc.vpk"
   "game" "|gameinfo_path|../base_content/hl2/hl2_sound_misc.vpk"
   "platform" "|gameinfo_path|../base_content/platform/platform_misc.vpk"
   "platform" "|gameinfo_path|../base_content/platform"
  }
 }
}
''', encoding='utf-8')
    shutil.copy2(GAME_ROOT / 'cfg/hzsh_v07_preview_server.cfg', GAME_ROOT / 'cfg/local_test_server.cfg')
    shutil.copy2(REPO / 'tools/canonical_server_control.py', TEST_ROOT / 'tools/server_control.py')
    shutil.copy2(REPO / 'tools/minimize_owned_server.py', TEST_ROOT / 'tools/minimize_owned_server.py')
    shutil.copy2(REPO / 'tools/local_test_environment.py', TEST_ROOT / 'tools/local_test_environment.py')
    launchers = {'启动测试服务器.cmd': '--start', '服务器状态.cmd': '--status', '服务器命令.cmd': '--console'}
    for name, argument in launchers.items():
        body = ('@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
                '"D:\\python\\python.exe" -B -X utf8 "%~dp0tools\\server_control.py" ' + argument + '\r\n'
                + ('if errorlevel 1 pause\r\n' if argument == '--start' else 'pause\r\n'))
        (TEST_ROOT / name).write_bytes(body.encode('ascii'))
    write(TEST_ROOT / '.local-test-environment.json', {
        'schema': 1, 'migrated_at': datetime.now(timezone(timedelta(hours=8))).isoformat(),
        'root': str(TEST_ROOT), 'game': str(GAME_ROOT), 'server_executable': str(SERVER_EXE),
        'port': 27169, 'map': 'zsh_desert_han_dn', 'source_archive': str(SOURCE),
        'policy': 'All future local dedicated-server tests use only this root. Old runtimes are historical archives.'})
    # Compare every original file after allowed gameinfo normalization.
    differences = [row['relative'] for row in inventory if sha(TEST_ROOT / row['relative']) != row['sha256']]
    if differences != ['cstrike/gameinfo.txt']:
        raise RuntimeError('Unexpected changes to accepted runtime: ' + repr(differences))
    cfg_files = [row for row in inventory if '/cfg/' in '/' + row['relative']
                 or '/configs/' in '/' + row['relative']]
    manifest = {'at': datetime.now(timezone(timedelta(hours=8))).isoformat(), 'source': str(SOURCE),
                'target': str(TEST_ROOT), 'game': str(GAME_ROOT), 'executable': str(SERVER_EXE),
                'source_retained': True, 'source_files': len(inventory), 'source_bytes': sum(x['bytes'] for x in inventory),
                'original_files': inventory, 'only_modified_original_files': differences,
                'original_configuration_files_preserved': len(cfg_files),
                'added_external_resources': resources, 'added_external_resource_count': len(resources),
                'stock_packages': stock, 'stock_package_count': len(stock),
                'main_sha256': sha(GAME_ROOT / 'addons/sourcemod/plugins/HanZombieShelter.smx'),
                'not_a_gameplay_or_multiplayer_test': True, 'passed': True}
    write(REPORTS / 'migration_manifest.json', manifest)
    print(json.dumps({k: v for k, v in manifest.items()
                      if k not in {'original_files', 'added_external_resources', 'stock_packages'}}, ensure_ascii=False))


if __name__ == '__main__':
    main()
