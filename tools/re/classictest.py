# Definitive Features On/Off check (gDbgPadOn = 2, input as bound): Square should hop (substate 9) only with the
# features on; prints gDefinitive and a screenshot (crosshair and prompts only with the features on).
# python classictest.py TAG  -> classic_TAG.png
import sys, struct, time
import btnscan as bs, fastshot as fs
from peek import mem
tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
bs.VARS.update({n: bs.sym(n) for n in ('gDefinitive',)})
while not bs.settle():
    pass
fs.front(); time.sleep(0.3)
print('gDefinitive =', struct.unpack('>i', mem(bs.VARS['gDefinitive'], 4))[0])
bs.put('gDbgPadOn', 2)
try:
    seen = set()
    bs.pad(0x2000); bs.wait_frames(3); bs.pad()
    for _ in range(20):
        bs.wait_frames(1); p = bs.player(); seen.add((p[8], p[9]))
    print('square -> states seen', sorted(seen), 'hop' if (1, 9) in seen else 'no hop')
finally:
    bs.pad(); bs.wait_frames(2); bs.put('gDbgPadOn', 0)
bs.wait_frames(30)
fs.shot('classic_%s.png' % tag)
