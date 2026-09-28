#!/usr/bin/env python3
import re
import sys

TRACE = re.compile(r'READ EEPROM\r?\n(Checksum Number: \d+\r?\n)?(Timeout Counter: \d+\r?\n)*(Read OK!\r?\n)?')


def boots(text):
    parts = re.split(r'=== BOOT (\d+) ===\n', text)
    return {int(parts[i]): TRACE.sub('', parts[i + 1]).replace('\r', '') for i in range(1, len(parts), 2)}


def main():
    b = boots(open(sys.argv[1], encoding='latin-1').read())
    want = [
        (1, 'PROPELLER SYNC CHECKOK'),
        (1, 'NAMING GAME'),
        (2, 'NAME ALREADY EXISTS'),
        (2, 'pinHeck System 2011-2016'),
        (2, 'Game: DOM - DOMINOS'),
        (2, 'Version: 006'),
    ]
    bad = [(n, s) for n, s in want if s not in b.get(n, '')]
    for n, s in bad:
        print('BANNER FAIL boot %d lacks %r' % (n, s))
    if 'pinHeck System' in b.get(1, ''):
        print('BANNER FAIL boot 1 printed the banner on a blank EEPROM')
        bad.append((1, 'banner'))
    print('banner: %s' % ('FAIL' if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
