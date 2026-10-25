"""Build/package the v0.8.2 Boss coordinate revision without touching a server."""
from __future__ import annotations

import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / 'tests/reports/v082_boss_spawnpoints_20261025'
BUILD = REPORT / 'build'
FORMAL = BUILD / 'HanZombieShelter.smx'
ARCHIVE = ROOT / 'dist/CSS-HZS-ZombieShelter-v0.8.2-boss-spawnpoints-update.zip'
PLUGIN = 'addons/sourcemod/plugins/HanZombieShelter.smx'
COMPILER = Path(r'D:\css\sp\sourcemod-1.12.0hzs套件\addons\sourcemod\scripting\spcomp64.exe')
DOC = 'docs/v0.8.2_Boss坐标点配置.md'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def stamp():
    return datetime.now(timezone(timedelta(hours=8))).isoformat()


def json_bytes(data):
    return (json.dumps(data, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def keep_write(path, data):
    assert path.resolve().is_relative_to(ROOT)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() != data:
        old = path.read_bytes()
        archive = REPORT / 'history' / (path.stem + '_' + digest(old) + path.suffix)
        archive.parent.mkdir(parents=True, exist_ok=True)
        if not archive.exists():
            archive.write_bytes(old)
    path.write_bytes(data)


def sources():
    scripting = ROOT / 'addons/sourcemod/scripting'
    paths = [scripting / 'HanZombieShelter.sp', *sorted((scripting / 'HanZombieShelter').glob('*.inc')),
             scripting / 'include/HanZombieScenarioAPI.inc', scripting / 'include/HanZombieShelterAPI.inc']
    return {p.relative_to(ROOT).as_posix(): p.read_bytes() for p in paths}


def build():
    before = sources()
    BUILD.mkdir(parents=True, exist_ok=True)
    if FORMAL.exists():
        previous = REPORT / 'history' / ('HanZombieShelter_' + digest(FORMAL.read_bytes()) + '.smx')
        previous.parent.mkdir(parents=True, exist_ok=True)
        if not previous.exists():
            shutil.copy2(FORMAL, previous)
    scripting = ROOT / 'addons/sourcemod/scripting'
    result = subprocess.run([str(COMPILER), str(scripting / 'HanZombieShelter.sp'),
        '-i' + str(scripting), '-i' + str(scripting / 'include'),
        '-i' + str(COMPILER.parent / 'include'), '-o' + str(FORMAL)],
        stdin=subprocess.DEVNULL, capture_output=True, encoding='utf-8', errors='replace')
    log = result.stdout + result.stderr
    keep_write(BUILD / 'HanZombieShelter_compile.log', log.encode('utf-8'))
    if result.returncode or re.search(r'\b(?:error|warning)\s+\d+', log, re.I):
        raise RuntimeError(log)
    assert sources() == before, 'Sources changed while compiling'
    receipt = {'at': stamp(), 'formal_sha256': digest(FORMAL.read_bytes()), 'warnings': 0,
               'compiler': str(COMPILER), 'production_input_count': len(before),
               'production_inputs': {name: digest(blob) for name, blob in before.items()},
               'diagnostic_injection': False, 'runtime_modified': False}
    keep_write(BUILD / 'build_receipt.json', json_bytes(receipt))
    return receipt


def package():
    receipt = json.loads((BUILD / 'build_receipt.json').read_text('utf-8'))
    code = sources()
    assert {name: digest(blob) for name, blob in code.items()} == receipt['production_inputs']
    formal = FORMAL.read_bytes()
    assert digest(formal) == receipt['formal_sha256'] and formal[:4] == b'FFPS'
    assert (ROOT / PLUGIN).read_bytes() == formal, 'Repository SMX has not been updated to the tested build'
    baseline = {row['path']: row['sha256'] for row in json.loads((REPORT / 'baseline.json').read_text('utf-8'))['repo_files']}
    for name, blob in code.items():
        if '/include/' in name:
            assert baseline[name] == digest(blob), 'Public/native API include changed'
    payload = {**code, PLUGIN: formal, DOC: (ROOT / DOC).read_bytes()}
    # The migration tool reuses this existing strict KeyValues parser at import.
    for name in ('migrate_v082_boss_spawn_points.py', 'migrate_v05_materials.py'):
        payload['tools/' + name] = (ROOT / 'tools' / name).read_bytes()
    candidates = sorted((REPORT / 'candidate_maps').rglob('*.cfg'))
    assert candidates, 'Missing offline map candidates'
    for path in candidates:
        payload['map_config_candidates/' + path.relative_to(REPORT / 'candidate_maps').as_posix()] = path.read_bytes()
    payload['README_v0.8.2_Boss坐标修订.txt'] = (
        '本包为已有v0.8.2环境的Boss坐标点池补充修订，未部署到当前运行服。\n'
        '先读docs/v0.8.2_Boss坐标点配置.md。新版主插件要求每张地图配置BossSpawnPoints/boss1..boss5。\n'
        'map_config_candidates仅保存旧点迁移与重复占位，尚未寻找或验证五个安全点；先选点再安排应用。\n'
        '候选local/release/repo各自保留不同来源的完整地图配置，请只合并BossSpawnPoints，不整套覆盖现场。\n'
        '相同Boss类型的原生customzombies数量必须与启用Boss条目数量一致；5个点可循环共享。\n'
        '本包没有HZS主体、引擎补丁、gamedata、新模型、诊断插件，也不会自动启停或重载游戏。\n'
        '正式SMX与源码身份见manifest.json；具体离线核验及未测范围见独立交付报告。\n'
    ).encode('utf-8')
    manifest = {'version': '0.8.2', 'revision': 'boss_spawnpoints', 'formal_sha256': digest(formal),
        'production_input_count': len(code), 'runtime_deployed': False, 'new_safe_positions_selected': False,
        'requires_map_point_migration': True, 'files': {name: {'sha256': digest(blob), 'bytes': len(blob)}
                                                     for name, blob in sorted(payload.items())}}
    payload['manifest.json'] = json_bytes(manifest)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, blob in sorted(payload.items()):
            assert not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts
            info = zipfile.ZipInfo(name, (2026, 10, 25, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, blob)
    data = stream.getvalue()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        assert len(archive.namelist()) == len(set(archive.namelist())) == len(payload)
        assert archive.testzip() is None
        assert [name for name in archive.namelist() if name.endswith('.smx')] == [PLUGIN]
        for name, blob in payload.items():
            assert archive.read(name) == blob
    assert sources() == code and (ROOT / DOC).read_bytes() == payload[DOC], 'Sources changed while packaging'
    keep_write(ARCHIVE, data)
    result = {'at': stamp(), 'passed': True, 'file_count': len(payload),
              'zip': str(ARCHIVE.relative_to(ROOT)), 'zip_sha256': digest(data), 'bytes': len(data),
              'formal_sha256': digest(formal), 'production_input_count': len(code),
              'exact_members_verified': True, 'all_file_bytes_verified': True, 'crc_verified': True,
              'runtime_modified': False, 'map_candidates': len(candidates)}
    keep_write(REPORT / 'package_verification.json', json_bytes(result))
    keep_write(REPORT / 'package_manifest.json', json_bytes(manifest))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'package'])
    args = parser.parse_args()
    result = build() if args.action == 'build' else package()
    print(json.dumps({k: v for k, v in result.items() if k != 'production_inputs'}, ensure_ascii=False, indent=2))
