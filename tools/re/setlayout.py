# usage: setlayout.py (game closed) - writes the modern layout into game\controls.json (the saved file wins over the
# defaults in input.cpp): Cross A, Circle C-Down, Square Z (side hop), R2 B, R1 C-Left, L1 L (strafe modifier),
# d-pad = the game's d-pad, C-Right unbound (R3 focus replaces it). A .bak of the old file is kept.
import json, os, shutil
p = os.path.join(os.environ['MM64_GAME_DIR'], 'controls.json')
if not os.path.exists(p + '.bak'):  # keep the first backup (the layout from before this script)
    shutil.copy(p, p + '.bak')
j = json.load(open(p))
c = j['controller']
btn = lambda i: {'input_id': i, 'input_type': 3}
none = {'input_id': 0, 'input_type': 0}
layout = {
    'A': [btn(0)], 'B': [{'input_id': 6, 'input_type': 4}], 'Z': [btn(2)], 'L': [btn(9)], 'R': [],
    'C_LEFT': [btn(10)], 'C_RIGHT': [], 'C_DOWN': [btn(1)], 'C_UP': [btn(3)], 'START': [btn(6)],
    'DPAD_LEFT': [btn(13)], 'DPAD_RIGHT': [btn(14)], 'DPAD_UP': [btn(11)], 'DPAD_DOWN': [btn(12)],
}
for k, v in layout.items():
    c[k] = (v + [none, none])[:2]
# Keyboard and mouse (SDL scancodes; mouse 1 left, 2 middle, 3 right). Shift sprint and C view swap are fixed in input.cpp.
key = lambda i: {'input_id': i, 'input_type': 1}
mouse = lambda i: {'input_id': i, 'input_type': 2}
kb = j['keyboard']
keys = {
    'A': [key(44)], 'B': [mouse(1), key(42)], 'L': [], 'R': [], 'Z': [key(20)], 'START': [key(40), key(12)],
    'C_LEFT': [mouse(2), key(21)], 'C_RIGHT': [], 'C_UP': [key(16), key(43)], 'C_DOWN': [key(8), key(224)],
    'DPAD_LEFT': [key(80)], 'DPAD_RIGHT': [key(79)], 'DPAD_UP': [key(82)], 'DPAD_DOWN': [key(81)],
}
for k, v in keys.items():
    kb[k] = (v + [none, none])[:2]
json.dump(j, open(p, 'w'), indent=4)
for k in layout:
    print(k, [(x['input_type'], x['input_id']) for x in c[k]])
for k in keys:
    print('kb', k, [(x['input_type'], x['input_id']) for x in kb[k]])
