#include "dmd.h"
#include <stdio.h>
#include <string.h>

/* the cog's scan loop (PROP_023.BIN, hub $336C) as pin writes: masks P16-P21, outa = P19|P21 at start, flags clear;
   per subframe level 0-15, per row 64 bytes high nibble first: data = value > level (muxc), then muxz/muxnz on P17;
   at the row's end P20 = first row (muxz), Z = 1, then P18 and P19 pulsed (muxz, muxnz) */
#define P(n) (1u << (n))

static uint8_t hub[65536];
static pinheck_dmd row, full;
static uint8_t subs[2][64][DMD_SUB];
static int levels[2][64], nsub[2];
static uint8_t frames[2][8][DMD_W * DMD_H];
static int nframe[2];
static uint32_t out, dir;
static uint64_t t;
static int fails;

static void on_sub(void *ctx, const uint8_t *s, int level, uint64_t when)
{
	int k = *(int *)ctx;
	(void)when;
	if (nsub[k] < 64) { memcpy(subs[k][nsub[k]], s, DMD_SUB); levels[k][nsub[k]] = level; }
	nsub[k]++;
}

static void on_frame(void *ctx, const uint8_t *f, uint64_t when)
{
	int k = *(int *)ctx;
	(void)when;
	if (nframe[k] < 8) memcpy(frames[k][nframe[k]], f, DMD_W * DMD_H);
	nframe[k]++;
}

static void put(void)
{
	t += 4;
	pinheck_dmd_pins(&row, t, out, dir);
	pinheck_dmd_pins(&full, t, out, dir);
}

static void mux(uint32_t pin, int v) { out = v ? out | pin : out & ~pin; put(); }

/* the cog runs subframes from its start: z is the Z flag, clear at cog start */
static void scan(uint32_t buf, int nsubs)
{
	int s, r, b, h, z = 0;
	for (s = 0; s < 6; s++) { dir |= P(16 + s); put(); }
	out = P(19); put();
	out |= P(21); put();
	for (s = 0; s < nsubs; s++) {
		const int level = s % 16;
		for (r = 0; r < 32; r++) {
			for (b = 0; b < 64; b++) {
				const uint8_t v = hub[buf + 64 * r + b];
				t += 8;
				for (h = 0; h < 2; h++) {
					const int n = h ? v & 15 : v >> 4;
					mux(P(16), level < n);
					mux(P(17), z);
					mux(P(17), !z);
				}
			}
			mux(P(20), r == 0);
			z = 1;
			mux(P(18), z);
			mux(P(18), !z);
			mux(P(19), z);
			mux(P(19), !z);
		}
	}
}

static void check(const char *name, int ok)
{
	if (!ok) { printf("DMD FAIL %s\n", name); fails++; }
}

static void reset(void)
{
	static int ids[2] = { 0, 1 };
	pinheck_dmd_init(&row, 0, hub, 0x5B0C, &ids[0], on_sub, on_frame);
	pinheck_dmd_init(&full, 1, NULL, 0, &ids[1], on_sub, on_frame);
	memset(nsub, 0, sizeof(nsub));
	memset(nframe, 0, sizeof(nframe));
	out = dir = 0;
}

int main(void)
{
	int x, y, k, ok;
	for (y = 0; y < 32; y++)
		for (x = 0; x < 128; x += 2) hub[0x5B0C + 64 * y + x / 2] = (uint8_t)(((x + y) % 16) << 4 | (x * 7 + y) % 16);
	reset();
	scan(0x5B0C, 40);
	/* the first subframe's first row has no row clock (P19 starts high): subframes from level 1 on */
	check("subframes", nsub[0] == 39 && nsub[1] == 39 && levels[0][0] == 1 && levels[0][15] == 0 && levels[1][15] == 0);
	check("row model = shifted bits", !memcmp(subs[0], subs[1], sizeof(subs[0])) && !memcmp(levels[0], levels[1], sizeof(levels[0])));
	/* subframe of level L: a dot is lit while its value is above L */
	for (ok = 1, y = 0; y < 32; y++)
		for (x = 0; x < 128; x++) {
			const uint8_t v = hub[0x5B0C + 64 * y + x / 2], n = x & 1 ? v & 15 : v >> 4;
			const int lit = subs[1][3][16 * y + x / 8] >> (7 - x % 8) & 1;
			if (lit != (n > levels[1][3])) ok = 0;
		}
	check("dots lit above the level", ok);
	/* whole cycles from level 0: one frame of 16 subframes = the dot values */
	check("frames", nframe[0] == 1 && nframe[1] == 1 && !memcmp(frames[0][0], frames[1][0], DMD_W * DMD_H));
	for (ok = nframe[0] == 1, y = 0; ok && y < 32; y++)
		for (x = 0; x < 128; x++) {
			const uint8_t v = hub[0x5B0C + 64 * y + x / 2], n = x & 1 ? v & 15 : v >> 4;
			if (frames[0][0][128 * y + x] != n) ok = 0;
		}
	check("frame = 4 bpp buffer", ok);
	/* the cog stops (DIRA cleared, a Propeller reboot) mid-subframe and starts again: nothing stale is kept */
	for (k = 0; k < 6; k++) { dir &= ~P(16 + k); put(); }
	out = 0; put();
	memset(nsub, 0, sizeof(nsub));
	memset(nframe, 0, sizeof(nframe));
	scan(0x5B0C, 33);
	check("restart", nsub[0] == 32 && nsub[1] == 32 && levels[0][0] == 1 && nframe[0] == 1 && !memcmp(subs[0], subs[1], sizeof(subs[0])));
	printf("dmd: %d failed\n", fails);
	return fails != 0;
}
