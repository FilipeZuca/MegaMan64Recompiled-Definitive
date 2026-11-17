# Per game frame trace of the player bytes that change when firing (btnscan "tap B"), during a B tap, B hold and after.
import struct
import btnscan as bs
from peek import mem
OFFS = [0x96, 0x97, 0x9A, 0x9B, 0xA5, 0xA6, 0xA7, 0xAC, 0xAD, 0xFA, 0xFE, 0x101, 0x102, 0x104, 0x105, 0x107, 0x174, 0x175, 0x178]
def row(tag):
    p = bs.player()
    print('%-6s f=%d ' % (tag, bs.frame()) + ' '.join('%03X:%02X' % (o, p[o]) for o in OFFS), flush=True)
bs.settle(); bs.put('gDbgPadOn', 1)
try:
    for tag, b, n in (('idle', 0, 3), ('B', 0x4000, 2), ('rel', 0, 25), ('holdB', 0x4000, 30), ('rel2', 0, 20)):
        bs.pad(b)
        for _ in range(n):
            f = bs.frame()
            while bs.frame() == f: pass
            row(tag)
finally:
    bs.pad(); bs.put('gDbgPadOn', 0)
