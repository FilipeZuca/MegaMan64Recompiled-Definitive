# Shots fired for one fire tap standing, walking forward and backpedaling (gDbgPadOn = 1, the game's own controls).
import sys, struct, btnscan as bs
from peek import mem
SHOTS, SIZE, B = 0x802059A0, 0x118, 0x4000
while not bs.settle(): pass
count = lambda: sum(1 for i in range(16) if mem(SHOTS + i * SIZE, 1)[0] & 1)
bs.put('gDbgPadOn', 1)
try:
    for name, stick in (('standing', (0, 0)), ('forward', (0, 80)), ('backpedal', (0, -80))):
        bs.pad(0, stick); bs.wait_frames(15)
        best = 0
        bs.pad(B, stick); bs.wait_frames(3); bs.pad(0, stick)
        for _ in range(15):
            bs.wait_frames(1); best = max(best, count())
        print('%-10s shots %d mode %d' % (name, best, bs.player()[0x112]))
        bs.pad(); bs.wait_frames(20)
finally:
    bs.pad(); bs.wait_frames(2); bs.put('gDbgPadOn', 0)
