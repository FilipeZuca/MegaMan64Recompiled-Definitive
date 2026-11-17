# Read-only: records RDRAM 0x80100000-0x80300000 raw (host word order) every 0.25 s, to diff enemy health while
# shooting. usage: python ramrec.py SECONDS OUTFILE.bin
import sys, time
import peek

LO, HI = 0x80100000, 0x80300000
end = time.time() + float(sys.argv[1])
with open(sys.argv[2], 'wb') as out:
    while time.time() < end:
        out.write(peek.read(peek.base + LO - 0x80000000, HI - LO))
        time.sleep(0.25)
