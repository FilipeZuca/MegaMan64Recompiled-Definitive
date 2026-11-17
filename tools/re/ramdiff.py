# Finds health-like values in a ramrec.py recording: u8/s16 values that only ever drop, in 2..15 discrete steps,
# and end at or near zero. usage: python ramdiff.py FILE.bin
import sys
import numpy as np

LO, SIZE = 0x80100000, 0x200000
raw = np.fromfile(sys.argv[1], dtype=np.uint8)
n = len(raw) // SIZE
words = raw[:n * SIZE].reshape(n, SIZE // 4, 4)[:, :, ::-1].reshape(n, SIZE)  # back to big-endian byte order
print(n, 'samples')
for kind, arr in (('u8', words.astype(np.int32)),
                  ('s16', words.reshape(n, SIZE // 2, 2).astype(np.int32)[:, :, 0] * 256 +
                          words.reshape(n, SIZE // 2, 2)[:, :, 1])):
    if kind == 's16':
        arr = np.where(arr >= 0x8000, arr - 0x10000, arr)
    d = np.diff(arr, axis=0)
    ups = (d > 0).sum(axis=0)
    downs = (d < 0).sum(axis=0)
    cand = np.where((ups == 0) & (downs >= 2) & (downs <= 15) & (arr[0] > 0) & (arr[-1] <= arr[0] // 4))[0]
    for i in cand[:200]:
        addr = LO + i * (1 if kind == 'u8' else 2)
        seq = arr[:, i]
        steps = seq[np.r_[True, np.diff(seq) != 0]]
        print('%s %08X %s' % (kind, addr, ' '.join(map(str, steps))))
