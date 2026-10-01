#!/usr/bin/env python3
import argparse
import os
import struct
import sys

# the module PINHECK_GAME has: 128x32, or The Jetsons' 128x64, whose firmware keeps no whole frame in hub RAM;
# America's Most Haunted's raw DMD logs each cycle of 16 subframes, one byte per dot (0-15), and its .VID frames
# are 4 bpp (two dots a byte, the left one high)
W, H = 128, 64 if os.environ.get('PINHECK_GAME') == 'jetsons' else 32
FRAME = W * H
DMD = os.environ.get('PINHECK_GAME') == 'amh'
TORN = 0xFFFFFFFF  # a raw DMD cycle the firmware's frame copy crossed: no whole frame
PROP_HZ = 104e6
PIC_HZ = 80e6


def read_log(path):
    try:
        d = open(path, 'rb').read()
    except OSError:
        sys.exit('frames: FAIL, no frame log %s' % path)
    rec = 20 + FRAME
    if len(d) % rec:
        sys.exit('frames: %s is not a whole number of records' % path)
    return [struct.unpack_from('<QQI', d, i) + (d[i + 20:i + rec],) for i in range(0, len(d), rec)]


def read_vid(path):
    d = open(path, 'rb').read()
    n = FRAME // 2 if DMD else FRAME
    if len(d) < 512 or (len(d) - 512) % n:
        sys.exit('frames: %s is not a .VID file' % path)
    if DMD:
        return [bytes(x for b in d[i:i + n] for x in (b >> 4, b & 15)) for i in range(512, len(d), n)]
    return [d[i:i + n] for i in range(512, len(d), n)]


def ascii(f):
    return '\n'.join(''.join('#' if f[y * W + x] else '.' for x in range(W)) for y in range(H))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--vid')
    ap.add_argument('--after', type=float, default=0.0, help='ignore frames latched before this many seconds of PIC32 time')
    ap.add_argument('--before', type=float, default=1e9, help='ignore frames latched from this many seconds of PIC32 time on')
    ap.add_argument('--show', type=int)
    a = ap.parse_args()
    recs = [r for r in read_log(a.log) if a.after <= r[1] / PIC_HZ < a.before]
    log = [(t, f) for t, _, _, f in recs]
    if not log:
        sys.exit('frames: no frames after %.1fs' % a.after)
    span = (log[-1][0] - log[0][0]) / PROP_HZ
    print('frames: %d frames from %.2fs to %.2fs, %.1f per second' % (len(log), recs[0][1] / PIC_HZ, recs[-1][1] / PIC_HZ, (len(log) - 1) / span if span > 0 else 0.0))
    hub = sorted(set(at for _, _, at, f in recs if f.count(f[0]) != FRAME))
    if not hub:
        sys.exit('frames: FAIL, every frame is uniform')
    if DMD:
        whole = [at for _, _, at, _ in recs if at != TORN]
        if hub != [hub[0], TORN][:len(hub)] or hub[0] == TORN or len(whole) < len(recs) // 2:
            sys.exit('frames: FAIL, raw DMD cycles at hub addresses %s, %d of %d whole' % (['none' if h == TORN else '$%04x' % h for h in hub], len(whole), len(recs)))
        print('frames: %d of %d cycles equal the firmware framebuffer at hub $%04x at their end, the others the frame copy crossed' % (len(whole), len(recs), hub[0]))
        recs = [r for r in recs if r[2] != TORN]
        log = [(t, f) for t, _, _, f in recs]
    elif H == 64:
        if hub != [0xFFFFFFFF]:
            sys.exit('frames: FAIL, a 128x64 frame is whole in hub RAM at %s' % ['$%04x' % h for h in hub if h != 0xFFFFFFFF])
        print('frames: no 128x64 frame is whole in hub RAM (the firmware sends it from two 1024-byte pages)')
    elif hub != [hub[0]] or hub[0] == 0xFFFFFFFF:
        sys.exit('frames: FAIL, decoded frames are not the firmware framebuffer (hub addresses %s)' % ['none' if h == 0xFFFFFFFF else '$%04x' % h for h in hub])
    else:
        print('frames: every non-uniform frame equals the firmware framebuffer at hub $%04x when latched' % hub[0])
    if a.show is not None:
        print(ascii(log[a.show][1]))
    if a.vid:
        vid = read_vid(a.vid)
        frames = [f for _, f in log]
        first = next((k for k, f in enumerate(frames) if f in (vid[0], vid[1])), None)
        if first is None:
            sys.exit('frames: FAIL, neither frame 0 nor frame 1 of %s appears' % a.vid)
        start = 0 if frames[first] == vid[0] else 1
        want, k = start + 1, first + 1
        while k < len(frames) and want < len(vid):
            if frames[k] == vid[want]:
                want += 1
            elif frames[k] != vid[want - 1]:
                break
            k += 1
        print('frames: %s: frames %d..%d of %d shown pixel-exact, contiguous and in order (log frames %d..%d)' % (a.vid.split('/')[-1], start, want - 1, len(vid), first, k - 1))
        if want != len(vid):
            sys.exit('frames: FAIL, clip interrupted at frame %d' % want)


if __name__ == '__main__':
    main()
