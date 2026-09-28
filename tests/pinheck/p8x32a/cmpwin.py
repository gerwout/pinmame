#!/usr/bin/env python3
import struct
import sys

a, b = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
if len(a) != len(b):
    print('  length differs: %d vs %d' % (len(a), len(b)))
for k in range(0, min(len(a), len(b)) & ~3, 4):
    x, y = struct.unpack_from('<I', a, k)[0], struct.unpack_from('<I', b, k)[0]
    if x != y:
        print('  $%04X  spinsim %08x  ours %08x' % (0x6000 + k, x, y))
