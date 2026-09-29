#!/usr/bin/env python3
"""Unit test of link.py's packet decoding on packets written the way PINHECK_LINK_LOG writes them."""
import os
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ next to the tests
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import link  # noqa: E402

fails = []


def check(ok, what):
    if not ok:
        fails.append(what)
        print('LINK FAIL: %s' % what)


def pkt(t, cmd, payload):
    b = bytes(payload) + bytes(15 - len(payload)) + bytes([cmd])
    return '%.6f %s gap 300\n' % (t, ' '.join('%02x' % x for x in b))


lines = [pkt(1.0, 0x0C, [1, 2, 1, 0x81]), pkt(1.5, 0x03, [1, 0x40, 0x42, 0x0F, 0x00]), pkt(2.0, 0x02, b'NBH'),
         pkt(2.1, 0x02, [0, 0, 0]), pkt(3.0, 0x0E, [3] + [0x20, 0xBC, 0xBE, 0x00] + list(b'BJH'))]
msg = 'PLAYER:1 BALL:2 '
for k in range(24):
    w = (msg * 3)[k:k + 8]
    lines.append(pkt(4.0 + 0.25 * k, 0x0F, [6] + list(w.encode())))
lines.append(pkt(11.0, 0x0F, [6] + list(b'FREE PLA')))
lines.append('garbage line\n')
fd, path = tempfile.mkstemp()
with os.fdopen(fd, 'w') as f:
    f.write(''.join(lines))
pk = link.packets(path)
os.remove(path)
check(len(pk) == 30, 'packets: %d, expected 30 (a short line is skipped)' % len(pk))
check(link.status(pk) == [(1.0, 1, 2, 1)], 'status %s' % link.status(pk))
check(link.scores(pk) == [(1.5, 1, 1000000)], 'scores %s' % link.scores(pk))
check(link.videos(pk) == [(2.0, 'NBH')], 'videos %s (a clip stop has no name)' % link.videos(pk))
check(link.hiscores(pk) == [(3.0, 3, 12500000, 'BJH')], 'high scores %s' % link.hiscores(pk))
check(link.ticker(pk) == [(4.0, (msg * 4)[:31]), (11.0, 'FREE PLA')], 'ticker %s' % link.ticker(pk))
check(link.said(pk, r'PLAYER:\d BALL:\d') == [(5.75, 'PLAYER:1 BALL:2'), (9.75, 'PLAYER:1 BALL:2')],
      'said %s' % link.said(pk, r'PLAYER:\d BALL:\d'))
print('link: ok' if not fails else 'link: %d failures' % len(fails))
sys.exit(1 if fails else 0)
