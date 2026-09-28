#!/usr/bin/env python3
import struct
import sys

BASE = 0x9D000000
PORT_OF_PIN = 0x9D02FBA0
MASK_OF_PIN = 0x9D02CAC0
TRIS_OF_PORT = 0x9D0314B4
PINS = 87
TRIS = {0xBF886000 + 0x40 * i: 'ABCDEFG'[i] for i in range(7)}
LINK = {14: ('F', 13, 'COMM_IN_TX, Propeller -> PIC32 data'),
        15: ('F', 12, 'COMM_CLK_RX, PIC32 -> Propeller clock'),
        16: ('F', 5, 'COMM_OUT, PIC32 -> Propeller data')}


def main():
    d = open(sys.argv[1], 'rb').read()
    b = lambda va: d[va - BASE]
    h = lambda va: struct.unpack_from('<H', d, va - BASE)[0]
    w = lambda va: struct.unpack_from('<I', d, va - BASE)[0]
    bad = 0
    pins = {}
    for n in range(PINS):
        port = b(PORT_OF_PIN + n)
        if not port:
            continue
        tris = w(TRIS_OF_PORT + 4 * port)
        mask = h(MASK_OF_PIN + 2 * n)
        if tris not in TRIS or mask == 0 or mask & (mask - 1):
            print('PINS FAIL pin %d: port pointer %08x mask %04x is not a single port bit' % (n, tris, mask))
            bad += 1
            continue
        pins[n] = (TRIS[tris], mask.bit_length() - 1)
    for n, (port, bit, what) in sorted(LINK.items()):
        got = pins.get(n)
        print('logical %d -> R%s%d  %s' % (n, got[0], got[1], what) if got else 'logical %d -> unmapped' % n)
        if got != (port, bit):
            print('PINS FAIL logical %d is %r, expected R%s%d' % (n, got, port, bit))
            bad += 1
    print('pins: %s (%d logical pins resolved)' % ('FAIL' if bad else 'ok', len(pins)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
