#include "../../../src/wpc/pinheck/prop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLK  (1u << 25)
#define DATA (1u << 26)
#define BITS 64

static pinheck_prop p;
static uint8_t rom[0x8000], ram[0x8000], eemem[131072];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("PROP FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t hub32(uint32_t a)
{
	const uint8_t *h = p.chip.hub + a;
	return (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
}

static uint32_t count(void) { return hub32(0x7FF0); }

static void boot(void)
{
	memset(eemem, 0xFF, sizeof(eemem));
	prop_init(&p, rom, eemem);
	memcpy(p.chip.hub, ram, sizeof(ram));
}

static void pulse(uint64_t *pic, int bit)
{
	prop_pic_pins(&p, *pic, CLK | (bit ? DATA : 0));
	prop_pic_pins(&p, *pic + 200, bit ? DATA : 0);
	*pic += 400;
}

static void rates(void)
{
	boot();
	CHECK(prop_time(&p, 20000) == 3000);
	prop_catch_up(&p, 200000);
	CHECK(p.nseg == 2 && p.seg[1].num == 13 && p.seg[1].den == 10);
	CHECK(prop_time(&p, 200010) - prop_time(&p, 200000) == 13);
	CHECK(prop_time(&p, p.seg[1].pic0) == p.seg[1].prop0);
}

static void echo_exact(void)
{
	uint32_t rec[BITS], off = 0;
	uint64_t pic = 200000, at[BITS];
	int k, prev = 0;
	boot();
	prop_catch_up(&p, pic);
	for (k = 0; k < BITS; k++) {
		int bit = (k * 7 + 3) % 5 < 2;
		at[k] = pic;
		prop_pic_pins(&p, pic, CLK | (bit ? DATA : 0));
		CHECK(prop_p24(&p, pic + 1) == prev);
		CHECK(prop_p24(&p, pic + 100) == bit);
		prop_pic_pins(&p, pic + 200, bit ? DATA : 0);
		pic += 400;
		prev = bit;
	}
	prop_catch_up(&p, pic);
	CHECK(count() == BITS);
	for (k = 0; k < BITS; k++) {
		rec[k] = hub32(0x1000 + 4 * (uint32_t)k);
		if (k == 0) off = rec[0] - (uint32_t)prop_time(&p, at[0]);
		CHECK(rec[k] - (uint32_t)prop_time(&p, at[k]) == off);
	}
	boot();
	pic = 200000;
	for (k = 0; k < BITS; k++) pulse(&pic, (k * 7 + 3) % 5 < 2);
	prop_catch_up(&p, pic);
	CHECK(count() == BITS);
	for (k = 0; k < BITS; k++) CHECK(hub32(0x1000 + 4 * (uint32_t)k) == rec[k]);
}

static void clkset_lazy(void)
{
	static const int at[] = { 0, 10, 100, 999 };
	uint64_t pic = 60000;
	int k;
	boot();
	for (k = 0; k < 1000; k++) pulse(&pic, 0);
	CHECK(p.nseg == 1);
	for (k = 0; k < 4; k++) {
		prop_catch_up(&p, 60000 + 400 * (uint64_t)at[k] + 150);
		CHECK(count() == (uint32_t)at[k] + 1);
	}
	CHECK(p.nseg == 2);
}

static void full_log(void)
{
	uint64_t pic = 60000;
	int k;
	boot();
	for (k = 0; k < 3000; k++) pulse(&pic, k & 1);
	CHECK(p.count <= PROP_EDGES);
	prop_catch_up(&p, pic);
	CHECK(count() == 3000);
}

static void reset_rebases(int restart_at_zero)
{
	uint64_t now, pic = 200000;
	boot();
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	pulse(&pic, 0);
	prop_catch_up(&p, pic);
	CHECK(count() == 2);
	now = p.chip.now;
	if (restart_at_zero) pic = 0;
	prop_reset(&p, pic);
	CHECK(p.chip.now >= now);
	CHECK(prop_time(&p, pic) == p.chip.now);
	CHECK(prop_time(&p, pic + 1000) > prop_time(&p, pic));
	pic += 200000;
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	prop_catch_up(&p, pic);
	CHECK(count() == 1);
}

static void clkset_reset_bit(void)
{
	uint64_t pic = 200000;
	boot();
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	pulse(&pic, 0);
	prop_catch_up(&p, pic);
	CHECK(count() == 2);
	p.chip.bus.clkset(p.chip.bus.ctx, p.chip.now, 0x80);
	pic += 1000;
	prop_catch_up(&p, pic);
	CHECK(p.nseg == 1 && p.seg[0].num == 3);
	pic += 400000;
	prop_catch_up(&p, pic);
	CHECK(p.nseg == 2 && p.seg[1].num == 13);
	pulse(&pic, 1);
	prop_catch_up(&p, pic);
	CHECK(count() == 1);
}

static int load(const char *path, uint8_t *dst)
{
	FILE *f = fopen(path, "rb");
	size_t n;
	if (!f) { perror(path); return 0; }
	n = fread(dst, 1, 0x8000, f);
	fclose(f);
	return n == 0x8000;
}

int main(int argc, char **argv)
{
	if (argc != 3 || !load(argv[1], rom) || !load(argv[2], ram)) {
		fprintf(stderr, "usage: prop_test echo.rom echo.ram\n");
		return 2;
	}
	rates();
	echo_exact();
	clkset_lazy();
	full_log();
	reset_rebases(1);
	reset_rebases(0);
	clkset_reset_bit();
	printf("prop: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
