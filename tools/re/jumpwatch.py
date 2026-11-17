# Logs frame-to-frame jumps of Mega Man's and the camera's position (a rebase of coordinates makes RT64 interpolate across it).
# usage: python jumpwatch.py [seconds] [threshold]
import struct, sys, time
from peek import mem
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 40
thr = int(sys.argv[2]) if len(sys.argv) > 2 else 60
prev = None
t0 = time.time()
n = 0
while time.time() - t0 < secs:
    p = mem(0x802049C4, 6)
    c = mem(0x802043E0, 6)
    f = struct.unpack('>I', mem(0x8016281C, 4))[0]
    cur = struct.unpack('>hhh', p) + struct.unpack('>hhh', c)
    if prev and cur != prev:
        d = [abs(a - b) for a, b in zip(cur, prev)]
        if max(d) > thr:
            print('t=%.2f frame=%d JUMP player=%s cam=%s delta=%s' % (time.time() - t0, f, cur[:3], cur[3:], d), flush=True)
            n += 1
    prev = cur
print('done, jumps:', n)
