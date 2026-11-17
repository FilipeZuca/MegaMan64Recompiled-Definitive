# Read-only: records the whole enemy actor list for a while (to find the health field by shooting an enemy).
# usage: python hprec.py SECONDS OUTFILE.bin   (each sample: 18 * 0x3C8 bytes, big-endian)
import sys, time
from peek import mem

end = time.time() + float(sys.argv[1])
with open(sys.argv[2], 'wb') as out:
    while time.time() < end:
        out.write(mem(0x801FFBE0, 18 * 0x3C8) + mem(0x802049B0, 0x20))
        time.sleep(0.1)
