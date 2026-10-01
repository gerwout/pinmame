#!/usr/bin/env python3
"""America's Most Haunted simulator check: in attract mode the firmware's console coil and servo commands
([MXXzzz], [SXXzzz]) and the simulator keys move the balls; the switches the simulator closes (PINHECK_OUT_LOG
'W' lines) must follow the coils and servos, and the servo outputs follow the game's servo range.
  amhsim.py plan DIR     write DIR/send, send_at, send_gap, keys.txt, frames
  amhsim.py verify DIR   check DIR/out2.log against the plan"""
import sys

FPS = 60
SEND_AT, GAP = 12.0, 0.05
# (time, console command) and (time, keys, hold frames): a ball's trip round the playfield, then the drain.
# The firmware itself launches a ball that reaches the shooter lane and ejects the scoop and the VUK in attract
# mode; the door is open (servo 1 at 5 degrees) and the Hellevator car down (servo 0 at 10 degrees)
SEND = [(12.0, '[E97000]'), (13.0, '[M20020]'), (22.0, '[S01090]'), (24.0, '[S01005]'), (26.0, '[S00160]'),
        (28.0, '[S00010]'), (29.0, '[M00250]'), (32.0, '[M16020]'), (32.5, '[M17020]'), (33.0, '[M18020]'),
        (33.5, '[M19020]')]
KEYS = [(17.0, 'S', 1), (20.0, 'D', 1), (23.0, 'D', 1), (25.0, 'E', 1), (29.05, 'G', 1), (31.0, 'Q', 1)]
END = 35.0
TROUGH = [84, 85, 86, 87]               # trough 1 (the eject position) to 4
SERVO_US = (544, 2400)                  # the game's servo levels 0 and 255
SHOOTER, SCOOP, VUK, CAR, DRAIN, LOOP, DOOR = 82, 37, 38, 64, 88, 95, 96


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

    def us(n, t0, t1):
        return sorted(set(round(u) for t, s, u in servo if s == n and t0 <= t < t1))

    rest = [s for s in [SHOOTER] + TROUGH if state(s, 11.0)]
    check(rest == TROUGH, 'at rest closed %s; expected the trough %s' % (rest, TROUGH))
    # attract mode: Hellevator down (10 degrees), door open (5), ghost middle (90), target down (160)
    for n, want in ((0, 647), (1, 593), (2, 1476), (3, 2200)):
        w = us(n, 11.0, 12.0)
        lv = round((want - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
        check(w and all(abs(u - want) < 10 for u in w) and abs((level('S%d' % (57 + n), 12.0) or -9) - lv) <= 1,
              'servo %d in attract: %s us and output %d at %s; expected %d us and %d' % (n, w, 57 + n, level('S%d' % (57 + n), 12.0), want, lv))
    load_ = coil_on(21, 13.0, 13.5)
    check(load_ and edges(84, load_[0], load_[0] + 0.1, 0) and edges(SHOOTER, load_[0], load_[0] + 0.5, 1),
          'trough: [M20020] at 13.0 (coil 21 %s) did not move trough 1 (84) to the shooter lane (82)' % load_[:1])
    check([state(s, 14.0) for s in TROUGH] == [1, 1, 1, 0], 'trough: the three balls did not roll down to 84-86: %s' % [state(s, 14.0) for s in TROUGH])
    lane = edges(SHOOTER, 13.0, 14.0, 1)
    launch = coil_on(23, lane[0], lane[0] + 1.0) if lane else []
    check(launch and edges(SHOOTER, launch[0], launch[0] + 0.1, 0), 'autoplunger: the firmware did not launch the ball from the shooter lane (82 %s, coil 23 %s)' % (lane[:1], launch[:1]))
    for name, sw_, coil, t in (('scoop', SCOOP, 11, 17.0), ('VUK behind the open door', VUK, 12, 20.0)):
        into = edges(sw_, t, t + 0.4, 1)
        kick = coil_on(coil, into[0], into[0] + 2.0) if into else []
        check(kick and edges(sw_, kick[0], kick[0] + 0.1, 0) and not edges(sw_, into[0], kick[0], 0),
              '%s: switch %d closed %s, the firmware fired coil %d %s, released %s' % (name, sw_, into[:1], coil, kick[:1], edges(sw_, t, t + 3.0, 0)))
    check(us(1, 22.6, 23.0) and all(abs(u - 1476) < 10 for u in us(1, 22.6, 23.0)), 'door: [S01090] did not close it (servo 1 %s)' % us(1, 22.6, 23.0))
    check(len(edges(DOOR, 23.0, 23.6, 1)) == 1 and len(edges(DOOR, 23.0, 23.6, 0)) == 1 and not edges(VUK, 23.0, 24.0),
          'door: a shot at the closed door did not close and open the door opto (96) alone')
    into = edges(CAR, 25.0, 25.2, 1)
    up = [t for t, s, u in servo if s == 0 and t >= 26.0 and u > 1400]
    out_ = edges(CAR, 25.2, 28.0, 0)
    check(into and up and out_ and out_[0] > up[0], 'Hellevator: the car switch (64) closed %s, the car rose %s, the ball left %s: it must ride up first' % (into[:1], up[:1], out_[:1]))
    mag = coil_on(1, 29.0, 29.1)
    opto = edges(LOOP, 29.0, 29.2, 1)
    check(mag and opto, 'ghost loop: magnet coil 1 %s and loop opto 95 %s' % (mag[:1], opto[:1]))
    check([state(s, 30.5) for s in [SHOOTER] + TROUGH] == [0, 1, 1, 1, 0], 'magnet: the trough changed while the ball was held')
    drain = edges(DRAIN, 31.0, 31.3, 1)
    kick = coil_on(22, drain[0], drain[0] + 2.0) if drain else []
    check(kick and edges(DRAIN, kick[0], kick[0] + 0.1, 0), 'drain: switch 88 closed %s, the firmware fired the drain kicker (22) %s' % (drain[:1], kick[:1]))
    check([state(s, 31.9) for s in [SHOOTER] + TROUGH] == [0, 1, 1, 1, 1], 'drain: the ball did not return to the trough (82, %s: %s)' % (TROUGH, [state(s, 31.9) for s in [SHOOTER] + TROUGH]))
    for c, s, t in ((17, 75, 32.0), (18, 75, 32.5), (19, 74, 33.0), (20, 74, 33.5)):
        on = coil_on(c, t, t + 0.4)
        check(on and edges(s, on[0], on[0] + 0.05, 1) and edges(s, on[0], on[0] + 0.2, 0),
              'flipper coil %d (%s) did not close and open its end-of-stroke switch %d' % (c, on[:1], s))
    for f in fails:
        print('SIM FAIL: ' + f)
    print('sim: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'verify'):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2]))
