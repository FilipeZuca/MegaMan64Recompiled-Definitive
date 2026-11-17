# Adds your descriptions to the marks recorded by rec.py (F9). Each mark is a line in glitches.txt plus a folder in marks/.
# usage: python mark.py notes   -> asks a note for every mark that has none (Enter skips, "q" quits)
#        python mark.py list    -> shows all marks with their notes
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, 'glitches.txt')
if not os.path.exists(LOG):
    sys.exit('no marks yet: run rec.py and press F9 when you see a problem')
lines = open(LOG).read().splitlines()
cmd = sys.argv[1] if len(sys.argv) > 1 else 'notes'
for i, line in enumerate(lines):
    m = re.match(r'(#(\d+) .*?shot=\S+)\s*(.*)', line)
    if not m:
        continue
    if cmd == 'list':
        print(line)
    elif cmd == 'notes' and not m.group(3):
        print(line)
        print('    clip: marks/%03d/clip.mp4' % int(m.group(2)))
        note = input('    what is wrong? ').strip()
        if note.lower() == 'q':
            break
        if note:
            lines[i] = m.group(1) + '  ' + note
            open(LOG, 'w').write('\n'.join(lines) + '\n')
