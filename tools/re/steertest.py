# Aim movement test (forced aim): direction of travel vs asked, upper body animation, screenshots, fire delay.
import struct, subprocess, time, math, sys
import btnscan as bs
from peek import mem
bs.VARS.update({n: bs.sym(n) for n in ('gDbgForceAim',)})
SHOTS, SIZE = 0x802059A0, 0x118
cnt = lambda: sum(mem(SHOTS + k * SIZE, 1)[0] & 1 for k in range(16))
tag = sys.argv[1]
t0 = time.time()
while not bs.settle() and time.time() - t0 < 60: pass
bs.put('gDbgPadOn', 1); bs.put('gDbgForceAim', 1)
# (name, raw N64 buttons, stick, asked camera-relative angle as (x, y))
tests = [('fwd', 0, (0, 80), (0, 80)), ('back', 0, (0, -80), (0, -80)), ('left', 0x20, (-80, 0), (-80, 0)), ('right', 0x10, (80, 0), (80, 0)),
         ('backR', 0x10, (57, -57), (80, -80)), ('fwdL', 0x20, (-57, 57), (-80, 80))]
try:
    for name, b, st, ask in tests:
        cam = struct.unpack('>i', mem(0x80162844, 4))[0] & 0xFFF; pos0 = struct.unpack('>hhh', bs.player()[0x14:0x1A])
        bs.pad(b, st); bs.wait_frames(12)
        p = bs.player(); pos = struct.unpack('>hhh', p[0x14:0x1A]); dx, dz = pos[0] - pos0[0], pos[2] - pos0[2]
        want = (cam + int(round(math.atan2(ask[0], ask[1]) / (2 * math.pi) * 4096))) & 0xFFF
        got = int(round(math.atan2(dx, dz) / (2 * math.pi) * 4096)) & 0xFFF
        n0 = cnt(); f0 = bs.frame(); bs.pad(b | 0x4000, st)
        while cnt() <= n0 and bs.frame() - f0 < 30: pass
        delay = bs.frame() - f0; bs.pad(b, st); bs.wait_frames(2)
        subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'winshot.ps1', '%s_%s.png' % (tag, name)], capture_output=True)
        p = bs.player()
        print('%-6s want=%4d got=%4d err=%+5d dist=%d walk=%d B=%08X fire=%d frames' % (name, want, got, ((got - want + 0x800) & 0xFFF) - 0x800,
              int(math.hypot(dx, dz)), p[0x112], struct.unpack('>I', p[0xF8:0xFC])[0], delay), flush=True)
        bs.pad(b, (-st[0], -st[1])); bs.wait_frames(12); bs.pad(); bs.wait_frames(6)
finally:
    bs.pad(); bs.put('gDbgForceAim', 0); bs.put('gDbgPadOn', 0)
print('alive', bool(subprocess.run(['powershell', '-NoProfile', '-Command', '(Get-Process MegaMan64Recompiled -ErrorAction SilentlyContinue).Id'], capture_output=True, text=True).stdout.strip()))
from PIL import Image
ims = [Image.open('%s_%s.png' % (tag, t[0])) for t in tests]; s = 0.22
w, h = int(ims[0].width * s), int(ims[0].height * s); o = Image.new('RGB', (w * 3, h * 2))
for i, im in enumerate(ims): o.paste(im.resize((w, h)), ((i % 3) * w, (i // 3) * h))
o.save('%s_sheet.png' % tag)
