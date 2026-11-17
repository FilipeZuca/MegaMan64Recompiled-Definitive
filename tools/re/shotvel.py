# Fires (injected) and dumps a flying shot twice, 1 game frame apart, looking for s32 fields equal to the per-frame
# displacement (velocity) in 16.16 or integer units.
import struct
import btnscan as bs
from peek import mem
SHOTS, SIZE = 0x802059A0, 0x118
bs.settle(); bs.put('gDbgPadOn', 1)
try:
    bs.wait_frames(8); bs.pad(0x4000); bs.wait_frames(3); bs.pad()
    i = [k for k in range(16) if mem(SHOTS + k * SIZE, 1)[0] & 1][0]
    bs.wait_frames(6)
    f0 = bs.frame(); a = mem(SHOTS + i * SIZE, SIZE)
    while bs.frame() == f0: pass
    b = mem(SHOTS + i * SIZE, SIZE)
finally:
    bs.pad(); bs.put('gDbgPadOn', 0)
pa, pb = struct.unpack('>iii', a[0x1C:0x28]), struct.unpack('>iii', b[0x1C:0x28])
d = [pb[k] - pa[k] for k in range(3)]
print('yaw +64:', struct.unpack('>h', a[0x64:0x66])[0], 'delta16.16:', d, 'int:', [x >> 16 for x in d])
for o in range(0, SIZE - 3, 2):
    v32 = struct.unpack('>i', a[o:o+4])[0]; v16 = struct.unpack('>h', a[o:o+2])[0]
    for k, dd in enumerate(d):
        if dd and (abs(v32 - dd) <= abs(dd) // 50 or (v16 and abs(v16 - (dd >> 16)) <= 1 and abs(dd >> 16) > 3)):
            print('candidate +%03X s32=%d s16=%d ~ axis %d' % (o, v32, v16, k))
print(' '.join('%03X:%d' % (o, struct.unpack('>i', a[o:o+4])[0]) for o in range(0x28, 0x118, 4) if a[o:o+4] != b'\0\0\0\0'))
