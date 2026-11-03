"""Offline distribution checks. No compilation, service or network access."""
from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("package_release", ROOT / "tools/package_release.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class ReleasePackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="hzsh_public_packages_")
        cls.directory = Path(cls.temporary.name)
        cls.first = cls.directory / "first"
        cls.second = cls.directory / "second"
        cls.summary = PACKAGE.package_release(ROOT, cls.first)
        PACKAGE.package_release(ROOT, cls.second)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def archive(self, kind):
        item = next(item for item in self.summary["packages"] if item["package"] == kind)
        return zipfile.ZipFile(self.first / item["file"])

    def test_all_artifacts_are_deterministic_and_sha_verified(self):
        self.assertEqual(self.summary["shelter_version"], PACKAGE.read_version(ROOT))
        self.assertEqual(len(self.summary["packages"]), 6)
        for first in self.first.iterdir():
            self.assertEqual(first.read_bytes(), (self.second / first.name).read_bytes(), first.name)
        for line in (self.first / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
            expected, name = line.split("  ", 1)
            self.assertEqual(hashlib.sha256((self.first / name).read_bytes()).hexdigest(), expected)
        for item in self.summary["packages"]:
            with self.archive(item["package"]) as archive:
                self.assertIsNone(archive.testzip())
                manifest = json.loads(archive.read(PACKAGE.MANIFEST))
                expected_names = {entry["path"] for entry in manifest["files"]}
                self.assertEqual(set(archive.namelist()), expected_names | {PACKAGE.MANIFEST})
                for entry in manifest["files"]:
                    raw = archive.read(entry["path"])
                    self.assertEqual(len(raw), entry["bytes"])
                    self.assertEqual(hashlib.sha256(raw).hexdigest(), entry["sha256"])
                for info in archive.infolist():
                    self.assertEqual(info.date_time, PACKAGE.STAMP)

    def test_runtime_contains_only_current_install_inputs(self):
        with self.archive("runtime") as archive:
            names = set(archive.namelist())
            self.assertIn("addons/sourcemod/plugins/HanZombieShelter.smx", names)
            self.assertIn("addons/sourcemod/configs/HanZombieShelter/lightstyle_audits/zsh_deadcity_han_dn.cfg", names)
            self.assertNotIn("addons/sourcemod/configs/HanZombieShelter/drops.cfg", names)
            self.assertIn("addons/sourcemod/configs/HanZombieShelter/maps/zsh_desert_han_dn.cfg", names)
            self.assertIn("addons/sourcemod/configs/HanZombieShelter/maps/zsh_deadcity_han_dn.cfg", names)
            self.assertIn("addons/sourcemod/configs/HanZombieScenario/HanZombieScenarioZombieData.cfg", names)
            self.assertNotIn("addons/sourcemod/configs/HanZombieShelter/maps/zsh_desertplant_cso_v1_d.cfg", names)
            for name in names:
                self.assertNotIn("development/", name)
                self.assertNotIn("rollback", name)
                self.assertNotIn("gamedata/", name)
                self.assertNotIn("scripting/", name)
                if name.startswith("addons/"):
                    self.assertEqual(archive.read(name), (ROOT / "plugin" / name).read_bytes())

    def test_extracted_source_has_complete_relative_includes_and_build_inputs(self):
        target = self.directory / "extracted_source"
        with self.archive("source") as archive:
            archive.extractall(target)
            self.assertNotIn("development/tests/ShelterIntegrationProbe.sp", archive.namelist())
            self.assertFalse(any(name.endswith(".smx") for name in archive.namelist()))
        self.assertEqual((target / "build.ps1").read_bytes(), (ROOT / "build.ps1").read_bytes())
        sources = PACKAGE.production_sources(target)
        self.assertEqual(sources, PACKAGE.production_sources(ROOT))
        self.assertIn(PACKAGE.SCRIPTING + "/HanZombieShelter.sp", sources)
        self.assertIn(PACKAGE.SCRIPTING + "/HanZombieShelterEnvironment.sp", sources)
        modules = list((ROOT / PACKAGE.SCRIPTING / "HanZombieShelter").glob("*.inc"))
        for module in modules:
            self.assertEqual((target / module.relative_to(ROOT)).read_bytes(), module.read_bytes())

    def test_resource_and_optional_files_keep_original_bytes(self):
        with self.archive("resources") as archive:
            payload = [name for name in archive.namelist() if name.split("/", 1)[0] in PACKAGE.ASSET_ROOTS]
            manifest = json.loads((ROOT / "resources/manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(set(payload), {entry["path"] for entry in manifest["files"]})
            self.assertEqual(len(payload), manifest["file_count"])
            self.assertNotIn("maps/zsh_desert_han.bsp", payload)
            self.assertNotIn("maps/zsh_desert_han.nav", payload)
            self.assertNotIn("scripts/game_sounds_weapons.txt", payload)
            for name in payload:
                self.assertEqual(archive.read(name), (ROOT / "resources" / name).read_bytes())
        with self.archive("optional-npc-follow-range") as archive:
            for name in PACKAGE.NPC_FILES:
                self.assertEqual(archive.read(name), (ROOT / "optional/npc-follow-range" / name).read_bytes())
            self.assertEqual(archive.read("LICENSE.project.txt"), (ROOT / "LICENSE").read_bytes())
        with self.archive("optional-hzs-translations") as archive:
            name = "addons/sourcemod/translations/HanZombieScenario.phrases.txt"
            self.assertEqual(archive.read(name), (ROOT / "optional/hzs-translations/HanZombieScenario.phrases.txt").read_bytes())

    def test_compatibility_package_is_flat_and_excludes_external_binaries(self):
        manifest = json.loads((ROOT / "optional/server-compat/manifest.json").read_text(encoding="utf-8"))
        with self.archive("server-compat") as archive:
            names = set(archive.namelist())
            self.assertFalse(any(name.startswith("resources/") or name.endswith(".smx") for name in names))
            for entry in manifest["files"]:
                output = entry["path"].removeprefix("resources/")
                self.assertIn(output, names)
                self.assertEqual(archive.read(output), (ROOT / "optional/server-compat" / entry["path"]).read_bytes())
            self.assertIn("KNOWN_MISSING.json", names)

    def test_manifest_tamper_is_rejected_before_output(self):
        output = self.directory / "tamper-parent" / "never-created"
        original = PACKAGE.read_input

        def tamper(root, relative):
            data = original(root, relative)
            if relative == "plugin/addons/sourcemod/configs/HanZombieShelter/resources.cfg":
                return data + b"\n// unexpected change\n"
            return data

        with patch.object(PACKAGE, "read_input", side_effect=tamper):
            with self.assertRaises(PACKAGE.PackageError):
                PACKAGE.package_release(ROOT, output)
        self.assertFalse(output.parent.exists())

    def test_source_copy_drift_is_rejected_before_output(self):
        output = self.directory / "source-drift-parent" / "never-created"
        original = PACKAGE.read_input

        def drift(root, relative):
            data = original(root, relative)
            if relative == "src/HanZombieShelter.sp":
                return data + b"\n// unsynchronized edit\n"
            return data

        with patch.object(PACKAGE, "read_input", side_effect=drift):
            with self.assertRaises(PACKAGE.PackageError):
                PACKAGE.package_release(ROOT, output)
        self.assertFalse(output.parent.exists())

    def test_missing_late_input_fails_before_creating_output(self):
        output = self.directory / "missing-parent" / "never-created"
        original = PACKAGE.read_input

        def fail_late(root, relative):
            if relative == "optional/hzs-translations/HanZombieScenario.phrases.txt":
                raise PACKAGE.PackageError("missing last package input")
            return original(root, relative)

        with patch.object(PACKAGE, "read_input", side_effect=fail_late):
            with self.assertRaises(PACKAGE.PackageError):
                PACKAGE.package_release(ROOT, output)
        self.assertFalse(output.parent.exists())

    def test_existing_output_is_never_overwritten(self):
        before = {path.name: path.read_bytes() for path in self.first.iterdir()}
        with self.assertRaises(PACKAGE.PackageError):
            PACKAGE.package_release(ROOT, self.first)
        self.assertEqual(before, {path.name: path.read_bytes() for path in self.first.iterdir()})
        empty = self.directory / "already-empty"
        empty.mkdir()
        with self.assertRaises(PACKAGE.PackageError):
            PACKAGE.package_release(ROOT, empty)
        self.assertEqual(list(empty.iterdir()), [])

    def test_unlisted_file_is_excluded_and_source_escape_rejected(self):
        fixture = self.directory / "source_fixture"
        for relative, data in PACKAGE.production_sources(ROOT).items():
            path = fixture / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        injected = fixture / PACKAGE.SCRIPTING / "HanZombieShelter/Unlisted.inc"
        injected.write_text("// unused diagnostic helper", encoding="utf-8")
        self.assertNotIn(injected.relative_to(fixture).as_posix(), PACKAGE.production_sources(fixture))
        main = fixture / PACKAGE.SCRIPTING / "HanZombieShelter.sp"
        main.write_bytes(main.read_bytes() + b'\n#include "../development/tests/Secret.inc"\n')
        with self.assertRaises(PACKAGE.PackageError):
            PACKAGE.production_sources(fixture)

    def test_unsafe_archive_paths_are_rejected(self):
        for path in ("../x", "x/../y", "/absolute", "C:/absolute", "x\\y", "x//y", "./x", ""):
            with self.subTest(path=path), self.assertRaises(PACKAGE.PackageError):
                PACKAGE.safe_name(path)


if __name__ == "__main__":
    unittest.main()
