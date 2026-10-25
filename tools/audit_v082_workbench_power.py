"""Audit the final workbench power configuration without running the game.

The receipt checks configuration values and protected-file preservation only;
it does not claim to execute SourcePawn growth or engine power allocation.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import runpy


REPO = Path(__file__).resolve().parents[1]
CONFIG = REPO / "addons/sourcemod/configs/HanZombieShelter/workbenches.cfg"
REPORTS = REPO / "tests/reports/v082"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=REPORTS / "workbench_power_protected_before.json")
    parser.add_argument("--output", type=Path, default=REPORTS / "workbench_power_config_audit.json")
    args = parser.parse_args()
    parse = runpy.run_path(str(REPO / "tools/migrate_v05_materials.py"))["parse"]
    parsed = parse(CONFIG.read_text(encoding="utf-8-sig"))
    work = parsed["ShelterWorkbenches"]
    station = work["Types"]["合成台"]
    growth = station["Growth"]
    base = station["Levels"]["1"]
    checks: list[dict] = []

    def check(name: str, passed: bool) -> None:
        checks.append({"check": name, "passed": bool(passed)})

    check("root/schema", list(parsed) == ["ShelterWorkbenches"] and work["schema_version"] == "2")
    check("Lv1 power requirement 50", base["power_required"] == "50")
    check("power growth per level 30", growth["Stats"]["power_required_per_level"] == "30")
    check("ten levels from anchor one", growth["max_level"] == "10" and growth["anchor_level"] == "1")
    check("no individual power override at Lv2..10", all("power_required" not in station["Levels"].get(str(level), {}) for level in range(2, 11)))
    check("HP growth retained", base["max_hp"] == "500" and growth["Stats"]["max_hp_per_level"] == "100")
    check("calibrated offset retained", station["Placement"]["origin_offset"] == "0 0 0.5")
    check("original model retained", station["model"] == "models/props/de_piranesi/pi_sundial.mdl")
    check("one per base retained", work["Limits"]["max_per_base"] == "1")
    configured = [int(base["power_required"]) + int(growth["Stats"]["power_required_per_level"]) * (level - int(growth["anchor_level"])) for level in range(1, 11)]
    expected = [50, 80, 110, 140, 170, 200, 230, 260, 290, 320]
    for level, (actual, approved) in enumerate(zip(configured, expected), 1):
        check(f"configured Lv{level} demand {approved}", actual == approved)

    protected = json.loads(args.baseline.read_text(encoding="utf-8"))["files"]
    protected_after = []
    for item in protected:
        path = Path(item["Path"])
        current = hashlib.sha256(path.read_bytes()).hexdigest().upper()
        check(f"protected unchanged: {path.relative_to(REPO)}", current == item["Hash"])
        protected_after.append({"path": str(path), "sha256": current.lower()})
    failed = sum(not item["passed"] for item in checks)
    report = {
        "sampled_at": datetime.now(timezone(timedelta(hours=8))).isoformat(),
        "kind": "offline_workbench_power_configuration_static_audit",
        "production_vm_executed": False, "engine_power_tested": False,
        "game_commands_sent": False, "server_started": False, "writes_live_server": False,
        "passed": len(checks) - failed, "failed": failed,
        "configured_power_by_level": configured,
        "protected_configuration_count": len(protected),
        "workbench_sha256": hashlib.sha256(CONFIG.read_bytes()).hexdigest(),
        "baseline": str(args.baseline), "checks": checks,
        "protected_configuration_hashes": protected_after,
        "limits": ["The earlier zero-power engine tests are historical evidence, not validation of this 50+30-per-level configuration.", "This audit does not execute engine power allocation or prove the runtime energized/unpowered gates."],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": str(args.output), "passed": report["passed"], "failed": failed, "power_by_level": configured, "protected_configuration_count": len(protected), "engine_power_tested": False}, ensure_ascii=True))
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
