# Sets Mega Man's yaw (player +0x56, 12-bit angle) so the stock camera swings round. usage: python yaw.py ANGLE
import ctypes, struct, sys
import peek

h = peek.k.OpenProcess(0x0438, False, peek.pid)
addr = 0x802049B0 + 0x56
word = addr & ~3
cur = bytearray(peek.mem(word, 4))
struct.pack_into('>H', cur, addr - word, int(sys.argv[1]) & 0xFFF)
buf, n = ctypes.c_uint32(struct.unpack('>I', bytes(cur))[0]), ctypes.c_size_t()
peek.k.WriteProcessMemory(h, ctypes.c_void_p(peek.base + word - 0x80000000), ctypes.byref(buf), 4, ctypes.byref(n))
