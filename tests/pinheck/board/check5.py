#!/usr/bin/env python3
"""Machine check 5: the firmware's console commands and service tests reach exactly the
PinMAME lamps, solenoids and switches of spec 4.5.
  check5.py plan DIR      write DIR/send, send_at, send_gap, keys.txt, frames
  check5.py timing LOG    board edges in one CPU slice carry distinct PinMAME times
  check5.py verify DIR    check DIR/out2.log and DIR/frames2.bin against the plan
  check5.py selftest      the verifier's helpers
PINHECK_GAME (dominos, rzspook, jetsons, amh) selects the game's servo and RGB tests, its menu, its switch-test
screen and its resting balls."""
import os
import struct
import sys

FPS = 60
GAME = os.environ.get('PINHECK_GAME', 'dominos')
MENU_AT = 10.5                                      # attract mode runs light shows: test from the service menu
SEND_AT, GAP = 11.5, 0.25
KEYS_AT = 39.0
if GAME == 'jetsons':                               # its Propeller reboots once before the sync (games.sh REBOOTS)
    MENU_AT, SEND_AT, KEYS_AT = 16.0, 17.0, 44.5
FRAME = 128 * (64 if os.environ.get('PINHECK_GAME') == 'jetsons' else 32)   # The Jetsons' module is 128x64
COLS, ROWS = 'QWERTYUI', 'ASDFGHJK'


def lamp(n):
    """firmware lamp or switch n (0-63) -> PinMAME number"""
    return (n // 8 + 1) * 10 + n % 8 + 1


LAMPS = ['L%d' % lamp(n) for n in range(64)]
COILS = ['S%d' % (c + 1) for c in range(24)]
GI = ['S%d' % s for s in list(range(25, 33)) + list(range(37, 45))]
RGB = ['S%d' % s for s in range(51, 57)]
if GAME in ('rzspook', 'amh'):
    RGB += ['S62', 'S63', 'S64']                    # the LDG light, the external WS2801 LED; America's Most Haunted's ghost
SERVO_US = (544, 2400) if GAME in ('rzspook', 'jetsons', 'amh') else (1000, 2000)   # the game's servo levels 0 and 255
# America's Most Haunted's ALL ON steps leave lamp 56 (Spook Again, 81) off
ALL_LAMPS = tuple(l for l in LAMPS if not (GAME == 'amh' and l == 'L81'))
# the switch test: the matrix's and the cabinet columns' x; cabinet inputs closed at rest (the coin door, and
# The Jetsons' trough opto under the ball the tests leave in the trough)
MX, CX = (8, 0) if GAME == 'jetsons' else (0, 32)
CAB_REST = (1, 10) if GAME == 'jetsons' else (1,)


def uart_plan():
    """(time, command, expectation) for every console command"""
    cmds = [('[E97000]', None), ('[L99000]', None)]
    cmds += [('[M%02d020]' % c, ('coil', c)) for c in range(24)]
    for v in range(1, 8):
        cmds += [('[L00%03d]' % v, ('level', v)), ('', None)]
    cmds += [('[L00000]', None)]
    cmds += [('[L%02d007]' % n, ('lamp', n)) for n in range(64)]
    cmds += [('[L98000]', ('alloff',))]
    return [(SEND_AT + k * GAP, c, e) for k, (c, e) in enumerate(cmds)]


def key_plan():
    """(time, keys, hold frames, expectation for the window after it)"""
    ev, t = [(MENU_AT, '0', 6, None)], KEYS_AT     # main menu: SWITCH EDGE (America's Most Haunted: MAIN SETTINGS)

    def tap(keys, dt, exp=None, hold=6):
        nonlocal t
        ev.append((t, keys, hold, exp))
        t += dt
    if GAME == 'amh':                               # MAIN SETTINGS, GAME SETTINGS, GAME AUDITS, then SWITCH EDGE
        for i in range(3):
            tap('RSHIFT', 1.0)
    tap('RSHIFT', 1.0)
    tap('RSHIFT', 1.0)                              # SOLENOID
    tap('0', 1.0)                                   # SOLENOID TEST: KNOCKER (Rob Zombie: AUTOPLUNGER, AMH: RFLIP HIGH)
    for c in (list(range(16, 24)) + list(range(16)) if GAME in ('rzspook', 'amh') else range(24)):
        tap('0', 1.5 if c == 1 else 0.5, ('fire', c))   # the shaker test ignores keys for a second
        tap('RSHIFT', 0.5)
    tap('7', 1.0)                                   # back to SOLENOID
    if GAME != 'jetsons':                           # The Jetsons has no servo test
        tap('RSHIFT', 1.0)                          # SERVO
    if GAME == 'jetsons':
        pass
    elif GAME == 'amh':
        # SERVO TEST: DOOR OPEN, DOOR CLOSE, TARGET UP, TARGET DOWN, GHOST LEFT, MIDDLE, RIGHT, HELL UP, HELL DOWN
        # (the factory angles 5, 90, 5, 160, 10, 90, 170, 160, 10 degrees on servos 1, 3, 2 and 0)
        tap('0', 1.0)
        for i, (s, us) in enumerate(((1, 593), (1, 1476), (3, 593), (3, 2200), (2, 647), (2, 1476), (2, 2305), (0, 2200), (0, 647))):
            if i:
                tap('RSHIFT', 1.0)
            tap('0', 1.5, ('position', s, us))
    elif GAME == 'rzspook':
        tap('0', 1.0)                               # SERVO TEST: GATE OPEN
        tap('0', 1.5, ('position', 0, 2222))        # the Spaulding gate opens
        tap('RSHIFT', 1.0)                          # GATE CLOSE
        tap('0', 1.5, ('position', 0, 1227))
        tap('RSHIFT', 1.0)                          # ROBOT START
        tap('0', 1.5, ('position', 1, 1476))
        tap('RSHIFT', 1.0)                          # ROBOT END
        tap('0', 1.5, ('position', 1, 2097))
    else:
        tap('0', 1.0)                               # SERVO TEST: NOID RIGHT
        tap('0', 1.5, ('servo', 0, 0))              # servo 0 runs at 0 deg
        tap('RSHIFT', 1.0)                          # NOID STOP
        tap('0', 1.0)                               # Enter: pulses stop
        tap('RSHIFT', 1.0)                          # NOID LEFT
        tap('0', 1.5, ('servo', 0, 255))            # servo 0 runs at 180 deg
    if GAME != 'jetsons':
        tap('7', 1.0)                               # back to SERVO
    if GAME == 'amh':
        tap('RSHIFT', 1.0)                          # SERVO DEFAULT
    tap('RSHIFT', 1.0)                              # LAMP
    tap('0', 0.5, ('gi', ()))                       # LAMP TEST: ALL OFF
    for n in range(8):
        tap('RSHIFT', 0.5, ('gi', (44 - n,)))       # PLAYFIELD GI = 1 << n
    for m in range(8):
        tap('RSHIFT', 0.5, ('gi', (32 - m,)))       # BACKBOX GI = m
    tap('RSHIFT', 0.5, ('gi', tuple(range(37, 45))))
    tap('RSHIFT', 0.5, ('gi', tuple(range(25, 33))))
    tap('RSHIFT', 0.5, ('gi+lamps', 'all'))
    tap('RSHIFT', 0.5, ('gi+lamps', 'none'))
    for n in range(64):
        tap('RSHIFT', 0.5, ('onelamp', n))          # LAMP = n
    tap('RSHIFT', 0.5, ('lamps', 'all'))
    tap('RSHIFT', 0.5, ('lamps', 'none'))
    tap('7', 1.0)                                   # back to LAMP
    tap('RSHIFT', 1.0)                              # RGB LIGHTING
    rgb = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    ext = (255, 255, 255) if GAME == 'rzspook' else ()          # the LDG light stays white from attract
    if GAME == 'amh':
        # GHOST=RED, GREEN, BLUE, then RGB1 and RGB2 RED, GREEN, BLUE: one LED lit at a time
        steps = [(0, 0, 0) * 2 + v for v in rgb[:3]] + [v + (0, 0, 0) * 2 for v in rgb[:3]] + [(0, 0, 0) + v + (0, 0, 0) for v in rgb[:3]]
        tap('0', 1.0, ('rgb', steps[0]))
        for v in steps[1:]:
            tap('RSHIFT', 1.0, ('rgb', v))
    else:
        tap('0', 1.0, ('rgb', rgb[0] + (0, 0, 0) + ext))    # RGB1 RED
        for v in rgb[1:]:
            tap('RSHIFT', 1.0, ('rgb', v + (0, 0, 0) + ext))
        for v in rgb:
            tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) + v + ext))
    if GAME == 'rzspook':
        # LDG RED, GREEN, BLUE, WHITE: the LDG light's lines are red, blue, green (SWAP G <-> B: NO)
        for v in [(255, 0, 0), (0, 0, 255), (0, 255, 0), (255, 255, 255)]:
            tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) * 2 + v))
    tap('7', 1.0)                                   # back to RGB LIGHTING
    for i in range(4 if GAME == 'jetsons' else 6 if GAME == 'amh' else 5):
        tap('LSHIFT', 1.0)                          # back to SWITCH EDGE
    tap('DEL', 0.5)                                 # simulator keys off: column/row keys reach the matrix
    tap('0', 1.5)                                   # SWITCH TEST
    for n in range(64):
        k = 'KEYCODE_%s KEYCODE_%s' % (COLS[n // 8], ROWS[n % 8])
        tap(k, 16 / FPS, ('switch', (n,), CAB_REST), hold=4)
        tap(k, 16 / FPS, ('switch', (), CAB_REST), hold=4)
    for key, cab in (('1', 12), ('5', 7), ('INSERT', 8), ('9', 2), ('0', 6)):
        tap(key, 16 / FPS, ('switch', (), tuple(sorted(CAB_REST + (cab,)))), hold=12)
        tap(None, 16 / FPS, ('switch', (), CAB_REST))
    tap('END', 32 / FPS, ('switch', (), CAB_REST[1:]), hold=4)
    tap('END', 32 / FPS, ('switch', (), CAB_REST), hold=4)
    for key, cab in (('LSHIFT', 4), ('RSHIFT', 3)):
        tap(key, 16 / FPS, ('switch', (), tuple(sorted(CAB_REST + (cab,)))), hold=12)
        tap(None, 16 / FPS, ('switch', (), CAB_REST))
    tap('7', 1.0, ('exit',))                        # Back (cabinet switch 5) leaves the switch test
    return ev, t + 0.5


def plan(d):
    send = ''.join(c + '~' for _, c, _ in uart_plan())
    ev, end = key_plan()
    keys = []
    for t, k, hold, _ in ev:
        if k:
            names = k if k.startswith('KEYCODE_') else 'KEYCODE_' + k
            keys.append('%d tap %d %s\n' % (round(t * FPS), hold, names))
    for name, text in (('send', send), ('send_at', '%g' % SEND_AT), ('send_gap', '%g' % GAP),
                       ('keys.txt', ''.join(keys)), ('frames', '%d' % round(end * FPS))):
        open('%s/%s' % (d, name), 'w').write(text + ('\n' if name != 'keys.txt' else ''))


def timing(path):
    """PinMAME time of each lamp edge must be the edge's own PIC32 cycle, also inside one CPU slice."""
    prev, n, bad = None, 0, 0
    for line in open(path):
        f = line.split()
        if len(f) < 3 or f[1] != 'L':
            continue
        t, cyc = float(f[0]), int(f[-1])
        if prev and 0 < cyc - prev[1] < 80000:
            n += 1
            if abs((t - prev[0]) - (cyc - prev[1]) / 80e6) > 2e-8:
                bad += 1
        prev = (t, cyc)
    print('timing: %d edge pairs less than 1 ms apart, %d with the wrong PinMAME time' % (n, bad))
    return 0 if n > 1000 and bad == 0 else 1


class Out:
    def __init__(self, path):
        self.ev, self.servo = {}, {}
        for line in open(path):
            f = line.split()
            if len(f) > 2 and f[1] == 'P':
                for x in f[2:]:
                    k, v = x.split('=')
                    self.ev.setdefault(k, []).append((float(f[0]), int(v)))
            elif len(f) > 3 and f[1] == 'V':
                self.servo.setdefault(int(f[2]), []).append((float(f[0]), float(f[3])))

    def at(self, name, t):
        v = 0
        for tt, vv in self.ev.get(name, []):
            if tt > t:
                break
            v = vv
        return v

    def rises(self, name, t0, t1):
        v, out = self.at(name, t0), []
        for tt, vv in self.ev.get(name, []):
            if t0 <= tt < t1:
                if v < 128 <= vv:
                    out.append(tt)
                v = vv
        return out

    def on(self, names, t):
        return tuple(n for n in names if self.at(n, t) >= 128)

    def mean(self, name, t0, t1):
        n = int((t1 - t0) * FPS)
        return sum(self.at(name, t0 + i / FPS) for i in range(n)) / n


def grid(f):
    """switch test screen: closed matrix switches (column 0 drawn on the right) and cabinet inputs (0-7 right, 8-15 left);
    the matrix at x = MX, the cabinet columns at x = CX"""
    px = lambda x, y: f[y * 128 + x]
    g, c2 = px(MX, 0), px(CX, 0)
    if not g or not c2:
        return None
    # America's Most Haunted draws the grid in two shades: any lit dot is a grid line
    line = (lambda x, y: px(x, y) != 0) if GAME == 'amh' else (lambda x, y: px(x, y) == (g if MX <= x < MX + 32 else c2))
    for y in range(32):
        for x in range(40):
            if (x % 4 in (0, 3) or y % 4 in (0, 3)) and not line(x, y):
                return None
    if any(px(x, y) == g for x in range(MX + 1, MX + 31, 4) for y in range(1, 31, 4)):
        return None
    sw = tuple(c * 8 + r for c in range(8) for r in range(8) if px(MX + (7 - c) * 4 + 1, r * 4 + 1))
    cab = tuple(k for k in range(16) if px(CX + (5 if k < 8 else 1), (k % 8) * 4 + 1))
    return sw, cab


def shown_states(grids):
    """the switch-test screens shown; a first screen with nothing closed is drawn before the inputs are read"""
    return set(grids[1:] if grids and grids[0] == ((), ()) else grids)


def selftest():
    fails = 0
    a, b = ((0, 1), (1, 10)), ((0, 1, 5), (1, 10))
    for got, want in ((shown_states([a, b]), {a, b}), (shown_states([((), ()), a]), {a}), (shown_states([]), set())):
        if got != want:
            print('CHECK5 FAIL: shown_states gave %s, expected %s' % (sorted(got), sorted(want)))
            fails += 1
    print('check5 selftest: %s' % ('FAIL' if fails else 'ok'))
    return 1 if fails else 0


def verify(d):
    o = Out(d + '/out2.log')
    raw = open(d + '/frames2.bin', 'rb').read()
    rec = 20 + FRAME
    frames = [(struct.unpack_from('<Q', raw, i + 8)[0] / 80e6, raw[i + 20:i + rec]) for i in range(0, len(raw) - rec + 1, rec)
              if GAME != 'amh' or struct.unpack_from('<I', raw, i + 16)[0] != 0xFFFFFFFF]   # a raw DMD cycle the frame copy crossed
    fails = []

    def check(ok, what):
        if not ok:
            fails.append(what)
    # attract mode: blinking start lamp 91, external WS2801 LED white; Domino's flashes GI_14, Rob Zombie
    # lights the playfield GI but its flashers GI_12 and GI_13
    if GAME == 'jetsons':
        # the start lamp stays lit; the external LED is never written; servos 0 (the Orbitty) and 1 at 90 degrees,
        # 1,476 us, in the game's range
        check(o.at('L91', MENU_AT) >= 128, 'start lamp 91 is not lit in attract')
        check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [0] * 3, 'external LED 0 on 62-64 is lit')
        for n, us in ((0, 1476), (1, 1476)):
            w = sorted(set(round(u) for tt, u in o.servo.get(n, []) if MENU_AT - 1.0 <= tt < MENU_AT))
            level = round((us - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
            check(w and all(abs(u - us) < 10 for u in w), 'attract servo %d pulses %s us, expected %d' % (n, w, us))
            check(abs(o.at('S%d' % (57 + n), MENU_AT) - level) <= 1, 'attract servo output %d is %d, expected %d' % (57 + n, o.at('S%d' % (57 + n), MENU_AT), level))
    elif GAME == 'amh':
        # the start lamp stays off; the ghost (on-board LED 2, 62-64) stays dark while RGB1 and RGB2 fade; the servos
        # rest at HELL DOWN, DOOR OPEN, GHOST MIDDLE and TARGET DOWN
        check(not o.rises('L91', 5, MENU_AT), 'start lamp 91 lit in attract')
        check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [0] * 3, 'the ghost on 62-64 is lit in attract')
        check(len(set(v for t, v in o.ev.get('S51', []) if 5 <= t < MENU_AT)) > 3, 'RGB1 (51) does not fade in attract')
        for n, us in ((0, 647), (1, 593), (2, 1476), (3, 2200)):
            w = sorted(set(round(u) for tt, u in o.servo.get(n, []) if MENU_AT - 1.0 <= tt < MENU_AT))
            level = round((us - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
            check(w and all(abs(u - us) < 10 for u in w), 'attract servo %d pulses %s us, expected %d' % (n, w, us))
            check(abs(o.at('S%d' % (57 + n), MENU_AT) - level) <= 1, 'attract servo output %d is %d, expected %d' % (57 + n, o.at('S%d' % (57 + n), MENU_AT), level))
    else:
        check(o.rises('L91', 5, MENU_AT), 'start lamp 91 never lit in attract')
        check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [255] * 3, 'external LED 0 is not white on 62-64')
    if GAME == 'jetsons':
        pass
    elif GAME == 'amh':
        # the attract show uses the four playfield GI circuits (GI_8-11: 37-40) and no other GI
        check(all(o.rises('S%d' % s, 5, MENU_AT) for s in range(37, 41)), 'attract playfield GI 37-40 never all lit')
        check(not any(o.rises('S%d' % s, 5, MENU_AT) for s in list(range(25, 33)) + list(range(41, 45))), 'attract lit GI other than 37-40')
    elif GAME == 'rzspook':
        gi = [o.at('S%d' % s, MENU_AT) for s in range(37, 45)]
        check(gi == [255] * 4 + [0, 0] + [255] * 2, 'attract playfield GI 37-44 is %s, expected all on but the flashers 41 and 42' % gi)
    else:
        check(o.rises('S43', 5, MENU_AT), 'attract flasher GI_14 never reached solenoid 43')
    levels = {}
    for t, cmd, e in uart_plan():
        if not e:
            continue
        if e[0] == 'coil':
            got = [c for c in COILS if o.rises(c, t, t + GAP)]
            check(got == [COILS[e[1]]], '%s fired %s, expected %s' % (cmd, got, COILS[e[1]]))
        elif e[0] == 'lamp':
            got = sorted(set(o.on(LAMPS, t + GAP - 0.03)) - set(o.on(LAMPS, t - 0.03)))
            check(got == [LAMPS[e[1]]] and len(o.on(LAMPS, t + GAP - 0.03)) == e[1] + 1, '%s lit %s, expected %s' % (cmd, got, LAMPS[e[1]]))
        elif e[0] == 'level':
            xs = sorted(o.at('L11', t + i / FPS) for i in range(int(2 * GAP * FPS)))
            levels[e[1]] = xs[len(xs) // 2] / 255
        elif e[0] == 'alloff':
            check(o.on(LAMPS, t + GAP) == (), '[L98000] left lamps on: %s' % (o.on(LAMPS, t + GAP),))
    lv = [levels[v] for v in range(1, 8)]
    print('levels: lamp 11 at brightness 1-7 reads %s (median, 0-1)' % ' '.join('%.3f' % x for x in lv))
    check(lv[0] < 0.3 and lv[6] > 0.8 and all(b - a >= 0.04 for a, b in zip(lv, lv[1:])), 'brightness levels not 7 distinct rising steps')
    ev, _ = key_plan()
    wanted = set()
    # the simulator's balls: [M17020] loaded one into the shooter lane, the solenoid test's PLUNGER launched it
    # and its LOAD BALL loaded the next, so the shooter lane (0) and trough 1 (1) rest closed; Noid Home (39) may.
    # Rob Zombie's trough keeps the other five of its seven balls (trough 1-5)
    t0 = min(t for t, k, hold, e in ev if e and e[0] == 'switch')
    rest = [g for g in (grid(f) for tt, f in frames if t0 - 1.0 <= tt < t0 - 0.1) if g]
    base = set(rest[-1][0]) if rest else set()
    # The Jetsons: the console's load and launch and the solenoid test's LOAD COIL and PLUNGER put two balls on the
    # playfield; the third rests on trough opto 1 (cabinet 10)
    # America's Most Haunted: the console's ball load and the solenoid test's BALL LOAD each put a ball in the shooter
    # lane, which the firmware's autoplunger launched; the other two rest in trough 1 and 2 (59, 60)
    want = {0, 1, 2, 3, 4, 5} if GAME == 'rzspook' else set() if GAME == 'jetsons' else {59, 60} if GAME == 'amh' else {0, 1}
    check(rest and base - {39} == want and rest[-1][1] == CAB_REST, 'switch test at rest shows %s, expected switches %s, maybe Noid Home (39), and cabinet inputs %s' % (rest[-1:], sorted(want), CAB_REST))
    for i, (t, k, hold, e) in enumerate(ev):
        if not e:
            continue
        t1 = ev[i + 1][0] if i + 1 < len(ev) else t + 1
        t, t1 = t - 0.1, t1 - 0.1                   # a scripted frame falls up to 1/60 s before its nominal time
        if e[0] == 'fire':
            got = [c for c in COILS if o.rises(c, t, t1)]
            check(got == [COILS[e[1]]], 'solenoid test item %d fired %s' % (e[1], got))
        elif e[0] == 'servo':
            w = [us for tt, us in o.servo.get(e[1], []) if t + 0.6 <= tt < t + 1.1]
            check(w and all(abs(us - (544 if e[2] == 0 else 2400)) < 10 for us in w), 'servo %d pulses %s' % (e[1], sorted(w)[::max(1, len(w) // 3)]))
            check(o.at('S%d' % (57 + e[1]), t + 1.1) == e[2], 'servo output %d is %d' % (57 + e[1], o.at('S%d' % (57 + e[1]), t + 1.1)))
            check(o.at('S58', t + 1.1) == 161, 'target bank servo 58 is %d, expected 161 (1631 us)' % o.at('S58', t + 1.1))
        elif e[0] == 'position':
            w = [us for tt, us in o.servo.get(e[1], []) if t + 0.2 <= tt < t + 0.5]
            level = round((e[2] - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
            check(w and all(abs(us - e[2]) < 10 for us in w), 'servo %d pulses %s, expected %d us' % (e[1], sorted(set(round(us) for us in w)), e[2]))
            check(abs(o.at('S%d' % (57 + e[1]), t + 0.5) - level) <= 1, 'servo output %d is %d, expected %d' % (57 + e[1], o.at('S%d' % (57 + e[1]), t + 0.5), level))
        elif e[0] in ('gi', 'gi+lamps'):
            want = tuple('S%d' % s for s in (e[1] if e[0] == 'gi' else (range(25, 33) if e[1] == 'all' else ()))) if e[0] == 'gi' else None
            if e[0] == 'gi+lamps':
                want = tuple(GI) if e[1] == 'all' else ()
                check(o.on(LAMPS, t1 - 0.05) == (ALL_LAMPS if e[1] == 'all' else ()), 'GI AND LAMPS %s: lamps %s' % (e[1], o.on(LAMPS, t1 - 0.05)))
            got = o.on(GI, t1 - 0.05)
            check(sorted(got) == sorted(want), 'lamp test GI step at %.1f: %s, expected %s' % (t, got, want))
        elif e[0] == 'onelamp':
            got = o.on(LAMPS, t1 - 0.05)
            check(got == (LAMPS[e[1]],), 'LAMP = %d lit %s' % (e[1], got))
        elif e[0] == 'lamps':
            got = o.on(LAMPS, t1 - 0.05)
            check(got == (ALL_LAMPS if e[1] == 'all' else ()), 'lamp test ALL %s: %d lamps' % (e[1], len(got)))
        elif e[0] == 'rgb':
            got = tuple(o.at(s, t1 - 0.05) for s in RGB)
            check(got == e[1], 'RGB test at %.1f: %s, expected %s' % (t, got, e[1]))
        elif e[0] == 'switch':
            got = [grid(f) for tt, f in frames if t + 0.12 <= tt < t1 + 0.2]
            got = [g for g in got if g]
            want = (tuple(sorted(base ^ set(e[1]))), e[2])
            check(want in got, 'switch test after %s: screens %s, expected %s' % (k, sorted(set(got)), want))
            wanted.add(want)
        elif e[0] == 'exit':
            after = [grid(f) for tt, f in frames if tt >= t + 0.1]
            check(after and after[-1] is None, 'Back did not leave the switch test')
    shown = shown_states([g for g in (grid(f) for tt, f in frames if tt >= KEYS_AT) if g])
    check(shown <= wanted, 'switch test showed unexpected states %s' % sorted(shown - wanted))
    for f in fails:
        print('BOARD FAIL: ' + f)
    print('check5: %d uart commands, %d service-test steps, %d failures' % (len(uart_plan()), sum(1 for x in ev if x[3]), len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if sys.argv[1:] == ['selftest']:
        sys.exit(selftest())
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'timing', 'verify'):
        sys.exit(__doc__)
    if sys.argv[1] == 'plan':
        plan(sys.argv[2])
    else:
        sys.exit((timing if sys.argv[1] == 'timing' else verify)(sys.argv[2]))
