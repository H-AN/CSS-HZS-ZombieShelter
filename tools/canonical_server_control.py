"""Installed as <fixed local test root>/tools/server_control.py.

The RCON credential is generated in controller memory and is never written to
disk or printed. Requests and responses use the local control directory.
"""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import argparse
import ctypes
import hashlib
import json
import os
import re
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import time
import uuid

SERVER = Path(r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')
ROOT = SERVER / '昼夜求生本地测试服务器'
PLAN = ROOT
GAME = ROOT / 'cstrike'
CONTROL = ROOT / 'control'
PORT = 27169
ADDRESS = '192.168.0.102:27169'
MAP = 'zsh_desert_han_dn'
ALLOWED_MAPS = (MAP, 'zsh_deadcity_han_dn')


def now():
    return datetime.now(timezone(timedelta(hours=8))).isoformat()


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def session():
    path = CONTROL / 'session.json'
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else None


def pid_running(pid):
    if not isinstance(pid, int) or pid < 1:
        return False
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        return False
    try:
        code = ctypes.c_uint32()
        return bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259
    finally:
        kernel.CloseHandle(handle)


def server_processes():
    query = "@(Get-Process srcds_win64,srcds -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,Path) | ConvertTo-Json -Compress"
    result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', query],
                            capture_output=True, text=True, encoding='utf-8', errors='replace',
                            creationflags=subprocess.CREATE_NO_WINDOW, check=True)
    rows = json.loads(result.stdout or '[]')
    return rows if isinstance(rows, list) else [rows]


def assert_stopped():
    if server_processes():
        raise RuntimeError('A dedicated server is already running. Preserve it; do not create a second session.')
    previous = session()
    if previous and pid_running(previous.get('controller_pid')):
        raise RuntimeError('The existing local test controller is still running.')


def check(require_main=False):
    if Path(__file__).resolve().parent != (ROOT / 'tools').resolve():
        raise RuntimeError('Launch only the controller installed inside the fixed local test environment.')
    for path in [SERVER / 'srcds_win64.exe', GAME / 'gameinfo.txt',
                 GAME / 'cfg/local_test_server.cfg', ROOT / '.local-test-environment.json']:
        if not path.is_file():
            raise RuntimeError('Local test prerequisite missing: ' + str(path))
    main = GAME / 'addons/sourcemod/plugins/HanZombieShelter.smx'
    if not main.is_file():
        raise RuntimeError('The deployed formal main plugin is missing.')
    return {'prepared': True, 'game': str(GAME), 'server_executable': str(SERVER / 'srcds_win64.exe'),
            'port': PORT, 'address': ADDRESS, 'main_sha256': digest(main), 'zero_bots': True}


def chosen_map_name(map_name=None):
    chosen_map = MAP if map_name is None else map_name
    if chosen_map not in ALLOWED_MAPS:
        raise ValueError('Local test map must be one of: ' + ', '.join(ALLOWED_MAPS))
    return chosen_map


def check_map(map_name):
    chosen_map = chosen_map_name(map_name)
    files = {
        'bsp': GAME / 'maps' / (chosen_map + '.bsp'),
        'shelter_map': GAME / 'addons/sourcemod/configs/HanZombieShelter/maps' / (chosen_map + '.cfg'),
        'hzs_stage': GAME / 'addons/sourcemod/configs/HanZombieScenario' / chosen_map / 'HanZombieScenarioStage.cfg',
    }
    for kind, path in files.items():
        if not path.is_file():
            raise RuntimeError('Local test map prerequisite missing (' + kind + '): ' + str(path))
    return {'map': chosen_map, 'map_files': {kind: str(path) for kind, path in files.items()}}


def install_main(source):
    check()
    assert_stopped()
    source = Path(source).resolve()
    if not source.is_file() or source.suffix.lower() != '.smx':
        raise ValueError('A compiled formal SMX is required.')
    target = GAME / 'addons/sourcemod/plugins/HanZombieShelter.smx'
    backup = ROOT / 'backups/main_plugins' / (digest(target) + '.smx')
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(target, backup)
    if source != target.resolve():
        shutil.copy2(source, target)
    record = {'at': now(), 'source': str(source), 'target': str(target), 'sha256': digest(target), 'started': False}
    write(CONTROL / 'main_installation.json', record)
    print(json.dumps(record, ensure_ascii=False))


def packet(sock, identifier, kind, text):
    body = struct.pack('<2i', identifier, kind) + text.encode('utf-8') + b'\0\0'
    sock.sendall(struct.pack('<i', len(body)) + body)


def receive(sock):
    def exact(count):
        data = b''
        while len(data) < count:
            chunk = sock.recv(count - len(data))
            if not chunk:
                raise ConnectionError('RCON closed')
            data += chunk
        return data
    length = struct.unpack('<i', exact(4))[0]
    if not 10 <= length <= 1048576:
        raise ValueError('RCON packet length invalid')
    body = exact(length)
    return (*struct.unpack_from('<2i', body), body[8:-2].decode('utf-8', errors='replace'))


def rcon(secret, text):
    with socket.create_connection(('127.0.0.1', PORT), timeout=3) as sock:
        sock.settimeout(8)
        packet(sock, 1, 3, secret)
        while True:
            identifier, kind, _ = receive(sock)
            if kind == 2:
                if identifier < 0:
                    raise RuntimeError('Own RCON authentication rejected')
                break
        packet(sock, 2, 2, text)
        packet(sock, 3, 2, 'echo HZSH_LOCAL_COMMAND_END')
        output = []
        while True:
            identifier, _, value = receive(sock)
            if identifier == 3:
                return ''.join(output)
            if identifier == 2:
                output.append(value)


def allowed(text):
    # Preserve the established no-BOT, no-failed-patch and no-session-reset rules.
    blocked = ['bot_add', 'fakeclient', 'bot_quota', 'bot_kill', 'rcon_password', 'password',
               'sm_zsh_fail', 'quit', 'disconnect', 'changelevel', 'cvarlist', 'hzs_botaddfix',
               'focused_follow', 'guarded_bot', 'hzsh_followrange_fix']
    if not isinstance(text, str) or not 0 < len(text) <= 16384:
        return False
    if any(token in text.lower() for token in blocked):
        return False
    if re.search(r'(?im)(?:^|[;\r\n])\s*(?:exec|map)\b', text):
        return False
    return True


def minimize_owned_server_window(pid):
    helper = ROOT / 'tools/minimize_owned_server.py'
    result = subprocess.run([sys.executable, '-B', '-X', 'utf8', str(helper), '--pid', str(pid)],
                            capture_output=True, text=True, encoding='utf-8', errors='replace',
                            timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        row = json.loads(result.stdout)
    except ValueError:
        row = {'server_pid': pid, 'minimized_or_hidden': False, 'helper_exit_code': result.returncode}
    write(CONTROL / 'server_window.json', row)
    return row.get('minimized_or_hidden', False)


def daemon(map_name=None):
    chosen_map = chosen_map_name(map_name)
    check(require_main=True)
    check_map(chosen_map)
    assert_stopped()
    for kind in [socket.SOCK_STREAM, socket.SOCK_DGRAM]:
        with socket.socket(socket.AF_INET, kind) as sock:
            sock.bind(('0.0.0.0', PORT))
    secret = secrets.token_hex(24)
    env = os.environ.copy()
    env.update(TEMP=str(ROOT / 'temp'), TMP=str(ROOT / 'temp'))
    arguments = [str(SERVER / 'srcds_win64.exe'), '-game', str(GAME), '-console', '-insecure', '-nohltv', '-dev',
                 '-ip', '0.0.0.0', '-port', str(PORT), '+maxplayers', '32', '+sv_lan', '1', '+sv_cheats', '1', '+sv_pure', '0',
                 '+servercfgfile', 'local_test_server.cfg', '+rcon_password', secret,
                 '+con_logfile', 'local_test_runtime.log', '+bot_quota', '0', '+map', chosen_map]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    try:
        process = subprocess.Popen(arguments, cwd=ROOT, env=env, startupinfo=startup,
                                   creationflags=subprocess.CREATE_NEW_CONSOLE | subprocess.CREATE_NEW_PROCESS_GROUP)
    except Exception:
        raise RuntimeError('The local test server failed to launch.') from None
    record = {'pid': process.pid, 'controller_pid': os.getpid(), 'session_id': uuid.uuid4().hex, 'port': PORT, 'address': ADDRESS,
              'listen_ip': '0.0.0.0', 'game': str(GAME), 'server_executable': str(SERVER / 'srcds_win64.exe'),
              'map': chosen_map, 'ready': False, 'kept_running': True, 'status': 'starting', 'started_at': time.time(),
              'main_sha256': digest(GAME / 'addons/sourcemod/plugins/HanZombieShelter.smx'),
              'secret_policy': 'Generated only in controller memory; never printed or serialized'}
    record['window_minimized'] = minimize_owned_server_window(process.pid)
    write(CONTROL / 'session.json', record)
    while process.poll() is None:
        if not record['ready']:
            try:
                output = rcon(secret, 'sm_zsh_status')
                if 'enabled=1 ready=1' in output:
                    if not record.get('window_minimized'):
                        record['window_minimized'] = minimize_owned_server_window(process.pid)
                    record.update(ready=True, status='ready', ready_at=now())
                    write(CONTROL / 'session.json', record)
            except (OSError, ConnectionError, RuntimeError, ValueError):
                pass
        for request in sorted(CONTROL.glob('request_*.json')):
            response = request.with_name(request.name.replace('request_', 'response_'))
            if response.exists():
                continue
            try:
                message = json.loads(request.read_text(encoding='utf-8'))
                text = message['command']
                if message.get('session_id') != record['session_id']:
                    result = {'ok': False, 'error': 'Request belongs to a different local test session.'}
                elif not allowed(text):
                    result = {'ok': False, 'error': 'Outside the retained zero-BOT session scope.'}
                else:
                    try:
                        result = {'ok': True, 'output': rcon(secret, text).replace(secret, '[redacted]')}
                    except Exception as exc:
                        result = {'ok': False, 'error': str(exc).replace(secret, '[redacted]')}
                write(response, result)
            except (ValueError, KeyError, TypeError):
                write(response, {'ok': False, 'error': 'Malformed request'})
        time.sleep(.12)
    record.update(ready=False, kept_running=False, status='exited', exit_code=process.returncode, exited_at=now())
    write(CONTROL / 'session.json', record)


def start(map_name=None):
    chosen_map = chosen_map_name(map_name)
    check(require_main=True)
    current = session()
    if current and pid_running(current.get('pid')) and pid_running(current.get('controller_pid')):
        if map_name is not None and current.get('map') != chosen_map:
            raise RuntimeError('The existing local test server is running map ' + str(current.get('map'))
                               + '; requested map ' + chosen_map + '. It was preserved; no map change or second server was started.')
        print(json.dumps({'already_running': True, **current}, ensure_ascii=False))
        return
    check_map(chosen_map)
    assert_stopped()
    CONTROL.mkdir(parents=True, exist_ok=True)
    lock = CONTROL / 'start.lock'
    try:
        handle = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        raise RuntimeError('Another start request is in progress. Check the server status.') from None
    os.close(handle)
    try:
        with (CONTROL / 'daemon_stdout.log').open('ab') as output, (CONTROL / 'daemon_stderr.log').open('ab') as error:
            controller = subprocess.Popen([sys.executable, '-B', '-X', 'utf8', str(Path(__file__).resolve()), '--daemon', '--map', chosen_map],
                                          cwd=ROOT, stdout=output, stderr=error, creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            current = session()
            if current and current.get('controller_pid') == controller.pid and current.get('ready'):
                print(json.dumps(current, ensure_ascii=False))
                return
            if controller.poll() is not None:
                raise RuntimeError('Controller exited; inspect control/daemon_stderr.log.')
            time.sleep(.2)
        raise RuntimeError('Startup is still pending. Inspect control/session.json; do not start a second server.')
    finally:
        lock.unlink(missing_ok=True)


def command(text):
    if not allowed(text):
        raise RuntimeError('Command is outside the retained zero-BOT session scope.')
    current = session()
    if not current or not pid_running(current.get('controller_pid')) or not pid_running(current.get('pid')):
        raise RuntimeError('The fixed local test server is not running.')
    token = uuid.uuid4().hex
    request = CONTROL / ('request_' + token + '.json')
    response = CONTROL / ('response_' + token + '.json')
    write(request, {'command': text, 'session_id': current['session_id']})
    deadline = time.monotonic() + 35
    while time.monotonic() < deadline:
        if response.exists():
            try:
                result = json.loads(response.read_text(encoding='utf-8'))
                print(json.dumps(result, ensure_ascii=False))
                return
            except PermissionError:
                pass
        time.sleep(.1)
    raise TimeoutError('Own local test controller request timed out.')


def status():
    current = session()
    print(json.dumps({'environment': check(), 'session': current,
                      'server_running': bool(current and pid_running(current.get('pid'))),
                      'controller_running': bool(current and pid_running(current.get('controller_pid')))}, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description='Fixed HZS local test server; no password files are needed.')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--start', action='store_true')
    mode.add_argument('--daemon', action='store_true')
    mode.add_argument('--command')
    mode.add_argument('--console', action='store_true')
    mode.add_argument('--status', action='store_true')
    mode.add_argument('--session', action='store_true')
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--install-main')
    parser.add_argument('--map', choices=ALLOWED_MAPS, help='Map for --start or --daemon; default: ' + MAP)
    args = parser.parse_args()
    if args.map is not None and not (args.start or args.daemon):
        parser.error('--map can be used only with --start or --daemon')
    if args.start:
        start(args.map)
    elif args.daemon:
        daemon(args.map)
    elif args.command is not None:
        command(args.command)
    elif args.console:
        print('Local server commands. Enter a blank line to close this command window.')
        while True:
            text = input('27169> ').strip()
            if not text:
                break
            try:
                command(text)
            except Exception as exc:
                print(str(exc))
    elif args.install_main:
        install_main(args.install_main)
    elif args.session:
        print(json.dumps(session(), ensure_ascii=False))
    elif args.check:
        print(json.dumps(check(), ensure_ascii=False))
    else:
        status()


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
