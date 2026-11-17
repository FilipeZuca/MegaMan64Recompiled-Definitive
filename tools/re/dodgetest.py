# Dodge (Square/Q = N64 Z through modern_pad, gDbgPadOn = 2) in each stick direction: compares the hop's travel
# angle with the camera-relative direction asked for.
import math, struct, time, subprocess
import btnscan as bs
from peek import mem
def ang(dx, dz):  # camera yaw convention (ratan2(x, z)), 0..4095
    return int(round(math.atan2(dx, dz) / (2 * math.pi) * 4096)) & 0xFFF
t0 = time.time()
while not bs.settle() and time.time() - t0 < 60: pass
bs.put('gDbgPadOn', 2)
try:
    for name, st in (('right', (80, 0)), ('left', (-80, 0)), ('fwd', (0, 80)), ('back', (0, -80)), ('left', (-80, 0)), ('right', (80, 0)), ('diag', (57, 57)), ('-diag', (-57, -57)), ('none', (0, 0)), ('back', (0, -80))):
        bs.settle()
        cam = struct.unpack('>i', mem(0x80162844, 4))[0] & 0xFFF
        p = bs.player(); yaw0 = struct.unpack('>h', p[0x56:0x58])[0] & 0xFFF; pos0 = struct.unpack('>hhh', p[0x14:0x1A])
        want = (yaw0 + 0x800) & 0xFFF if st == (0, 0) else (cam + ang(st[0], st[1])) & 0xFFF
        bs.pad(0x2000, st); bs.wait_frames(2); bs.pad(0, (0, 0)); bs.wait_frames(30)
        s = bs.snap(bs.player()); dx, dz = s['pos'][0] - pos0[0], s['pos'][2] - pos0[2]
        got = ang(dx, dz); err = ((got - want + 0x800) & 0xFFF) - 0x800
        print('%-5s want=%4d got=%4d err=%+5d dist=%d' % (name, want, got, err, int(math.hypot(dx, dz))), flush=True)
finally:
    bs.pad(); bs.put('gDbgPadOn', 0)
print('alive', bool(subprocess.run(['powershell','-NoProfile','-Command','(Get-Process MegaMan64Recompiled -ErrorAction SilentlyContinue).Id'],capture_output=True,text=True).stdout.strip()))
