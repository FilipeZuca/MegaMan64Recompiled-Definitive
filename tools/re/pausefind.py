# Finds RAM bytes that flag the pause menu: samples RDRAM while playing, opens the pause with an injected START
# (gDbgPadOn), samples again, closes it, samples again. Prints bytes stable in each phase that differ only while paused.
# usage: python pausefind.py [BUTTON_HEX=1000]
import sys, time
import numpy as np
import peek
import btnscan as bs  # reuses the injection helpers (importing runs no tests: guarded below)

LO, SIZE = 0x80100000, 0x200000
btn = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x1000

def sample(n=6):
    out = []
    for _ in range(n):
        raw = np.frombuffer(peek.read(peek.base + LO - 0x80000000, SIZE), dtype=np.uint8)
        out.append(raw.reshape(-1, 4)[:, ::-1].reshape(-1))  # big-endian byte order
        bs.wait_frames(3)
    return np.array(out)

bs.put('gDbgPadOn', 1)
try:
    bs.pad()
    bs.wait_frames(5)
    play = sample()
    bs.pad(btn); bs.wait_frames(3); bs.pad(); bs.wait_frames(30)
    paused = sample()
    bs.pad(btn); bs.wait_frames(3); bs.pad(); bs.wait_frames(30)
    after = sample()
finally:
    bs.pad()
    bs.put('gDbgPadOn', 0)

stable = lambda a: (a == a[0]).all(axis=0)
mask = stable(play) & stable(paused) & stable(after) & (play[0] == after[0]) & (play[0] != paused[0])
idx = np.nonzero(mask)[0]
print(len(idx), 'candidates')
for i in idx:
    print('%08X play=%02X paused=%02X' % (LO + i, play[0][i], paused[0][i]))
