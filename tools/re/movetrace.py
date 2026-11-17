# Read-only: logs Mega Man's state/substate, held buttons (raw pad and his own copy), speed, yaw and the side/forward
# velocities while the player moves, aims and shoots. usage: python movetrace.py SECONDS
import sys, time, struct
from peek import mem

end = time.time() + float(sys.argv[1])
out = open(sys.argv[2], "w") if len(sys.argv) > 2 else sys.stdout
last, prev_pos, t0 = None, None, time.time()
while time.time() < end:
    p = mem(0x802049B0, 0x130)
    raw = mem(0x801BC380, 4)
    if len(p) < 0x130 or len(raw) < 4:
        time.sleep(0.05)
        continue
    state, sub = p[8], p[9]
    held = struct.unpack('>H', p[0x11C:0x11E])[0]
    rawb = struct.unpack('>H', raw[0:2])[0]
    sx, sy = struct.unpack('>bb', raw[2:4])
    pos = struct.unpack('>hhh', p[0x14:0x1A])
    yaw = struct.unpack('>h', p[0x56:0x58])[0] & 0xFFF
    vf, vy, vs = struct.unpack('>hhh', p[0x44:0x4A])
    speed = 0
    if prev_pos:
        speed = ((pos[0] - prev_pos[0]) ** 2 + (pos[2] - prev_pos[2]) ** 2) ** 0.5
    prev_pos = pos
    key = (state, sub, held, rawb, round(speed / 4))
    if key != last:
        print('%6.2f st=%d sub=%2X raw=%04X stick=%4d,%4d held=%04X v=(%d,%d,%d) speed=%5.1f yaw=%03X' %
              (time.time() - t0, state, sub, rawb, sx, sy, held, vf, vy, vs, speed, yaw), file=out, flush=True)
        last = key
    time.sleep(1 / 60)
