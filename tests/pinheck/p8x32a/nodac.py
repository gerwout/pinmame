#!/usr/bin/env python3
import sys

MASK = ~(3 << 14) & 0xFFFFFFFF
last = None
for line in open(sys.argv[1]):
    if line.startswith('P '):
        _, t, o, d = line.split()
        o, d = int(o, 16) & MASK, int(d, 16) & MASK
        if (o, d) == last:
            continue
        last = (o, d)
        print('P %s %08x %08x' % (t, o, d))
    elif not line.startswith('D '):
        print(line.rstrip('\n'))
