#!/usr/bin/env python3
"""Playfield simulator check: in attract mode the firmware's console coil and servo commands
([MXXzzz], [SXXzzz]) and the simulator keys move the balls; the switches the simulator closes
(PINHECK_OUT_LOG 'W' lines) must follow the coils and servos.
  sim.py plan DIR     write DIR/send, send_at, send_gap, keys.txt, frames
  sim.py verify DIR   check DIR/out2.log against the plan"""
import sys

FPS = 60
SEND_AT, GAP = 10.0, 0.05
# (time, console command) and (time, keys, hold frames): one ball's trip round the playfield
# [MXX002] pulses a coil for 30 firmware loop units, about 5 ms: shorter than a frame
SEND = [(10.0, '[E97000]'), (12.0, '[M17002]'), (13.5, '[M16002]'), (17.0, '[M08002]'),
        (19.55, '[M06250]')] + [(20.95 + 0.05 * i, '[M05250]') for i in range(20)] + \
       [(28.0, '[M17002]'), (29.0, '[M16002]'), (29.5, '[M17002]'), (30.5, '[M16002]')]
KEYS = [(15.0, 'LCONTROL S', 1), (18.0, 'LCONTROL R', 1), (19.5, 'LCONTROL R', 1), (21.0, 'V', 1),
        (21.6, 'F', 1), (23.5, 'F', 1), (26.0, 'Q', 1), (32.0, 'Q', 1), (32.5, 'Q', 1),
        # service menu: SERVO TEST, NOID RIGHT runs the Noid's continuous-rotation servo, NOID STOP stops it
        (35.0, '0', 6), (36.0, 'RSHIFT', 6), (37.0, 'RSHIFT', 6), (38.0, 'RSHIFT', 6), (39.0, '0', 6), (40.0, '0', 6),
        (44.0, 'RSHIFT', 6), (45.0, '0', 6)]
END = 48.0


def plan(d):
    slots = {round((t - SEND_AT) / GAP): c for t, c in SEND}
    send = ''.join(slots.get(k, '') + '~' for k in range(max(slots) + 1))
    keys = ''.join('%d tap %d %s\n' % (round(t * FPS), h, ' '.join('KEYCODE_' + k for k in ks.split())) for t, ks, h in KEYS)
    for name, text in (('send', send), ('send_at', '%g' % SEND_AT), ('send_gap', '%g' % GAP),
                       ('keys.txt', keys), ('frames', '%d' % round(END * FPS))):
        open('%s/%s' % (d, name), 'w').write(text + ('\n' if name != 'keys.txt' else ''))


def load(path):
    sw, coils, servo = [], [], []
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
    return sw, coils, servo


def verify(d):
    sw, coils, servo = load(d + '/out2.log')
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

    def pulse(c, t0):
        on = coil_on(c, t0, t0 + 1.0)
        off = [t for t, w in coils if on and t > on[0] and not w >> (c - 1) & 1]
        return off[0] - on[0] if off else 1.0

    rest = sorted(s for s in (12, 13, 14, 11) if state(s, 9.0))
    check(rest == [12, 13, 14], 'at rest closed %s, expected the trough 12 13 14' % rest)
    eject = coil_on(18, 12.0, 13.0)
    check(pulse(18, 12.0) < 1.0 / FPS and pulse(17, 13.5) < 1.0 / FPS and pulse(9, 16.9) < 1.0 / FPS,
          'the test pulses are not shorter than a frame: %.4f %.4f %.4f s' % (pulse(18, 12.0), pulse(17, 13.5), pulse(9, 16.9)))
    check(eject and edges(12, eject[0], eject[0] + 0.1, 0) and edges(11, eject[0], eject[0] + 1.0, 1),
          'trough: [M17002] at 12.0 (coil 18 %s) did not move trough 1 (12) to the shooter lane (11)' % eject[:1])
    check(state(12, 13.4) == 1 and state(14, 13.4) == 0, 'trough: the next ball did not roll down to 12')
    launch = coil_on(17, 13.5, 14.5)
    check(launch and edges(11, launch[0], launch[0] + 0.1, 0), 'autolauncher: [M16002] did not clear the shooter lane')
    kick = coil_on(9, 16.9, 17.5)
    check(state(25, 16.9) == 1 and kick and edges(25, kick[0], kick[0] + 0.1, 0),
          'left scoop: held %d, coil 9 %s, released %s' % (state(25, 16.9), kick[:1], edges(25, 16.9, 17.5, 0)))
    check(edges(46, 18.0, 18.2, 1) and edges(36, 18.0, 18.6, 1), 'orbit: a left orbit shot without the post did not reach the right orbit (36)')
    post = coil_on(7, 19.5, 19.7)
    check(post and len(edges(46, 19.5, 20.2, 1)) == 2 and not edges(36, 19.5, 20.6),
          'up-post: coil 7 at %s, left orbit closed %s, right orbit %s: the ball must come back down the left orbit' %
          (post[:1], edges(46, 19.5, 20.2, 1), edges(36, 19.5, 20.6)))
    check(edges(96, 21.0, 21.2, 1), 'oven ramp opto (96) did not close')
    check(not edges(31, 21.5, 22.0), 'magnet: the ball left the magnet while coil 6 held it (Delivery target 31 closed)')
    check(edges(31, 23.5, 23.7, 1), 'magnet: the ball was not released (Delivery target 31 stayed open)')
    check(edges(14, 26.0, 27.5, 1) and [state(s, 27.9) for s in (11, 12, 13, 14)] == [0, 1, 1, 1],
          'drain: the ball did not return to the trough (11-14: %s)' % [state(s, 27.9) for s in (11, 12, 13, 14)])
    served = [t for t in coil_on(18, 27.9, 31.0)]
    check(len(edges(11, 27.9, 31.0, 1)) == 2 and len(edges(11, 27.9, 31.5, 0)) == 2,
          'two balls: two ejects (coil 18 at %s) did not reach and leave the shooter lane' % served[:1])
    check(len(edges(14, 32.0, 34.4, 1)) == 2 and [state(s, 34.4) for s in (11, 12, 13, 14)] == [0, 1, 1, 1],
          'two balls: Q twice did not drain both balls on the playfield (11-14: %s)' % [state(s, 34.4) for s in (11, 12, 13, 14)])
    run = [t for t, n, us in servo if n == 0 and 40.0 <= t < 44.0 and 500 < us < 600]
    home = edges(58, 40.0, 44.0)
    check(run and len(home) >= 3, 'Noid: servo 0 pulses %d times at 544 us, Noid Home (58) changed %s' % (len(run), home))
    check(not edges(58, 45.3, 48.0), 'Noid: still turning after NOID STOP')
    for f in fails:
        print('SIM FAIL: ' + f)
    print('sim: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'verify'):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2]))
