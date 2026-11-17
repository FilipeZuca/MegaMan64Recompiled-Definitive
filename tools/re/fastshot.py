# Fast screenshot of the game window (no PowerShell start-up, no wait): call front() once, then shot(path) as needed.
import ctypes, ctypes.wintypes as w, subprocess
from PIL import ImageGrab

u = ctypes.WinDLL('user32', use_last_error=True)
pid = int(subprocess.run(['powershell', '-NoProfile', '-Command', '(Get-Process MegaMan64Recompiled).Id'],
                         capture_output=True, text=True).stdout.split()[0])
hwnd = None

def _find():
    found = []
    @ctypes.WINFUNCTYPE(ctypes.c_bool, w.HWND, w.LPARAM)
    def cb(h, _):
        p = w.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h) and u.GetWindowTextLengthW(h) > 0:
            found.append(h)
        return True
    u.EnumWindows(cb, 0)
    return found[0]

def front():
    global hwnd
    hwnd = _find()
    u.ShowWindow(hwnd, 9)
    u.SetForegroundWindow(hwnd)

def shot(path):
    r = w.RECT()
    u.GetWindowRect(hwnd, ctypes.byref(r))
    ImageGrab.grab((r.left, r.top, r.right, r.bottom)).save(path)
