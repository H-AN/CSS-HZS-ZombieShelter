"""Create the 58-file Boss-melee/deadcity increment from its frozen formal receipt.

No build, deployment, HZS configuration write or game action is performed.
--dry-run permits the recorded point staging file and draft documentation but
writes no ZIP/report; a real package requires the maintained point fragment and
an explicit document freeze. The three previous release ZIPs are preserved.
"""
from __future__ import annotations

import argparse
import ast
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path, PurePosixPath
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "tests/reports/v082_boss_damage_20261025"
ROUTES = ROOT / "tests/reports/v082_deadcity_routes_20261025"
DIST = ROOT / "dist"
ARCHIVE = DIST / "CSS-HZS-ZombieShelter-v0.8.2-boss-damage-deadcity-update.zip"
FORMAL = REPORT / "build/HanZombieShelter.smx"
RECEIPT = REPORT / "formal_build.json"
BUILD_LOG = REPORT / "build/HanZombieShelter_compile.log"
PLUGIN = "addons/sourcemod/plugins/HanZombieShelter.smx"
MIGRATION = "tools/migrate_v082_boss_damage.py"
DOC = "docs/v0.8.2_Boss近战成长与死城点位.md"
POINT_SOURCE = ROOT / "server-support/map-point-updates/zsh_deadcity_han_dn/BossSpawnPoints.cfg"
POINT_DESTINATION = "map_point_updates/zsh_deadcity_han_dn/BossSpawnPoints.cfg"
INSTALL_NAME = "README_v0.8.2_Boss近战与死城增量.txt"
SUPERSEDED_SMX = "8e007ad817b48ca84e48d0f7d50ee47707b987bc93d66dc31e8149b512a43e6e"
OLD_PACKAGES = tuple(DIST / ("CSS-HZS-ZombieShelter-v0.8.2-" + suffix + "-update.zip")
                     for suffix in ("crafting", "boss-spawnpoints", "boss-routes"))

INSTALL_TEXT = """v0.8.2 Boss近战成长与死城五点增量

依赖已有完整v0.8.2环境与原有HZS／外部资源。先阅读docs/v0.8.2_Boss近战成长与死城点位.md；本包不含整份外部HZS数据、Stage、诊断配置或资源。

1. 备份现有主SMX与相关配置，由服主安排维护时替换addons/sourcemod/plugins/HanZombieShelter.smx。对应52份生产来源与完整文件身份见manifest.json。
2. 使用tools/migrate_v082_boss_damage.py，仅生成服务器目录之外的候选、源字节备份与JSON回执：
   python tools/migrate_v082_boss_damage.py --source <现有HanZombieScenarioZombieData.cfg> --output <独立候选目录/HanZombieScenarioZombieData.cfg>
   工具只依赖Python标准库。仅zombies/boss_pangzi、boss_angela、boss_yixing的damage允许精确10→30；已30不变，其他原值／缺键／重复键拒绝，其他字节保持。不要用其他服务器整份ZombieData覆盖现场。
3. 官方全局路径会自动扫描每图ZombieData覆盖，发现覆盖需单独审查；用另存备份作源时应提供--override-dir指向实际HanZombieScenario目录。工具拒绝直接覆盖源或写固定本地live ROOT。服主审核准确三叶差异后安排安装，并确认外部HZS在正常mapstart实际重读；磁盘修改和主SMX热替换不等于原生NPC已换用30。
4. map_point_updates/zsh_deadcity_han_dn/BossSpawnPoints.cfg仅合并到本图ShelterMap根下；是5个origin＋5个angles的片段，不是完整地图配置。已有同名块逐叶更新，不制造重复块；不得套用于沙漠DN或其他BSP。
5. 三类型×五点是受控通路测试。正式死城恢复原单胖子与原生Stage数量，不加入diag_angela／diag_yixing；本包不分发三类型诊断Stage或地图成品。每点到任一基地即可，失败尝试和其他基地可达性限制见新说明。

Boss原生普通近战基础30，按普通怪既有天数倍率增长；一次性原生近战凭据用于区分技能。Boss建筑攻击120与技能原值保留。冷启动同一游戏时刻的重复定位回调不扣预算，原5游戏秒／50有效尝试上限保持。
前轮全部Boss胜利规则继续保留；死城交付为单胖子，不能把1／1后的实际换图、多人战斗或长期续局视为本增量包完整性检查已经验收。

包不会自动启停、换图、重载、创建或复活NPC。安装与生效由服主安排；原始运行报告留在仓库，不随最小增量包分发。旧三份ZIP与冻结说明保留，新包自身SHA256只写外部报告，不回写随包说明。
"""


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def relative(path):
    return path.resolve().relative_to(ROOT).as_posix()


def within(path, boundary):
    require(path.resolve() != boundary.resolve() and path.resolve().is_relative_to(boundary.resolve()), "Unsafe output boundary: " + str(path))


def code_files():
    scripting = ROOT / "addons/sourcemod/scripting"
    return {relative(path): path for path in [scripting / "HanZombieShelter.sp",
            *sorted((scripting / "HanZombieShelter").glob("*.inc")),
            scripting / "include/HanZombieScenarioAPI.inc", scripting / "include/HanZombieShelterAPI.inc"]}


def migration_module(raw):
    tree = ast.parse(raw.decode("utf-8-sig"))
    imports = {node.module.split(".")[0] for node in ast.walk(tree) if isinstance(node, ast.ImportFrom) and node.module}
    imports.update(alias.name.split(".")[0] for node in ast.walk(tree) if isinstance(node, ast.Import) for alias in node.names)
    require(imports <= sys.stdlib_module_names, "Bundled migration must use only Python standard-library imports")
    verification = read_json(REPORT / "migration_tool_verification.json")
    require(verification["tool_sha256"] == digest(raw) and verification["unit_tests_failed"] == 0, "Migration source differs from its verified standalone tool")
    name = "v082_damage_package_migration"
    spec = importlib.util.spec_from_file_location(name, ROOT / MIGRATION)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module, sorted(imports)


def point_fragment(module, dry_run):
    staged = ROUTES / "user_captured_points.cfg"
    maintained = POINT_SOURCE.is_file()
    require(maintained or dry_run, "Final maintained deadcity point fragment is not yet available")
    source = POINT_SOURCE if maintained else staged
    blob = source.read_bytes()
    require(blob == staged.read_bytes(), "Deadcity point fragment must preserve exact captured bytes")
    preparation = read_json(ROUTES / "candidate_preparation.json")
    require(digest(blob) == preparation["sources"]["user_points"]["sha256"], "Deadcity point identity differs from recorded user selections")
    tree = module.parse(blob)
    require(list(tree) == ["BossSpawnPoints"], "Point fragment must contain one BossSpawnPoints root")
    points = tree["BossSpawnPoints"]
    require(set(points) == {f"boss{i}" for i in range(1, 6)}, "Five named point slots are required")
    for row in points.values():
        require(set(row) == {"origin", "angles"}, "Each point must preserve origin and angles only")
        for field, bound in [("origin", 15000.0), ("angles", 360.0)]:
            values = [float(part) for part in row[field].value.split()]
            require(len(values) == 3 and all(math.isfinite(value) and abs(value) <= bound for value in values), "Invalid point vector")
    return source, {"map": "zsh_deadcity_han_dn", "point_count": 5, "leaf_count": 10,
                    "sha256": digest(blob), "maintained_fragment_available": maintained,
                    "staging_only": not maintained, "coordinates_adjusted": False,
                    "formal_boss_count": 1, "temporary_three_type_stage_included": False,
                    "route_acceptance_claimed_by_packaging": False}


def collect(expected_smx=None, dry_run=False):
    build = read_json(RECEIPT)
    formal = FORMAL.read_bytes()
    formal_sha = digest(formal)
    require(build.get("diagnostic") is False and formal[:4] == b"FFPS" and build["sha256"] == formal_sha, "Formal SMX/receipt identity mismatch")
    require(formal_sha != SUPERSEDED_SMX, "8e007 is a superseded intermediate build and must not be packaged")
    if expected_smx is not None:
        require(formal_sha == expected_smx.lower(), "Formal build differs from the explicit frozen identity")
    log = BUILD_LOG.read_text(encoding="utf-8-sig")
    require(log == build["log"] and "SourcePawn Compiler 1.12.0.7250" in log and "Code size:" in log
            and not re.search(r"\b(?:warning|error)\s+\d+", log, re.I), "Formal compiler log must match the clean recorded build")
    files = code_files()
    code = {name: path.read_bytes() for name, path in files.items()}
    require(len(code) == 52 and {name: digest(blob) for name, blob in code.items()} == build["production_inputs"], "52 production-source snapshots differ from the final build receipt")
    require((ROOT / PLUGIN).read_bytes() == formal, "Repository SMX is not the final formal build")
    baseline = {row["path"]: row["sha256"] for row in read_json(ROOT / "tests/reports/v082_boss_routes_20261025/baseline.json")["repo_files"]}
    for name, blob in code.items():
        if "/include/" in name:
            require(baseline.get(name) == digest(blob), "Public/native API changed: " + name)
    migration_raw = (ROOT / MIGRATION).read_bytes()
    module, imports = migration_module(migration_raw)
    points_source, points = point_fragment(module, dry_run)
    inputs = {name: (path, "production_code") for name, path in files.items()}
    inputs.update({PLUGIN: (FORMAL, "formal_main_smx"), MIGRATION: (ROOT / MIGRATION, "standalone_three_leaf_migration"),
                   DOC: (ROOT / DOC, "current_documentation"), POINT_DESTINATION: (points_source, "deadcity_point_fragment")})
    payload = {name: path.read_bytes() for name, (path, _) in inputs.items()}
    require({name: payload[name] for name in code} == code and payload[MIGRATION] == migration_raw, "Source changed during collection")
    document = payload[DOC].decode("utf-8-sig")
    frozen = re.search(r"(?m)^文档状态：已冻结\s*$", document) is not None
    require(not frozen or "文档状态：草稿" not in document, "Documentation has conflicting draft/frozen declarations")
    payload[INSTALL_NAME] = INSTALL_TEXT.encode("utf-8")
    rows = {name: {"sha256": digest(blob), "bytes": len(blob),
                  "origin": relative(inputs[name][0]) if name in inputs else "generated_installation_notes",
                  "role": inputs[name][1] if name in inputs else "installation_notes"}
            for name, blob in sorted(payload.items())}
    manifest = {"version": "0.8.2", "revision": "boss_damage_and_deadcity_points", "formal_sha256": formal_sha,
                "formal_build_receipt_sha256": digest(RECEIPT.read_bytes()), "formal_compiler_log_sha256": digest(BUILD_LOG.read_bytes()),
                "production_input_count": len(code), "production_inputs": {name: rows[name] for name in sorted(code)},
                "documentation_frozen": frozen, "documentation_sha256": digest(payload[DOC]),
                "point_fragment": points, "migration_standard_library_imports": imports, "additional_parser_dependency": False,
                "whole_external_hzs_configuration_included": False, "temporary_diagnostic_stage_or_plugins_included": False,
                "requires_existing_complete_v082_environment": True, "runtime_acceptance_claimed_by_packaging": False,
                "file_count_including_manifest": len(payload) + 1, "manifest_self_hash_recorded_externally": True, "files": rows}
    payload["manifest.json"] = encoded(manifest)
    require(len(payload) == 58, "Expected the approved 58-member minimum increment")
    return payload, manifest, inputs


def archive_bytes(payload):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, blob in sorted(payload.items()):
            info = zipfile.ZipInfo(name, (2026, 10, 25, 0, 0, 0))
            info.compress_type, info.external_attr = zipfile.ZIP_DEFLATED, 0o100644 << 16
            archive.writestr(info, blob, compresslevel=9)
    return output.getvalue()


def verify(blob, payload):
    checks = []
    def check(name, passed):
        checks.append({"name": name, "passed": passed})
        require(passed, "Archive verification failed: " + name)
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        names = archive.namelist()
        check("exact 58 approved members without duplicates", len(names) == len(set(names)) == 58 and set(names) == set(payload))
        check("safe member paths", all(not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts and "\\" not in name for name in names))
        check("CRC verification", archive.testzip() is None)
        check("only formal main SMX included", [name for name in names if name.endswith(".smx")] == [PLUGIN])
        check("only maintained-map fragment is a CFG", [name for name in names if name.endswith(".cfg")] == [POINT_DESTINATION])
        forbidden = ("diagnostic", "probe", "python_deps", "optional-plugins", "gamedata", "testhelper", "HanZombieScenarioZombieData.cfg", "HanZombieScenarioStage.cfg")
        check("no diagnostics external configs resources or patches", not any(word.lower() in name.lower() for name in names for word in forbidden)
              and not any(PurePosixPath(name).parts[0] in {"models", "materials", "sound", "maps"} for name in names))
        for name, expected in sorted(payload.items()):
            actual = archive.read(name)
            check("exact bytes size and SHA256: " + name, actual == expected and len(actual) == len(expected) and digest(actual) == digest(expected))
    check("identical payload reproduces exact ZIP bytes", archive_bytes(payload) == blob)
    return checks


def atomic_write(path, blob, boundary):
    within(path, boundary)
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_name(path.name + ".new-" + digest(blob)[:16])
    within(pending, boundary)
    if pending.exists():
        require(pending.read_bytes() == blob, "Conflicting pending artifact")
    else:
        with pending.open("xb") as handle:
            handle.write(blob)
    pending.replace(path)


def preserve_write(path, blob, boundary, history_dir):
    within(path, boundary)
    previous = None
    if path.exists() and path.read_bytes() != blob:
        old = path.read_bytes()
        history = history_dir / (path.stem + "_" + digest(old) + path.suffix)
        within(history, boundary)
        if history.exists():
            require(history.read_bytes() == old, "Conflicting artifact history")
        else:
            atomic_write(history, old, boundary)
        previous = {"path": relative(history), "sha256": digest(old), "bytes": len(old)}
    if not path.exists() or path.read_bytes() != blob:
        atomic_write(path, blob, boundary)
    require(path.read_bytes() == blob, "Written artifact differs from verified bytes")
    return previous


def package(expected_smx=None, dry_run=False):
    protected = {relative(path): digest(path.read_bytes()) for path in OLD_PACKAGES}
    payload, manifest, inputs = collect(expected_smx, dry_run)
    blob = archive_bytes(payload)
    checks = verify(blob, payload)
    for name, (path, _) in inputs.items():
        require(path.read_bytes() == payload[name], "Input changed during packaging: " + name)
    require(all(digest((ROOT / name).read_bytes()) == identity for name, identity in protected.items()), "A protected previous ZIP changed")
    result = {"dry_run": dry_run, "zip": {"path": relative(ARCHIVE), "sha256": digest(blob), "bytes": len(blob)},
              "file_count": len(payload), "checks_passed": len(checks), "checks_failed": 0,
              "formal_sha256": manifest["formal_sha256"], "production_input_count": len(manifest["production_inputs"]),
              "documentation_frozen": manifest["documentation_frozen"], "point_fragment": manifest["point_fragment"],
              "old_zip_hashes_preserved": protected, "live_or_production_modified": False,
              "runtime_acceptance_claimed_by_packaging": False}
    if dry_run:
        return result
    require(manifest["documentation_frozen"], "Refusing draft documentation; wait for root's final facts and explicit freeze")
    require(not manifest["point_fragment"]["staging_only"], "Refusing staged point data in final package")
    previous = preserve_write(ARCHIVE, blob, DIST, DIST / "history/v0.8.2-boss-damage-deadcity")
    stamp = datetime.now(timezone.utc).isoformat()
    receipt = {**result, "at": stamp, "passed": True, "previous_same_revision_preserved": previous,
               "manifest_sha256": digest(payload["manifest.json"]), "production_inputs": manifest["production_inputs"],
               "files": {name: {"sha256": digest(data), "bytes": len(data)} for name, data in sorted(payload.items())}}
    verification = {**result, "at": stamp, "passed": True, "checks": checks, "manifest_sha256": digest(payload["manifest.json"])}
    for path, data in [(REPORT / "package_manifest.json", encoded(receipt)),
                       (REPORT / "package_verification.json", encoded(verification)),
                       (REPORT / "package_zip_sha256.txt", (result["zip"]["sha256"] + "  " + ARCHIVE.name + "\n").encode("utf-8"))]:
        preserve_write(path, data, REPORT, REPORT / "history/packaging")
    require(all(digest((ROOT / name).read_bytes()) == identity for name, identity in protected.items()), "Protected ZIP changed after packaging")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-smx-sha256", help="Require root's explicit final frozen formal identity")
    parser.add_argument("--dry-run", action="store_true", help="Verify in memory only, permitting recorded point staging and draft docs")
    args = parser.parse_args()
    if args.expected_smx_sha256 and not re.fullmatch(r"[0-9a-fA-F]{64}", args.expected_smx_sha256):
        parser.error("--expected-smx-sha256 must be exactly 64 hexadecimal characters")
    if not args.dry_run and not args.expected_smx_sha256:
        parser.error("Final writes require --expected-smx-sha256 from root's freeze")
    print(json.dumps(package(args.expected_smx_sha256, args.dry_run), ensure_ascii=False, indent=2))
