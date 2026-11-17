# Teleports Mega Man to a bookmark or to coordinates, inside the area that is loaded (it can't change areas).
# usage: python goto.py 3            -> mark #3 from glitches.txt (written by mark.py)
#        python goto.py 1688 -1 14968 -> x y z
#        python goto.py list         -> shows the bookmarks
# Writes the s16 position (+0x14) and the real 16.16 position (+0x1C/+0x20/+0x24) and stops his velocity.
import ctypes, os, re, struct, sys
import peek

HERE = os.path.dirname(os.path.abspath(__file__))
PLAYER = 0x802049B0


def marks():
    path = os.path.join(HERE, 'glitches.txt')
    out = {}
    if os.path.exists(path):
        for line in open(path):
            m = re.match(r'#(\d+) (\S+ \S+) pos=(-?\d+),(-?\d+),(-?\d+).*?shot=\S+\s*(.*)', line)
            if m:
                out[int(m.group(1))] = (int(m.group(3)), int(m.group(4)), int(m.group(5)), m.group(2), m.group(6))
    return out


h = peek.k.OpenProcess(0x0438, False, peek.pid)


def write32(addr, value):
    """Writes one aligned 32-bit word the way poke.py does (the recomp stores each word in host byte order)."""
    buf, n = ctypes.c_uint32(value & 0xFFFFFFFF), ctypes.c_size_t()
    peek.k.WriteProcessMemory(h, ctypes.c_void_p(peek.base + addr - 0x80000000), ctypes.byref(buf), 4, ctypes.byref(n))


def set16(addr, value):
    word = addr & ~3
    cur = struct.unpack('>I', peek.mem(word, 4))[0]
    if addr & 2:
        cur = (cur & 0xFFFF0000) | (value & 0xFFFF)
    else:
        cur = (cur & 0x0000FFFF) | ((value & 0xFFFF) << 16)
    write32(word, cur)


def teleport(x, y, z):
    set16(PLAYER + 0x14, x)
    set16(PLAYER + 0x16, y)
    set16(PLAYER + 0x18, z)
    write32(PLAYER + 0x1C, (x & 0xFFFF) << 16)
    write32(PLAYER + 0x20, (y & 0xFFFF) << 16)
    write32(PLAYER + 0x24, (z & 0xFFFF) << 16)
    write32(PLAYER + 0x44, 0)  # velocity forward


args = sys.argv[1:]
if not args or args[0] == 'list':
    for n, (x, y, z, when, note) in sorted(marks().items()):
        print('#%03d %s pos=%d,%d,%d  %s' % (n, when, x, y, z, note))
    sys.exit(0)
if len(args) == 1:
    mk = marks()
    n = int(args[0])
    if n not in mk:
        sys.exit('no mark #%d (python goto.py list)' % n)
    x, y, z = mk[n][:3]
else:
    x, y, z = map(int, args[:3])
before = struct.unpack('>hhh', peek.mem(PLAYER + 0x14, 6))
teleport(x, y, z)
after = struct.unpack('>hhh', peek.mem(PLAYER + 0x14, 6))
print('from', before, 'to', after)
