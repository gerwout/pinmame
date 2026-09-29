#!/usr/bin/env python3
import argparse
import struct
import sys

FRAME = 4096
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
    if len(d) < 512 or (len(d) - 512) % FRAME:
        sys.exit('frames: %s is not a .VID file' % path)
    return [d[i:i + FRAME] for i in range(512, len(d), FRAME)]


def ascii(f):
    return '\n'.join(''.join('#' if f[y * 128 + x] else '.' for x in range(128)) for y in range(32))


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
    if hub != [hub[0]] or hub[0] == 0xFFFFFFFF:
        sys.exit('frames: FAIL, decoded frames are not the firmware framebuffer (hub addresses %s)' % ['none' if h == 0xFFFFFFFF else '$%04x' % h for h in hub])
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
