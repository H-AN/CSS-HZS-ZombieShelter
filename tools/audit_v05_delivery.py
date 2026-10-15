"""Collect final v0.5 evidence without contacting or modifying any server."""
from pathlib import Path
import hashlib
import json
import re
from migrate_v05_materials import load

ROOT = Path(__file__).resolve().parents[1]
REPORTS = ROOT / 'tests/reports/v05'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def last_summary(records, pattern):
    found = []
    for row in records:
        found.extend(re.findall(pattern, row['output']))
    if not found:
        raise ValueError('Expected test summary is absent')
    return dict(zip(('passed', 'failed', 'skipped'), map(int, found[-1])))


def collect_real_client(directory):
    session_path = directory / 'session.json'
    session = json.loads(session_path.read_text(encoding='utf-8-sig'))
    records = [row for row in session['entries'] if row.get('kind') == 'rcon']
    transcript = '\n'.join(row['output'] for row in records)
    statuses = []
    for row in records:
        if row['command'] != 'zsh_v05_client status':
            continue
        statuses.extend({key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', line)}
                        for line in row['output'].splitlines() if line.startswith('[V05ClientSession]'))
    if not statuses:
        raise ValueError('Real-client session status samples are absent')
    fire = []
    for line in transcript.splitlines():
        if not line.startswith('[V05RealFire] group='):
            continue
        fields = dict(re.findall(r'(\w+)=(\S+)', line))
        fire.append({'group': fields['group'], 'uid': int(fields['uid']),
                     'interval_log_text': fields['interval'], 'interval_reported': float(fields['interval']),
                     'shots': int(fields['shots']), 'min_delta': float(fields['min_delta']),
                     'max_delta': float(fields['max_delta']), 'mean_delta': float(fields['mean_delta']),
                     'elapsed': float(fields['elapsed'])})
    death_lines = [line for line in transcript.splitlines()
                   if line.startswith('[ShelterRealMaterial] client_serial=')]
    if not death_lines:
        raise ValueError('Actual ordinary-NPC death evidence is absent')
    death = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', death_lines[-1])}
    natural = any(row.get('real_clients') == 1 and row.get('alive_real') == 1
                  and row.get('bots') == 0 and row.get('hzs_npcs') == 101
                  and row.get('phase') == 1 and row.get('running') == 1
                  and row.get('external_day_fixture') == 0 for row in statuses)
    summary = last_summary(records, r'\[ShelterTestSummary\] Passed=(\d+) Failed=(\d+) Skipped=(\d+)')
    api = last_summary(records, r'\[ShelterAPISummary\] Passed=(\d+) Failed=(\d+) Skipped=(\d+)')
    errors = sorted(path.name for path in directory.glob('errors*.log'))
    result = {
        'session': str(session_path.relative_to(ROOT)), 'session_sha256': sha(session_path),
        'status': session['status'], 'integration': summary, 'independent_api': api,
        'scope': session['scope'], 'server_bind_ip': session.get('server_bind_ip'),
        'client_connect_ip': session.get('client_connect_ip'), 'port': session['port'],
        'max_real_clients': max(row.get('real_clients', 0) for row in statuses),
        'max_living_real_clients': max(row.get('alive_real', 0) for row in statuses),
        'all_status_samples_zero_bots': all(row.get('bots') == 0 for row in statuses),
        'status_sample_count': len(statuses), 'native_registry_peak': max(row.get('hzs_npcs', 0) for row in statuses),
        'natural_hzs_day_with_101_npcs_observed': natural,
        'external_day_fixture': session['external_day_fixture'],
        'ordinary_native_death': death,
        'valid_v1_five_cell_canary_passed': '[ShelterAPI] PASS valid v1 five-cell array preserves adjacent guard cells' in transcript,
        'firing': {'groups': fire, 'group_count': len(fire),
                   'total_observed_shots': sum(row['shots'] for row in fire),
                   'source': 'Only the ten V05RealFire group rows from this session RCON transcript; no repeated log counting.',
                   'interval_note': 'Raw interval log text is preserved, including 0.699 for group 07; it is not silently rounded to 0.700.',
                   'scope': 'Ten temporary same-model configuration groups against one existing native ordinary NPC with fixture HP/damage; not a 256-entity or all-weapon combat sample.'},
        'passed_assertions': [line.removeprefix('[ShelterTest] PASS ') for line in transcript.splitlines()
                              if line.startswith('[ShelterTest] PASS ')],
        'skipped_details': [line.removeprefix('[ShelterTest] SKIP ') for line in transcript.splitlines()
                            if line.startswith('[ShelterTest] SKIP ')],
        'screenshots': session['screenshots'], 'visual_review_claim_from_session': session['visual_review'],
        'cleanup_exit_code': session['exitCode'], 'cleanup_errors': session['cleanup_errors'],
        'source_mod_error_log_files': errors,
    }
    review_path = directory / 'visual_review.json'
    if review_path.is_file():
        result['actual_visual_review'] = {'path': str(review_path.relative_to(ROOT)), 'sha256': sha(review_path),
                                         'review': json.loads(review_path.read_text(encoding='utf-8-sig'))}
    if (summary['failed'] or api['failed'] or not natural or session['external_day_fixture']
            or not result['all_status_samples_zero_bots'] or result['max_real_clients'] != 1
            or session['exitCode'] != 0 or session['cleanup_errors'] or errors
            or len(fire) != 10 or len({row['group'] for row in fire}) != 10
            or any(row['shots'] < 2 for row in fire) or not result['valid_v1_five_cell_canary_passed']
            or death.get('original_hp') != 100 or death.get('actual_death') != 1
            or death.get('bridge_delta') != 1 or death.get('core_delta') != 1):
        raise ValueError('Real-client evidence has failed, missing, or inconsistent assertions')
    return result


def collect_visual_session(directory):
    path = directory / 'session.json'
    session = json.loads(path.read_text(encoding='utf-8-sig'))
    records = [row for row in session['entries'] if row.get('kind') == 'rcon']
    summary = last_summary(records, r'\[ShelterTestSummary\] Passed=(\d+) Failed=(\d+) Skipped=(\d+)')
    statuses = [{key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', line)}
                for row in records if row['command'] == 'zsh_v05_client status'
                for line in row['output'].splitlines() if line.startswith('[V05ClientSession]')]
    errors = sorted(log.name for log in directory.glob('errors*.log'))
    result = {'session': str(path.relative_to(ROOT)), 'session_sha256': sha(path),
              'integration': summary, 'status': session['status'],
              'cleanup_exit_code': session['exitCode'], 'cleanup_errors': session['cleanup_errors'],
              'all_status_samples_zero_bots': bool(statuses) and all(row.get('bots') == 0 for row in statuses),
              'max_real_clients': max((row.get('real_clients', 0) for row in statuses), default=0),
              'screenshots': session['screenshots'], 'source_mod_error_log_files': errors,
              'scope': 'Separate visual follow-up; these assertion counts are not added to real_client_02 or engine samples.'}
    if (summary['failed'] or session['exitCode'] != 0 or session['cleanup_errors'] or errors
            or not result['all_status_samples_zero_bots'] or result['max_real_clients'] != 1):
        raise ValueError('Visual follow-up session has failed assertions or cleanup')
    return result


def collect():
    engine_dir = REPORTS / 'engine_run_04_final'
    production_dir = REPORTS / 'production_run_06_final'
    if not (production_dir / '原生主体联合RCON.json').is_file():
        production_dir = REPORTS / 'production_run_05_final'
    if not (production_dir / '原生主体联合RCON.json').is_file():
        production_dir = REPORTS / 'production_run_04'
    records = json.loads((engine_dir / '原生主体联合RCON.json').read_text(encoding='utf-8-sig'))
    production = json.loads((production_dir / '原生主体联合RCON.json').read_text(encoding='utf-8-sig'))
    migration = json.loads((REPORTS / 'final_live_migration/configs/migration_report.json').read_text(encoding='utf-8-sig'))
    source = Path(migration['source'])
    output = Path(migration['output'])
    preserved = all(sha(source / name) == digest for name, digest in migration['source_sha256'].items())
    source_resources = load(source / 'resources.cfg')['ShelterResources']
    migrated_resources = load(output / 'resources.cfg')['ShelterResources']
    source_gatherables = source_resources.get('Materials', source_resources.get('Gatherables', {}))
    gathering_preserved = True
    for key, value in source_gatherables.items():
        expected = {k: v for k, v in value.items() if k != 'display_name'}
        gathering_preserved &= migrated_resources['Gatherables'][key] == expected
    costs_preserved = True
    building_files = ('bases.cfg', 'turrets.cfg', 'flame_turrets.cfg', 'grenade_turrets.cfg',
        'mines.cfg', 'fences.cfg', 'generators.cfg', 'amplifiers.cfg', 'lights.cfg')
    for filename in building_files:
        before, after = next(iter(load(source / filename).values())), next(iter(load(output / filename).values()))
        for recipe, fields in before.get('BuildRecipes', {}).items():
            costs_preserved &= fields.get('cost', {}) == after['BuildRecipes'][recipe].get('cost', {})
        profile_key = 'Profiles' if filename == 'bases.cfg' else 'Types'
        for type_id, fields in before[profile_key].items():
            current = after[profile_key][type_id]
            costs_preserved &= fields.get('Growth', {}) == current.get('Growth', {})
            for level, row in fields['Levels'].items():
                costs_preserved &= all(current['Levels'][level].get(k) == v for k, v in row.items())
    stages = load(output / 'days.cfg')['ShelterDays']['Stages']
    weapons = source.parent / 'HanZombieScenario/HanZombieScenarioDamageConfig.cfg'
    m4 = re.search(r'"weapon_m4a1"\s+"([^"]+)"', weapons.read_text(encoding='utf-8-sig')).group(1)
    runtime_errors = json.loads((engine_dir / '原生主体联合错误分类.json').read_text(encoding='utf-8-sig'))
    production_errors = json.loads((production_dir / '原生主体联合错误分类.json').read_text(encoding='utf-8-sig'))
    baseline = json.loads((REPORTS / 'baseline/source-baseline.json').read_text(encoding='utf-8-sig'))
    changed = [row['path'] for row in baseline['files'] if sha(ROOT / row['path']) != row['actual']]
    real_client = collect_real_client(REPORTS / 'real_client_02')
    visual_path = REPORTS / 'real_client_03_visual/visual_review.json'
    visual_review = {'available': visual_path.is_file(), 'path': str(visual_path.relative_to(ROOT))}
    if visual_path.is_file():
        # Preserve the reviewer's actual findings verbatim. Artifact presence is
        # not interpreted as an all-pass visual verdict.
        visual_review['sha256'] = sha(visual_path)
        visual_review['review'] = json.loads(visual_path.read_text(encoding='utf-8-sig'))
        if (visual_path.parent / 'session.json').is_file():
            visual_review['session'] = collect_visual_session(visual_path.parent)
            documented = visual_review['review'].get('rcon_summary')
            if documented is not None and documented != visual_review['session']['integration']:
                raise ValueError('Visual review summary does not agree with the recorded session')
        picture_evidence = []
        for row in visual_review['review'].get('pictures', []):
            picture = visual_path.parent / row['file']
            if not picture.is_file():
                raise ValueError('A reviewed visual evidence image is absent')
            picture_evidence.append({'path': str(picture.relative_to(ROOT)), 'sha256': sha(picture),
                                     'review_result': row.get('result')})
        visual_review['reviewed_picture_evidence'] = picture_evidence
    report = {
        'version': '0.5.0-dev', 'date': '2026-10-15', 'timezone': 'Asia/Shanghai',
        'source_baseline': {'checked': len(baseline['files']), 'all_matched_before_implementation': all(x['matches'] for x in baseline['files']),
            'modified_baseline_files': changed},
        'integration': last_summary(records, r'\[ShelterTestSummary\] Passed=(\d+) Failed=(\d+) Skipped=(\d+)'),
        'integration_evidence_directory': str(engine_dir.relative_to(ROOT)),
        'independent_api': last_summary(records, r'\[ShelterAPISummary\] Passed=(\d+) Failed=(\d+) Skipped=(\d+)'),
        'api_scope': 'Engine-only initial/late snapshots, provider unload and optional absence; real_client_02 additionally verifies the valid-client v1 five-cell canary and v2 per-ID agreement.',
        'real_client': real_client,
        'visual_review': visual_review,
        'integration_cases': [r['command'] for r in records if r['command'].startswith('zsh_test ')],
        'engine_error_lines': runtime_errors['runtime_error_line_count'],
        'production_error_lines': production_errors['runtime_error_line_count'],
        'production_evidence_directory': str(production_dir.relative_to(ROOT)),
        'production_test_command_absent': any(r['command'] == 'zsh_test summary' and 'Unknown command' in r['output'] for r in production),
        'production_plugins': next(r['output'] for r in production if r['command'] == 'sm plugins list'),
        'zero_bot_sample': next(r['output'] for r in records if r['command'] == 'status'),
        'recording_server': {'modified': False, 'port': 27129, 'all_original_config_hashes_still_match': preserved,
            'gathering_fields_preserved': gathering_preserved, 'original_build_level_growth_costs_preserved': costs_preserved,
            'all_days_20_60': all(s['day_target_alive'] == '20' and s['night_target_alive'] == '60' for s in stages.values()),
            'm4a1_rule': m4, 'migration_preview': str(output)},
        'builds': {'main': {'path': 'addons/sourcemod/plugins/HanZombieShelter.smx', 'sha256': sha(ROOT / 'addons/sourcemod/plugins/HanZombieShelter.smx')},
            'probe': {'path': 'tests/compiled/ShelterIntegrationProbe.smx', 'sha256': sha(ROOT / 'tests/compiled/ShelterIntegrationProbe.smx')},
            'consumer': {'path': 'tests/compiled/ShelterAPIConsumer.smx', 'sha256': sha(ROOT / 'tests/compiled/ShelterAPIConsumer.smx')}},
        'not_verified': ['Terminal Boss kill/drop/outcome path',
            'Facility owner disconnect/offline reward attribution through the complete live-client lifecycle',
            'All shipped weapons and defense types in real combat beyond the sampled machine-gun firing cadence',
            'BOT regression: no verified patch installed; no BOT or fake client was added',
            'Real multiplayer/concurrency/bandwidth/long load and 256 physical buildings',
            'Runtime map change/local engine crash boundary'],
    }
    if not visual_review['available']:
        report['not_verified'].append('Independent visual acceptance of captured frames; visual_review.json is not yet present')
    management_dir = REPORTS / 'real_client_04_management'
    if (management_dir / 'session.json').is_file():
        report['management_visual'] = collect_visual_session(management_dir)
        review = management_dir / 'visual_review.json'
        if review.is_file():
            report['management_visual']['actual_review'] = json.loads(review.read_text(encoding='utf-8-sig'))
    if report['integration']['failed'] or report['independent_api']['failed'] or not all((preserved, gathering_preserved, costs_preserved)):
        raise ValueError('Delivery evidence has a failed assertion or preservation mismatch')
    target = REPORTS / 'final/summary.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('version', 'integration', 'independent_api', 'engine_error_lines', 'production_error_lines', 'production_test_command_absent', 'recording_server')}, ensure_ascii=False, indent=2))
    print(json.dumps({'real_client': real_client['integration'], 'real_client_api': real_client['independent_api'],
        'real_firing_groups': real_client['firing']['group_count'], 'real_observed_shots': real_client['firing']['total_observed_shots'],
        'native_registry_peak': real_client['native_registry_peak'], 'zero_bots': real_client['all_status_samples_zero_bots'],
        'original_ordinary_hp': real_client['ordinary_native_death']['original_hp'],
        'cleanup_exit_code': real_client['cleanup_exit_code'], 'visual_review_available': visual_review['available'],
        'visual_session': visual_review.get('session', {}).get('integration'),
        'visual_cleanup_exit_code': visual_review.get('session', {}).get('cleanup_exit_code'),
        'production_evidence_directory': report['production_evidence_directory']}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    collect()
