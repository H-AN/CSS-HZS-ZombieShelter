"""No process launch: validate the single-client driver's script boundary."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('v05_real_driver', ROOT / 'tools/test_v05_real_client.py')
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


class RealClientDriverTests(unittest.TestCase):
    def test_v05_steps_are_accepted_without_launching(self):
        steps = [{'rcon': 'zsh_test v05_ui_setup'}, {'client': 'menuselect 1'},
                 {'wait': 0.7}, {'screenshot': 'variant'}, {'rcon': 'zsh_test v05_real_cleanup'}]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'steps.json'
            path.write_text(json.dumps(steps), encoding='utf-8')
            self.assertEqual(driver.load_steps(path), steps)

    def test_bot_respawn_password_and_multicommands_are_rejected(self):
        for command in ('bot_add_ct', 'bot_quota 1', 'sm_respawn @all',
                        'rcon_password secret', 'zsh_test v05_single; bot_add_ct'):
            with self.subTest(command=command), self.assertRaises(ValueError):
                driver.validate_rcon(command)
        self.assertFalse(driver.valid_client_command('connect 127.0.0.1:27129'))
        self.assertFalse(driver.valid_client_command('jointeam 3;retry'))

    def test_process_matching_reads_only_the_exact_executable(self):
        path = ROOT / 'tests/runtime/v05_server/srcds_win64.exe'
        rows = [{'ExecutablePath': str(path), 'ProcessId': 1},
                {'ExecutablePath': str(ROOT / 'tests/runtime/v04_server/srcds_win64.exe'), 'ProcessId': 2}]
        self.assertEqual([row['ProcessId'] for row in driver.matching_processes(rows, path)], [1])


if __name__ == '__main__':
    unittest.main()
