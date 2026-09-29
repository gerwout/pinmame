#!/usr/bin/env python3
"""Group 'perf report --sort srcfile' output (on stdin) into the machine's components."""
import re
import sys

GROUPS = [
    ('PIC32 (mips32, pic32mx)', ('mips32.c', 'pic32mx.c', 'pic32mxcpu.c')),
    ('Propeller core (p8x32a)', ('p8x32a.c',)),
    ('Propeller stepping and link (prop.c)', ('prop.c', 'bootldr.c')),
    ('SD card and EEPROM (sd, vfat, zipsrc, eeprom, zlib)', ('sd.c', 'vfat.c', 'zipsrc.c', 'eeprom.c', 'inflate.c', 'inffast.c', 'crc32.c', 'adler32.c')),
    ('display and audio (display.c, audio.c)', ('display.c', 'audio.c')),
    ('board and driver (board.c, pinheck.c, rtc.c)', ('board.c', 'pinheck.c', 'rtc.c')),
]
total = {}
for line in sys.stdin:
    m = re.match(r'\s*([\d.]+)%\s+(\S*)\s*$', line)
    if not m:
        continue
    pct, src = float(m.group(1)), m.group(2)
    name = 'other (PinMAME, kernel, libraries)'
    for g, files in GROUPS:
        if src in files:
            name = g
            break
    total[name] = total.get(name, 0.0) + pct
for name, pct in sorted(total.items(), key=lambda x: -x[1]):
    print('%6.1f%%  %s' % (pct, name))
