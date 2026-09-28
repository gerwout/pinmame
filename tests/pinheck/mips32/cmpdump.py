#!/usr/bin/env python3
import struct
import sys

NAMES = ['scratch+%d' % (4 * i) for i in range(64)] + ['$%d' % i for i in range(32)] + ['hi', 'lo']


def words(path):
    d = open(path, 'rb').read()
    return struct.unpack('<%dI' % (len(d) // 4), d[:len(d) // 4 * 4])


a, b = words(sys.argv[1]), words(sys.argv[2])
if len(a) != len(b):
    print('  length differs: qemu %d words, ours %d words' % (len(a), len(b)))
for i, (x, y) in enumerate(zip(a, b)):
    if x != y:
        print('  %-12s qemu %08x  ours %08x' % (NAMES[i] if i < len(NAMES) else 'word %d' % i, x, y))
