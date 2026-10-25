"""Read-only source/configuration audit for the v0.8.2 knife crafting catalogue.

Reads the repository and the fixed local server configuration. Writes only the
explicit JSON receipt, never server files. This is not a SourcePawn VM or an
engine test; it cannot establish actual weapon delivery or in-game work HP.
"""
from __future__ import annotations

import argparse
import copy
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import runpy


REPO = Path(__file__).resolve().parents[1]
FIXED_ROOT = Path(
    r"D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\昼夜求生本地测试服务器"
)
BASE = REPO / "addons/sourcemod/configs/HanZombieShelter"
LIVE = FIXED_ROOT / "cstrike/addons/sourcemod/configs"
EXPECTED = {
    # level, v0.8.1 repair/construction HP, unchanged gathering multiplier,
    # approved cost order: wood, iron, copper, silver, gold, credits, plague_core.
    "xintiao": (1, 25, "1.000", (75, 25, 5, 0, 0, 1, 0)),
    "xintiao2": (2, 25, "1.000", (100, 35, 10, 0, 0, 2, 0)),
    "lianming": (3, 32, "1.280", (120, 45, 15, 2, 0, 3, 0)),
    "longya": (4, 32, "1.280", (140, 60, 20, 4, 0, 4, 0)),
    "zhuixing": (5, 33, "1.307", (160, 75, 25, 6, 1, 5, 0)),
    "beijixing": (6, 34, "1.360", (180, 90, 30, 8, 2, 6, 1)),
    "beijixing2": (7, 34, "1.360", (200, 110, 35, 10, 3, 8, 1)),
    "chongsu": (8, 34, "1.360", (220, 130, 40, 12, 4, 10, 2)),
    "wange": (9, 37, "1.467", (250, 160, 50, 15, 6, 12, 3)),
    "apexshuangren": (10, 50, "2.000", (300, 200, 75, 25, 10, 20, 5)),
}
MATERIAL_ORDER = ("wood", "iron", "copper", "silver", "gold", "credits", "plague_core")


def audit() -> dict:
    parse = runpy.run_path(str(REPO / "tools/migrate_v05_materials.py"))["parse"]
    checks: list[dict] = []
    receipts: list[dict] = []

    def check(name: str, passed: bool) -> None:
        checks.append({"check": name, "passed": bool(passed)})

    def load(path: Path, root: str) -> dict:
        raw = path.read_bytes()
        parsed = parse(raw.decode("utf-8-sig"))
        check(f"strict KV/root: {path.name}", list(parsed) == [root])
        receipts.append({"path": str(path), "sha256": hashlib.sha256(raw).hexdigest()})
        return parsed[root]

    work = load(BASE / "weapons.cfg", "ShelterWeapons")
    live_work = load(LIVE / "HanZombieShelter/weapons.cfg", "ShelterWeapons")
    hws = load(LIVE / "HanWeaponSystem/HanWeaponData.cfg", "HanWeaponSystem")
    delta = load(LIVE / "DeltaForceKnife.cfg", "DeltaForceKnife")
    crafting = load(BASE / "weapon_crafting.cfg", "ShelterWeaponCrafting")
    materials = load(BASE / "materials.cfg", "ShelterMaterials")["Materials"]
    stations = load(BASE / "workbenches.cfg", "ShelterWorkbenches")["Types"]
    weapons = work["Weapons"]
    live_weapons = live_work["Weapons"]
    recipes = crafting["Recipes"]
    check("work schema unchanged", work["schema_version"] == live_work["schema_version"] == "1")
    check("11 crafting recipes", set(recipes) == {"axe", *("delta_" + key for key in EXPECTED)})
    check("registered personal material ids", all(key in materials and int(value) > 0 for recipe in recipes.values() for key, value in recipe["cost"].items()))
    check("axe actual HWS command", recipes["axe"]["command"] == hws["weapon_axe"]["command"] == "sm_axe")
    check("axe final approved cost", recipes["axe"]["cost"] == {"wood": "50", "iron": "20"})
    check("axe default station/lv1", "required_station_type" not in recipes["axe"] and recipes["axe"].get("required_level", "1") == "1")
    check("axe/default work unchanged", weapons["tomahawk_axe"] == live_weapons["tomahawk_axe"] and weapons["default_knife"] == live_weapons["default_knife"])
    check("excluded incomplete apexmike", not any("apexmike" in str(value) for value in (weapons, recipes)))

    expected_merged = copy.deepcopy(live_weapons)
    knife_rows = []
    for short, (level, old_hp, gather, amounts) in EXPECTED.items():
        key = "delta_" + short
        classname = "weapon_" + short
        command = "sm_shuangren" if short == "apexshuangren" else "sm_" + short
        item = weapons[key]
        recipe = recipes[key]
        expected_cost = {material: str(amount) for material, amount in zip(MATERIAL_ORDER, amounts) if amount}
        expected_merged[key]["repair_hp_per_hit"] = str(old_hp * 10)
        expected_merged[key]["construction_hp_per_hit"] = str(old_hp * 10)
        check(f"{short}: actual class mapping", item["classname"] == hws[classname]["useclassname"] == delta[short]["ClassName"] == classname)
        check(f"{short}: actual registered command configuration", recipe["command"] == hws[classname]["command"] == command)
        check(f"{short}: repair times ten", item["repair_hp_per_hit"] == str(old_hp * 10))
        check(f"{short}: construction times ten", item["construction_hp_per_hit"] == str(old_hp * 10))
        check(f"{short}: gather unchanged", item["gather_multiplier"] == live_weapons[key]["gather_multiplier"] == gather)
        check(f"{short}: approved level/cost", int(recipe["required_level"]) == level and recipe["cost"] == expected_cost)
        check(f"{short}: available station type", recipe.get("required_station_type", next(iter(stations))) in stations)
        knife_rows.append({"id": short, "display_name": recipe["display_name"], "classname": classname, "command": command, "minimum_level": level, "v081_hp": old_hp, "repair_hp": old_hp * 10, "construction_hp": old_hp * 10, "gather_multiplier": gather, "cost": {key: int(value) for key, value in expected_cost.items()}})

    check("complete local table preserved with only the allowed knife HP updates", weapons == expected_merged)
    failed = sum(not item["passed"] for item in checks)
    return {
        "sampled_at": datetime.now(timezone(timedelta(hours=8))).isoformat(),
        "kind": "offline_configuration_static_audit",
        "production_vm_executed": False,
        "server_started": False,
        "client_started": False,
        "writes_live_server": False,
        "passed": len(checks) - failed,
        "failed": failed,
        "checks": checks,
        "source_file_hashes": receipts,
        "knives": knife_rows,
        "limits": [
            "Command names are checked against HWS configuration, not a live command registry.",
            "Configuration equality does not establish actual weapon delivery or in-game HP effects.",
            "Chinese labels without a reliable local source use the original configuration ID.",
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=REPO / "tests/reports/v082/knife_crafting_config_audit.json")
    args = parser.parse_args()
    report = audit()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": str(args.output), "passed": report["passed"], "failed": report["failed"], "production_vm_executed": False, "server_started": False}, ensure_ascii=True))
    raise SystemExit(1 if report["failed"] else 0)


if __name__ == "__main__":
    main()
