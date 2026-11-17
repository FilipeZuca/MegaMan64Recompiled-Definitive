# What each game button does in the pause menu (gDbgPadOn = 1: as the game sees it, no modern_pad). -> menubtn_TAG.png
import sys, struct, time
import btnscan as bs, fastshot as fs
from peek import mem
from PIL import Image
A, B, Z, L, R, CU, CD, CL, CR, START = 0x8000, 0x4000, 0x2000, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1, 0x1000
tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
menu = lambda: struct.unpack('>I', mem(0x801D90C0, 4))[0] != 0
while not bs.settle():
    pass
fs.front(); bs.put('gDbgPadOn', 1); shots = []
def step(name, b):
    bs.pad(b); bs.wait_frames(3); bs.pad(); bs.wait_frames(25)
    p = 'mb_%s_%d.png' % (tag, len(shots)); fs.shot(p); shots.append(p); print(len(shots) - 1, name, 'menu=%s' % menu(), flush=True)
try:
    step('start', START)
    for n, b in [('Z', Z), ('C-Up', CU), ('C-Right', CR), ('R', R)]:
        step(n, b)
    step('A (enter Normal Items)', A)
    for n, b in [('Z', Z), ('C-Up', CU), ('C-Right', CR), ('R', R), ('L', L)]:
        step(n, b)
    for i in range(4):
        if menu(): step('B', B)
    if menu(): step('START', START)
finally:
    bs.pad(); bs.wait_frames(2); bs.put('gDbgPadOn', 0)
ims = [Image.open(p).resize((384, 240)) for p in shots]
sheet = Image.new('RGB', (384 * 5, 240 * ((len(ims) + 4) // 5)))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 5) * 384, (i // 5) * 240))
sheet.save('menubtn_%s.png' % tag)
