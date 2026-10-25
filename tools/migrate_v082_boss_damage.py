"""Create an offline, byte-preserving three-Boss damage candidate (10 -> 30).

Explicit --source/--output are required. This tool never writes inside the
fixed live server ROOT and never overwrites an input or a different artifact.
Only zombies/{boss_pangzi,boss_angela,boss_yixing}/damage may change. Values
already equal to 30 are accepted unchanged; every other value fails closed.
Duplicate keys (including case aliases), missing targets and malformed KV fail.
UTF-8/BOM, whitespace, comments, CRLF and all non-target bytes are retained.

An exact source backup and JSON leaf/byte receipt accompany the candidate.
Repeating the same input/outputs is idempotent. After changing the source,
choose new artifact paths to retain the earlier audit, rather than overwrite it.
For an official global config, map-specific ZombieData overrides are checked
automatically; any such file requires separate review and stops this migration.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from typing import NamedTuple

FIXED_ROOT = Path(r"D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器")
TARGETS = ("boss_pangzi", "boss_angela", "boss_yixing")


class MigrationError(ValueError):
    pass


class Token(NamedTuple):
    kind: str
    value: str
    start: int
    end: int


def digest(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def lex(raw: bytes) -> list[Token]:
    tokens, position = [], 3 if raw.startswith(b"\xef\xbb\xbf") else 0
    while position < len(raw):
        if raw[position] in b" \t\r\n":
            position += 1
        elif raw[position:position + 2] == b"//":
            newline = raw.find(b"\n", position + 2)
            position = len(raw) if newline < 0 else newline + 1
        elif raw[position:position + 2] == b"/*":
            end = raw.find(b"*/", position + 2)
            if end < 0:
                raise MigrationError(f"Unterminated block comment at byte {position}")
            position = end + 2
        elif raw[position] in b"{}":
            tokens.append(Token(chr(raw[position]), "", position, position + 1))
            position += 1
        elif raw[position] == 34:
            start, position = position, position + 1
            while position < len(raw):
                if raw[position] == 92:
                    position += 2
                    continue
                if raw[position] == 34:
                    value = raw[start + 1:position].decode("utf-8")
                    value = value.replace(r'\"', '"').replace(r'\\', '\\')
                    tokens.append(Token("string", value, start, position + 1))
                    position += 1
                    break
                position += 1
            else:
                raise MigrationError(f"Unterminated quoted string at byte {start}")
        else:
            raise MigrationError(f"Unexpected non-quoted token at byte {position}")
    return tokens


def parse(raw: bytes) -> dict:
    tokens, cursor = lex(raw), 0

    def block(nested=False, depth=0):
        nonlocal cursor
        if depth > 128:
            raise MigrationError("KV nesting exceeds 128")
        result, folded = {}, set()
        while cursor < len(tokens):
            key = tokens[cursor]
            cursor += 1
            if key.kind == "}":
                if not nested:
                    raise MigrationError("Unexpected closing brace")
                return result
            if key.kind != "string" or cursor >= len(tokens):
                raise MigrationError(f"Missing key/value at byte {key.start}")
            if key.value.casefold() in folded:
                raise MigrationError(f"Duplicate or case-alias key {key.value!r} at byte {key.start}")
            folded.add(key.value.casefold())
            value = tokens[cursor]
            cursor += 1
            if value.kind == "{":
                result[key.value] = block(True, depth + 1)
            elif value.kind == "string":
                result[key.value] = value
            else:
                raise MigrationError(f"Expected value or block at byte {value.start}")
        if nested:
            raise MigrationError("Missing closing brace")
        return result

    return block()


def flatten(node: dict, parent=()) -> dict[tuple, str]:
    leaves = {}
    for key, value in node.items():
        path = parent + (key,)
        if isinstance(value, dict):
            leaves.update(flatten(value, path))
        else:
            leaves[path] = value.value
    return leaves


def build_candidate(raw: bytes) -> tuple[bytes, dict]:
    before = parse(raw)
    if list(before) != ["zombies"] or not isinstance(before["zombies"], dict):
        raise MigrationError("Expected one zombies root block")
    edits, target_values = [], {}
    for key in TARGETS:
        node = before["zombies"].get(key)
        if not isinstance(node, dict) or not isinstance(node.get("damage"), Token):
            raise MigrationError(f"Missing scalar zombies/{key}/damage")
        token = node["damage"]
        literal = raw[token.start + 1:token.end - 1]
        if token.value not in ("10", "30") or literal not in (b"10", b"30"):
            raise MigrationError(f"Unknown damage for {key}: expected exact 10 or 30, found {token.value!r}")
        target_values[key] = token.value
        if token.value == "10":
            edits.append((token.start + 1, token.end - 1, b"30", key))
    pieces, cursor = [], 0
    for start, end, replacement, _ in sorted(edits):
        pieces.extend((raw[cursor:start], replacement))
        cursor = end
    pieces.append(raw[cursor:])
    candidate = b"".join(pieces)
    old, new = flatten(before), flatten(parse(candidate))
    added = {"/".join(k): v for k, v in new.items() if k not in old}
    removed = {"/".join(k): v for k, v in old.items() if k not in new}
    changed = {"/".join(k): {"before": old[k], "after": new[k]}
               for k in old.keys() & new.keys() if old[k] != new[k]}
    expected = {f"zombies/{key}/damage" for key, value in target_values.items() if value == "10"}
    if added or removed or set(changed) != expected or any(new[("zombies", key, "damage")] != "30" for key in TARGETS):
        raise MigrationError("Unexpected parsed leaf change")
    # Every allowed replacement has the same length. Verify original bytes by
    # reverting exactly those three possible content ranges, not by re-emitting KV.
    reverted = bytearray(candidate)
    for start, end, _, _ in edits:
        reverted[start:end] = raw[start:end]
    if bytes(reverted) != raw:
        raise MigrationError("Bytes outside the approved target spans changed")
    return candidate, {
        "target_values_before": target_values, "target_damage_after": 30,
        "changed_leaf_count": len(changed), "source_leaf_count": len(old), "output_leaf_count": len(new),
        "leaf_diff": {"added": added, "removed": removed, "changed": dict(sorted(changed.items()))},
        "approved_byte_spans": [{"start": s, "end_exclusive": e, "boss": key,
                                 "before": raw[s:e].decode("ascii"), "after": replacement.decode("ascii")}
                                for s, e, replacement, key in sorted(edits)],
        "other_parsed_leaves_unchanged": True, "other_source_bytes_unchanged": True,
        "idempotent_noop": not edits,
    }


def scan_overrides(source: Path, override_dir: Path | None) -> dict:
    scope = override_dir
    if scope is None and source.name == "HanZombieScenarioZombieData.cfg" and source.parent.name == "HanZombieScenario":
        scope = source.parent
    if scope is None:
        return {"checked": False, "reason": "not an official global path; use --override-dir for a deployment scan"}
    scope = scope.resolve()
    if not scope.is_dir():
        raise MigrationError(f"Override scan directory missing: {scope}")
    found = sorted(str(p.resolve()) for p in scope.rglob("HanZombieScenarioZombieData.cfg") if p.resolve() != source)
    if found:
        raise MigrationError("Map-specific/other ZombieData files require separate review: " + ", ".join(found))
    return {"checked": True, "directory": str(scope), "overrides": [],
            "engine_rule": "HZS prefers configs/HanZombieScenario/<map>/HanZombieScenarioZombieData.cfg when present"}


def validate_destinations(source: Path, paths: tuple[Path, ...]) -> None:
    if len(set(paths)) != len(paths) or source in paths:
        raise MigrationError("Source, output, backup and receipt must be distinct files")
    fixed = FIXED_ROOT.resolve()
    for path in paths:
        if path == fixed or fixed in path.parents:
            raise MigrationError("Writing inside fixed live ROOT is forbidden; generate a separate candidate")


def migrate(source: Path, output: Path, *, backup: Path | None = None,
            report: Path | None = None, override_dir: Path | None = None, check_only=False) -> dict:
    source, output = source.resolve(), output.resolve()
    backup = (backup or output.with_name(output.stem + ".source_before.cfg")).resolve()
    report = (report or output.with_suffix(output.suffix + ".damage-migration.json")).resolve()
    validate_destinations(source, (output, backup, report))
    if output.suffix.lower() != ".cfg" or backup.suffix.lower() != ".cfg" or report.suffix.lower() != ".json":
        raise MigrationError("Output and backup must be .cfg; receipt must be .json")
    raw = source.read_bytes()
    candidate, details = build_candidate(raw)
    overrides = scan_overrides(source, override_dir)
    receipt = {"kind": "offline_three_boss_damage_migration", "source": str(source),
               "source_sha256": digest(raw), "output": str(output), "output_sha256": digest(candidate),
               "backup": str(backup), "backup_sha256": digest(raw), "override_scan": overrides,
               **details, "writes_live_server": False, "hws_or_hzs_source_modified": False,
               "game_commands_sent": False, "runtime_tested": False}
    artifacts = ((output, candidate), (backup, raw))
    for path, data in artifacts:
        if path.exists() and path.read_bytes() != data:
            raise MigrationError(f"Different artifact already exists; choose a new path: {path}")
    previous = None
    if report.exists():
        previous = json.loads(report.read_text(encoding="utf-8"))
        if {key: previous.get(key) for key in receipt} != receipt:
            raise MigrationError("Different receipt exists; retain it and choose a new path")
    if check_only:
        if not all(path.is_file() for path, _ in artifacts) or previous is None:
            raise MigrationError("--check requires existing matching candidate, backup and receipt")
    else:
        for path, data in ((backup, raw), (output, candidate)):
            if not path.exists():
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("xb") as handle:
                    handle.write(data)
        if previous is None:
            receipt["created_at"] = datetime.now(timezone.utc).isoformat()
            report.parent.mkdir(parents=True, exist_ok=True)
            with report.open("x", encoding="utf-8", newline="\n") as handle:
                json.dump(receipt, handle, ensure_ascii=False, indent=2)
                handle.write("\n")
    return previous or receipt


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--override-dir", type=Path)
    parser.add_argument("--check", action="store_true", help="Read-only verify existing candidate/backup/receipt")
    args = parser.parse_args(argv)
    try:
        receipt = migrate(args.source, args.output, backup=args.backup, report=args.report,
                          override_dir=args.override_dir, check_only=args.check)
    except (MigrationError, OSError, UnicodeError) as exc:
        parser.exit(2, f"Refused: {exc}\n")
    print(json.dumps({"output": receipt["output"], "source_sha256": receipt["source_sha256"],
                      "output_sha256": receipt["output_sha256"], "changed_leaf_count": receipt["changed_leaf_count"],
                      "other_source_bytes_unchanged": True, "writes_live_server": False,
                      "checked_only": args.check}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
