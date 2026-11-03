"""Build reviewed, installable ZIP components without compiling or deploying.

Only explicit product inputs and the production include closure are selected.
Historical ZIPs, reports, control scripts and test plugins are never copied.
All inputs are read before the output directory is created. Existing output
directories are refused, including empty ones.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[1]
PRODUCT = "CSS-HZS-ZombieShelter"
STAMP = (1980, 1, 1, 0, 0, 0)
MANIFEST = "MANIFEST.sha256.json"
SCRIPTING = "src"
RELEASE_PROFILE = "release"
NPC_FILES = (
    "CREDITS.md", "LICENSE.upstream-MIT.txt", "manifest.json", "README.md",
    "addons/sourcemod/gamedata/hzsh_npc_follow.gamedata.txt",
    "addons/sourcemod/plugins/HanZombieShelterNPCFollow.smx",
    "addons/sourcemod/scripting/HanZombieShelterNPCFollow.sp",
)
ASSET_ROOTS = {"models", "materials", "sound", "maps", "scripts", "particles", "resource"}
EXCLUDED_ASSETS = {
    "maps/zsh_desert_han.bsp", "maps/zsh_desert_han.nav",
    "scripts/game_sounds_weapons.txt",
}


class PackageError(ValueError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def safe_name(name: str) -> str:
    path = PurePosixPath(name)
    if (not name or "\\" in name or ":" in name or path.is_absolute()
            or any(part in ("", ".", "..") for part in name.split("/"))):
        raise PackageError(f"Unsafe package path: {name}")
    return name


def read_input(root: Path, relative: str) -> bytes:
    safe_name(relative)
    path = root.joinpath(*PurePosixPath(relative).parts)
    # Keep explicit inputs within this checkout, including through symlinks.
    if not path.resolve().is_relative_to(root.resolve()):
        raise PackageError(f"Input escapes checkout: {relative}")
    if not path.is_file():
        raise PackageError(f"Required package input missing: {relative}")
    return path.read_bytes()


def read_version(root: Path) -> str:
    data = read_input(root, f"{SCRIPTING}/HanZombieShelter/Types.inc")
    matches = re.findall(r'^\s*#define\s+SHELTER_VERSION\s+"(\d+\.\d+\.\d+)"\s*$',
                         data.decode("utf-8-sig"), re.MULTILINE)
    if len(matches) != 1:
        raise PackageError("Expected exactly one numeric SHELTER_VERSION in Types.inc")
    return matches[0]


def manifest_inputs(root: Path, folder: str, *, allowed_roots=None,
                    compatibility=False) -> dict[str, bytes]:
    """Load only reviewed manifest members and verify their exact bytes."""
    manifest = json.loads(read_input(root, f"{folder}/manifest.json"))
    entries = manifest.get("files")
    if not isinstance(entries, list) or not entries:
        raise PackageError(f"Missing file list in {folder}/manifest.json")
    if manifest.get("file_count", len(entries)) != len(entries):
        raise PackageError(f"Manifest count mismatch: {folder}")
    result = {}
    for entry in entries:
        relative = safe_name(entry["path"])
        parts = PurePosixPath(relative).parts
        output = relative
        if compatibility:
            if parts[0] == "resources" and len(parts) > 2 and parts[1] in ASSET_ROOTS:
                output = "/".join(parts[1:])
            elif not (relative.startswith("addons/sourcemod/configs/")
                      or relative.startswith("addons/sourcemod/translations/")):
                raise PackageError(f"Non-configuration compatibility input: {relative}")
        elif parts[0] not in allowed_roots:
            raise PackageError(f"Input outside public payload: {folder}/{relative}")
        if folder == "plugin":
            allowed = (
                relative in (
                    "addons/sourcemod/plugins/HanZombieShelter.smx",
                    "addons/sourcemod/plugins/HanZombieShelterEnvironment.smx",
                    "addons/sourcemod/translations/HanZombieShelter.phrases.txt",
                )
                or relative.startswith((
                    "addons/sourcemod/configs/HanZombieShelter/",
                    "addons/sourcemod/configs/HanZombieShelterEnvironment/",
                    "addons/sourcemod/configs/HanZombieScenario/",
                ))
            )
            if not allowed or relative.endswith("/drops.cfg"):
                raise PackageError(f"Non-product runtime input: {relative}")
        if output in EXCLUDED_ASSETS:
            raise PackageError(f"Retired or global asset cannot be distributed: {output}")
        data = read_input(root, f"{folder}/{relative}")
        if digest(data) != entry["sha256"] or len(data) != entry["bytes"]:
            raise PackageError(f"Manifest identity mismatch: {folder}/{relative}")
        if output.casefold() in {name.casefold() for name in result}:
            raise PackageError(f"Duplicate manifest member: {folder}/{output}")
        result[output] = data
    return result


def production_sources(root: Path) -> dict[str, bytes]:
    """Include the two production entrypoints, API headers and local closure.

    Local includes are restricted to the product module/header directories.
    A source change that tries to include tests or other repository files fails
    preflight instead of silently bringing those files into the source package.
    """
    initial = (
        "HanZombieShelter.sp", "HanZombieShelterEnvironment.sp",
        "include/HanZombieScenarioAPI.inc", "include/HanZombieShelterAPI.inc",
    )
    pending = [f"{SCRIPTING}/{name}" for name in initial]
    result = {}
    while pending:
        relative = pending.pop()
        if relative in result:
            continue
        data = read_input(root, relative)
        result[relative] = data
        for include in re.findall(r'^\s*#(?:try)?include\s+"([^"]+)"',
                                  data.decode("utf-8-sig"), re.MULTILINE):
            # Production includes intentionally use forward-slash paths.
            safe_name(include)
            target = (PurePosixPath(relative).parent / include).as_posix()
            allowed = (target.startswith(f"{SCRIPTING}/HanZombieShelter/")
                       or target.startswith(f"{SCRIPTING}/include/"))
            if not allowed or not target.endswith(".inc"):
                raise PackageError(f"Non-product local include in {relative}: {include}")
            pending.append(target)
    return result


def installation_text(kind: str, version: str) -> bytes:
    intro = f"# {PRODUCT} v{version} — {kind}\n\n"
    texts = {
        "runtime": """本包包含当前主插件、独立环境组件、默认配置和正式翻译。

安装：将本包 addons/ 合并到 CS:S 服务端的 cstrike/。已有服务器先备份
SMX、翻译和现场配置，逐项合并新配置，保留自己的数值和地图坐标。
在安排好的维护时机正常加载；不要通过清库存或重置进度完成升级。
检查 SourceMod 插件列表、错误日志及 sm_zsh_status。

前置：可运行的 CS:S、Metamod:Source、SourceMod、兼容 HZS 主体及其依赖。
本包不含 HZS 主体或其他外部插件。首次使用默认组合，同时安装 resources 和
server-compat 包；地图与建筑资源由前者提供，新增丧尸/十刀及技能资源由后者提供。
默认斧头及十刀配方依赖外部 HWS/武器插件注册 sm_axe、sm_shuangren 等命令。
没有对应插件时，先停用或调整这些配方。

默认配置可按自己的玩法修改。提供 zsh_desert_han_dn
和 zsh_deadcity_han_dn 的完整 Shelter 地图配置、原生 Stage 及 NPC 数据。
沙漠 DN 为2基地/3 Boss，死城 DN 为6基地/1胖子；五种采集物基础每击收益为10。
lightstyle_audits 是与实际 BSP 指纹配对的运行输入，不能直接删掉或跨图套用。
HanZombieShelterEnvironment.smx 及其配置是可选昼夜视觉组件。

本包没有测试插件、追击补丁或回退版 SMX。旧 desertplant 配置仅保存在历史示例中。
源码在项目仓库的 src/ 目录。
""",
        "source": """本包包含主插件、环境组件、全部生产模块、两个 API 头文件和构建入口。
不含 HZS 主体实现、SourceMod 工具链、测试夹具或历史现场脚本。

准备 SourceMod 1.12 的编译器和标准 include，在解压目录运行 PowerShell：

```powershell
.\\build.ps1 -Compiler 'C:\\sourcemod\\addons\\sourcemod\\scripting\\spcomp64.exe' -Target Main
.\\build.ps1 -Compiler 'C:\\sourcemod\\addons\\sourcemod\\scripting\\spcomp64.exe' -Target Environment
```

include 默认使用编译器旁的 include/，也可传 -SourceModInclude 指定。
输出为 build/*.smx，不会自动部署到服务器。
本独立源码包支持 Main 和 Environment；根构建脚本中的其他目标需要完整仓库。
src/ 内的主 .sp 和模块目录相对位置必须保持。源码不包含运行配置或资源，
安装时另外获取 runtime 包及所需地图/资源。API 以 include/ 下正式声明为准。
""",
        "resources": """这是主玩法资源包，含 models/、materials/、sound/、maps/、scripts/。
将这些目录合并到服务器 cstrike/，保持目录层次，并同步资源下载渠道。
不要把本包名称或外层 resources/ 目录嵌套到服务器路径中。

包含两张默认 DN 地图的 BSP/NAV 和主玩法建筑、炮塔、设施资源。
不包含废弃的 zsh_desert_han 或全局 game_sounds_weapons.txt。资源来源和已知缺件
见 SOURCES.md，模型依赖见 dependencies.json。
额外丧尸、Boss、十刀和外部技能资源位于 server-compat 包，默认NPC配置需要它。
本包不包含 Valve 的游戏VPK、HZS/HWS 主体插件，也不伪造原有缺失的Vox音频。
按原目录安装模型侧件、材质和纹理，并提供客户端下载。
""",
        "server-compat": """这是默认丧尸和武器组合的兼容配置与资源包，不包含外部插件SMX。

将 addons/、models/、materials/、sound/、scripts/、maps/、particles/、resource/
中实际存在的目录合并到服务器 cstrike/，保持层次。已有配置先备份并比较。
按需求安装 HZS 主体、HWS、DeltaForceKnife 和兼容的 HZS 技能插件。
默认的21类普通丧尸及三个Boss使用本包NPC资源；默认武器配方使用本包刀具资源
与HWS/刀插件配置。外部丧尸技能与Shelter玩家技能是不同系统。

本包从实际引用收集资源，不包含管理员、密码、玩家数据、服务器控制或日志。
缺失材质、动画音效和Vox等原有问题见 KNOWN_MISSING.json；不会用占位文件掩盖。
SOURCE_MANIFEST.json 使用仓库中的路径记录原始来源；安装后的成员和校验和以
MANIFEST.sha256.json 为准。源码、许可和安装依赖应从对应插件项目取得。
""",
        "optional-npc-follow-range": """本包保留独立 NPC 追击距离插件的原始 7 个文件及其原许可、署名和说明。
包名使用主项目版本便于配套下载，不改变子插件本身的版本或验证范围。

按本包 README.md 安装；只将 addons/ 合并到服务器 cstrike/。
这是依赖已审计引擎签名的可选 Windows x64 补丁，不是主插件必装前置。
请先核对平台、gamedata、原始指令以及是否有其他插件写同一补丁位置。
禁止与 LocalHZSContinuousFollow 或其他同项补丁直接叠加。
本包不包含 BOT 修复，也不代表所有地图的 NPC 路线都已通过。

原始上游 MIT 许可见 LICENSE.upstream-MIT.txt，来源见 CREDITS.md。
根项目许可证额外保存在 LICENSE.project.txt，不替换原始许可文件。
""",
        "optional-hzs-translations": """本包仅为缺少主体翻译的 HZS 安装提供兼容补充。
将 addons/sourcemod/translations/HanZombieScenario.phrases.txt 放到
服务器 cstrike/ 下的同名目录。已有完整或定制翻译时先比较并保留原文件，
不要直接覆盖。它不是 HZS 主体插件，也不提供 NPC、地图或其他运行依赖。
""",
    }
    footer = ("\n文件核对：MANIFEST.sha256.json 记录除自身以外每个成员的字节数和 SHA256。\n"
              "相邻下载清单 SHA256SUMS 用于核对 ZIP 本身；许可证随包保留。\n")
    return (intro + texts[kind] + footer).encode("utf-8")


def prepare_packages(root: Path) -> tuple[str, dict[str, dict[str, bytes]]]:
    """Read and validate every package input; performs no filesystem writes."""
    root = root.resolve()
    version = read_version(root)
    license_data = read_input(root, "LICENSE")
    packages = {}

    runtime = manifest_inputs(root, "plugin", allowed_roots={"addons"})
    runtime["RUNTIME_MANIFEST.json"] = read_input(root, "plugin/manifest.json")
    packages["runtime"] = runtime

    source = production_sources(root)
    # The independently browsable sources must match the install tree at release.
    for path, data in source.items():
        installed = "plugin/addons/sourcemod/scripting/" + path.removeprefix("src/")
        if data != read_input(root, installed):
            raise PackageError(f"Source copies differ: {path} and {installed}")
    source["build.ps1"] = read_input(root, "build.ps1")
    packages["source"] = source

    resources = manifest_inputs(root, "resources", allowed_roots=ASSET_ROOTS)
    for source_name, package_name in (
        ("manifest.json", "manifest.json"),
        ("dependencies.json", "dependencies.json"),
        ("SOURCES.md", "SOURCES.md"),
    ):
        resources[package_name] = read_input(root, f"resources/{source_name}")
    packages["resources"] = resources

    compatibility = manifest_inputs(root, "optional/server-compat", compatibility=True)
    for source_name, package_name in (
        ("manifest.json", "SOURCE_MANIFEST.json"),
        ("missing.json", "KNOWN_MISSING.json"),
        ("CREDITS.md", "CREDITS.md"),
        ("licenses/HanWeaponSystem-LICENSE.txt", "licenses/HanWeaponSystem-LICENSE.txt"),
        ("licenses/DeltaForceKnife-LICENSE.txt", "licenses/DeltaForceKnife-LICENSE.txt"),
        ("licenses/hzs-supporting-plugin-LICENSE.txt", "licenses/hzs-supporting-plugin-LICENSE.txt"),
    ):
        compatibility[package_name] = read_input(root, f"optional/server-compat/{source_name}")
    packages["server-compat"] = compatibility

    packages["optional-npc-follow-range"] = {
        path: read_input(root, f"optional/npc-follow-range/{path}") for path in NPC_FILES
    }
    packages["optional-hzs-translations"] = {
        "addons/sourcemod/translations/HanZombieScenario.phrases.txt":
            read_input(root, "optional/hzs-translations/HanZombieScenario.phrases.txt")
    }

    for kind, entries in packages.items():
        license_name = "LICENSE.project.txt" if kind == "optional-npc-follow-range" else "LICENSE"
        entries[license_name] = license_data
        entries["INSTALL.md"] = installation_text(kind, version)
        folded = set()
        for path in entries:
            safe_name(path)
            if path.casefold() in folded:
                raise PackageError(f"Case-insensitive duplicate member in {kind}: {path}")
            folded.add(path.casefold())
        manifest = {
            "schema_version": 1,
            "package": kind,
            "shelter_version": version,
            "manifest_excludes_itself": True,
            "files": [
                {"path": path, "bytes": len(data), "sha256": digest(data)}
                for path, data in sorted(entries.items())
            ],
        }
        entries[MANIFEST] = json_bytes(manifest)
    return version, packages


def write_zip(path: Path, entries: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, STAMP)
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def package_release(root: Path = ROOT, output: Path | None = None) -> dict:
    # Input preflight must finish before mkdir or any other mutation.
    version, packages = prepare_packages(root)
    destination = Path(output) if output is not None else root / "dist" / f"public-v{version}-{RELEASE_PROFILE}"
    destination = destination.resolve()
    if destination.exists():
        raise PackageError(f"Output directory already exists; choose a new --output: {destination}")
    destination.mkdir(parents=True, exist_ok=False)
    results = []
    for kind, entries in packages.items():
        name = f"{PRODUCT}-v{version}-{kind}.zip"
        path = destination / name
        write_zip(path, entries)
        with zipfile.ZipFile(path) as archive:
            if archive.testzip() is not None or archive.namelist() != sorted(entries):
                raise PackageError(f"ZIP verification failed: {name}")
            for member, expected in entries.items():
                if archive.read(member) != expected:
                    raise PackageError(f"ZIP member mismatch: {name}/{member}")
        raw = path.read_bytes()
        results.append({"package": kind, "file": name, "members": len(entries),
                        "bytes": len(raw), "sha256": digest(raw)})
    summary = {"schema_version": 1, "shelter_version": version,
               "distribution_revision": RELEASE_PROFILE,
               "zip_timestamp": list(STAMP), "packages": results}
    (destination / "summary.json").write_bytes(json_bytes(summary))
    checksums = "".join(f"{item['sha256']}  {item['file']}\n" for item in results)
    checksums += f"{digest(json_bytes(summary))}  summary.json\n"
    (destination / "SHA256SUMS").write_bytes(checksums.encode("utf-8"))
    return summary


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        help="New output directory; default: dist/public-v<VERSION>-release")
    args = parser.parse_args(argv)
    try:
        result = package_release(output=args.output)
    except (PackageError, OSError, UnicodeError, KeyError, TypeError) as error:
        print(f"Package rejected: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
