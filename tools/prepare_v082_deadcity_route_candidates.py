"""Prepare exact user points and separate one-/three-Boss deadcity candidates.

Offline only: explicit source-map, points, source-stage and output-dir. Optional
mirror-report-dir may write only inside fixed ROOT/reports, never cstrike.
Source files are preserved; existing different artifacts are never overwritten.
The formal map only replaces its ten point leaves. Diagnostic candidates add
two Boss profiles and change one Stage customzombies leaf; they are not deployed.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

from migrate_v082_boss_damage import MigrationError, Token, flatten, lex, parse
from migrate_v082_boss_spawn_points import validate_points

FIXED_ROOT = Path(r"D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器")
MAP = "zsh_deadcity_han_dn"


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def scalar(tree, path):
    node = tree
    for key in path:
        if not isinstance(node, dict) or key not in node:
            raise MigrationError("Missing " + "/".join(path))
        node = node[key]
    if not isinstance(node, Token):
        raise MigrationError("Not a scalar: " + "/".join(path))
    return node


def leaf_diff(before, after):
    a, b = flatten(parse(before)), flatten(parse(after))
    return {"added": {"/".join(k): v for k, v in b.items() if k not in a},
            "removed": {"/".join(k): v for k, v in a.items() if k not in b},
            "changed": {"/".join(k): {"before": a[k], "after": b[k]}
                        for k in a.keys() & b.keys() if a[k] != b[k]}}


def patch_scalars(raw, replacements):
    tree, edits = parse(raw), []
    for path, value in replacements.items():
        token = scalar(tree, path)
        encoded = value.replace("\\", "\\\\").replace('"', '\\"').encode("utf-8")
        edits.append((token.start + 1, token.end - 1, encoded, "/".join(path)))
    output, cursor, spans = bytearray(), 0, []
    for start, end, value, path in sorted(edits):
        output.extend(raw[cursor:start])
        at = len(output)
        output.extend(value)
        spans.append({"path": path, "source_start": start, "source_end": end,
                      "output_start": at, "output_end": len(output),
                      "before": raw[start:end].decode("utf-8"), "after": value.decode("utf-8")})
        cursor = end
    output.extend(raw[cursor:])
    reverted = bytearray(output)
    for span in reversed(spans):
        reverted[span["output_start"]:span["output_end"]] = raw[span["source_start"]:span["source_end"]]
    if bytes(reverted) != raw:
        raise MigrationError("Unexpected bytes outside selected scalar content spans")
    return bytes(output), spans


def block_close(raw, wanted):
    tokens, cursor, found = lex(raw), 0, None

    def visit(path=()):
        nonlocal cursor, found
        while cursor < len(tokens):
            key = tokens[cursor]
            cursor += 1
            if key.kind == "}":
                if path == wanted:
                    found = key.start
                return
            value = tokens[cursor]
            cursor += 1
            if value.kind == "{":
                visit(path + (key.value,))
    visit()
    if found is None:
        raise MigrationError("Missing target block close")
    return found


def build_candidates(map_raw, point_raw, stage_raw):
    point_tree, map_tree, stage_tree = parse(point_raw), parse(map_raw), parse(stage_raw)
    if list(point_tree) != ["BossSpawnPoints"] or set(point_tree["BossSpawnPoints"]) != {f"boss{i}" for i in range(1, 6)}:
        raise MigrationError("Captured points must contain exactly boss1..boss5")
    points = {}
    for slot, node in point_tree["BossSpawnPoints"].items():
        if not isinstance(node, dict) or set(node) != {"origin", "angles"}:
            raise MigrationError("Each captured point requires only origin and angles")
        points[slot] = {field: scalar(point_tree, ("BossSpawnPoints", slot, field)).value for field in ("origin", "angles")}
    validate_points(points)  # Validation only: preserve the user's original strings below.
    if list(map_tree) != ["ShelterMap"] or scalar(map_tree, ("ShelterMap", "map")).value != MAP:
        raise MigrationError("Expected the deadcity DN map config")
    bosses = map_tree["ShelterMap"].get("Bosses")
    if not isinstance(bosses, dict) or set(bosses) != {"guard_01"} or scalar(map_tree, ("ShelterMap", "Bosses", "guard_01", "boss_profile")).value != "guard_fat":
        raise MigrationError("Expected the existing single guard_01/guard_fat map")
    stage_path = ("Stage", "Shelter", "customzombies")
    if list(stage_tree) != ["Stage"] or scalar(stage_tree, stage_path).value != "boss_pangzi:1":
        raise MigrationError("Expected exact existing single-Boss Stage customzombies")
    replacements = {("ShelterMap", "BossSpawnPoints", slot, field): value
                    for slot, fields in points.items() for field, value in fields.items()}
    formal, spans = patch_scalars(map_raw, replacements)
    formal_diff = leaf_diff(map_raw, formal)
    if formal_diff["added"] or formal_diff["removed"] or set(formal_diff["changed"]) != {"/".join(p) for p in replacements}:
        raise MigrationError("Formal candidate must change exactly the ten original point leaves")
    for path, value in replacements.items():
        if scalar(parse(formal), path).value != value:
            raise MigrationError("User point string changed")
    end = block_close(formal, ("ShelterMap", "Bosses"))
    insert_at = formal.rfind(b"\n", 0, end) + 1
    if formal[insert_at:end].strip():
        raise MigrationError("Bosses closing brace must be on its own line")
    newline = "\r\n" if b"\r\n" in formal else "\n"
    lines = []
    for identifier, profile in (("diag_angela", "guard_angela"), ("diag_yixing", "guard_yixing")):
        lines.extend([f'        "{identifier}"', '        {', f'            "boss_profile" "{profile}"', '        }'])
    added = (newline.join(lines) + newline).encode("utf-8")
    diagnostic = formal[:insert_at] + added + formal[insert_at:]
    assert diagnostic[:insert_at] + diagnostic[insert_at + len(added):] == formal
    diag_diff = leaf_diff(formal, diagnostic)
    expected_added = {"ShelterMap/Bosses/diag_angela/boss_profile": "guard_angela",
                      "ShelterMap/Bosses/diag_yixing/boss_profile": "guard_yixing"}
    if diag_diff != {"added": expected_added, "removed": {}, "changed": {}}:
        raise MigrationError("Unexpected diagnostic map change")
    diag_stage, stage_spans = patch_scalars(stage_raw, {stage_path: "boss_pangzi:1,boss_angela:1,boss_yixing:1"})
    stage_diff = leaf_diff(stage_raw, diag_stage)
    if stage_diff["added"] or stage_diff["removed"] or set(stage_diff["changed"]) != {"/".join(stage_path)}:
        raise MigrationError("Unexpected diagnostic Stage change")
    details = {"map": MAP, "points": points, "user_point_strings_preserved": True,
               "formal_map_diff_from_current": formal_diff, "formal_map_byte_spans": spans,
               "diagnostic_map_diff_from_formal": diag_diff,
               "diagnostic_map_diff_from_current": leaf_diff(map_raw, diagnostic),
               "diagnostic_stage_diff_from_current": stage_diff, "diagnostic_stage_byte_spans": stage_spans,
               "all_nonselected_source_bytes_preserved": True,
               "formal_enabled_bosses": {"guard_01": "guard_fat"},
               "diagnostic_enabled_bosses": {"guard_01": "guard_fat", "diag_angela": "guard_angela", "diag_yixing": "guard_yixing"},
               "formal_customzombies": "boss_pangzi:1", "diagnostic_customzombies": "boss_pangzi:1,boss_angela:1,boss_yixing:1",
               "diagnostic_is_temporary_restore_formal_after_testing": True,
               "deployed": False, "game_commands_sent": False, "runtime_tested": False}
    return {"user_captured_points.cfg": point_raw, "config/source_map_before.cfg": map_raw,
            "config/source_stage_before.cfg": stage_raw, "config/formal/zsh_deadcity_han_dn.cfg": formal,
            "config/formal/HanZombieScenarioStage.cfg": stage_raw,
            "config/diagnostic/zsh_deadcity_han_dn.cfg": diagnostic,
            "config/diagnostic/HanZombieScenarioStage.cfg": diag_stage}, details


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-map", required=True, type=Path)
    parser.add_argument("--points", required=True, type=Path)
    parser.add_argument("--source-stage", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--mirror-report-dir", type=Path)
    args = parser.parse_args(argv)
    inputs = {p.resolve() for p in (args.source_map, args.points, args.source_stage)}
    destinations = [args.output_dir.resolve()] + ([args.mirror_report_dir.resolve()] if args.mirror_report_dir else [])
    fixed, report_root = FIXED_ROOT.resolve(), (FIXED_ROOT / "reports").resolve()
    for destination in destinations:
        if destination == fixed or (fixed in destination.parents and not destination.is_relative_to(report_root)):
            parser.error("Fixed ROOT writes are limited to reports; cstrike/live writes are forbidden")
    blobs = [p.read_bytes() for p in (args.source_map, args.points, args.source_stage)]
    artifacts, details = build_candidates(*blobs)
    receipt = {"kind": "offline_deadcity_exact_user_points_and_temporary_three_boss_candidates",
               "sources": {label: {"path": str(path.resolve()), "sha256": digest(data)} for label, path, data in zip(
                   ("current_map", "user_points", "current_stage"), (args.source_map, args.points, args.source_stage), blobs)},
               "artifacts": {name: {"bytes": len(data), "sha256": digest(data)} for name, data in artifacts.items()}, **details}
    receipt_path = "candidate_preparation.json"
    prior = None
    for destination in destinations:
        path = destination / receipt_path
        if path.exists():
            candidate = json.loads(path.read_text(encoding="utf-8"))
            if {key: candidate.get(key) for key in receipt} != receipt:
                parser.error("Different existing receipt; choose another output directory")
            prior = candidate
        for name, data in artifacts.items():
            path = (destination / name).resolve()
            if path in inputs or (path.exists() and path.read_bytes() != data):
                parser.error("Output would overwrite an input or different artifact: " + str(path))
    if prior is None:
        receipt["created_at"] = datetime.now(timezone.utc).isoformat()
    else:
        receipt = prior
    artifacts[receipt_path] = (json.dumps(receipt, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    for destination in destinations:
        for name, data in artifacts.items():
            path = destination / name
            if not path.exists():
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("xb") as handle:
                    handle.write(data)
    print(json.dumps({"output_dir": str(destinations[0]), "mirror_report_dir": str(destinations[1]) if len(destinations) > 1 else None,
                      "user_points_sha256": receipt["sources"]["user_points"]["sha256"], "artifacts": receipt["artifacts"],
                      "deployed": False}, ensure_ascii=False))


if __name__ == "__main__":
    main()
