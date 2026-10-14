#!/usr/bin/env python3
"""Build and audit SourceMod Chinese/English phrases without external dependencies.

Manifests: tests/phrases_*.json, as {key: {format?, en, chi}}.
Run with --check to verify the committed file without rewriting it.
This audits literal phrase references and parameter counts; dynamic references
are reported and the shipped configuration labels are checked independently.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "addons/sourcemod/translations/HanZombieShelter.phrases.txt"
MANDATORY = ("phrases_hud.json", "phrases_world.json", "phrases_inventory.json")
LANGUAGES = ("en", "chi")
FORBIDDEN = {"\u00b7": "|", "\u00d7": "x"}
HAN = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
FORMAT_ENTRY = re.compile(r"\{([1-9][0-9]*):([^{}]+)\}")
FORMAT_TYPE = re.compile(r"[-+0 #]*[0-9]*(?:\.[0-9]+)?[scdifxX]")
PLACEHOLDER = re.compile(r"\{([1-9][0-9]*)\}")
# Values are zero-based: key argument, first translated value argument.
# Keep these aligned with the external plugin's localization helpers.
PHRASE_HELPERS = {"AnnouncePhrase": (0, 1), "TranslateLabel": (1, None),
    "AdminAdd": (3, None), "AddDefenseMenuItem": (4, None)}
DIRECT_FORMATTERS = {"Format": 2, "FormatEx": 2, "PrintToChat": 1,
    "PrintToChatAll": 0, "PrintHintText": 1, "PrintCenterText": 1,
    "ReplyToCommand": 1, "ShowHudText": 2, "PrintToConsole": 1,
    "HudFeedback": 1, "Tell": 1}
LOG_CALLS = {"LogError", "LogMessage", "LogToFile", "LogToFileEx", "PrintToServer", "ConfigFailure", "ForceChangeLevel"}
DEFAULT_LABEL_FIELDS = {"display_name", "victory_phrase", "loss_phrase"}


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    start: int
    end: int
    line: int


def decode_string(raw: str) -> str:
    return re.sub(r"\\([\\\"nrt])", lambda m: {"n": "\n", "r": "\r", "t": "\t", "\\": "\\", '"': '"'}[m[1]], raw)


def tokenize(source: str) -> list[Token]:
    pattern = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_][A-Za-z0-9_]*|\s+|.', re.DOTALL)
    tokens = []
    for match in pattern.finditer(source):
        value = match.group()
        if value.isspace() or value.startswith("//") or value.startswith("/*"):
            continue
        kind = "string" if value.startswith('"') else "token"
        tokens.append(Token(kind, decode_string(value[1:-1]) if kind == "string" else value,
            match.start(), match.end(), source.count("\n", 0, match.start()) + 1))
    return tokens


def call_arguments(tokens: list[Token], opening: int):
    depth = 0
    args, current = [], []
    for i in range(opening + 1, len(tokens)):
        value = tokens[i].value
        if tokens[i].kind == "string":
            current.append(tokens[i])
            continue
        if value in ("(", "[", "{"):
            depth += 1
        elif value in (")", "]", "}"):
            if value == ")" and depth == 0:
                if current or args:
                    args.append(current)
                return args, i
            depth -= 1
        if value == "," and depth == 0:
            args.append(current)
            current = []
        else:
            current.append(tokens[i])
    return [], opening


def literal(argument: list[Token]) -> str | None:
    if len(argument) == 1 and argument[0].kind == "string":
        return argument[0].value
    return None


def literal_choices(argument: list[Token]) -> list[str]:
    value = literal(argument)
    if value is not None:
        return [value]
    depth = 0
    question = None
    pending = 0
    for i, token in enumerate(argument):
        if token.kind == "string":
            continue
        if token.value in ("(", "[", "{"):
            depth += 1
        elif token.value in (")", "]", "}"):
            depth -= 1
        elif depth == 0 and token.value == "?":
            if question is None:
                question = i
            pending += 1
        elif depth == 0 and token.value == ":" and question is not None:
            pending -= 1
            if pending == 0:
                return literal_choices(argument[question + 1:i]) + literal_choices(argument[i + 1:])
    return []


def unique_json_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result and result[key] != value:
            raise ValueError(f"Conflicting duplicate JSON key: {key!r}")
        result[key] = value
    return result


def load_manifests(root: Path):
    errors, phrases, origins = [], {}, {}
    paths = sorted((root / "tests").glob("phrases_*.json"))
    names = {p.name for p in paths}
    for name in MANDATORY:
        if name not in names:
            errors.append(f"Missing required manifest: tests/{name}")
    for path in paths:
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=unique_json_object)
            if isinstance(data, dict) and "phrases" in data:
                data = data["phrases"]
            if isinstance(data, list):
                entries = [(item.get("key", item.get("name")), item) for item in data]
            else:
                entries = list(data.items())
            for key, item in entries:
                if not isinstance(key, str) or not key or not isinstance(item, dict):
                    raise ValueError("Every phrase needs a nonempty key and an object")
                value = {lang: item[lang] for lang in LANGUAGES}
                value["format"] = item.get("format", item.get("#format", ""))
                if not all(isinstance(v, str) for v in value.values()):
                    raise ValueError(f"{key}: format/en/chi must be strings")
                if key in phrases and phrases[key] != value:
                    errors.append(f"Conflicting duplicate phrase {key!r}: {origins[key]} vs {path.name}")
                else:
                    phrases[key], origins[key] = value, path.name
        except (ValueError, KeyError, AttributeError, TypeError) as error:
            errors.append(f"{path.name}: {error}")
    return phrases, errors, paths


def validate_phrases(phrases):
    errors, counts = [], {}
    for key, values in phrases.items():
        fmt = values["format"]
        entries = FORMAT_ENTRY.findall(fmt)
        canonical = ",".join("{" + index + ":" + kind + "}" for index, kind in entries)
        if canonical != fmt:
            errors.append(f"{key}: malformed SourceMod #format {fmt!r}")
        indices = [int(i) for i, _ in entries]
        if indices != list(range(1, len(indices) + 1)):
            errors.append(f"{key}: #format indices must be consecutive from 1")
        for _, kind in entries:
            if not FORMAT_TYPE.fullmatch(kind):
                errors.append(f"{key}: unsupported format type {kind!r}")
        counts[key] = len(indices)
        for lang in LANGUAGES:
            text = values[lang]
            used = {int(x) for x in PLACEHOLDER.findall(text)}
            if used != set(indices):
                errors.append(f"{key}/{lang}: placeholders {sorted(used)} do not match #format {indices}")
            remainder = PLACEHOLDER.sub("", text)
            if "{" in remainder or "}" in remainder:
                errors.append(f"{key}/{lang}: unsupported brace expression")
            for bad, replacement in FORBIDDEN.items():
                if bad in text:
                    errors.append(f"{key}/{lang}: replace U+{ord(bad):04X} with ASCII {replacement!r}")
            if not text:
                errors.append(f"{key}/{lang}: empty translation")
    return errors, counts


def audit_sources(root, phrases, counts, allow_custom_labels=False):
    errors, refs, dynamic, custom = [], [], [], []
    source_root = root / "addons/sourcemod/scripting"
    paths = [source_root / "HanZombieShelter.sp", *sorted((source_root / "HanZombieShelter").glob("*.inc"))]
    for path in paths:
        if not path.is_file():
            errors.append(f"Missing source: {path}")
            continue
        source = path.read_text(encoding="utf-8-sig")
        tokens = tokenize(source)
        calls = []
        for i in range(len(tokens) - 1):
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", tokens[i].value) and tokens[i + 1].value == "(":
                args, end = call_arguments(tokens, i + 1)
                if end + 1 < len(tokens) and tokens[end + 1].value == "{":
                    continue  # Function declarations are not invocation references.
                calls.append((tokens[i], args, tokens[end].end))
        relative = path.relative_to(root).as_posix()
        for token in tokens:
            if token.kind != "string":
                continue
            in_log = any(call.value in LOG_CALLS and call.start < token.start < end for call, _, end in calls)
            if not in_log and HAN.search(token.value):
                errors.append(f"{relative}:{token.line}: Chinese source literal outside logs: {token.value!r}")
            if not in_log:
                for bad, replacement in FORBIDDEN.items():
                    if bad in token.value:
                        errors.append(f"{relative}:{token.line}: replace U+{ord(bad):04X} with ASCII {replacement!r}")
        for call, args, _ in calls:
            key_arg = value_arg = None
            if call.value in PHRASE_HELPERS:
                key_arg, value_arg = PHRASE_HELPERS[call.value]
            elif call.value in DIRECT_FORMATTERS:
                fmt_index = DIRECT_FORMATTERS[call.value]
                if fmt_index < len(args):
                    fmt = literal(args[fmt_index])
                    if fmt in ("%T", "%t"):
                        key_arg = fmt_index + 1
                        value_arg = key_arg + (2 if fmt == "%T" else 1)
                    elif fmt and ("%T" in fmt or "%t" in fmt):
                        dynamic.append(f"{relative}:{call.line}: composite translation format requires review")
            if key_arg is None or key_arg >= len(args):
                continue
            keys = literal_choices(args[key_arg])
            if not keys:
                dynamic.append(f"{relative}:{call.line}: {call.value} dynamic phrase key")
                continue
            for key in keys:
                if key not in phrases:
                    errors.append(f"{relative}:{call.line}: undefined phrase {key!r}")
                    continue
                refs.append({"file": relative, "line": call.line, "key": key})
                if value_arg is not None and len(args) - value_arg != counts[key]:
                    errors.append(f"{relative}:{call.line}: {key!r} expects {counts[key]} values, got {len(args) - value_arg}")
                if call.value == "TranslateLabel" and counts[key]:
                    errors.append(f"{relative}:{call.line}: label {key!r} unexpectedly requires parameters")
    config_root = root / "addons/sourcemod/configs/HanZombieShelter"
    for path in sorted(config_root.rglob("*.cfg")):
        tokens = tokenize(path.read_text(encoding="utf-8-sig"))
        for left, right in zip(tokens, tokens[1:]):
            if left.kind != "string" or left.value not in DEFAULT_LABEL_FIELDS or right.kind != "string":
                continue
            if right.value in phrases:
                if counts[right.value]:
                    errors.append(f"{path.name}:{right.line}: configured label {right.value!r} needs arguments")
                refs.append({"file": path.relative_to(root).as_posix(), "line": right.line, "key": right.value})
            elif allow_custom_labels:
                custom.append(f"{path.name}:{right.line}: literal custom label {right.value!r}")
            else:
                errors.append(f"{path.name}:{right.line}: shipped label is not a phrase: {right.value!r}")
    return errors, refs, dynamic, custom


def escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def render(phrases):
    lines = ["// Generated by tools/build_translations.py from tests/phrases_*.json.",
        "// Language keys: en = English, chi = Simplified Chinese. UTF-8.", '"Phrases"', "{"]
    for key in sorted(phrases):
        item = phrases[key]
        lines.extend([f'    "{escape(key)}"', "    {"])
        if item["format"]:
            lines.append(f'        "#format"    "{escape(item["format"])}"')
        for lang in LANGUAGES:
            lines.append(f'        "{lang}"    "{escape(item[lang])}"')
        lines.append("    }")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check existing output instead of rewriting")
    parser.add_argument("--allow-custom-labels", action="store_true", help="Permit literal labels in locally customized config")
    parser.add_argument("--report", type=Path, help="Optionally write the audit result as JSON")
    args = parser.parse_args(argv)
    phrases, errors, manifests = load_manifests(ROOT)
    phrase_errors, counts = validate_phrases(phrases)
    errors.extend(phrase_errors)
    source_errors, refs, dynamic, custom = audit_sources(ROOT, phrases, counts, args.allow_custom_labels)
    errors.extend(source_errors)
    output = render(phrases)
    if args.check and (not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8-sig") != output):
        errors.append("Translation file is absent or stale; run tools/build_translations.py")
    if not errors and not args.check:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(output, encoding="utf-8")
    report = {"ok": not errors, "phrase_count": len(phrases), "manifest_count": len(manifests),
        "literal_reference_count": len(refs), "references": refs,
        "dynamic_references": dynamic, "custom_labels": custom, "errors": errors,
        "output": str(OUTPUT)}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "references"}, ensure_ascii=False, indent=2))
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    raise SystemExit(main())
