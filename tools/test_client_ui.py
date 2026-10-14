"""Windows client UI probe: requires the integration test binary, a connected living
client, and the local RCON password file created by Start-LocalShelter.ps1.
This uses test fixtures (materials, health and position), captures screenshots,
and sends scan-code keys only when the CSS window is foreground. Inspect the
screenshots before accepting UI results. Restore the production binary afterward.
"""
from pathlib import Path
import subprocess,time,json,shutil,sys
from local_rcon import command,ROOT
from client_keyboard import press
sys.stdout.reconfigure(encoding='utf-8',errors='replace')
REPO=Path(__file__).resolve().parents[1]
CSS=Path(r'D:\SteamLibrary\steamapps\common\Counter-Strike Source')
REPORT=REPO/'tests/reports/v02/ui_after_motd';REPORT.mkdir(parents=True,exist_ok=True)
entries=[]
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
def rcon(text):
    out=command(text);entries.append({'command':text,'output':out});print(text,out,flush=True)
    if '[ShelterTest] FAIL' in out:raise AssertionError(out)
    return out
def client(*args):
    subprocess.run([str(CSS/'cstrike_win64.exe'),'-hijack','-game','cstrike',*args],cwd=CSS,startupinfo=si,timeout=15)
def cfg(name,contents):
    (CSS/'cstrike/cfg'/name).write_text(contents,encoding='ascii');client('+exec',name)
def shot(name):
    client('+jpeg',name,'100');time.sleep(.6)
    src=CSS/'cstrike/screenshots'/(name+'.jpg');shutil.copy2(src,REPORT/src.name)
try:
    rcon('zsh_live closepanels');cfg('zsh_verify_stop.cfg','-attack\n-use\nhideconsole\n')
    shot('zsh02_motd_closed')
    if '--menus-only' not in sys.argv:
        rcon('zsh_live resource');time.sleep(.7);shot('zsh02_resource_ready_clear')
        cfg('zsh_verify_swing.cfg','use weapon_knife\n+attack\nhideconsole\n');time.sleep(3.0)
        cfg('zsh_verify_stop.cfg','-attack\n-use\nhideconsole\n');shot('zsh02_resource_empty_clear')
        rcon('zsh_live verify_resource')
        rcon('zsh_live repair');time.sleep(.5)
        cfg('zsh_verify_swing.cfg','use weapon_knife\n+attack\nhideconsole\n');time.sleep(3)
        cfg('zsh_verify_stop.cfg','-attack\n-use\nhideconsole\n');shot('zsh02_wall_repair_full_clear')
        rcon('zsh_live verify_repair')
    rcon('zsh_live base 0');time.sleep(.5)
    press('E');shot('zsh02_base_menu_chi_clear')
    press('1');shot('zsh02_upgrade_cost_chi_clear')
    press('1');shot('zsh02_upgraded_chi_clear');rcon('sm_zsh_status')
    press('2');shot('zsh02_storage_chi_clear')
    press('1');shot('zsh02_transfer_chi_clear');press('2');shot('zsh02_deposit_chi_clear')
    rcon('zsh_live english');time.sleep(1.1);shot('zsh02_base_menu_en_clear')
    press('2');shot('zsh02_storage_en_clear')
    rcon('zsh_live chinese');press('0')
    cfg('zsh_verify_view.cfg','sv_cheats 1\nsetpos 2750 -2200 440\nsetang 33 128 0\nhideconsole\n')
    shot('zsh02_green_square_clear')
    rcon('zsh_live base 0');rcon('zsh_live audio')
finally:
    (REPORT/('ui_menu_rcon.json' if '--menus-only' in sys.argv else 'ui_rcon.json')).write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf-8')
