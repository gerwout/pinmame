#!/usr/bin/env python3
"""The display module's look (spec M8/M9 addendum 5): a model of display.c's config decoding and rendering.
  look.py crosscheck BIN   the model against display.c through BIN (lookdump)"""
import subprocess
import sys

W, H = 128, 32
LW, LH = 2 * W, 2 * H
ROUND, SQUARE, HIGHREZ = 0, 1, 2
SHAPES = ('ROUND', 'SQUARE', 'HIGHREZ')
EXACT = {'shape': SQUARE, 'brightness': 255, 'position': 340, 'bar': 62}


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
    else:
        sys.exit(__doc__)
