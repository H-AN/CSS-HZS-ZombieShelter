"""Exercise the real offline migration tool; never create or contact a game."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import runpy
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
API = runpy.run_path(str(ROOT / "tools/migrate_v082_boss_spawn_points.py"))
MigrationError = API["MigrationError"]
SOURCE = ('\ufeff// source comments and CRLF must remain byte-identical\r\n'
          '"ShelterMap"\r\n{\r\n    "schema_version" "1"\r\n'
          '    "map" "zsh_desert_han_dn"\r\n'
          '    "label" "测试源 {quoted braces}"\r\n'
          '    "Bosses" { "boss_01" { "boss_profile" "guard_fat" "marker_targetname" "old_marker" } }\r\n'
          '} // trailing source comment\r\n').encode("utf-8")


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hzsh_v082_boss_points_")
        self.folder = Path(self.temporary.name)
        self.source = self.folder / "source.cfg"
        self.source.write_bytes(SOURCE)
        self.output = self.folder / "candidate/map.cfg"

    def tearDown(self):
        self.temporary.cleanup()

    def migrate(self, **kwargs):
        return API["migrate"](self.source, self.output, use_known=True, **kwargs)

    def test_only_ten_leaves_added_and_all_other_bytes_retained(self):
        result = self.migrate()
        receipt = json.loads(Path(result["report"]).read_text(encoding="utf-8"))
        self.assertEqual(self.source.read_bytes(), SOURCE)
        self.assertTrue(self.output.read_bytes().startswith(b"\xef\xbb\xbf"))
        self.assertTrue(receipt["other_source_bytes_unchanged"])
        self.assertTrue(receipt["other_parsed_leaves_unchanged"])
        self.assertEqual(len(receipt["leaf_diff"]["added"]), 10)
        self.assertEqual(receipt["leaf_diff"]["removed"], {})
        self.assertEqual(receipt["leaf_diff"]["changed"], {})
        self.assertFalse(receipt["safe_spawn_verified"])

    def test_repeat_and_check_are_idempotent(self):
        first = self.migrate()
        report = Path(first["report"])
        saved = (self.output.read_bytes(), report.read_bytes())
        self.migrate()
        checked = self.migrate(check_only=True)
        self.assertTrue(checked["checked_only"])
        self.assertEqual(saved, (self.output.read_bytes(), report.read_bytes()))

    def test_known_local_extras_preserve_three_existing_origins(self):
        points, _ = API["known_points"]("zsh_desert_han_dn", True)
        self.assertEqual(points["boss1"]["origin"], "1585 1756 -1533.96875")
        self.assertEqual(points["boss2"]["origin"], "1280 2800 -1493.96875")
        self.assertEqual(points["boss3"]["origin"], "2400 1280 -1493.96875")
        self.assertEqual(points["boss4"], points["boss1"])
        self.assertEqual(points["boss5"], points["boss1"])

    def test_known_formal_single_boss_has_five_repeated_legacy_points(self):
        points, _ = API["known_points"]("zsh_deadcity_han_dn")
        self.assertEqual(points["boss1"]["origin"], "224 0 36.03125")
        self.assertTrue(all(row == points["boss1"] for row in points.values()))
        _, details = API["build_candidate"](SOURCE, points)
        self.assertEqual(details["duplicate_slots"], {"boss2": "boss1", "boss3": "boss1", "boss4": "boss1", "boss5": "boss1"})

    def test_five_slots_required_without_unknown_slot(self):
        points, _ = API["known_points"]("zsh_deadcity_han_dn")
        missing = copy.deepcopy(points)
        del missing["boss5"]
        with self.assertRaises(MigrationError):
            API["validate_points"](missing)
        extra = copy.deepcopy(points)
        extra["boss6"] = points["boss1"]
        with self.assertRaises(MigrationError):
            API["validate_points"](extra)

    def test_invalid_origin_components_are_rejected(self):
        points, _ = API["known_points"]("zsh_deadcity_han_dn")
        for value in ("nan 0 0", "inf 0 0", "1e309 0 0", "15000.01 0 0", "-15000.01 0 0", "1 2", [True, 0, 0], "1 2 3;quit", "١ 0 0"):
            with self.subTest(origin=value):
                changed = copy.deepcopy(points)
                changed["boss1"]["origin"] = value
                with self.assertRaises(MigrationError):
                    API["validate_points"](changed)

    def test_origin_boundary_allowed_angles_default_zero(self):
        points = {name: {"origin": [-15000, 0, 15000]} for name in API["SLOTS"]}
        actual = API["validate_points"](points)
        self.assertEqual(actual["boss1"], {"origin": "-15000 0 15000", "angles": "0 0 0"})

    def test_nonfinite_or_non_engine_float_angles_are_rejected(self):
        points, _ = API["known_points"]("zsh_deadcity_han_dn")
        for value in ("nan 0 0", "1e100 0 0", "360.01 0 0", "-360.01 0 0", [0, 0], [False, 0, 0]):
            with self.subTest(angles=value):
                changed = copy.deepcopy(points)
                changed["boss1"]["angles"] = value
                with self.assertRaises(MigrationError):
                    API["validate_points"](changed)

    def test_existing_point_table_is_never_overwritten(self):
        points, _ = API["known_points"]("zsh_desert_han_dn")
        existing, _ = API["build_candidate"](SOURCE, points)
        self.source.write_bytes(existing)
        with self.assertRaises(MigrationError):
            self.migrate()
        self.assertEqual(self.source.read_bytes(), existing)
        self.assertFalse(self.output.exists())

    def test_duplicate_kv_or_wrong_root_rejected(self):
        for data in (b'"ShelterMap" { "map" "a" "map" "b" }', b'"Other" { "map" "zsh_desert_han_dn" }', b'"ShelterMap" "scalar"'):
            with self.subTest(source=data):
                self.source.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.migrate()
                self.assertFalse(self.output.exists())

    def test_input_output_and_report_collisions_rejected(self):
        with self.assertRaises(MigrationError):
            API["migrate"](self.source, self.source, use_known=True)
        with self.assertRaises(MigrationError):
            self.migrate(report=self.source)
        with self.assertRaises(MigrationError):
            self.migrate(report=self.output)
        self.assertEqual(self.source.read_bytes(), SOURCE)

    def test_fixed_root_output_and_report_rejected_before_writing(self):
        forbidden = API["FIXED_ROOT"] / "candidate_should_never_be_written.cfg"
        with self.assertRaises(MigrationError):
            API["migrate"](self.source, forbidden, use_known=True)
        with self.assertRaises(MigrationError):
            self.migrate(report=API["FIXED_ROOT"] / "candidate_should_never_be_written.json")
        self.assertFalse(self.output.exists())

    def test_conflicting_output_or_receipt_is_preserved(self):
        self.output.parent.mkdir()
        self.output.write_bytes(b"preserve this file")
        with self.assertRaises(MigrationError):
            self.migrate()
        self.assertEqual(self.output.read_bytes(), b"preserve this file")
        self.output.unlink()
        report = self.folder / "existing.json"
        report.write_text("{}", encoding="utf-8")
        with self.assertRaises(MigrationError):
            self.migrate(report=report)
        self.assertFalse(self.output.exists())
        self.assertEqual(report.read_text(encoding="utf-8"), "{}")

    def test_check_missing_output_writes_nothing(self):
        with self.assertRaises(MigrationError):
            self.migrate(check_only=True)
        self.assertFalse(self.output.parent.exists())

    def test_json_wrapper_defaults_and_duplicate_json_rejection(self):
        points = {name: {"origin": [1, 2, 3]} for name in API["SLOTS"]}
        path = self.folder / "points.json"
        path.write_text(json.dumps({"BossSpawnPoints": points}), encoding="utf-8")
        result = API["migrate"](self.source, self.output, points_json=path)
        receipt = json.loads(Path(result["report"]).read_text(encoding="utf-8"))
        self.assertEqual(receipt["points"]["boss5"]["angles"], "0 0 0")
        path.write_text('{"boss1":{},"boss1":{}}', encoding="utf-8")
        with self.assertRaises(MigrationError):
            API["load_points_json"](path)

    def test_json_input_not_overwritten_and_unknown_map_rejected(self):
        points = self.folder / "points.cfg"
        points.write_text("{}", encoding="utf-8")
        with self.assertRaises(MigrationError):
            API["migrate"](self.source, points, points_json=points)
        with self.assertRaises(MigrationError):
            API["known_points"]("unknown_map")
        with self.assertRaises(MigrationError):
            API["known_points"]("zsh_deadcity_han_dn", True)
        self.assertEqual(points.read_text(encoding="utf-8"), "{}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
