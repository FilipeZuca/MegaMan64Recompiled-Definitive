# Checks the modern layout through gDbgPadOn = 2 (buttons injected as bound, then modern_pad runs).
import struct, time
import btnscan as bs
from peek import mem
Z, L, A, START, CD = 0x2000, 0x0020, 0x8000, 0x1000, 0x0004
menu = lambda: struct.unpack('>I', mem(0x801D90C0, 4))[0] != 0
t0 = time.time()
while time.time() - t0 < 60 and not bs.settle():
    pass
bs.put('gDbgPadOn', 2)
try:
    for name, steps in [('square standing', bs.tap(Z)),
                        ('square standing stick right', [(Z, (80, 0), 3), (0, (0, 0), 20)]),
                        ('walk fwd then square', [(0, (0, 80), 15), (Z, (0, 80), 3), (0, (0, 80), 25)]),
                        ('L1 + stick left (strafe)', [(L, (-80, 0), 30)]),
                        ('L1 alone', [(L, (0, 0), 20)]),
                        ('cross = jump', bs.tap(A))]:
        print(bs.run(name, steps).split('\n    bytes')[0], flush=True)
    bs.settle()
    bs.pad(START); bs.wait_frames(3); bs.pad(); bs.wait_frames(30)
    print('pause open:', menu())
    p0 = bs.snap(bs.player())
    bs.pad(A); bs.wait_frames(3); bs.pad(); bs.wait_frames(30)
    print('after cross in pause: menu=%s' % menu())
    if menu():
        bs.pad(START); bs.wait_frames(3); bs.pad(); bs.wait_frames(30)
        print('closed with START: menu=%s' % menu())
    s = bs.snap(bs.player())
    print('state after pause: %d/%X y %d -> %d' % (s['st'], s['sub'], p0['pos'][1], s['pos'][1]))
finally:
    bs.pad(); bs.wait_frames(2); bs.put('gDbgPadOn', 0)
