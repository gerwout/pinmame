#!/usr/bin/env python3
import sys
import zipfile

d = bytes((i * 7 + 3) & 0xFF for i in range(70000))
args = sys.argv[1:]
pre = ''
if args[0] == '--prefix':
    pre = args[1]
    args = args[2:]
z = zipfile.ZipFile(args[0], 'w')
if pre:
    z.writestr(pre, b'')
z.writestr(zipfile.ZipInfo(pre + 'DOM_V006.PRG'), b'firmware')
z.writestr(pre + 'DMD/', b'')
z.writestr(pre + 'DMD/_DA/', b'')
z.writestr(pre + 'DMD/_DQ/', b'')
z.writestr(zipfile.ZipInfo(pre + 'DMD/_DA/AA0.VID'), d[:40000], compress_type=zipfile.ZIP_DEFLATED)
z.writestr(zipfile.ZipInfo(pre + 'DMD/_DA/AB0.VID'), d[:512], compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo(pre + 'DMD/_DZ/ZMS.spr'), d[:1000], compress_type=zipfile.ZIP_DEFLATED)
z.writestr(zipfile.ZipInfo(pre + 'DMD/_DZ/EMPTY.FNT'), b'', compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo(pre + 'SFX/_FA/A01.wav'), d[:70000], compress_type=zipfile.ZIP_DEFLATED)
z.writestr(zipfile.ZipInfo(pre + 'SFX/_FA/TOOLONGNAME.wav'), d[:10], compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo(pre + 'SFX/_FA/two.dots.wav'), d[:10], compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo(pre + 'OTHER/X.BIN'), d[:10], compress_type=zipfile.ZIP_STORED)
z.close()
