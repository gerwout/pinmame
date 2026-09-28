#!/usr/bin/env python3
import hashlib
import re
import sys
import zlib

CRC32 = 0xF99B3070
SHA1 = 'b7b4fdf4f096db7d18bda6355725cb42ae4a9378'


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: romimage.py spinsim/rom.h out.rom')
    src = open(sys.argv[1]).read()
    data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', src.split('romdata', 1)[1])[:32768])
    crc, sha = zlib.crc32(data) & 0xFFFFFFFF, hashlib.sha1(data).hexdigest()
    if len(data) != 32768 or crc != CRC32 or sha != SHA1:
        raise SystemExit('romimage: got %d bytes crc32 %08x sha1 %s, expected crc32 %08x sha1 %s' % (len(data), crc, sha, CRC32, SHA1))
    open(sys.argv[2], 'wb').write(data)
    print('p8x32a.rom: 32768 bytes, crc32 %08x, sha1 %s' % (crc, sha))


if __name__ == '__main__':
    main()
