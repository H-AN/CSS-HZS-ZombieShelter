"""Leave the formal v0.5 isolated server and one normal Steam client running.

Uses the confirmed LAN/hijack/RCON workflow. Never invokes test probe commands,
adds BOTs, respawns clients, stops processes, or changes the recording server.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import socket
import subprocess
import sys
import time

from local_server_runtime import startup_arguments
from migrate_v05_materials import load as load_keyvalues
from test_v05_real_client import (CLIENT_LAN_IP, PORT, REPO, SERVER_BIND_IP,
                                  RCON, RealClientSession, a2s_info,
                                  matching_processes, udp_endpoints,
                                  windows_processes)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ManualReviewSession(RealClientSession):
    def __init__(self, options):
        super().__init__(options)
        self.results.update({'purpose': 'formal v0.5 manual review; leave both processes running',
                             'scope': 'one ordinary Steam client; production plugin only',
                             'ready': False, 'kept_running': False,
                             'recording_server_port': 27129, 'recording_server_touched': False,
                             'formal_smx_sha256': None, 'client_rcon_password_written_to_report': False})

    def expected_main_source(self):
        return REPO / 'addons/sourcemod/plugins/HanZombieShelter.smx'

    def profile_label(self):
        return 'formal'

    def formal_preflight(self):
        if self.options.reuse_client:
            self.reuse_client_preflight()
        else:
            self.preflight()
        plugin_directory = self.server / 'cstrike/addons/sourcemod/plugins'
        plugins = sorted(path.relative_to(plugin_directory).as_posix()
                         for path in plugin_directory.rglob('*.smx'))
        expected = ['HanZombieShelter.smx', 'UnmodifiedHZS.smx']
        if self.options.supporting_fixes:
            expected += ['hzs_botaddfix_guarded.smx', 'hzsh_followrange_fix.smx']
        if plugins != sorted(expected):
            raise RuntimeError('Manual review plugin files do not match the exact selected formal profile')
        formal = self.expected_main_source()
        staged = plugin_directory / 'HanZombieShelter.smx'
        if not formal.is_file() or digest(formal) != digest(staged):
            raise RuntimeError('Isolated main SMX does not match the required ' + self.profile_label() + ' build')
        self.results['main_profile'] = self.profile_label()
        self.results['expected_main_smx_sha256'] = digest(formal)
        self.results['expected_main_smx_path'] = str(formal)
        if self.profile_label() == 'formal':
            self.results['formal_smx_sha256'] = digest(formal)
            self.results['formal_smx_path'] = str(formal)
        self.results['isolated_smx_path'] = str(staged)
        self.results['plugin_files'] = plugins
        self.results['supporting_fixes'] = self.options.supporting_fixes
        self.results['reused_client'] = self.options.reuse_client
        cfg_root = self.server / 'cstrike/addons/sourcemod/configs/HanZombieShelter'
        zombies = load_keyvalues(cfg_root / 'zombies.cfg')['ShelterZombies']
        self.expected_native_pool = int(zombies['NativePool']['initial_ordinary_count'])
        self.results['expected_native_pool'] = self.expected_native_pool
        if self.options.supporting_fixes:
            hashes = {}
            for name in ('hzs_botaddfix_guarded.smx', 'hzsh_followrange_fix.smx'):
                built = REPO / 'tests/compiled/supporting' / name
                staged = plugin_directory / name
                if not built.is_file() or digest(built) != digest(staged):
                    raise RuntimeError('A supporting fix SMX does not match the audited compiled artifact: ' + name)
                hashes[name] = digest(staged)
            self.results['supporting_smx_sha256'] = hashes
        self.results['native_smx_sha256'] = digest(plugin_directory / 'UnmodifiedHZS.smx')
        self.record('formal_preflight_passed')

    def reuse_client_preflight(self):
        if not (self.server / '.shelter-v05-isolated.json').is_file():
            raise RuntimeError('Prepare the isolated v0.5 tree first')
        for path in (self.server / 'srcds_win64.exe', self.client / 'cstrike_win64.exe'):
            if not path.is_file():
                raise RuntimeError('A confirmed executable is missing')
        rows = windows_processes()
        if matching_processes(rows, self.server / 'srcds_win64.exe'):
            raise RuntimeError('The isolated server is still running; this helper never stops it')
        source_clients = [row for row in rows if row.get('Name', '').lower()
                          in ('cstrike_win64.exe', 'cstrike.exe', 'hl2.exe')]
        matches = matching_processes(rows, self.client / 'cstrike_win64.exe')
        if len(source_clients) != 1 or len(matches) != 1:
            raise RuntimeError('--reuse-client requires exactly one existing client at the fixed CSS executable path')
        if not any(row.get('Name', '').lower() == 'steam.exe' for row in rows):
            raise RuntimeError('Steam must remain running')
        for transport in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
            with socket.socket(socket.AF_INET, transport) as port:
                port.bind((SERVER_BIND_IP, PORT))
        allowed = {'', 'bot_quota 0', 'bot_join_after_player 1', 'sv_hibernate_when_empty 0'}
        for name in ('autoexec.cfg', 'server.cfg', 'zsh_codex_test.cfg'):
            text = (self.server / 'cstrike/cfg' / name).read_text(encoding='utf-8-sig')
            if any(line.split('//', 1)[0].strip() not in allowed for line in text.splitlines()):
                raise RuntimeError('Isolated startup cfg has unexpected commands: ' + name)
        self.client_pids = {int(matches[0]['ProcessId'])}
        self.results['clientPIDs'] = sorted(self.client_pids)
        self.record('existing_fixed_path_client_selected', pid=int(matches[0]['ProcessId']))

    def production_rcon(self, command):
        if command not in ('status', 'sm_zsh_status', 'sm plugins list',
                           'sm_hzsh_botadd_patch_status', 'sm_hzsh_follow_patch_status'):
            raise ValueError('Only formal status commands are supported here')
        output = self.rcon_connection.command(command)
        self.record('rcon', command=command, output=output)
        print('[V05 manual review]', command, output[:700], flush=True)
        if 'Unknown command' in output:
            raise RuntimeError('The formal status command was unavailable')
        return output

    def configure_client_rcon(self):
        # The password is sent only through this unique short-lived cfg. It is
        # absent from process command lines, report fields and console messages.
        name = self.cfg('manual_rcon', 'con_enable 1\nrcon_address "127.0.0.1:27139"\n'
                        'rcon_password "' + self.secret + '"\nhideconsole\n')
        path = self.client / 'cstrike/cfg' / name
        try:
            self.client_control('+exec', name)
            self.wait(1)
        finally:
            if path.is_file():
                path.unlink()
        self.results['client_manual_rcon'] = {'address': '127.0.0.1:27139',
                                             'password_location': 'running client memory only',
                                             'temporary_cfg_deleted': not path.exists(),
                                             'example': 'rcon sm_zsh_status'}
        self.record('client_manual_rcon_configured')

    def start_and_leave_running(self):
        self.formal_preflight()
        # Retain earlier sessions separately so a shutdown exception or failed
        # candidate from yesterday is never attributed to this runtime.
        history = self.report / ('previous_logs_' + self.run_id)
        previous = list((self.server / 'cstrike/addons/sourcemod/logs').glob('*.log'))
        console = self.server / 'cstrike/shelter_runtime.log'
        if console.is_file():
            previous.append(console)
        for path in previous:
            history.mkdir(parents=True, exist_ok=True)
            shutil.move(str(path), str(history / path.name))
        self.results['status'] = 'starting_' + self.profile_label() + '_server'
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        self.server_process = subprocess.Popen(
            startup_arguments(self.server, PORT, 'zsh_codex_test.cfg', self.secret, bind_ip=SERVER_BIND_IP),
            cwd=self.server, startupinfo=startup,
            creationflags=subprocess.CREATE_NEW_CONSOLE | subprocess.CREATE_NEW_PROCESS_GROUP)
        self.results['startupPID'] = self.server_process.pid
        self.results['kept_running'] = True
        self.record('formal_server_started', pid=self.server_process.pid)
        deadline = time.monotonic() + 90
        while self.rcon_connection is None:
            if self.server_process.poll() is not None:
                raise RuntimeError('Formal server exited before RCON authentication')
            try:
                self.rcon_connection = RCON(PORT, self.secret)
            except (OSError, ConnectionError):
                if time.monotonic() >= deadline:
                    raise TimeoutError('Formal server RCON startup timed out') from None
                time.sleep(0.5)
        plugins = self.production_rcon('sm plugins list')
        if '<Error>' in plugins or '<Failed>' in plugins:
            raise RuntimeError('A selected runtime plugin reports an error')
        expected_count = 4 if self.options.supporting_fixes else 2
        if not re.search(r'Listing\s+' + str(expected_count) + r'\s+plugins', plugins, re.IGNORECASE):
            raise RuntimeError('The runtime plugin listing does not match the selected formal profile')
        if self.options.supporting_fixes:
            # Follow detour activation waits until OnConfigsExecuted. Poll its
            # real status before connecting a client or allowing natural NPCs.
            patch_deadline = time.monotonic() + 10
            while True:
                bot = self.production_rcon('sm_hzsh_botadd_patch_status')
                follow = self.production_rcon('sm_hzsh_follow_patch_status')
                bot_ready = all(flag in bot for flag in ('ready=1', 'bytes_verified=1'))
                follow_ready = all(flag in follow for flag in ('ready=1', 'idle=1', 'range_nop=1', 'path_nop=1'))
                if bot_ready and follow_ready:
                    self.record('supporting_fixes_verified_before_client_connection')
                    break
                plugins = self.production_rcon('sm plugins list')
                if '<Error>' in plugins or '<Failed>' in plugins:
                    raise RuntimeError('A supporting fix reports a runtime plugin error')
                if time.monotonic() >= patch_deadline:
                    raise TimeoutError('Supporting fixes did not report verified readiness within 10 seconds')
                self.wait(0.25)
        self.record('server_udp_endpoints', endpoints=udp_endpoints(self.server_process.pid))
        self.record('lan_udp_probe', **a2s_info(CLIENT_LAN_IP, PORT))
        name = self.cfg('manual_connect', 'con_enable 1\nconnect ' + CLIENT_LAN_IP + ':27139\n')
        if not self.options.reuse_client:
            self.client_process = subprocess.Popen(
                [str(self.client / 'cstrike_win64.exe'), '-steam', '-game', 'cstrike',
                 '-windowed', '-w', '1280', '-h', '720', '-novid', '+exec', name], cwd=self.client,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            self.client_pids.add(self.client_process.pid)
        self.results['clientPIDs'] = sorted(self.client_pids)
        self.results['status'] = 'connecting_one_normal_steam_client'
        self.record('ordinary_steam_client_reused' if self.options.reuse_client else 'ordinary_steam_client_started',
                    pids=sorted(self.client_pids))
        deadline = time.monotonic() + 40
        while not matching_processes(windows_processes(), self.client / 'cstrike_win64.exe'):
            if time.monotonic() >= deadline:
                raise TimeoutError('The normal Steam CSS process did not appear')
            self.wait(0.5)
        self.wait(2)
        self.client_control('+exec', name)

        joined = False
        ready_at = None
        deadline = time.monotonic() + self.options.connect_timeout
        while True:
            status = self.production_rcon('status')
            population = re.search(r'players\s*:\s*(\d+)\s+humans?,\s*(\d+)\s+bots?', status)
            if not population:
                raise RuntimeError('Could not read the formal server player count')
            humans, bots = map(int, population.groups())
            if bots or humans > 1:
                raise RuntimeError('Manual review found an unexpected extra participant; processes left running')
            if humans == 1 and not joined and re.search(r'^#\s+\d+.*\bactive\b', status, re.MULTILINE):
                join_cfg = self.cfg('manual_join', 'jointeam 3\nwait 30\njoinclass 1\nhideconsole\n')
                self.client_control('+exec', join_cfg)
                joined = True
                self.record('single_normal_team_and_class_selection')
            shelter = self.production_rcon('sm_zsh_status')
            fields = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', shelter)}
            ready = (joined and humans == 1 and bots == 0 and fields.get('enabled') == 1
                     and fields.get('phase') == 1 and fields.get('bosses', 0) > 0
                     and fields.get('tracked') == self.expected_native_pool + fields.get('bosses', 0))
            if ready:
                if ready_at is None:
                    ready_at = time.monotonic()
                if time.monotonic() - ready_at >= self.options.observe_seconds:
                    self.results['ready'] = True
                    self.results['natural_hzs_startup'] = (self.profile_label() + ' plugin: one human, zero bots, phase=1, tracked='
                                                           + str(fields['tracked']))
                    self.results['ready_status'] = {'humans': humans, 'bots': bots, **fields}
                    break
            else:
                ready_at = None
            if time.monotonic() >= deadline:
                raise TimeoutError('Formal natural startup was not observed; owned processes remain running for diagnosis')
            self.wait(2)
        # Wait until the normal game thread is active before executing and
        # deleting the unique credential cfg; map loading can defer +exec.
        self.configure_client_rcon()
        self.client_command('hideconsole')
        self.wait(1)
        self.screenshot(self.profile_label() + '_manual_review_ready')
        self.results['status'] = 'ready_for_user_manual_review; server_and_client_kept_running'
        self.results['exitCode'] = self.server_process.poll()
        self.record('manual_review_ready', kept_running=True)

    def release_python_only(self):
        # Intentionally no cleanup(), quit, disconnect, terminate or taskkill.
        if self.rcon_connection is not None:
            self.rcon_connection.close()
            self.rcon_connection = None
        for path in self.cfg_paths:
            if path.is_file():
                path.unlink()
        self.results['python_rcon_socket_closed'] = True
        self.results['kept_running'] = self.server_process is not None and self.server_process.poll() is None
        if self.server_process is not None:
            self.results['exitCode'] = self.server_process.poll()
        self.record('python_finished_without_stopping_processes', kept_running=self.results['kept_running'])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-dir', default=str(REPO / 'tests/reports/v05/manual_review'))
    parser.add_argument('--connect-timeout', type=float, default=180)
    parser.add_argument('--observe-seconds', type=float, default=5)
    parser.add_argument('--reuse-client', action='store_true', help='Reconnect the one existing fixed-path CSS client; never create or stop it')
    parser.add_argument('--supporting-fixes', action='store_true', help='Require exactly the formal pair plus the two audited focused fixes')
    options = parser.parse_args(argv)
    if not 20 <= options.connect_timeout <= 600 or not 0 <= options.observe_seconds <= 120:
        parser.error('Connect timeout 20..600 seconds; ready observation 0..120 seconds')
    session = ManualReviewSession(options)
    code = 0
    try:
        session.start_and_leave_running()
    except Exception as error:
        code = 1
        session.results['status'] = 'not_ready; any_started_processes_kept_for_diagnosis'
        session.record('error', error_type=type(error).__name__, message=str(error).replace(session.secret, '[redacted]'))
        print('[V05 manual review]', type(error).__name__, str(error).replace(session.secret, '[redacted]'), flush=True)
    finally:
        session.release_python_only()
    print('[V05 manual review] Report:', session.report / 'session.json', flush=True)
    print('[V05 manual review] Kept running:', session.results['kept_running'],
          'server PID:', session.results['startupPID'], 'client PIDs:', session.results['clientPIDs'], flush=True)
    return code


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    raise SystemExit(main())
