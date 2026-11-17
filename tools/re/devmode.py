# Turns the RT64 developer mode (F1 debugger: texture inspector, draw call list, pause) on or off in the installed game's
# graphics.json. The game rewrites that file when it closes, so it only runs with the game closed.
# usage: python devmode.py on|off|status
import os
import json, subprocess, sys

PATH = os.path.join(os.environ['MM64_GAME_DIR'], 'graphics.json')
running = subprocess.run(['tasklist'], capture_output=True, text=True).stdout.lower().count('megaman64recompiled.exe')
cfg = json.load(open(PATH, encoding='utf-8'))
if len(sys.argv) < 2 or sys.argv[1] == 'status':
    print('developer_mode =', cfg.get('developer_mode'))
    sys.exit(0)
if running:
    sys.exit('close the game first, it rewrites graphics.json when it exits')
cfg['developer_mode'] = sys.argv[1] == 'on'
json.dump(cfg, open(PATH, 'w', encoding='utf-8'), indent=4, sort_keys=True)
print('developer_mode =', cfg['developer_mode'])
