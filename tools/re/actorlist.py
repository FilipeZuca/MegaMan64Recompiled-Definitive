# Read-only: lists the active entries of the game's actor/object lists with flags, talk handler, position and
# distance to Mega Man, to see which list a given enemy lives in. usage: python actorlist.py
import struct
from peek import mem

LISTS = [("npc/enemy", 0x801FFBE0, 0x3C8, 18), ("multi", 0x801BA0C0, 0x5AC, 4),
         ("object", 0x8020DF00, 0x118, 32), ("static", 0x80206B70, 0x5C, 32)]
p = mem(0x802049B0, 0x60)
px, py, pz = struct.unpack('>hhh', p[0x14:0x1A])
print('player', px, py, pz)
for name, base, size, count in LISTS:
    for i in range(count):
        a = mem(base + i * size, 0x20)
        if len(a) < 0x20 or not (a[0] & a[6] & 1):
            continue
        x, y, z = struct.unpack('>hhh', a[0x14:0x1A])
        talk = struct.unpack('>I', a[0x10:0x14])[0]
        d = ((x - px) ** 2 + (z - pz) ** 2) ** 0.5
        print('%-9s %2d %08X b0=%02X b6=%02X id=%02X%02X talk=%08X pos=(%d,%d,%d) dist=%d' %
              (name, i, base + i * size, a[0], a[6], a[1], a[2], talk, x, y, z, d))
