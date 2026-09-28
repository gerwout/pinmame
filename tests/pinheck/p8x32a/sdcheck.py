#!/usr/bin/env python3
import sys
import zipfile
import zlib

EXPECT = ['MBR', 'BOOT', 'DIR:/+0', 'DIR:SFX+0', 'DIR:SFX+512', 'DIR:/+0', 'DIR:DMD+0', 'DIR:DMD+512', 'DIR:DMD/_DZ+0'] + \
         ['FILE:DMD/_DZ/ZMB.VID+%d' % (512 * k) for k in range(1, 9)] + ['DIR:/+0']


def main():
    z = zipfile.ZipFile(sys.argv[2])
    names = {n.upper(): n for n in z.namelist()}
    reads = [l.split(None, 4) for l in open(sys.argv[1]) if l.startswith('D ')]
    labels = [r[4].strip() for r in reads]
    if labels != EXPECT:
        print('sd: FAIL, boot reads were:\n  ' + '\n  '.join(labels[:40]))
        return 1
    for r in reads:
        lab = r[4].strip()
        if not lab.startswith('FILE:'):
            continue
        path, off = lab[5:].rsplit('+', 1)
        data = z.read(names[path.upper()])[int(off):int(off) + 512]
        data += b'\0' * (512 - len(data))
        if '%08x' % zlib.crc32(data) != r[3]:
            print('sd: FAIL, sector %s (%s) differs from the zip entry' % (r[2], lab))
            return 1
    print('sd: boot mounted the card and read DMD/_DZ/ZMB.VID frame 0 (%d sectors, file sectors match the zip)' % len(reads))
    return 0


sys.exit(main())
