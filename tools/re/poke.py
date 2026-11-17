# Writes a 32-bit value into the running game's RDRAM (debug switches in the patches). usage: poke.py SYMBOL|ADDR VALUE
# Only for patch debug variables; never for game state.
import os
import ctypes, sys
import peek

def sym(name):
    for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'patches', 'patches.map')):
        if line.split()[-1:] == [name]:
            return int(line.split()[0], 16)
    sys.exit('symbol not found: ' + name)

target = sys.argv[1]
addr = int(target, 16) if target.startswith('0x') else sym(target)
value = int(sys.argv[2], 0) & 0xFFFFFFFF
h = peek.k.OpenProcess(0x0438, False, peek.pid)  # VM_OPERATION | VM_READ | VM_WRITE | QUERY_INFORMATION
# The recomp keeps each 32-bit word in host byte order, so an aligned word is written as a little-endian int.
buf = ctypes.c_uint32(value)
n = ctypes.c_size_t()
ok = peek.k.WriteProcessMemory(h, ctypes.c_void_p(peek.base + addr - 0x80000000), ctypes.byref(buf), 4, ctypes.byref(n))
print('wrote' if ok else 'failed', hex(addr), value)
