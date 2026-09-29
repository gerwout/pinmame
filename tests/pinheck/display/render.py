#!/usr/bin/env python3
"""Check a PinMAME screen snapshot of a pinHeck game against the decoded frame log:
the visible area is SCALE x the 128x32 frame wide and the full screen high, the frame is
drawn SCALE x SCALE at the top left, and everything else is black apart from the core's
lamp/switch/solenoid panel, which starts 3 rows under the display and uses at most its two
dot pens (off and on; their exact values depend on the palette's brightness and gamma)."""
import argparse
import struct
import sys
import zlib

sys.dont_write_bytecode = True  # no __pycache__ next to the tests
from frames import FRAME, read_log  # noqa: E402

W, H = 128, 32
SCALE = 2
SCREEN_H = 256            # CORE_SCREENY: standalone PinMAME shows the full screen height
TOL = 7                   # 8 -> 5 bit -> 8 bit rounding of a 15 bpp screen


def read_png(path):
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('render: FAIL, %s is not a PNG' % path)
    i, idat = 8, b''
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i + 8])
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', d[i + 8:i + 18])
        elif t == b'IDAT':
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    if (depth, ctype) != (8, 2):
        sys.exit('render: FAIL, %s is not 8 bit RGB' % path)
    raw, bpp, stride = zlib.decompress(idat), 3, 3 * w
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[3 * x:3 * x + 3]) for x in range(w)])
        prev = line
    return w, h, rows


def rgb332(v):
    return (((v >> 5) & 7) * 255 // 7, ((v >> 2) & 7) * 255 // 7, (v & 3) * 255 // 3)


def near(p, q):
    return all(abs(a - b) <= TOL for a, b in zip(p, q))


def frame_matches(rows, f):
    return all(near(rows[y][x], rgb332(f[(y // SCALE) * W + x // SCALE])) for y in range(H * SCALE) for x in range(W * SCALE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('png')
    ap.add_argument('log')
    ap.add_argument('--last', type=int, default=8, help='the snapshot must show one of the last N logged frames')
    a = ap.parse_args()
    w, h, rows = read_png(a.png)
    fail = 0
    if (w, h) != (W * SCALE, SCREEN_H):
        print('render: FAIL, visible area %dx%d, want %dx%d (the %dx%d frame at %dx%d, full screen height)' % (w, h, W * SCALE, SCREEN_H, W, H, SCALE, SCALE))
        fail = 1
    if w < W * SCALE or h < H * SCALE:
        sys.exit('render: FAIL, the visible area cannot hold the scaled frame')
    frames = [f for _, _, _, f in read_log(a.log)][-a.last:]
    hit = next((k for k in range(len(frames) - 1, -1, -1) if frame_matches(rows, frames[k])), None)
    if hit is None:
        print('render: FAIL, the frame region shows none of the last %d decoded frames' % len(frames))
        fail = 1
    elif frames[hit].count(frames[hit][0]) == FRAME:
        print('render: FAIL, the matched frame is uniform, nothing was checked')
        fail = 1
    else:
        print('render: frame region equals decoded frame %d of the last %d, %dx%d per pixel' % (hit, len(frames), SCALE, SCALE))
    panel = H * SCALE + 3
    bad = [(x, y) for y in range(min(h, panel)) for x in range(w)
           if not (y < H * SCALE and x < W * SCALE) and not near(rows[y][x], (0, 0, 0))]
    pens = set(rows[y][x] for y in range(panel, h) for x in range(w)) - {(0, 0, 0)}
    if bad:
        x, y = bad[0]
        print('render: FAIL, %d pixels between the frame and the core panel are not black, first at (%d,%d) = %s' % (len(bad), x, y, rows[y][x]))
        fail = 1
    elif len(pens) > 2:
        print('render: FAIL, the core panel area holds %d colours, more than its two dot pens' % len(pens))
        fail = 1
    else:
        print('render: %dx%d visible, black outside the frame apart from the core panel (%d dot pens)' % (w, h, len(pens)))
    sys.exit(fail)


if __name__ == '__main__':
    main()
