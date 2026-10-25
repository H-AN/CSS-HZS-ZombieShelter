"""Explicit deadcity 3x5 real-client route batch; never restart or create NPCs.

Root owns diagnostic deployment, native admissions, weapon/visual preparation,
stop and cleanup. This file is inert at import. Run only after reviewing a new
JPEG in this report directory and configuring the three existing native Bosses.
The frozen 779e diagnostic is reused; this runner never invokes r.build().
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import time
import uuid

import test_v082_boss_routes as r

REPORT = r.t.ROOT / "reports/v082_deadcity_routes_20261025"
LOCAL = r.REPO / "tests/reports/v082_deadcity_routes_20261025"
BUILD = LOCAL / "build"
MAP = "zsh_deadcity_han_dn"
BOSSES = ("guard_01", "diag_angela", "diag_yixing")
BASES = ("A", "B", "C", "D", "E", "F")
DIAGNOSTIC_SHA256 = "779e848f9059863c12e5808877041ca6d7433700f1a356ce173ba9d01079dc91"

# All shared actions write only the deadcity report tree after this import.
r.REPORT, r.LOCAL, r.BUILD = REPORT, LOCAL, BUILD
r.t.REPORT, r.t.BUILD = REPORT, BUILD


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def probe_state(label, active=None, identity=None):
    row = r.command("sm_v082_bosspath status", label)
    states = [item for item in row["rows"] if item.get("kind") == "state"]
    require(len(states) == 1, "Expected exactly one diagnostic state row")
    state = states[0]
    require(state["running"] and not state["terminal"] and state["guards_injected"], "Diagnostic/world not ready for route observation")
    require(state["actor_serial"] > 0 and state["boss_count"] == len(BOSSES), "Need one existing living real actor and three configured Bosses")
    if active is not None:
        require(bool(state["active"]) == active, "Unexpected diagnostic fixture active state")
    bases = [item for item in row["rows"] if item.get("kind") == "base_state"]
    require(len(bases) == 6 and {item["core"] for item in bases} == set(range(6)) and all(item["alive"] for item in bases), "All six deadcity bases must be ready and alive")
    bosses = {item["id"]: item for item in row["rows"] if item.get("kind") == "boss_state"}
    require(set(bosses) == set(BOSSES), "Expected exactly guard_01, diag_angela and diag_yixing")
    require(all(item["health"] > 1 and item["bound"] and not item["defeated"] and not item["dead"] and item["npc_slot"] >= 0 and item["body_ref"] not in (-1, 0) for item in bosses.values()), "All three exact native Boss instances must already exist alive; no creation/revival is permitted")
    if identity is not None:
        require((state["epoch"], state["actor_serial"], state["config_revision"]) == identity["session"], "Epoch, actor serial or configuration changed")
        require({name: (item["npc_slot"], item["body_ref"]) for name, item in bosses.items()} == identity["bosses"], "Existing Boss instance identity changed")
    return row, state, bosses


def preflight(prefix, begin):
    engine = r.command("status", prefix + "_engine")
    actual_map = re.search(r"(?m)^map\s*:\s*(\S+)", engine["output"])
    players = re.search(r"players\s*:\s*(\d+) humans?,\s*(\d+) bots?", engine["output"])
    require(actual_map is not None and actual_map[1] == MAP, "Actual engine map must be deadcity; startup session.map is historical")
    require(players is not None and tuple(map(int, players.groups())) == (1, 0), "Require exactly one ordinary real client and zero bots")
    require(r.t.sha(r.t.GAME / "addons/sourcemod/plugins/HanZombieShelter.smx") == DIAGNOSTIC_SHA256, "Deploy the agreed frozen diagnostic first; this runner does not build or deploy it")
    shelter = r.command("sm_zsh_status", prefix + "_shelter")
    require(re.search(r"enabled=1 ready=1 phase=[12]", shelter["output"]) is not None and "bases=6/6" in shelter["output"], "Production mode must be running with six live bases before begin")
    gate = json.loads((REPORT / "client_visual_gate.json").read_text(encoding="utf-8"))
    require(gate["clear_confirmed"] and gate["server_pid"] == r.t.owned_server()["pid"], "Root must review a current unobstructed JPEG and confirm the new-directory visual gate")
    initial, state, _ = probe_state(prefix + "_before_begin")
    points = [item for item in initial["rows"] if item.get("kind") == "configured_point"]
    require(len(points) == 5 and {item["point"] for item in points} == set(range(1, 6)), "Five configured points must already be loaded")
    if not state["active"]:
        require(begin, "Root must explicitly begin the fixture first or invoke this batch with --begin")
        started = r.command("sm_v082_bosspath begin", prefix + "_begin")
        require(any(item.get("kind") == "begin" for item in started["rows"]), "Fixture begin refused; wait for native readiness, do not fake callbacks")
    initial, state, bosses = probe_state(prefix + "_initial", active=True)
    require(state["stage"] in (0, 4), "Finish or explicitly stop the previous scene before starting the batch")
    identity = {"session": (state["epoch"], state["actor_serial"], state["config_revision"]),
                "bosses": {name: (item["npc_slot"], item["body_ref"]) for name, item in bosses.items()}}
    return initial, state, identity


def reload_existing_weapon(tag):
    try:
        r.t.client("+reload", tag + "_reload_down")
        time.sleep(.3)
    finally:
        r.t.client("-reload", tag + "_reload_up")


def run(begin=False):
    prefix = "dc_" + uuid.uuid4().hex[:8]
    initial, start, identity = preflight(prefix, begin)
    completed = {(item["id"], point + 1) for item in initial["rows"] if item.get("kind") == "boss_state"
                 for point, value in enumerate(item["point_results"]) if value == 1}
    report = {"started": r.t.now(), "run_id": prefix, "map": MAP, "epoch": start["epoch"],
              "actor_serial": start["actor_serial"], "config_revision": start["config_revision"],
              "existing_boss_instances": identity["bosses"], "diagnostic_sha256": DIAGNOSTIC_SHA256,
              "base_attempt_order": list(BASES), "timeout_seconds": 180, "preexisting_passed": sorted(completed),
              "attempts": [], "npc_motion_target_or_path_written_during_chase": False,
              "automatic_restart_npc_creation_or_cleanup": False, "cleanup_is_root_managed": True}
    r.record("deadcity_route_batch", report)
    shots = 0
    try:
        for boss in BOSSES:
            for point in range(1, 6):
                if (boss, point) in completed:
                    continue
                for attempt, base in enumerate(BASES, 1):
                    tag = f"{prefix}_{boss}_p{point}_{base.lower()}{attempt}"
                    print(json.dumps({"starting": tag, "base_order": BASES}, ensure_ascii=False), flush=True)
                    if shots % 5 == 0:
                        reload_existing_weapon(tag)
                    prepared = r.command(f"sm_v082_bosspath prepare {boss} {point} 180", tag + "_prepare")
                    failure = next((item for item in prepared["rows"] if item.get("kind") == "scene_result"), None)
                    if failure is not None:
                        report["attempts"].append({"boss": boss, "point": point, "return_base": base, "phase": "prepare", **failure})
                        r.record("deadcity_route_batch", report)
                        break  # Exact-point geometry does not depend on return base.
                    prepared_sample = next((item for item in prepared["rows"] if item.get("kind") == "prepared"), None)
                    require(prepared_sample is not None, "No prepared scene; root must inspect the retained fixture")
                    for shot in range(1, 4):
                        r.t.tap("attack", tag + f"_hit{shot}")
                        shots += 1
                        time.sleep(.35)
                        current, state, bosses = probe_state(tag + f"_after_hit{shot}", active=True, identity=identity)
                        if state["stage"] == 2:
                            require(bosses[boss]["awakened"] and bosses[boss]["health"] < prepared_sample["health"], "Need actual damage and production awakened, never a synthetic wake")
                            break
                        require(state["stage"] == 1, "Real-hit scene failed; root must inspect before continuing")
                    require(state["stage"] == 2, "No observed real hit after three gated shots; no fake wake or target assignment will be used")
                    returned = r.command("sm_v082_bosspath return_to_base " + base, tag + "_return")
                    require(any(item.get("kind") == "chase_started" for item in returned["rows"]), "Production base teleport or chase-start gate rejected")
                    deadline = time.monotonic() + 195.0
                    poll = 0
                    while time.monotonic() < deadline:
                        time.sleep(4.0)
                        poll += 1
                        current, state, bosses = probe_state(tag + f"_poll{poll:02}", active=True, identity=identity)
                        if state["stage"] == 4:
                            break
                    require(state["stage"] == 4, "Bounded fixture failed to finish; root owns stop and cleanup")
                    sample = next((item for item in current["rows"] if item.get("kind") == "status" and item.get("boss_id") == boss), None)
                    require(sample is not None and sample["body_ref"] == identity["bosses"][boss][1], "Missing exact-instance status sample")
                    passed = bosses[boss]["point_results"][point - 1] == 1
                    if passed:
                        require(0 <= sample["safe_core"] < 6, "Movement alone cannot pass; Boss must physically enter a production green zone")
                    result = {"boss": boss, "point": point, "return_base": base, "base_attempt_index": attempt,
                              "passed": passed, "arrived_core": sample["safe_core"], "returned_core": sample["returned_core"],
                              "player_safe_core": sample["player_safe_core"], "travel": sample["travel"],
                              "chase_seconds_at_poll": sample["chase_seconds"], "body_ref": sample["body_ref"],
                              "visual_ref": sample["visual_ref"], "scene": sample["scene"],
                              "status_receipt": tag + f"_poll{poll:02}.json", "base_attempt_order": list(BASES)}
                    report["attempts"].append(result)
                    r.record("deadcity_route_batch", report)
                    print(json.dumps(result, ensure_ascii=False), flush=True)
                    if passed:
                        completed.add((boss, point))
                        break  # Any one base is sufficient; do not test the rest.
        final, _, _ = probe_state(prefix + "_final", active=True, identity=identity)
        report["completed"] = r.t.now()
        report["matrix"] = {item["id"]: item["point_results"] for item in final["rows"] if item.get("kind") == "boss_state"}
        report["combinations_passed"] = sum(value == 1 for values in report["matrix"].values() for value in values)
        report["combinations_not_passed"] = 15 - report["combinations_passed"]
        report["root_cleanup_required"] = True
        r.record("deadcity_route_batch", report)
        return report
    except (Exception, KeyboardInterrupt) as error:
        report.update(interrupted=r.t.now(), error=str(error), root_stop_or_cleanup_required=True,
                      automatic_cleanup_performed=False, fixture_retained_for_inspection=True)
        r.record("deadcity_route_batch", report)
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--begin", action="store_true", help="Explicitly permit fixture begin after readiness checks; no cleanup is automatic")
    args = parser.parse_args()
    result = run(args.begin)
    print(json.dumps({key: result[key] for key in ("run_id", "matrix", "combinations_passed", "combinations_not_passed", "root_cleanup_required")}, ensure_ascii=False, indent=2))
    raise SystemExit(0 if result["combinations_not_passed"] == 0 else 1)
