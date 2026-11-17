# Asks the game to change scene the way a door does: scene id (0x801BC438), entrance (0x801BC439), transition flag 0x80
# (0x801BC46A, see func_8005D4CC). usage: python warp.py SCENE [ENTRANCE]
import ctypes, struct, sys
import peek

h = peek.k.OpenProcess(0x0438, False, peek.pid)


def put8(addr, value):
    word = addr & ~3
    cur = bytearray(peek.mem(word, 4))
    cur[addr - word] = value & 0xFF
    buf, n = ctypes.c_uint32(struct.unpack('>I', bytes(cur))[0]), ctypes.c_size_t()
    peek.k.WriteProcessMemory(h, ctypes.c_void_p(peek.base + word - 0x80000000), ctypes.byref(buf), 4, ctypes.byref(n))


scene = int(sys.argv[1])
ent = int(sys.argv[2]) if len(sys.argv) > 2 else 0
print('before: scene %d entrance %d loaded %d' % (peek.mem(0x801BC438, 1)[0], peek.mem(0x801BC439, 1)[0], struct.unpack('>H', peek.mem(0x801BC450, 2))[0]))
put8(0x801BC438, scene)
put8(0x801BC439, ent)
put8(0x801BC46A, 0x80)
print('requested scene %d entrance %d' % (scene, ent))
