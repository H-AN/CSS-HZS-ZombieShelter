"""Verify protected bytes and package the formal performance update, no game mutations."""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import re
import shutil
import zipfile

import test_v083_live as live

def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

def main():
    formal = json.loads((live.REPORT / 'formal_build.json').read_text(encoding='utf-8'))
    before = json.loads((live.REPORT / 'live_before.json').read_text(encoding='utf-8'))['files']
    current = live.inventory()
    changed = [key for key, value in before.items() if current.get(key) != value]
    extra = sorted(set(current) - set(before))
    assert changed == ['addons/sourcemod/plugins/HanZombieShelter.smx'], changed
    assert not extra, extra
    baseline = json.loads((live.WORK / 'checkpoints/00_baseline/manifest.json').read_text(encoding='utf-8'))
    old = baseline['files']
    repo_changes = [key for key, value in old.items() if live.legacy.sha(live.ROOT / key) != value]
    assert not [key for key in repo_changes if '/configs/' in key or '/scripting/include/' in key or 'Environment' in key], repo_changes
    sha = formal['sha256']
    for path in [Path(formal['output']), live.ROOT / 'addons/sourcemod/plugins/HanZombieShelter.smx', live.legacy.GAME / 'addons/sourcemod/plugins/HanZombieShelter.smx']:
        assert live.legacy.sha(path) == sha, path
    for key, value in formal['production_sources'].items():
        assert live.legacy.sha(live.ROOT / key) == value, key
    inputs = {**formal['production_sources'], 'addons/sourcemod/scripting/HanZombieShelter.sp': formal['production_main_sha256']}
    for name in ['HanZombieScenarioAPI.inc', 'HanZombieShelterAPI.inc']:
        key = 'addons/sourcemod/scripting/include/' + name
        assert live.legacy.sha(live.ROOT / key) == old[key], key
        inputs[key] = old[key]
    assert len(inputs) == 53
    formal.update(production_inputs=inputs, production_input_count=len(inputs), input_manifest_completed_at=live.legacy.now())
    write(live.REPORT / 'formal_build.json', formal)
    # Every ZIP that existed at baseline must stay byte-identical.
    old_zips = baseline['protected_dist']
    for key, value in old_zips.items():
        if isinstance(value, str):
            path = live.ROOT / key
            if not path.is_file():path = live.ROOT / 'dist' / key
            assert live.legacy.sha(path) == value, key
    restored = live.legacy.ROOT / 'backups/v083_performance_20261030/addons/sourcemod/plugins/HanZombieShelter.smx'
    assert live.legacy.sha(restored) == 'a02f00dcbe0ae4c054d83bda3b018da65ca1671ba3a9035783be5e1b67e06ae0'
    protected = {'at': live.legacy.now(), 'live_protected_files_before': len(before), 'live_changed': changed, 'live_unchanged': len(before) - len(changed), 'live_extra': extra,
        'repo_production_before': len(old), 'repo_changed': repo_changes, 'repo_unchanged': len(old) - len(repo_changes), 'new_production': ['addons/sourcemod/scripting/HanZombieShelter/RuntimeIndex.inc'],
        'formal_sha256': sha, 'source_inputs': 53, 'config_and_public_api_and_external_live_trees_preserved': True, 'old_zip_manifest_keys': list(old_zips),
        'runtime_rollback_smx': str(restored), 'all_checked': True}
    write(live.REPORT / 'preservation_verification.json', protected)
    doc = live.ROOT / 'docs/v0.8.3_性能优化与回退.md'
    package = live.ROOT / 'dist/CSS-HZS-ZombieShelter-v0.8.3-performance-update.zip'
    assert not package.exists(), 'Never overwrite a frozen package'
    entries = {'addons/sourcemod/plugins/HanZombieShelter.smx': Path(formal['output']), 'addons/sourcemod/scripting/HanZombieShelter.sp': live.ROOT / 'addons/sourcemod/scripting/HanZombieShelter.sp',
        'docs/v0.8.3_性能优化与回退.md': doc, 'rollback/HanZombieShelter-v0.8.2.smx': restored}
    entries.update({key: live.ROOT / key for key in inputs})
    with zipfile.ZipFile(package, 'x', zipfile.ZIP_DEFLATED) as archive:
        for key, path in sorted(entries.items()):archive.writestr(key, path.read_bytes())
    with zipfile.ZipFile(package) as archive:
        assert archive.testzip() is None
        assert set(archive.namelist()) == set(entries)
        for key, path in entries.items():assert archive.read(key) == path.read_bytes(), key
    delivery = {'at': live.legacy.now(), 'path': str(package), 'sha256': live.legacy.sha(package), 'bytes': package.stat().st_size, 'members': len(entries),
        'members_sha256': {key: live.legacy.sha(path) for key, path in entries.items()}, 'passed': True, 'scope': 'package identity/integrity only; not gameplay assertions'}
    write(live.REPORT / 'package_verification.json', delivery)
    summary = {'at': live.legacy.now(), 'formal_sha256': sha, 'production_inputs': 53, 'compile_errors': 0, 'compile_warnings': 0,
        'offline': {'entity_lookup_assertions': 1233, 'resource_assertions': 352, 'progression_assertions': 112},
        'isolated_real_sourcemod': {'ordered_index_assertions': 3791, 'power_scenarios': 160, 'progression_assertions': 112, 'not_additive_to_offline_progression': True},
        'actual_engine_entities': {'maintenance_assertions': 18, 'hidden_test_entities_not_visual_gameplay': True},
        'actual_client': {'humans': 1, 'bots': 0, 'axe_work': True, 'gather_deplete_natural_regrow': True, 'protected_diagnostic_conditions': True, 'not_multiplayer': True},
        'formal_cold_start': json.loads((live.REPORT / 'formal_cold_start.json').read_text(encoding='utf-8')),
        'limitations': ['No original cloud-server 4-5-player A/B or 5Mbps capacity proof', 'No leak-free or long-duration certification', 'Timer merge and other conditional plan items not implemented', 'Original workbench level/menu report not resolved by this optimization', 'Historical fixture errors retained'],
        'preservation': protected, 'package': {k: v for k, v in delivery.items() if k != 'members_sha256'}, 'local_release_ready': True, 'cloud_multiplayer_accepted': False}
    write(live.REPORT / 'verification_summary.json', summary)
    repository_report = live.ROOT / 'tests/reports/v083_performance_20261030'
    repository_report.mkdir(exist_ok=False)
    for path in live.REPORT.glob('*.json'):
        shutil.copy2(path, repository_report / path.name)
    print(json.dumps({'protected_files': len(before), 'changed': changed, 'formal_sha256': sha, 'package_sha256': delivery['sha256'], 'members': len(entries)}, ensure_ascii=True))

if __name__ == '__main__':
    import sys
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
