#!/usr/bin/env python3
"""Machine check 5: the firmware's console commands and service tests reach exactly the
PinMAME lamps, solenoids and switches of spec 4.5.
  check5.py plan DIR      write DIR/send, send_at, send_gap, keys.txt, frames
  check5.py timing LOG    board edges in one CPU slice carry distinct PinMAME times
  check5.py verify DIR    check DIR/out2.log and DIR/frames2.bin against the plan"""
import struct
import sys

FPS = 60
MENU_AT = 10.5                                      # attract mode runs light shows: test from the service menu
SEND_AT, GAP = 11.5, 0.25
KEYS_AT = 39.0
FRAME = 4096
COLS, ROWS = 'QWERTYUI', 'ASDFGHJK'


def lamp(n):
    """firmware lamp or switch n (0-63) -> PinMAME number"""
    return (n // 8 + 1) * 10 + n % 8 + 1


LAMPS = ['L%d' % lamp(n) for n in range(64)]
COILS = ['S%d' % (c + 1) for c in range(24)]
GI = ['S%d' % s for s in list(range(25, 33)) + list(range(37, 45))]
RGB = ['S%d' % s for s in range(51, 57)]


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
    ev, t = [(MENU_AT, '0', 6, None)], KEYS_AT     # main menu: SWITCH EDGE

    def tap(keys, dt, exp=None, hold=6):
        nonlocal t
        ev.append((t, keys, hold, exp))
        t += dt
    tap('RSHIFT', 1.0)
    tap('RSHIFT', 1.0)                              # SOLENOID
    tap('0', 1.0)                                   # SOLENOID TEST: KNOCKER
    for c in range(24):
        tap('0', 1.5 if c == 1 else 0.5, ('fire', c))   # the shaker test ignores keys for a second
        tap('RSHIFT', 0.5)
    tap('7', 1.0)                                   # back to SOLENOID
    tap('RSHIFT', 1.0)                              # SERVO
    tap('0', 1.0)                                   # SERVO TEST: NOID RIGHT
    tap('0', 1.5, ('servo', 0, 0))                  # servo 0 runs at 0 deg
    tap('RSHIFT', 1.0)                              # NOID STOP
    tap('0', 1.0)                                   # Enter: pulses stop
    tap('RSHIFT', 1.0)                              # NOID LEFT
    tap('0', 1.5, ('servo', 0, 255))                # servo 0 runs at 180 deg
    tap('7', 1.0)                                   # back to SERVO
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
    tap('0', 1.0, ('rgb', rgb[0] + (0, 0, 0)))      # RGB1 RED
    for v in rgb[1:]:
        tap('RSHIFT', 1.0, ('rgb', v + (0, 0, 0)))
    for v in rgb:
        tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) + v))
    tap('7', 1.0)                                   # back to RGB LIGHTING
    for i in range(5):
        tap('LSHIFT', 1.0)                          # back to SWITCH EDGE
    tap('0', 1.5)                                   # SWITCH TEST
    for n in range(64):
        k = 'KEYCODE_%s KEYCODE_%s' % (COLS[n // 8], ROWS[n % 8])
        tap(k, 16 / FPS, ('switch', (n,), (1,)), hold=4)
        tap(k, 16 / FPS, ('switch', (), (1,)), hold=4)
    for key, cab in (('1', 12), ('5', 7), ('INSERT', 8), ('9', 2), ('0', 6)):
        tap(key, 16 / FPS, ('switch', (), (1, cab)), hold=12)
        tap(None, 16 / FPS, ('switch', (), (1,)))
    tap('END', 32 / FPS, ('switch', (), ()), hold=4)
    tap('END', 32 / FPS, ('switch', (), (1,)), hold=4)
    for key, cab in (('LSHIFT', 4), ('RSHIFT', 3)):
        tap(key, 16 / FPS, ('switch', (), (1, cab)), hold=12)
        tap(None, 16 / FPS, ('switch', (), (1,)))
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
    """switch test screen: closed matrix switches (column 0 drawn on the right) and cabinet inputs (0-7 right, 8-15 left)"""
    px = lambda x, y: f[y * 128 + x]
    g, c2 = px(0, 0), px(32, 0)
    if not g or not c2:
        return None
    for y in range(32):
        for x in range(40):
            if (x % 4 in (0, 3) or y % 4 in (0, 3)) and px(x, y) != (g if x < 32 else c2):
                return None
    if any(px(x, y) == g for x in range(1, 31, 4) for y in range(1, 31, 4)):
        return None
    sw = tuple(c * 8 + r for c in range(8) for r in range(8) if px((7 - c) * 4 + 1, r * 4 + 1))
    cab = tuple(k for k in range(16) if px(37 if k < 8 else 33, (k % 8) * 4 + 1))
    return sw, cab


def verify(d):
    o = Out(d + '/out2.log')
    raw = open(d + '/frames2.bin', 'rb').read()
    rec = 20 + FRAME
    frames = [(struct.unpack_from('<Q', raw, i + 8)[0] / 80e6, raw[i + 20:i + rec]) for i in range(0, len(raw) - rec + 1, rec)]
    fails = []

    def check(ok, what):
        if not ok:
            fails.append(what)
    # attract mode: blinking start lamp 91, external WS2801 LED white, GI_14 flashing
    check(o.rises('L91', 5, MENU_AT), 'start lamp 91 never lit in attract')
    check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [255] * 3, 'external LED 0 is not white on 62-64')
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
        elif e[0] in ('gi', 'gi+lamps'):
            want = tuple('S%d' % s for s in (e[1] if e[0] == 'gi' else (range(25, 33) if e[1] == 'all' else ()))) if e[0] == 'gi' else None
            if e[0] == 'gi+lamps':
                want = tuple(GI) if e[1] == 'all' else ()
                check(o.on(LAMPS, t1 - 0.05) == (tuple(LAMPS) if e[1] == 'all' else ()), 'GI AND LAMPS %s: lamps %s' % (e[1], o.on(LAMPS, t1 - 0.05)))
            got = o.on(GI, t1 - 0.05)
            check(sorted(got) == sorted(want), 'lamp test GI step at %.1f: %s, expected %s' % (t, got, want))
        elif e[0] == 'onelamp':
            got = o.on(LAMPS, t1 - 0.05)
            check(got == (LAMPS[e[1]],), 'LAMP = %d lit %s' % (e[1], got))
        elif e[0] == 'lamps':
            got = o.on(LAMPS, t1 - 0.05)
            check(got == (tuple(LAMPS) if e[1] == 'all' else ()), 'lamp test ALL %s: %d lamps' % (e[1], len(got)))
        elif e[0] == 'rgb':
            got = tuple(o.at(s, t1 - 0.05) for s in RGB)
            check(got == e[1], 'RGB test at %.1f: %s, expected %s' % (t, got, e[1]))
        elif e[0] == 'switch':
            got = [grid(f) for tt, f in frames if t + 0.12 <= tt < t1 + 0.2]
            got = [g for g in got if g]
            check((e[1], e[2]) in got, 'switch test after %s: screens %s, expected %s' % (k, sorted(set(got)), (e[1], e[2])))
            wanted.add((e[1], e[2]))
        elif e[0] == 'exit':
            after = [grid(f) for tt, f in frames if tt >= t + 0.1]
            check(after and after[-1] is None, 'Back did not leave the switch test')
    shown = set(g for g in (grid(f) for tt, f in frames if tt >= KEYS_AT) if g)
    check(shown <= wanted, 'switch test showed unexpected states %s' % sorted(shown - wanted))
    for f in fails:
        print('BOARD FAIL: ' + f)
    print('check5: %d uart commands, %d service-test steps, %d failures' % (len(uart_plan()), sum(1 for x in ev if x[3]), len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'timing', 'verify'):
        sys.exit(__doc__)
    if sys.argv[1] == 'plan':
        plan(sys.argv[2])
    else:
        sys.exit((timing if sys.argv[1] == 'timing' else verify)(sys.argv[2]))
