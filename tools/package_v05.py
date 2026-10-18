"""Package the external development build without test plugins or live config."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def package(output: Path):
    output = output.resolve()
    if not output.is_relative_to(ROOT / 'dist') or output == ROOT / 'dist':
        raise ValueError('Package output must be a new directory below repository dist')
    if output.exists():
        raise ValueError('Package directory exists; choose a new destination')
    version_source = (ROOT / 'addons/sourcemod/scripting/HanZombieShelter/Types.inc').read_text(encoding='utf-8-sig')
    version = re.search(r'#define SHELTER_VERSION "([^"]+)"', version_source).group(1)
    output.mkdir(parents=True)
    shutil.copytree(ROOT / 'addons', output / 'addons', ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copytree(ROOT / 'sound', output / 'sound')
    if (ROOT / 'materials').is_dir():
        shutil.copytree(ROOT / 'materials', output / 'materials')
    shutil.copytree(ROOT / 'docs', output / 'docs')
    shutil.copy2(ROOT / 'README.md', output / 'README.md')
    shutil.copy2(ROOT / 'build.ps1', output / 'build.ps1')
    (output / 'tests').mkdir(exist_ok=True)
    for source in (ROOT / 'tests').glob('phrases_*.json'):
        shutil.copy2(source, output / 'tests' / source.name)
    # Keep the reviewable acceptance artifacts referenced by the documentation.
    # Runtime trees, prior log archives and test SMX files stay out of the bundle.
    evidence_sets = ('baseline', 'final', 'engine_run_04_final',
                     'production_run_06_final', 'real_client_02', 'real_client_03_visual', 'real_client_04_management')
    for name in evidence_sets:
        source_dir = ROOT / 'tests/reports/v05' / name
        if not source_dir.is_dir():
            continue
        destination = output / 'tests/reports/v05' / name
        for source in source_dir.iterdir():
            if source.is_file() and source.suffix in ('.json', '.jpg', '.log'):
                # Server consoles are not required to review the structured
                # assertions and images; include only explicit final build logs.
                if source.suffix == '.log' and name != 'final':
                    continue
                destination.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination / source.name)
    # Follow-up reports contain nested configuration-delta records and the
    # read-only patch diagnostic. Keep those reports and actual screenshots,
    # including failed attempts, without copying runtime/config-backup trees or
    # the optional patch binaries. Earlier acceptance sets above are retained.
    report_root = ROOT / 'tests/reports/v05'
    for source_dir in report_root.iterdir():
        if not source_dir.is_dir() or not source_dir.name.startswith(('followup', 'supporting_patches', 'manual_review', 'support_buildings')):
            continue
        for source in source_dir.rglob('*'):
            if not source.is_file():
                continue
            relative = source.relative_to(source_dir)
            if any(part.startswith(('previous_logs', 'before_candidate')) or part == 'before_configs'
                   for part in relative.parts[:-1]):
                continue
            include = source.suffix.lower() in ('.json', '.jpg', '.jpeg', '.png')
            if source.suffix.lower() == '.log' and source.parent == source_dir:
                include = (source.name.startswith(('build-', 'build_')) or source.name.endswith('-tests.log')
                           or source.name in ('translation.log', 'shelter_probe_assertions.log', 'shelter_runtime.log'))
            if include:
                destination = output / 'tests/reports/v05' / source_dir.name / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
    # The review starter refuses live recording paths and test plugin binaries.
    # Its shared driver module is included for the established hijack/RCON helpers.
    for name in ('migrate_v05.py', 'migrate_v05_materials.py', 'build_translations.py',
                 'start_v05_manual_review.py', 'test_v05_real_client.py',
                 'local_server_runtime.py', 'prepare_isolated_server.py',
                 'apply_v05_followup_config.py', 'control_v05_running.py',
                 'apply_support_building_config.py',
                 'package_v05.py'):
        (output / 'tools').mkdir(exist_ok=True)
        shutil.copy2(ROOT / 'tools' / name, output / 'tools' / name)
    plugins = sorted(p.relative_to(output).as_posix() for p in output.rglob('*.smx'))
    if plugins != ['addons/sourcemod/plugins/HanZombieShelter.smx']:
        raise ValueError('A production package must contain exactly HanZombieShelter.smx')
    if list((output / 'addons').rglob('hzsh_patch_diagnostic.sp')):
        raise ValueError('The temporary patch diagnostic source must stay under tests/supporting')
    manifest = {'version': version, 'release_status': 'development_build', 'api_version': 2,
        'environment_api_version': 1, 'live_recording_profile_included': False,
        'test_plugins_included': False, 'supporting_patch_plugins_included': False,
        'files': {p.relative_to(output).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in output.rglob('*') if p.is_file()}}
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    archive = Path(str(output) + '.zip')
    if archive.exists():
        raise ValueError('ZIP exists; choose a new package destination')
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as stream:
        for path in output.rglob('*'):
            if path.is_file():
                stream.write(path, path.relative_to(output))
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/CSS-HZS-ZombieShelter-v0.5.0-dev-review3')
    args = parser.parse_args()
    print(package(args.output))
