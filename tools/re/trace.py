# Read-only: logs lock-on related state changes from the running game. usage: trace.py SECONDS
import sys, time, struct
from peek import mem

def snap():
    raw = struct.unpack('>H', mem(0x801BC380, 2))[0]
    held, = struct.unpack('>H', mem(0x801D87C2, 2))
    p = mem(0x802049B0, 0x180)
    return {
        'raw': raw, 'held': held,
        'state': p[8], 'sub': p[9], 'pheld': struct.unpack('>H', p[0x11C:0x11E])[0],
        'inflags': p[0xBD], 'combat': p[0x171],
        'target': struct.unpack('>I', mem(0x800AE498, 4))[0],
        'hud': struct.unpack('>I', mem(0x80206B30, 4))[0],
        'bar2': mem(0x801BC684, 1)[0],
        'near': struct.unpack('>I', mem(0x801FEE34, 4))[0],
    }

end, last, seen200 = time.time() + float(sys.argv[1]), None, 0
while time.time() < end:
    try:
        s = snap()
    except Exception as e:  # game closed
        print('read failed:', e); break
    # Only lock-on relevant changes, so minutes of walking around stay quiet.
    key = (s['held'] & 0x300, s['sub'] == 0xE, s['target'] != 0, s['hud'], s['bar2'], s['combat'], s['near'] > 0)
    if key != last:
        print('%6.2f ' % (time.time() % 1000) + ' '.join('%s=%X' % kv for kv in s.items()), flush=True)
        last = key
    if s['held'] & 0x200:
        seen200 += 1
        if seen200 > 150:  # ~3 s of lock-on button data is enough
            break
    time.sleep(0.02)
print('done, frames with 0x200 held:', seen200)
