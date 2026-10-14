"""Real CS:S UI regression for Shelter 0.3; run only on the test server.

Requires a living real client already connected to port 27129 and the integration
probe with LiveManagementProbe.inc loaded. Does not start, connect or restart
anything. Fixtures use RCON; E and all menu choices use client_keyboard.press
(scan codes with a foreground-window check). JPEGs require independent visual
inspection: successful input or file creation is not visual acceptance.

Usage:
    python tools/test_client_ui_v03.py
    python tools/test_client_ui_v03.py --menus-only
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from client_keyboard import press
from local_rcon import ROOT, command

REPO = Path(__file__).resolve().parents[1]
DEFAULT_CSS = Path(r"D:\SteamLibrary\steamapps\common\Counter-Strike Source")
DEFAULT_REPORT = REPO / "tests/reports/v03/ui"
STOP_CFG = "zsh03_verify_stop.cfg"
SWING_CFG = "zsh03_verify_swing.cfg"
COMMAND_CFG = "zsh03_verify_command.cfg"


class UIProbe:
    def __init__(self, css: Path, report: Path, step_delay: float = 1.0):
        self.css = css.resolve()
        self.report = report.resolve()
        self.step_delay = step_delay
        self.run_id = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
        self.entries: list[dict] = []
        self.screenshots: list[str] = []
        self.cleanup_errors: list[str] = []
        self.admin_requested = False
        self.status = "not_started"
        self.error = ""
        self.report.mkdir(parents=True, exist_ok=True)
        self.log_path = self.report / f"zsh03_{self.run_id}_ui_rcon.json"
        self.startup = subprocess.STARTUPINFO()
        self.startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        self.startup.wShowWindow = 0

    def record(self, kind: str, **details):
        self.entries.append({
            "time_utc": datetime.now(timezone.utc).isoformat(),
            "kind": kind,
            **details,
        })
        self.save()

    def save(self):
        payload = {
            "run_id": self.run_id,
            "status": self.status,
            "error": self.error,
            "visual_review": "pending_independent_screenshot_inspection",
            "server_directory": str(ROOT),
            "client_directory": str(self.css),
            "screenshots": self.screenshots,
            "cleanup_errors": self.cleanup_errors,
            "entries": self.entries,
        }
        # Save on every step, so a failed assertion never loses earlier evidence.
        self.log_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def rcon(self, text: str) -> str:
        try:
            output = command(text)
        except Exception as exc:
            self.record("rcon", command=text, error=repr(exc))
            raise
        self.record("rcon", command=text, output=output)
        print(text, output, flush=True)
        if (re.search(r"\bFAIL\b", output, re.IGNORECASE)
                or "no living real client" in output.lower()
                or "unknown command" in output.lower()):
            raise AssertionError(f"RCON rejected {text!r}: {output}")
        return output

    def client(self, *args: str):
        argv = [str(self.css / "cstrike_win64.exe"), "-hijack", "-game", "cstrike", *args]
        result = subprocess.run(
            argv, cwd=self.css, startupinfo=self.startup, timeout=15, check=True,
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        self.record("client_hijack", args=list(args), returncode=result.returncode)

    def cfg(self, name: str, contents: str):
        if name not in (STOP_CFG, SWING_CFG, COMMAND_CFG):
            raise ValueError("Unexpected client fixture filename")
        path = self.css / "cstrike/cfg" / name
        path.write_text(contents, encoding="ascii")
        self.client("+exec", name)

    def stop_attack(self):
        self.cfg(STOP_CFG, "-attack\n-attack2\n-use\nhideconsole\n")

    def key(self, key: str, purpose: str):
        press(key)
        self.record("physical_key", key=key, purpose=purpose)
        time.sleep(self.step_delay)

    def settle(self):
        time.sleep(self.step_delay)

    def shot(self, label: str):
        if not re.fullmatch(r"[a-z0-9_]+", label):
            raise ValueError("Invalid screenshot label")
        name = f"zsh03_{self.run_id}_{label}"
        source = self.css / "cstrike/screenshots" / (name + ".jpg")
        if source.exists():
            raise FileExistsError(f"Refusing to reuse old screenshot: {source}")
        self.client("+jpeg", name, "100")
        time.sleep(0.6)
        deadline = time.monotonic() + 5.0
        while not source.is_file() or source.stat().st_size == 0:
            if time.monotonic() >= deadline:
                raise TimeoutError(f"The client did not create {source}")
            time.sleep(0.1)
        target = self.report / source.name
        if target.exists():
            raise FileExistsError(f"Refusing to overwrite UI evidence: {target}")
        shutil.copy2(source, target)
        self.screenshots.append(str(target))
        self.record("screenshot", file=str(target), bytes=target.stat().st_size,
                    visual_review="required")
        print("Screenshot for review:", target, flush=True)

    def admin_home(self):
        # Open the player's command locally; RCON client 0 cannot own this menu.
        self.cfg(COMMAND_CFG, "sm_zsh_admin\nhideconsole\n")
        self.settle()

    def knife_regression(self):
        self.rcon("zsh_live resource")
        self.settle()
        self.shot("resource_ready_chi")
        self.cfg(SWING_CFG, "use weapon_knife\n+attack\nhideconsole\n")
        time.sleep(3.0)
        self.stop_attack()
        self.shot("resource_depleted_chi")
        self.rcon("zsh_live verify_resource")

        self.rcon("zsh_live repair")
        self.settle()
        self.cfg(SWING_CFG, "use weapon_knife\n+attack\nhideconsole\n")
        time.sleep(3.0)
        self.stop_attack()
        self.shot("base_repaired_full_chi")
        self.rcon("zsh_live verify_repair")

    def defense_transactions(self):
        self.rcon("zsh_live defense")
        self.settle()
        self.rcon("zsh_live aim_defense")
        self.key("E", "open owned defense in the base safety zone")
        self.shot("defense_menu_chi")
        self.key("1", "view defense upgrade materials")
        self.shot("defense_upgrade_cost_chi")
        self.key("1", "confirm paid defense upgrade")
        self.rcon("zsh_live verify_upgrade")
        self.shot("defense_upgraded_chi")
        self.key("2", "view dismantle confirmation and refund")
        self.shot("defense_dismantle_cost_chi")
        self.rcon("zsh_live record_refund")
        self.key("1", "confirm one dismantle transaction")
        self.rcon("zsh_live verify_dismantle")
        self.shot("defense_dismantled_chi")

    def administrator_menus(self):
        self.rcon("zsh_live defense")  # Fresh fixture resets personal wood to 1000.
        self.admin_requested = True  # Cleanup even if the following RCON check fails.
        self.rcon("zsh_live admin_on")
        self.settle()
        self.rcon("zsh_live aim_defense")
        self.key("E", "open defense menu with administrator entry")
        self.key("3", "open nearby administrator building controls")
        self.shot("admin_nearby_building_chi")

        self.admin_home()
        self.shot("admin_home_chi")
        self.key("3", "list actual online human players")
        self.shot("admin_real_players_chi")
        self.key("1", "select the first connected real player fixture")
        self.shot("admin_player_materials_chi")
        self.key("1", "select the real player's wood")
        self.shot("admin_player_wood_chi")
        self.key("1", "add ten wood through the administrator menu")
        self.rcon("zsh_live verify_admin_material")
        self.shot("admin_player_wood_added_chi")

        self.admin_home()
        self.key("2", "list independent shared base warehouses")
        self.shot("admin_base_list_chi")
        self.key("1", "select base A warehouse")
        self.shot("admin_base_materials_chi")
        self.key("1", "view shared wood amount controls")
        self.shot("admin_base_wood_chi")

        self.admin_home()
        self.key("4", "list every managed building")
        self.shot("admin_global_buildings_chi")
        self.key("1", "open the first base from the remote global list")
        self.shot("admin_global_base_chi")
        self.admin_home()
        self.key("1", "view current-day reset confirmation without executing it")
        self.shot("admin_day_reset_confirm_chi")
        self.key("2", "cancel the day reset and return")
        self.key("0", "close administrator menu")

    def amplifier_and_languages(self):
        self.rcon("zsh_live amplifier")
        self.settle()
        self.rcon("zsh_live aim_defense")
        self.key("E", "inspect the physical amplifier and team bonuses")
        self.shot("amplifier_menu_chi")
        self.key("1", "inspect amplifier next-tier materials and base requirement")
        self.shot("amplifier_upgrade_cost_chi")
        self.key("0", "close amplifier confirmation without changing its level")

        # Language fixtures change client language and may open the local base menu.
        self.rcon("zsh_live english")
        self.key("0", "close the fixture's base menu before an actual E interaction")
        self.rcon("zsh_live aim_defense")
        self.settle()
        self.key("E", "open amplifier menu with English client language")
        self.shot("amplifier_menu_en")
        self.key("1", "show English amplifier upgrade quote")
        self.shot("amplifier_upgrade_cost_en")
        self.key("0", "close English amplifier quote")

        self.rcon("zsh_live base 0")
        self.settle()
        self.key("E", "open the safety-zone base menu in English")
        self.shot("base_menu_en")
        self.key("2", "open English shared warehouse")
        self.shot("base_storage_en")
        self.key("1", "inspect personal and shared wood balances")
        self.shot("base_transfer_en")

        self.admin_home()
        self.shot("admin_home_en")
        self.key("4", "inspect the global building list in English")
        self.shot("admin_global_buildings_en")
        self.rcon("zsh_live chinese")
        self.key("0", "close the fixture's Chinese base menu")
        self.rcon("zsh_live base 0")
        self.settle()
        self.key("E", "open the safety-zone base menu in Chinese")
        self.shot("base_menu_chi")
        self.key("2", "open Chinese shared warehouse")
        self.shot("base_storage_chi")
        self.key("1", "inspect personal and shared materials in Chinese")
        self.shot("base_transfer_chi")
        self.key("0", "close the transfer menu")

    def alarms_and_budget(self):
        self.rcon("zsh_live clear_fixture")
        self.settle()  # Let the engine remove the old facility before tracing the base.
        self.rcon("zsh_live repair")  # Position the actual crosshair on base A.
        self.rcon("zsh_live alerts")  # NPC damage flags keep both alarms active for 30 s.
        self.settle()
        self.shot("base_a_aim_and_ab_alerts_chi")
        self.rcon("zsh_live budget")
        self.settle()
        self.shot("entity_budget_rejection_chi")
        self.rcon("sm_zsh_status")

    def run(self, menus_only: bool):
        binary = self.css / "cstrike_win64.exe"
        if not binary.is_file():
            raise FileNotFoundError(binary)
        if not (self.css / "cstrike/cfg").is_dir():
            raise FileNotFoundError(self.css / "cstrike/cfg")
        self.status = "running"
        self.save()
        try:
            closed = self.rcon("zsh_live closepanels")
            self.rcon("zsh_live clear_fixture")
            if "[LiveUI]" not in closed:
                raise AssertionError("The integration UI fixture did not confirm that MOTD was closed")
            self.stop_attack()
            self.rcon("zsh_live base 0")
            self.rcon("zsh_live chinese")
            self.key("0", "close any existing menu before regression")
            self.shot("motd_closed")
            self.rcon("zsh_test admin_menus")  # Real-client authorization and token assertions.
            if not menus_only:
                self.knife_regression()
            self.defense_transactions()
            self.administrator_menus()
            self.amplifier_and_languages()
            self.alarms_and_budget()
            self.status = "automated_steps_complete_visual_review_pending"
        except BaseException as exc:
            self.status = "failed"
            self.error = repr(exc)
            self.record("failure", error=self.error)
            raise
        finally:
            # A failed check stops the sequence; only releasing input and the temporary
            # administrator identity is permitted afterward.
            try:
                self.stop_attack()
            except Exception as exc:
                self.cleanup_errors.append("stop_attack: " + repr(exc))
            if self.admin_requested:
                try:
                    self.rcon("zsh_live admin_off")
                except Exception as exc:
                    self.cleanup_errors.append("admin_off: " + repr(exc))
            if self.cleanup_errors:
                self.status = "failed_cleanup"
            self.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--menus-only", action="store_true", help="Skip the real knife regression")
    parser.add_argument("--css", type=Path, default=DEFAULT_CSS)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--step-delay", type=float, default=1.0)
    args = parser.parse_args()
    if not 0.4 <= args.step_delay <= 1.0:
        parser.error("--step-delay must be between 0.4 and 1.0 seconds")
    probe = UIProbe(args.css, args.report, args.step_delay)
    try:
        probe.run(args.menus_only)
    except BaseException as exc:
        if probe.status == "not_started":
            probe.status = "failed_preflight"
            probe.error = repr(exc)
            probe.record("failure", error=probe.error)
        print("UI probe stopped:", repr(exc), file=sys.stderr, flush=True)
        print("Saved evidence log:", probe.log_path, flush=True)
        return 1
    if probe.cleanup_errors:
        print("Cleanup failed:", probe.cleanup_errors, file=sys.stderr, flush=True)
        return 1
    print("Physical input and RCON checks completed; screenshots still require visual review.", flush=True)
    print("Evidence log:", probe.log_path, flush=True)
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    raise SystemExit(main())
