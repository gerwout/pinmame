#!/usr/bin/env python3
"""Assemble prop.c's spin-pause selection (CPU_RELAX) for each CPU PinMAME builds on, with clang:
an instruction the target lacks (ARMv6's missing `yield`) fails the LISY build."""
import os, re, subprocess, sys, tempfile

src = open(sys.argv[1]).read()
m = re.search(r'#if defined\(__x86_64__\) \|\| defined\(__i386__\)\n#define CPU_RELAX\(\).*?#endif\n', src, re.S)
if not m:
    sys.exit('RELAX FAIL: no CPU_RELAX selection in %s' % sys.argv[1])
fail = 0
os.makedirs(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build'), exist_ok=True)
with tempfile.TemporaryDirectory(dir=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build')) as d:
    c = os.path.join(d, 'relax.c')
    open(c, 'w').write(m.group(0) + 'void relax(void) { CPU_RELAX(); }\n')
    for target, march in (('x86_64-linux-gnu', None), ('i686-linux-gnu', None), ('aarch64-linux-gnu', None),
                          ('armv7-linux-gnueabihf', 'armv7-a'), ('armv6-linux-gnueabihf', 'armv6'),
                          ('armv6kz-linux-gnueabihf', 'armv6kz')):
        cmd = ['clang', '--target=' + target, '-c', c, '-o', os.path.join(d, 'relax.o')] + (['-march=' + march] if march else [])
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            print('RELAX FAIL %s: %s' % (target, r.stderr.strip().splitlines()[0]))
            fail += 1
print('relax: %d targets failed' % fail)
sys.exit(1 if fail else 0)
