/* Uncertain reads (mips32_uncertain): random programs load a device word whose bits in a mask are known only later;
   each runs once with the true word and once with wrong bits there, settled through bus.settle (at once or after
   some instructions), with a timer-like interrupt whose handler stores every register, and traps and overflows whose
   handler returns past them. The programs mix ALU, shift, HI/LO, SPECIAL2/3, branch, jump-register, load and store
   (LWL/LWR, LL/SC) instructions. Registers, HI/LO, PC and memory must end the same. */
#include "mips32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEV 0xF000u   /* the device word, physical */
#define CLR 0xFFF4u   /* a store here drops the interrupt request */
#define PROG 0x1000u  /* program, physical (kseg0 0x80001000) */
#define NPROG 64

static uint8_t kmem[0x10000];
static mips32_state cpu;
static uint32_t dev_true, dev_mask, dev_guess, rng;
static int uncertain, settle_after, asked, shadow;

static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static uint32_t rd(void *ctx, uint32_t pa, int size, int fetch, int *err)
{
	uint32_t v = 0;
	int i;
	(void)ctx; (void)fetch;
	if (pa >= DEV && pa < DEV + 4) {
		unsigned sh = (pa & 3) * 8;
		uint32_t sm = size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1;
		uint32_t w = uncertain ? (dev_true & ~dev_mask) | (dev_guess & dev_mask) : dev_true;
		if (uncertain && ((dev_mask >> sh) & sm)) { mips32_uncertain(&cpu, (dev_mask >> sh) & sm, sh); asked = 0; }
		return (w >> sh) & sm;
	}
	if (pa >= sizeof(kmem) || pa + (unsigned)size > sizeof(kmem)) { *err = 1; return 0; }
	for (i = 0; i < size; i++) v |= (uint32_t)kmem[pa + i] << (8 * i);
	return v;
}

static void wr(void *ctx, uint32_t pa, uint32_t v, int size, int *err)
{
	int i;
	(void)ctx;
	if (pa == CLR) { mips32_set_eic(&cpu, 0, 0, 0); return; }
	if (pa >= DEV && pa < DEV + 4) return;
	if (pa >= sizeof(kmem) || pa + (unsigned)size > sizeof(kmem)) { *err = 1; return; }
	for (i = 0; i < size; i++) kmem[pa + i] = (uint8_t)(v >> (8 * i));
}

/* the true bits, in the read's value (token: its shift); unknown for settle_after asks after each uncertain read */
static int settle(void *ctx, uint32_t token, int wait, uint32_t *bits)
{
	(void)ctx;
	if (!wait && asked++ < settle_after) return 0;
	*bits = dev_true >> token;
	return 1;
}

static void put(uint32_t pa, uint32_t w) { kmem[pa] = (uint8_t)w; kmem[pa + 1] = (uint8_t)(w >> 8); kmem[pa + 2] = (uint8_t)(w >> 16); kmem[pa + 3] = (uint8_t)(w >> 24); }

#define R(op, rs, rt, rd, sa, fn) ((uint32_t)(op) << 26 | (uint32_t)(rs) << 21 | (uint32_t)(rt) << 16 | (uint32_t)(rd) << 11 | (uint32_t)(sa) << 6 | (fn))
#define I(op, rs, rt, imm) ((uint32_t)(op) << 26 | (uint32_t)(rs) << 21 | (uint32_t)(rt) << 16 | ((imm) & 0xFFFFu))

static unsigned reg(void) { return 1 + rnd() % 12; }

#define SP3(rs, rt, rd, sa, fn) R(0x1F, rs, rt, rd, sa, fn)

/* random instructions from slot k of NPROG into p; returns how many. Base registers: 24 = the device (kseg0),
   25 = scratch RAM, 13 = jump targets */
static int insn(int k, uint32_t *p)
{
	static const uint8_t fn[] = { 0x20, 0x21, 0x22, 0x23, 0x24, 0x24, 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B, 0x0A, 0x0B, 0x04, 0x06, 0x07 };
	static const uint8_t ld[] = { 0x20, 0x21, 0x23, 0x24, 0x25 };
	static const uint8_t md[] = { 0x18, 0x19, 0x1A, 0x1B, 0x11, 0x13 };
	static const uint8_t s2[] = { 0x00, 0x01, 0x02, 0x04, 0x05, 0x20, 0x21 };
	static const uint8_t bsh[] = { 0x02, 0x10, 0x18 };
	static const uint8_t tr[] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x36 };
	static const uint8_t tri[] = { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0E };
	static const uint8_t bz[] = { 0x00, 0x01, 0x10, 0x11 };
	static const uint8_t sh[] = { 0x00, 0x02, 0x03 };
	static const uint8_t st[] = { 0x2B, 0x28, 0x29 };
	unsigned a, b;
	switch (rnd() % 30) {
	case 0: case 1: p[0] = I(ld[rnd() % 5], 24, reg(), 0); return 1;
	case 2: { unsigned o = ld[rnd() % 5], off = o == 0x23 ? 0 : o == 0x21 || o == 0x25 ? (rnd() % 2) * 2 : rnd() % 4; p[0] = I(o, 24, reg(), off); return 1; }
	case 3: case 4: case 5: p[0] = R(0, reg(), reg(), reg(), 0, fn[rnd() % sizeof(fn)]); return 1;
	case 6: p[0] = I(0x0C, reg(), reg(), rnd() & 0xFFFF); return 1;                         /* andi */
	case 7: p[0] = I(0x08 + rnd() % 7, reg(), reg(), rnd() & 0xFFFF); return 1;             /* addi addiu slti sltiu andi ori xori */
	case 8: p[0] = R(0, (rnd() % 4) ? 0 : 1, reg(), reg(), rnd() % 32, sh[rnd() % 3]); return 1; /* sll, srl or rotr, sra */
	case 9: p[0] = I(st[rnd() % 3], 25, reg(), (rnd() % 16) * 4 + (rnd() % 2) * 2); return 1; /* sw, sb, sh */
	case 10: p[0] = I(0x23, 25, reg(), (rnd() % 16) * 4); return 1;                          /* lw scratch */
	case 11: p[0] = R(0, reg(), reg(), 0, 0, md[rnd() % sizeof(md)]); return 1;              /* mult(u) div(u) mthi mtlo */
	case 12: p[0] = R(0, 0, 0, reg(), 0, (rnd() % 2) ? 0x10 : 0x12); return 1;               /* mfhi mflo */
	case 13: p[0] = R(0x1C, reg(), reg(), reg(), 0, s2[rnd() % sizeof(s2)]); return 1;       /* madd(u) mul msub(u) clz clo */
	case 14: p[0] = SP3(0, reg(), reg(), bsh[rnd() % 3], 0x20); return 1;                    /* wsbh seb seh */
	case 15: a = rnd() % 32; b = rnd() % (32 - a); p[0] = SP3(reg(), reg(), b, a, 0x00); return 1; /* ext: pos a, size b + 1 */
	case 16: a = rnd() % 32; b = a + rnd() % (32 - a); p[0] = SP3(reg(), reg(), b, a, 0x04); return 1; /* ins: lsb a, msb b */
	case 17: p[0] = I((rnd() % 2) ? 0x22 : 0x26, (rnd() % 2) ? 24 : 25, reg(), rnd() % 4); return 1; /* lwl lwr, device or scratch */
	case 18: p[0] = I(0x0F, 0, reg(), rnd() & 0xFFFF); return 1;                            /* lui */
	case 19: p[0] = R(0, reg(), reg(), 0, 0, tr[rnd() % sizeof(tr)]); return 1;           /* teq tne tge tgeu tlt tltu */
	case 20: p[0] = I(0x01, reg(), tri[rnd() % sizeof(tri)], rnd() & 0xFFFF); return 1;     /* tgei tgeiu tlti tltiu teqi tnei */
	case 21: p[0] = I(0x30, 25, reg(), (rnd() % 16) * 4); return 1;                          /* ll */
	case 22: p[0] = I(0x38, 25, reg(), (rnd() % 16) * 4); return 1;                          /* sc */
	case 23: case 24: {
		/* lui/ori r13, a forward target; jr r13 or jalr rd, r13 */
		uint32_t t = 0x80000000u + PROG + 4 * (uint32_t)(k + 4 + (int)(rnd() % 4));
		if (k + 8 >= NPROG) return 0;
		p[0] = I(0x0F, 0, 13, t >> 16);
		p[1] = I(0x0D, 13, 13, t & 0xFFFF);
		p[2] = (rnd() % 2) ? R(0, 13, 0, 0, 0, 0x08) : R(0, 13, 0, reg(), 0, 0x09);
		return 3;
	}
	case 25: {
		int fwd = 2 + (int)(rnd() % 6);
		if (k + fwd + 1 >= NPROG) return 0;
		p[0] = (rnd() % 2) ? I(0x01, reg(), bz[rnd() % 4], fwd) : I(0x06 + rnd() % 2, reg(), 0, fwd); /* bltz bgez bltzal bgezal; blez bgtz */
		return 1;
	}
	case 26: p[0] = 0x41400000u | reg() << 16 | reg() << 11; return 1;                       /* rdpgpr (previous set = this one) */
	default: {
		int fwd = 2 + (int)(rnd() % 6);
		if (k + fwd + 1 >= NPROG) return 0;
		p[0] = I(0x04 + rnd() % 2, reg(), reg(), fwd);                                     /* beq, bne */
		return 1;
	}
	}
}

static void load_program(void)
{
	int k, n, j;
	uint32_t w[4];
	memset(kmem, 0, sizeof(kmem));
	/* the handler at 0x180: store registers 1-12 (base 26), drop the request (base 27); in shadow set 1 it reads
	   them from the interrupted set with RDPGPR. An exception (ExcCode not 0) returns past the instruction. */
	for (k = 1; k <= 12; k++) {
		put(0x180 + 8 * (uint32_t)(k - 1), shadow ? 0x41400000u | (uint32_t)k << 16 | 13u << 11 : 0);
		put(0x184 + 8 * (uint32_t)(k - 1), I(0x2B, 26, shadow ? 13 : k, 4 * k));
	}
	put(0x180 + 96, I(0x2B, 27, 0, 0));
	put(0x180 + 100, 0x40000000u | 14u << 16 | 13u << 11);  /* mfc0 r14, Cause */
	put(0x180 + 104, I(0x0C, 14, 14, 0x7C));
	put(0x180 + 108, I(0x04, 14, 0, 4));                      /* beq r14, r0, eret */
	put(0x180 + 112, 0);
	put(0x180 + 116, 0x40000000u | 14u << 16 | 14u << 11);  /* mfc0 r14, EPC */
	put(0x180 + 120, I(0x09, 14, 14, 4));
	put(0x180 + 124, 0x40800000u | 14u << 16 | 14u << 11);  /* mtc0 r14, EPC */
	put(0x180 + 128, 0x42000018u);
	for (k = 0; k < NPROG; k += n) {
		n = insn(k, w);
		if (!n) { w[0] = 0; n = 1; }
		while (n > 0 && k + n > NPROG) n--;
		for (j = 0; j < n; j++) put(PROG + 4 * (uint32_t)(k + j), w[j]);
		if (!n) n = 1;
	}
	put(PROG + 4 * NPROG, 0x1000FFFFu); /* b . */
	put(PROG + 4 * NPROG + 4, 0);
}

static void reset_cpu(void)
{
	mips32_bus bus;
	int k;
	memset(&bus, 0, sizeof(bus));
	bus.read = rd;
	bus.write = wr;
	bus.settle = settle;
	mips32_init(&cpu, &bus, 2, 0x00018700u);
	mips32_direct(&cpu, 0, 0, DEV, kmem, kmem); /* code and RAM; the device and CLR go through the bus */
	cpu.status = 0x00000001u;
	cpu.pc = 0x80000000u + PROG;
	cpu.npc = cpu.pc + 4;
	for (k = 1; k < 24; k++) mips32_regs(&cpu)[k] = rnd();
	mips32_regs(&cpu)[24] = 0x80000000u + DEV;
	mips32_regs(&cpu)[25] = 0x80000000u + 0x8000u;
	cpu.gpr[0][26] = cpu.gpr[1][26] = 0x80007000u;
	cpu.gpr[0][27] = cpu.gpr[1][27] = 0x80000000u + CLR;
	asked = 0;
}

/* a reset keeps the registers: a load still uncertain then holds its true value */
static int reset_settles(void)
{
	memset(kmem, 0, sizeof(kmem));
	put(PROG, I(0x23, 24, 1, 0));
	put(PROG + 4, 0x1000FFFFu);
	put(PROG + 8, 0);
	rng = 1;
	reset_cpu();
	dev_true = 0x12345678u;
	dev_mask = 0x00F00000u;
	dev_guess = ~dev_true;
	settle_after = 1000;
	uncertain = 1;
	mips32_run(&cpu, 10);
	mips32_reset(&cpu);
	if (mips32_regs(&cpu)[1] == dev_true) return 0;
	printf("UNCERTAIN FAIL reset: r1 %08x, true %08x\n", (unsigned)mips32_regs(&cpu)[1], (unsigned)dev_true);
	return 1;
}

/* uncertain_test [programs [first]]: programs from number first on (default 20000 from 0) */
int main(int argc, char **argv)
{
	static uint8_t mem_a[sizeof(kmem)];
	uint32_t regs_a[32], seed;
	int t, fails = reset_settles(), ints = 0, count = argc > 1 ? atoi(argv[1]) : 20000, first = argc > 2 ? atoi(argv[2]) : 0;
	unsigned long excs = 0;
	for (t = first; t < first + count; t++) {
		uint32_t s0, irq_at, k;
		int pass, slices;
		uint64_t cyc_a = 0;
		uint32_t pc_a = 0, hi_a = 0, lo_a = 0;
		seed = 0x9E3779B9u * (uint32_t)(t + 1);
		rng = seed;
		dev_true = rnd();
		dev_mask = (rnd() % 3) ? 1u << (rnd() % 32) : rnd();
		dev_guess = ~dev_true;
		settle_after = (int)(rnd() % 40);
		shadow = (int)(rnd() % 2);
		irq_at = rnd() % 80;
		s0 = rnd();
		for (pass = 0; pass < 2; pass++) {
			rng = s0;
			load_program();
			reset_cpu();
			uncertain = pass;
			for (slices = 0; slices < 100 && cpu.cycles < 200; slices++) {
				if (cpu.cycles >= irq_at && cpu.cycles < irq_at + 4) { mips32_set_eic(&cpu, 1, 0, shadow); if (pass) ints++; }
				mips32_run(&cpu, 1 + (int)((cpu.cycles * 7) % 5));
			}
			mips32_settle(&cpu);
			if (pass) excs += cpu.exc_seq;
			if (!pass) {
				memcpy(mem_a, kmem, sizeof(kmem));
				memcpy(regs_a, mips32_regs(&cpu), sizeof(regs_a));
				pc_a = cpu.pc; hi_a = cpu.hi; lo_a = cpu.lo; cyc_a = cpu.cycles;
				continue;
			}
			if (memcmp(mem_a, kmem, sizeof(kmem)) || memcmp(regs_a, mips32_regs(&cpu), sizeof(regs_a)) || pc_a != cpu.pc ||
			    hi_a != cpu.hi || lo_a != cpu.lo || cyc_a != cpu.cycles) {
				if (fails++ < 5) {
					printf("UNCERTAIN FAIL case %d (seed %08x, mask %08x):", t, (unsigned)seed, (unsigned)dev_mask);
					for (k = 1; k < 32; k++) if (regs_a[k] != mips32_regs(&cpu)[k]) printf(" r%u %08x/%08x", (unsigned)k, (unsigned)regs_a[k], (unsigned)mips32_regs(&cpu)[k]);
					printf(" pc %08x/%08x\n", (unsigned)pc_a, (unsigned)cpu.pc);
				}
			}
		}
	}
	printf("uncertain: %d programs from %d, %d interrupt raises, %lu exceptions, %d differ\n", count, first, ints, excs, fails);
	return fails != 0;
}
