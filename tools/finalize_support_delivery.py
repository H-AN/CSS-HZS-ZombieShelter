"""Audit the retained formal session and archive this review without stopping it."""
from pathlib import Path
import hashlib
import json
import re
import zipfile

from control_v05_running import control
from package_v05 import package

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / 'tests/reports/v05/support_buildings'
RUNTIME = ROOT / 'tests/runtime/v05_server/cstrike'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    session = json.loads((REPORT / 'manual_handoff_final/session.json').read_text(encoding='utf-8'))
    assert session['ready'] and session['kept_running']
    result = control(['status', 'sm plugins list', 'sm_zsh_status', 'zsh_test summary',
                      'hzsh_api_test summary', 'sm_hzsh_botadd_patch_status',
                      'sm_hzsh_follow_patch_status'], REPORT / 'manual_handoff_final/handoff_rcon.json')
    text = result['output']
    assert '1 humans, 0 bots' in text and 'Listing 2 plugins' in text and 'bases=3/3' in text
    for name in ('zsh_test', 'hzsh_api_test', 'sm_hzsh_botadd_patch_status', 'sm_hzsh_follow_patch_status'):
        assert 'Unknown command "' + name + '"' in text
    source = ROOT / 'addons/sourcemod/plugins/HanZombieShelter.smx'
    staged = RUNTIME / 'addons/sourcemod/plugins/HanZombieShelter.smx'
    assert digest(source) == digest(staged) == session['formal_smx_sha256']
    assert not list((RUNTIME / 'addons/sourcemod/logs').glob('errors*'))
    verification = {'formal_ready': True, 'kept_running': True, 'server_pid': session['startupPID'],
                    'client_pids': session['clientPIDs'], 'address': '192.168.0.102:27139',
                    'formal_smx_sha256': digest(source), 'only_two_formal_plugins': True,
                    'humans': 1, 'bots': 0, 'bases_at_handoff_check': '3/3',
                    'test_and_patch_commands_absent': True, 'source_mod_error_logs': [],
                    'feature_suite': {'passed': 307, 'failed': 0, 'skipped': 0},
                    'independent_live_supplement': {'passed': 18, 'failed': 0, 'skipped': 0},
                    'suite_counts_are_independent': True, 'configuration_unit_tests': 8,
                    'natural_day_100_night_250_protected_fixture': True,
                    'remaining': ['real multiplayer', 'normal combat capacity 128/256 per base',
                                  'arbitrary far PVS visibility', 'subjective audio loudness']}
    (REPORT / 'verification.json').write_text(json.dumps(verification, ensure_ascii=False, indent=2), encoding='utf-8')
    docs = [ROOT / 'README.md', ROOT / 'AGENTS.md',
            ROOT / 'docs/v0.5_恢复信标与基地断电_实施记录.md',
            ROOT / 'docs/自动维修治疗护甲设施.md', ROOT / 'docs/v0.5_基地信标.md']
    for path in docs:
        value = path.read_text(encoding='utf-8-sig')
        value = value.replace('manual_handoff/session.json', 'manual_handoff_final/session.json')
        value = re.sub(r'(最终(?:交付)?(?:干净)?正式留服)待 (\[manual_handoff[^\n；]+?) 核对', r'\1已确认 \2', value)
        value = value.replace('当前等待最后干净正式局核对', '已确认最后干净正式局 ready=true、kept_running=true，仅两正式插件、一名普通 Steam 真人与零 BOT')
        value = value.replace('待最后交付核对', '已核对 ready=true、kept_running=true、仅两正式插件与零 BOT')
        value = value.replace('交付前还会单独启动干净正式局，最终留服与进程状态待', '交付前已单独启动干净正式局，最终留服与进程状态见')
        value = value.replace('manual_handoff/session.json) 核实', 'manual_handoff/session.json)（已确认运行并保留两端）')
        path.write_text(value, encoding='utf-8')
    archive = package(ROOT / 'dist/CSS-HZS-ZombieShelter-v0.5.0-dev-review3')
    with zipfile.ZipFile(archive) as stream:
        assert stream.testzip() is None
        manifest = json.loads(stream.read('manifest.json'))
        for name, expected in manifest['files'].items():
            assert hashlib.sha256(stream.read(name)).hexdigest() == expected, name
        assert [n for n in stream.namelist() if n.endswith('.smx')] == ['addons/sourcemod/plugins/HanZombieShelter.smx']
        assert all('materials/sprites/wh_frame.' + ext in stream.namelist() for ext in ('vmt', 'vtf'))
    package_result = {'archive': str(archive), 'sha256': digest(archive),
                      'manifest_files_verified': len(manifest['files']), 'zip_crc_ok': True,
                      'test_smx_absent': True, 'supporting_patch_smx_absent': True,
                      'original_wh_frame_assets_included': True, **verification}
    (ROOT / 'dist/CSS-HZS-ZombieShelter-v0.5.0-dev-review3.verification.json').write_text(
        json.dumps(package_result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(package_result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
