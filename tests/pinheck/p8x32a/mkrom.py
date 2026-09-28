#!/usr/bin/env python3
import struct
import sys

START, END, WORKER, PATCH = 0xC0DE5EED, 0xC0DEE0D0, 0xC0DE0B0B, 0xC0DEADD1
UNSCR = [10, 26, 24, 29, 27, 13, 22, 28, 2, 25, 18, 9, 5, 16, 31, 23,
         1, 30, 14, 0, 11, 8, 15, 20, 17, 4, 19, 6, 12, 21, 7, 3]


def scramble(w):
    r = 0
    for k in range(32):
        if w >> k & 1:
            r |= 1 << UNSCR[k]
    return r


def pasm(binary):
    words = [struct.unpack_from('<I', binary, i)[0] for i in range(0, len(binary) - 3, 4)]
    a = words.index(START) + 1
    b = words.index(END, a)
    if b - a > 496:
        raise SystemExit('mkrom: PASM block longer than 496 longs')
    code = words[a:b]
    if WORKER in words:
        addr = (words.index(WORKER) + 1) * 4
        code = [addr if w == PATCH else w for w in code]
    return code


def launcher(binary):
    words = [struct.unpack_from('<I', binary, i)[0] for i in range(0, len(binary) - 3, 4)]
    entry = (words.index(START) + 1) * 4
    return [(3 << 26) | (1 << 22) | (15 << 18) | (3 << 9) | 2,
            (3 << 26) | (1 << 23) | (1 << 22) | (15 << 18) | (4 << 9) | 1,
            (3 << 26) | (1 << 22) | (15 << 18) | (4 << 9) | 3,
            ((entry >> 2) << 4) | 1,
            0]


def main():
    args = [a for a in sys.argv[1:] if a != '--launch']
    if len(args) != 3:
        raise SystemExit('usage: mkrom.py [--launch] prog.binary out.rom out.ram')
    binary = open(args[0], 'rb').read()
    code = launcher(binary) if '--launch' in sys.argv else pasm(binary)
    rom = bytearray(32768)
    for k, w in enumerate(code):
        struct.pack_into('<I', rom, 0x7800 + 4 * k, scramble(w))
    open(args[1], 'wb').write(rom)
    open(args[2], 'wb').write(binary[:32768] + bytes(32768 - min(len(binary), 32768)))


if __name__ == '__main__':
    main()
