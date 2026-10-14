"""Real E/1/1 confirmation for formula level 99 -> 100; integration server only."""
from pathlib import Path
import sys
from test_client_ui_v03 import UIProbe, DEFAULT_CSS, REPO

sys.stdout.reconfigure(encoding='utf-8')
p=UIProbe(DEFAULT_CSS,REPO/'tests/reports/v03/growth_ui')
p.status='running'
try:
    p.rcon('zsh_live closepanels');p.stop_attack();p.rcon('zsh_live clear_fixture')
    p.rcon('zsh_live chinese');p.key('0','close existing menu')
    for kind in ['base','turret']:
        p.rcon('zsh_test growth_'+kind+'_ui');p.settle()
        if kind=='turret':p.rcon('zsh_live aim_defense');p.settle()
        p.key('E','open level 99 '+kind);p.shot(kind+'_99_menu')
        p.key('1','view formula quote for level 100');p.shot(kind+'_100_quote')
        p.key('1','confirm paid level 100 upgrade');p.rcon('zsh_test growth_verify_ui')
        p.shot(kind+'_100_capped');p.key('0','close max-level menu')
    p.rcon('zsh_live amplifier');p.settle();p.rcon('zsh_live aim_defense')
    p.key('E','inspect translated amplifier percentages');p.shot('amplifier_percent_chi')
    p.rcon('zsh_live english');p.key('0','close automatic base menu')
    p.rcon('zsh_live aim_defense');p.settle();p.key('E','inspect English amplifier percentages')
    p.shot('amplifier_percent_en');p.key('0','close amplifier menu')
    p.rcon('zsh_live chinese');p.key('0','restore client language')
    p.status='automated_steps_complete_visual_review_pending'
except BaseException as exc:
    p.status='failed';p.error=repr(exc);raise
finally:
    p.stop_attack();p.save()
print('Growth and percent menu evidence:',p.log_path)
