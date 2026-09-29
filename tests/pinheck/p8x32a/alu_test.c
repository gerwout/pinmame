/* The core's ALUs (alu, alu_run, bitrev) against the Milestone 3 ALU, kept here verbatim as the reference. */
#include "p8x32a.c"
#include <stdlib.h>

static uint32_t ref_bitrev(uint32_t x)
{
	uint32_t r = 0;
	int k;
	for (k = 0; k < 32; k++) r |= ((x >> k) & 1u) << (31 - k);
	return r;
}

static uint32_t ref_alu(unsigned i, uint32_t s, uint32_t d, unsigned pc, int run, int ci, int zi,
                    uint32_t bus_q, int bus_c, int *wr, int *co, int *zo)
{
	uint32_t r, dr = ref_bitrev(d), rot_r, lo, fill, log_r, add_d, add_s, add_r, sum_lo;
	unsigned sh = s & 31, ls;
	int rot_c, log_c, add_sub, add_ci, add_co, add_cm, add_cs, add_c, cin, c30, b31;
	uint64_t rot;
	static const unsigned ads_sel[4] = { 0, 1, 2, 3 };

	switch (i & 7) {
	case 0: fill = d & 0x7FFFFFFFu; break;
	case 1: fill = dr & 0x7FFFFFFFu; break;
	case 4: case 5: fill = ci ? 0x7FFFFFFFu : 0; break;
	case 6: fill = (d >> 31) ? 0x7FFFFFFFu : 0; break;
	default: fill = 0; break;
	}
	lo = (i & 1) ? dr : d;
	rot = (((uint64_t)fill << 32) | lo) >> sh;
	rot_r = (((i >> 1) & 3) != 3 && (i & 1)) ? ref_bitrev((uint32_t)rot) : (uint32_t)rot;
	rot_c = (((i >> 1) & 3) != 3 && (i & 1)) ? (int)(dr & 1) : (int)(d & 1);

	if (i & 4) ls = ((unsigned)(((i & 2) ? zi : ci) ^ (int)(i & 1))) << 1;
	else ls = (((i >> 1) & 1) << 1) | (unsigned)!(((i >> 1) ^ i) & 1);
	if (i & 8) {
		switch (ls) {
		case 0: log_r = d & ~s; break;
		case 1: log_r = d & s; break;
		case 2: log_r = d | s; break;
		default: log_r = d ^ s; break;
		}
	} else if (i & 4) {
		switch (i & 3) {
		case 0: log_r = (d & 0xFFFFFE00u) | (s & 511); break;
		case 1: log_r = (d & 0xFFFC01FFu) | ((s & 511) << 9); break;
		case 2: log_r = ((s & 511) << 23) | (d & 0x007FFFFFu); break;
		default: log_r = (d & 0xFFFFFE00u) | (pc & 511); break;
		}
	} else
		log_r = s;
	log_c = parity(log_r);

	(void)ads_sel;
	if (((i >> 4) & 3) == 2) {
		int ads[4];
		ads[0] = 0; ads[1] = (int)(s >> 31); ads[2] = ci; ads[3] = zi;
		add_sub = ads[(i >> 1) & 3] ^ (int)(i & 1);
	} else if (i == 0x32 || i == 0x34 || i == 0x36 || (i >> 2) == 0xF)
		add_sub = 0;
	else
		add_sub = 1;
	add_ci = ((((i >> 3) & 7) == 6 && ((i & 7) == 1 || (i & 2))) && ci) || (((i >> 3) & 3) == 3 && (i & 3) == 1);
	add_d = (((i >> 3) & 3) == 1) ? 0 : d;
	add_s = ((i & 31) == 0x19 || ((i >> 1) & 15) == 0xD) ? 0xFFFFFFFFu : add_sub ? ~s : s;
	cin = add_ci ^ add_sub;
	sum_lo = (add_d & 0x7FFFFFFFu) + (add_s & 0x7FFFFFFFu) + (uint32_t)cin;
	c30 = (int)(sum_lo >> 31);
	b31 = (int)(add_d >> 31) + (int)(add_s >> 31) + c30;
	add_r = (sum_lo & 0x7FFFFFFFu) | ((uint32_t)(b31 & 1) << 31);
	add_co = b31 >> 1;
	add_cm = c30;
	add_cs = add_co ^ (int)(add_d >> 31) ^ (int)(add_s >> 31);
	if (i == 0x38) add_c = add_co;
	else if (((i >> 3) & 7) == 5) add_c = (int)(s >> 31);
	else if ((i & 0x20) && ((i >> 2) & 3) == 1) add_c = add_co ^ add_cm;
	else if (((i >> 1) & 15) == 8) add_c = add_cs;
	else add_c = add_co ^ add_sub;

	if ((i >> 2) == 4) *wr = (int)(i & 1) ^ ((i & 2) ? !add_co : add_cs);
	else if (i == 0x38) *wr = add_co;
	else *wr = 1;

	if (i & 0x20) r = add_r;
	else if (i & 0x10) r = log_r;
	else if (i & 8) r = rot_r;
	else r = (run || (pc >> 4) != 31) ? bus_q : 0;

	switch ((i >> 3) & 7) {
	case 0: *co = bus_c; break;
	case 1: *co = rot_c; break;
	case 3: *co = log_c; break;
	default: *co = add_c; break;
	}
	*zo = !r && (zi || !(((i >> 3) & 7) == 6 && ((i & 7) == 1 || (i & 2))));
	return r;
}

static uint32_t rnd_state = 12345;

static uint32_t rnd(void)
{
	rnd_state ^= rnd_state << 13;
	rnd_state ^= rnd_state >> 17;
	rnd_state ^= rnd_state << 5;
	return rnd_state;
}

static uint32_t operand(void)
{
	static const uint32_t edge[] = { 0, 1, 2, 31, 32, 511, 0x55555555u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xAAAAAAAAu, 0xFFFFFFFEu, 0xFFFFFFFFu };
	switch (rnd() % 4) {
	case 0: return edge[rnd() % (sizeof(edge) / sizeof(edge[0]))];
	case 1: return rnd() & 0x3F;
	default: return rnd();
	}
}

int main(int argc, char **argv)
{
	unsigned long n = argc > 1 ? strtoul(argv[1], NULL, 0) : 4000000, k, bad = 0;
	for (k = 0; k < n; k++) {
		unsigned i = (unsigned)(k & 63), pc = rnd() & 511;
		uint32_t s = operand(), d = operand(), q = rnd(), r1, r2;
		int run = (int)(rnd() & 1), ci = (int)(rnd() & 1), zi = (int)(rnd() & 1), bc = (int)(rnd() & 1);
		int w1, c1, z1, w2, c2, z2;
		if (ref_bitrev(d) != bitrev(d) && bad++ < 10) printf("ALU FAIL: bitrev %08x\n", (unsigned)d);
		r1 = ref_alu(i, s, d, pc, run, ci, zi, q, bc, &w1, &c1, &z1);
		r2 = alu(i, s, d, pc, run, ci, zi, q, bc, &w2, &c2, &z2);
		if ((r1 != r2 || w1 != w2 || c1 != c2 || z1 != z2) && bad++ < 10)
			printf("ALU FAIL: alu op %02x s %08x d %08x pc %03x run %d c %d z %d: ref %08x w%d c%d z%d, got %08x w%d c%d z%d\n",
			       i, (unsigned)s, (unsigned)d, pc, run, ci, zi, (unsigned)r1, w1, c1, z1, (unsigned)r2, w2, c2, z2);
		/* alu_run: a running cog's non-hub, non-wait ops, as the core calls it (no hub result) */
		if (i < 4 || i >= 0x3C) continue;
		r1 = ref_alu(i, s, d, pc, 1, ci, zi, 0, bc, &w1, &c1, &z1);
		r2 = alu_run(i, s, d, pc, ci, zi, bc, &w2, &c2, &z2);
		if ((r1 != r2 || w1 != w2 || c1 != c2 || z1 != z2) && bad++ < 10)
			printf("ALU FAIL: alu_run op %02x s %08x d %08x pc %03x c %d z %d: ref %08x w%d c%d z%d, got %08x w%d c%d z%d\n",
			       i, (unsigned)s, (unsigned)d, pc, ci, zi, (unsigned)r1, w1, c1, z1, (unsigned)r2, w2, c2, z2);
	}
	printf("alu: %lu cases, %lu mismatches\n", n, bad);
	return bad != 0;
}
