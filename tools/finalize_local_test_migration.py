"""Install the self-contained local controller and redirect its legacy entry."""
from pathlib import Path
import hashlib
import json
import shutil
from local_test_environment import TEST_ROOT, GAME_ROOT

REPO = Path(__file__).resolve().parents[1]
OLD_SCRIPT = Path(r'D:\gmod昼夜求生资源\文档\制作计划\v0.7.0\preview_update\scripts\runtime_preview.py')


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def install_launchers():
    launchers = {'启动测试服务器.cmd': '--start', '服务器状态.cmd': '--status', '服务器命令.cmd': '--console'}
    for name, argument in launchers.items():
        text = ('@echo off\r\nchcp 65001 >nul\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
                '"%~dp0tools\\python\\python.exe" -B -X utf8 "%~dp0tools\\server_control.py" ' + argument + '\r\n'
                'set "server_result=%errorlevel%"\r\n'
                + ('if not "%server_result%"=="0" pause\r\n' if argument == '--start' else 'pause\r\n')
                + 'exit /b %server_result%\r\n')
        (TEST_ROOT / name).write_bytes(text.encode('ascii'))


def main():
    manifest_path = TEST_ROOT / 'reports/migration_20261024/migration_manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    python_source = Path(r'D:\python')
    python_target = TEST_ROOT / 'tools/python'
    python_target.mkdir(parents=True, exist_ok=True)
    for name in ['python.exe', 'python3.dll', 'python312.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'LICENSE.txt']:
        shutil.copy2(python_source / name, python_target / name)
    shutil.copytree(python_source / 'DLLs', python_target / 'DLLs', dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pdb'))
    shutil.copytree(python_source / 'Lib', python_target / 'Lib', dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('site-packages', '__pycache__', 'test', 'tests', 'idlelib', 'tkinter'))
    shutil.copy2(REPO / 'tools/canonical_server_control.py', TEST_ROOT / 'tools/server_control.py')
    install_launchers()
    legacy_backup = TEST_ROOT / 'backups/migration_20261024/runtime_preview.before.py'
    if not legacy_backup.exists():
        shutil.copy2(OLD_SCRIPT, legacy_backup)
    shim = '''"""Compatibility import: all local server activity uses the fixed environment.

The previous implementation is preserved in the new root's migration backups.
"""
from pathlib import Path
import runpy

_controller = Path(r"D:\\css\\CS2\\steamcmd\\steamapps\\common\\Counter-Strike Source Dedicated Server\\昼夜求生本地测试服务器\\tools\\server_control.py")
if not _controller.is_file():
    raise RuntimeError("The fixed local test environment is not installed. Do not start the retired runtime.")
globals().update({key: value for key, value in runpy.run_path(str(_controller), run_name="fixed_local_test_api").items()
                  if not key.startswith("__")})
if __name__ == "__main__":
    main()
'''
    OLD_SCRIPT.write_text(shim, encoding='utf-8')
    source_root = Path(manifest['source'])
    (source_root / 'MIGRATED_TO.txt').write_text(
        '2026-10-24: historical archive only; do not start or deploy here.\n'
        'Current local test root: ' + str(TEST_ROOT) + '\n'
        'Current game: ' + str(GAME_ROOT) + '\n'
        'Use the startup CMD in the current root.\n', encoding='utf-8')
    bundle = [{'relative': p.relative_to(TEST_ROOT).as_posix(), 'bytes': p.stat().st_size, 'sha256': sha(p)}
              for p in sorted(python_target.rglob('*')) if p.is_file()]
    manifest['bundled_python'] = {'path': str(python_target / 'python.exe'), 'files': len(bundle),
                                  'bytes': sum(p['bytes'] for p in bundle), 'inventory': bundle,
                                  'scope': 'Controller standard library only; no third-party site-packages or user data.'}
    manifest['legacy_controller_redirect'] = {'path': str(OLD_SCRIPT), 'backup': str(legacy_backup),
                                             'backup_sha256': sha(legacy_backup), 'shim_sha256': sha(OLD_SCRIPT)}
    manifest['controller_sha256'] = sha(TEST_ROOT / 'tools/server_control.py')
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'controller_installed': True, 'bundled_python_files': len(bundle),
                      'bundled_python_bytes': manifest['bundled_python']['bytes'],
                      'legacy_controller_now_targets': str(GAME_ROOT)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
