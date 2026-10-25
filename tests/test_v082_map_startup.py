"""Offline map-selection checks; every process/network/start action is mocked."""
from __future__ import annotations

import ast
from contextlib import redirect_stdout, redirect_stderr
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "tests/reports/v082_deadcity_setup_20261025"
FIXED_ROOT = Path(r"D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器")
MAP_REPORT = FIXED_ROOT / "reports/v082_deadcity_setup_20261025"
SOURCE = ROOT / "tools/canonical_server_control.py"
spec = importlib.util.spec_from_file_location("v082_map_control_under_test", SOURCE)
control = importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)
DESERT = "zsh_desert_han_dn"
DEADCITY = "zsh_deadcity_han_dn"


class MapSelectionTests(unittest.TestCase):
    def test_default_and_two_map_whitelist(self):
        self.assertEqual(control.chosen_map_name(), DESERT)
        self.assertEqual(control.chosen_map_name(DESERT), DESERT)
        self.assertEqual(control.chosen_map_name(DEADCITY), DEADCITY)
        for value in ["", "de_dust2", "../zsh_deadcity_han_dn", DEADCITY + ";quit"]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                control.chosen_map_name(value)

    def test_both_real_maps_have_bsp_shelter_and_exact_stage_path_read_only(self):
        for name in [DESERT, DEADCITY]:
            with self.subTest(map=name):
                row = control.check_map(name)
                self.assertEqual(row["map"], name)
                self.assertEqual(set(row["map_files"]), {"bsp", "shelter_map", "hzs_stage"})
                self.assertEqual(Path(row["map_files"]["hzs_stage"]), control.GAME / "addons/sourcemod/configs/HanZombieScenario" / name / "HanZombieScenarioStage.cfg")

    def test_missing_each_map_prerequisite_refuses_before_any_launch(self):
        for kind, missing in control.check_map(DEADCITY)["map_files"].items():
            with self.subTest(kind=kind), mock.patch.object(Path, "is_file", lambda path: str(path) != missing), self.assertRaisesRegex(RuntimeError, kind):
                control.check_map(DEADCITY)

    def test_cli_routes_explicit_map_to_start_and_daemon(self):
        for mode in ["start", "daemon"]:
            with self.subTest(mode=mode), mock.patch.object(sys, "argv", ["server_control.py", "--" + mode, "--map", DEADCITY]), mock.patch.object(control, mode) as operation:
                control.main()
                operation.assert_called_once_with(DEADCITY)
        with mock.patch.object(sys, "argv", ["server_control.py", "--start"]), mock.patch.object(control, "start") as operation:
            control.main()
            operation.assert_called_once_with(None)

    def test_cli_rejects_unknown_map_and_map_outside_start_modes(self):
        for arguments in [["--start", "--map", "de_dust2"], ["--status", "--map", DEADCITY], ["--map", DEADCITY]]:
            with self.subTest(args=arguments), mock.patch.object(sys, "argv", ["server_control.py", *arguments]), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                control.main()
            self.assertEqual(raised.exception.code, 2)

    def test_explicit_different_running_map_refused_without_spawn_or_stop(self):
        running = {"pid": 101, "controller_pid": 102, "map": DESERT}
        with mock.patch.object(control, "check"), mock.patch.object(control, "session", return_value=running), mock.patch.object(control, "pid_running", return_value=True), mock.patch.object(control, "assert_stopped") as stopped, mock.patch.object(control, "check_map") as assets, mock.patch.object(control.subprocess, "Popen") as launch:
            with self.assertRaisesRegex(RuntimeError, "running map " + DESERT + "; requested map " + DEADCITY):
                control.start(DEADCITY)
            stopped.assert_not_called()
            assets.assert_not_called()
            launch.assert_not_called()

    def test_default_request_retains_running_other_map_and_same_explicit_map_returns_it(self):
        running = {"pid": 101, "controller_pid": 102, "map": DEADCITY}
        for requested in [None, DEADCITY]:
            output = io.StringIO()
            with self.subTest(requested=requested), mock.patch.object(control, "check"), mock.patch.object(control, "session", return_value=running), mock.patch.object(control, "pid_running", return_value=True), mock.patch.object(control.subprocess, "Popen") as launch, redirect_stdout(output):
                control.start(requested)
                result = json.loads(output.getvalue())
                self.assertTrue(result["already_running"])
                self.assertEqual(result["map"], DEADCITY)
                launch.assert_not_called()

    def test_start_passes_map_to_hidden_daemon_argv(self):
        process = mock.Mock(pid=456)
        ready = {"controller_pid": 456, "ready": True, "map": DEADCITY}
        with mock.patch.object(control, "check"), mock.patch.object(control, "check_map") as assets, mock.patch.object(control, "session", side_effect=[None, ready]), mock.patch.object(control, "assert_stopped"), mock.patch.object(Path, "mkdir"), mock.patch.object(Path, "open", mock.mock_open()), mock.patch.object(Path, "unlink"), mock.patch.object(control.os, "open", return_value=123), mock.patch.object(control.os, "close"), mock.patch.object(control.subprocess, "Popen", return_value=process) as launch, redirect_stdout(io.StringIO()):
            control.start(DEADCITY)
            assets.assert_called_once_with(DEADCITY)
            args, kwargs = launch.call_args
            self.assertEqual(args[0][-3:], ["--daemon", "--map", DEADCITY])
            self.assertEqual(kwargs["creationflags"], control.subprocess.CREATE_NO_WINDOW)

    def test_daemon_uses_selected_map_in_server_argv_and_session_without_network(self):
        process = mock.Mock(pid=654, returncode=0)
        process.poll.return_value = 0  # Never enter the network/control request loop.
        with mock.patch.object(control, "check"), mock.patch.object(control, "check_map") as assets, mock.patch.object(control, "assert_stopped"), mock.patch.object(control.socket, "socket"), mock.patch.object(control.secrets, "token_hex", return_value="fixture-only-placeholder"), mock.patch.object(control, "minimize_owned_server_window", return_value=True), mock.patch.object(control, "digest", return_value="fixture-main-hash"), mock.patch.object(control, "rcon") as network, mock.patch.object(control, "write") as records, mock.patch.object(control.subprocess, "Popen", return_value=process) as launch:
            control.daemon(DEADCITY)
            assets.assert_called_once_with(DEADCITY)
            argv = launch.call_args.args[0]
            self.assertEqual(argv[-2:], ["+map", DEADCITY])
            self.assertEqual(argv[argv.index("-game") + 1], str(control.GAME))
            self.assertEqual(argv[argv.index("+bot_quota") + 1], "0")
            self.assertEqual(argv[argv.index("+sv_pure") + 1], "0")
            self.assertTrue(records.call_args_list)
            for args, _ in records.call_args_list:
                self.assertEqual(args[1]["map"], DEADCITY)
                self.assertNotIn("fixture-only-placeholder", json.dumps(args[1]))
            network.assert_not_called()

    def test_existing_command_guard_is_identical_to_backed_up_controller(self):
        manifest = json.loads((REPORT / "startup_tools_baseline.json").read_text(encoding="utf-8"))
        original = next(row for row in manifest["files"] if row["source"].endswith("tools\\server_control.py"))
        previous = ast.parse((REPORT / original["backup"]).read_text(encoding="utf-8-sig"))
        current = ast.parse(SOURCE.read_text(encoding="utf-8-sig"))
        previous_guard = next(node for node in previous.body if isinstance(node, ast.FunctionDef) and node.name == "allowed")
        current_guard = next(node for node in current.body if isinstance(node, ast.FunctionDef) and node.name == "allowed")
        self.assertEqual(ast.dump(previous_guard, include_attributes=False), ast.dump(current_guard, include_attributes=False))
        for command in ["bot_add", "bot_quota 1", "fakeclient", "rcon_password x", "password x", "changelevel " + DEADCITY, "map " + DEADCITY, "quit", "sm_zsh_fail", "status;exec x.cfg"]:
            self.assertFalse(control.allowed(command), command)
        self.assertTrue(control.allowed("sm_zsh_status"))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(MapSelectionTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    receipt = {"at": datetime.now(timezone.utc).isoformat(), "passed": result.wasSuccessful(),
               "tests_run": result.testsRun, "failures": len(result.failures), "errors": len(result.errors),
               "controller_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
               "real_game_actions": False, "real_network_connections": False, "rcon_credentials_read": False,
               "process_creation_and_control_functions_mocked": True,
               "live_map_asset_existence_only_read": True,
               "limitations": ["No server/client start, map loading or real readiness/lifecycle validation"]}
    for directory in [REPORT, MAP_REPORT]:
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "startup_map_parameter_tests.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    raise SystemExit(0 if result.wasSuccessful() else 1)
