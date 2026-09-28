#!/usr/bin/env python3
import os
import re
import sys
import zipfile

z = zipfile.ZipFile(sys.argv[1])
ext = sys.argv[2]
ok83 = re.compile(r'^[A-Z0-9$%\'\-_@~`!(){}^#&]{1,8}(\.[A-Z0-9$%\'\-_@~`!(){}^#&]{1,3})?$')
want = {}
for n in z.namelist():
    parts = n.upper().rstrip('/').split('/')
    if parts[0] not in ('DMD', 'SFX') or n.endswith('/') or not all(ok83.match(p) for p in parts):
        continue
    want['/'.join(parts)] = n
got = {}
for dp, dn, fn in os.walk(ext):
    for f in fn:
        p = os.path.join(dp, f)
        got[os.path.relpath(p, ext).upper()] = p
bad = [k for k in want if k not in got or open(got[k], 'rb').read() != z.read(want[k])]
extra = [k for k in got if k not in want]
if bad or extra:
    print('mount: FAIL, %d missing or different, %d extra: %s' % (len(bad), len(extra), (bad + extra)[:5]))
    sys.exit(1)
print('mount: %d files byte-exact against %s' % (len(want), os.path.basename(sys.argv[1])))
