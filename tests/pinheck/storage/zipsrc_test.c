#include "zipsrc.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("ZIPSRC FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t pat(uint32_t i) { return (uint8_t)(i * 7 + 3); }

static int find(zipsrc *z, const char *name)
{
	const vfat_source *s = zipsrc_source(z);
	int k;
	for (k = 0; k < s->count; k++)
		if (!strcmp(s->name(s->ctx, k), name)) return k;
	return -1;
}

static int same(const uint8_t *b, uint32_t off, uint32_t n)
{
	uint32_t k;
	for (k = 0; k < n; k++)
		if (b[k] != pat(off + k)) return 0;
	return 1;
}

int main(int argc, char **argv)
{
	zipsrc z;
	const vfat_source *s;
	uint8_t b[512];
	int aa0, ab0, zms, a01, dq;

	CHECK(zipsrc_open(&z, "does-not-exist.zip", 50000) != 0);
	if (argc != 2 || zipsrc_open(&z, argv[1], 50000)) { printf("zipsrc: cannot open test zip\n"); return 1; }
	s = zipsrc_source(&z);
	CHECK(s->count == 12);
	aa0 = find(&z, "DMD/_DA/AA0.VID");
	ab0 = find(&z, "DMD/_DA/AB0.VID");
	zms = find(&z, "DMD/_DZ/ZMS.spr");
	a01 = find(&z, "SFX/_FA/A01.wav");
	dq = find(&z, "DMD/_DQ/");
	CHECK(aa0 >= 0 && ab0 >= 0 && zms >= 0 && a01 >= 0 && dq >= 0);
	CHECK(s->size(s->ctx, dq) == 0);
	CHECK(s->size(s->ctx, aa0) == 40000);
	CHECK(s->read(s->ctx, aa0, 39000, b, 512) == 0 && same(b, 39000, 512));
	CHECK(s->read(s->ctx, ab0, 0, b, 512) == 0 && same(b, 0, 512));
	CHECK(s->read(s->ctx, zms, 500, b, 500) == 0 && same(b, 500, 500));
	CHECK(z.cache_used == 41000);
	CHECK(s->read(s->ctx, a01, 69990, b, 10) == 0 && same(b, 69990, 10));
	CHECK(z.cache_used == 70000 && !z.e[aa0].data && !z.e[zms].data);
	CHECK(s->read(s->ctx, zms, 0, b, 512) == 0 && same(b, 0, 512));
	CHECK(z.cache_used == 1000 && !z.e[a01].data);
	CHECK(s->read(s->ctx, aa0, 39900, b, 101) != 0);
	CHECK(s->read(s->ctx, aa0, 0, b, 512) == 0 && same(b, 0, 512));
	zipsrc_close(&z);
	printf("zipsrc: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
