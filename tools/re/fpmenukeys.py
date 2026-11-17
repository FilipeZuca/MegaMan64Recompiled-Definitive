# First person stick in menus with real keys (through controls.cpp, unlike gDbgPadOn): C switches to first person,
# holding D in gameplay must strafe (+0x112 != 0), then in Normal Items D/A must move the cursor, not the page.
# python fpmenukeys.py TAG  -> fpmenu_TAG.png (game window must be free to take focus)
import sys, time, ctypes
import fastshot as fs
from peek import mem
from PIL import Image
u = ctypes.windll.user32
VK = {'C': 0x43, 'D': 0x44, 'A': 0x41, 'E': 0x45, 'ENTER': 0x0D, 'BACK': 0x08}
tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
shots = []

def key(name, hold=0.12):
    vk = VK[name]; sc = u.MapVirtualKeyW(vk, 0)
    u.keybd_event(vk, sc, 0, 0); time.sleep(hold); u.keybd_event(vk, sc, 2, 0); time.sleep(0.6)

def shot(name):
    p = 'fk_%s_%d.png' % (tag, len(shots)); fs.shot(p); shots.append(p); print(name, flush=True)

fs.front(); time.sleep(0.5)
if 'fp' not in sys.argv[2:]:
    key('C'); time.sleep(0.8)
shot('first person')
vk = VK['D']; sc = u.MapVirtualKeyW(vk, 0)
u.keybd_event(vk, sc, 0, 0)
seen = set()
for _ in range(10):
    time.sleep(0.05); seen.add(mem(0x802049B0 + 0x112, 1)[0])
u.keybd_event(vk, sc, 2, 0); time.sleep(0.6)
print('gameplay D: walk modes +0x112 seen', sorted(seen))
key('ENTER'); shot('pause')
key('E'); shot('normal items')
key('D'); shot('D')
key('D'); shot('D again')
key('A'); shot('A')
key('BACK'); key('BACK'); shot('back x2')
key('C'); time.sleep(0.8); shot('third person again')
ims = [Image.open(p).resize((480, 300)) for p in shots]
sheet = Image.new('RGB', (480 * 4, 300 * ((len(ims) + 3) // 4)))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 4) * 480, (i // 4) * 300))
sheet.save('fpmenu_%s.png' % tag)
