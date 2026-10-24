"""Offline launch regression checks: this module never starts a server."""
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from local_server_runtime import runtime_paths, startup_arguments
from local_test_environment import GAME_ROOT, MAP, PORT, SERVER_EXE, TEST_ROOT
import local_rcon


class RunnerPathTests(unittest.TestCase):
    def test_fixed_game_and_parent_executable_are_absolute(self):
        server, game, report = runtime_paths(TEST_ROOT, 'tests/reports/v05/offline')
        self.assertTrue(server.is_absolute())
        self.assertTrue(game.is_absolute())
        self.assertTrue(report.is_absolute())
        self.assertEqual(game, server / 'cstrike')
        arguments = startup_arguments(server, PORT, 'zsh_codex_test.cfg', 'offline-test-secret')
        game_argument = Path(arguments[arguments.index('-game') + 1])
        self.assertEqual((server / game_argument).resolve(), game)
        self.assertEqual(Path(arguments[0]), SERVER_EXE)
        self.assertEqual(game, GAME_ROOT.resolve())
        self.assertEqual(arguments[arguments.index('+map') + 1], MAP)

    def test_historical_server_roots_are_rejected_before_launch(self):
        for old_root in ('tests/runtime/v05_server', TEST_ROOT.parent, TEST_ROOT.parent / 'cstrike'):
            with self.subTest(root=old_root), self.assertRaises(ValueError):
                runtime_paths(old_root, 'tests/reports/v05/offline')
            with self.subTest(root=old_root), self.assertRaises(ValueError):
                startup_arguments(old_root, PORT, 'test.cfg', 'offline')

    def test_historical_and_arbitrary_ports_are_rejected(self):
        for port in (27129, 27139, 27015):
            with self.subTest(port=port), self.assertRaises(ValueError):
                startup_arguments(TEST_ROOT, port, 'test.cfg', 'offline')

    def test_startup_retains_zero_bot_policy(self):
        arguments = startup_arguments(TEST_ROOT, PORT, 'test.cfg', 'offline')
        self.assertEqual(arguments[arguments.index('+bot_quota') + 1], '0')
        self.assertFalse(any(item.lower() in ('+bot_add', '+bot_add_ct', '+bot_add_t') for item in arguments))
        self.assertEqual(arguments[arguments.index('-port') + 1], str(PORT))

    def test_real_client_mode_can_bind_all_local_interfaces(self):
        arguments = startup_arguments(TEST_ROOT, PORT, 'test.cfg', 'offline', bind_ip='0.0.0.0')
        self.assertEqual(arguments[arguments.index('-ip') + 1], '0.0.0.0')

    def test_loopback_listener_is_rejected_for_real_client_tests(self):
        with self.assertRaises(ValueError):
            startup_arguments(TEST_ROOT, PORT, 'test.cfg', 'offline', bind_ip='127.0.0.1')


class FixedControllerAdapterTests(unittest.TestCase):
    def test_rcon_uses_private_controller_and_returns_only_command_output(self):
        completed = SimpleNamespace(returncode=0, stdout='{"ok":true,"output":"running"}', stderr='')
        with patch.object(Path, 'is_file', return_value=True), patch.object(local_rcon.subprocess, 'run', return_value=completed) as run:
            self.assertEqual(local_rcon.command('status'), 'running')
        arguments = run.call_args.args[0]
        self.assertEqual(arguments[-3:], [str(local_rcon.CONTROLLER), '--command', 'status'])

    def test_rcon_rejects_command_chaining_without_contacting_controller(self):
        for text in ('', 'status; quit', 'status\nquit'):
            with self.subTest(text=text), patch.object(local_rcon.subprocess, 'run') as run:
                with self.assertRaises(ValueError):
                    local_rcon.command(text)
                run.assert_not_called()

    def test_rcon_reports_controller_rejection(self):
        completed = SimpleNamespace(returncode=0, stdout='{"ok":false,"error":"rejected"}', stderr='')
        with patch.object(Path, 'is_file', return_value=True), patch.object(local_rcon.subprocess, 'run', return_value=completed):
            with self.assertRaisesRegex(RuntimeError, 'rejected'):
                local_rcon.command('status')


if __name__ == '__main__':
    unittest.main()
