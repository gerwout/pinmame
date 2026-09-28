#!/usr/bin/env python3
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

NS = '{http://schemas.microsoft.com/developer/msbuild/2003}'


def cond_config(c):
    m = re.search(r"==\s*'([^']+)'", c or '')
    return m.group(1) if m else None


def check(path):
    root = ET.parse(path).getroot()
    configs = [p.get('Include') for p in root.iter(NS + 'ProjectConfiguration')]
    default = {c: '$(IntDir)' for c in configs}
    for g in root.iter(NS + 'ItemDefinitionGroup'):
        c = cond_config(g.get('Condition'))
        for o in g.iter(NS + 'ObjectFileName'):
            for cfg in ([c] if c else configs):
                default[cfg] = o.text or '$(IntDir)'
    bad = []
    for cfg in configs:
        seen = {}
        for item in root.iter(NS + 'ClCompile'):
            inc = item.get('Include')
            if not inc:
                continue
            out = default.get(cfg, '$(IntDir)')
            for o in item.findall(NS + 'ObjectFileName'):
                oc = cond_config(o.get('Condition'))
                if oc is None or oc == cfg:
                    out = o.text or out
            if '%(' in out:
                continue
            name = os.path.splitext(inc.replace('\\', '/').split('/')[-1])[0].lower() + '.obj'
            key = (out.rstrip('\\') + '\\' + name).lower()
            if key in seen and seen[key] != inc:
                bad.append('%s [%s]: %s and %s both compile to %s' % (os.path.basename(path), cfg, seen[key], inc, key))
            seen.setdefault(key, inc)
    return bad


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'vcproj'
    bad = []
    for p in sorted(glob.glob(os.path.join(root, '*.vcxproj'))):
        bad += check(p)
    pinheck = [b for b in bad if 'pinheck' in b.lower()]
    for b in pinheck:
        print('vcxproj: ' + b)
    other = len(bad) - len(pinheck)
    if other:
        print('vcxproj: %d pre-existing collisions not involving pinHeck files (not checked)' % other)
    print('vcxproj: %s' % ('FAIL' if pinheck else 'ok'))
    return 1 if pinheck else 0


if __name__ == '__main__':
    sys.exit(main())
