#!/usr/bin/env python3
import argparse
import os
import random

BASE = 23
TEMP = 1
DEST = [r for r in range(32) if r != BASE]
DEST_NZ = [r for r in DEST if r not in (0, TEMP)]
SRC = list(range(32))

R3 = ['addu', 'subu', 'and', 'or', 'xor', 'nor', 'slt', 'sltu', 'movn', 'movz', 'mul']
RV = ['sllv', 'srlv', 'srav', 'rotrv']
R2S = ['clz', 'clo']
R2T = ['seb', 'seh', 'wsbh']
SH = ['sll', 'srl', 'sra', 'rotr']
IS = ['addiu', 'slti', 'sltiu']
IU = ['andi', 'ori', 'xori']
MD = ['mult', 'multu', 'madd', 'maddu', 'msub', 'msubu']
BR2 = ['beq', 'bne', 'beql', 'bnel']
BR1 = ['blez', 'bgtz', 'bltz', 'bgez', 'blezl', 'bgtzl', 'bltzl', 'bgezl']
BRAL = ['bltzal', 'bgezal', 'bltzall', 'bgezall']


def reg(n):
    return '$%d' % n


class Gen:
    def __init__(self, rnd):
        self.r = rnd
        self.label = 0
        self.out = []

    def emit(self, s):
        self.out.append('\t' + s)

    def d(self):
        return reg(self.r.choice(DEST))

    def s(self):
        return reg(self.r.choice(SRC))

    def simple(self):
        r = self.r
        k = r.randrange(9)
        if k == 0:
            return '%s %s, %s, %s' % (r.choice(R3), self.d(), self.s(), self.s())
        if k == 1:
            return '%s %s, %s, %s' % (r.choice(RV), self.d(), self.s(), self.s())
        if k == 2:
            return '%s %s, %s' % (r.choice(R2S), self.d(), self.s())
        if k == 3:
            return '%s %s, %s' % (r.choice(R2T), self.d(), self.s())
        if k == 4:
            return '%s %s, %s, %d' % (r.choice(SH), self.d(), self.s(), r.randrange(32))
        if k == 5:
            return '%s %s, %s, %d' % (r.choice(IS), self.d(), self.s(), r.randrange(-32768, 32768))
        if k == 6:
            return '%s %s, %s, 0x%x' % (r.choice(IU), self.d(), self.s(), r.randrange(65536))
        if k == 7:
            pos = r.randrange(32)
            size = r.randrange(1, 33 - pos)
            return '%s %s, %s, %d, %d' % (r.choice(['ext', 'ins']), self.d(), self.s(), pos, size)
        return 'lui %s, 0x%x' % (self.d(), r.randrange(65536))

    def hilo(self):
        r = self.r
        k = r.randrange(3)
        if k == 0:
            self.emit('%s %s, %s' % (r.choice(MD), self.s(), self.s()))
        elif k == 1:
            self.emit('%s %s' % (r.choice(['mfhi', 'mflo']), self.d()))
        else:
            self.emit('%s %s' % (r.choice(['mthi', 'mtlo']), self.s()))

    def div(self):
        rt = self.s()
        self.emit('srl %s, %s, 1' % (reg(TEMP), rt))
        self.emit('ori %s, %s, 1' % (reg(TEMP), reg(TEMP)))
        self.emit('%s $0, %s, %s' % (self.r.choice(['div', 'divu']), self.s(), reg(TEMP)))

    def mem(self):
        r = self.r
        k = r.randrange(5)
        if k == 0:
            op = r.choice(['lw', 'sw'])
            off = r.randrange(0, 256, 4)
        elif k == 1:
            op = r.choice(['lh', 'lhu', 'sh'])
            off = r.randrange(0, 256, 2)
        elif k == 2:
            op = r.choice(['lb', 'lbu', 'sb'])
            off = r.randrange(256)
        elif k == 3:
            op = r.choice(['lwl', 'lwr', 'swl', 'swr'])
            off = r.randrange(256)
        else:
            off = r.randrange(0, 256, 4)
            self.emit('ll %s, %d(%s)' % (self.d(), off, reg(BASE)))
            self.emit('sc %s, %d(%s)' % (self.d(), off, reg(BASE)))
            return
        rt = self.s() if op.startswith('s') else self.d()
        self.emit('%s %s, %d(%s)' % (op, rt, off, reg(BASE)))

    def block(self, head):
        lab = 'L%d' % self.label
        self.label += 1
        for h in head(lab):
            self.emit(h)
        self.emit(self.simple())
        for _ in range(self.r.randrange(4)):
            self.emit(self.simple())
        self.out.append(lab + ':')

    def branch(self):
        r = self.r
        k = r.randrange(6)
        if k == 0:
            self.block(lambda l: ['%s %s, %s, %s' % (r.choice(BR2), self.s(), self.s(), l)])
        elif k == 1:
            self.block(lambda l: ['%s %s, %s' % (r.choice(BR1), self.s(), l)])
        elif k == 2:
            src = reg(r.choice([x for x in SRC if x != 31]))
            self.block(lambda l: ['%s %s, %s' % (r.choice(BRAL), src, l)])
        elif k == 3:
            self.block(lambda l: ['%s %s' % (r.choice(['j', 'jal']), l)])
        elif k == 4:
            rd = reg(r.choice(DEST_NZ))
            self.block(lambda l: ['la %s, %s' % (reg(TEMP), l), 'jalr %s, %s' % (rd, reg(TEMP))])
        else:
            self.block(lambda l: ['la %s, %s' % (reg(TEMP), l), 'jr %s' % reg(TEMP)])

    def program(self, count):
        r = self.r
        self.out += ['#include "common.h"', '\t.text', '\t.globl __start', '__start:']
        self.emit('la %s, scratch' % reg(BASE))
        for n in (1, 2):
            v = r.getrandbits(32)
            self.emit('lui $1, 0x%x' % (v >> 16))
            self.emit('ori $1, $1, 0x%x' % (v & 0xFFFF))
            self.emit('mthi $1' if n == 1 else 'mtlo $1')
        for n in range(1, 32):
            if n == BASE:
                continue
            v = r.getrandbits(32)
            self.emit('lui %s, 0x%x' % (reg(n), v >> 16))
            self.emit('ori %s, %s, 0x%x' % (reg(n), reg(n), v & 0xFFFF))
        for _ in range(count):
            k = r.randrange(10)
            if k < 4:
                self.emit(self.simple())
            elif k < 6:
                self.mem()
            elif k < 7:
                self.hilo()
            elif k < 8:
                self.div()
            else:
                self.branch()
        for n in range(32):
            self.emit('sw %s, %d(%s)' % (reg(n), 256 + 4 * n, reg(BASE)))
        self.emit('mfhi $1')
        self.emit('sw $1, 384(%s)' % reg(BASE))
        self.emit('mflo $1')
        self.emit('sw $1, 388(%s)' % reg(BASE))
        self.emit('WRITE_EXIT(scratch, 392)')
        self.out += ['\t.data', '\t.align 4', 'scratch:']
        data = [r.randrange(256) for _ in range(256)]
        for i in range(0, 256, 16):
            self.emit('.byte ' + ', '.join(str(b) for b in data[i:i + 16]))
        self.emit('.space 136')
        return '\n'.join(self.out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--count', type=int, default=200)
    ap.add_argument('--first', type=int, default=1)
    ap.add_argument('--length', type=int, default=300)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for seed in range(a.first, a.first + a.count):
        with open(os.path.join(a.out, 'r%05d.S' % seed), 'w') as f:
            f.write(Gen(random.Random(seed)).program(a.length))


if __name__ == '__main__':
    main()
