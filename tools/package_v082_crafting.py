"""Package the formal v0.8.2 crafting increment without touching a live server.

The allow-list is deliberate: the build directory also contains diagnostics,
and copying that directory or the complete addons tree would ship test plugins
or overwrite a server owner's existing configuration.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import zipfile


ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "tests/reports/v082_crafting_20261025"
DIST = ROOT / "dist"
ARCHIVE = DIST / "CSS-HZS-ZombieShelter-v0.8.2-crafting-update.zip"
FORMAL = REPORT / "build/HanZombieShelter.smx"
BUILD_LOG = REPORT / "build/HanZombieShelter_compile.log"
SOURCE = ROOT / "addons/sourcemod/scripting"
PLUGIN_PATH = "addons/sourcemod/plugins/HanZombieShelter.smx"
CONFIG_NAMES = ("workbenches.cfg", "weapon_crafting.cfg", "material_crafting.cfg", "weapons.cfg")
API_NAMES = ("HanZombieScenarioAPI.inc", "HanZombieShelterAPI.inc")
DOC_PATH = "docs/v0.8.2_合成台配置说明.md"
INSTALL_NAME = "README_v0.8.2_安装与配置合并.md"


INSTALL_TEXT = """# v0.8.2 合成台增量包

本包依赖已有的 v0.8.0 完整昼夜求生环境；不是全新服务器安装包。已有 v0.8.1 本地兼容插件与资源按现场保留。合成台使用 CS:S 内置模型 `models/props/de_piranesi/pi_sundial.mdl`，不新增模型、材质或音效下载。

## 安装与合并

1. 先备份现有主 SMX 和相关配置。将本包 `addons/sourcemod/plugins/HanZombieShelter.smx` 作为主插件更新文件；源码和两份 API include 供审阅及编译，不需要安装其他来源仓库源码。
2. 新增 `workbenches.cfg`、`weapon_crafting.cfg`、`material_crafting.cfg` 到 `addons/sourcemod/configs/HanZombieShelter`。如果已经有现场同名文件，逐项合并并保留服主自定义值。
3. 本包 `weapons.cfg` 是工效配置参考。请手动合并十把刀的维修／施工 HP 等相关条目，保留现场既有武器及服主调整，不要把整份发行配置覆盖到测试服或正式服。
4. 武器和材料合成配方、合成台建造／升级费用均可独立编辑。新增配方也应按字段合并；不要整套覆盖 `mode.cfg`、`days.cfg`、`skills.cfg` 或其他现场配置。
5. 外部 HWS、刀插件、它们注册的武器指令及其资源继续由原系统提供。本包不包含外部武器系统、模型资源、HZS 主体 SMX、gamedata 或可选引擎补丁；请保留已有依赖。
6. 本包不自动部署、不启停服务器或客户端、不发送换图命令。配置沿用现有按会话加载规则；由服主安排正常维护后启用。

## 功能与配置入口

只有近距离瞄准已完工合成台按 E，才进入武器合成／材料合成；不增加基地或快捷菜单入口，不接入 B 购买菜单，也不修改 `mp_buytime`。所有合成费用只扣玩家个人背包。

武器配方显式 `required_station_type` 时按合成台 `Types` 组名精确匹配；省略时绑定配置中第一个可用合成台类型。材料配方在任意可用合成台开放。武器实际发放仍由配置指令对应的外部系统负责，指令派发不等于收到了外部发放成功回执。

合成台最终供电需求为 Lv.1 的 50，每级增加 30，Lv.10 为 320。缺电时仍可查看配方和管理设施，但不能扣材料进行合成。详细字段、配方示例与合并边界见 `docs/v0.8.2_合成台配置说明.md`。

## 文件核验

`manifest.json` 记录正式 SMX 身份、全部随包文件的 SHA256／尺寸和生产代码来源列表。manifest 自身及 ZIP 的哈希另记录在打包工具生成的外部报告中。

本包的内容与哈希核验只说明打包完整性，不单独声明真人操作、最终供电运行或多人玩法已通过验收；实际运行结论以独立交付报告为准。
"""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def relative(path: Path) -> str:
    return path.resolve().relative_to(ROOT).as_posix()


def require_under(path: Path, parent: Path) -> None:
    resolved, boundary = path.resolve(), parent.resolve()
    if resolved == boundary or not resolved.is_relative_to(boundary):
        raise ValueError(f"Output path is outside the intended directory: {path}")


def input_files() -> dict[str, tuple[Path, str]]:
    files: dict[str, tuple[Path, str]] = {
        PLUGIN_PATH: (FORMAL, "formal_plugin"),
        "addons/sourcemod/scripting/HanZombieShelter.sp": (SOURCE / "HanZombieShelter.sp", "main_source"),
        "addons/sourcemod/translations/HanZombieShelter.phrases.txt":
            (ROOT / "addons/sourcemod/translations/HanZombieShelter.phrases.txt", "translation"),
        DOC_PATH: (ROOT / DOC_PATH, "documentation"),
    }
    for path in sorted((SOURCE / "HanZombieShelter").glob("*.inc")):
        files[relative(path)] = (path, "production_include")
    for name in API_NAMES:
        path = SOURCE / "include" / name
        files[relative(path)] = (path, "api_include")
    for name in CONFIG_NAMES:
        path = ROOT / "addons/sourcemod/configs/HanZombieShelter" / name
        files[relative(path)] = (path, "configuration")
    if not any(role == "production_include" for _, role in files.values()):
        raise ValueError("No production HanZombieShelter include files found")
    for name, (path, _) in files.items():
        if not path.is_file():
            raise FileNotFoundError(f"Required package input missing: {name} <- {path}")
    return files


def collect(expected_smx: str | None) -> tuple[dict[str, bytes], dict, dict[str, tuple[Path, str]]]:
    inputs = input_files()
    payload = {name: path.read_bytes() for name, (path, _) in inputs.items()}
    formal_sha = sha256(payload[PLUGIN_PATH])
    if expected_smx is not None and formal_sha != expected_smx.lower():
        raise ValueError(f"Formal SMX identity mismatch: expected {expected_smx}, found {formal_sha}")
    if payload[PLUGIN_PATH][:4] != b"FFPS":
        raise ValueError("Formal plugin does not have the SourcePawn SMX file signature")
    version_source = payload["addons/sourcemod/scripting/HanZombieShelter/Types.inc"].decode("utf-8-sig")
    match = re.search(r'^#define\s+SHELTER_VERSION\s+"([^"]+)"', version_source, re.MULTILINE)
    if match is None or match.group(1) != "0.8.2":
        raise ValueError("Production SHELTER_VERSION must be 0.8.2")
    compile_log = BUILD_LOG.read_text(encoding="utf-8-sig")
    if "SourcePawn Compiler 1.12.0.7250" not in compile_log or re.search(r"\b(?:error|warning)\s+\d+\b", compile_log, re.IGNORECASE):
        raise ValueError("Expected clean formal compiler log for SourcePawn 1.12.0.7250")
    if "Code size:" not in compile_log:
        raise ValueError("Formal compiler log lacks a successful output summary")

    code_roles = {"main_source", "production_include", "api_include"}
    code_inputs = [name for name, (_, role) in inputs.items() if role in code_roles]
    newer = [name for name in code_inputs if inputs[name][0].stat().st_mtime_ns > FORMAL.stat().st_mtime_ns]
    if newer:
        raise ValueError("Production source is newer than the formal SMX; recompile first: " + ", ".join(newer))

    # These include files are reference-only. Refuse to conceal an API change
    # if the baseline recorded by this implementation session is available.
    baseline_path = REPORT / "baseline_hashes.json"
    api_checks = []
    if baseline_path.is_file():
        baseline = {entry["path"]: entry["sha256"] for entry in json.loads(baseline_path.read_text(encoding="utf-8-sig"))}
        for name, (_, role) in inputs.items():
            if role != "api_include":
                continue
            expected = baseline.get(name)
            if expected is None or expected != sha256(payload[name]):
                raise ValueError(f"API reference differs from this session's baseline: {name}")
            api_checks.append({"path": name, "sha256": expected, "unchanged": True})

    payload[INSTALL_NAME] = INSTALL_TEXT.encode("utf-8")
    rows = {}
    for name, data in sorted(payload.items()):
        origin = relative(inputs[name][0]) if name in inputs else "generated_installation_notes"
        role = inputs[name][1] if name in inputs else "installation_notes"
        rows[name] = {"sha256": sha256(data), "size": len(data), "origin": origin, "role": role}
    manifest = {
        "schema_version": 1,
        "version": "0.8.2",
        "release_date": "2026-10-25",
        "package_kind": "incremental_crafting_update",
        "requires_existing_complete_environment": True,
        "formal_smx": rows[PLUGIN_PATH],
        "formal_compiler_log_sha256": sha256(BUILD_LOG.read_bytes()),
        "production_code_input_count": len(code_inputs),
        "production_code_inputs": [{"path": name, **rows[name]} for name in sorted(code_inputs)],
        "api_baseline_checks": api_checks,
        "test_plugins_included": False,
        "optional_engine_patches_included": False,
        "external_weapon_system_sources_or_resources_included": False,
        "new_model_downloads_required": False,
        "runtime_acceptance_claimed_by_packaging": False,
        "file_count_including_manifest": len(payload) + 1,
        "manifest_self_hash_recorded_externally": True,
        "files": rows,
    }
    payload["manifest.json"] = json_bytes(manifest)
    return payload, manifest, inputs


def zip_bytes(payload: dict[str, bytes]) -> bytes:
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(payload.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 25, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data, compresslevel=9)
    return stream.getvalue()


def verify_zip(data: bytes, payload: dict[str, bytes]) -> list[dict]:
    checks = []

    def check(name: str, passed: bool) -> None:
        checks.append({"name": name, "passed": passed})
        if not passed:
            raise ValueError(f"Package verification failed: {name}")

    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = archive.namelist()
        check("exact allow-listed member set", len(names) == len(set(names)) and set(names) == set(payload))
        check("archive CRC verification", archive.testzip() is None)
        check("safe relative member paths", all(not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts and "\\" not in name for name in names))
        check("only the formal main SMX", [name for name in names if name.lower().endswith(".smx")] == [PLUGIN_PATH])
        forbidden = ("diagnostic", "probe", "testhelper", "python_deps", "optional-plugins", "hzs_botaddfix", "followrange")
        check("no diagnostics dependencies or optional patches", not any(word in name.lower() for name in names for word in forbidden))
        cfg_names = {PurePosixPath(name).name for name in names if name.lower().endswith(".cfg")}
        check("exact four configuration files", cfg_names == set(CONFIG_NAMES))
        check("no bundled weapon resources or engine files", not any(PurePosixPath(name).parts[0] in {"models", "materials", "sound", "maps", "server-support"} or "/gamedata/" in name or "/extensions/" in name for name in names))
        for name in sorted(payload):
            check("size and SHA256: " + name, len(archive.read(name)) == len(payload[name]) and sha256(archive.read(name)) == sha256(payload[name]))
    return checks


def write_atomic(path: Path, data: bytes, boundary: Path) -> None:
    require_under(path, boundary)
    path.parent.mkdir(parents=True, exist_ok=True)
    candidate = path.with_name(path.name + ".new-" + sha256(data)[:16])
    require_under(candidate, boundary)
    if candidate.exists():
        if candidate.read_bytes() != data:
            raise ValueError(f"Conflicting candidate file already exists: {candidate}")
    else:
        with candidate.open("xb") as handle:
            handle.write(data)
    candidate.replace(path)


def preserve_report(path: Path, data: bytes) -> None:
    require_under(path, REPORT)
    if path.exists() and path.read_bytes() != data:
        old = path.read_bytes()
        history = REPORT / "history/packaging" / f"{path.stem}_{sha256(old)}{path.suffix}"
        require_under(history, REPORT)
        if history.exists() and history.read_bytes() != old:
            raise ValueError(f"Conflicting report history: {history}")
        if not history.exists():
            write_atomic(history, old, REPORT)
    write_atomic(path, data, REPORT)


def package(expected_smx: str | None = None, dry_run: bool = False) -> dict:
    payload, manifest, inputs = collect(expected_smx)
    data = zip_bytes(payload)
    checks = verify_zip(data, payload)
    # Catch edits during packaging instead of publishing a mixed source set.
    for name, (path, _) in inputs.items():
        if path.read_bytes() != payload[name]:
            raise ValueError(f"Input changed during packaging: {name}")
    identity = {"path": relative(ARCHIVE), "sha256": sha256(data), "size": len(data)}
    if dry_run:
        return {"dry_run": True, "zip": identity, "files": len(payload), "checks_passed": len(checks), "formal_smx_sha256": manifest["formal_smx"]["sha256"]}

    require_under(ARCHIVE, DIST)
    previous = None
    if ARCHIVE.exists() and ARCHIVE.read_bytes() != data:
        old = ARCHIVE.read_bytes()
        history = DIST / "history/v0.8.2-crafting" / f"{ARCHIVE.stem}_{sha256(old)}.zip"
        require_under(history, DIST)
        if history.exists():
            if history.read_bytes() != old:
                raise ValueError(f"Conflicting ZIP history: {history}")
        else:
            history.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ARCHIVE, history)
            if sha256(history.read_bytes()) != sha256(old):
                raise ValueError("Previous ZIP history hash verification failed")
        previous = {"path": relative(history), "sha256": sha256(old), "size": len(old)}
    if not ARCHIVE.exists() or ARCHIVE.read_bytes() != data:
        write_atomic(ARCHIVE, data, DIST)
    if ARCHIVE.read_bytes() != data:
        raise ValueError("Written ZIP identity differs from verified in-memory package")
    timestamp = datetime.now(timezone(timedelta(hours=8))).isoformat()
    receipt = {
        "generated_at": timestamp,
        "passed": True,
        "zip": identity,
        "previous_zip_preserved": previous,
        "files": [{"path": name, "sha256": sha256(blob), "size": len(blob)} for name, blob in sorted(payload.items())],
        "production_code_inputs": manifest["production_code_inputs"],
        "formal_smx": manifest["formal_smx"],
        "manifest_sha256": sha256(payload["manifest.json"]),
        "live_environment_modified": False,
        "repo_formal_smx_modified": False,
    }
    verification = {
        "generated_at": timestamp,
        "passed": True,
        "checks_passed": len(checks),
        "checks_failed": 0,
        "checks": checks,
        "zip": identity,
        "file_count": len(payload),
        "formal_smx_sha256": manifest["formal_smx"]["sha256"],
        "runtime_acceptance_claimed": False,
        "game_commands_sent": False,
        "live_environment_modified": False,
    }
    preserve_report(REPORT / "package_manifest.json", json_bytes(receipt))
    preserve_report(REPORT / "package_verification.json", json_bytes(verification))
    preserve_report(REPORT / "package_zip_sha256.txt", (identity["sha256"] + "  " + ARCHIVE.name + "\n").encode("utf-8"))
    return {"zip": identity, "files": len(payload), "checks_passed": len(checks), "previous_zip_preserved": previous}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-smx-sha256", help="Refuse packaging unless the formal SMX matches this exact identity")
    parser.add_argument("--dry-run", action="store_true", help="Verify the in-memory package without writing ZIP or reports")
    args = parser.parse_args()
    if args.expected_smx_sha256 and not re.fullmatch(r"[0-9a-fA-F]{64}", args.expected_smx_sha256):
        parser.error("--expected-smx-sha256 must contain exactly 64 hexadecimal characters")
    print(json.dumps(package(args.expected_smx_sha256, args.dry_run), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
