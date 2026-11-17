# Button scan: drives controller 1 through the patch switch gDbgPadOn (mouse_camera.c) and logs what Mega Man does for
# each button and combo (state/substate path, jump height, displacement, yaw, player bytes that changed).
# usage: python btnscan.py [OUT.txt] [only-test-name-substring]   (game open, Mega Man standing in an open area)
import os
import ctypes, struct, sys, time
import peek
from peek import mem

PLAYER, PAD, FRAME = 0x802049B0, 0x801BC380, 0x8016281C
A, B, Z, START = 0x8000, 0x4000, 0x2000, 0x1000
DU, DD, DL, DR = 0x0800, 0x0400, 0x0200, 0x0100
L, R, CU, CD, CL, CR = 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001
NAMES = {A: 'A', B: 'B', Z: 'Z', START: 'START', DU: 'DU', DD: 'DD', DL: 'DL', DR: 'DR', L: 'L', R: 'R',
         CU: 'CU', CD: 'CD', CL: 'CL', CR: 'CR'}
FWD, BACK, LEFT, RIGHT = (0, 80), (0, -80), (-80, 0), (80, 0)

def sym(name):
    for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'patches', 'patches.map')):
        if line.split()[-1:] == [name]:
            return int(line.split()[0], 16)
    sys.exit('symbol not found (rebuild with the gDbgPad patch): ' + name)

VARS = {n: sym(n) for n in ('gDbgPadOn', 'gDbgPadButtons', 'gDbgPadX', 'gDbgPadY')}
hw = peek.k.OpenProcess(0x0438, False, peek.pid)

def put(name, value):
    buf, n = ctypes.c_uint32(value & 0xFFFFFFFF), ctypes.c_size_t()
    peek.k.WriteProcessMemory(hw, ctypes.c_void_p(peek.base + VARS[name] - 0x80000000), ctypes.byref(buf), 4,
                              ctypes.byref(n))

def pad(buttons=0, stick=(0, 0)):
    put('gDbgPadButtons', buttons)
    put('gDbgPadX', stick[0])
    put('gDbgPadY', stick[1])

def frame():
    return struct.unpack('>I', mem(FRAME, 4))[0]

def player():
    return mem(PLAYER, 0x200)

def snap(p):
    return {'st': p[8], 'sub': p[9], 'walk': p[0x112], 'pos': struct.unpack('>hhh', p[0x14:0x1A]),
            'yaw': struct.unpack('>h', p[0x56:0x58])[0] & 0xFFF, 'held': struct.unpack('>H', p[0x11C:0x11E])[0]}

def wait_frames(n):
    f0 = frame()
    t0 = time.time()
    while frame() - f0 < n and time.time() - t0 < n / 15 + 1:
        time.sleep(0.004)

def settle():
    """Neutral pad until Mega Man stands still (state 1/0) for a few frames, at most 4 s."""
    pad()
    t0, still = time.time(), 0
    while time.time() - t0 < 4:
        s = snap(player())
        still = still + 1 if (s['st'], s['sub']) == (1, 0) else 0
        if still >= 6:
            return True
        time.sleep(0.03)
    return False

# A test is a list of steps (buttons, stick, game frames). The last step is always a neutral pad for the landing.
def tap(b, stick=(0, 0)):
    return [(b, stick, 3)]

def hold(b, n=40, stick=(0, 0)):
    return [(b, stick, n)]

TESTS = []
for b in (A, B, Z, R, L, CL, CD, CR, DU, DD, DL, DR):
    TESTS.append(('tap ' + NAMES[b], tap(b)))
    TESTS.append(('hold ' + NAMES[b], hold(b)))
for name, st in (('fwd', FWD), ('back', BACK), ('left', LEFT), ('right', RIGHT)):
    TESTS.append(('walk %s + A' % name, [(0, st, 12), (A, st, 3), (0, st, 30)]))
    TESTS.append(('walk %s + B' % name, [(0, st, 12), (B, st, 3), (0, st, 20)]))
    TESTS.append(('stand %s+A same frame' % name, [(A, st, 3), (0, st, 30)]))
for s in (Z, R, L, CR):
    for name, st in (('left', LEFT), ('right', RIGHT), ('back', BACK), ('fwd', FWD)):
        TESTS.append(('%s + %s + A' % (NAMES[s], name), [(s, st, 12), (s | A, st, 3), (s, st, 30)]))
    TESTS.append(('%s + A (no stick)' % NAMES[s], [(s, (0, 0), 12), (s | A, (0, 0), 3), (s, (0, 0), 30)]))
    TESTS.append(('%s + B' % NAMES[s], [(s, (0, 0), 12), (s | B, (0, 0), 3), (s, (0, 0), 20)]))
TESTS += [
    ('Z+R together', hold(Z | R)),
    ('L+R together', hold(L | R)),
    ('A then A in air', [(A, (0, 0), 3), (0, (0, 0), 6), (A, (0, 0), 3), (0, (0, 0), 30)]),
    ('A then B in air', [(A, (0, 0), 3), (0, (0, 0), 6), (B, (0, 0), 3), (0, (0, 0), 30)]),
    ('A then CL in air', [(A, (0, 0), 3), (0, (0, 0), 6), (CL, (0, 0), 3), (0, (0, 0), 30)]),
    ('A hold long', hold(A, 40)),
    ('B hold 90 (charge?)', hold(B, 90)),
    ('CL hold 90', hold(CL, 90)),
    ('double tap fwd', [(0, FWD, 3), (0, (0, 0), 3), (0, FWD, 20)]),
    ('double tap back', [(0, BACK, 3), (0, (0, 0), 3), (0, BACK, 20)]),
    ('double tap left', [(0, LEFT, 3), (0, (0, 0), 3), (0, LEFT, 20)]),
    ('fwd then back + A', [(0, FWD, 12), (A, BACK, 3), (0, BACK, 30)]),
    ('A+B same frame', tap(A | B)),
    ('B+CL same frame', tap(B | CL)),
    ('CD+A', tap(CD | A)),
    ('back hold 30 + A', [(0, BACK, 30), (A, BACK, 3), (0, BACK, 30)]),
    # Menus last: they may leave the game on another screen.
    ('tap CU (map?)', tap(CU) + [(0, (0, 0), 40)] + tap(CU)),
]

def run(name, steps):
    ok = settle()
    p0 = player()
    s0 = snap(p0)
    path, seen = [], {}
    ymin = ymax = s0['pos'][1]
    f_start = frame()
    full = steps + [(0, (0, 0), 25)]
    for buttons, stick, frames in full:
        pad(buttons, stick)
        f0 = frame()
        t0 = time.time()
        while frame() - f0 < frames and time.time() - t0 < frames / 15 + 1:
            p = player()
            s = snap(p)
            key = (s['st'], s['sub'], s['walk'])
            if not path or path[-1] != key:
                path.append(key)
            ymax = max(ymax, s['pos'][1])
            ymin = min(ymin, s['pos'][1])
            for off in range(0x200):
                if off in (0x14, 0x15, 0x16, 0x17, 0x18, 0x19) or 0x1C <= off < 0x28:
                    continue
                if p[off] != p0[off]:
                    seen.setdefault(off, set()).add(p[off])
            time.sleep(0.004)
    s1 = snap(player())
    dx, dz = s1['pos'][0] - s0['pos'][0], s1['pos'][2] - s0['pos'][2]
    dyaw = ((s1['yaw'] - s0['yaw'] + 0x800) & 0xFFF) - 0x800
    path_s = ' > '.join('%d/%X%s' % (a, b, '/w%d' % c if c else '') for a, b, c in path)
    bytes_s = ' '.join('+%03X:%s' % (o, ','.join('%02X' % v for v in sorted(vs)[:4]))
                       for o, vs in sorted(seen.items()) if len(vs) <= 6)[:400]
    return ('%-28s %s rise=%-4d move=(%d,%d) yaw%+d frames=%d\n    path: %s\n    bytes: %s' %
            (name, '' if ok else '[not idle]', s0['pos'][1] - ymin, dx, dz, dyaw, frame() - f_start, path_s, bytes_s))

if __name__ == '__main__':
    out = open(sys.argv[1] if len(sys.argv) > 1 else 'btnscan.txt', 'w')
    only = sys.argv[2] if len(sys.argv) > 2 else ''
    put('gDbgPadOn', 1)
    try:
        pad()
        wait_frames(5)
        for name, steps in TESTS:
            if only and only not in name:
                continue
            line = run(name, steps)
            print(line, flush=True)
            out.write(line + '\n')
            out.flush()
    finally:
        pad()
        wait_frames(2)
        put('gDbgPadOn', 0)
