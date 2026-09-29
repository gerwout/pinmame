#!/usr/bin/env python3
import sys
import wave

import numpy as np


def load(path):
    w = wave.open(path, 'rb')
    rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    d = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64).reshape(-1, ch)
    w.close()
    return rate, d


def main():
    if len(sys.argv) not in (3, 4, 5):
        sys.exit('usage: corr.py capture.wav reference.wav [playback_rate [fps]]')
    rate, cap = load(sys.argv[1])
    rrate, ref = load(sys.argv[2])
    if len(sys.argv) >= 4:
        rrate = float(sys.argv[3])
    fps = int(sys.argv[4]) if len(sys.argv) == 5 else 60
    # the OSD mixes sample_rate // fps samples per frame, so that many per emulated second of fps frames
    eff = (rate // fps) * fps
    n = int(len(ref) * eff / rrate)
    t = np.arange(n) * (rrate / eff)
    ref = np.stack([np.interp(t, np.arange(len(ref)), ref[:, c]) for c in range(2)], axis=1)
    # same first-order DC blocker as audio.c (10 Hz), since the emulated output includes it
    r = np.exp(-2.0 * np.pi * 10.0 / rate)
    hp = np.zeros_like(ref)
    x1 = np.zeros(2)
    y1 = np.zeros(2)
    for i in range(len(ref)):
        y1 = ref[i] - x1 + r * y1
        x1 = ref[i]
        hp[i] = y1
    ref = hp
    if len(cap) < n:
        sys.exit('corr: capture shorter than the reference')
    a, b = cap.sum(axis=1), ref.sum(axis=1)
    m = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    x = np.fft.irfft(np.fft.rfft(a, m) * np.conj(np.fft.rfft(b, m)), m)[:len(a) - n + 1]
    lag = int(np.argmax(np.abs(x)))
    def ncc(u, v):
        u = u - u.mean()
        v = v - v.mean()
        return float(np.dot(u, v) / np.sqrt(np.dot(u, u) * np.dot(v, v)))

    stereo = ncc(ref[:, 0], ref[:, 1])
    out, at = [], []
    for c in range(2):
        best, bd = -1.0, lag
        for d in range(max(0, lag - 100), min(len(cap) - n, lag + 100) + 1):
            r = ncc(cap[d:d + n, c], ref[:, c])
            if r > best:
                best, bd = r, d
        out.append(best)
        at.append(bd)
    swap = [ncc(cap[at[c]:at[c] + n, c], ref[:, 1 - c]) for c in range(2)]
    print('corr: L %.4f R %.4f, swapped L~R %.4f R~L %.4f, source L/R %.4f, at %.4f s (%d samples at %d Hz, %d per emulated second, source played at %.2f Hz)' % (
        out[0], out[1], swap[0], swap[1], stereo, lag / eff, n, rate, eff, rrate))
    if abs(stereo) > 0.5:
        sys.exit('corr: reference is not stereo enough to detect a channel swap')
    sys.exit(0 if min(out) >= 0.95 and max(abs(x) for x in swap) <= 0.2 else 1)


if __name__ == '__main__':
    main()
