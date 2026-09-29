#!/usr/bin/env python3
"""Scripted game: coin, start, three balls played on the playfield simulator (a multiball among them),
every ball drained, high-score entry and match, checked against the firmware's own output.
  game.py plan DIR          write DIR/keys.txt, send, frames
  game.py verify DIR DMD    check DIR/{link2.log,out2.log,uart2.log,frames2.bin}; DMD = the update's DMD folder"""
import hashlib
import os
import re
import struct
import sys
import time

sys.dont_write_bytecode = True  # no __pycache__ next to the tests
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import link  # noqa: E402

FPS = 60
RTC = 1790683200          # PINHECK_RTC: 2026-09-29 12:00:00, so every run plays the same game
COIN, START, PLUNGE = 12.0, 13.0, 18.0
# (keys, seconds to the next shot): a scoop holds the ball about 2.5 s, the magnet about 1.2 s
BALL = [('LCONTROL S', 3.0), ('Z', 1.2), ('X', 1.2), ('M', 1.2), ('Z', 1.2), ('LCONTROL N', 1.5), ('RCONTROL N', 1.5),
        ('RCONTROL R', 1.5), ('C', 1.2), ('V', 2.0), ('RCONTROL S', 3.0), ('G', 1.5), ('LCONTROL L', 1.2), ('RCONTROL L', 1.2),
        ('B', 1.2), ('LCONTROL B', 1.2), ('RCONTROL B', 1.2), ('LCONTROL MINUS', 1.2), ('RCONTROL MINUS', 1.2),
        ('LCONTROL I', 1.2), ('RCONTROL I', 1.2), ('C', 1.2), ('C', 1.2), ('V', 2.0), ('V', 2.0), ('LCONTROL S', 3.0),
        ('LCONTROL R', 1.5), ('LCONTROL N', 1.5), ('Z', 1.2), ('X', 1.2), ('M', 1.2), ('RCONTROL S', 3.0), ('LCONTROL S', 3.0),
        ('LCONTROL R', 1.5), ('RCONTROL R', 1.5), ('LCONTROL S', 3.0), ('V', 2.0), ('LCONTROL R', 1.5), ('RCONTROL R', 1.5),
        ('LCONTROL S', 3.0), ('LSHIFT', 1.0), ('RSHIFT', 1.0)]
DRAIN = ['LCONTROL O', 'RCONTROL O', 'Q']      # left outlane, right outlane, between the flippers
# the switch each key's shot closes first (Task 1's key table)
SWITCH = {'LCONTROL S': 25, 'RCONTROL S': 48, 'Z': 28, 'X': 27, 'M': 26, 'LCONTROL N': 38, 'RCONTROL N': 37, 'LCONTROL R': 46,
          'RCONTROL R': 36, 'C': 95, 'V': 96, 'G': 47, 'LCONTROL L': 41, 'RCONTROL L': 42, 'B': 44, 'LCONTROL B': 45,
          'RCONTROL B': 43, 'LCONTROL MINUS': 22, 'RCONTROL MINUS': 16, 'LCONTROL I': 23, 'RCONTROL I': 17,
          'LCONTROL O': 24, 'RCONTROL O': 18, 'LSHIFT': 4, 'RSHIFT': 3}
SWEEP, NEXT = 15, 25                           # Q every second after the drain, next plunge after the drain
ENTRY, END = 16, 12                            # name entry keys after the last drain; the end after the last key
INITIALS = 'ACE'


def script():
    """(seconds, keys, hold frames) of the whole game, and the drain time of each ball"""
    ev, drains, p = [(COIN, '5', 6), (START, '1', 6)], [], PLUNGE
    for b in range(3):
        ev.append((p, 'SPACE', 40))
        t = p + 3
        for k, dt in BALL:
            ev.append((t, k, 6 if 'SHIFT' in k else 1))
            t += dt
        ev.append((t, DRAIN[b], 1))
        ev += [(t + 1 + i, 'Q', 1) for i in range(SWEEP)]
        drains.append(t)
        p = t + NEXT
    return ev, drains


def entry_and_end(last_drain):
    """name entry (right flipper: next letter, start: take it; a letter starts from the one before) and the end"""
    ev, t = [], last_drain + ENTRY
    for k, n in (('1', 0), ('RSHIFT', 2), ('1', 0), ('RSHIFT', 2), ('1', 0)):   # A, C, E
        for _ in range(max(n, 1)):
            ev.append((t, k, 6))
            t += 0.5
    return ev, t + END


def plan(d):
    ev, drains = script()
    more, end = entry_and_end(drains[-1])
    keys = ''.join('%d tap %d %s\n' % (round(t * FPS), h, ' '.join('KEYCODE_' + x for x in k.split())) for t, k, h in ev + more)
    for name, text in (('keys.txt', keys), ('send', '[E97000]'), ('rtc', '%d' % RTC), ('frames', '%d' % round(end * FPS))):
        open('%s/%s' % (d, name), 'w').write(text + ('\n' if name != 'keys.txt' else ''))


def board(path):
    """(switch changes, coil rises, levels) from PINHECK_OUT_LOG: W, S and P lines"""
    sw, rises, lv, prev = [], [], {}, 0
    for line in open(path):
        f = line.split()
        if len(f) < 3:
            continue
        t = float(f[0])
        if f[1] == 'W':
            sw += [(t, int(a), int(b)) for a, b in (x.split('=') for x in f[2:])]
        elif f[1] == 'S':
            w = int(f[2], 16)
            if w & ~prev:
                rises.append((t, w & ~prev))
            prev = w
        elif f[1] == 'P':
            for x in f[2:]:
                k, v = x.split('=')
                lv.setdefault(k, []).append((t, int(v)))
    return sw, rises, lv


def frames(path):
    raw, rec = open(path, 'rb').read(), 20 + 4096
    return [(struct.unpack_from('<Q', raw, i + 8)[0] / 80e6, hashlib.md5(raw[i + 20:i + rec]).digest())
            for i in range(0, len(raw) - rec + 1, rec)]


def clip_shown(dmd, name, t, shown):
    """frames of DMD/_D<x>/<name>.VID shown pixel-exact within 3 s after the clip started"""
    path = '%s/_D%s/%s.VID' % (dmd, name[0], name)
    if not os.path.exists(path):
        return 0
    v = open(path, 'rb').read()
    want = set(hashlib.md5(v[512 + 4096 * k:512 + 4096 * (k + 1)]).digest() for k in range((len(v) - 512) // 4096))
    return len(set(h for tt, h in shown if t <= tt < t + 3.0 and h in want))


def verify(d, dmd):
    pk = link.packets(d + '/link2.log')
    sw, rises, lv = board(d + '/out2.log')
    uart = open(d + '/uart2.log', 'rb').read().decode('latin-1').replace('\r', '')
    ev, drains = script()
    plunges = [t for t, k, h in ev if k == 'SPACE']
    fails, checks = [], [0]

    def check(ok, what):
        checks[0] += 1
        if not ok:
            fails.append(what)

    def state(n, t):
        v = 0
        for tt, s, x in sw:
            if tt > t:
                break
            if s == n:
                v = x
        return v

    def closed(n, t0, t1):
        return [t for t, s, v in sw if s == n and v and t0 <= t < t1]

    def fired(c, t0, t1):
        return [t for t, w in rises if t0 <= t < t1 and w >> (c - 1) & 1]

    def ejects(t0, t1):   # the trough coil pulses twice per ball, 95 ms apart
        ts = fired(18, t0, t1)
        return [t for i, t in enumerate(ts) if i == 0 or t - ts[i - 1] > 0.5]

    sc = link.scores(pk)
    st = link.status(pk)
    end = drains[-1] + 60
    # coin, start, attract
    check(closed(7, COIN, COIN + 0.3), 'the coin key closed no coin switch (7)')
    starts = [x for x in st if x[0] > START and x[2] == 1]
    check(starts and starts[0][0] < START + 1.5, 'no status with ball 1 within 1.5 s of the start key: %s' % st[:4])
    check([p for t, p, v in sc if COIN < t < START + 1.5 and v == 0] == [1, 2, 3, 4], 'the four scores were not zeroed for the game')
    blinks = [t for t, v in lv.get('L91', []) if v >= 128]
    check(len([t for t in blinks if 5 < t < COIN]) >= 3 and not [t for t in blinks if START + 1 < t < drains[-1]],
          'the start button lamp (91) must blink in attract mode and stay dark in the game')
    # every ball: served from the trough, plunged, drained with all others
    served = ejects(START, end)
    for b in range(3):
        t0 = START if b == 0 else drains[b - 1]
        msg = [t for t, m in link.said(pk, r'PLAYER:1 BALL:%d' % (b + 1)) if t0 < t < plunges[b] + 5]
        check([x for x in st if t0 < x[0] < plunges[b] and x[2] == b + 1], 'ball %d: no status packet before its plunge' % (b + 1))
        check(msg, "ball %d: the scrolling text never showed 'PLAYER:1 BALL:%d'" % (b + 1, b + 1))
        e = [t for t in served if t0 < t < plunges[b]]
        check(len(e) == 1 and closed(11, e[0], e[0] + 1.0), 'ball %d: trough ejects %s before the plunge, shooter lane %s' % (b + 1, e, closed(11, t0, plunges[b])))
        gone = [t for t, s, v in sw if s == 11 and not v and plunges[b] < t < plunges[b] + 1.5]
        check(gone, 'ball %d: the plunger did not clear the shooter lane' % (b + 1))
        home = e[0] - 0.05 if e else drains[b]
        check([state(n, home) for n in (12, 13, 14)] == [1, 1, 1], 'ball %d: the trough was not full when it was served' % (b + 1))
    check([state(n, drains[-1] + SWEEP + 1) for n in (11, 12, 13, 14)] == [0, 1, 1, 1], 'the last ball: not every ball back in the trough')
    # the skill shot: the left scoop straight after the plunge is worth a million, then the scoop kicks the ball out
    ls = plunges[0] + 3
    jump = [v2 - v1 for (t1, p1, v1), (t2, p2, v2) in zip(sc, sc[1:]) if ls < t2 < ls + 1.0]
    kick = fired(9, ls, ls + 5)
    check(closed(25, ls - 0.1, ls + 0.2) and jump == [1000000] and kick, 'skill shot: left scoop %s, score steps %s, kick %s' % (closed(25, ls - 0.1, ls + 0.2), jump, kick[:1]))
    # multiball: more balls served during a ball than balls played
    extra = [t for t in served if not any(t0 < t < p for t0, p in zip([START] + drains, plunges))]
    launched = fired(17, START, end)
    check(extra and launched, 'no multiball: extra trough ejects %s, autolauncher %s' % (extra, launched))
    missing = sorted(set(k for t, k, h in ev if k in SWITCH and not closed(SWITCH[k], START, end)))
    check(not missing, 'the simulator never closed the switch of %s' % missing)
    # coils the shots and flippers fire
    for c, what in ((3, 'lower pop'), (4, 'left pop'), (5, 'right pop'), (6, 'magnet'), (9, 'left scoop'), (13, 'right scoop'),
                    (11, 'left sling'), (20, 'right sling'), (10, 'left flipper'), (19, 'right flipper')):
        check(fired(c, START, end), 'the %s (solenoid %d) never fired' % (what, c))
    # score, high-score entry, match, back to attract
    vals = [v for t, p, v in sc if p == 1 and t > START]
    check(all(b >= a for a, b in zip(vals, vals[1:])), 'player 1 score went down')
    final = vals[-1] if vals else 0
    over = [x for x in st if x[0] > drains[-1] and x[2] == 0]
    check(over, 'no status packet with ball 0 after the last drain')
    hs = [x for x in link.hiscores(pk) if x[0] > drains[-1]]
    rank = [r for t, r, v, i in hs if i == INITIALS and v == final]
    check(len(hs) == 5 and rank, 'high-score table after the game %s, expected %s with %d' % ([x[1:] for x in hs], INITIALS, final))
    clips = [(t, v) for t, v in link.videos(pk) if START < t < end]
    entry = [v for t, v in clips if t > drains[-1] and v.startswith('NE')]
    match = [(t, v) for t, v in clips if t > drains[-1] and v.startswith('NA')]
    check(entry[:1] == ['NEA'] and match, 'name entry clips %s, match clips %s' % (entry, match))
    check(len(re.findall(r'Send High Scores', uart.split('Ball Search: DISABLED', 1)[-1])) == 5,
          'UART1: the high scores were not sent to the display five times after the game')
    for line in ('pizzaDispatchModeStart[player] 0', 'COLLECTING JP??', 'MODES ARE ENDING'):
        check(line in uart, 'UART1: the game never printed %r' % line)
    check('%s ' % time.strftime('%Y/%-m/%-d', time.gmtime(RTC)) in uart, 'UART1: the clock did not start at PINHECK_RTC')
    # the display: clips the firmware starts are shown pixel-exact
    shown = frames(d + '/frames2.bin')
    exact = [(t, v) for t, v in clips if clip_shown(dmd, v, t, shown)]
    check([v for t, v in exact if v == 'BLK'], 'the skill shot clip BLK was never shown pixel-exact')
    check(len(exact) * 2 > len(clips), 'only %d of %d clips shown pixel-exact' % (len(exact), len(clips)))
    for f in fails:
        print('GAME FAIL: ' + f)
    print('game: 3 balls, %d trough ejects, %d autolaunches, final score %d' % (len(served), len(launched), final))
    print('game: high score %s %s, match %s' % (rank[0] + 1 if rank else '-', INITIALS, ' '.join(v for t, v in match)))
    print('game: %d clips started, %d shown pixel-exact' % (len(clips), len(exact)))
    print('game: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) < 3 or sys.argv[1] not in ('plan', 'verify') or (sys.argv[1] == 'verify') != (len(sys.argv) == 4):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2], sys.argv[3]))
