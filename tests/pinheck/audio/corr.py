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
    if len(sys.argv) not in (3, 4):
        sys.exit('usage: corr.py capture.wav reference.wav [playback_rate]')
    rate, cap = load(sys.argv[1])
    rrate, ref = load(sys.argv[2])
    if len(sys.argv) == 4:
        rrate = float(sys.argv[3])
    n = int(len(ref) * rate / rrate)
    t = np.arange(n) * (rrate / rate)
    ref = np.stack([np.interp(t, np.arange(len(ref)), ref[:, c]) for c in range(2)], axis=1)
    if len(cap) < n:
        sys.exit('corr: capture shorter than the reference')
    a, b = cap.sum(axis=1), ref.sum(axis=1)
    m = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    x = np.fft.irfft(np.fft.rfft(a, m) * np.conj(np.fft.rfft(b, m)), m)[:len(a) - n + 1]
    lag = int(np.argmax(np.abs(x)))
    out = []
    for c in range(2):
        v = ref[:, c] - ref[:, c].mean()
        best = -1.0
        for d in range(max(0, lag - 100), min(len(cap) - n, lag + 100) + 1):
            u = cap[d:d + n, c] - cap[d:d + n, c].mean()
            best = max(best, float(np.dot(u, v) / np.sqrt(np.dot(u, u) * np.dot(v, v))))
        out.append(best)
    print('corr: L %.4f R %.4f at %.3f s (%d samples at %d Hz, source played at %.2f Hz)' % (out[0], out[1], lag / rate, n, rate, rrate))
    sys.exit(0 if min(out) >= 0.95 else 1)


if __name__ == '__main__':
    main()
