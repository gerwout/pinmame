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


WREGS = ['w0', 'w1', 'w2', 'w3', 'v']


class Gen:
    def __init__(self, rnd, hubflags, worker=None):
        self.r = rnd
        self.hubflags = hubflags
        self.out = []
        self.label = 0
        # worker: a second random stream for a scan-cog-shaped cog 1 (reads hub RAM, computes, writes OUTA only)
        self.w = worker

    def emit(self, s):
        self.out.append('        ' + s)

    def cond(self):
        return self.r.choice(CONDS) if self.r.random() < 0.3 else ''

    def src(self):
        if self.w and self.w.random() < 0.05:
            return 'ina'
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

    def worker(self):
        w = self.w
        pins = 0
        while not pins:
            pins = w.getrandbits(16) << 8 & w.choice([0xFF00, 0xFF0000, 0x0F0F00, 0x1000, 0x80000])
        out = ['        long    $C0DEE0D0', '        long    $C0DE0B0B', '        org     0',
               'worker  mov     dira, wpins', '        mov     wp, par', ':loop']

        def alu():
            if w.random() < 0.8:
                op, wr = w.choice(WRITES), True
            else:
                op, wr = w.choice(TESTS), False
            src = '#%d' % w.randrange(512) if w.random() < 0.4 else w.choice(WREGS)
            f = [x for x in ('wz', 'wc') if w.random() < 0.5]
            if wr and w.random() < 0.1:
                f.append('nr')
            c = w.choice(CONDS) if w.random() < 0.3 else ''
            return '%-12s %-7s %s, %s%s' % (c, op, w.choice(WREGS), src, ' ' + ', '.join(f) if f else '')

        def outa():
            k = w.randrange(5)
            if k == 0:
                return 'mov     outa, %s' % w.choice(WREGS)
            if k == 1:
                return 'xor     outa, %s' % w.choice(WREGS)
            if k == 2:
                return 'and     outa, %s' % w.choice(WREGS)
            return '%-7s outa, wpins' % ('muxc' if k == 3 else 'muxz')

        out.append('        %-7s v, wp%s' % (w.choice(['rdbyte', 'rdword', 'rdlong']), w.choice(['', ' wz'])))
        for _ in range(w.randrange(1, 8)):
            out.append('        ' + (outa() if w.random() < 0.25 else alu()))
        out.append('        ' + outa())
        out += ['        add     wp, #%d' % w.randrange(1, 4), '        and     wp, wmask', '        or      wp, wbase',
                '        jmp     #:loop', 'wpins   long    $%08X' % pins, 'wp      long    0', 'wmask   long    $FF',
                'wbase   long    $6200']
        out += ['%-7s long    $%08X' % (x, w.getrandbits(32)) for x in WREGS]
        return out

    def program(self, n):
        r = self.r
        self.out += ['PUB main', '  cognew(@entry, 0)', '  repeat until long[$6FFC]', 'DAT',
                     '        long    $C0DE5EED', '        org     0']
        self.out.append('entry')
        if self.w:
            # cog 1 on the hub bytes at $6200 (hub(), below, writes them); its pins run lazily from about 20000 cycles
            for x in ('mov     wx, wk', 'shl     wx, #2', 'or      wx, wpar', 'or      wx, #1', 'coginit wx',
                      'mov     wx, cnt', 'add     wx, wdel', 'waitcnt wx, #0'):
                self.emit(x)
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
        if self.w:
            self.emit('cogstop one')
        self.emit('wrlong  one, done')
        self.emit('cogid   ptr')
        self.emit('cogstop ptr')
        for reg in REGS:
            self.out.append('%-7s long    $%08X' % (reg, r.getrandbits(32)))
        for k, reg in enumerate(HUB):
            self.out.append('%-7s long    $%04X' % (reg, 0x6200 + r.randrange(0, 0x100, 4) + k))
        self.out += ['fl      long    0', 'ptr     long    $6000', 'done    long    $6FFC', 'one     long    1',
                     'hmask   long    $FF', 'hbase   long    $6200']
        if self.w:
            self.out += ['wx      long    0', 'wpar    long    $62000000', 'wdel    long    %d' % self.w.randrange(16000, 24000),
                         'wk      long    $C0DEADD1'] + self.worker()
        self.out.append('        long    $C0DEE0D0')
        return '\n'.join(self.out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--count', type=int, default=100)
    ap.add_argument('--first', type=int, default=1)
    ap.add_argument('--length', type=int, default=150)
    ap.add_argument('--hubflags', action='store_true')
    ap.add_argument('--workers', type=float, default=0, help='share of programs with a scan-cog-shaped cog 1 (RTL only)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for seed in range(a.first, a.first + a.count):
        with open(os.path.join(a.out, 'r%05d.spin' % seed), 'w') as f:
            w = random.Random(seed * 7919 + 1)
            f.write(Gen(random.Random(seed), a.hubflags, w if w.random() < a.workers else None).program(a.length))


if __name__ == '__main__':
    main()
