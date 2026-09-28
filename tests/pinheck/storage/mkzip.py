#!/usr/bin/env python3
import sys
import zipfile

d = bytes((i * 7 + 3) & 0xFF for i in range(70000))
z = zipfile.ZipFile(sys.argv[1], 'w')
z.writestr(zipfile.ZipInfo('DOM_V006.PRG'), b'firmware')
z.writestr('DMD/', b'')
z.writestr('DMD/_DA/', b'')
z.writestr('DMD/_DQ/', b'')
z.writestr(zipfile.ZipInfo('DMD/_DA/AA0.VID'), d[:40000], compress_type=zipfile.ZIP_DEFLATED)
z.writestr(zipfile.ZipInfo('DMD/_DA/AB0.VID'), d[:512], compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo('DMD/_DZ/ZMS.spr'), d[:1000], compress_type=zipfile.ZIP_DEFLATED)
z.writestr(zipfile.ZipInfo('DMD/_DZ/EMPTY.FNT'), b'', compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo('SFX/_FA/A01.wav'), d[:70000], compress_type=zipfile.ZIP_DEFLATED)
z.writestr(zipfile.ZipInfo('SFX/_FA/TOOLONGNAME.wav'), d[:10], compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo('SFX/_FA/two.dots.wav'), d[:10], compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo('OTHER/X.BIN'), d[:10], compress_type=zipfile.ZIP_STORED)
z.close()
