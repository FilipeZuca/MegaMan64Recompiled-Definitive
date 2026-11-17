# Read-only peek into the running game's RDRAM. usage: peek.py ADDR:TYPE[:COUNT] ...   TYPE = u8|u16|u32
import ctypes, ctypes.wintypes as w, subprocess, sys, struct

pid = int(subprocess.run(['powershell', '-NoProfile', '-Command',
                          '(Get-Process MegaMan64Recompiled -ErrorAction Stop).Id'],
                         capture_output=True, text=True).stdout.split()[0])
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = w.HANDLE
k.VirtualQueryEx.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k.VirtualQueryEx.restype = ctypes.c_size_t
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                ctypes.POINTER(ctypes.c_size_t)]

class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p),
                ('AllocationProtect', w.DWORD), ('PartitionId', w.WORD), ('RegionSize', ctypes.c_size_t),
                ('State', w.DWORD), ('Protect', w.DWORD), ('Type', w.DWORD)]

h = k.OpenProcess(0x0410, False, pid)
if not h:
    sys.exit('OpenProcess failed')

def read(addr, size):
    buf, n = ctypes.create_string_buffer(size), ctypes.c_size_t()
    return buf.raw[:n.value] if k.ReadProcessMemory(h, addr, buf, size, ctypes.byref(n)) else b''

def find_base():
    pat = bytes.fromhex('001000010100002000040008')  # pad map table at 0x800A8D78, word-swapped
    addr, mbi = 0, MBI()
    while k.VirtualQueryEx(h, addr, ctypes.byref(mbi), ctypes.sizeof(mbi)):
        size = mbi.RegionSize
        if mbi.State == 0x1000 and mbi.Protect in (0x04, 0x02, 0x40) and size <= 1 << 30:
            d, i = read(addr, size), -1
            while (i := d.find(pat, i + 1)) >= 0:
                b = addr + i - 0xA8D78
                # RDRAM base is 64K aligned and the whole 8 MB must be readable (rules out the ROM image copy).
                if b & 0xFFFF == 0 and len(read(b + 0x7FFFFC, 4)) == 4:
                    return b
        addr += size
    sys.exit('RDRAM not found (game not started?)')

base = find_base()

def mem(vaddr, n):
    """n bytes at N64 address vaddr in big-endian order (the recomp stores each 32-bit word swapped)."""
    off = vaddr - 0x80000000
    lo, hi = off & ~3, (off + n + 3) & ~3
    d = read(base + lo, hi - lo)
    be = b''.join(d[i:i + 4][::-1] for i in range(0, len(d), 4))
    return be[off - lo:off - lo + n]

if __name__ == '__main__':
    for spec in sys.argv[1:]:
        p = spec.split(':')
        a, t, c = int(p[0], 16), p[1], int(p[2]) if len(p) > 2 else 1
        sz = {'u8': 1, 'u16': 2, 'u32': 4}[t]
        vals = struct.unpack('>%d%s' % (c, {'u8': 'B', 'u16': 'H', 'u32': 'I'}[t]), mem(a, sz * c))
        print('%08X %s:' % (a, t), ' '.join('%0*X' % (sz * 2, v) for v in vals))
