# Analyzes hprec.py output: for each actor, the u8/s16 fields that only ever drop, in a few discrete steps (health).
import sys, struct
S = 18 * 0x3C8 + 0x20
d = open(sys.argv[1], 'rb').read()
samples = [d[i:i + S] for i in range(0, len(d) - S + 1, S)]
print(len(samples), 'samples')
for a in range(18):
    base = a * 0x3C8
    types = set(s[base + 2] for s in samples if s[base] & 1)
    if not types:
        continue
    for off in range(0x20, 0x3C8, 2):
        vals = [struct.unpack('>h', s[base + off:base + off + 2])[0] for s in samples if s[base] & 1]
        steps = [(x, y) for x, y in zip(vals, vals[1:]) if x != y]
        if 2 <= len(steps) <= 12 and all(y < x for x, y in steps) and vals[0] > 0:
            print('actor %d types %s off +0x%X: %s' % (a, [hex(t) for t in types], off,
                  ' '.join(str(v) for v in [vals[0]] + [y for _, y in steps])))
