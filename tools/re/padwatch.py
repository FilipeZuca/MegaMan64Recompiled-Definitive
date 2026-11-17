# Logs every change of the game's controller 1 state (buttons u16, stick x/y, and the bytes after them) with a timestamp,
# to see what the game is given while a menu or a Yes/No choice won't respond. usage: python padwatch.py [seconds]
import struct, sys, time
from peek import mem

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 20
t0 = time.time()
last = None
print('Press right / left / up / down on the stick, then the d-pad, then the confirm button, for %d s...' % secs)
while time.time() - t0 < secs:
    raw = mem(0x801BC380, 16)
    if raw != last:
        b = struct.unpack('>H', raw[0:2])[0]
        sx, sy = struct.unpack('>bb', raw[2:4])
        print('t=%.2f buttons=%04X stick=(%d,%d) rest=%s' % (time.time() - t0, b, sx, sy, raw[4:].hex()), flush=True)
        last = raw
    time.sleep(0.005)
print('done')
