import os
import sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import btnscan as bs
from peek import mem
SHOTS, SIZE = 0x802059A0, 0x118
cnt = lambda: sum(mem(SHOTS + k * SIZE, 1)[0] & 1 for k in range(16))
bs.VARS.update({n: bs.sym(n) for n in ('gDbgForceMode',)})
mode = int(sys.argv[1])
while not bs.settle(): pass
bs.put('gDbgPadOn', 1); bs.put('gDbgForceMode', mode)
try:
    btn, st = (0x20, (0, 0)) if mode == 2 else (0, (-80, 0))
    bs.pad(btn, st); bs.wait_frames(15)
    for k in range(2):
        bs.pad(btn | 0x4000, st); bs.wait_frames(2); bs.pad(btn, st)
        out = []
        for i in range(8):
            bs.wait_frames(1); p = bs.player(); out.append('%02X/%d' % (p[0x174], cnt()))
        print('press', k, ' '.join(out), flush=True)
    bs.pad(); bs.wait_frames(20)
finally:
    bs.pad(); bs.put('gDbgForceMode', -1); bs.put('gDbgPadOn', 0)
