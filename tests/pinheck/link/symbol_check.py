#!/usr/bin/env python3
import glob
import re
import sys

FUNC = re.compile(r'^(?!static\b)[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{', re.M)
BARE = re.compile(r'^([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{', re.M)
KEYWORDS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'do', 'else'}


def defined(path):
    try:
        text = open(path, encoding='latin-1').read()
    except OSError:
        return set()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    names = set(FUNC.findall(text))
    for m in BARE.finditer(text):
        prev = text[:m.start()].rstrip('\n').rsplit('\n', 1)[-1]
        if 'static' not in prev.split() and m.group(1) not in KEYWORDS:
            names.add(m.group(1))
    return {n for n in names - KEYWORDS if not n.isupper()}


root = sys.argv[1] if len(sys.argv) > 1 else '.'
ours = [p for p in glob.glob(root + '/src/cpu/mips32/*.c') + glob.glob(root + '/src/cpu/pic32mx/*.c')
        + glob.glob(root + '/src/cpu/p8x32a/*.c') + glob.glob(root + '/src/wpc/pinheck/*.c')
        + glob.glob(root + '/src/wpc/pinheck*.c') + glob.glob(root + '/src/wpc/sims/pinheck/*.c')]
mine = {}
for p in ours:
    for name in defined(p):
        mine.setdefault(name, p)
clash = 0
for p in glob.glob(root + '/src/**/*.c', recursive=True):
    if p in ours:
        continue
    for name in defined(p) & set(mine):
        print('symbols: FAIL %s defined in %s and %s' % (name, mine[name], p))
        clash += 1
print('symbols: ok' if not clash else 'symbols: %d clashes' % clash)
sys.exit(1 if clash else 0)
