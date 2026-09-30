#!/usr/bin/env python3
"""libpinmame.cpp renames strcasecmp to _stricmp for MSVC. Under MinGW that rename turns <string.h>'s inline
strcasecmp wrapper into a _stricmp calling itself, and PinmameRun() hangs; MinGW must keep its own strcasecmp.
Preprocesses the rename block with the MinGW compiler and checks the call still names strcasecmp."""
import os, re, subprocess, sys, tempfile

src = open(sys.argv[1]).read()
m = re.search(r'#if[^\n]*\n#define strcasecmp _stricmp\n#endif\n', src)
if not m:
    sys.exit('MINGW FAIL: no strcasecmp rename block in %s' % sys.argv[1])
build = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build')
os.makedirs(build, exist_ok=True)
with tempfile.TemporaryDirectory(dir=build) as d:
    c = os.path.join(d, 'sc.c')
    open(c, 'w').write(m.group(0) + '#include <string.h>\nint pinheck_cmp(const char *a, const char *b) { return strcasecmp(a, b); }\n')
    r = subprocess.run([sys.argv[2], '-E', c], capture_output=True, text=True)
if r.returncode:
    sys.exit('MINGW FAIL: %s' % r.stderr.strip().splitlines()[0])
call = re.search(r'pinheck_cmp\([^)]*\) \{ return (\w+)\(', r.stdout).group(1)
if call != 'strcasecmp':
    sys.exit('MINGW FAIL: under MinGW strcasecmp becomes %s' % call)
print('mingw: strcasecmp kept')
