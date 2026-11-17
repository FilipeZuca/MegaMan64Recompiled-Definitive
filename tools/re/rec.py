# Glitch recorder. Leave it running in a terminal while you play; press F9 (works with the game focused) right after
# you see something wrong. Each press saves tools/re/marks/NNN/ with:
#   clip.mp4   the ~15-18 s before the press plus ~3.6 s after (F9 is ~3.6 s before the end of the clip)
#   trace.csv  per-game-frame state in the same window (t_rel = seconds relative to the press)
#   shot.png   screenshot at the press
# and appends one line to tools/re/glitches.txt (read by goto.py). Closing the game asks for a note on each new mark (or: python mark.py notes)
# usage: python rec.py          requires ffmpeg on PATH and Pillow; the game window must stay visible (GDI capture)
import collections, csv, ctypes, ctypes.wintypes as w, glob, os, shutil, struct, subprocess, sys, threading, time, winsound
from PIL import ImageGrab

HERE = os.path.dirname(os.path.abspath(__file__))
RING, MARKS, LOG = (os.path.join(HERE, n) for n in ('_ring', 'marks', 'glitches.txt'))
BEFORE, AFTER, SEG, FPS, VK_F9 = 15, 3.6, 3, 30, 0x78
user32 = ctypes.windll.user32
user32.SetProcessDPIAware()  # physical pixels, same as GDI capture


def connect():
    """peek.py scans the game's memory when imported and exits if the game is not up yet."""
    while True:
        try:
            import peek
            return peek
        except (SystemExit, Exception):
            print('waiting for the game...')
            time.sleep(2)


peek = connect()


def sym(name):
    for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'patches', 'patches.map')):
        if line.split()[-1:] == [name]:
            return int(line.split()[0], 16)


FP_ADDR = sym('sFirstPersonActive')
trace = collections.deque(maxlen=60 * (BEFORE + SEG + 10))
lock = threading.Lock()


def client_rect():
    """Visible client area of the game window in screen pixels (x, y, w, h), or None."""
    out = []

    @ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def cb(h, _):
        pid = w.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == peek.pid and user32.IsWindowVisible(h) and not user32.IsIconic(h):
            r, pt = w.RECT(), w.POINT(0, 0)
            user32.GetClientRect(h, ctypes.byref(r))
            user32.ClientToScreen(h, ctypes.byref(pt))
            if r.right > 100 and r.bottom > 100:
                out.append((pt.x, pt.y, r.right & ~1, r.bottom & ~1))
        return True

    user32.EnumWindows(cb, 0)
    return out[0] if out else None


class Ring:
    """ffmpeg writing 3 s .ts segments into a 30 s ring; restarted when the window moves or resizes."""

    def __init__(self):
        self.proc, self.rect, self.run = None, None, 0
        shutil.rmtree(RING, ignore_errors=True)
        os.makedirs(RING)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.stdin.write(b'q')
                self.proc.stdin.flush()
                self.proc.wait(5)
            except Exception:
                self.proc.kill()
        self.proc = None

    def update(self):
        rect = client_rect()
        if rect and (rect != self.rect or self.proc is None or self.proc.poll() is not None):
            self.stop()
            self.run += 1
            self.rect = rect
            x, y, cw, ch = rect
            cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'gdigrab', '-framerate', str(FPS),
                   '-draw_mouse', '0', '-offset_x', str(x), '-offset_y', str(y), '-video_size', '%dx%d' % (cw, ch),
                   '-i', 'desktop', '-vf', 'scale=min(1280\\,iw):-2', '-c:v', 'libx264', '-preset', 'ultrafast',
                   '-crf', '30', '-pix_fmt', 'yuv420p', '-force_key_frames', 'expr:gte(t,n_forced*%d)' % SEG,
                   '-f', 'segment', '-segment_time', str(SEG), '-segment_wrap', '10', '-reset_timestamps', '1',
                   os.path.join(RING, 'r%d_%%02d.ts' % self.run)]
            self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=open(os.path.join(RING, 'ffmpeg.log'), 'w'))
            print('recording %dx%d at %d,%d' % (cw, ch, x, y))
        for f in glob.glob(os.path.join(RING, '*.ts')):  # leftovers of older runs
            if time.time() - os.path.getmtime(f) > 40:
                os.remove(f)


def sample():
    p = peek.mem(0x802049B0, 0x60)
    cam = peek.mem(0x80204348, 0x14)
    pad = peek.mem(0x801BC380, 4)
    frame = struct.unpack('>I', peek.mem(0x8016281C, 4))[0]
    x, y, z = struct.unpack('>hhh', p[0x14:0x1A])
    yaw = struct.unpack('>h', p[0x56:0x58])[0] & 0xFFF
    camyaw = struct.unpack('>H', cam[0x10:0x12])[0] & 0xFFF
    sx, sy = struct.unpack('>bb', peek.mem(0x801BC382, 2))
    return (time.time(), frame, x, y, z, yaw, p[8], p[9], camyaw, cam[5], cam[6], peek.mem(FP_ADDR, 1)[0],
            struct.unpack('>H', pad[:2])[0], sx, sy)


COLS = ('t_rel', 'frame', 'x', 'y', 'z', 'yaw', 'state', 'substate', 'cam_yaw', 'cam_mode', 'cam_scripted', 'fp',
        'buttons', 'stick_x', 'stick_y')


def next_number():
    nums = [int(d) for d in os.listdir(MARKS) if d.isdigit()] if os.path.isdir(MARKS) else []
    return max(nums + [0]) + 1


def finalize(n, t0, rows_at_press):
    d = os.path.join(MARKS, '%03d' % n)
    time.sleep(AFTER)
    with lock:
        rows = [r for r in trace if t0 - BEFORE <= r[0] <= t0 + AFTER]
    with open(os.path.join(d, 'trace.csv'), 'w', newline='') as f:
        wr = csv.writer(f)
        wr.writerow(COLS)
        for r in rows:
            wr.writerow(['%.2f' % (r[0] - t0)] + list(r[1:]))
    segs = [(os.path.getmtime(f), f) for f in glob.glob(os.path.join(RING, '*.ts'))]
    segs = [s for s in segs if s[0] >= t0 - BEFORE]
    if segs:
        latest = max(os.path.basename(f).split('_')[0] for _, f in segs)  # one run = one resolution
        tmp = os.path.join(d, 'seg')
        os.makedirs(tmp, exist_ok=True)
        names = []
        for i, (_, f) in enumerate(sorted(s for s in segs if os.path.basename(s[1]).startswith(latest + '_'))):
            dst = os.path.join(tmp, '%02d.ts' % i)
            shutil.copy(f, dst)
            names.append(dst)
        lst = os.path.join(tmp, 'list.txt')
        open(lst, 'w').write(''.join("file '%s'\n" % p.replace('\\', '/') for p in names))
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', lst,
                        '-c', 'copy', '-movflags', '+faststart', os.path.join(d, 'clip.mp4')], capture_output=True)
        shutil.rmtree(tmp, ignore_errors=True)
    ok = os.path.exists(os.path.join(d, 'clip.mp4'))
    r = rows_at_press
    line = '#%03d %s pos=%d,%d,%d yaw=%d state=%d/%d frame=%d fp=%d shot=marks/%03d/shot.png  ' % (
        n, time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(t0)), r[2], r[3], r[4], r[5], r[6], r[7], r[1], r[11], n)
    with open(LOG, 'a') as f:
        f.write(line + '\n')
    print('saved #%03d (clip %s, %d trace rows)  ->  python mark.py notes' % (n, 'ok' if ok else 'MISSING', len(rows)))
    winsound.Beep(1320, 120)


def mark(last_row):
    t0 = time.time()
    n = next_number()
    d = os.path.join(MARKS, '%03d' % n)
    os.makedirs(d)
    winsound.Beep(880, 120)
    rect = client_rect()
    if rect:
        ImageGrab.grab(bbox=(rect[0], rect[1], rect[0] + rect[2], rect[1] + rect[3]), all_screens=True).save(
            os.path.join(d, 'shot.png'))
    print('F9 -> mark #%03d, collecting...' % n)
    threading.Thread(target=finalize, args=(n, t0, last_row), daemon=True).start()


def ask_notes():
    # The game closed: if a mark still has no description, bring this window up and ask for them (mark.py notes).
    import re as regex
    if not os.path.exists(LOG):
        return
    if not any(regex.match(r'#\d+ .*?shot=\S+\s*$', l) for l in open(LOG).read().splitlines()):
        return
    time.sleep(5)  # the last clip is still being written for a few seconds after F9
    h = ctypes.windll.kernel32.GetConsoleWindow()
    if h:
        user32.ShowWindow(h, 9)
        user32.SetForegroundWindow(h)
    print(chr(10) + 'Game closed. Describe each mark in one line (Enter skips, q stops):')
    subprocess.run([sys.executable, os.path.join(HERE, 'mark.py'), 'notes'])


def main():
    ring, was, fails, last_row, next_ring = Ring(), False, 0, None, 0
    print('recording. F9 = mark a problem (video), Ctrl+C = quit.')
    try:
        while True:
            if time.time() >= next_ring:  # once a second: (re)start ffmpeg if the window moved or ffmpeg died
                ring.update()
                next_ring = time.time() + 1
            try:
                row = sample()
                fails = 0
                with lock:
                    if not trace or trace[-1][1] != row[1]:  # one row per game frame (30 Hz)
                        trace.append(row)
                last_row = row
            except Exception:
                fails += 1
                if fails > 150:  # game closed or restarted: ask for notes, then reattach by restarting this script
                    ring.stop()
                    ask_notes()
                    os.execv(sys.executable, [sys.executable] + sys.argv)
            down = bool(user32.GetAsyncKeyState(VK_F9) & 0x8000)
            if down and not was and last_row:
                mark(last_row)
            was = down
            time.sleep(1 / 60)
    except KeyboardInterrupt:
        pass
    finally:
        ring.stop()


if __name__ == '__main__':
    main()
