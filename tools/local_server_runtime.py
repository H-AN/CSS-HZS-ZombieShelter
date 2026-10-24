"""Pure path checks for the sole local server; historical runners are retired."""
from pathlib import Path

from local_test_environment import (
    CONTROLLER, GAME_ROOT, MAP, PORT, SERVER_EXE, TEST_ROOT,
    require_test_port, require_test_root,
)


def retired_local_runner():
    """Block legacy runners before they copy configs, start, or stop any process."""
    raise SystemExit(
        'This historical local-server runner is retired. '
        f'Use tools/Start-LocalShelter.ps1 or "{CONTROLLER}" --start; '
        f'the only local test root is {TEST_ROOT} (port {PORT}).'
    )


def runtime_paths(server_root, report_dir):
    server = require_test_root(server_root)
    return server, GAME_ROOT.resolve(), Path(report_dir).resolve()


def startup_arguments(server_root, port, server_config, secret, bind_ip='0.0.0.0'):
    require_test_root(server_root)
    require_test_port(port)
    if bind_ip != '0.0.0.0':
        raise ValueError('The confirmed local test listener must be 0.0.0.0')
    return [str(SERVER_EXE), '-game', str(GAME_ROOT.resolve()),
            '-console', '-insecure', '-nohltv', '-dev', '-ip', bind_ip,
            '-port', str(port), '+maxplayers', '32', '+sv_lan', '1',
            '+servercfgfile', server_config,
            '+rcon_password', secret, '+con_logfile', 'shelter_runtime.log',
            '+bot_quota', '0', '+map', MAP]
