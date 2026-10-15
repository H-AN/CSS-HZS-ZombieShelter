"""One ordinary Steam CSS client on the isolated v0.5 server, never a fake client.

Default: start/connect one client, observe native startup, capture JPEGs and close
only the owned processes. Use --rcon-script for explicit v0.5 fixture steps.
No existing RCON password file is read and no password/launch command is logged.
"""
from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path
import re
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import time
import uuid

from local_server_runtime import startup_arguments

REPO = Path(__file__).resolve().parents[1]
ISOLATED = (REPO / 'tests/runtime/v05_server').resolve()
CLIENT = Path(r'D:\SteamLibrary\steamapps\common\Counter-Strike Source')
PORT = 27139
CLIENT_LAN_IP = '192.168.0.102'  # Confirmed preferred Ethernet IPv4 at this site.
SERVER_BIND_IP = '0.0.0.0'       # Matches the established Start-LocalShelter.ps1.


def udp_endpoints(pid):
    script = ("[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new(); @(Get-NetUDPEndpoint -OwningProcess " + str(int(pid)) +
              " -ErrorAction SilentlyContinue | Select-Object LocalAddress,LocalPort,OwningProcess) | ConvertTo-Json -Compress")
    encoded = base64.b64encode(script.encode('utf-16le')).decode('ascii')
    result = subprocess.run(['powershell', '-NoProfile', '-EncodedCommand', encoded],
                            capture_output=True, text=True, encoding='utf-8-sig', errors='replace', check=True, timeout=20)
    rows = json.loads(result.stdout.strip() or '[]')
    return rows if isinstance(rows, list) else [rows]


def a2s_info(address, port):
    query = b'\xff\xff\xff\xffTSource Engine Query\x00'
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.settimeout(2)
        connection.sendto(query, (address, port))
        response, remote = connection.recvfrom(4096)
        if response[:5] == b'\xff\xff\xff\xffA' and len(response) >= 9:
            connection.sendto(query + response[5:9], (address, port))
            response, remote = connection.recvfrom(4096)
        if response[:5] not in (b'\xff\xff\xff\xffI', b'\xff\xff\xff\xffm'):
            raise ConnectionError('The LAN UDP endpoint did not return a valid A2S_INFO response')
        return {'address': remote[0], 'port': remote[1], 'response_type': chr(response[4]),
                'response_bytes': len(response), 'query': 'A2S_INFO', 'passed': True}


def windows_processes():
    # Deliberately select no CommandLine property: it may contain private secrets.
    script = ("[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new(); @(Get-CimInstance Win32_Process | Where-Object { $_.Name -in "
              "@('srcds_win64.exe','cstrike_win64.exe','cstrike.exe','hl2.exe','steam.exe') } | "
              "Select-Object ProcessId,Name,ExecutablePath) | ConvertTo-Json -Compress")
    encoded = base64.b64encode(script.encode('utf-16le')).decode('ascii')
    result = subprocess.run(['powershell', '-NoProfile', '-EncodedCommand', encoded],
                            capture_output=True, text=True, encoding='utf-8-sig', errors='replace', check=True, timeout=25)
    rows = json.loads(result.stdout.strip() or '[]')
    return rows if isinstance(rows, list) else [rows]


def matching_processes(rows, executable):
    target = str(Path(executable).resolve()).casefold()
    return [row for row in rows if (row.get('ExecutablePath') or '').casefold() == target]


def validate_rcon(text):
    if not isinstance(text, str) or any(ch in text for ch in (';', '\n', '\r', '\x00')):
        raise ValueError('One RCON command is required per step')
    text = text.strip()
    allowed = (text in ('status', 'sm_zsh_status', 'zsh_test summary', 'zsh_test logic')
               or re.fullmatch(r'zsh_v05_[a-z0-9_]+(?: [a-zA-Z0-9_. -]+)?', text)
               or re.fullmatch(r'zsh_test v05[a-z0-9_]*(?: [a-zA-Z0-9_. -]+)?', text)
               or re.fullmatch(r'hzsh_api_test [a-z0-9_]+', text)
               or re.fullmatch(r'sm_zsh_defense_trace [01]', text))
    if not allowed:
        raise ValueError('Only v0.5 probe/status/API commands are allowed in an RCON script')
    return text


def load_steps(path):
    if path is None:
        return []
    raw = Path(path).read_text(encoding='utf-8-sig')
    if raw.lstrip().startswith(('[', '{')):
        payload = json.loads(raw)
        steps = payload['steps'] if isinstance(payload, dict) else payload
    else:
        steps = [{'rcon': line.strip()} for line in raw.splitlines()
                 if line.strip() and not line.lstrip().startswith('#')]
    if not isinstance(steps, list):
        raise ValueError('The script must contain a JSON array or newline RCON commands')
    for step in steps:
        if not isinstance(step, dict):
            raise ValueError('Each script step must be an object')
        actions = set(step) & {'rcon', 'client', 'key', 'wait', 'screenshot'}
        if len(actions) != 1 or set(step) - actions - {'contains', 'not_contains'}:
            raise ValueError('Each step needs exactly one supported action')
        if 'rcon' in step:
            validate_rcon(step['rcon'])
        if 'client' in step and not valid_client_command(step['client']):
            raise ValueError('Unsupported client command')
        if 'key' in step and str(step['key']).upper() not in tuple('0123456789E') + ('ENTER',):
            raise ValueError('Unsupported physical key')
        if 'wait' in step and not 0 <= float(step['wait']) <= 60:
            raise ValueError('Wait steps must be between 0 and 60 seconds')
        if 'screenshot' in step and not re.fullmatch(r'[a-z0-9_]+', str(step['screenshot'])):
            raise ValueError('Invalid screenshot label')
    return steps


def valid_client_command(text):
    return isinstance(text, str) and bool(re.fullmatch(
        r'(?:sm_build|menuselect [0-9]|hideconsole|slot[1-5]|use weapon_[a-z0-9_]+|[+-](?:attack2?|use))', text))


class RCON:
    def __init__(self, port, secret):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=1)
        self.socket.settimeout(0.6)
        self.sequence = 10
        self.send(1, 3, secret)
        while True:
            identifier, kind, _ = self.receive()
            if kind == 2:
                if identifier == -1:
                    raise ConnectionError('Ephemeral RCON authentication rejected')
                return

    def send(self, identifier, kind, text):
        payload = struct.pack('<ii', identifier, kind) + text.encode('utf-8') + b'\0\0'
        self.socket.sendall(struct.pack('<i', len(payload)) + payload)

    def receive(self):
        def exact(count):
            result = b''
            while len(result) < count:
                chunk = self.socket.recv(count - len(result))
                if not chunk:
                    raise ConnectionError('RCON closed')
                result += chunk
            return result
        count = struct.unpack('<i', exact(4))[0]
        if not 10 <= count <= 1048576:
            raise ConnectionError('Invalid RCON packet size')
        payload = exact(count)
        identifier, kind = struct.unpack_from('<ii', payload)
        return identifier, kind, payload[8:-2].decode('utf-8', errors='replace')

    def command(self, text):
        self.sequence += 1
        self.send(self.sequence, 2, text)
        output = []
        try:
            while True:
                identifier, _, message = self.receive()
                if identifier == self.sequence:
                    output.append(message)
        except socket.timeout:
            pass
        except ConnectionError:
            if text != 'quit':
                raise
        return ''.join(output)

    def close(self):
        self.socket.close()


class RealClientSession:
    def __init__(self, options):
        self.options = options
        self.server = ISOLATED
        self.client = CLIENT.resolve()
        self.report = Path(options.report_dir).resolve()
        self.run_id = uuid.uuid4().hex[:12]
        if (self.report / 'session.json').exists():
            self.report = self.report / ('run_' + self.run_id)
        self.secret = secrets.token_hex(24)
        self.server_process = None
        self.client_process = None
        self.client_pids = set()
        self.rcon_connection = None
        self.cfg_paths = []
        self.started = time.monotonic()
        self.results = {'run_id': self.run_id, 'scope': 'one real Steam client; no multiplayer acceptance',
                        'server_root': str(self.server), 'port': PORT, 'client_root': str(self.client),
                        'server_bind_ip': SERVER_BIND_IP, 'client_connect_ip': CLIENT_LAN_IP,
                        'startupPID': None, 'exitCode': None, 'clientPIDs': [], 'entries': [],
                        'screenshots': [], 'visual_review': 'pending independent screenshot inspection',
                        'natural_hzs_startup': 'not_observed', 'external_day_fixture': False,
                        'status': 'not_started', 'cleanup_errors': []}

    def record(self, kind, **details):
        self.results['entries'].append({'elapsed': round(time.monotonic() - self.started, 3),
                                        'kind': kind, **details})
        self.report.mkdir(parents=True, exist_ok=True)
        (self.report / 'session.json').write_text(json.dumps(self.results, ensure_ascii=False, indent=2), encoding='utf-8')

    def preflight(self):
        if self.server != ISOLATED or self.server == self.client:
            raise ValueError('This driver can launch only tests/runtime/v05_server on port 27139')
        if not (self.server / '.shelter-v05-isolated.json').is_file():
            raise ValueError('Prepare the isolated v0.5 server first')
        for path in (self.server / 'srcds_win64.exe', self.client / 'cstrike_win64.exe'):
            if not path.is_file():
                raise FileNotFoundError('Required executable missing: ' + str(path))
        rows = windows_processes()
        if matching_processes(rows, self.server / 'srcds_win64.exe'):
            raise RuntimeError('The isolated test server is already running')
        if any(row.get('Name', '').lower() in ('cstrike_win64.exe', 'cstrike.exe', 'hl2.exe') for row in rows):
            raise RuntimeError('An existing Source client is running; this driver will not redirect its session')
        if not any(row.get('Name', '').lower() == 'steam.exe' for row in rows):
            raise RuntimeError('Start Steam normally before this single-client test')
        for transport in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
            with socket.socket(socket.AF_INET, transport) as port:
                port.bind((SERVER_BIND_IP, PORT))
        for name in ('autoexec.cfg', 'server.cfg', 'zsh_codex_test.cfg'):
            text = (self.server / 'cstrike/cfg' / name).read_text(encoding='utf-8-sig')
            commands = [line.split('//', 1)[0].strip() for line in text.splitlines()]
            allowed = {'', 'bot_quota 0', 'bot_join_after_player 1', 'sv_hibernate_when_empty 0'}
            if any(command not in allowed for command in commands):
                raise RuntimeError('Isolated startup configuration has unexpected commands: ' + name)

    def rcon(self, command, *, cleanup=False):
        if not cleanup:
            validate_rcon(command)
        output = self.rcon_connection.command(command)
        self.record('rcon', command=command, output=output)
        print('[V05 real client]', command, output[:600], flush=True)
        if not cleanup and (re.search(r'\bFAIL\b', output) or 'Unknown command' in output):
            raise AssertionError('A v0.5 fixture failed; see session.json')
        return output

    def cfg(self, label, text):
        path = self.client / 'cstrike/cfg' / ('zsh05_' + self.run_id + '_' + label + '.cfg')
        if path.exists():
            raise FileExistsError('Refusing to replace a client configuration')
        path.write_text(text, encoding='ascii')
        self.cfg_paths.append(path)
        return path.name

    def client_control(self, *arguments):
        rows = matching_processes(windows_processes(), self.client / 'cstrike_win64.exe')
        if not rows:
            raise RuntimeError('The owned CSS client is absent; refusing to launch another through -hijack')
        for row in rows:
            self.client_pids.add(int(row['ProcessId']))
        self.results['clientPIDs'] = sorted(self.client_pids)
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        result = subprocess.run([str(self.client / 'cstrike_win64.exe'), '-hijack', '-game', 'cstrike', *arguments],
                                cwd=self.client, startupinfo=startup, timeout=20, capture_output=True)
        self.record('client_control', arguments=list(arguments), exit_code=result.returncode)
        if result.returncode:
            raise RuntimeError('The owned client did not accept its control command')

    def client_command(self, command):
        if not valid_client_command(command):
            raise ValueError('Unsupported client command')
        name = self.cfg('cmd_' + str(len(self.cfg_paths)), command + '\nhideconsole\n')
        self.client_control('+exec', name)

    def screenshot(self, label):
        if not re.fullmatch(r'[a-z0-9_]+', label):
            raise ValueError('Invalid screenshot label')
        name = 'zsh05_' + self.run_id + '_' + label
        source = self.client / 'cstrike/screenshots' / (name + '.jpg')
        target = self.report / source.name
        if source.exists() or target.exists():
            raise FileExistsError('Refusing to overwrite screenshot evidence')
        self.client_control('+jpeg', name, '100')
        deadline = time.monotonic() + 8
        while not source.is_file() or source.stat().st_size == 0:
            if time.monotonic() >= deadline:
                raise TimeoutError('The client did not write its JPEG screenshot')
            time.sleep(0.1)
        shutil.copy2(source, target)
        self.results['screenshots'].append(str(target))
        self.record('screenshot', file=str(target), bytes=target.stat().st_size)

    def wait(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.server_process.poll() is not None:
                raise RuntimeError('The owned isolated server exited')
            time.sleep(min(1, max(0.01, deadline - time.monotonic())))

    def start(self):
        self.preflight()
        self.results['status'] = 'starting_isolated_server'
        history = self.report / ('previous_logs_' + self.run_id)
        old_logs = list((self.server / 'cstrike/addons/sourcemod/logs').glob('*.log'))
        old_console = self.server / 'cstrike/shelter_runtime.log'
        if old_console.is_file():
            old_logs.append(old_console)
        for path in old_logs:
            history.mkdir(parents=True, exist_ok=True)
            shutil.move(str(path), str(history / path.name))
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        self.server_process = subprocess.Popen(startup_arguments(self.server, PORT, 'zsh_codex_test.cfg', self.secret, bind_ip=SERVER_BIND_IP),
                                               cwd=self.server, startupinfo=startup,
                                               creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.results['startupPID'] = self.server_process.pid
        self.record('server_started', pid=self.server_process.pid)
        deadline = time.monotonic() + 90
        while self.rcon_connection is None:
            if self.server_process.poll() is not None:
                raise RuntimeError('Isolated server exited before RCON authentication')
            try:
                self.rcon_connection = RCON(PORT, self.secret)
            except (OSError, ConnectionError):
                if time.monotonic() >= deadline:
                    raise TimeoutError('Isolated server RCON startup timed out') from None
                time.sleep(0.5)
        self.rcon('zsh_v05_client status')
        endpoints = udp_endpoints(self.server_process.pid)
        self.record('server_udp_endpoints', endpoints=endpoints)
        if not any(int(row['LocalPort']) == PORT for row in endpoints):
            raise RuntimeError('The owned server did not bind its UDP gameplay port')
        try:
            self.record('lan_udp_probe', **a2s_info(CLIENT_LAN_IP, PORT))
        except (OSError, ConnectionError) as probe_error:
            self.record('lan_udp_probe', passed=False, error_type=type(probe_error).__name__,
                        address=CLIENT_LAN_IP, port=PORT)
            raise RuntimeError('The confirmed LAN gameplay endpoint did not answer A2S_INFO') from None
        connect_cfg = self.cfg('connect', 'con_enable 1\nconnect ' + CLIENT_LAN_IP + ':27139\n')
        # A normal Steam client is intentionally visible. Existing clients were
        # refused in preflight and this process is the only participant created.
        self.client_process = subprocess.Popen([str(self.client / 'cstrike_win64.exe'), '-steam', '-game', 'cstrike',
                                               '-windowed', '-w', '1280', '-h', '720', '-novid', '+exec', connect_cfg],
                                              cwd=self.client)
        self.client_pids.add(self.client_process.pid)
        self.results['status'] = 'connecting_one_ordinary_steam_client'
        self.record('ordinary_steam_client_started', pid=self.client_process.pid)
        # Reissue through the established -hijack path for reliability. The first
        # failed run showed connection retries, so lost +exec was not established.
        process_deadline = time.monotonic() + 40
        while not matching_processes(windows_processes(), self.client / 'cstrike_win64.exe'):
            if time.monotonic() >= process_deadline:
                raise TimeoutError('The ordinary Steam CSS process did not appear')
            self.wait(0.5)
        self.wait(2)
        self.client_control('+exec', connect_cfg)
        self.record('connection_cfg_reissued_to_existing_client')
        deadline = time.monotonic() + self.options.connect_timeout
        joined = False
        while True:
            status = self.rcon('zsh_v05_client status')
            fields = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', status)}
            if fields.get('bots', 0) or fields.get('real_clients', 0) > 1:
                raise RuntimeError('The isolated session has an unexpected extra participant')
            if fields.get('real_clients') == 1 and not joined:
                name = self.cfg('join', 'jointeam 3\nwait 30\njoinclass 1\nhideconsole\n')
                self.client_control('+exec', name)
                joined = True
            if fields.get('alive_real') == 1:
                break
            if time.monotonic() >= deadline:
                raise TimeoutError('The ordinary client did not naturally join alive; no respawn was attempted')
            self.wait(1)
        rows = windows_processes()
        for row in matching_processes(rows, self.client / 'cstrike_win64.exe'):
            self.client_pids.add(int(row['ProcessId']))
        self.results['clientPIDs'] = sorted(self.client_pids)
        self.results['status'] = 'one_real_client_connected_alive'
        self.rcon('zsh_v05_client closepanels')
        self.client_command('hideconsole')
        self.wait(1)
        self.screenshot('connected')

    def execute(self, steps):
        for step in steps:
            output = ''
            if 'rcon' in step:
                output = self.rcon(step['rcon'])
            elif 'client' in step:
                self.client_command(step['client'])
            elif 'key' in step:
                from client_keyboard import press
                press(str(step['key']))
                self.record('physical_key', key=str(step['key']))
            elif 'wait' in step:
                self.wait(float(step['wait']))
            elif 'screenshot' in step:
                self.screenshot(str(step['screenshot']))
            if 'contains' in step and step['contains'] not in output:
                raise AssertionError('Expected fixture output was absent; see session.json')
            if 'not_contains' in step and step['not_contains'] in output:
                raise AssertionError('Forbidden fixture output was present; see session.json')

    def observe(self):
        deadline = time.monotonic() + self.options.observe_seconds
        while True:
            status = self.rcon('zsh_v05_client status')
            fields = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', status)}
            if fields.get('bots', 0) or fields.get('real_clients', 0) > 1:
                raise RuntimeError('An unexpected extra participant joined the isolated session')
            if fields.get('running') == 1 and fields.get('hzs_npcs', 0) > 0 and not fields.get('external_day_fixture'):
                self.results['natural_hzs_startup'] = 'observed with one ordinary Steam client and native NPC registry'
            if time.monotonic() >= deadline:
                break
            self.wait(min(5, deadline - time.monotonic()))
        if self.options.external_day:
            status = self.rcon('zsh_v05_client external_day')
            self.results['external_day_fixture'] = 'external StartDay fixture only' in status
        self.rcon('sm_zsh_status')
        self.screenshot('observed')

    def cleanup(self):
        # The native plugin unloads while GameRules still exists. Only this
        # session's server/client handles or freshly verified owned PIDs close.
        if self.rcon_connection is not None and self.server_process is not None and self.server_process.poll() is None:
            for command in ('sm_zsh_fail', 'sm plugins unload UnmodifiedHZS', 'quit'):
                try:
                    self.rcon(command, cleanup=True)
                    if command == 'sm_zsh_fail':
                        time.sleep(4.5)
                except Exception as error:
                    self.results['cleanup_errors'].append(type(error).__name__ + ': ' + command)
            try:
                self.server_process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                pass
        if self.rcon_connection is not None:
            self.rcon_connection.close()
        if self.server_process is not None and self.server_process.poll() is None:
            self.results['cleanup_errors'].append('owned server required forced termination')
            self.server_process.terminate()
            try:
                self.server_process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                # Never stringify a server TimeoutExpired: its command contains
                # the ephemeral RCON secret. The owned handle remains explicit.
                self.results['cleanup_errors'].append('owned server termination timed out')
        if self.server_process is not None:
            self.results['exitCode'] = self.server_process.poll()
            if self.results['exitCode'] != 0:
                self.results['cleanup_errors'].append('server exit code ' + str(self.results['exitCode']))
        if self.client_process is not None:
            try:
                rows = matching_processes(windows_processes(), self.client / 'cstrike_win64.exe')
                if rows:
                    name = self.cfg('close', '-attack\n-attack2\n-use\ndisconnect\nquit\n')
                    self.client_control('+exec', name)
                    time.sleep(2)
            except Exception as error:
                self.results['cleanup_errors'].append(type(error).__name__ + ': client close')
            rows = windows_processes()
            for row in matching_processes(rows, self.client / 'cstrike_win64.exe'):
                if int(row['ProcessId']) in self.client_pids:
                    subprocess.run(['taskkill', '/PID', str(row['ProcessId']), '/F'], capture_output=True, timeout=15)
        for path in self.cfg_paths:
            if path.is_file():
                path.unlink()
        for name in ('shelter_runtime.log',):
            path = self.server / 'cstrike' / name
            if path.is_file():
                shutil.copy2(path, self.report / name)
        for path in (self.server / 'cstrike/addons/sourcemod/logs').glob('*.log'):
            if path.name.startswith(('errors_', 'L', 'shelter_')):
                shutil.copy2(path, self.report / path.name)
        self.record('cleanup_complete', server_exit_code=self.results['exitCode'])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-dir', default=str(REPO / 'tests/reports/v05/real_client'))
    parser.add_argument('--observe-seconds', type=float, default=30)
    parser.add_argument('--connect-timeout', type=float, default=150)
    parser.add_argument('--rcon-script', type=Path, help='JSON steps or one v0.5 RCON command per line')
    parser.add_argument('--external-day', action='store_true', help='After observation, explicitly start only the external day fixture if native startup did not occur')
    options = parser.parse_args(argv)
    if not 0 <= options.observe_seconds <= 1800 or not 5 <= options.connect_timeout <= 600:
        parser.error('Observation 0..1800 seconds; connect timeout 5..600 seconds')
    steps = load_steps(options.rcon_script)  # Validate before launching anything.
    session = RealClientSession(options)
    code = 0
    try:
        session.start()
        session.observe()
        session.execute(steps)
        session.results['status'] = 'setup_and_requested_steps_completed; visual review remains required'
    except Exception as error:
        code = 1
        session.results['status'] = 'failed'
        session.record('error', error_type=type(error).__name__, message=str(error))
        print('[V05 real client]', type(error).__name__, str(error), flush=True)
        if session.client_process is not None:
            try:
                session.screenshot('failure_diagnostic')
            except Exception as screenshot_error:
                session.record('failure_screenshot_unavailable', error_type=type(screenshot_error).__name__)
    finally:
        session.cleanup()
    if session.results['cleanup_errors']:
        code = 1
    return code


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    raise SystemExit(main())
