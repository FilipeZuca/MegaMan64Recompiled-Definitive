import os
import sys, struct, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import btnscan as bs
from peek import mem
SHOTS, SIZE = 0x802059A0, 0x118
while not bs.settle(): pass
bs.put('gDbgPadOn', 1)
try:
    cam = struct.unpack('>i', mem(0x80162844, 4))[0] & 0xFFF
    p = bs.player(); yaw = struct.unpack('>h', p[0x56:0x58])[0] & 0xFFF; pos = struct.unpack('>hhh', p[0x14:0x1A])
    print('cam', cam, 'yaw+800', (yaw + 0x800) & 0xFFF, 'player', pos)
    bs.pad(0x4000); bs.wait_frames(2); bs.pad()
    for i in range(8):
        bs.wait_frames(2)
        for k in range(16):
            s = mem(SHOTS + k * SIZE, 0x20)
            if s[0] & 1:
                x, y, z = struct.unpack('>hhh', s[0x14:0x1A])
                print(i, 'shot', (x, y, z), 'heading', int(math.atan2(x - pos[0], z - pos[2]) / (2 * math.pi) * 4096) & 0xFFF)
finally:
    bs.pad(); bs.put('gDbgPadOn', 0)
