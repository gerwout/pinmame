#!/usr/bin/env python3
import argparse
import os
import random
import re

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


def multi(r, hubflags, n, cogs):
    """Several cogs on shared hub bytes (RTL only): cog 0 starts cogs 1..cogs-1 on a second program,
    each with PAR $6040 * k and its registers mixed with PAR. Their bodies mix hub reads and writes on $6200-$62FF
    (each cog its own share), INA reads, OUTA writes on P8 (cog 0 drives it), short WAITCNTs, CNT and PAR sources,
    lock operations, a jump
    through a register, a jump whose target and a hub read whose destination the code rewrites; the started cogs also
    wait in idle loops (sleep) for their flag byte at $6100 + k, which cog 0 writes now and then and at its end, or for
    P8, which cog 0 toggles and leaves high."""
    names = re.compile(r'(?<![$\w])(r\d+|h\d|L\d+|fl|t|ptr|done|one|zero|hmask|hbase|entry|lk|pin|tgt|myf|f6100|J\d+|D\d+|W\d+)\b')

    def body(g, cog0):
        hub = g.r.choice([4, 8, 12])
        for _ in range(n):
            k = g.r.randrange(24)
            if k < 2:
                e = g.r.randrange(3)
                if e == 0:
                    g.emit('%-7s %s, ina%s' % (g.r.choice(['mov', 'and', 'xor', 'add']), g.r.choice(REGS), g.r.choice(['', ' wz', ' wc'])))
                elif e == 1:
                    g.emit('%-7s outa, %s' % (g.r.choice(['xor', 'or', 'andn']), 'pin'))
                else:
                    g.emit('mov     t, cnt')
                    g.emit('add     t, #%d' % g.r.randrange(9, 40))
                    g.emit('waitcnt t, #0')
            elif k < 3:
                # a lock: new, set or clear (their C comes from the hub)
                g.emit('%-7s lk%s' % (g.r.choice(['locknew', 'lockset', 'lockclr']), g.r.choice([' wc', ''])))
                g.emit('and     lk, #7')
            elif k < 4:
                lab, lab2 = 'L%d' % g.label, 'L%d' % (g.label + 1)
                g.label += 2
                m = g.r.randrange(3)
                if m == 0:
                    # a jump through a register
                    g.emit('mov     tgt, #%s' % lab)
                    g.emit('jmp     tgt')
                    g.emit(g.alu())
                    g.out.append(lab)
                elif m == 1:
                    # a jump whose target the code rewrites
                    g.emit('movs    J%d, #%s' % (g.label, lab))
                    g.emit(g.alu())
                    g.out.append('J%d     jmp     #%s' % (g.label, lab2))
                    g.emit(g.alu())
                    g.out.append(lab2)
                    g.emit(g.alu())
                    g.out.append(lab)
                else:
                    # a hub read whose destination the code rewrites
                    g.emit('movd    D%d, #%s' % (g.label, g.r.choice(REGS)))
                    g.emit(g.alu())
                    g.out.append('D%d     rdlong  0-0, %s' % (g.label, g.r.choice(HUB)))
                    g.out.append(lab)
                    g.out.append(lab2)
            elif k < 5 and cog0:
                # a started cog's flag byte
                g.emit('mov     t, #%d' % g.r.randrange(1, cogs))
                g.emit('add     t, f6100')
                g.emit('wrbyte  one, t')
            elif k < 5 and g.r.random() < 0.3:
                lab = 'W%d' % g.label
                g.label += 1
                if g.r.random() < 0.5:
                    # sleep until the flag byte is not zero
                    g.emit('wrbyte  zero, myf')
                    g.out.append(lab)
                    g.emit('rdbyte  t, myf')
                    g.emit('tjz     t, #%s' % lab)
                else:
                    # sleep until P8 is high
                    g.out.append(lab)
                    g.emit('test    pin, ina wz')
                    g.emit('if_z    jmp     #%s' % lab)
            elif k < 6:
                # CNT and PAR as sources
                g.emit('%-12s %-7s %s, %s%s' % (g.cond(), g.r.choice(['add', 'xor', 'sub', 'mov']), g.r.choice(REGS),
                                                g.r.choice(['cnt', 'par']), g.flags(True)))
            elif k < 16 - hub:
                g.emit(g.alu())
            elif k < 16:
                g.hub()
            else:
                g.branch()

    def block(head, tail, cog0):
        g = Gen(r, hubflags)
        g.out.append('entry')
        for x in head:
            g.emit(x)
        g.emit('test    %s, #%d wz, wc' % (r.choice(REGS), r.randrange(512)))
        body(g, cog0)
        if cog0:
            # the sleepers' flags and pin: set at the end
            g.emit('mov     t, f6100')
            for _ in range(cogs):
                g.emit('wrbyte  one, t')
                g.emit('add     t, #1')
            g.emit('or      outa, pin')
        g.emit('muxc    fl, #1')
        g.emit('muxz    fl, #2')
        for reg in REGS + ['fl']:
            g.emit('wrlong  %s, ptr' % reg)
            g.emit('add     ptr, #4')
        g.emit('wrlong  one, done')
        g.emit('cogid   ptr')
        g.emit('cogstop ptr')
        for reg in REGS:
            g.out.append('%-7s long    $%08X' % (reg, r.getrandbits(32)))
        for k in range(4):
            g.out.append('%-7s long    $%04X' % ('h%d' % k, 0x6200 + r.randrange(0, 0x100, 4) + k))
        g.out += ['fl      long    0', 't       long    0', 'one     long    1', 'zero    long    0', 'hmask   long    $FF',
                  'hbase   long    $6200', 'lk      long    0', 'pin     long    $100', 'tgt     long    0', 'myf     long    0',
                  'f6100   long    $6100'] + tail
        return g.out

    start = ['mov     x, wk', 'shl     x, #2', 'or      x, #%1000', 'mov     cn, #%d' % (cogs - 1), 'mov     cpar, #$40',
             ':start', 'mov     y, cpar', 'add     y, par0', 'shl     y, #16', 'or      y, x', 'coginit y', 'add     cpar, #$40',
             'djnz    cn, #:start', 'or      dira, pin']
    a = block(start, ['ptr     long    $6000', 'done    long    $6FFC', 'x       long    0', 'y       long    0',
                      'cn      long    0', 'cpar    long    0', 'par0    long    $6000', 'wk      long    $C0DEADD1'], True)
    a = [('        ' + x[1:].strip() if x == ':start' else x) for x in a]
    a = [(':start' if x.strip() == ':start' else x) for x in a]
    mix = ['xor     %s, par' % reg for reg in REGS] + ['mov     ptr, par', 'mov     done, par', 'add     done, #$3C',
                                                    'mov     myf, par', 'shr     myf, #6', 'and     myf, #7', 'add     myf, f6100']
    w = block(mix, ['ptr     long    0', 'done    long    0'], False)
    w = [names.sub(lambda m: 'w' + m.group(1), x) for x in w]
    out = ['PUB main', 'DAT', '        long    $C0DE5EED', '        org     0'] + a
    out += ['        long    $C0DEE0D0', '        long    $C0DE0B0B', '        org     0'] + w
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--count', type=int, default=100)
    ap.add_argument('--first', type=int, default=1)
    ap.add_argument('--length', type=int, default=150)
    ap.add_argument('--hubflags', action='store_true')
    ap.add_argument('--workers', type=float, default=0, help='share of programs with a scan-cog-shaped cog 1 (RTL only)')
    ap.add_argument('--cogs', type=int, default=0, help='programs of this many cogs on shared hub bytes (RTL only)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if a.cogs:
        for seed in range(a.first, a.first + a.count):
            with open(os.path.join(a.out, 'm%05d.spin' % seed), 'w') as f:
                f.write(multi(random.Random(seed), a.hubflags, a.length, a.cogs))
        return
    for seed in range(a.first, a.first + a.count):
        with open(os.path.join(a.out, 'r%05d.spin' % seed), 'w') as f:
            w = random.Random(seed * 7919 + 1)
            f.write(Gen(random.Random(seed), a.hubflags, w if w.random() < a.workers else None).program(a.length))


if __name__ == '__main__':
    main()
