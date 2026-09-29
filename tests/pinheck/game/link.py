#!/usr/bin/env python3
"""PIC32-to-Propeller link packets as logged by PINHECK_LINK_LOG: '<seconds> <16 hex bytes> gap <cycles>',
byte 15 the command.
  link.py LOG   print the decoded game-flow packets"""
import re
import sys

SFX, VIDEO, SCORE, STATUS, HISCORE, TICKER = 0x01, 0x02, 0x03, 0x0C, 0x0E, 0x0F


def packets(path):
    """(seconds, command, payload bytes 0-14) for every logged packet"""
    out = []
    for line in open(path):
        f = line.split()
        if len(f) >= 17:
            b = bytes(int(x, 16) for x in f[1:17])
            out.append((float(f[0]), b[15], b[:15]))
    return out


def u32(b):
    return b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24


def text(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)


def scores(pk):
    """(seconds, player, score) of every score update"""
    return [(t, b[0], u32(b[1:5])) for t, c, b in pk if c == SCORE]


def status(pk):
    """(seconds, players, ball, player) of every status packet"""
    return [(t, b[0], b[1], b[2]) for t, c, b in pk if c == STATUS]


def videos(pk):
    """(seconds, clip name) of every clip started; the clip is DMD/_D<first letter>/<name>.VID"""
    return [(t, b[:3].decode('latin-1')) for t, c, b in pk if c == VIDEO and b[:3].isalnum()]


def hiscores(pk):
    """(seconds, rank, score, initials) of every high-score table entry sent"""
    return [(t, b[0], u32(b[1:5]), text(b[5:8])) for t, c, b in pk if c == HISCORE]


def _runs(pk):
    """each run of scrolling text as [(seconds, character)]: a run goes on while each 8-character window
    is the previous one moved on by one character"""
    runs, prev = [], None
    for t, c, b in pk:
        if c != TICKER:
            continue
        w = text(b[1:9]).rstrip('.')
        if prev is not None and len(w) == len(prev) and w[:-1] == prev[1:]:
            runs[-1].append((t, w[-1]))
        elif w != prev:
            runs.append([(t, ch) for ch in w])
        prev = w
    return runs


def ticker(pk):
    """(seconds, text) for each run of scrolling text, the text as far as it scrolled"""
    return [(r[0][0], ''.join(ch for _, ch in r)) for r in _runs(pk)]


def said(pk, pattern):
    """(seconds, match) for each match of the regular expression in the scrolling text, at the time its last
    character scrolled in"""
    out = []
    for r in _runs(pk):
        s = ''.join(ch for _, ch in r)
        out += [(r[m.end() - 1][0], m.group(0)) for m in re.finditer(pattern, s)]
    return out


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pk = packets(sys.argv[1])
    for t, n, b, p in status(pk):
        print('%9.3f status players %d ball %d player %d' % (t, n, b, p))
    for t, p, s in scores(pk):
        print('%9.3f score player %d %d' % (t, p, s))
    for t, v in videos(pk):
        print('%9.3f video %s' % (t, v))
    for t, r, s, i in hiscores(pk):
        print('%9.3f high score %d %d %s' % (t, r, s, i))
    for t, m in ticker(pk):
        print('%9.3f ticker %s' % (t, m[:60]))
