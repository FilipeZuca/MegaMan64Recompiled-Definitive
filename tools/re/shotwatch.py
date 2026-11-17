# Read-only: logs every new buster shot for its first frames (heading +0x64, position) with Mega Man's yaw, combat
# state, the patch's sAiming / sAimPointValid / sAimPoint. usage: python shotwatch.py SECONDS OUT.txt
import os
import struct, sys, time
from peek import mem
SHOTS, SIZE, PLAYER = 0x802059A0, 0x118, 0x802049B0
def sym(n):
    for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'patches', 'patches.map')):
        if l.split()[-1:] == [n]: return int(l.split()[0], 16)
A_AIM, A_VALID, A_PT = sym('sAiming'), sym('sAimPointValid'), sym('sAimPoint.0')
out = open(sys.argv[2], 'w')
end = time.time() + float(sys.argv[1]); live = {}
def log(s):
    print(s, flush=True); out.write(s + '\n'); out.flush()
while time.time() < end:
    p = mem(PLAYER, 0x180)
    pyaw = struct.unpack('>h', p[0x56:0x58])[0] & 0xFFF
    ppos = struct.unpack('>hhh', p[0x14:0x1A])
    aim = struct.unpack('>I', mem(A_AIM, 4))[0]; valid = mem(A_VALID, 1)[0]
    pt = struct.unpack('>fff', mem(A_PT, 12))
    for i in range(16):
        s = mem(SHOTS + i * SIZE, 0x70)
        if not s[0] & 1:
            live.pop(i, None); continue
        n = live.get(i, 0)
        if n < 24:
            yaw = struct.unpack('>h', s[0x64:0x66])[0]
            pos = struct.unpack('>hhh', s[0x14:0x1A]); vel = struct.unpack('>hh', s[0x44:0x46] + s[0x48:0x4A])
            log('slot%d f%d shotyaw=%d pos=%s vel=%s | pyaw=%d ppos=%s combat=%d sAiming=%d valid=%d aimpt=(%.0f,%.0f,%.0f)' %
                (i, n, yaw, pos, vel, pyaw, ppos, p[0x171], aim, valid, *pt))
        live[i] = n + 1
    time.sleep(1 / 120)
