#!/usr/bin/env python3
import glob
import os
import re
import sys

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))

GROUPS = [
    (['src/cpu/p8x32a/p8x32a.c', 'src/cpu/p8x32a/p8x32a.h', 'src/wpc/pinheck/eeprom.c', 'src/wpc/pinheck/eeprom.h',
      'src/wpc/pinheck/prop.c', 'src/wpc/pinheck/prop.h', 'src/wpc/pinheck/rtc.c', 'src/wpc/pinheck/rtc.h',
      'src/wpc/pinheck/bootldr.c', 'src/wpc/pinheck/bootldr.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/prop.o $(PINOBJ)/pinheck/eeprom.o $(PINOBJ)/pinheck/rtc.o $(PINOBJ)/pinheck/bootldr.o $(OBJ)/cpu/p8x32a/p8x32a.o\n'),
    (['src/wpc/pinheck/sd.c', 'src/wpc/pinheck/sd.h', 'src/wpc/pinheck/vfat.c', 'src/wpc/pinheck/vfat.h',
      'src/wpc/pinheck/zipsrc.c', 'src/wpc/pinheck/zipsrc.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/sd.o $(PINOBJ)/pinheck/vfat.o $(PINOBJ)/pinheck/zipsrc.o\n'),
    (['src/wpc/pinheck/audio.c', 'src/wpc/pinheck/audio.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/audio.o\n'),
    (['src/wpc/pinheck/display.c', 'src/wpc/pinheck/display.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/display.o\n'),
    (['src/wpc/pinheck/board.c', 'src/wpc/pinheck/board.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/board.o\n'),
]
# game definitions with a playfield simulator: (source, pinmame.mak line after pinheckgames.o)
SIMS = [
    ('src/wpc/sims/pinheck/dominos.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/dominos.o\n'),
    ('src/wpc/sims/pinheck/rzspook.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/rzspook.o\n'),
    ('src/wpc/sims/pinheck/jetsons.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/jetsons.o\n'),
]
changed = []


def edit(path, anchor, insert):
    s = open(path, newline='').read()
    if insert.strip() in s:
        return
    if s.count(anchor) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(anchor), anchor))
    s = s.replace(anchor, anchor + insert, 1)
    open(path, 'w', newline='').write(s)
    changed.append(path)


def line_span(s, i):
    start = s.rfind('\n', 0, i) + 1
    end = s.find('\n', i)
    end = len(s) if end < 0 else end + 1
    nl = '\r\n' if s[start:end].endswith('\r\n') else '\n'
    return start, end, nl


def line_after(path, needle, lines):
    s = open(path, newline='').read()
    if s.count(needle) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(needle), needle))
    start, end, nl = line_span(s, s.find(needle))
    indent = s[start:s.find(needle)]
    lines = [l for l in lines if indent + l + nl not in s]
    if not lines:
        return
    s = s[:end] + ''.join(indent + l + nl for l in lines) + s[end:]
    open(path, 'w', newline='').write(s)
    changed.append(path)


def win(p):
    return '..\\' + p.replace('/', '\\')


srcs = []
for files, drvlibs in GROUPS:
    if not all(os.path.exists(f) for f in files):
        continue
    srcs += files
    edit('src/pinmame.mak', 'DRVLIBS += $(PINOBJ)/pinheck.o\n', drvlibs)
edit('src/pinmame.mak', 'OBJDIRS += $(PINOBJ)\n', 'OBJDIRS += $(PINOBJ)/pinheck $(OBJ)/cpu/p8x32a\n')
for path, pingames in SIMS:
    if not os.path.exists(path):
        continue
    srcs.append(path)
    edit('src/pinmame.mak', 'PINGAMES += $(PINOBJ)/pinheckgames.o\n', pingames)
    edit('src/pinmame.mak', 'OBJDIRS += $(PINOBJ)/sims/se/prelim\n', 'OBJDIRS += $(PINOBJ)/sims/pinheck\n')

for path in sorted(glob.glob('cmake/*/CMakeLists*.txt')):
    if 'src/wpc/pinheckgames.c' in open(path).read():
        line_after(path, 'src/wpc/pinheckgames.c', srcs)

for path in sorted(glob.glob('vcproj/*.vcxproj')):
    s = open(path).read()
    if 'pinheckgames.c' not in s:
        continue
    line_after(path, '<ClCompile Include="..\\src\\wpc\\pinheckgames.c" />',
               ['<ClCompile Include="%s" />' % win(p) for p in srcs
                if p.endswith('.c') and '<ClCompile Include="%s"' % win(p) not in s])
    line_after(path, '<ClInclude Include="..\\src\\wpc\\pinheck.h" />',
               ['<ClInclude Include="%s" />' % win(p) for p in srcs if p.endswith('.h')])

PINHECK_ITEM = re.compile(r'<ClCompile Include="(\.\.\\src\\wpc\\pinheck\\[^"]+\.c)" />')
for path in sorted(glob.glob('vcproj/*.vcxproj')):
    s = open(path, newline='').read()
    configs = re.findall(r'<ProjectConfiguration Include="([^"]+)">', s)
    m = PINHECK_ITEM.search(s)
    while m:
        start, end, nl = line_span(s, m.start())
        indent = s[start:m.start()]
        block = indent + '<ClCompile Include="%s">' % m.group(1) + nl
        block += ''.join(indent + "  <ObjectFileName Condition=\"'$(Configuration)|$(Platform)'=='%s'\">$(IntDir)pinheck\\</ObjectFileName>" % c + nl for c in configs)
        block += indent + '</ClCompile>' + nl
        s = s[:start] + block + s[end:]
        changed.append(path)
        m = PINHECK_ITEM.search(s)
    open(path, 'w', newline='').write(s)

for path in sorted(glob.glob('vcproj/*.vcxproj.filters')):
    s = open(path, newline='').read()
    anchor = '<ClCompile Include="..\\src\\wpc\\pinheckgames.c">'
    if anchor not in s:
        continue
    if s.count(anchor) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(anchor), anchor))
    start, end, nl = line_span(s, s.find(anchor))
    items = ''.join('    <ClCompile Include="%s">%s      <Filter>Source Files\\PinMAME</Filter>%s    </ClCompile>%s'
                    % (win(p), nl, nl, nl) for p in srcs
                    if p.endswith('.c') and '<ClCompile Include="%s">' % win(p) not in s)
    if not items:
        continue
    s = s[:start] + items + s[start:]
    open(path, 'w', newline='').write(s)
    changed.append(path)

for p in sorted(set(changed)):
    print('updated', p)
