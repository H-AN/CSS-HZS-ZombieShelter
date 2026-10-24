"""The user's sole authorized local dedicated-server test environment."""
from pathlib import Path

INSTALL_ROOT = Path(r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')
TEST_ROOT = INSTALL_ROOT / '昼夜求生本地测试服务器'
GAME_ROOT = TEST_ROOT / 'cstrike'
SERVER_EXE = INSTALL_ROOT / 'srcds_win64.exe'
CONTROLLER = TEST_ROOT / 'tools' / 'server_control.py'
PORT = 27169
MAP = 'zsh_desert_han_dn'


def require_test_root(value):
    resolved = Path(value).resolve()
    if resolved != TEST_ROOT.resolve():
        raise ValueError(f'本地服务端测试仅允许使用固定目录：{TEST_ROOT}')
    return resolved


def require_test_port(value):
    if int(value) != PORT:
        raise ValueError(f'本地测试仅允许使用唯一端口 {PORT}')
    return PORT
