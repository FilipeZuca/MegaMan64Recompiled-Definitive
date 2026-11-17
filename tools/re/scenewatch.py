# Logs every change in the words around the scene variables while you walk through a door.
# Window 0x801BC3C0..0x801BC480 (scene id 0x801BC438, loaded scene 0x801BC450). usage: python scenewatch.py [seconds]
import struct, sys, time
from peek import mem

BASE, N = 0x801BC3C0, 0xC0
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 120
t0, last = time.time(), None
while time.time() - t0 < secs:
    cur = mem(BASE, N)
    if last is not None and cur != last:
        ch = ['%08X:%s>%s' % (BASE + i, last[i:i + 1].hex(), cur[i:i + 1].hex()) for i in range(N) if cur[i] != last[i]]
        print('t=%.2f %s' % (time.time() - t0, ' '.join(ch)), flush=True)
    last = cur
    time.sleep(0.02)
print('done')
