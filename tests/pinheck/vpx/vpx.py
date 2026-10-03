#!/usr/bin/env python3
"""What a libpinmame host (VPX standalone's PinMAME plugin, or any other) receives, from host's logs.
  vpx.py displays DIR...        every announced display index exists and gets updates
  vpx.py probe DIR...           lamps and solenoids 0, -1 and 100000 read 0 on the games run
  vpx.py plan DIR               the workloads: DIR/switches.txt, send, send_at, send_gap, frames; DIR/mech.txt
  vpx.py outputs DIR            lamps, solenoids, GI, RGB, servos and switches through the API equal the driver's
  vpx.py media DIR REF.wav      display frames and sound through the API equal the driver's frame log and capture;
                                every frame on the panel for a vblank (1/60 s) or more reaches the host
  vpx.py dmd DIR                America's Most Haunted's raw DMD through the API: 16 shades, the driver's decoded frames
  vpx.py mech ON_DIR OFF_DIR    HandleMechanics bit 0 turns the simulated Noid on and off
  vpx.py restart DIR            a second session in the same process: the link log is complete at the first
                                session's end, and the second session's packets start whole
  vpx.py sessions DIR           with the plugin API over two sessions (-p -R), the second session enumerates and reads it again
  vpx.py selftest DIR           media's frame rule on synthetic runs written to DIR"""
import array
import os
import struct
import sys
import wave

FPS = 60
W, H = 128, 64 if os.environ.get('PINHECK_GAME') == 'jetsons' else 32    # the module PINHECK_GAME has
MENU_AT, SEND_AT, GAP = 10.5, 11.5, 0.25
ENTER, BACK, LFLIP, RFLIP = 6, 5, 114, 112      # cabinet switches; the flippers through PinMAME's flipper column
PROBE_AT = 900                                  # frame: switch numbers that do not exist are set, then cleared
BAD = (9, 10, 19, 20, 29, 30, 50, 89, 90, 99, 100, 109, 119, 120, 128, 0, -1)


def api_log(d):
    """api.log -> ({key: [(frame, value)]}, other lines)"""
    ser, other = {}, []
    for line in open(os.path.join(d, 'api.log')):
        f = line.split()
        if f and f[0] == 'O':
            fr = int(f[1])
            for x in f[2:]:
                k, v = x.split('=')
                ser.setdefault(k, []).append((fr, int(v)))
        else:
            other.append(line.rstrip('\n'))
    return ser, other


def displays(dirs):
    fail = 0
    for d in dirs:
        _, other = api_log(d)
        avail = [l.split() for l in other if l.startswith('avail ')]
        counts = set(int(a[2]) for a in avail)
        idx = sorted(int(a[1]) for a in avail)
        upd = {int(l.split()[1]): int(l.split()[7]) for l in other if l.startswith('display ')}
        n = counts.pop() if len(counts) == 1 else None
        # libpinmame reserves one slot more than it announces (the segment games' extra 128x32 DMD)
        ok = n is not None and idx == list(range(len(idx))) and n in (len(idx), len(idx) + 1) and \
            len(idx) > 0 and all(upd.get(i, 0) > 0 for i in idx)
        print('displays: %s announces %s display(s), indices %s, updates %s%s' % (
            os.path.basename(d), n if n is not None else sorted(counts), idx, [upd.get(i, 0) for i in idx], '' if ok else '  FAIL'))
        fail += not ok
    return fail


def probe(dirs):
    """lamps and solenoids 0, -1 and 100000 read 0 and do not stop the emulation on pb_l5, tz_94h and babypac:
    -1 and 100000 are out of range; 0 is a real lamp on some systems (Taito, GTS80, GTS3)"""
    fail = 0
    for d in dirs:
        api, other = api_log(d)
        vals = dict((k, set(v for _, v in api.get(k, []))) for k in ('X0', 'X-1', 'X100000', 'Y0', 'Y-1', 'Y100000'))
        ok = any(l.startswith('end ') for l in other) and all(v == {0} for v in vals.values())
        print('probe: %s %s%s' % (os.path.basename(d), ' '.join('%s=%s' % (k, sorted(v)) for k, v in vals.items()), '' if ok else '  FAIL'))
        fail += not ok
    return fail


def menu_plan():
    """(time, switch) presses of the service menu walk after the console commands"""
    ev, t = [], 19.0

    def press(sw, dt):
        nonlocal t
        ev.append((t, sw))
        t += dt
    for _ in range(3):
        press(RFLIP, 1.0)                           # AUDIO/MUSIC, SOLENOID, SERVO
    press(ENTER, 1.0)                               # SERVO TEST: NOID RIGHT
    press(ENTER, 1.5)                               # servo 0 at 0 degrees
    press(RFLIP, 1.0)                               # NOID STOP
    press(ENTER, 1.0)
    press(RFLIP, 1.0)                               # NOID LEFT
    press(ENTER, 1.5)                               # servo 0 at 180 degrees
    press(BACK, 1.0)                                # SERVO
    press(RFLIP, 1.0)                               # LAMP
    press(ENTER, 0.5)                               # LAMP TEST: ALL OFF
    for _ in range(20):
        press(RFLIP, 0.5)                           # PLAYFIELD GI 1..128, BACKBOX GI 0..7, ALL ... GI ON, GI AND LAMPS ALL ON, ALL OFF
    press(BACK, 1.0)                                # LAMP
    press(RFLIP, 1.0)                               # RGB LIGHTING
    press(ENTER, 1.0)                               # RGB1 RED
    for _ in range(7):
        press(RFLIP, 1.0)                           # ... RGB2 WHITE
    press(LFLIP, 1.0)                               # back to RGB2 BLUE
    press(BACK, 1.0)
    return ev, t


def uart_plan():
    return ['[E97000]', '[L99000]'] + ['[M%02d020]' % c for c in range(24)] + ['[L98000]']


def plan(d):
    ev, end = menu_plan()
    lines = ['%d %d 1\n%d %d 0\n' % (round(MENU_AT * FPS), ENTER, round(MENU_AT * FPS) + 6, ENTER)]
    lines += ['%d %d 1\n%d %d 0\n' % (round(t * FPS), sw, round(t * FPS) + 6, sw) for t, sw in ev]
    lines += ['%d %d 1\n' % (PROBE_AT, n) for n in BAD] + ['%d %d 0\n' % (PROBE_AT + 30, n) for n in BAD]
    mech = [(MENU_AT, ENTER)] + [(MENU_AT + 1 + k, RFLIP) for k in range(3)] + [(MENU_AT + 4, ENTER), (MENU_AT + 5, ENTER)]
    open(os.path.join(d, 'mech.txt'), 'w').write(''.join('%d %d 1\n%d %d 0\n' % (round(t * FPS), sw, round(t * FPS) + 6, sw) for t, sw in mech))
    for name, text in (('switches.txt', ''.join(lines)), ('send', ''.join(c + '~' for c in uart_plan())),
                       ('send_at', '%g' % SEND_AT), ('send_gap', '%g' % GAP), ('frames', '%d' % round((end + 1) * FPS))):
        open(os.path.join(d, name), 'w').write(text + ('\n' if name != 'switches.txt' else ''))


def driver_log(d):
    """out.log -> {key: [(vblank, value)]}: P lamps/solenoids, B lamp bits, W switches"""
    ser = {}
    for line in open(os.path.join(d, 'out.log')):
        f = line.split()
        if len(f) < 3 or f[1] not in ('P', 'B', 'W'):
            continue
        k = round(float(f[0]) * FPS)
        if f[1] == 'B':
            m = bytes.fromhex(f[2])
            for i in range(72):
                ser.setdefault('B%d' % ((i // 8 + 1) * 10 + i % 8 + 1), []).append((k, (m[i // 8] >> (i % 8)) & 1))
            continue
        for x in f[2:]:
            n, v = x.split('=')
            ser.setdefault(('W' + n) if f[1] == 'W' else n, []).append((k, int(v)))
    return ser


class Series:
    def __init__(self, pts):
        self.pts, self.i, self.v = pts, 0, 0

    def at(self, k):
        """value at frame k; k must not decrease between calls"""
        while self.i < len(self.pts) and self.pts[self.i][0] <= k:
            self.v = self.pts[self.i][1]
            self.i += 1
        return self.v


LAMPS = [c * 10 + r for c in range(1, 9) for r in range(1, 9)] + [91]
SOLS = list(range(1, 33)) + list(range(37, 49)) + list(range(51, 65))
DRIVEN = list(range(1, 33)) + list(range(37, 45)) + list(range(51, 59)) + [62, 63, 64]
SWITCHES = list(range(1, 9)) + [c * 10 + r for c in range(1, 10) for r in range(1, 9)]


def outputs(d):
    """Each value a host read at frame f must be one the driver's output has at vblank f-1 .. f+2: an output's
    integrator brings its value up to date one query late, so the host's reads between the driver's vblank
    logs move the driver's series by up to two frames. Matrix lamps are strobed, and a read between two
    vblanks may catch one in its dark phase: up to 1% of a lamp's reads may differ."""
    api, other = api_log(d)
    drv = driver_log(d)
    physout = 'physout 1' in ' '.join(l for l in other if l.startswith('options '))
    last = max(fr for s in api.values() for fr, _ in s)
    fails = []
    same = lambda t: lambda a, r: abs(a - r) <= t
    pairs = []                                   # (API key, driver key, match, fraction of reads that may differ)
    for n in LAMPS:
        if physout:
            pairs += [('L%d' % n, 'L%d' % n, same(1), 0.01), ('PL%d' % n, 'L%d' % n, same(1), 0.01)]
        else:
            pairs += [('L%d' % n, 'B%d' % n, same(0), 0.01), ('PL%d' % n, 'B%d' % n, lambda a, r: a == 255 * r, 0.01)]
    for n in SOLS:
        pairs.append(('S%d' % n, 'S%d' % n, same(1), 0))
        if physout or n > 44:
            pairs.append(('PS%d' % n, 'S%d' % n, same(1), 0))
        elif n <= 32:                            # a legacy host's solenoid: on from half power
            pairs.append(('PS%d' % n, 'S%d' % n, lambda a, r: a == (255 if r >= 128 else 0), 0))
    pairs += [('W%d' % n, 'W%d' % n, same(0), 0) for n in SWITCHES]
    compared, lit, bad = 0, set(), 0
    for ak, dk, match, frac in pairs:
        if ak not in api:
            fails.append('%s is not exported' % ak)
            continue
        a = Series(api[ak])
        r = [Series(drv.get(dk, [])) for _ in range(4)]
        n, first = 0, None
        for f in range(2, last - 1):
            av, rv = a.at(f), [r[i].at(f - 1 + i) for i in range(4)]
            compared += 1
            if not any(match(av, x) for x in rv):
                n += 1
                first = first or '%s is %d at frame %d, the driver has %s' % (ak, av, f, '/'.join(map(str, rv)))
            elif av:
                lit.add(ak)
        bad += n
        if n > frac * (last - 3):
            fails.append('%s (%d reads differ)' % (first, n))
    for n in range(37, 45):                      # a legacy host's GI_8..15: on at some time in the frame
        if api.get('PS%d' % n):
            lit.update('PS%d' % n for _, v in api['PS%d' % n] if v)
    want = ['%s%d' % (p, n) for n in LAMPS for p in ('L', 'PL')] + ['%s%d' % (p, n) for n in DRIVEN for p in ('S', 'PS')]
    want += ['W%d' % n for n in (3, 4, 5, 6)]
    dark = [k for k in want if k not in lit]
    if dark:
        fails.append('never seen on: %s' % ' '.join(dark))
    groups = {l.split()[2]: l.split(':', 1)[1].split() for l in other if l.startswith('plugin group ')}
    ids = [int(x) for x in groups.get('Solenoids', [])]
    if not all(n in ids for n in range(37, 45)):
        fails.append('the plugin lists no solenoids 37-44 (GI_8..15): %s' % ids)
    for n in BAD:
        vals = set(v for _, v in api.get('X%d' % n, [(0, 0)]))
        if vals != {0}:
            fails.append('lamp %d, which does not exist, reads %s' % (n, sorted(vals)))
    moved = [x for x in open(os.path.join(d, 'out.log'))
             if x.split()[1:2] == ['W'] and PROBE_AT <= round(float(x.split()[0]) * FPS) <= PROBE_AT + 32]
    if moved:
        fails.append('setting switches that do not exist moved real ones: %s' % moved[0].strip())
    for f in fails:
        print('VPX FAIL: ' + f)
    print('outputs: %s, %d reads of %d outputs compared, %d differ, %d failures' % (
        'physical outputs' if physout else 'legacy outputs', compared, len(pairs), bad, len(fails)))
    return 1 if fails else 0


def rgb565(v):
    """a frame byte as libpinmame exports it: RGB332 -> the driver's 15-bit pen -> 5.6.5"""
    r, g, b = ((v >> 5) & 7) * 255 // 7, ((v >> 2) & 7) * 255 // 7, (v & 3) * 255 // 3
    p = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
    return (p & 0x7fe0) << 1 | (p >> 4) & 0x20 | (p & 0x1f)


def media(d, ref):
    fails = []
    _, other = api_log(d)
    avail = [l for l in other if l.startswith('avail ')]
    if avail != ['avail 0 1 type 15 %dx%d depth 16 length 0' % (W, H)]:
        fails.append('display announced as %s, expected one %dx%d VIDEO display of depth 16' % (avail, W, H))
    lut = [rgb565(v) for v in range(256)]
    raw = open(os.path.join(d, 'frames.log'), 'rb').read()
    rec = 20 + W * H
    drv, start, last = [], [], 0.0
    for i in range(0, len(raw) - rec + 1, rec):
        f = raw[i + 20:i + rec]
        last = struct.unpack_from('<Q', raw, i + 8)[0] / 80e6       # PIC32 time stamp
        if not drv or drv[-1] != f:
            drv.append(f)
            start.append(last)
    # the host reads the panel at vblank n (n / FPS s), up to its last frame
    ends = [l for l in other if l.startswith('end ')]
    stop = int(ends[0].split()[1]) / FPS if ends else last
    on = [min(t, stop) - s for s, t in zip(start, start[1:] + [stop])]
    long_ = [i for i in range(len(drv)) if on[i] >= 1.0 / FPS]
    want = [array.array('H', (lut[v] for v in f)).tobytes() for f in drv]
    raw = open(os.path.join(d, 'frames.bin'), 'rb').read()
    rec = 4 + W * H * 2
    got = {False: [], True: []}
    plugin_run = any(l.startswith('plugin display') for l in other)
    for i in range(0, len(raw) - rec + 1, rec):
        tag = struct.unpack_from('<I', raw, i)[0]
        f = raw[i + 4:i + rec]
        if tag >= 0x80000000 and not got[True] and not any(f):
            continue                             # the plugin's frame buffer before the first frame
        got[tag >= 0x80000000].append(f)
    for plugin in (False, True) if plugin_run else (False,):
        k, seen, shown = 0, 0, set()
        for f in got[plugin]:
            while k < len(want) and want[k] != f:
                k += 1
            if k == len(want):
                break
            seen += 1
            shown.add(k)
        path = 'plugin' if plugin else 'callback'
        if seen < len(got[plugin]):
            fails.append('%s frame %d is none of the decoded frames that follow the last match' % (path, seen))
        else:
            print('media: %s path: %d frames, each a decoded frame, in order; the frame log has %d distinct frames' % (path, seen, len(want)))
        missed = [i for i in long_ if i not in shown]
        if missed:
            fails.append('%s path missed %d of the %d frames on the panel for a vblank or more (first: frame %d, %.1f ms from %.3f s)' % (
                path, len(missed), len(long_), missed[0], on[missed[0]] * 1000, start[missed[0]]))
        else:
            print('media: %s path: all %d frames on the panel for a vblank or more; %d shorter ones, %d of them shown' % (
                path, len(long_), len(drv) - len(long_), len(shown) - len(long_)))
    a = array.array('h', open(os.path.join(d, 'audio.raw'), 'rb').read())
    with wave.open(os.path.join(d, 'api.wav'), 'wb') as out:     # for corr.py
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(44100)
        out.writeframes(a.tobytes())
    w = array.array('h', open(ref, 'rb').read()[44:])
    n = min(len(a), len(w))
    diff = max((abs(a[i] - w[i]) for i in range(n)), default=99)
    loud = sum(1 for i in range(n) if abs(w[i]) > 1000)
    print('media: %d API samples, %d captured, largest difference %d, %d loud samples' % (len(a), len(w), diff, loud))
    if abs(len(a) - len(w)) > 2 * 800 or diff > 1 or loud < 1000:
        fails.append('the API stream is not the driver\'s (TPDF dither allows 1)')
    for f in fails:
        print('VPX FAIL: ' + f)
    return 1 if fails else 0


def dmd(d, rawmode=False):
    """America's Most Haunted's raw DMD as a libpinmame host gets it: one 128x32 DMD of 16 shades (depth 4). The
    driver hands the core each vblank's subframes (PINHECK_DMD_LOG); the core shows the sum of the last 16, so each
    frame of the callback path (bytes, 255 * sum / 16; raw: the sum) and of the plugin path (floats, sum / 16) must
    equal that sum at a vblank, the frames in the order of their vblanks (libpinmame passes a frame on when it changed)"""
    fails = []
    _, other = api_log(d)
    avail = [l for l in other if l.startswith('avail ')]
    if avail != ['avail 0 1 type 14 128x32 depth 4 length 0']:
        fails.append('display announced as %s, expected one 128x32 DMD of depth 4' % avail)
    raw, i, subs, cum = open(os.path.join(d, 'dmd.log'), 'rb').read(), 0, [], []
    while i + 4 <= len(raw):
        n = struct.unpack_from('<I', raw, i)[0]
        subs += [raw[i + 4 + 512 * k:i + 4 + 512 * (k + 1)] for k in range(n)]
        cum.append(len(subs))
        i += 4 + 512 * n
    dots = [bytes((v >> (7 - b)) & 1 for b in range(8)) for v in range(256)]   # a subframe byte as 8 dots of 0/1
    expanded, sums = {}, {}

    def window(k):
        """the dots' sums (one byte each) over the last 16 subframes the core had after vblank k"""
        if k not in sums:
            total = 0
            for j in range(max(0, cum[k] - 16), cum[k]):
                if j not in expanded:
                    expanded[j] = int.from_bytes(b''.join(dots[v] for v in subs[j]), 'big')
                total += expanded[j]                     # no carries: a byte sums at most 16 dots
            sums[k] = total.to_bytes(W * H, 'big')
        return sums[k]
    host = {False: [], True: []}
    raw, i = open(os.path.join(d, 'frames.bin'), 'rb').read(), 0
    while i + 4 <= len(raw):
        plugin = struct.unpack_from('<I', raw, i)[0] >= 0x80000000
        n = W * H * (4 if plugin else 1)
        host[plugin].append(raw[i + 4:i + 4 + n])
        i += 4 + n
    # sums 0-16; raw (PINMAME_DMD_MODE_RAW): the core's raw frame, the sum held to 15
    lum8 = bytes(min(15, v) for v in range(256)) if rawmode else bytes(min(255, int(255.0 * v / 16.0)) for v in range(256))
    for plugin in (False, True):
        frames = host[plugin]
        if plugin and not frames:
            continue
        path = 'plugin' if plugin else 'callback'
        conv = (lambda w: struct.pack('<%df' % (W * H), *(v / 16.0 for v in w))) if plugin else (lambda w: w.translate(lum8))
        k, good, lit = 0, 0, 0
        for x in frames:
            lit += any(x)
            hit = next((j for j in range(k, len(cum)) if conv(window(j)) == x), None)
            if hit is None:
                continue
            good += 1
            k = hit
        print('dmd: %s path: %d of %d frames (%d not blank) equal the sum of the last 16 subframes at a vblank, in order' % (path, good, len(frames), lit))
        if not frames or good != len(frames) or lit < len(frames) // 2:
            fails.append('%s path: %d of %d frames are not the sum of the last 16 subframes at a vblank' % (path, len(frames) - good, len(frames)))
    for f in fails:
        print('VPX FAIL: ' + f)
    return 1 if fails else 0


def mech(on, off):
    fails = []
    for d, want in ((on, True), (off, False)):
        api, _ = api_log(d)
        edges = [fr for fr, v in api.get('W58', [])][1:]
        print('mech: %s, Noid Home changed %d times' % (os.path.basename(d), len(edges)))
        if (len(edges) >= 2) != want:
            fails.append('Noid Home %s with HandleMechanics %s' % ('moved' if edges else 'never moved', os.path.basename(d)))
    for f in fails:
        print('VPX FAIL: ' + f)
    return 1 if fails else 0


def restart(d):
    fails = []
    _, other = api_log(d)
    if not any(l.startswith('restart after ') for l in other) or not any(l.startswith('end ') for l in other):
        fails.append('the host did not run two sessions')
    first = open(os.path.join(d, 'link1.log')).read()
    lines = open(os.path.join(d, 'link.log')).read().splitlines(True)
    times = [float(l.split()[0]) for l in lines]
    drops = [i for i in range(1, len(times)) if times[i] < times[i - 1]]
    if len(drops) != 1:
        fails.append('the link log has %d time resets, want 1 (one per new session)' % len(drops))
    else:
        k = drops[0]
        if first != ''.join(lines[:k]):
            fails.append('at the first session\'s end the link log held %d of its %d bytes' % (len(first), len(''.join(lines[:k]))))
        one = [l.split()[1:17] for l in lines[:k]][:40]
        two = [l.split()[1:17] for l in lines[k:]][:40]
        if len(two) < 40 or one != two:
            n = next((i for i in range(min(len(one), len(two))) if one[i] != two[i]), min(len(one), len(two)))
            fails.append('the second session\'s packet %d is %s, the first session\'s %s' % (n, ' '.join(two[n]) if n < len(two) else '-', ' '.join(one[n]) if n < len(one) else '-'))
        print('restart: %d packets in the first session, %d in the second, the first 40 of each equal' % (k, len(lines) - k) if not fails else 'restart: %d + %d packets' % (k, len(lines) - k))
    for f in fails:
        print('VPX FAIL: ' + f)
    return 1 if fails else 0


def sessions(d):
    fails = []
    _, other = api_log(d)
    k = next((i for i, l in enumerate(other) if l.startswith('restart after ')), None)
    if k is None or not any(l.startswith('end ') for l in other):
        fails.append('the host did not run two sessions')
        k = len(other)
    groups = [sum(1 for l in part if l.startswith('plugin group ')) for part in (other[:k], other[k:])]
    disp = [l.split() for l in other if l.startswith('plugin display ')]
    if not disp:
        disp = [['plugin', 'display', '0x0', 'format', '3']]
    w, h = map(int, disp[0][2].split('x'))
    raw = open(os.path.join(d, 'frames.bin'), 'rb').read()
    rec = 4 + w * h * {3: 2, 2: 3}.get(int(disp[0][4]), 4)    # RGB565, RGB888, else 32-bit luminance
    tags = [struct.unpack_from('<I', raw, i)[0] for i in range(0, len(raw) - rec + 1, rec)]
    tags = [t & 0x7fffffff for t in tags if t >= 0x80000000]
    drop = next((i for i in range(1, len(tags)) if tags[i] < tags[i - 1]), len(tags))
    frames = [drop, len(tags) - drop]
    print('sessions: plugin groups enumerated %d + %d, plugin frames read %d + %d' % tuple(groups + frames))
    if not all(groups) or not all(frames):
        fails.append('a session without the plugin API\'s groups or frames')
    for f in fails:
        print('VPX FAIL: ' + f)
    return 1 if fails else 0


def selftest(d):
    """media on synthetic runs: frames of (ms on the panel, exported); each case (name, frames, expected exit)"""
    cases = [('all shown', [(40, 1), (40, 1), (40, 1), (40, 1)], 0),
             ('a 60 ms frame missing', [(40, 1), (60, 0), (40, 1), (40, 1)], 1),
             ('three 10 ms frames missing', [(40, 1), (10, 0), (10, 0), (10, 0), (40, 1), (40, 1)], 0),
             ('a 17 ms frame missing', [(40, 1), (17, 0), (40, 1)], 1)]
    fail = 0
    for name, frames, want in cases:
        c = os.path.join(d, name.replace(' ', '_'))
        os.makedirs(c, exist_ok=True)
        lut = [rgb565(v) for v in range(256)]
        log, got, t = b'', b'', 5.0
        for k, (ms, shown) in enumerate(frames):
            px = bytes([k + 1]) * (W * H)
            for r in range(max(1, ms // 10)):     # the Propeller resends a frame every 10 ms
                log += struct.pack('<QQI', 0, round((t + r * 0.01) * 80e6), 0) + px
            if shown:
                got += struct.pack('<I', k + 1) + array.array('H', (lut[v] for v in px)).tobytes()
            t += ms / 1000.0
        open(os.path.join(c, 'frames.log'), 'wb').write(log)
        open(os.path.join(c, 'frames.bin'), 'wb').write(got)
        open(os.path.join(c, 'api.log'), 'w').write('avail 0 1 type 15 128x32 depth 16 length 0\n')
        pcm = array.array('h', [3000, -3000] * 1000).tobytes()
        open(os.path.join(c, 'audio.raw'), 'wb').write(pcm)
        with wave.open(os.path.join(c, 'capture.wav'), 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(44100)
            w.writeframes(pcm)
        stdout, sys.stdout = sys.stdout, open(os.devnull, 'w')
        try:
            got_rc = media(c, os.path.join(c, 'capture.wav'))
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        ok = got_rc == want
        print('selftest: %s: media %s%s' % (name, 'fails' if got_rc else 'passes', '' if ok else '  FAIL'))
        fail += not ok
    return fail


if __name__ == '__main__':
    a = sys.argv[1:]
    if a[:1] == ['displays'] and len(a) > 1:
        sys.exit(1 if displays(a[1:]) else 0)
    if a[:1] == ['probe'] and len(a) > 1:
        sys.exit(1 if probe(a[1:]) else 0)
    if a[:1] == ['plan'] and len(a) == 2:
        sys.exit(plan(a[1]))
    if a[:1] == ['outputs'] and len(a) == 2:
        sys.exit(outputs(a[1]))
    if a[:1] == ['media'] and len(a) == 3:
        sys.exit(media(a[1], a[2]))
    if a[:1] == ['dmd'] and len(a) == 2:
        sys.exit(dmd(a[1]))
    if a[:1] == ['dmd'] and len(a) == 3 and a[2] == 'raw':
        sys.exit(dmd(a[1], True))
    if a[:1] == ['mech'] and len(a) == 3:
        sys.exit(mech(a[1], a[2]))
    if a[:1] == ['restart'] and len(a) == 2:
        sys.exit(restart(a[1]))
    if a[:1] == ['sessions'] and len(a) == 2:
        sys.exit(sessions(a[1]))
    if a[:1] == ['selftest'] and len(a) == 2:
        sys.exit(1 if selftest(a[1]) else 0)
    sys.exit(__doc__)
