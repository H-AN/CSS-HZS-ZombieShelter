"""Export a reversible, low-cost recording profile; --apply writes live configs only.

Does not reload maps, change plugin binaries, seed inventories, or touch HZS source.
"""
from pathlib import Path
import argparse, datetime, hashlib, json, re, shutil

REPO = Path(__file__).resolve().parents[1]
SERVER = Path(r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')
HANDOFF = Path(r'D:\gmod昼夜求生资源\文档\2026年10月15日_v0.4_进度文档')
CONFIG = Path('cstrike/addons/sourcemod/configs')

def parse(text):
    tokens = re.findall(r'"((?:\\.|[^"\\])*)"|([{}])|//[^\n]*', text)
    items = [a if a else b for a, b in tokens if a or b]
    pos = 0
    def block(nested=False):
        nonlocal pos
        result = {}
        while pos < len(items):
            key = items[pos]; pos += 1
            if key == '}':
                if not nested: raise ValueError('Unexpected }')
                return result
            if pos >= len(items): raise ValueError('Missing value')
            value = items[pos]; pos += 1
            if value == '{': value = block(True)
            if key in result: raise ValueError(f'Duplicate key {key}')
            result[key] = value
        if nested: raise ValueError('Missing }')
        return result
    return block()

def emit(data, level=0):
    lines = []; indent = '    ' * level
    for key, value in data.items():
        if isinstance(value, dict):
            lines += [f'{indent}"{key}"', indent+'{', emit(value, level+1), indent+'}']
        else: lines.append(f'{indent}"{key}" "{value}"')
    return '\n'.join(lines)

def load(path): return parse(path.read_text(encoding='utf-8-sig'))
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    output = HANDOFF / '03_录制演示配置' / '生效配置'
    backup = SERVER / 'zsh_test_backup' / f'v04_demo_{stamp}'
    changed = {}; recipe_rows = []; upgrade_rows = []
    base = SERVER / CONFIG / 'HanZombieShelter'
    docs = {}
    for path in base.rglob('*.cfg'):
        docs[path.relative_to(base).as_posix()] = load(path)
    def root(name): return next(iter(docs[name].values()))
    root('mode.cfg')['Rules'].update(day_seconds='300', night_seconds='120')
    resources = root('resources.cfg')
    for material, values in resources['Materials'].items():
        values.update(yield_per_successful_hit='3', grow_seconds='10')
    resources['Pools']['desert_mixed']['Weights'].update(wood='45', iron='40', copper='10', silver='4', gold='1')
    for stage in root('days.cfg')['Stages'].values():
        stage.update(day_target_alive='20', night_target_alive='60', raider_hp_multiplier='1.0',
                     max_raider_hp='150', spawn_batch='6', spawn_interval_seconds='0.5')
        stage['Cvars'].update(hzs_maxzombiespawn='60')
        if 'extra_hp_multiplier_per_day' in stage: stage['extra_hp_multiplier_per_day']='0.0'
        if 'hp_multiplier_cap' in stage: stage['hp_multiplier_cap']='1.0'
        if 'night_target_cap' in stage: stage['night_target_cap']='60'
    root('bosses.cfg')['BossTypes']['guard_fat']['max_hp']='3000'
    recipes = {
        'build_basic_turret':(3,2), 'build_heavy_turret':(4,3), 'build_flame_turret':(3,3),
        'build_fence':(2,0), 'build_mine':(0,2), 'build_damage_amplifier':(3,3),
        'build_generator':(4,3), 'build_lamp':(2,1), 'build_grenade_turret':(4,4),
    }
    facility_files = ('bases.cfg','turrets.cfg','flame_turrets.cfg','fences.cfg','mines.cfg',
                      'amplifiers.cfg','generators.cfg','lights.cfg','grenade_turrets.cfg')
    for name in facility_files:
        cfg = root(name)
        for recipe, values in cfg.get('BuildRecipes',{}).items():
            wood, iron = recipes[recipe]; values['cost'] = {'wood':str(wood),'iron':str(iron)}
            recipe_rows.append({'recipe':recipe,'wood':wood,'iron':iron,'copper':0,'silver':0,'gold':0})
        for kind, profile in cfg.get('Types',cfg.get('Profiles',{})).items():
            for level, values in profile.get('Levels',{}).items():
                if int(level)>1:
                    wood, iron = (int(level),int(level)+1)
                    values['upgrade_cost']={'wood':str(wood),'iron':str(iron)}
                    upgrade_rows.append({'file':name,'type':kind,'target_level':int(level),'wood':wood,'iron':iron})
    # Export the whole active config tree, not just the cheaper rows.
    for name, data in docs.items():
        relative = CONFIG / 'HanZombieShelter' / name
        changed[relative] = ('// v0.4 recording profile, 2026-10-15. Separate from release defaults.\n'+emit(data)+'\n').encode('utf-8')
    damage_relative = CONFIG / 'HanZombieScenario/HanZombieScenarioDamageConfig.cfg'
    damage = load(SERVER / damage_relative)
    old_rule = damage['ScenarioDmgConfig'].get('weapon_m4a1')
    damage['ScenarioDmgConfig']['weapon_m4a1']='+0.0'
    changed[damage_relative] = ('// Recording profile: M4A1 keeps normal engine damage; avoid negative damage healing NPCs.\n'+emit(damage)+'\n').encode('utf-8')
    records = []
    for relative, content in changed.items():
        source = SERVER / relative; target = output / relative
        if args.apply:
            preserved = backup / relative; preserved.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,preserved)
        target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(content)
        records.append({'server_relative':relative.as_posix(),'before_sha256':sha(source),
                        'profile_sha256':sha(target),'bytes':len(content)})
    # Preserve the bridge and NPC templates as reference, without changing them.
    for relative in (CONFIG/'HanZombieScenario/HanZombieScenarioZombieData.cfg',
                     CONFIG/'HanZombieScenario/zsh_desertplant_cso_v1_d/HanZombieScenarioStage.cfg'):
        target=output/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(SERVER/relative,target)
    report={'created':datetime.datetime.now().isoformat(),'server_root':str(SERVER),'port':27129,
            'profile':'recording-v04','applied':args.apply,'backup':str(backup) if args.apply else None,
            'previous_m4a1_rule':old_rule,'current_m4a1_rule':'+0.0','changed':records,
            'recipes':recipe_rows,'upgrades':upgrade_rows,'ordinary_template_hp':100,
            'ordinary_activation_hp_cap':150,'boss_hp':3000,
            'core_source_modified':False,'release_defaults_modified':False}
    if args.apply:
        for relative, content in changed.items(): (SERVER/relative).write_bytes(content)
        for item in records:
            item['live_sha256']=sha(SERVER/item['server_relative'])
            assert item['live_sha256']==item['profile_sha256']
    report_path=HANDOFF/'03_录制演示配置'/'配置应用记录.json'
    report_path.parent.mkdir(parents=True,exist_ok=True)
    report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    if args.apply:
        shutil.copy2(report_path,backup/'配置应用记录.json')
    print(json.dumps({'applied':args.apply,'configs':len(records),'backup':report['backup'],'report':str(report_path)},ensure_ascii=False))

if __name__=='__main__':main()
