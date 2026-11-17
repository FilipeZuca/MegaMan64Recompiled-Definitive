# Times the game's frame counter (30 Hz) and reports slow frames with Mega Man's z, to see if crossing the world loop stalls.
# usage: python framegap.py [seconds] [slow_ms]
import struct, sys, time
from peek import mem
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 60
slow = float(sys.argv[2]) if len(sys.argv) > 2 else 50
last = None
lastt = time.perf_counter()
t0 = lastt
n = frames = 0
worst = 0
prevz = None
while time.perf_counter() - t0 < secs:
    f = struct.unpack('>I', mem(0x8016281C, 4))[0]
    z = struct.unpack('>h', mem(0x802049C8, 2))[0]
    now = time.perf_counter()
    if f != last:
        if last is not None:
            gap = (now - lastt) * 1000
            frames += 1
            worst = max(worst, gap)
            wrap = prevz is not None and abs(z - prevz) > 3000
            if gap > slow or wrap:
                print('t=%.2f frame=%d gap=%.0fms z=%d%s' % (now - t0, f, gap, z, '  <-- WRAP' if wrap else ''), flush=True)
                n += 1
        last, lastt, prevz = f, now, z
print('frames', frames, 'slow/wrap events', n, 'worst %.0fms' % worst)
