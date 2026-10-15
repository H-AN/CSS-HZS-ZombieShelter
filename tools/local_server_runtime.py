"""Pure launch-path construction shared with offline runner regression tests."""
from pathlib import Path


def runtime_paths(server_root, report_dir):
    server = Path(server_root).resolve()
    return server, server / 'cstrike', Path(report_dir).resolve()


def startup_arguments(server_root, port, server_config, secret, bind_ip='127.0.0.1'):
    server = Path(server_root).resolve()
    return [str(server / 'srcds_win64.exe'), '-game', str(server / 'cstrike'),
            '-console', '-insecure', '-nohltv', '-dev', '-ip', bind_ip,
            '-port', str(port), '+maxplayers', '32', '+sv_lan', '1',
            '+sv_cheats', '1', '+servercfgfile', server_config,
            '+rcon_password', secret, '+con_logfile', 'shelter_runtime.log',
            '+bot_quota', '0', '+map', 'zsh_desertplant_cso_v1_d']
