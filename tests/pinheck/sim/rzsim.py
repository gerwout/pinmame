#!/usr/bin/env python3
"""Rob Zombie's Spookshow simulator check: in attract mode the firmware's console coil and servo commands
([MXXzzz], [SXXzzz]) and the simulator keys move the balls; the switches the simulator closes
(PINHECK_OUT_LOG 'W' lines) must follow the coils and servos.
  rzsim.py plan DIR     write DIR/send, send_at, send_gap, keys.txt, frames
  rzsim.py verify DIR   check DIR/out2.log against the plan"""
import sys

FPS = 60
SEND_AT, GAP = 10.0, 0.05
# (time, console command) and (time, keys, hold frames): a ball's trip round the playfield, two balls locked on the
# right rail and released one by one, two balls drained. [MXX002] pulses a coil for about 5 ms, shorter than a
# frame; [S00160] sets the gate servo to 160 degrees
SEND = [(10.0, '[E97000]'), (12.0, '[M17002]'), (13.5, '[M16002]'), (16.5, '[M03002]'), (19.0, '[M06002]'),
        (22.0, '[M17002]'), (22.5, '[M16002]'), (24.8, '[M04002]'), (25.6, '[M04002]'), (26.5, '[S00160]'),
        (28.0, '[M02002]'), (28.5, '[M07002]'), (29.0, '[M18002]'), (29.5, '[M12002]'),
        (31.0, '[M17002]'), (32.0, '[M16002]'), (32.5, '[M17002]')]
KEYS = [(15.0, 'V', 1), (18.0, 'RCONTROL N', 1), (20.0, 'RCONTROL N', 1), (21.0, 'RCONTROL N', 1), (23.8, 'RCONTROL N', 1),
        (26.3, 'G', 1), (27.3, 'G', 1), (30.0, 'Q', 1), (30.5, 'Q', 1), (33.0, 'SPACE', 40), (35.0, 'Q', 1), (35.5, 'Q', 1)]
END = 38.0
TROUGH = list(range(12, 19))


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

    rest = [s for s in [11] + TROUGH if state(s, 9.0)]
    check(rest == TROUGH and state(48, 9.0) == 0, 'at rest closed %s, drop target %d; expected the trough 12-18, the target up' % (rest, state(48, 9.0)))
    check(pulse(18, 12.0) < 1.0 / FPS and pulse(17, 13.5) < 1.0 / FPS and pulse(4, 16.4) < 1.0 / FPS,
          'the test pulses are not shorter than a frame: %.4f %.4f %.4f s' % (pulse(18, 12.0), pulse(17, 13.5), pulse(4, 16.4)))
    eject = coil_on(18, 12.0, 13.0)
    check(eject and edges(12, eject[0], eject[0] + 0.1, 0) and edges(11, eject[0], eject[0] + 1.0, 1),
          'trough: [M17002] at 12.0 (coil 18 %s) did not move trough 1 (12) to the shooter lane (11)' % eject[:1])
    check([state(s, 13.4) for s in TROUGH] == [1] * 6 + [0], 'trough: the six balls did not roll down to 12-17: %s' % [state(s, 13.4) for s in TROUGH])
    launch = coil_on(17, 13.5, 14.5)
    check(launch and edges(11, launch[0], launch[0] + 0.1, 0), 'autoplunger: [M16002] did not clear the shooter lane')
    vuk = coil_on(4, 16.4, 17.0)
    check(state(43, 16.4) == 1 and vuk and edges(43, vuk[0], vuk[0] + 0.1, 0),
          'VUK: held %d, coil 4 %s, released %s' % (state(43, 16.4), vuk[:1], edges(43, 16.4, 17.0, 0)))
    check(edges(35, 18.0, 18.2, 1) == [] and edges(48, 18.0, 18.2, 1) and state(48, 18.9) == 1,
          'drop target: the right inner orbit with the target up did not knock it down (48 %s, 35 %s)' % (edges(48, 18.0, 18.2), edges(35, 18.0, 18.2)))
    reset = coil_on(7, 19.0, 19.5)
    check(reset and edges(48, reset[0], reset[0] + 0.1, 0) and state(48, 19.9) == 0, 'drop target: coil 7 (%s) did not raise it' % reset[:1])
    check(edges(48, 20.0, 20.2, 1) and not edges(35, 20.0, 20.9), 'drop target: the second shot did not knock it down, or went past it')
    check(edges(35, 21.0, 21.2, 1) and edges(34, 21.2, 21.6, 1) and edges(33, 21.4, 22.0, 1) and not edges(33, 21.9, 24.8),
          'lock: the ball behind the dropped target did not come to rest on Rail Lower (35 %s, 34 %s, 33 %s)' %
          (edges(35, 21.0, 21.2), edges(34, 21.0, 22.0), edges(33, 21.0, 24.8)))
    check(edges(34, 24.0, 24.6, 1) and not edges(34, 24.6, 24.8) and state(35, 24.7) == 0,
          'lock: the second ball did not wait on Rail Upper behind the first (34 %s)' % edges(34, 23.8, 24.8))
    post = coil_on(5, 24.8, 25.0)
    out = edges(33, 24.8, 25.0, 0)
    check(post and out and edges(22, out[0], out[0] + 0.5, 1) and edges(34, out[0], out[0] + 0.6, 0) and state(33, 25.5) == 1,
          'stop post: coil 5 at %s released %s; the first ball must roll to the right inlane (22 %s), the second down to Rail Lower' %
          (post[:1], out, edges(22, 24.8, 25.4, 1)))
    post = coil_on(5, 25.6, 25.8)
    check(post and edges(33, 25.6, 25.8, 0) and [state(s, 26.2) for s in (33, 34, 35)] == [0, 0, 0],
          'stop post: the second pulse (%s) did not release the second ball' % post[:1])
    shut = [us for t, n, us in servo if n == 0 and 25.5 <= t < 26.4]
    check(shut and all(abs(us - 1227) < 10 for us in shut), 'gate servo before [S00160]: %s us, expected 1227 (closed)' % sorted(set(round(u) for u in shut)))
    check(edges(95, 26.3, 26.5, 1) and not edges(96, 26.3, 27.2), 'Spaulding: a shot at the closed gate must close the gate opto (95) only')
    wide = [us for t, n, us in servo if n == 0 and 26.9 <= t < 27.3]
    check(wide and all(abs(us - 2201) < 10 for us in wide), 'gate servo after [S00160]: %s us, expected 2201' % sorted(set(round(u) for u in wide)))
    check(edges(95, 27.3, 27.5, 1) and edges(96, 27.3, 27.8, 1), 'Spaulding: through the open gate the ball must pass the exit opto (96)')
    for c, s, t in ((3, 41, 28.0), (8, 41, 28.5), (19, 24, 29.0), (13, 25, 29.5)):
        on = coil_on(c, t, t + 0.4)
        check(on and edges(s, on[0], on[0] + 0.05, 1) and edges(s, on[0], on[0] + 0.1, 0),
              'flipper coil %d (%s) did not close and open its end-of-stroke switch %d' % (c, on[:1], s))
    check(edges(17, 30.0, 31.0, 1) and [state(s, 30.95) for s in [11] + TROUGH] == [0] + [1] * 7,
          'drain: the two balls did not return to the trough (11-18: %s)' % [state(s, 30.95) for s in [11] + TROUGH])
    check(len(edges(11, 30.9, 34.5, 1)) == 2 and len(edges(11, 30.9, 34.5, 0)) == 2,
          'two balls: an autoplunger and a plunger (Space) launch did not each clear the shooter lane')
    check(len(edges(18, 35.0, 37.5, 1)) >= 1 and [state(s, 37.5) for s in [11] + TROUGH] == [0] + [1] * 7,
          'two balls: Q twice did not drain both balls (11-18: %s)' % [state(s, 37.5) for s in [11] + TROUGH])
    for f in fails:
        print('SIM FAIL: ' + f)
    print('sim: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'verify'):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2]))
