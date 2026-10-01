/* Uncertain reads (mips32_uncertain): random programs load a device word whose bits in a mask are known only later;
   each runs once with the true word and once with wrong bits there, settled through bus.settle (at once or after
   some instructions), with a timer-like interrupt whose handler stores every register. Registers, HI/LO, PC and
   memory must end the same. */
#include "mips32.h"
#include <stdio.h>
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
	if (pa + (unsigned)size > sizeof(kmem)) { *err = 1; return 0; }
	for (i = 0; i < size; i++) v |= (uint32_t)kmem[pa + i] << (8 * i);
	return v;
}

static void wr(void *ctx, uint32_t pa, uint32_t v, int size, int *err)
{
	int i;
	(void)ctx;
	if (pa == CLR) { mips32_set_eic(&cpu, 0, 0, 0); return; }
	if (pa >= DEV && pa < DEV + 4) return;
	if (pa + (unsigned)size > sizeof(kmem)) { *err = 1; return; }
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

/* one random instruction at slot k of NPROG; base registers: 24 = the device (kseg0), 25 = scratch RAM */
static uint32_t insn(int k)
{
	static const uint8_t fn[] = { 0x21, 0x23, 0x24, 0x24, 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B, 0x0A, 0x0B, 0x04, 0x06 };
	static const uint8_t ld[] = { 0x20, 0x21, 0x23, 0x24, 0x25 };
	switch (rnd() % 13) {
	case 12: return 0x41400000u | reg() << 16 | reg() << 11;                    /* rdpgpr (previous set = this one) */
	case 0: case 1: return I(ld[rnd() % 5], 24, reg(), 0) | ((rnd() % 4) & 0);
	case 2: { unsigned o = ld[rnd() % 5], off = o == 0x23 ? 0 : o == 0x21 || o == 0x25 ? (rnd() % 2) * 2 : rnd() % 4; return I(o, 24, reg(), off); }
	case 3: case 4: case 5: return R(0, reg(), reg(), reg(), 0, fn[rnd() % sizeof(fn)]);
	case 6: return I(0x0C, reg(), reg(), rnd() & 0xFFFF);                        /* andi */
	case 7: return I(0x09 + rnd() % 6, reg(), reg(), rnd() & 0xFFFF);            /* addiu slti sltiu andi ori xori */
	case 8: return R(0, 0, reg(), reg(), rnd() % 32, (rnd() % 2) ? 0x00 : 0x02);  /* sll, srl */
	case 9: return I((rnd() % 2) ? 0x2B : 0x28, 25, reg(), (rnd() % 16) * 4);   /* sw, sb */
	case 10: return I(0x23, 25, reg(), (rnd() % 16) * 4);                        /* lw scratch */
	default: {
		int fwd = 2 + (int)(rnd() % 6);
		if (k + fwd + 1 >= NPROG) return 0;
		return I(0x04 + rnd() % 2, reg(), reg(), fwd);                              /* beq, bne */
	}
	}
}

static void load_program(void)
{
	int k;
	memset(kmem, 0, sizeof(kmem));
	/* the handler at 0x180: store registers 1-12 (base 26), drop the request (base 27), return; in shadow set 1
	   it reads them from the interrupted set with RDPGPR */
	for (k = 1; k <= 12; k++) {
		put(0x180 + 8 * (uint32_t)(k - 1), shadow ? 0x41400000u | (uint32_t)k << 16 | 13u << 11 : 0);
		put(0x184 + 8 * (uint32_t)(k - 1), I(0x2B, 26, shadow ? 13 : k, 4 * k));
	}
	put(0x180 + 96, I(0x2B, 27, 0, 0));
	put(0x180 + 100, 0x42000018u);
	for (k = 0; k < NPROG; k++) put(PROG + 4 * (uint32_t)k, insn(k));
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

int main(void)
{
	static uint8_t mem_a[sizeof(kmem)];
	uint32_t regs_a[32], seed;
	int t, fails = 0, ints = 0;
	for (t = 0; t < 20000; t++) {
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
	printf("uncertain: 20000 programs, %d interrupt raises, %d differ\n", ints, fails);
	return fails != 0;
}
