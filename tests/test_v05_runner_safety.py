"""Offline launch regression checks: this module never starts a server."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from local_server_runtime import runtime_paths, startup_arguments


class RunnerPathTests(unittest.TestCase):
    def test_relative_server_and_report_are_absolute_before_changing_cwd(self):
        server, game, report = runtime_paths('tests/runtime/v05_server', 'tests/reports/v05/offline')
        self.assertTrue(server.is_absolute())
        self.assertTrue(game.is_absolute())
        self.assertTrue(report.is_absolute())
        self.assertEqual(game, server / 'cstrike')
        arguments = startup_arguments(server, 27139, 'zsh_codex_test.cfg', 'offline-test-secret')
        game_argument = Path(arguments[arguments.index('-game') + 1])
        # Re-resolving as a subprocess inside cwd=server must not duplicate
        # tests/runtime/v05_server in its -game path.
        self.assertEqual((server / game_argument).resolve(), game)
        self.assertEqual(Path(arguments[0]), server / 'srcds_win64.exe')

    def test_absolute_and_relative_inputs_produce_the_same_launch_paths(self):
        server, _, _ = runtime_paths('tests/runtime/v05_server', 'tests/reports/v05/offline')
        self.assertEqual(startup_arguments('tests/runtime/v05_server', 27139, 'test.cfg', 'offline'),
                         startup_arguments(server, 27139, 'test.cfg', 'offline'))

    def test_startup_retains_zero_bot_policy(self):
        arguments = startup_arguments('tests/runtime/v05_server', 27139, 'test.cfg', 'offline')
        self.assertEqual(arguments[arguments.index('+bot_quota') + 1], '0')
        self.assertFalse(any(item.lower() in ('+bot_add', '+bot_add_ct', '+bot_add_t') for item in arguments))
        self.assertEqual(arguments[arguments.index('-port') + 1], '27139')

    def test_real_client_mode_can_bind_all_local_interfaces(self):
        arguments = startup_arguments('tests/runtime/v05_server', 27139, 'test.cfg', 'offline', bind_ip='0.0.0.0')
        self.assertEqual(arguments[arguments.index('-ip') + 1], '0.0.0.0')


if __name__ == '__main__':
    unittest.main()
