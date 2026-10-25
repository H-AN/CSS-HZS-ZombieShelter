"""Package the frozen v0.8.2 Boss route/round revision, without building or deploying.

Only the new ZIP and this session's packaging reports may be written. Existing
crafting/Boss-point packages, production files and live servers are untouched.
Use --dry-run while documentation is being finalized; it writes no ZIP/report.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timedelta, timezone
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
REPORT = ROOT / "tests/reports/v082_boss_routes_20261025"
OLD_REPORT = ROOT / "tests/reports/v082_boss_spawnpoints_20261025"
DIST = ROOT / "dist"
ARCHIVE = DIST / "CSS-HZS-ZombieShelter-v0.8.2-boss-routes-update.zip"
FORMAL = REPORT / "build/HanZombieShelter.smx"
BUILD_RECEIPT = REPORT / "formal_build.json"
BUILD_LOG = REPORT / "build/HanZombieShelter_compile.log"
EXPECTED_FORMAL = "374c50f3aabef6eeac1bc7b9a5da69e76477bbfad0f48605c9aebbf2e9e00276"
PLUGIN = "addons/sourcemod/plugins/HanZombieShelter.smx"
ROUTE_DOC = "docs/v0.8.2_Boss点位联测与回合规则.md"
HISTORICAL_DOC = "docs/v0.8.2_Boss坐标点配置.md"
POINT_SOURCE = "server-support/map-point-updates/zsh_desert_han_dn/BossSpawnPoints.cfg"
POINT_DESTINATION = "map_point_updates/zsh_desert_han_dn/BossSpawnPoints.cfg"
INSTALL_NAME = "README_v0.8.2_Boss路线与回合修订.txt"
OLD_ARCHIVES = (
    DIST / "CSS-HZS-ZombieShelter-v0.8.2-crafting-update.zip",
    DIST / "CSS-HZS-ZombieShelter-v0.8.2-boss-spawnpoints-update.zip",
)
DOC_DRAFT_MARKERS = ("交接草稿", "尚待收尾证据", "等待主代理的最终回执")

INSTALL_TEXT = """v0.8.2 Boss路线与回合规则增量修订

本包依赖已有完整v0.8.2昼夜求生环境及原有HZS、外部武器系统和地图资源。
先读docs/v0.8.2_Boss点位联测与回合规则.md。本包只更新正式主SMX、对应源码与说明，不是全新服务器安装包。

安装与配置合并：
1. 备份现有主插件和地图配置，由服主安排维护时替换addons/sourcemod/plugins/HanZombieShelter.smx。
2. map_point_updates/zsh_desert_han_dn/BossSpawnPoints.cfg仅供当前本地DN沙漠图使用。把其中BossSpawnPoints块合并到该图ShelterMap根下；已有同名块时逐叶核对／更新5个origin和5个angles，不创建重复块。它不是完整地图配置，也不是可直接覆盖的addons配置文件。
3. P1、P2是用户主动选择的两个铁块平台点，保留原XY/Z和朝向，不下降到NAV地板。该点表不能套到zsh_desertplant_cso_v1_d或其他地图。
4. map_config_candidates保留前轮7份来源不同的旧迁移候选与重复占位。其他地图没有本轮路线验收；候选local/release/repo各自的Boss定义与配置继续保留，不整份覆盖现场，也不要用旧DN候选替换本次DN点表。
5. 每个有效启用Boss定义都属于Boss击杀胜利目标，旧required_boss_ids不再筛选必杀对象。所有启用Boss死亡才满足Boss胜利；按生存天数救援仍是独立规则。原生Stage同key实例数仍必须与启用定义数精确匹配；本包不替换Stage或HZS配置。
6. 现存活Boss在自然新引擎回合重新绑定并休眠，保留已有伤害和死亡进度。首次定位Hull检查失败会在有限窗口内重试；仍使用原点和真实Hull，不新建或复活NPC。

验证范围：
新说明记录本地图3种Boss各5点、每组合至少到达一个基地的受控真实追击结果，同时保留到另一基地的超时尝试。不能据此宣称任意基地均可达、其他地图已验证或多人战斗／负载验收完成。旧docs/v0.8.2_Boss坐标点配置.md为冻结的前轮说明；其中required_boss_ids、未选新点／未部署等时态与语义以本次新说明为准。

随包manifest.json记录52份生产输入、正式SMX与全部文件的SHA256／尺寸。包完整性核验不单独产生运行验收结论；详细原始测试报告在仓库中查阅，不随此最小增量包分发。
本包不含诊断／采点／测试助手、HZS主体、外部武器源码或资源、新模型下载、gamedata、可选引擎补丁，也不会启停、重载或换图。
"""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def relative(path: Path) -> str:
    return path.resolve().relative_to(ROOT).as_posix()


def require_under(path: Path, boundary: Path) -> None:
    if path.resolve() == boundary.resolve() or not path.resolve().is_relative_to(boundary.resolve()):
        raise ValueError(f"Output outside the intended directory: {path}")


def fail_unless(condition: bool, reason: str) -> None:
    if not condition:
        raise ValueError(reason)


def source_files() -> dict[str, Path]:
    scripting = ROOT / "addons/sourcemod/scripting"
    files = [scripting / "HanZombieShelter.sp", *sorted((scripting / "HanZombieShelter").glob("*.inc")),
             scripting / "include/HanZombieScenarioAPI.inc", scripting / "include/HanZombieShelterAPI.inc"]
    return {relative(path): path for path in files}


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def parse_kv(blob: bytes):
    # Only the existing syntax parser is reused. It has no server dependencies.
    name = "v082_routes_package_keyvalues"
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, ROOT / "tools/migrate_v05_materials.py")
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name].parse(blob.decode("utf-8-sig"))


def validate_points(blob: bytes) -> dict:
    document = parse_kv(blob)
    expected = read_json(REPORT / "selected_points.json")
    fail_unless(document == expected, "Maintained point fragment differs from the user's exact selected coordinates/angles")
    points = document.get("BossSpawnPoints", {})
    fail_unless(set(points) == {f"boss{i}" for i in range(1, 6)}, "Point fragment must contain exactly boss1..boss5")
    for row in points.values():
        fail_unless(set(row) == {"origin", "angles"}, "Each maintained point must contain exactly origin and angles")
        for field, bound in [("origin", 15000.0), ("angles", 360.0)]:
            values = [float(token) for token in row[field].split()]
            fail_unless(len(values) == 3 and all(math.isfinite(value) and abs(value) <= bound for value in values), "Invalid maintained point vector")
    captured = (REPORT / "user_captured_points.cfg").read_bytes()
    fail_unless(blob == captured, "Maintained point fragment must preserve the complete captured file bytes")
    deployment = read_json(REPORT / "map_points_deployment.json")
    fail_unless(deployment["captured_points_sha256"] == digest(blob), "Point deployment receipt refers to a different captured file")
    fail_unless(deployment["only_new_map_point_leaves"] and deployment["candidate"]["added_leaves"] == 10
                and deployment["candidate"]["changed_old_leaves"] == 0 and deployment["candidate"]["removed_old_leaves"] == 0,
                "Recorded DN point migration must preserve all existing map leaves")
    return {"map": "zsh_desert_han_dn", "point_count": 5, "leaf_count": 10,
            "captured_sha256": digest(blob), "coordinates_adjusted": False,
            "recorded_local_map_output_sha256": deployment["source_after"],
            "deployment_receipt_sha256": digest((REPORT / "map_points_deployment.json").read_bytes()),
            "cross_map_use_allowed": False, "other_maps_route_acceptance_claimed": False}


def collect(expected_smx: str) -> tuple[dict[str, bytes], dict, dict[str, tuple[Path, str]]]:
    receipt = read_json(BUILD_RECEIPT)
    code_files = source_files()
    code = {name: path.read_bytes() for name, path in code_files.items()}
    fail_unless(len(code) == 52, "Expected exactly 52 final production inputs")
    fail_unless({name: digest(blob) for name, blob in code.items()} == receipt["production_inputs"], "Production inputs differ from the formal build receipt")
    fail_unless(receipt.get("diagnostic") is False, "Build receipt must explicitly identify a formal build")
    log = BUILD_LOG.read_text(encoding="utf-8-sig")
    fail_unless(log == receipt["log"] and "SourcePawn Compiler 1.12.0.7250" in log and "Code size:" in log
                and not re.search(r"\b(?:warning|error)\s+\d+", log, re.IGNORECASE), "Formal compiler log must match the clean recorded 1.12.0.7250 build")
    formal = FORMAL.read_bytes()
    fail_unless(formal[:4] == b"FFPS" and digest(formal) == expected_smx.lower() == receipt["sha256"].lower(), "Formal SMX identity mismatch")
    fail_unless((ROOT / PLUGIN).read_bytes() == formal, "Repository formal main SMX differs from the recorded formal build")
    baseline = {row["path"]: row["sha256"] for row in read_json(REPORT / "baseline.json")["repo_files"]}
    for name, blob in code.items():
        if "/include/" in name:
            fail_unless(baseline.get(name) == digest(blob), f"API reference changed from this session's baseline: {name}")
    inputs = {name: (path, "production_code") for name, path in code_files.items()}
    inputs[PLUGIN] = (FORMAL, "formal_main_smx")
    inputs[ROUTE_DOC] = (ROOT / ROUTE_DOC, "current_route_and_round_rules")
    inputs[HISTORICAL_DOC] = (ROOT / HISTORICAL_DOC, "historical_coordinate_reference")
    inputs[POINT_DESTINATION] = (ROOT / POINT_SOURCE, "current_dn_point_fragment")
    for name in ("migrate_v082_boss_spawn_points.py", "migrate_v05_materials.py"):
        inputs["tools/" + name] = (ROOT / "tools" / name, "migration_tool")
    previous_manifest = read_json(OLD_REPORT / "package_manifest.json")
    candidates = sorted((OLD_REPORT / "candidate_maps").glob("*/*.cfg"))
    fail_unless(len(candidates) == 7, "Expected the seven unchanged historical map candidates")
    for path in candidates:
        name = "map_config_candidates/" + path.relative_to(OLD_REPORT / "candidate_maps").as_posix()
        fail_unless(previous_manifest["files"][name]["sha256"] == digest(path.read_bytes()), f"Historical candidate changed: {name}")
        inputs[name] = (path, "historical_unverified_map_candidate")
    payload = {name: path.read_bytes() for name, (path, _) in inputs.items()}
    fail_unless({name: payload[name] for name in code} == code, "Production input changed during collection")
    points = validate_points(payload[POINT_DESTINATION])
    doc_text = payload[ROUTE_DOC].decode("utf-8-sig")
    documentation_ready = not any(marker in doc_text for marker in DOC_DRAFT_MARKERS)
    payload[INSTALL_NAME] = INSTALL_TEXT.encode("utf-8")
    rows = {name: {"sha256": digest(blob), "bytes": len(blob),
                   "origin": relative(inputs[name][0]) if name in inputs else "generated_installation_notes",
                   "role": inputs[name][1] if name in inputs else "installation_notes"}
            for name, blob in sorted(payload.items())}
    manifest = {"version": "0.8.2", "revision": "boss_routes_and_round_rules", "package_kind": "incremental_update",
                "formal_sha256": digest(formal), "formal_build_receipt_sha256": digest(BUILD_RECEIPT.read_bytes()),
                "formal_compiler_log_sha256": digest(BUILD_LOG.read_bytes()), "production_input_count": len(code),
                "production_inputs": {name: rows[name] for name in sorted(code)},
                "documentation_ready": documentation_ready, "current_documentation": ROUTE_DOC,
                "historical_documentation_semantics_superseded_by": ROUTE_DOC,
                "point_fragment": points, "historical_candidates_unchanged_from_previous_package": True,
                "runtime_acceptance_claimed_by_packaging": False, "automatic_deployment": False,
                "diagnostics_or_external_resources_included": False, "requires_existing_complete_v082_environment": True,
                "file_count_including_manifest": len(payload) + 1, "manifest_self_hash_recorded_externally": True,
                "files": rows}
    payload["manifest.json"] = json_bytes(manifest)
    fail_unless(len(payload) == 67, "Expected the approved 67-member minimal increment")
    return payload, manifest, inputs


def zip_bytes(payload: dict[str, bytes]) -> bytes:
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, blob in sorted(payload.items()):
            info = zipfile.ZipInfo(name, (2026, 10, 25, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, blob, compresslevel=9)
    return stream.getvalue()


def verify(data: bytes, payload: dict[str, bytes]) -> list[dict]:
    checks = []

    def check(name: str, passed: bool) -> None:
        checks.append({"name": name, "passed": passed})
        fail_unless(passed, "ZIP verification failed: " + name)

    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = archive.namelist()
        check("exact approved member set without duplicates", len(names) == len(set(names)) == 67 and set(names) == set(payload))
        check("safe relative archive paths", all(not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts and "\\" not in name for name in names))
        check("CRC verification", archive.testzip() is None)
        check("only formal main SMX included", [name for name in names if name.lower().endswith(".smx")] == [PLUGIN])
        forbidden = ("diagnostic", "probe", "testhelper", "python_deps", "optional-plugins", "gamedata", "extensions", "test_v082", "server_control")
        check("no diagnostic helpers optional patches or engine dependencies", not any(word in name.lower() for name in names for word in forbidden))
        check("no external models materials sounds maps or HZS binaries", not any(PurePosixPath(name).parts[0] in {"models", "materials", "sound", "maps"} for name in names))
        check("no whole现场 configuration deployment tree", not any(name.startswith("addons/sourcemod/configs/") for name in names))
        for name, expected in sorted(payload.items()):
            actual = archive.read(name)
            check("bytes size and SHA256: " + name, actual == expected and len(actual) == len(expected) and digest(actual) == digest(expected))
    return checks


def atomic_write(path: Path, blob: bytes, boundary: Path) -> None:
    require_under(path, boundary)
    path.parent.mkdir(parents=True, exist_ok=True)
    candidate = path.with_name(path.name + ".new-" + digest(blob)[:16])
    require_under(candidate, boundary)
    if candidate.exists():
        fail_unless(candidate.read_bytes() == blob, "Conflicting pending output: " + str(candidate))
    else:
        with candidate.open("xb") as handle:
            handle.write(blob)
    candidate.replace(path)


def preserve_write(path: Path, blob: bytes, boundary: Path, history_dir: Path) -> dict | None:
    require_under(path, boundary)
    previous = None
    if path.exists() and path.read_bytes() != blob:
        old = path.read_bytes()
        history = history_dir / f"{path.stem}_{digest(old)}{path.suffix}"
        require_under(history, boundary)
        if history.exists():
            fail_unless(history.read_bytes() == old, "Conflicting previous output history")
        else:
            atomic_write(history, old, boundary)
        previous = {"path": relative(history), "sha256": digest(old), "bytes": len(old)}
    if not path.exists() or path.read_bytes() != blob:
        atomic_write(path, blob, boundary)
    fail_unless(path.read_bytes() == blob, "Written output differs from verified bytes")
    return previous


def package(expected_smx: str = EXPECTED_FORMAL, dry_run: bool = False) -> dict:
    protected = {relative(path): digest(path.read_bytes()) for path in OLD_ARCHIVES}
    payload, manifest, inputs = collect(expected_smx)
    data = zip_bytes(payload)
    checks = verify(data, payload)
    deterministic = zip_bytes(payload) == data
    fail_unless(deterministic, "Identical payload did not reproduce the exact ZIP bytes")
    checks.append({"name": "identical payload reproduces exact ZIP bytes", "passed": deterministic})
    for name, (path, _) in inputs.items():
        fail_unless(path.read_bytes() == payload[name], "Input changed while packaging: " + name)
    identity = {"path": relative(ARCHIVE), "sha256": digest(data), "bytes": len(data)}
    result = {"dry_run": dry_run, "zip": identity, "file_count": len(payload), "checks_passed": len(checks),
              "checks_failed": 0, "formal_sha256": manifest["formal_sha256"],
              "production_input_count": manifest["production_input_count"], "documentation_ready": manifest["documentation_ready"],
              "old_zip_hashes_preserved": protected, "runtime_acceptance_claimed_by_packaging": False,
              "live_environment_modified": False, "production_files_modified": False}
    fail_unless(all(digest((ROOT / name).read_bytes()) == sha for name, sha in protected.items()), "Protected old package changed")
    if dry_run:
        return result
    fail_unless(manifest["documentation_ready"], "Route documentation is still a draft; only --dry-run is permitted until it is finalized")
    previous = preserve_write(ARCHIVE, data, DIST, DIST / "history/v0.8.2-boss-routes")
    stamp = datetime.now(timezone(timedelta(hours=8))).isoformat()
    receipt = {**result, "at": stamp, "passed": True, "previous_new_revision_preserved": previous,
               "manifest_sha256": digest(payload["manifest.json"]), "production_inputs": manifest["production_inputs"],
               "files": {name: {"sha256": digest(blob), "bytes": len(blob)} for name, blob in sorted(payload.items())}}
    verification = {**result, "at": stamp, "passed": True, "checks": checks, "manifest_sha256": digest(payload["manifest.json"])}
    preserve_write(REPORT / "package_manifest.json", json_bytes(receipt), REPORT, REPORT / "history/packaging")
    preserve_write(REPORT / "package_verification.json", json_bytes(verification), REPORT, REPORT / "history/packaging")
    preserve_write(REPORT / "package_zip_sha256.txt", (identity["sha256"] + "  " + ARCHIVE.name + "\n").encode("utf-8"), REPORT, REPORT / "history/packaging")
    fail_unless(all(digest((ROOT / name).read_bytes()) == sha for name, sha in protected.items()), "Protected old package changed after output")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-smx-sha256", default=EXPECTED_FORMAL, help="Require this exact formal SMX identity")
    parser.add_argument("--dry-run", action="store_true", help="Validate in memory; write no ZIP or packaging report")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", args.expected_smx_sha256):
        parser.error("--expected-smx-sha256 must have exactly 64 hexadecimal characters")
    print(json.dumps(package(args.expected_smx_sha256, args.dry_run), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
