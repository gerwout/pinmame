#include "p8x32a.h"
#include "eeprom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static p8x32a chip;
static cat24m01 ee;
static uint8_t eemem[0x20000];
static int have_ee;
static uint32_t ext, eebits;
static uint64_t at_t[64];
static uint32_t at_v[64];
static int nat;
static int stop_cog = -1;
static uint32_t stop_ptr;
static uint64_t stop_at = P8X32A_NEVER;
static uint64_t known_to = P8X32A_NEVER;
static struct ev { uint64_t t; size_t seq; char line[48]; } *evs;
static size_t nev, cap;

static void emit(uint64_t t, const char *line)
{
	if (nev == cap) {
		cap = cap ? cap * 2 : 4096;
		evs = realloc(evs, cap * sizeof(*evs));
		if (!evs) { fprintf(stderr, "p8run: out of memory\n"); exit(2); }
	}
	evs[nev].t = t;
	evs[nev].seq = nev;
	strcpy(evs[nev].line, line);
	nev++;
}

static uint32_t pins_in(void *ctx, uint64_t t)
{
	uint32_t v = ext;
	int k;
	(void)ctx;
	for (k = 0; k < nat && at_t[k] <= t; k++) v = at_v[k];
	return have_ee ? (v & ~0x30000000u) | eebits : v;
}

static uint64_t pins_next(void *ctx, uint64_t t)
{
	int k;
	(void)ctx;
	for (k = 0; k < nat; k++)
		if (at_t[k] > t) return at_t[k] <= known_to ? at_t[k] : P8X32A_NEVER;
	return P8X32A_NEVER;
}

static int rank(const struct ev *e) { return e->line[0] == 'P' ? 0 : e->line[0] == 'K' ? 1 : 2; }

static int evcmp(const void *a, const void *b)
{
	const struct ev *x = a, *y = b;
	if (x->t != y->t) return x->t < y->t ? -1 : 1;
	if (rank(x) != rank(y)) return rank(x) - rank(y);
	return x->seq < y->seq ? -1 : 1;
}

static void pins_out(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx;
	if (have_ee) {
		int scl = (dir >> 28 & 1) ? (int)(out >> 28 & 1) : 1;
		int sda = (dir >> 29 & 1) ? (int)(out >> 29 & 1) : 1;
		int drv = cat24m01_update(&ee, scl, sda);
		eebits = 0x10000000u | (uint32_t)drv << 29;
	}
	char b[48];
	sprintf(b, "P %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
	emit(t, b);
}

static void cog_start(void *ctx, uint64_t t, int cog, uint32_t ptr)
{
	(void)ctx;
	char b[48];
	sprintf(b, "S %llu %d %07x", (unsigned long long)t, cog, (unsigned)ptr);
	emit(t, b);
	if (cog == stop_cog && ptr == stop_ptr) { stop_at = t; chip.stop = 1; }
}

static void clkset(void *ctx, uint64_t t, uint8_t cfg)
{
	(void)ctx;
	char b[48];
	sprintf(b, "K %llu %02x", (unsigned long long)t, cfg);
	emit(t, b);
}

static void logmsg(void *ctx, const char *msg)
{
	(void)ctx;
	fprintf(stderr, "%s\n", msg);
}

static int load(const char *path, uint8_t *dst, size_t max, size_t *got)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return 0; }
	*got = fread(dst, 1, max, f);
	fclose(f);
	return 1;
}

static uint64_t halted(void)
{
	uint64_t last = 0;
	int n;
	for (n = 0; n < 8; n++) {
		const p8x32a_cog *c = &chip.cog[n];
		if (c->ev != 0) return P8X32A_NEVER;
		if (c->disable_at != P8X32A_NEVER && c->disable_at > last) last = c->disable_at;
	}
	return last;
}

int main(int argc, char **argv)
{
	p8x32a_bus bus = { NULL, pins_in, pins_next, pins_out, cog_start, clkset, logmsg };
	const char *rom = NULL, *ram = NULL, *eep = NULL, *dump = NULL;
	unsigned long long limit = 1000000, t, end, quantum = 4096;
	int halt = 0, i;
	size_t n;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-rom") && i + 1 < argc) rom = argv[++i];
		else if (!strcmp(argv[i], "-ram") && i + 1 < argc) ram = argv[++i];
		else if (!strcmp(argv[i], "-eeprom") && i + 1 < argc) eep = argv[++i];
		else if (!strcmp(argv[i], "-dump") && i + 1 < argc) dump = argv[++i];
		else if (!strcmp(argv[i], "-cycles") && i + 1 < argc) limit = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-ext") && i + 1 < argc) ext = (uint32_t)strtoul(argv[++i], NULL, 16);
		else if (!strcmp(argv[i], "-stop") && i + 2 < argc) { stop_cog = atoi(argv[i + 1]); stop_ptr = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else if (!strcmp(argv[i], "-halt")) halt = 1;
		else if (!strcmp(argv[i], "-quantum") && i + 1 < argc) quantum = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-extat") && i + 2 < argc && nat < 64) { at_t[nat] = strtoull(argv[i + 1], NULL, 0); at_v[nat++] = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else { fprintf(stderr, "usage: p8run -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-halt] [-quantum n] [-extat cycle hex]... [-dump f]\n"); return 2; }
	}
	if (!rom) { fprintf(stderr, "p8run: -rom is required\n"); return 2; }
	p8x32a_init(&chip, &bus);
	if (!load(rom, chip.hub + 0x8000, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "p8run: rom must be 32768 bytes\n"); return 2; }
	if (ram && !load(ram, chip.hub, 0x8000, &n)) return 2;
	if (eep) {
		memset(eemem, 0xFF, sizeof(eemem));
		if (!load(eep, eemem, sizeof(eemem), &n)) return 2;
		cat24m01_init(&ee, eemem, 0);
		have_ee = 1;
		eebits = 0x30000000u;
	}
	printf("P 0 00000000 00000000\nK 0 00\n");
	end = limit;
	for (t = 0; t < limit; t += quantum) {
		unsigned long long to = t + quantum - 1 < limit - 1 ? t + quantum - 1 : limit - 1;
		uint64_t h;
		if (quantum != 4096) known_to = to;
		p8x32a_run_until(&chip, to);
		if (chip.stop) { end = stop_at + 1; break; }
		if (halt && (h = halted()) != P8X32A_NEVER && h >= 2) { end = h; break; }
	}
	{
		size_t k;
		qsort(evs, nev, sizeof(*evs), evcmp);
		for (k = 0; k < nev; k++)
			if (evs[k].t < end) printf("%s\n", evs[k].line);
	}
	printf("E %llu\n", end);
	if (dump) {
		FILE *f = fopen(dump, "wb");
		if (!f) { perror(dump); return 2; }
		fwrite(chip.hub, 1, 0x8000, f);
		fclose(f);
	}
	return 0;
}
