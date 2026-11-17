# Sends real input to the game window (SDL/ImGui read scancodes and need mouse motion before a click).
# usage: python ui.py key F1            keys: F1..F12, ESC, ENTER, SPACE, or one letter/digit
#        python ui.py click X Y         X,Y in pixels of the winshot.ps1 screenshot (window rect, title bar included)
#        python ui.py move X Y          move only (hover)
#        python ui.py drag X1 Y1 X2 Y2
#        python ui.py wheel X Y N       N notches (negative = scroll down)
import ctypes, ctypes.wintypes as w, subprocess, sys, time

u = ctypes.windll.user32
u.SetProcessDPIAware()
SC = {'ESC': (0x1B, 0x01), 'ENTER': (0x0D, 0x1C), 'SPACE': (0x20, 0x39)}
for i in range(1, 13):
    SC['F%d' % i] = (0x6F + i, 0x3A + i if i <= 10 else 0x57 + i - 11)


def hwnd():
    pid = int(subprocess.run(['powershell', '-NoProfile', '-Command', '(Get-Process MegaMan64Recompiled).Id'],
                             capture_output=True, text=True).stdout.split()[0])
    found = []

    @ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def cb(h, _):
        p = w.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h):
            found.append(h)
        return True

    u.EnumWindows(cb, 0)
    return found[0]


def focus(h):
    u.ShowWindow(h, 9)
    u.SetForegroundWindow(h)
    time.sleep(0.4)


def origin(h):
    r = w.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top


def mouse(x, y, ox, oy):
    u.SetCursorPos(ox + x, oy + y)
    time.sleep(0.12)
    u.mouse_event(1, 1, 0, 0, 0)  # relative nudge so SDL sees a motion event
    time.sleep(0.12)


cmd = sys.argv[1]
h = hwnd()
focus(h)
ox, oy = origin(h)
if cmd == 'key':
    name = sys.argv[2].upper()
    vk, sc = SC[name] if name in SC else (ord(name), 0)
    u.keybd_event(vk, sc, 0, 0)
    time.sleep(0.12)
    u.keybd_event(vk, sc, 2, 0)
elif cmd in ('click', 'move'):
    mouse(int(sys.argv[2]), int(sys.argv[3]), ox, oy)
    if cmd == 'click':
        u.mouse_event(2, 0, 0, 0, 0)
        time.sleep(0.12)
        u.mouse_event(4, 0, 0, 0, 0)
elif cmd == 'rclick':
    mouse(int(sys.argv[2]), int(sys.argv[3]), ox, oy)
    u.mouse_event(8, 0, 0, 0, 0)
    time.sleep(0.12)
    u.mouse_event(16, 0, 0, 0, 0)
elif cmd == 'wheel':
    mouse(int(sys.argv[2]), int(sys.argv[3]), ox, oy)
    u.mouse_event(0x800, 0, 0, 120 * int(sys.argv[4]) & 0xFFFFFFFF, 0)
elif cmd == 'drag':
    x1, y1, x2, y2 = map(int, sys.argv[2:6])
    mouse(x1, y1, ox, oy)
    u.mouse_event(2, 0, 0, 0, 0)
    for i in range(1, 11):
        u.SetCursorPos(ox + x1 + (x2 - x1) * i // 10, oy + y1 + (y2 - y1) * i // 10)
        time.sleep(0.03)
    u.mouse_event(4, 0, 0, 0, 0)
time.sleep(0.5)
