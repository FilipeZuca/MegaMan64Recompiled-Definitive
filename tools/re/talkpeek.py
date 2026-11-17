# Read-only: during a conversation, prints Mega Man's state and every active entry of the actor/object lists near him
# with its talk handler, to see which list an NPC lives in. usage: python talkpeek.py
import struct, math
from peek import mem

p = mem(0x802049B0, 0x60)
px, py, pz = struct.unpack('>hhh', p[0x14:0x1A])
print('player state', p[8], 'sub', hex(p[9]), 'pos', px, py, pz)
LISTS = [("npc", 0x801FFBE0, 0x3C8, 18), ("multi", 0x801BA0C0, 0x5AC, 4),
         ("object", 0x8020DF00, 0x118, 32), ("static", 0x80206B70, 0x5C, 32)]
for name, base, size, count in LISTS:
    for i in range(count):
        a = mem(base + i * size, 0x20)
        if len(a) < 0x20 or not (a[0] & 3):
            continue
        offs = (0x14, 0x1C, 0x38) if name == 'static' else (0x14,)
        for off in offs:
            x, y, z = struct.unpack('>hhh', mem(base + i * size + off, 6))
            d = math.hypot(x - px, z - pz)
            if d < 1500:
                print('%-6s %2d b0=%02X b6=%02X type=%02X h=%08X off+%X pos=(%d,%d,%d) d=%d' %
                      (name, i, a[0], a[6], a[2], struct.unpack('>I', a[0x10:0x14])[0], off, x, y, z, d))
