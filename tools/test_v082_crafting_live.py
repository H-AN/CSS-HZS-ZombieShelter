"""Explicit v0.8.2 test actions against the sole fixed local environment.

No action runs at import time. Never reads RCON credentials or starts a second
server. Client actions minimize the owned server and close native panels; test
input also requires a JPEG that has actually been reviewed with view_image.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys
import time

from local_test_environment import TEST_ROOT as ROOT, CONTROLLER
from local_rcon import command
from minimize_owned_server import minimize_server

REPO = Path(__file__).resolve().parents[1]
GAME = ROOT / 'cstrike'
CLIENT = Path(r'D:\SteamLibrary\steamapps\common\Counter-Strike Source\cstrike')
PYTHON = Path(r'D:\python\python.exe')
COMPILER = Path(r'D:\css\sp\sourcemod-1.12.0hzs套件\addons\sourcemod\scripting\spcomp64.exe')
INCLUDES = COMPILER.parent / 'include'
REPORT = ROOT / 'reports/v082_crafting_20261025'
BUILD = REPO / 'tests/reports/v082_crafting_20261025/build'
BACKUP = ROOT / 'backups/v082_crafting_20261025'


def now():
    return datetime.now(timezone(timedelta(hours=8))).isoformat()


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def processes():
    query = "@(Get-Process srcds_win64,cstrike_win64 -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,Path) | ConvertTo-Json -Compress"
    result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', query],
                            stdin=subprocess.DEVNULL, capture_output=True, encoding='utf-8',
                            creationflags=subprocess.CREATE_NO_WINDOW, check=True)
    rows = json.loads(result.stdout or '[]')
    return rows if isinstance(rows, list) else [rows]


def controller(*args):
    result = subprocess.run([str(PYTHON), '-B', '-X', 'utf8', str(CONTROLLER), *args],
                            stdin=subprocess.DEVNULL, capture_output=True, encoding='utf-8',
                            creationflags=subprocess.CREATE_NO_WINDOW, timeout=65)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return json.loads(result.stdout)


def owned_server():
    session = json.loads((ROOT / 'control/session.json').read_text('utf-8'))
    servers = [p for p in processes() if p['ProcessName'] == 'srcds_win64']
    assert len(servers) == 1 and servers[0]['Id'] == session['pid'], 'Sole server identity changed'
    assert Path(servers[0]['Path']).resolve() == (ROOT.parent / 'srcds_win64.exe').resolve()
    return session


def minimize():
    session = owned_server()
    result = minimize_server(session['pid'])
    assert result['minimized_or_hidden'], result
    write(REPORT / 'server_window_latest.json', {'at': now(), **result})
    return result


def compile_source(source, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(COMPILER), str(source), '-i' + str(REPO / 'addons/sourcemod/scripting'),
                             '-i' + str(REPO / 'addons/sourcemod/scripting/include'),
                             '-i' + str(INCLUDES), '-o' + str(output)], stdin=subprocess.DEVNULL,
                            capture_output=True, encoding='utf-8', errors='replace')
    log = result.stdout + result.stderr
    (output.parent / (output.stem + '_compile.log')).write_text(log, encoding='utf-8')
    assert result.returncode == 0 and not re.search(r'\b(?:error|warning)\s+\d+', log, re.I), log
    return {'at': now(), 'output': str(output), 'sha256': sha(output), 'compiler': str(COMPILER), 'log': log}


def build(diagnostic=False):
    original = REPO / 'addons/sourcemod/scripting/HanZombieShelter.sp'
    source = original
    if diagnostic:
        text = original.read_text('utf-8-sig')
        marker = 'public Plugin myinfo='
        assert text.count(marker) == 1
        text = text.replace(marker, '#include "' + (REPO / 'tests/V082CraftingLiveProbe.inc').as_posix() + '"\n\n' + marker)
        marker = 'public void OnPluginStart()\n{'
        assert text.count(marker) == 1
        text = text.replace(marker, marker + '\n    RegisterV082CraftingProbe();')
        source = BUILD / 'HanZombieShelterV082Diagnostic.sp'
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(text, encoding='utf-8')
    output = BUILD / ('HanZombieShelterV082Diagnostic.smx' if diagnostic else 'HanZombieShelter.smx')
    receipt = REPORT / ('diagnostic_build.json' if diagnostic else 'formal_build.json')
    if output.exists():
        history = BUILD / 'history' / (output.stem + '_' + sha(output) + '.smx')
        history.parent.mkdir(parents=True, exist_ok=True)
        if not history.exists():
            shutil.copy2(output, history)
    if receipt.exists():
        prior = json.loads(receipt.read_text('utf-8'))
        write(REPORT / 'build_history' / (receipt.stem + '_' + prior['sha256'] + '.json'), prior)
    row = compile_source(source, output)
    row.update(diagnostic=diagnostic, production_main_sha256=sha(original),
               production_sources={str(p.relative_to(REPO)).replace('\\', '/'): sha(p)
                   for p in sorted((REPO / 'addons/sourcemod/scripting/HanZombieShelter').glob('*.inc'))})
    write(receipt, row)
    return row


def backup(path):
    target = BACKUP / path.relative_to(GAME)
    if path.exists() and not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)


def deploy_configs():
    assert not any(p['ProcessName'] == 'srcds_win64' for p in processes()), 'Preserve running server'
    source = REPO / 'addons/sourcemod/configs/HanZombieShelter'
    rows = []
    for name in ('workbenches.cfg', 'weapon_crafting.cfg', 'material_crafting.cfg'):
        destination = GAME / 'addons/sourcemod/configs/HanZombieShelter' / name
        backup(destination)
        shutil.copy2(source / name, destination)
        rows.append({'path': str(destination), 'sha256': sha(destination)})
    destination = GAME / 'addons/sourcemod/configs/HanZombieShelter/weapons.cfg'
    backup(destination)
    text = destination.read_bytes().decode('utf-8')
    values = {'beijixing': 340, 'beijixing2': 340, 'chongsu': 340, 'xintiao': 250,
              'xintiao2': 250, 'lianming': 320, 'longya': 320, 'zhuixing': 330,
              'wange': 370, 'apexshuangren': 500}
    updates = []
    for name, value in values.items():
        block = re.compile(r'("delta_' + re.escape(name) + r'"\s*\{)([^{}]*)(\})')
        matches = list(block.finditer(text))
        assert len(matches) == 1, f'Expected one existing knife block: {name}'
        match = matches[0]
        content = match.group(2)
        for key in ('repair_hp_per_hit', 'construction_hp_per_hit'):
            pattern = re.compile(r'("' + key + r'"\s*")([0-9]+)(")')
            found = pattern.findall(content)
            assert len(found) == 1 and int(found[0][1]) in (value // 10, value), (name, key, found)
            content = pattern.sub(lambda m: m.group(1) + str(value) + m.group(3), content)
            updates.append({'knife': name, 'field': key, 'before': int(found[0][1]), 'after': value})
        text = text[:match.start(2)] + content + text[match.end(2):]
    destination.write_bytes(text.encode('utf-8'))
    rows.append({'path': str(destination), 'sha256': sha(destination), 'updates': updates})
    # Preserve every existing translation, appending only this release's phrases.
    destination = GAME / 'addons/sourcemod/translations/HanZombieShelter.phrases.txt'
    backup(destination)
    current = destination.read_bytes().decode('utf-8')
    source_text = (REPO / 'addons/sourcemod/translations/HanZombieShelter.phrases.txt').read_text('utf-8-sig')
    for key in ('BuildWorkbenchName', 'HUDWorkbenchUseHint', 'CraftingOpen'):
        if re.search(r'"' + key + r'"\s*\{', current):
            continue
        match = re.search(r'    "' + key + r'"\s*\{[^{}]*\}', source_text)
        assert match, key
        end = current.rfind('}')
        assert end >= 0
        newline = '\r\n' if '\r\n' in current else '\n'
        block = match.group(0).replace('\r\n', '\n').replace('\n', newline)
        current = current[:end] + block + newline + current[end:]
    destination.write_bytes(current.encode('utf-8'))
    rows.append({'path': str(destination), 'sha256': sha(destination)})
    parse = runpy.run_path(str(REPO / 'tools/migrate_v05_materials.py'))['parse']
    before = parse((BACKUP / 'addons/sourcemod/configs/HanZombieShelter/weapons.cfg').read_text('utf-8-sig'))
    after = parse((GAME / 'addons/sourcemod/configs/HanZombieShelter/weapons.cfg').read_text('utf-8-sig'))
    for name, value in values.items():
        before['ShelterWeapons']['Weapons']['delta_' + name]['repair_hp_per_hit'] = str(value)
        before['ShelterWeapons']['Weapons']['delta_' + name]['construction_hp_per_hit'] = str(value)
    assert before == after, 'Unexpected live weapon configuration change'
    result = {'at': now(), 'scope': 'Three new cfgs, 20 authorized knife HP leaves and 3 new phrases; existing cfgs preserved', 'files': rows}
    write(REPORT / 'config_deployment.json', result)
    return result


def panels(require_client=True):
    minimize()
    result = command('sm_hzsh_test_close_panels')
    if '[CssTestPanels] closed=1' not in result:
        output = BUILD / 'hzsh_v082_close_panels.smx'
        compile_source(REPO / 'tools/fixtures/CssTestPanels.sp', output)
        destination = GAME / 'addons/sourcemod/plugins/tests' / output.name
        destination.parent.mkdir(exist_ok=True)
        assert not destination.exists(), 'Unknown existing test helper'
        shutil.copy2(output, destination)
        loaded = command('sm plugins load tests/' + output.name)
        assert 'successfully' in loaded.lower(), loaded
        result = command('sm_hzsh_test_close_panels')
    assert '[CssTestPanels] closed=1' in result, result
    if require_client:
        assert 'real_clients=1' in result, result
    return result


def client(text, label, bootstrap=False):
    assert re.fullmatch(r'[a-z0-9_]+', label)
    assert not re.search(r'\b(record|stop|quit|disconnect|bot_add|bot_quota)\b', text, re.I)
    if not bootstrap:
        gate = json.loads((REPORT / 'client_visual_gate.json').read_text('utf-8'))
        assert gate['clear_confirmed'] and gate['server_pid'] == owned_server()['pid']
    panels(require_client=not bool(re.search(r'(?m)^connect\s+', text)))
    filename = 'hzsh_v082_' + label + '.cfg'
    saved = REPORT / 'client_cfg' / filename
    saved.parent.mkdir(parents=True, exist_ok=True)
    saved.write_text('gameui_hide\n' + text.rstrip() + '\n', encoding='utf-8')
    shutil.copy2(saved, CLIENT / 'cfg' / filename)
    clients = [p for p in processes() if p['ProcessName'] == 'cstrike_win64']
    args = [str(CLIENT.parent / 'cstrike_win64.exe'), '-game', 'cstrike', '+exec', filename]
    if not clients:
        assert bootstrap and re.search(r'(?m)^connect\s+', text), 'Only an explicit bootstrap may launch the ordinary client'
        # A first game launch is a retained GUI process, not a short-lived
        # hijack sender. Never wrap it in run(timeout=...), which would kill it.
        subprocess.Popen(args, cwd=CLIENT.parent, stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        assert len(clients) == 1, 'Cannot uniquely identify the ordinary client'
        args.insert(1, '-hijack')
        subprocess.run(args, cwd=CLIENT.parent, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE, check=True, timeout=20)


def capture(label, bootstrap=False):
    filename = 'hzsh_v082_' + label
    source = CLIENT / 'screenshots' / (filename + '.jpg')
    destination = REPORT / 'screenshots' / source.name
    assert not source.exists() and not destination.exists(), 'Never overwrite screenshot evidence'
    client('jpeg ' + filename + ' 100', label + '_jpeg', bootstrap=bootstrap)
    deadline = time.monotonic() + 15
    previous = -1
    stable = 0
    while time.monotonic() < deadline:
        if source.exists():
            data = source.read_bytes()
            stable = stable + 1 if data.endswith(b'\xff\xd9') and len(data) == previous else 0
            previous = len(data)
            if stable >= 2:
                break
        time.sleep(.15)
    assert stable >= 2, 'No complete stable client JPEG'
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    result = {'at': now(), 'path': str(destination), 'sha256': sha(destination), 'server_pid': owned_server()['pid']}
    write(REPORT / (label + '_capture.json'), result)
    return result


def tap(button, label):
    """One gated real-client button press and release, without frame coalescing."""
    assert button in ('attack', 'use') and re.fullmatch(r'[a-z0-9_]+', label)
    gate = json.loads((REPORT / 'client_visual_gate.json').read_text('utf-8'))
    assert gate['clear_confirmed'] and gate['server_pid'] == owned_server()['pid']
    panels()
    assert len([p for p in processes() if p['ProcessName'] == 'cstrike_win64']) == 1
    def edge(sign, suffix):
        filename = 'hzsh_v082_' + label + '_' + suffix + '.cfg'
        saved = REPORT / 'client_cfg' / filename
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_text('gameui_hide\n' + sign + button + '\n', encoding='utf-8')
        shutil.copy2(saved, CLIENT / 'cfg' / filename)
        subprocess.run([str(CLIENT.parent / 'cstrike_win64.exe'), '-hijack', '-game', 'cstrike', '+exec', filename],
                       cwd=CLIENT.parent, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE, check=True, timeout=10)
    try:
        edge('+', 'down')
        time.sleep(.12)
    finally:
        edge('-', 'up')
    return {'at': now(), 'button': button, 'hold_seconds': .12, 'label': label}


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'build-diagnostic', 'deploy-configs', 'install', 'start', 'command', 'client', 'tap', 'capture', 'confirm-visual', 'status'])
    parser.add_argument('--text', default='')
    parser.add_argument('--label', default='')
    parser.add_argument('--bootstrap', action='store_true')
    args = parser.parse_args()
    if args.action in ('build', 'build-diagnostic'):
        result = build(args.action == 'build-diagnostic')
    elif args.action == 'deploy-configs':
        result = deploy_configs()
    elif args.action == 'install':
        result = controller('--install-main', args.text)
    elif args.action == 'start':
        result = controller('--start')
        minimize()
    elif args.action == 'command':
        result = {'at': now(), 'command': args.text, 'output': command(args.text)}
        if args.label:
            write(REPORT / (args.label + '.json'), result)
    elif args.action == 'client':
        client(args.text, args.label, args.bootstrap)
        result = {'sent': True, 'label': args.label}
    elif args.action == 'capture':
        result = capture(args.label, args.bootstrap)
    elif args.action == 'tap':
        result = tap(args.text, args.label)
    elif args.action == 'confirm-visual':
        captured = json.loads((REPORT / (args.label + '_capture.json')).read_text('utf-8'))
        assert sha(Path(captured['path'])) == captured['sha256']
        result = {**captured, 'clear_confirmed': True, 'review': 'Reviewed using view_image; native panels absent'}
        write(REPORT / 'client_visual_gate.json', result)
    else:
        result = {'processes': processes(), 'controller': controller('--status')}
    if args.action in ('build', 'build-diagnostic'):
        result = {key: result[key] for key in ('output', 'sha256', 'diagnostic', 'log')}
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
