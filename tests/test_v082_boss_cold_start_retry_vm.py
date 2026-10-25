"""Regression for paused simulation time exhausting Boss startup retries.

Runs the existing exact-production retry suite into a new report directory,
then tests frozen-time callbacks and resumed simulation using the same SMX.
No original report, live file, entity, or game process is changed.
"""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "tests/reports/v082_boss_damage_20261025/cold_start_retry"
spec = importlib.util.spec_from_file_location("boss_cold_start_support", ROOT / "tests/test_v082_boss_position_retry_vm.py")
support = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = support
spec.loader.exec_module(support)
support.REPORT = REPORT
support.BUILD = REPORT / "vm"
support.base.compat.BUILD = support.BUILD


def run():
    prior = support.run()
    assert prior["passed"], prior
    (REPORT / "existing_retry_suite.json").write_text(json.dumps(prior, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    smx = support.BUILD / "boss_position_retry_vm.smx"
    plugin = support.base.SourcePawnPlugin(io.BytesIO(smx.read_bytes()), smsys_options={"natives_cls": support.RetryNatives})
    vm = plugin.runtime
    vm.console_redirect = None
    native = support.RetryNatives
    checks = []

    def call(name, *args):
        return vm.call_function_by_name("VM_" + name, *args)

    def check(name, actual, expected):
        checks.append(dict(name=name, passed=actual == expected, actual=actual, expected=expected))

    def reset():
        native.now = 0.0
        native.packs = {}
        native.timers = []
        native.teleports = []
        native.messages = []
        native.errors = []
        native.health = {}
        call("Reset")
        vm.amx._local_to_string = vm.amx._getheapstring
        call("Queue")
        return native.timers[0]["pack"]

    pack = reset()
    cont, stop = call("Continue"), call("Stop")
    actions = [call("Callback", pack) for _ in range(50)]
    check("50 zero-game-time callbacks all wait", actions, [cont] * 50)
    check("frozen initial clock spends no attempts and samples no incomplete hull", [call("State", i) for i in (0, 1, 2)], [1, 0, 0])
    check("frozen initial clock emits no false bad-point failure", native.errors, [])
    check("frozen initial body remains parked", [call("State", i) for i in (9, 10)], [0, 1])
    check("frozen initial clock does not teleport or write health", [len(native.teleports), len(native.health)], [0, 0])
    native.now = 0.1
    check("first advancing frame samples transient hull once", call("Callback", pack), cont)
    check("first advancing frame spends one attempt", [call("State", i) for i in (1, 2)], [1, 1])
    actions = [call("Callback", pack) for _ in range(50)]
    check("50 repeated callbacks at nonzero identical time all wait", actions, [cont] * 50)
    check("identical time never spends duplicate attempts or traces", [call("State", i) for i in (0, 1, 2)], [1, 1, 1])
    check("waiting does not schedule extra timers", len(native.timers), 1)
    call("FinalBounds")
    call("SetClear", 1)
    call("SetSavedHP", 43123)
    native.now = 0.2
    check("resumed simulation uses final real hull and succeeds", call("Callback", pack), stop)
    check("resumed success is the second real attempt", [call("State", i) for i in (0, 1, 2)], [0, 2, 2])
    check("resumed success preserves saved damage and sleeping state", [native.health[100], call("State", 6), call("State", 7), call("State", 11)], [43123, 43123, 43123, 0])
    check("resumed success teleports exactly once to unchanged configured point", native.teleports,
          [{"entity": 100, "origin": [100.25, 200.5, -1535.96875], "angles": [0.0, 180.0, 0.0], "velocity": [0.0, 0.0, 0.0]}])
    check("success attempt log counts advancing-time tries only", any("attempts=2" in line for line in native.messages), True)
    check("same-time callback after success still stops", call("Callback", pack), stop)
    check("same-time success replay cannot teleport again", len(native.teleports), 1)
    check("no failure after frozen-time recovery", native.errors, [])

    pack = reset()
    for _ in range(50):
        call("Callback", pack)
    for attempt in range(1, 50):
        native.now = attempt / 10.0
        assert call("Callback", pack) == cont
    check("genuinely blocked point retains 49 real attempts before deadline", [call("State", i) for i in (0, 1, 2)], [1, 49, 49])
    for _ in range(50):
        assert call("Callback", pack) == cont
    check("frozen time near deadline cannot spend final attempt", [call("State", i) for i in (0, 1, 2)], [1, 49, 49])
    native.now = 5.0
    check("bad point still stops at five active game seconds", call("Callback", pack), stop)
    check("bad point maximum remains exactly 50 real attempts", [call("State", i) for i in (0, 1, 2)], [0, 50, 50])
    check("terminal bad-point error occurs once", len(native.errors), 1)
    check("bad-point failure reports actual active elapsed time", "50 attempts (5.00s)" in native.errors[0], True)
    check("bad point stays parked without NPC generation/teleport/HP refill", [len(native.teleports), len(native.health), call("State", 9), call("State", 10)], [0, 0, 0, 1])
    check("frozen callback after terminal failure remains stopped", call("Callback", pack), stop)
    check("terminal failure is not repeated", len(native.errors), 1)

    for name, field, value in (("dead", 6, 1), ("old epoch", 1, 8), ("defeated", 9, 1), ("already positioned", 10, 50000)):
        pack = reset()
        assert call("Callback", pack) == cont
        call("Guard", field, value)
        check(name + " guard is checked before frozen-time wait", call("Callback", pack), stop)
        check(name + " frozen-time guard does not spend attempts", call("State", 1), 0)

    failed = sum(not row["passed"] for row in checks)
    result = dict(at=datetime.now(timezone(timedelta(hours=8))).isoformat(),
                  passed=prior["checks_passed"] + len(checks) - failed,
                  failed=prior["checks_failed"] + failed,
                  existing_suite_checks=prior["checks_passed"], cold_start_checks=len(checks),
                  production_source_sha256=prior["production_source_sha256"],
                  fixture_smx_sha256=hashlib.sha256(smx.read_bytes()).hexdigest(),
                  fixture_compile_warning_count=prior["fixture_compile_warning_count"],
                  scope="Exact production Queue/BossPosition/helpers in offline Pawn VM with time/hull/entity adapters; no engine startup or live gameplay claim.",
                  preserved_historical_suite="tests/reports/v082_boss_routes_20261025/startup_retry/boss_position_retry_vm_verification.json remains unchanged.",
                  frozen_time_rule="Context/epoch/ref/death/defeat/success guards are checked first. Nonadvancing game time consumes no retries and performs no hull queries. Valid pending tasks wait for simulation to advance; active game-time 5s / 50 tries caps stay unchanged.",
                  checks=checks)
    (REPORT / "cold_start_retry_vm.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("passed", "failed", "existing_suite_checks", "cold_start_checks", "fixture_compile_warning_count")}, ensure_ascii=False))
    for row in checks:
        if not row["passed"]:
            print(json.dumps(row, ensure_ascii=False))
    return 1 if result["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(run())
