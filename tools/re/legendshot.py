# Pause menu legend: open pause (START) and Normal Items, prints; opens the map (C-Up as the game sees it), print.
import sys, struct
import btnscan as bs, fastshot as fs
from peek import mem
from PIL import Image
tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
menu = lambda: struct.unpack('>I', mem(0x801D90C0, 4))[0] != 0
while not bs.settle():
    pass
fs.front(); bs.put('gDbgPadOn', 2); shots = []
def step(name, b):
    bs.pad(b); bs.wait_frames(3); bs.pad(); bs.wait_frames(25)
    p = 'lg_%s_%d.png' % (tag, len(shots)); fs.shot(p); shots.append(p); print(len(shots) - 1, name, menu(), flush=True)
try:
    step('pause', 0x1000)
    step('circle: normal items', 0x0004)
    step('square (nothing)', 0x2000)
    step('cross back', 0x8000)
    step('cross close?', 0x8000)
    if menu(): step('start close', 0x1000)
    step('triangle: map', 0x0008)
    if menu(): step('triangle close', 0x0008)
    if menu(): step('cross close', 0x8000)
finally:
    bs.pad(); bs.wait_frames(2); bs.put('gDbgPadOn', 0)
ims = [Image.open(p).resize((640, 400)) for p in shots]
sheet = Image.new('RGB', (640 * 3, 400 * ((len(ims) + 2) // 3)))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 3) * 640, (i // 3) * 400))
sheet.save('legend_%s.png' % tag)
