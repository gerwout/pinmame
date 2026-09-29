#!/usr/bin/env python3
"""The display module's look (spec M8/M9 addendum 5): a model of display.c's config decoding and rendering,
and the look check, which steps the service menu's display settings through every value.
  look.py crosscheck BIN   the model against display.c through BIN (lookdump)
  look.py plan DIR         write DIR/keys.txt and DIR/frames for launch 2
  look.py verify DIR       check DIR/prop2.log's config packets and DIR/snap/*.png against the model"""
import os
import struct
import subprocess
import sys
import zlib

sys.dont_write_bytecode = True  # no __pycache__ next to the tests
from frames import read_log  # noqa: E402

W, H = 128, 32
LW, LH = 2 * W, 2 * H
ROUND, SQUARE, HIGHREZ = 0, 1, 2
SHAPES = ('ROUND', 'SQUARE', 'HIGHREZ')
EXACT = {'shape': SQUARE, 'brightness': 255, 'position': 340, 'bar': 62}
PROP_DEFAULT = '00 b1 01 54 00 00 00 ff 00 80 00 20 00 3e'   # the Propeller's own packet at start-up
SAVED = '00 b1 01 55 00 00 00 af 00 80 00 20 00 00'          # at the next start-up: POSITION, BRIGHTNESS, BAR BRIGHT kept
FPS = 60
TOL = 7                   # 8 -> 5 bit -> 8 bit rounding of a 15 bpp screen


def parse(cfg):
    """14 bytes -> (look, interpreted), as pinheck_display_look"""
    if cfg is None or len(cfg) != 14:
        return dict(EXACT), False
    w = [cfg[2 * i] << 8 | cfg[2 * i + 1] for i in range(7)]
    if (w[4], w[5]) != (W, H):
        return dict(EXACT), False
    return {'shape': w[2] if w[2] <= HIGHREZ else SQUARE, 'brightness': min(w[3], 255),
            'position': w[1], 'bar': min(w[6], 62)}, True


def rgb332(v):
    return (((v >> 5) & 7) * 255 // 7, ((v >> 2) & 7) * 255 // 7, (v & 3) * 255 // 3)


def scale2x(f, x, y, sx, sy):
    p = f[y * W + x]
    up = f[(y - 1) * W + x] if y > 0 else p
    down = f[(y + 1) * W + x] if y < H - 1 else p
    left = f[y * W + x - 1] if x > 0 else p
    right = f[y * W + x + 1] if x < W - 1 else p
    v, h = (down if sy else up), (right if sx else left)
    v2, h2 = (up if sy else down), (left if sx else right)
    return v if v == h and h != v2 and v != h2 else p


def render(look, f):
    """128x32 RGB332 frame -> 64 rows of 256 (r, g, b), as pinheck_display_render"""
    img = [[(0, 0, 0)] * LW for _ in range(LH)]
    dy = (look['position'] - 340) // 4
    pal = [rgb332(v) for v in range(256)]
    for y in range(LH):
        if not 0 <= y + dy < LH:
            continue
        row, Y, sy = img[y + dy], y >> 1, y & 1
        for x in range(LW):
            X, sx = x >> 1, x & 1
            if look['shape'] == HIGHREZ:
                c = pal[scale2x(f, X, Y, sx, sy)]
            elif look['shape'] == SQUARE or not (sx or sy):
                c = pal[f[Y * W + X]]
            else:
                dots = [pal[f[b * W + a]] for b in range(Y, Y + sy + 1) for a in range(X, X + sx + 1) if a < W and b < H]
                m = (1 + sx) * (1 + sy)
                c = tuple(sum(d[k] for d in dots) * look['bar'] // (124 * m) for k in range(3))
            row[x] = tuple(v * look['brightness'] // 255 for v in c)
    return img


def name(look):
    return '%s brightness %d position %d bar %d' % (SHAPES[look['shape']], look['brightness'], look['position'], look['bar'])


def near(p, q):
    return all(abs(a - b) <= TOL for a, b in zip(p, q))


def matches(rows, img):
    return all(near(rows[y][x], img[y][x]) for y in range(LH) for x in range(LW))


def packet(st):
    """the config packet the Propeller sends for the PIC32's menu state st"""
    words = (250, st['position'], st['shape'], st['brightness'], W, H, st['bar'])
    return ' '.join('%02x %02x' % (v >> 8, v & 255) for v in words)


def step(st, item):
    """one Enter on a MAIN SETTINGS display item, as DOM_V006 (0x9D017BBC-0x9D017D8C)"""
    st = dict(st)
    if item == 'shape':
        st['shape'] = (st['shape'] + 1) % 3
    elif item == 'brightness':
        st['brightness'] = st['brightness'] + 5 if st['brightness'] + 5 < 256 else 175
    elif item == 'position':
        st['position'] = st['position'] + 1 if st['position'] + 1 < 501 else 300
    else:
        st['bar'] = st['bar'] + 2 if st['bar'] + 2 < 63 else 0
    return st


def key_plan():
    """(frame, key, hold, packet expected after it or None, look of a snapshot or None)"""
    ev, t = [], 630
    st = {'shape': ROUND, 'brightness': 255, 'position': 340, 'bar': 62}

    def tap(key, dt, exp=None, snap=None, hold=6):
        nonlocal t
        ev.append((t, key, hold, exp, snap))
        t += dt
    tap('0', 60)                                    # main menu: SWITCH EDGE
    for _ in range(7):
        tap('RSHIFT', 30)                           # ... CHANGE: MAIN SETTINGS
    tap('0', 60)                                    # MAIN SETTINGS: FREE PLAY
    for _ in range(12):
        tap('RSHIFT', 30)                           # ... PIXEL SHAPE
    tap('F12', 36, snap=dict(st))                   # the start-up look
    # every value; then one more of the stored three (175, 341, 0), with the shape back at ROUND
    for item, n in (('shape', 3), ('brightness', 18), ('position', 202), ('bar', 33)):
        for _ in range(n):
            st = step(st, item)
            if (item == 'shape' or (item == 'brightness' and st['brightness'] == 175) or
                    (item == 'position' and st['position'] in (500, 300)) or (item == 'bar' and st['bar'] == 0)):
                tap('0', 24, packet(st))
                tap('F12', 12, snap=dict(st))
            else:
                tap('0', 15, packet(st))
        tap('RSHIFT', 30)                           # the next item
    tap('7', 60)                                    # Back: leave MAIN SETTINGS, which stores the settings
    return ev, t


def plan(d):
    ev, end = key_plan()
    open(os.path.join(d, 'keys.txt'), 'w').write(''.join('%d tap %d KEYCODE_%s\n' % (t, hold, key) for t, key, hold, _, _ in ev))
    open(os.path.join(d, 'frames'), 'w').write('%d\n' % (end + 30))
    open(os.path.join(d, 'snap3.txt'), 'w').write('1190 tap 2 KEYCODE_F12\n')


def read_png(path):
    d = open(path, 'rb').read()
    i, idat = 8, b''
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i + 8])
        if t == b'IHDR':
            w, h = struct.unpack('>II', d[i + 8:i + 16])
        elif t == b'IDAT':
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), 3 * w, [], bytearray(3 * w)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - 3] if x >= 3 else 0
            b, c = prev[x], prev[x - 3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[3 * x:3 * x + 3]) for x in range(w)])
        prev = line
    return rows


def verify(d):
    fail = 0
    log = open(os.path.join(d, 'prop2.log'), errors='replace').read().splitlines()
    got = [l.split(' ', 2)[2] for l in log if l.startswith('display: config ')]
    ev, _ = key_plan()
    want = [PROP_DEFAULT] + [e[3] for e in ev if e[3]]
    bad = next((k for k in range(max(len(got), len(want))) if k >= len(got) or k >= len(want) or got[k] != want[k]), None)
    if bad is not None:
        print('LOOK FAIL: config packet %d is %s, expected %s' % (bad, got[bad] if bad < len(got) else 'missing',
                                                                  want[bad] if bad < len(want) else 'none'))
        fail += 1
    else:
        print('look: %d config packets, one per menu step, as predicted' % len(got))
    if 'display: unknown config packet, exact pixels' in log:
        print('LOOK FAIL: the driver did not interpret a config packet')
        fail += 1
    for p in got:
        if not parse(bytes.fromhex(p))[1]:
            print('LOOK FAIL: packet %s does not decode' % p)
            fail += 1
    frames = [(pic / 80e6, f) for _, pic, _, f in read_log(os.path.join(d, 'frames2.bin'))]
    snaps = [e for e in ev if e[4]]
    files = ['dominos.png'] + ['domi%04d.png' % k for k in range(len(snaps) - 1)]
    for (t, _, _, _, st), fn in zip(snaps, files):
        path = os.path.join(d, 'snap', fn)
        if not os.path.exists(path):
            print('LOOK FAIL: no snapshot %s (%s)' % (fn, name(st)))
            fail += 1
            continue
        rows = read_png(path)
        recent = [f for ft, f in frames if ft <= t / FPS][-8:]
        hit = next((f for f in reversed(recent) if matches(rows, render(st, f))), None)
        if hit is None:
            print('LOOK FAIL: %s shows none of the last %d frames in the look %s' % (fn, len(recent), name(st)))
            fail += 1
        elif st != EXACT and matches(rows, render(EXACT, hit)):
            print('LOOK FAIL: %s (%s) equals the exact square rendering' % (fn, name(st)))
            fail += 1
        else:
            print('look: %s = %s' % (fn, name(st)))
    got = [l.split(' ', 2)[2] for l in open(os.path.join(d, 'prop3.log'), errors='replace').read().splitlines()
           if l.startswith('display: config ')]
    if got != [SAVED]:
        print('LOOK FAIL: after the restart the config packets are %s, expected %s' % (got, SAVED))
        fail += 1
    else:
        st = parse(bytes.fromhex(SAVED))[0]
        rows = read_png(os.path.join(d, 'snap3', 'dominos.png'))
        recent = [f for _, pic, _, f in read_log(os.path.join(d, 'frames3.bin')) if pic / 80e6 <= 1190 / FPS][-8:]
        if not any(matches(rows, render(st, f)) for f in recent):
            print('LOOK FAIL: after the restart the snapshot shows none of the last %d frames in the look %s' % (len(recent), name(st)))
            fail += 1
        else:
            print('look: after the restart %s' % name(st))
    print('look: %d failed' % fail)
    return fail


CROSS = ((ROUND, 255, 340, 62), (ROUND, 175, 340, 0), (ROUND, 200, 343, 30), (SQUARE, 255, 340, 62),
         (SQUARE, 180, 339, 62), (HIGHREZ, 255, 340, 62), (HIGHREZ, 230, 500, 62), (HIGHREZ, 255, 300, 10))


def crosscheck(prog):
    """this model against display.c (lookdump) on a pseudo-random frame, byte for byte"""
    out = subprocess.run([prog] + ['%d,%d,%d,%d' % c for c in CROSS], stdout=subprocess.PIPE, check=True).stdout
    frame, n, fail = out[:W * H], LW * LH * 3, 0
    for k, (shape, brightness, position, bar) in enumerate(CROSS):
        look = {'shape': shape, 'brightness': brightness, 'position': position, 'bar': bar}
        want = bytes(v for row in render(look, frame) for p in row for v in p)
        got = out[W * H + k * n:W * H + (k + 1) * n]
        if got != want:
            i = next(i for i in range(n) if i >= len(got) or got[i] != want[i])
            print('LOOK FAIL: display.c and look.py differ in %s at pixel (%d,%d)' % (name(look), i // 3 % LW, i // 3 // LW))
            fail += 1
    if not fail:
        print('look: display.c equals look.py in %d looks' % len(CROSS))
    return fail


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'crosscheck':
        sys.exit(1 if crosscheck(sys.argv[2]) else 0)
    elif len(sys.argv) == 3 and sys.argv[1] == 'plan':
        plan(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'verify':
        sys.exit(1 if verify(sys.argv[2]) else 0)
    else:
        sys.exit(__doc__)
