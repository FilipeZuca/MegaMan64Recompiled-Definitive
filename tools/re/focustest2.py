# Lock-on reach and first person aim correction: stands west of the nearest enemy at several distances, presses F,
# checks the lock (yaw error); then, locked at 1000, turns the view with the mouse: the yaw must move off the target
# and stop near gFocusAdjust (0x90), and come back when turned the other way.
import os
import sys, struct, math, ctypes, subprocess, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import btnscan as bs, fastshot as fs, peek
from peek import mem
OUT = os.path.dirname(os.path.abspath(__file__)) + os.sep
u = ctypes.WinDLL('user32')
bs.VARS.update({n: bs.sym(n) for n in ('gDbgForceMode',)})
def key_f():
    sc = u.MapVirtualKeyW(0x46, 0)
    u.keybd_event(0x46, sc, 0, 0); time.sleep(0.08); u.keybd_event(0x46, sc, 2, 0)
def enemies():
    out = []
    for i in range(18):
        a = mem(0x801FFBE0 + i * 0x3C8, 0x20)
        if a[0] & a[6] & 2 and struct.unpack('>I', a[0x10:0x14])[0] == 0:
            out.append(struct.unpack('>hhh', a[0x14:0x1A]))
    return out
def yaw(): return struct.unpack('>h', bs.player()[0x56:0x58])[0] & 0xFFF
def ang(dx, dz): return int(round(math.atan2(dx, dz) / (2 * math.pi) * 4096)) & 0xFFF
def go(x, y, z, face):
    subprocess.run([sys.executable, 'goto.py', str(x), str(y), str(z)], capture_output=True)
    # face: player yaw (camera yaw - 0x800); set through goto's writer
    import goto
def setyaw(v):
    w = 0x802049B0 + 0x54  # aligned word holding +0x56 (low half)
    cur = bytearray(mem(w, 4)); cur[2:4] = struct.pack('>H', v & 0xFFF)
    buf, n = ctypes.c_uint32(struct.unpack('>I', bytes(cur))[0]), ctypes.c_size_t()
    peek.k.WriteProcessMemory(bs.hw, ctypes.c_void_p(peek.base + w - 0x80000000), ctypes.byref(buf), 4, ctypes.byref(n))

def nearest():
    p = struct.unpack('>hhh', bs.player()[0x14:0x1A])
    return p, min(enemies(), key=lambda q: math.hypot(q[0] - p[0], q[2] - p[2]))

def err():
    p, e = nearest()
    want = (ang(e[0] - p[0], e[2] - p[2]) - 0x800) & 0xFFF
    return ((yaw() - want + 0x800) & 0xFFF) - 0x800, int(math.hypot(e[0] - p[0], e[2] - p[2]))

def mouse(dx, frames):
    for _ in range(frames):
        u.mouse_event(1, dx, 0, 0, 0); bs.wait_frames(1)

while not bs.settle(): pass
fs.front(); bs.put('gDbgForceMode', 2); bs.wait_frames(10)
try:
    _, e = nearest()
    for dist in (2400, 3300, 4200):
        x, z = e[0] - dist, e[2]
        subprocess.run([sys.executable, 'goto.py', str(x), str(e[1]), str(z)], capture_output=True)
        bs.wait_frames(5)
        setyaw(((ang(e[0] - x, e[2] - z) - 0x800) & 0xFFF) + 0x100); bs.wait_frames(5)  # ~22 degrees off
        key_f(); bs.wait_frames(30)
        er, d = err()
        print('dist', d, 'yaw err after F', er, 'locked' if abs(er) < 40 else 'NOT locked', flush=True)
        if abs(er) < 40:
            key_f(); bs.wait_frames(5)
    x, z = e[0] - 1000, e[2]
    subprocess.run([sys.executable, 'goto.py', str(x), str(e[1]), str(z)], capture_output=True)
    bs.wait_frames(5); setyaw((ang(e[0] - x, e[2] - z) - 0x800) & 0xFFF); bs.wait_frames(3)
    key_f(); bs.wait_frames(20)
    print('locked at 1000, err', err()[0], flush=True)
    for label, dx, n in (('mouse right small', 4, 6), ('mouse right more', 6, 20), ('mouse left', -6, 15)):
        mouse(dx, n); bs.wait_frames(8)
        print(label, 'err', err()[0], flush=True)
    fs.shot(OUT + 'focus_adjust.png')
    key_f(); bs.wait_frames(5)
finally:
    bs.put('gDbgForceMode', 0xFFFFFFFF)
