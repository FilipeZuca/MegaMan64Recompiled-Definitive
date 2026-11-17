# Frames from pressing fire to the shot appearing: no pose, and pose held at different keyframes (forced aim).
import struct, time
import btnscan as bs
from peek import mem
bs.VARS.update({n: bs.sym(n) for n in ('gAimPose', 'gDbgForceAim', 'gAimPoseFrame')})
SHOTS, SIZE = 0x802059A0, 0x118
live = lambda: sum(mem(SHOTS + k * SIZE, 1)[0] & 1 for k in range(16))
t0 = time.time()
while not bs.settle() and time.time() - t0 < 60: pass
bs.put('gDbgPadOn', 1)
try:
    for pose, frame in ((0, 6), (1, 6), (1, 9), (1, 12)):
        for rep in range(3):
            bs.put('gAimPose', pose); bs.put('gAimPoseFrame', frame); bs.put('gDbgForceAim', 1)
            bs.pad(); bs.wait_frames(40)
            while live(): bs.wait_frames(2)
            f0 = bs.frame(); bs.pad(0x4000)
            while not live() and bs.frame() - f0 < 40: pass
            d = bs.frame() - f0; bs.pad(); bs.wait_frames(3)
            print('pose=%d hold=%d delay=%d frames' % (pose, frame, d), flush=True)
finally:
    bs.pad(); bs.put('gDbgForceAim', 0); bs.put('gDbgPadOn', 0); bs.put('gAimPoseFrame', 6); bs.put('gAimPose', 1)
