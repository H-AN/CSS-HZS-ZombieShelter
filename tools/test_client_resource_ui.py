"""Real knife harvesting across wood -> gold -> iron; integration probe only."""
from pathlib import Path
import re
import sys
import time
from test_client_ui_v03 import UIProbe, DEFAULT_CSS, REPO, SWING_CFG

sys.stdout.reconfigure(encoding='utf-8')
p=UIProbe(DEFAULT_CSS,REPO/'tests/reports/v03/resource_ui')
p.status='running'
prepared=False
try:
    p.stop_attack();p.rcon('zsh_live closepanels');p.rcon('zsh_live chinese')
    p.key('0','close existing menu');p.rcon('zsh_live clear_fixture')
    # Existing live fixture parks ordinary NPCs and extends daytime for UI work.
    p.rcon('zsh_live resource');p.settle()
    p.rcon('zsh_test resource_ui');prepared=True;p.settle()
    p.shot('wood_ready')
    for stage,label,duration in [(0,'gold',3.0),(2,'iron',0.8)]:
        p.cfg(SWING_CFG,'use weapon_knife\n+attack\nhideconsole\n')
        time.sleep(duration);p.stop_attack()
        state=p.rcon('zsh_test resource_verify_ui '+str(stage))
        p.shot(label+'_hidden')
        elapsed=float(re.search(r'elapsed=([\d.]+)',state)[1])
        # RCON + screenshot already consume about 1 second after the sample.
        time.sleep(max(0.0,4.8-elapsed-1.1))
        p.shot(label+'_fading')
        time.sleep(4.0)
        p.rcon('zsh_test resource_verify_ui '+str(stage+1));p.settle()
        p.shot(label+'_mature')
    p.status='automated_steps_complete_visual_review_pending'
except BaseException as exc:
    p.status='failed';p.error=repr(exc);raise
finally:
    p.stop_attack()
    if prepared:
        try:p.rcon('zsh_test resource_restore_ui')
        except Exception as exc:p.cleanup_errors.append(repr(exc))
    p.save()
print('Resource cycle evidence:',p.log_path)
