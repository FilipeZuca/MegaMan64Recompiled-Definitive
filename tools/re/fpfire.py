# First person (gDbgForceMode 2): aim held, walking or standing, one shot; screenshots per frame.
import os
import sys, struct, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import btnscan as bs, fastshot as fs
from PIL import Image
OUT = os.path.dirname(os.path.abspath(__file__)) + os.sep
tag = sys.argv[1]; aim = int(sys.argv[2]); btn = int(sys.argv[3], 0); st = (int(sys.argv[4]), int(sys.argv[5]))
bs.VARS.update({n: bs.sym(n) for n in ('gDbgForceAim', 'gDbgForceMode')})
while not bs.settle(): pass
fs.front(); bs.put('gDbgPadOn', 1); bs.put('gDbgForceMode', 2)
names = []
try:
    bs.wait_frames(10)
    bs.put('gDbgForceAim', aim); bs.pad(btn, st); bs.wait_frames(15)
    bs.pad(btn | 0x4000, st); t0 = time.time()
    for i in range(10):
        if i == 1: bs.pad(btn, st)
        p = bs.player()
        print(i, 'st=%d/%d walk=%d B=%08X c%02X 174=%02X' % (p[8], p[9], p[0x112], struct.unpack('>I', p[0xF8:0xFC])[0], p[0x100], p[0x174]), flush=True)
        n = OUT + 'fp_%s_%02d.png' % (tag, i); fs.shot(n); names.append(n)
    bs.pad(btn, (-st[0], -st[1])); bs.wait_frames(15)
finally:
    bs.pad(); bs.put('gDbgForceAim', 0); bs.wait_frames(5); bs.put('gDbgForceMode', -1); bs.put('gDbgPadOn', 0)
ims = [Image.open(n).crop((200, 300, 1400, 1000)) for n in names]
s = 0.25; w, h = int(ims[0].width * s), int(ims[0].height * s)
o = Image.new('RGB', (w * 5, h * 2))
for i, im in enumerate(ims): o.paste(im.resize((w, h)), ((i % 5) * w, (i // 5) * h))
o.save(OUT + 'fp_%s_sheet.png' % tag)
