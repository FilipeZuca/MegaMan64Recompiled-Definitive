# Pause menu: does the left stick sideways change the page or move the cursor? Prints after each input.
# python menustick.py TAG   (gDbgPadOn = 2: input as bound, through modern_pad). Sheet: menustick_TAG.png
import sys, struct, time
import btnscan as bs, fastshot as fs
from peek import mem
from PIL import Image
START, DR, DL = 0x1000, 0x0100, 0x0200
tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
menu = lambda: struct.unpack('>I', mem(0x801D90C0, 4))[0] != 0
t0 = time.time()
while time.time() - t0 < 60 and not bs.settle():
    pass
fs.front()
bs.put('gDbgPadOn', 2)
shots = []
def step(name, b, stick, n):
    bs.pad(b, stick); bs.wait_frames(n); bs.pad(); bs.wait_frames(20)
    p = 'ms_%s_%d.png' % (tag, len(shots)); fs.shot(p); shots.append((name, p))
    print(name, 'menu=%s' % menu(), flush=True)
try:
    step('start', START, (0, 0), 3)
    step('confirm (circle)', 0x0004, (0, 0), 3)
    step('stick right', 0, (80, 0), 8)
    step('stick right', 0, (80, 0), 8)
    step('stick left', 0, (-80, 0), 8)
    step('dpad right', DR, (0, 0), 3)
    step('dpad left', DL, (0, 0), 3)
    for i in range(3):
        if menu():
            step('back (cross)', 0x8000, (0, 0), 3)
    if menu():
        step('close', START, (0, 0), 3)
finally:
    bs.pad(); bs.wait_frames(2); bs.put('gDbgPadOn', 0)
ims = [Image.open(p).resize((480, 300)) for _, p in shots]
sheet = Image.new('RGB', (480 * 4, 300 * ((len(ims) + 3) // 4)))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 4) * 480, (i // 4) * 300))
sheet.save('menustick_%s.png' % tag)
