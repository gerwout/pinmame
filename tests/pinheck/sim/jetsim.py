#!/usr/bin/env python3
"""The Jetsons simulator check: in attract mode the firmware's console coil commands ([MXXzzz]) and the
simulator keys move the balls; the switches the simulator closes (PINHECK_OUT_LOG 'W' lines) must follow
the coils, and the servo outputs follow the game's servo range.
  jetsim.py plan DIR     write DIR/send, send_at, send_gap, keys.txt, frames
  jetsim.py verify DIR   check DIR/out2.log against the plan"""
import sys

FPS = 60
SEND_AT, GAP = 12.0, 0.05
# (time, console command) and (time, keys, hold frames): a ball's trip round the playfield, then two drains.
# [MXX002] pulses a coil for about 5 ms, shorter than a frame; [M16250] holds the up-post up for longer.
# The firmware itself launches a ball that reaches the shooter lane and ejects the scoop in attract mode
SEND = [(12.0, '[E97000]'), (14.0, '[M04002]'), (20.5, '[M19002]'),
        (23.55, '[M16250]'), (25.0, '[M02002]'), (25.5, '[M03002]'), (26.0, '[M06002]'), (26.5, '[M07002]'),
        (29.0, '[M04002]')]
KEYS = [(17.0, 'S', 1), (19.5, 'K', 1), (22.0, 'LCONTROL R', 1), (23.5, 'LCONTROL R', 1), (28.0, 'Q', 1), (31.0, 'Q', 1)]
END = 33.0
TROUGH = [92, 53, 54]                   # trough 1 (the eject position: Trough Opto 1), 2, 3
SERVO_US = (544, 2400)                  # the game's servo levels 0 and 255


def plan(d):
    slots = {round((t - SEND_AT) / GAP): c for t, c in SEND}
    send = ''.join(slots.get(k, '') + '~' for k in range(max(slots) + 1))
    keys = ''.join('%d tap %d %s\n' % (round(t * FPS), h, ' '.join('KEYCODE_' + k for k in ks.split())) for t, ks, h in KEYS)
    for name, text in (('send', send), ('send_at', '%g' % SEND_AT), ('send_gap', '%g' % GAP),
                       ('keys.txt', keys), ('frames', '%d' % round(END * FPS))):
        open('%s/%s' % (d, name), 'w').write(text + ('\n' if name != 'keys.txt' else ''))


def load(path):
    sw, coils, servo, out = [], [], [], {}
    for line in open(path):
        f = line.split()
        if len(f) < 3:
            continue
        t = float(f[0])
        if f[1] == 'W':
            sw += [(t, int(a), int(b)) for a, b in (x.split('=') for x in f[2:])]
        elif f[1] == 'S':
            coils.append((t, int(f[2], 16)))
        elif f[1] == 'V':
            servo.append((t, int(f[2]), float(f[3])))
        elif f[1] == 'P':
            for x in f[2:]:
                k, v = x.split('=')
                out.setdefault(k, []).append((t, int(v)))
    return sw, coils, servo, out


def verify(d):
    sw, coils, servo, out = load(d + '/out2.log')
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

    def edges(n, t0, t1, val=None):
        return [tt for tt, s, x in sw if s == n and t0 <= tt < t1 and (val is None or x == val)]

    def coil_on(c, t0, t1):
        return [t for t, w in coils if t0 <= t < t1 and w >> (c - 1) & 1]

    def level(name, t):
        v = None
        for tt, x in out.get(name, []):
            if tt > t:
                break
            v = x
        return v

    rest = [s for s in [51] + TROUGH if state(s, 11.0)]
    check(rest == TROUGH, 'at rest closed %s; expected the trough %s' % (rest, TROUGH))
    for n, us in ((0, 1476), (1, 1476)):
        w = sorted(set(round(u) for t, s, u in servo if s == n and 13.0 <= t < 14.0))
        lv = round((us - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
        check(w and all(abs(u - us) < 10 for u in w) and abs((level('S%d' % (57 + n), 14.0) or -9) - lv) <= 1,
              'servo %d (%s) in attract: %s us and output %d at %s; expected %d us (90 degrees) and %d' %
              (n, 'the Orbitty' if n == 0 else 'servo 1', w, 57 + n, level('S%d' % (57 + n), 14.0), us, lv))
    load_ = coil_on(5, 14.0, 15.0)
    check(load_ and edges(92, load_[0], load_[0] + 0.1, 0) and edges(51, load_[0], load_[0] + 1.0, 1),
          'trough: [M04002] at 14.0 (coil 5 %s) did not move trough 1 (92) to the shooter lane (51)' % load_[:1])
    check([state(s, 15.4) for s in TROUGH] == [1, 1, 0], 'trough: the two balls did not roll down to 92 and 53: %s' % [state(s, 15.4) for s in TROUGH])
    lane = edges(51, 14.0, 15.0, 1)
    launch = coil_on(6, lane[0], lane[0] + 0.3) if lane else []
    check(launch and edges(51, launch[0], launch[0] + 0.1, 0), 'autolauncher: the firmware did not launch the ball from the shooter lane (51 %s, coil 6 %s)' % (lane[:1], launch[:1]))
    into = edges(96, 17.0, 17.2, 1)
    scoop = coil_on(9, into[0], into[0] + 2.0) if into else []
    check(scoop and edges(96, scoop[0], scoop[0] + 0.1, 0) and not edges(96, into[0], scoop[0], 0),
          'scoop: opto 96 closed %s, the firmware fired coil 9 %s, released %s' % (into[:1], scoop[:1], edges(96, 17.0, 19.0, 0)))
    kick = coil_on(20, 20.4, 21.0)
    check(state(18, 20.4) == 1 and kick and edges(18, kick[0], kick[0] + 0.1, 0),
          'kickout hole: switch 18 held %d, coil 20 %s, released %s' % (state(18, 20.4), kick[:1], edges(18, 20.4, 21.0, 0)))
    check(edges(56, 22.0, 22.2, 1) and edges(11, 22.0, 22.6, 1), 'orbit: a left orbit shot without the post did not reach the right orbit (11)')
    post = coil_on(17, 23.5, 23.7)
    check(post and len(edges(56, 23.5, 24.2, 1)) == 2 and not edges(11, 23.5, 24.6),
          'up-post: coil 17 at %s, left orbit closed %s, right orbit %s: the ball must come back down the left orbit' %
          (post[:1], edges(56, 23.5, 24.2, 1), edges(11, 23.5, 24.6)))
    for c, s, t in ((3, 55, 25.0), (4, 55, 25.5), (7, 26, 26.0), (8, 26, 26.5)):
        on = coil_on(c, t, t + 0.4)
        check(on and edges(s, on[0], on[0] + 0.05, 1) and edges(s, on[0], on[0] + 0.1, 0),
              'flipper coil %d (%s) did not close and open its end-of-stroke switch %d' % (c, on[:1], s))
    check([state(s, 28.95) for s in [51] + TROUGH] == [0, 1, 1, 1], 'drain: the ball did not return to the trough (51, %s: %s)' % (TROUGH, [state(s, 28.95) for s in [51] + TROUGH]))
    check(len(edges(51, 29.0, 30.5, 1)) == 1 and len(edges(51, 29.0, 30.5, 0)) == 1, 'second ball: the load and the firmware\'s launch did not each move it')
    check([state(s, 32.5) for s in [51] + TROUGH] == [0, 1, 1, 1], 'second ball: Q did not drain it to the trough (51, %s: %s)' % (TROUGH, [state(s, 32.5) for s in [51] + TROUGH]))
    for f in fails:
        print('SIM FAIL: ' + f)
    print('sim: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'verify'):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2]))
