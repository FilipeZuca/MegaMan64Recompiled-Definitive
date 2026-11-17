# Forced aim, one fire press: per game frame B layer counter, shot count, screenshot; contact sheet.
import os
import sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import btnscan as bs, fastshot as fs
from peek import mem
from PIL import Image
OUT = os.path.dirname(os.path.abspath(__file__)) + os.sep
mode = sys.argv[1]; st = (0, 80) if mode == 'walk' else (0, 0)
bs.VARS.update({n: bs.sym(n) for n in ('gDbgForceAim',)})
SHOTS, SIZE = 0x802059A0, 0x118
cnt = lambda: sum(mem(SHOTS + k * SIZE, 1)[0] & 1 for k in range(16))
while not bs.settle(): pass
fs.front(); bs.put('gDbgPadOn', 1)
names = []
try:
    bs.put('gDbgForceAim', 1); bs.pad(0, st); bs.wait_frames(15)
    bs.pad(0x4000, st)
    for i in range(18):
        f = bs.frame()
        while bs.frame() == f: pass
        if i == 1: bs.pad(0, st)
        p = bs.player()
        print(i, 'f=%d B=%08X c%02X A c%02X 174=%02X shots=%d' % (bs.frame(), struct.unpack('>I', p[0xF8:0xFC])[0], p[0x100], p[0xA4], p[0x174], cnt()), flush=True)
        n = OUT + 'af_%s_%02d.png' % (mode, i); fs.shot(n); names.append(n)
finally:
    bs.pad(); bs.put('gDbgForceAim', 0); bs.wait_frames(10)
    if mode == 'walk': bs.pad(0, (0, -80)); bs.wait_frames(30); bs.pad()
    bs.put('gDbgPadOn', 0)
ims = [Image.open(n).crop((900, 250, 1500, 1000)) for n in names[:12]]
s = 0.3; w, h = int(ims[0].width * s), int(ims[0].height * s)
o = Image.new('RGB', (w * 6, h * 2))
for i, im in enumerate(ims): o.paste(im.resize((w, h)), ((i % 6) * w, (i // 6) * h))
o.save(OUT + 'af_%s_sheet.png' % mode)
