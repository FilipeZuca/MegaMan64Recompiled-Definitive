# Second shot delay: tap fire, wait GAP frames, tap again; pose on/off, standing and strafing (forced aim).
import time
import btnscan as bs
from peek import mem
bs.VARS.update({n: bs.sym(n) for n in ('gAimPose', 'gDbgForceAim')})
SHOTS, SIZE = 0x802059A0, 0x118
def count(): return sum(mem(SHOTS + k * SIZE, 1)[0] & 1 for k in range(16))
def fire_wait(st, limit=40):
    n0 = count(); f0 = bs.frame(); bs.pad(0x4000, st)
    while count() <= n0 and bs.frame() - f0 < limit: pass
    d = bs.frame() - f0; bs.pad(0, st); return d
bs.put('gDbgPadOn', 1)
try:
    for move, st in (('stand', (0, 0)), ('strafe', (80, 0)), ('back', (0, -80))):
        for pose in (0, 1):
            res = []
            for gap in (4, 8, 12, 16):
                bs.put('gAimPose', pose); bs.put('gDbgForceAim', 1); bs.pad(0, st); bs.wait_frames(30)
                while count(): bs.wait_frames(2)
                fire_wait(st); bs.wait_frames(gap); res.append('gap%d:%d' % (gap, fire_wait(st)))
                bs.pad(0, (-st[0], -st[1])); bs.wait_frames(20)
            print(move, 'pose=%d' % pose, ' '.join(res), flush=True)
finally:
    bs.pad(); bs.put('gDbgForceAim', 0); bs.put('gDbgPadOn', 0); bs.put('gAimPose', 1)
