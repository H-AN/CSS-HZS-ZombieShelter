"""Read-only audit of real Boss-route JSONL and the matching report receipts.

No server/client module is imported and no game commands are sent. The default
time boundary is the recorded server restart, not a historical PID or epoch.
Each fixture `begin` is a separate run even if scene numbers/epochs repeat.
Outputs go to a separate repository report directory; fixed ROOT is input only.
Run with --require-complete after the batch finishes. Chase durations come from
scene_result, never from a later status poll. SVG paths use recorded samples.
For another map, supply --reports, --map-config and --batch-receipt. The default
output follows --reports/route_audit, so a new map cannot replace the old index.
Both flat controller receipts and {"session": {...}} restart envelopes work.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import html
import json
import math
from pathlib import Path
import re

from migrate_v05_materials import parse as parse_kv

REPO = Path(__file__).resolve().parents[1]
FIXED_ROOT = Path(r"D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器")
REPORTS = REPO / "tests/reports/v082_boss_routes_20261025"
DEFAULT_LOG = FIXED_ROOT / "cstrike/addons/sourcemod/logs/v082_boss_path_probe.jsonl"
TRACE_KINDS = {"chase_started", "sample", "status", "arrived_at_any_base", "timed_out", "stuck"}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def session_from_receipt(receipt: dict) -> dict:
    session = receipt.get("session", receipt)
    if not isinstance(session, dict) or not isinstance(session.get("started_at"), (int, float)):
        raise ValueError("Server receipt requires a flat or nested session with started_at")
    if not math.isfinite(session["started_at"]) or not session.get("session_id"):
        raise ValueError("Server receipt requires a finite startup time and explicit session_id")
    return session


def report_paths(reports: Path, output: Path | None = None, map_config: Path | None = None,
                 batch_receipt: Path | None = None) -> tuple[Path, Path, Path]:
    return ((output or reports / "route_audit").resolve(),
            (map_config or reports / "baseline_live_map.cfg").resolve(),
            (batch_receipt or reports / "real_route_batch.json").resolve())


def timestamp(value: str | float | None) -> float | None:
    if value is None:
        return None
    try:
        return float(value)
    except ValueError:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def read_log(data: bytes) -> tuple[list[dict], list[dict]]:
    rows, errors = [], []
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        if exc.end != len(data):
            raise ValueError(f"Non-UTF8 log bytes at {exc.start}") from exc
        text = data[:exc.start].decode("utf-8-sig")
        errors.append({"byte": exc.start, "error": "incomplete UTF8 tail", "incomplete_tail": True})
    lines = text.splitlines(keepends=True)
    context_time, context_epoch = None, None
    for line_number, line in enumerate(lines, 1):
        try:
            row = json.loads(line, parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
            if not isinstance(row, dict):
                raise ValueError("record is not an object")
        except (ValueError, json.JSONDecodeError) as exc:
            errors.append({"line": line_number, "error": str(exc),
                           "incomplete_tail": line_number == len(lines) and not line.endswith("\n")})
            continue
        if "time" in row:
            context_time = row["time"]
        if "epoch" in row:
            context_epoch = row["epoch"]
        row = {**row, "_line": line_number, "_wall_time": context_time, "_epoch": context_epoch}
        rows.append(row)
    return rows, errors


def split_runs(rows: list[dict], session: dict, since: float, until: float | None) -> list[dict]:
    runs, current, state = [], None, {}
    server_key = str(session.get("session_id") or int(session["started_at"]))[:32]
    points, bosses = {}, {}
    for row in rows:
        wall = row.get("_wall_time")
        if wall is None or wall < since or (until is not None and wall > until):
            continue
        if row.get("kind") == "state":
            state = row
        elif row.get("kind") == "configured_point":
            points[row["point"]] = row
        elif row.get("kind") == "boss_state":
            bosses[row["id"]] = row
        if row.get("kind") == "begin":
            run_id = f"{server_key}_e{row.get('_epoch')}_t{int(wall)}_l{row['_line']}"
            current = {"run_id": run_id, "begin": row, "initial_state": state.copy(),
                       "configured_points": points.copy(), "bosses": bosses.copy(), "rows": []}
            runs.append(current)
        if current is not None:
            current["rows"].append(row)
            if row.get("kind") == "configured_point":
                current["configured_points"][row["point"]] = row
            if row.get("kind") == "boss_state":
                current["bosses"][row["id"]] = row
    return runs


def vector(value) -> bool:
    return isinstance(value, list) and len(value) == 3 and all(
        isinstance(x, (int, float)) and math.isfinite(x) for x in value)


def audit_scene(rows: list[dict], run: dict, max_gap: float = 2.5,
                max_speed: float = 1200.0) -> dict:
    result = next((r for r in reversed(rows) if r.get("kind") == "scene_result"), None)
    prepared = next((r for r in rows if r.get("kind") == "prepared"), None)
    start = next((r for r in rows if r.get("kind") == "chase_started"), None)
    result_line = result["_line"] if result else math.inf
    measured = [r for r in rows if r.get("kind") in TRACE_KINDS and r.get("stage") == 3
                and r["_line"] <= result_line and (not start or r["_line"] >= start["_line"])]
    reference = result or start or prepared or next((r for r in rows if "boss_id" in r), {})
    geometry = next((r for r in rows if r.get("kind") == "point_geometry"), None)
    arrival = next((r for r in measured if r.get("kind") == "arrived_at_any_base"), None)
    end = arrival or next((r for r in reversed(measured) if r.get("kind") in {"timed_out", "stuck"}), None)
    if end is None and measured:
        end = measured[-1]
    hits = [r for r in rows if r.get("kind") == "actor_damage_observed"]
    wake_events = [r for r in rows if r.get("kind") == "awakened" and "reason" in r]
    return_events = [r for r in rows if r.get("kind") == "return_to_base"]
    expected_serial = run["initial_state"].get("actor_serial")
    expected_userid = run["initial_state"].get("actor_userid")
    boss_ref = reference.get("boss_ref", reference.get("body_ref"))
    point = reference.get("point")
    configured = run["configured_points"].get(point, {})
    checks, warnings = {}, []
    checks["finished_scene_result"] = result is not None
    checks["real_actor_hit"] = bool(hits) and all(
        h.get("actor_serial") == expected_serial and h.get("actor_userid") == expected_userid
        and h.get("boss_ref") == boss_ref and h.get("nonlethal_damage", 0) > 0
        and h.get("requested_damage", 0) > 0 and h.get("buttons", 0) & 1 for h in hits)
    checks["production_awaken_event"] = bool(wake_events) and any(
        r.get("kind") == "awakened" and r.get("awakened") == 1 for r in rows)
    checks["return_and_chase_events"] = bool(return_events) and start is not None
    checks["ordered_hit_wake_return_chase"] = bool(hits and wake_events and return_events and start) and (
        hits[0]["_line"] < wake_events[0]["_line"] < return_events[0]["_line"] < start["_line"])
    checks["actual_health_drop"] = bool(prepared and start) and start.get("health", math.inf) < prepared.get("health", -math.inf)
    checks["exact_unadjusted_supported_point"] = bool(geometry and prepared and vector(configured.get("origin"))) and (
        geometry.get("point_was_adjusted") is False and geometry.get("real_hull_clear") == 1
        and geometry.get("real_supported_floor") == 1
        and vector(prepared.get("origin"))
        and math.dist(prepared["origin"], configured["origin"]) <= .1)
    valid_vectors = bool(measured) and all(vector(r.get("origin"))
        and isinstance(r.get("game_time"), (int, float)) and math.isfinite(r["game_time"]) for r in measured)
    checks["finite_trajectory"] = valid_vectors
    trace, duplicate_count, discontinuities = [], 0, []
    if valid_vectors:
        for r in measured:
            if trace and r["game_time"] == trace[-1]["game_time"]:
                if math.dist(r["origin"], trace[-1]["origin"]) > .25:
                    discontinuities.append({"line": r["_line"], "reason": "different_positions_at_same_time"})
                duplicate_count += 1
                continue
            trace.append(r)
    distance, peak_speed, largest_gap, largest_step = 0.0, 0.0, 0.0, 0.0
    for a, b in zip(trace, trace[1:]):
        delta = b["game_time"] - a["game_time"]
        step = math.dist(a["origin"], b["origin"])
        speed = step / delta if delta > 0 else 0.0
        distance += step
        largest_step, largest_gap, peak_speed = max(largest_step, step), max(largest_gap, delta), max(peak_speed, speed)
        if delta <= 0 or delta > max_gap or speed > max_speed:
            discontinuities.append({"from_line": a["_line"], "to_line": b["_line"],
                                    "dt": delta, "step": step, "speed": speed})
    checks["continuous_bounded_samples"] = len(trace) >= 2 and not discontinuities
    checks["one_body_reference"] = bool(measured) and all(r.get("body_ref") == boss_ref for r in measured)
    checks["one_epoch"] = all(r.get("_epoch") == run["begin"].get("_epoch") for r in rows)
    checks["positive_chase_travel"] = bool(result and start and end) and result.get("travel", 0) > 0 and distance > 0
    displacement = math.dist(start["origin"], end["origin"]) if start and end and valid_vectors else 0.0
    checks["reported_travel_consistent"] = bool(result and valid_vectors) and (
        result.get("travel", -1) + max(5.0, result.get("travel", 0) * .01) >= max(distance, displacement))
    checks["result_time_matches_endpoint"] = bool(result and end) and abs(result["game_time"] - end["game_time"]) <= .05
    checks["result_hit_count"] = bool(result) and result.get("hit_count", 0) >= 1
    checks["arrived_in_returned_player_base"] = bool(arrival and start) and (
        arrival.get("safe_core", -1) >= 0
        and arrival.get("safe_core") == arrival.get("returned_core") == arrival.get("player_safe_core")
        and start.get("returned_core") == start.get("player_safe_core") == arrival.get("safe_core"))
    checks["arrival_before_result"] = bool(arrival and result) and arrival["_line"] < result["_line"]
    raw_pass = result is not None and result.get("passed") is True
    reason = result.get("reason", "scene_in_progress") if result else "scene_in_progress"
    if result and reason == "no_real_hit_awaken_within_60_seconds":
        category = "operator_no_hit_wait_failure_not_a_walking_attempt"
    elif result and not raw_pass and start:
        category = "walking_attempt_failed"
    elif result and not raw_pass:
        category = "setup_or_sequence_failure_not_a_walking_attempt"
    elif raw_pass and all(checks.values()):
        category = "audited_walking_pass"
    elif raw_pass:
        category = "raw_pass_with_missing_or_invalid_evidence"
    else:
        category = "in_progress"
    if any(r.get("move_type") == 0 for r in measured):
        warnings.append("MoveType0 occurs in actual samples; movement is assessed by recorded positions, not MoveType alone")
    return {"scene": reference.get("scene"), "boss": reference.get("boss_id"), "point": point,
            "body_ref": boss_ref, "classification": category, "raw_pass": raw_pass,
            "audited_pass": category == "audited_walking_pass", "reason": reason,
            "return_core": start.get("returned_core") if start else None,
            "best_base": chr(65 + start["returned_core"]) if start and start.get("returned_core", -1) >= 0 else None,
            "arrived_core": arrival.get("safe_core") if arrival else None,
            "chase_seconds": result.get("chase_seconds") if result else None,
            "travel": result.get("travel") if result else None,
            "hit_count": result.get("hit_count") if result else len(hits),
            "start_origin": start.get("origin") if start else None,
            "end_origin": end.get("origin") if end else None,
            "prepared_origin": prepared.get("origin") if prepared else None,
            "player_return_origin": start.get("player_origin") if start else None,
            "player_arrival_origin": arrival.get("player_origin") if arrival else None,
            "checks": checks, "failed_checks": [k for k, v in checks.items() if not v],
            "warnings": warnings, "first_line": rows[0]["_line"], "last_line": rows[-1]["_line"],
            "result_line": result.get("_line") if result else None,
            "trajectory_audit": {"samples": len(trace), "same_time_duplicates": duplicate_count,
                "sampled_polyline_length": distance, "endpoint_displacement": displacement,
                "largest_gap_seconds": largest_gap, "largest_step": largest_step,
                "peak_sampled_speed": peak_speed, "gap_limit_seconds": max_gap,
                "sampled_speed_limit": max_speed, "discontinuities": discontinuities,
                "scope": "bounded sampled continuity is not frame-by-frame proof; source audit is separate"},
            "trajectory": [{"game_time": r["game_time"], "origin": r["origin"], "line": r["_line"]} for r in trace]}


def function_body(text: str, name: str) -> str:
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text)
    if not match:
        raise ValueError(f"Function missing: {name}")
    begin, depth = match.end(), 1
    for i in range(begin, len(text)):
        depth += (text[i] == "{") - (text[i] == "}")
        if depth == 0:
            return text[begin:i]
    raise ValueError(f"Unclosed function: {name}")


def fixture_source_audit(path: Path) -> dict:
    raw = path.read_bytes()
    text = raw.decode("utf-8-sig")
    forbidden = r"\b(?:TeleportEntity|SetEntPropVector|SetEntDataEnt2|SetEntityMoveType|Han_SetZombieTarget|Han_SetCustomAnimation(?:Ex)?)\s*\("
    names = ("V082BossPathTimer", "V082BossPathBeforeNPCTick", "V082BPFinish", "V082BPSample")
    functions = {name: not re.search(forbidden, function_body(text, name)) for name in names}
    return {"path": str(path), "sha256": sha(raw), "functions_without_direct_motion_target_animation_setters": functions,
            "passed": all(functions.values()), "scope": "direct-call source inspection of measured timer/sample hooks; not a complete engine execution trace",
            "measurement_motion_owner": "existing HZS/native game AI; fixture prepares and later cleans up NPC positions",
            "diagnostic_limits": "other NPCs parked, campaign clock/admissions paused, exposure disabled, building damage blocked, actor hits made nonlethal"}


def base_boxes(path: Path) -> list[dict]:
    config = parse_kv(path.read_text(encoding="utf-8-sig"))["ShelterMap"]
    result = []
    core = -1
    for core_id, row in config["Cores"].items():
        if str(row.get("enabled", "1")) == "0":
            continue
        core += 1
        origin = [float(v) for v in row["origin"].split()]
        angles = [float(v) for v in row["angles"].split()]
        if len(origin) != 3 or len(angles) != 3 or abs(angles[0]) > .01 or abs(angles[2]) > .01:
            raise ValueError("Safe-box audit supports explicit horizontal map-base yaw only")
        angle = math.radians(angles[1])
        for box_id, box in config["SafeZones"][row["safe_zone"]]["Boxes"].items():
            low, high = ([float(v) for v in box[k].split()] for k in ("mins", "maxs"))
            corners = []
            for x, y in ((low[0], low[1]), (high[0], low[1]), (high[0], high[1]), (low[0], high[1])):
                corners.append([origin[0] + x * math.cos(angle) - y * math.sin(angle),
                                origin[1] + x * math.sin(angle) + y * math.cos(angle)])
            result.append({"core": core, "id": core_id, "box": box_id, "corners_xy": corners,
                           "origin": origin, "yaw": angles[1], "local_mins": low, "local_maxs": high})
    return result


def inside_safe_box(point, core: int | None, boxes: list[dict], tolerance=.01) -> bool:
    if not vector(point) or core is None:
        return False
    for box in boxes:
        if box["core"] != core:
            continue
        delta = [point[a] - box["origin"][a] for a in range(3)]
        yaw = math.radians(box["yaw"])
        local = [delta[0] * math.cos(yaw) + delta[1] * math.sin(yaw),
                 -delta[0] * math.sin(yaw) + delta[1] * math.cos(yaw), delta[2]]
        if all(box["local_mins"][a] - tolerance <= local[a] <= box["local_maxs"][a] + tolerance for a in range(3)):
            return True
    return False


def audit_safe_box_positions(scene: dict, boxes: list[dict]) -> None:
    """Cross-check the logged core IDs against actual configured 3D safe boxes."""
    if not scene["raw_pass"]:
        return
    core = scene.get("return_core")
    for label, key in (("boss_endpoint_in_actual_base_box", "end_origin"),
                       ("player_return_in_actual_base_box", "player_return_origin"),
                       ("player_arrival_in_actual_base_box", "player_arrival_origin")):
        scene["checks"][label] = inside_safe_box(scene.get(key), core, boxes)
    scene["failed_checks"] = [key for key, passed in scene["checks"].items() if not passed]
    scene["audited_pass"] = not scene["failed_checks"]
    if not scene["audited_pass"]:
        scene["classification"] = "raw_pass_with_missing_or_invalid_evidence"


def batch_belongs_to_run(batch: dict, run: dict, next_begin: float = math.inf) -> bool:
    started = timestamp(batch.get("started")) or 0
    return batch.get("epoch") == run["begin"].get("_epoch") and (
        batch.get("actor_serial") == run["initial_state"].get("actor_serial")) and (
        run["begin"].get("_wall_time", 0) <= started < next_begin)


def plot_svg(path: Path, scenes: list[dict], best: list[dict], points: dict, boxes: list[dict]) -> None:
    # Independent panels avoid conflating three distinct Boss trajectories.
    bosses = sorted({s["boss"] for s in scenes if s.get("boss")})
    coordinates = [v["origin"][:2] for v in points.values()]
    coordinates += [p for box in boxes for p in box["corners_xy"]]
    coordinates += [r["origin"][:2] for s in scenes for r in s["trajectory"]]
    if not coordinates:
        return
    low = [min(p[a] for p in coordinates) - 150 for a in (0, 1)]
    high = [max(p[a] for p in coordinates) + 150 for a in (0, 1)]
    panel, height = 420, 650
    width = panel * max(1, len(bosses))
    scale = min(370 / max(1, high[0] - low[0]), 510 / max(1, high[1] - low[1]))
    palette = ["#d73027", "#4575b4", "#e69f00", "#7b3294", "#008837"]
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
           '<rect width="100%" height="100%" fill="white"/>',
           '<style>text{font-family:Arial,sans-serif;font-size:13px}</style>',
           '<text x="20" y="24" font-size="17">Recorded Boss routes: solid = audited best pass; dashed = failed walking attempt</text>',
           '<text x="20" y="45">XY engine units; base safe boxes from map config; plotted lines connect actual recorded samples.</text>']
    best_scenes = {s["scene"] for s in best}
    for i, boss in enumerate(bosses):
        def xy(p):
            return (i * panel + 25 + (p[0] - low[0]) * scale, 590 - (p[1] - low[1]) * scale)
        svg.append(f'<text x="{i * panel + 25}" y="72">{html.escape(boss)}</text>')
        for box in boxes:
            polygon = " ".join(f"{xy(p)[0]:.2f},{xy(p)[1]:.2f}" for p in box["corners_xy"])
            center = [sum(p[a] for p in box["corners_xy"]) / 4 for a in (0, 1)]
            x, y = xy(center)
            svg.extend([f'<polygon points="{polygon}" fill="#d9f0d3" stroke="#2f7d32" stroke-width="1.5"/>',
                        f'<text x="{x:.2f}" y="{y:.2f}" text-anchor="middle">{chr(65 + box["core"])}</text>'])
        for scene in scenes:
            if scene["boss"] != boss or not scene["trajectory"]:
                continue
            passed = scene["scene"] in best_scenes
            if not passed and scene["classification"] != "walking_attempt_failed":
                continue
            polyline = " ".join(f"{xy(r['origin'])[0]:.2f},{xy(r['origin'])[1]:.2f}" for r in scene["trajectory"])
            color = palette[scene["point"] - 1] if passed else "#888888"
            dash = "" if passed else ' stroke-dasharray="4 4" opacity=".4"'
            svg.append(f'<polyline points="{polyline}" fill="none" stroke="{color}" stroke-width="1.6"{dash}><title>{html.escape(boss)} point {scene["point"]}, scene {scene["scene"]}, {scene["chase_seconds"]}s</title></polyline>')
        for number, point in sorted(points.items()):
            x, y = xy(point["origin"])
            svg.extend([f'<circle cx="{x:.2f}" cy="{y:.2f}" r="4" fill="{palette[number - 1]}"/>',
                        f'<text x="{x + 6:.2f}" y="{y - 5:.2f}">P{number}</text>'])
    svg.append('<text x="20" y="630">A timeout remains a failed attempt even when a later attempt reaches the other base. This is controlled route evidence, not a full campaign test.</text>')
    svg.append('</svg>')
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--reports", type=Path, default=REPORTS)
    parser.add_argument("--server-receipt", type=Path)
    parser.add_argument("--output-dir", type=Path, help="Default: the selected --reports/route_audit")
    parser.add_argument("--map-config", type=Path, help="Actual map safe-zone config; default reports/baseline_live_map.cfg")
    parser.add_argument("--batch-receipt", type=Path, help="Batch JSON; default reports/real_route_batch.json")
    parser.add_argument("--boss-id", action="append", help="Expected exact Boss ID; repeat once per expected Boss")
    parser.add_argument("--expected-boss-count", type=int, default=3)
    parser.add_argument("--since", help="Unix timestamp or ISO time; default recorded restart time")
    parser.add_argument("--until", help="Optional inclusive Unix timestamp or ISO time")
    parser.add_argument("--run-id", help="Choose a fixture begin; default latest run after restart")
    parser.add_argument("--max-log-bytes", type=int, help="Read an exact historical byte prefix for reproducibility")
    parser.add_argument("--max-sample-gap", type=float, default=2.5)
    parser.add_argument("--max-sampled-speed", type=float, default=1200.0)
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args(argv)
    output, map_path, batch_path = report_paths(args.reports, args.output_dir, args.map_config, args.batch_receipt)
    if output == FIXED_ROOT.resolve() or FIXED_ROOT.resolve() in output.parents:
        parser.error("Output must be separate from fixed ROOT (input only)")
    if args.max_sample_gap <= 0 or args.max_sampled_speed <= 0 or not math.isfinite(args.max_sample_gap) or not math.isfinite(args.max_sampled_speed):
        parser.error("Sampling limits must be positive")
    if args.expected_boss_count < 1 or (args.max_log_bytes is not None and args.max_log_bytes < 1):
        parser.error("Expected Boss count and any log byte limit must be positive")
    if args.boss_id and (len(set(args.boss_id)) != len(args.boss_id) or len(args.boss_id) != args.expected_boss_count):
        parser.error("--boss-id values must be unique and match --expected-boss-count")
    session_path = args.server_receipt or args.reports / "restart_server_session.json"
    session = session_from_receipt(read_json(session_path))
    since = timestamp(args.since) if args.since else float(session["started_at"])
    until = timestamp(args.until)
    raw = args.log.read_bytes()
    if args.max_log_bytes is not None:
        raw = raw[:args.max_log_bytes]
    rows, parse_errors = read_log(raw)
    runs = split_runs(rows, session, since, until)
    if not runs:
        parser.error("No fixture begin after the selected server/time boundary")
    selected = next((r for r in runs if r["run_id"] == args.run_id), None) if args.run_id else runs[-1]
    if selected is None:
        parser.error("Unknown --run-id")
    scene_rows = defaultdict(list)
    relevant = {"point_geometry", "prepare", "prepared", "actor_damage_observed", "awakened",
                "return_to_base", "scene_result", "dormant_drift"} | TRACE_KINDS
    for row in selected["rows"]:
        if row.get("scene", 0) > 0 and row.get("kind") in relevant:
            scene_rows[row["scene"]].append(row)
    scenes = [audit_scene(value, selected, args.max_sample_gap, args.max_sampled_speed)
              for _, value in sorted(scene_rows.items())]
    boxes = base_boxes(map_path)
    map_raw = map_path.read_bytes()
    map_data = parse_kv(map_raw.decode("utf-8-sig"))["ShelterMap"]
    if session.get("map") != map_data.get("map"):
        parser.error("Map safe-zone config does not match the selected server session map")
    for scene in scenes:
        audit_safe_box_positions(scene, boxes)
    best, missing = [], []
    actual_bosses = sorted(selected["bosses"])
    bosses = sorted(args.boss_id) if args.boss_id else actual_bosses
    identities_match = bosses == actual_bosses and len(actual_bosses) == args.expected_boss_count
    for boss in bosses:
        for point in range(1, 6):
            passed = [s for s in scenes if s["boss"] == boss and s["point"] == point and s["audited_pass"]]
            if passed:
                best.append(min(passed, key=lambda s: s["chase_seconds"]))
            else:
                missing.append({"boss": boss, "point": point})
    batch = read_json(batch_path) if batch_path.exists() else {}
    source_audit = fixture_source_audit(REPO / "tests/V082BossPathProbe.inc")
    run_dir = output / selected["run_id"]
    run_dir.mkdir(parents=True, exist_ok=True)
    def compact(scene):
        return {k: v for k, v in scene.items() if k != "trajectory"}
    failures = [compact(s) for s in scenes if s["classification"] == "walking_attempt_failed"]
    non_walking = [compact(s) for s in scenes if "not_a_walking_attempt" in s["classification"]]
    evidence_failures = [compact(s) for s in scenes if s["classification"] == "raw_pass_with_missing_or_invalid_evidence"]
    fatal_parse_errors = [e for e in parse_errors if not e["incomplete_tail"]]
    expected_combinations = args.expected_boss_count * 5
    goal = len(best) == expected_combinations and identities_match and not evidence_failures and not fatal_parse_errors and source_audit["passed"]
    selected_index = runs.index(selected)
    next_begin = runs[selected_index + 1]["begin"]["_wall_time"] if selected_index + 1 < len(runs) else math.inf
    batch_matches = batch_belongs_to_run(batch, selected, next_begin)
    audited_matrix = {boss: [int(any(s["boss"] == boss and s["point"] == point for s in best))
                             for point in range(1, 6)] for boss in bosses}
    matrix_agrees = bool(batch_matches and batch.get("matrix") == audited_matrix)
    summary = {
        "generated_at": datetime.now(timezone.utc).isoformat(), "run_id": selected["run_id"],
        "audit_tool_sha256": sha(Path(__file__).read_bytes()),
        "scope": "read-only controlled real-hit/native-AI route audit; not natural full-campaign or multiplayer evidence",
        "server_session_receipt": str(session_path), "server_receipt": session,
        "map_config": {"path": str(map_path), "sha256": sha(map_raw), "map": map_data.get("map"),
                       "safe_core_count": len({b["core"] for b in boxes}), "safe_box_count": len(boxes)},
        "phase_or_player_process_not_resampled": True,
        "source_log": {"path": str(args.log), "bytes_read": len(raw), "sha256": sha(raw),
                       "json_rows": len(rows), "parse_errors": parse_errors,
                       "reproduce": "use --max-log-bytes with bytes_read and the same session/time/run selection"},
        "selection": {"since_unix": since, "until_unix": until, "epoch": selected["begin"].get("_epoch"),
                      "begin_line": selected["begin"]["_line"], "begin_unix": selected["begin"].get("time"),
                      "actor_serial": selected["initial_state"].get("actor_serial"),
                      "actor_userid": selected["initial_state"].get("actor_userid")},
        "runs_found": [{"run_id": r["run_id"], "begin_line": r["begin"]["_line"],
                        "begin_time": r["begin"].get("time"), "epoch": r["begin"].get("_epoch")} for r in runs],
        "goal": "expected Boss instances x five original points; each combination reaches any one returned-to base",
        "expected_boss_ids": bosses, "observed_boss_ids": actual_bosses, "boss_identities_match": identities_match,
        "goal_met": goal, "combinations_passed": len(best), "expected_combinations": expected_combinations,
        "audited_matrix": audited_matrix,
        "missing_combinations": missing, "classification_counts": dict(Counter(s["classification"] for s in scenes)),
        "best_routes": [compact(s) for s in best], "failed_walking_attempts": failures,
        "operator_or_setup_failures": non_walking, "invalid_raw_passes": evidence_failures,
        "batch_receipt": {"path": str(batch_path), "matches_selected_run": batch_matches,
                          "completed": batch.get("completed") if batch_matches else None,
                          "matrix": batch.get("matrix") if batch_matches else None,
                          "matrix_agrees_with_audit": matrix_agrees,
                          "poll_times_used_for_chase_duration": False},
        "fixture_source_audit": source_audit, "safe_boxes": boxes,
        "trajectory_limits": "Actual sampled positions and bounded gaps are checked. No interpolation is treated as an observed engine frame. MoveType0 alone is not a motion failure.",
        "artifacts": {"scenes": "scene_audit.json", "trajectories": "trajectories.json", "plot": "trajectories.svg"}}
    write_json(run_dir / "scene_audit.json", [compact(s) for s in scenes])
    write_json(run_dir / "trajectories.json", {str(s["scene"]): s["trajectory"] for s in scenes})
    plot_svg(run_dir / "trajectories.svg", scenes, best, selected["configured_points"], boxes)
    write_json(run_dir / "summary.json", summary)
    write_json(output / "index.json", {"latest_selected_run": selected["run_id"],
               "summary": str(run_dir / "summary.json"), "runs_found": summary["runs_found"]})
    print(json.dumps({"summary": str(run_dir / "summary.json"), "goal_met": goal,
                      "combinations_passed": len(best), "classes": summary["classification_counts"],
                      "invalid_raw_passes": len(evidence_failures), "batch_completed": bool(batch_matches and batch.get("completed"))}, ensure_ascii=False))
    return 0 if not args.require_complete or (goal and batch_matches and batch.get("completed") and matrix_agrees) else 1


if __name__ == "__main__":
    raise SystemExit(main())
