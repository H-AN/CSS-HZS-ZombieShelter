"""Formal v0.8.3 handoff; restart only the explicitly approved diagnostic session."""
from pathlib import Path
import ctypes
import json
import shutil
import subprocess
import sys
import time

import test_v083_live as live

APPROVED_SESSION = '47d9d7b82745462fb800342e3c8fd768'

def restart():
    assert '--approved-test-restart' in sys.argv, 'Requires explicit approval for this test session'
    session = live.legacy.owned_server()
    assert session['session_id'] == APPROVED_SESSION and session['main_sha256'] == '0722b121f93dc158284598ab3589cdefec6efe96af8797cb3dbb774e72ef8e86', 'Preserve every other session'
    formal = json.loads((live.REPORT / 'formal_build.json').read_text(encoding='utf-8'))
    source = Path(formal['output'])
    assert live.legacy.sha(source) == formal['sha256']
    for relative, expected in formal['production_sources'].items():
        assert live.legacy.sha(live.ROOT / relative) == expected, relative
    assert live.legacy.sha(live.ROOT / 'addons/sourcemod/scripting/HanZombieShelter.sp') == formal['production_main_sha256']
    clients = [p for p in live.legacy.processes() if p['ProcessName'] == 'cstrike_win64']
    for name in ['V083CloseAim.smx', 'hzsh_v082_close_panels.smx']:
        target = live.legacy.GAME / 'addons/sourcemod/plugins/tests' / name
        if target.exists():
            assert target.relative_to(live.legacy.GAME).as_posix() not in json.loads((live.REPORT / 'live_before.json').read_text(encoding='utf-8'))['files']
            live.request('sm plugins unload tests/' + name, 'before_restart_unload_' + name[:-4].lower())
            target.unlink()
    receipt = {'at': live.legacy.now(), 'approved_scope': 'User explicitly approved restarting only this newly created diagnostic test server, preserving the ordinary client', 'previous_session': session, 'preserved_clients': clients, 'formal_sha256': formal['sha256']}
    live.write('authorized_restart_preflight.json', receipt)
    # The fixed controller intentionally has no stop command. Close only the
    # verified server's own console, without titles/command lines or credentials.
    windows = live.legacy.minimize_server(session['pid'])
    assert windows['minimized_or_hidden'] and windows['windows']
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    user.PostMessageW.restype = ctypes.c_int
    for row in windows['windows']:
        assert user.PostMessageW(row['hwnd'], 0x0010, 0, 0), 'Cannot close the selected console'
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and any(p['ProcessName'] == 'srcds_win64' for p in live.legacy.processes()):
        time.sleep(.2)
    assert not any(p['ProcessName'] == 'srcds_win64' for p in live.legacy.processes()), 'No forced kill or second server; investigate the original console'
    assert [p for p in live.legacy.processes() if p['ProcessName'] == 'cstrike_win64'] == clients, 'Client process changed unexpectedly'
    time.sleep(.4)
    target = live.legacy.GAME / 'addons/sourcemod/plugins/HanZombieShelter.smx'
    shutil.copy2(source, target)
    repo_target = live.ROOT / 'addons/sourcemod/plugins/HanZombieShelter.smx'
    assert live.legacy.sha(repo_target) == 'a02f00dcbe0ae4c054d83bda3b018da65ca1671ba3a9035783be5e1b67e06ae0', 'Do not overwrite unrelated repo changes'
    shutil.copy2(source, repo_target)
    started = live.legacy.controller('--start', '--map', 'zsh_deadcity_han_dn')
    assert started['session_id'] != APPROVED_SESSION and started['main_sha256'] == formal['sha256'] and started['ready']
    receipt.update(completed=live.legacy.now(), new_session=started)
    live.write('formal_cold_start.json', receipt)
    print(json.dumps({'ready': started['ready'], 'pid': started['pid'], 'session_id': started['session_id'], 'sha256': formal['sha256'], 'client_preserved': True}, ensure_ascii=True))

if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    restart()
