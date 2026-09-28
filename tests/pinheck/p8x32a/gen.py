#!/usr/bin/env python3
import argparse
import os
import random

CONDS = ['if_never', 'if_nc_and_nz', 'if_nc_and_z', 'if_nc', 'if_c_and_nz', 'if_nz', 'if_c_ne_z',
         'if_nc_or_nz', 'if_c_and_z', 'if_c_eq_z', 'if_z', 'if_nc_or_z', 'if_c', 'if_c_or_nz', 'if_c_or_z', '']
WRITES = ['ror', 'rol', 'shr', 'shl', 'rcr', 'rcl', 'sar', 'rev', 'mins', 'maxs', 'min', 'max', 'movs', 'movd',
          'movi', 'and', 'andn', 'or', 'xor', 'muxc', 'muxnc', 'muxz', 'muxnz', 'add', 'sub', 'addabs', 'subabs',
          'sumc', 'sumnc', 'sumz', 'sumnz', 'mov', 'neg', 'abs', 'absneg', 'negc', 'negnc', 'negz', 'negnz',
          'addx', 'subx', 'adds', 'subs', 'addsx', 'subsx', 'cmpsub']
TESTS = ['test', 'testn', 'cmp', 'cmpx', 'cmps', 'cmpsx']
REGS = ['r%d' % k for k in range(12)]
HUB = ['h%d' % k for k in range(4)]


class Gen:
    def __init__(self, rnd, hubflags):
        self.r = rnd
        self.hubflags = hubflags
        self.out = []
        self.label = 0

    def emit(self, s):
        self.out.append('        ' + s)

    def cond(self):
        return self.r.choice(CONDS) if self.r.random() < 0.3 else ''

    def src(self):
        return '#%d' % self.r.randrange(512) if self.r.random() < 0.4 else self.r.choice(REGS)

    def flags(self, writes):
        f = []
        if self.r.random() < 0.5:
            f.append('wz')
        if self.r.random() < 0.5:
            f.append('wc')
        if writes and self.r.random() < 0.15:
            f.append('nr')
        if not writes and self.r.random() < 0.15:
            f.append('wr')
        return ' ' + ', '.join(f) if f else ''

    def alu(self):
        if self.r.random() < 0.8:
            op, w = self.r.choice(WRITES), True
        else:
            op, w = self.r.choice(TESTS), False
        return '%-12s %-7s %s, %s%s' % (self.cond(), op, self.r.choice(REGS), self.src(), self.flags(w))

    def hub(self):
        op = self.r.choice(['rdlong', 'rdword', 'rdbyte', 'wrlong', 'wrword', 'wrbyte'])
        fl = self.flags(False).replace(', wr', '').replace(' wr', '') if self.hubflags else ''
        fl = '' if fl.strip() in ('', ',') else fl
        self.emit('%-12s %-7s %s, %s%s' % (self.cond(), op, self.r.choice(REGS), self.r.choice(HUB), fl))
        if self.r.random() < 0.3:
            h = self.r.choice(HUB)
            self.emit('add     %s, #%d' % (h, self.r.randrange(1, 4)))
            self.emit('and     %s, hmask' % h)
            self.emit('or      %s, hbase' % h)

    def branch(self):
        lab = 'L%d' % self.label
        self.label += 1
        k = self.r.randrange(4)
        if k == 0:
            self.emit('%-12s djnz    %s, #%s' % (self.cond(), self.r.choice(REGS), lab))
        elif k == 1:
            self.emit('%-12s tjz     %s, #%s' % (self.cond(), self.r.choice(REGS), lab))
        elif k == 2:
            self.emit('%-12s tjnz    %s, #%s' % (self.cond(), self.r.choice(REGS), lab))
        else:
            self.emit('%-12s jmp     #%s' % (self.cond(), lab))
        for _ in range(self.r.randrange(4)):
            self.emit(self.alu())
        self.out.append(lab)

    def program(self, n):
        r = self.r
        self.out += ['PUB main', '  cognew(@entry, 0)', '  repeat until long[$6FFC]', 'DAT',
                     '        long    $C0DE5EED', '        org     0']
        self.out.append('entry')
        self.emit('test    %s, #%d wz, wc' % (r.choice(REGS), r.randrange(512)))
        for _ in range(n):
            k = r.randrange(10)
            if k < 7:
                self.emit(self.alu())
            elif k < 8:
                self.hub()
            else:
                self.branch()
        self.emit('muxc    fl, #1')
        self.emit('muxz    fl, #2')
        for reg in REGS + ['fl']:
            self.emit('wrlong  %s, ptr' % reg)
            self.emit('add     ptr, #4')
        self.emit('wrlong  one, done')
        self.emit('cogid   ptr')
        self.emit('cogstop ptr')
        for reg in REGS:
            self.out.append('%-7s long    $%08X' % (reg, r.getrandbits(32)))
        for k, reg in enumerate(HUB):
            self.out.append('%-7s long    $%04X' % (reg, 0x6200 + r.randrange(0, 0x100, 4) + k))
        self.out += ['fl      long    0', 'ptr     long    $6000', 'done    long    $6FFC', 'one     long    1',
                     'hmask   long    $FF', 'hbase   long    $6200', '        long    $C0DEE0D0']
        return '\n'.join(self.out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--count', type=int, default=100)
    ap.add_argument('--first', type=int, default=1)
    ap.add_argument('--length', type=int, default=150)
    ap.add_argument('--hubflags', action='store_true')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for seed in range(a.first, a.first + a.count):
        with open(os.path.join(a.out, 'r%05d.spin' % seed), 'w') as f:
            f.write(Gen(random.Random(seed), a.hubflags).program(a.length))


if __name__ == '__main__':
    main()
