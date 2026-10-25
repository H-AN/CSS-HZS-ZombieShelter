"""Append five explicit BossSpawnPoints to a separate candidate map config.

This offline migration never edits the source, a BSP or the fixed server ROOT.
Known coordinates are historical marker data, not five verified safe spawns.
Existing point tables are rejected, and different existing outputs are never
overwritten. Use --check to verify an existing candidate and receipt read-only.
--known-map repeats a recorded native marker five times; the optional local
extras flag retains the two previously configured desert companion markers.
JSON origins need three finite +/-15000 numbers; optional angles default to
0 0 0 and otherwise need three finite +/-360 numbers.
"""
from __future__ import annotations

import argparse
import copy
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import runpy
import struct
from typing import Any


FIXED_ROOT = Path(r"D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器")
SLOTS = tuple(f"boss{index}" for index in range(1, 6))
_PARSE = runpy.run_path(str(Path(__file__).with_name("migrate_v05_materials.py")))["parse"]
_NUMBER = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$", re.ASCII)
_ENTITY_POINTS = {
    "zsh_desertplant_cso_v1_d": "-176 0 2.031",
    "zsh_desert_han": "1585 1756 -1533.96875",
    "zsh_desert_han_dn": "1585 1756 -1533.96875",
    "zsh_deadcity_han": "224 0 36.03125",
    "zsh_deadcity_han_dn": "224 0 36.03125",
}
_LOCAL_EXTRA = ("1280 2800 -1493.96875", "2400 1280 -1493.96875")


class MigrationError(ValueError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _json_pairs(pairs: list[tuple[str, Any]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise MigrationError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_points_json(path: Path) -> tuple[dict, dict]:
    raw = path.read_bytes()
    def reject_constant(value: str) -> None:
        raise MigrationError(f"non-finite JSON constant: {value}")
    loaded = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=_json_pairs, parse_constant=reject_constant)
    if isinstance(loaded, dict) and set(loaded) == {"BossSpawnPoints"}:
        loaded = loaded["BossSpawnPoints"]
    return validate_points(loaded), {"kind": "explicit_five_slot_json", "path": str(path.resolve()), "sha256": sha256(raw)}


def _vector(value: Any, field: str, origin: bool) -> str:
    parts = value.split() if isinstance(value, str) else value
    if not isinstance(parts, (list, tuple)) or len(parts) != 3:
        raise MigrationError(f"{field}: expected three numbers")
    result = []
    for part in parts:
        if isinstance(part, bool) or not isinstance(part, (str, int, float)):
            raise MigrationError(f"{field}: invalid numeric component")
        atom = str(part).strip()
        if not _NUMBER.fullmatch(atom):
            raise MigrationError(f"{field}: invalid numeric component {atom!r}")
        number = float(atom)
        bound = 15000 if origin else 360
        if not math.isfinite(number) or abs(number) > bound:
            raise MigrationError(f"{field}: expected finite +/-{bound} coordinates")
        try:
            if not math.isfinite(struct.unpack("<f", struct.pack("<f", number))[0]):
                raise MigrationError(f"{field}: exceeds finite engine float range")
        except OverflowError as error:
            raise MigrationError(f"{field}: exceeds finite engine float range") from error
        result.append(atom)
    return " ".join(result)


def validate_points(points: Any) -> dict[str, dict[str, str]]:
    if not isinstance(points, dict) or set(points) != set(SLOTS):
        raise MigrationError("BossSpawnPoints must contain exactly boss1..boss5")
    normalized = {}
    for name in SLOTS:
        row = points[name]
        if not isinstance(row, dict) or "origin" not in row or set(row) - {"origin", "angles"}:
            raise MigrationError(f"{name}: origin required; only origin/angles are allowed")
        normalized[name] = {"origin": _vector(row["origin"], f"{name}/origin", True),
                            "angles": _vector(row.get("angles", "0 0 0"), f"{name}/angles", False)}
    return normalized


def known_points(map_name: str, include_local_extra: bool = False) -> tuple[dict, dict]:
    if map_name not in _ENTITY_POINTS:
        raise MigrationError(f"no recorded marker coordinates for map {map_name!r}; supply --points-json")
    if include_local_extra and map_name not in ("zsh_desert_han", "zsh_desert_han_dn"):
        raise MigrationError("local extra markers apply only to the two recorded desert maps")
    origins = [_ENTITY_POINTS[map_name]] * 5
    if include_local_extra:
        origins[1:3] = _LOCAL_EXTRA
    points = validate_points({slot: {"origin": position, "angles": "0 180 0"} for slot, position in zip(SLOTS, origins)})
    return points, {"kind": "recorded_markers_20261025", "map": map_name,
                    "include_local_extra_markers": include_local_extra,
                    "source": "read-only BSP entity lump; local extra origins from LocalHZSExtraBossMarkers.cfg",
                    "safe_spawn_verified": False}


def _flatten(node: dict, prefix: str = "") -> dict[str, str]:
    result = {}
    for key, value in node.items():
        path = f"{prefix}/{key}" if prefix else key
        if isinstance(value, dict):
            result.update(_flatten(value, path))
        else:
            result[path] = value
    return result


def _root_closing_index(text: str) -> int:
    depth = 0
    closing = None
    for token in re.finditer(r'//[^\n]*|"(?:\\.|[^"\\])*"|[{}]', text):
        atom = token.group()
        if atom == "{":
            depth += 1
        elif atom == "}":
            depth -= 1
            if depth == 0:
                closing = token.start()
    if depth != 0 or closing is None:
        raise MigrationError("could not locate the validated ShelterMap root close")
    return closing


def build_candidate(raw: bytes, points: dict) -> tuple[bytes, dict]:
    text = raw.decode("utf-8-sig")
    before = _PARSE(text)
    if list(before) != ["ShelterMap"] or not isinstance(before["ShelterMap"], dict):
        raise MigrationError("source must have one ShelterMap root")
    if "BossSpawnPoints" in before["ShelterMap"]:
        raise MigrationError("source already contains BossSpawnPoints; preserved unchanged, not overwritten")
    points = validate_points(points)
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = ["    // 仅迁移旧坐标；五槽待选点，未安全验收；重复点允许重叠。",
             "    // 保留旧 Bosses/marker 叶；本工具不改 BSP、不选择新坐标。",
             '    "BossSpawnPoints"', "    {"]
    for slot in SLOTS:
        lines.extend([f'        "{slot}"', "        {", f'            "origin" "{points[slot]["origin"]}"',
                      f'            "angles" "{points[slot]["angles"]}"', "        }"])
    lines.append("    }")
    closing = _root_closing_index(text)
    addition = ("" if text[:closing].endswith(("\r", "\n")) else newline) + newline.join(lines) + newline
    bom = b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b""
    insert_byte = len(bom) + len(text[:closing].encode("utf-8"))
    inserted = addition.encode("utf-8")
    output = bom + (text[:closing] + addition + text[closing:]).encode("utf-8")
    after = _PARSE(output.decode("utf-8-sig"))
    restored = copy.deepcopy(after)
    added_points = restored["ShelterMap"].pop("BossSpawnPoints", None)
    if restored != before or added_points != points:
        raise MigrationError("parsed source changed outside BossSpawnPoints")
    if output[:insert_byte] + output[insert_byte + len(inserted):] != raw:
        raise MigrationError("original bytes changed outside the inserted block")
    old_leaves, new_leaves = _flatten(before), _flatten(after)
    added = {key: value for key, value in new_leaves.items() if key not in old_leaves}
    removed = {key: value for key, value in old_leaves.items() if key not in new_leaves}
    changed = {key: {"before": old_leaves[key], "after": new_leaves[key]} for key in old_leaves.keys() & new_leaves.keys() if old_leaves[key] != new_leaves[key]}
    if len(added) != 10 or removed or changed or any(not key.startswith("ShelterMap/BossSpawnPoints/") for key in added):
        raise MigrationError("unexpected leaf diff")
    first_by_coordinate = {}
    duplicates = {}
    for slot, row in points.items():
        coordinate = tuple(float(atom) for field in ("origin", "angles") for atom in row[field].split())
        if coordinate in first_by_coordinate:
            duplicates[slot] = first_by_coordinate[coordinate]
        else:
            first_by_coordinate[coordinate] = slot
    return output, {"map": before["ShelterMap"].get("map", ""), "points": points,
                    "source_leaf_count": len(old_leaves), "output_leaf_count": len(new_leaves),
                    "leaf_diff": {"added": added, "removed": removed, "changed": changed},
                    "other_parsed_leaves_unchanged": True, "other_source_bytes_unchanged": True,
                    "duplicate_slots": duplicates, "safe_spawn_verified": False}


def _validate_destination(path: Path, inputs: set[Path]) -> None:
    resolved = path.resolve()
    fixed = FIXED_ROOT.resolve()
    if resolved in inputs:
        raise MigrationError("output/report may not overwrite an input file")
    if resolved == fixed or fixed in resolved.parents:
        raise MigrationError("writing inside the fixed live server ROOT is forbidden")


def migrate(source: Path, output: Path, *, points_json: Path | None = None, use_known: bool = False,
            include_local_extra: bool = False, report: Path | None = None, check_only: bool = False) -> dict:
    source, output = source.resolve(), output.resolve()
    report = report.resolve() if report else output.with_suffix(output.suffix + ".migration.json")
    points_json = points_json.resolve() if points_json else None
    inputs = {source} | ({points_json} if points_json else set())
    _validate_destination(output, inputs)
    _validate_destination(report, inputs)
    if report == output or output.suffix.lower() != ".cfg" or report.suffix.lower() != ".json":
        raise MigrationError("output must be a separate .cfg and report a separate .json")
    if bool(points_json) == use_known or (include_local_extra and not use_known):
        raise MigrationError("choose --points-json or --known-map; local extras require --known-map")
    raw = source.read_bytes()
    parsed = _PARSE(raw.decode("utf-8-sig"))
    if list(parsed) != ["ShelterMap"] or not isinstance(parsed["ShelterMap"], dict):
        raise MigrationError("source must have one ShelterMap root")
    if "BossSpawnPoints" in parsed["ShelterMap"]:
        raise MigrationError("source already contains BossSpawnPoints; preserved unchanged, not overwritten")
    map_name = parsed["ShelterMap"].get("map", "")
    points, point_source = load_points_json(points_json) if points_json else known_points(map_name, include_local_extra)
    candidate, details = build_candidate(raw, points)
    receipt = {"kind": "offline_boss_spawn_points_candidate_migration", "source": str(source),
               "source_sha256": sha256(raw), "output": str(output), "output_sha256": sha256(candidate),
               "point_source": point_source, **details, "writes_live_server": False, "bsp_modified": False,
               "game_commands_sent": False, "runtime_tested": False}
    if output.exists() and output.read_bytes() != candidate:
        raise MigrationError("different output already exists; choose a new candidate path")
    previous = None
    if report.exists():
        previous = json.loads(report.read_text(encoding="utf-8"), object_pairs_hook=_json_pairs)
        if {key: previous.get(key) for key in receipt} != receipt:
            raise MigrationError("different migration report already exists; choose a new report path")
    if check_only:
        if not output.is_file() or previous is None:
            raise MigrationError("--check requires the existing candidate and matching report")
    else:
        if not output.exists():
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open("xb") as handle:
                handle.write(candidate)
        if previous is None:
            report.parent.mkdir(parents=True, exist_ok=True)
            receipt["created_at"] = datetime.now(timezone(timedelta(hours=8))).isoformat()
            with report.open("x", encoding="utf-8", newline="\n") as handle:
                json.dump(receipt, handle, ensure_ascii=False, indent=2)
                handle.write("\n")
    result = previous or receipt
    return {"output": str(output), "report": str(report), "source_sha256": result["source_sha256"],
            "output_sha256": result["output_sha256"], "added_leaves": 10, "changed_old_leaves": 0,
            "removed_old_leaves": 0, "checked_only": check_only, "safe_spawn_verified": False}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Read-only source ShelterMap configuration")
    parser.add_argument("--output", type=Path, required=True, help="Explicit separate candidate .cfg, outside the fixed live ROOT")
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument("--points-json", type=Path, help="Strict JSON object with exactly boss1..boss5 (or one BossSpawnPoints wrapper)")
    choice.add_argument("--known-map", action="store_true", help="Use recorded 2026-10-25 native marker data, repeated into five slots")
    parser.add_argument("--include-local-extra-markers", action="store_true", help="With --known-map, preserve Angela/Yixing old points on the two desert maps")
    parser.add_argument("--report", type=Path, help="Separate JSON source/hash/leaf-diff receipt; default <output>.migration.json")
    parser.add_argument("--check", action="store_true", help="Verify existing output/report without writing")
    args = parser.parse_args()
    try:
        result = migrate(args.source, args.output, points_json=args.points_json, use_known=args.known_map,
                         include_local_extra=args.include_local_extra_markers, report=args.report, check_only=args.check)
    except (MigrationError, ValueError, OSError) as error:
        parser.error(str(error))
    print(json.dumps(result, ensure_ascii=True))


if __name__ == "__main__":
    main()
