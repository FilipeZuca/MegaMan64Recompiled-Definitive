# Read-only: while the player locks/switches focus, logs the focused actor and the flags, distance and camera-relative
# angle of every active entry of the enemy actor list. usage: python focustrace.py SECONDS OUTFILE
import os
import sys, time, struct, math
from peek import mem

def sym(name):
    # Patch symbol addresses move every build: read them from the current map.
    for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'patches', 'patches.map')):
        if line.split()[-1:] == [name]:
            return int(line.split()[0], 16)
TARGET_ADDR, YAW_ADDR = sym('sFocusTarget'), sym('sOrbitYaw')
end = time.time() + float(sys.argv[1])
out = open(sys.argv[2], "w")
last = None
while time.time() < end:
    p = mem(0x802049B0, 0x60)
    px, pz = struct.unpack('>h', p[0x14:0x16])[0], struct.unpack('>h', p[0x18:0x1A])[0]
    target = struct.unpack('>I', mem(TARGET_ADDR, 4))[0]
    yaw = struct.unpack('>i', mem(YAW_ADDR, 4))[0]
    rows = []
    for i in range(18):
        a = mem(0x801FFBE0 + i * 0x3C8, 0x1A)
        if not a[0] & 1:
            continue
        x, z = struct.unpack('>h', a[0x14:0x16])[0], struct.unpack('>h', a[0x18:0x1A])[0]
        dx, dz = x - px, z - pz
        ang = (int(math.atan2(dx, dz) / math.pi * 2048) - int(yaw) + 2048) % 4096 - 2048
        rows.append('%d:%02X/%02X t%02X d%d a%d' % (i, a[0], a[6], a[2], math.hypot(dx, dz), ang))
    tgt = (target - 0x801FFBE0) // 0x3C8 if target else -1
    pyaw = struct.unpack('>h', p[0x56:0x58])[0] & 0xFFF
    line = 'tgt=%d yaw=%d pyaw=%d pos=%d,%d | %s' % (tgt, int(yaw) & 0xFFF, pyaw, px, pz, '  '.join(rows))
    key = (tgt, ' '.join(r.split(' d')[0] for r in rows))
    if key != last:
        print('%6.2f %s' % (time.time() % 1000, line), file=out, flush=True)
        last = key
    time.sleep(1 / 30)
