#include "p8x32a.h"
#ifdef P8X32A_JIT
#include "p8x32ajit.h"
#endif
#include "eeprom.h"
#include "sd.h"
#include "vfat.h"
#include "zipsrc.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static p8x32a chip;
static cat24m01 ee;
static uint8_t eemem[0x20000];
static int have_ee, have_sd, notrace, sleeps, nolazy, lazies;
static sd_card sd;
static vfat vf;
static zipsrc zs;
static uint32_t sdbit = 1;
static uint64_t cur_t;
static uint32_t ext, eebits;
static uint64_t at_t[8192];
static uint32_t at_v[8192];
static int nat;
static int stop_cog = -1;
static uint32_t stop_ptr;
static uint64_t stop_at = P8X32A_NEVER;
static uint64_t known_to = P8X32A_NEVER;
static uint64_t clkshift, clk_at;
static struct ev { uint64_t t; size_t seq; char line[96]; } *evs;
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
	if (have_sd) v = (v & ~1u) | sdbit;
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

/* N, L, M: the core's pin changes outside a lazy cog's pins, that cog's pin changes, its pins from then on; merged into P */
static int rank(const struct ev *e) { return strchr("PNLM", e->line[0]) ? 0 : e->line[0] == 'K' ? 1 : 2; }

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
	cur_t = t;
	if (have_sd) {
		int cs = (dir >> 3 & 1) ? (int)(out >> 3 & 1) : 1;
		int sclk = (dir >> 1 & 1) ? (int)(out >> 1 & 1) : 0;
		int mosi = (dir >> 2 & 1) ? (int)(out >> 2 & 1) : 1;
		sdbit = (uint32_t)sd_update(&sd, cs, sclk, mosi);
	}
	if (notrace) return;
	{
		char b[48];
		sprintf(b, "N %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
		emit(t, b);
	}
}

static void lazy(void *ctx, uint64_t t, uint32_t mask, uint32_t out, uint32_t dir)
{
	char b[64];
	(void)ctx;
	if (notrace) return;
	sprintf(b, "M %llu %08x %08x %08x", (unsigned long long)t, (unsigned)mask, (unsigned)out, (unsigned)dir);
	emit(t, b);
}

static void lazy_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	char b[48];
	(void)ctx;
	/* a host retimes at a CLKSET: the pin changes before it come first */
	if (t < clk_at) fprintf(stderr, "p8run: lazy pin change at %llu after a CLKSET at %llu\n", (unsigned long long)t, (unsigned long long)clk_at);
	if (notrace) return;
	sprintf(b, "L %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
	emit(t, b);
}

static void node_path(int n, char *out)
{
	char part[13];
	int k, j;
	out[0] = 0;
	if (n <= 0) return;
	node_path(vf.node[n].parent, out);
	for (k = 0, j = 0; k < 8 && vf.node[n].name[k] != ' '; k++) part[j++] = vf.node[n].name[k];
	if (vf.node[n].name[8] != ' ') {
		part[j++] = '.';
		for (k = 8; k < 11 && vf.node[n].name[k] != ' '; k++) part[j++] = vf.node[n].name[k];
	}
	part[j] = 0;
	if (out[0]) strcat(out, "/");
	strcat(out, part);
}

static void sector_label(uint32_t lba, char *out)
{
	uint32_t r;
	if (lba < vf.part_start) { strcpy(out, lba ? "GAP" : "MBR"); return; }
	r = lba - vf.part_start;
	if (r < 32) { strcpy(out, r == 0 || r == 6 ? "BOOT" : r == 1 || r == 7 ? "FSINFO" : "RESERVED"); return; }
	if (r < vf.data_start) { sprintf(out, "FAT+%u", (unsigned)(r - 32)); return; }
	{
		uint32_t cl = 2 + (r - vf.data_start) / VFAT_SPC;
		int k;
		for (k = 0; k < vf.nalloc; k++) {
			vfat_node *n = &vf.node[vf.alloc[k]];
			if (cl >= n->first && cl < n->first + n->nclus) {
				char path[64];
				node_path(vf.alloc[k], path);
				sprintf(out, "%s:%s+%u", n->is_dir ? "DIR" : "FILE", path[0] ? path : "/",
				        (unsigned)(((cl - n->first) * VFAT_SPC + (r - vf.data_start) % VFAT_SPC) * 512));
				return;
			}
		}
	}
	strcpy(out, "FREE");
}

static int sd_read(void *ctx, uint32_t lba, uint8_t *buf)
{
	char line[96], label[72];
	int r;
	(void)ctx;
	r = vfat_read(&vf, lba, buf);
	sector_label(lba, label);
	sprintf(line, "D %llu %u %08lx %s", (unsigned long long)cur_t, (unsigned)lba, crc32(0L, buf, 512), label);
	emit(cur_t, line);
	return r;
}

static void cog_start(void *ctx, uint64_t t, int cog, uint32_t ptr)
{
	(void)ctx;
	char b[48];
	sprintf(b, "S %llu %d %07x", (unsigned long long)t, cog, (unsigned)ptr);
	emit(t, b);
	if (cog == stop_cog && ptr == stop_ptr) { stop_at = t; chip.stop = 1; }
}

static FILE *ctrlog;

static void ctr_state(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq)
{
	(void)ctx;
	if (ctrlog) fprintf(ctrlog, "C %llu %d %d %08x %08x\n", (unsigned long long)t, cog, ctr, (unsigned)ctr_reg, (unsigned)frq);
}

/* -clkshift d: like prop.c's retime, a CLKSET moves queued -extat edges d cycles earlier (not before t + 1) */
static void clkset(void *ctx, uint64_t t, uint8_t cfg)
{
	char b[48];
	int k;
	(void)ctx;
	clk_at = t;
	for (k = 0; k < nat; k++)
		if (at_t[k] > t + 1) at_t[k] = at_t[k] - clkshift > t + 1 ? at_t[k] - clkshift : t + 1;
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
	p8x32a_bus bus = { NULL, pins_in, pins_next, pins_out, cog_start, clkset, logmsg, ctr_state, ~0x30000001u, lazy, lazy_pins };
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
		else if (!strcmp(argv[i], "-notrace")) notrace = 1;
		else if (!strcmp(argv[i], "-nolazy")) nolazy = 1;
		else if (!strcmp(argv[i], "-sleeps")) sleeps = 1;
		else if (!strcmp(argv[i], "-lazies")) lazies = 1;
		else if (!strcmp(argv[i], "-ctrlog") && i + 1 < argc) { if (!(ctrlog = fopen(argv[++i], "w"))) { perror(argv[i]); return 2; } }
		else if (!strcmp(argv[i], "-uart") && i + 2 < argc) {
			uint64_t t0 = strtoull(argv[i + 1], NULL, 0);
			size_t nb = strlen(argv[i + 2]) / 2, k;
			int b;
			for (k = 0; k < nb && nat + 11 <= 8192; k++) {
				char hx[3];
				uint32_t byte;
				hx[0] = argv[i + 2][2 * k]; hx[1] = argv[i + 2][2 * k + 1]; hx[2] = 0;
				byte = (uint32_t)strtoul(hx, NULL, 16) | 0x100u;
				at_t[nat] = t0; at_v[nat++] = ext & ~(1u << 24);
				for (b = 0; b < 9; b++) { at_t[nat] = t0 + 903 * (uint64_t)(b + 1); at_v[nat++] = (ext & ~(1u << 24)) | ((byte >> b) & 1) << 24; }
				t0 += 903 * 11;
			}
			i += 2;
		}
		else if (!strcmp(argv[i], "-sd") && i + 1 < argc) {
			sd_blockdev dev;
			if (zipsrc_open(&zs, argv[++i], 64u << 20) || vfat_init(&vf, zipsrc_source(&zs))) { fprintf(stderr, "p8run: cannot open romset %s\n", argv[i]); return 2; }
			dev.ctx = NULL;
			dev.sectors = vfat_sectors(&vf);
			dev.read = sd_read;
			sd_init(&sd, &dev);
			have_sd = 1;
		}
		else if (!strcmp(argv[i], "-quantum") && i + 1 < argc) quantum = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-clkshift") && i + 1 < argc) clkshift = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-extat") && i + 2 < argc && nat < 8192) { at_t[nat] = strtoull(argv[i + 1], NULL, 0); at_v[nat++] = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else { fprintf(stderr, "usage: p8run -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-halt] [-quantum n] [-sd romset.zip] [-notrace] [-sleeps] [-ctrlog f] [-uart cycle hexbytes] [-extat cycle hex]... [-clkshift n] [-dump f]\n"); return 2; }
	}
	if (!rom) { fprintf(stderr, "p8run: -rom is required\n"); return 2; }
	p8x32a_init(&chip, &bus);
#ifdef P8X32A_JIT
	if ((chip.jit = p8x32a_jit_new()) != NULL) chip.jit_build = p8x32a_jit_build;
#endif
	if (!load(rom, chip.hub + 0x8000, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "p8run: rom must be 32768 bytes\n"); return 2; }
	/* a cog may run lazily on any pin no device here watches */
	chip.lazy_ok = nolazy ? 0 : ~((have_ee ? 0x30000000u : 0) | (have_sd ? 0xFu : 0));
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
		uint32_t no = 0, nd = 0, lm = 0, lo = 0, ld = 0, po = 0, pd = 0;
		qsort(evs, nev, sizeof(*evs), evcmp);
		for (k = 0; k < nev; k++) {
			const char *l = evs[k].line;
			unsigned long long t;
			unsigned a, b, c;
			if (evs[k].t >= end) continue;
			if (!strchr("NLM", l[0])) { printf("%s\n", l); continue; }
			if (l[0] == 'N') { sscanf(l + 2, "%llu %x %x", &t, &a, &b); no = a; nd = b; }
			else if (l[0] == 'L') { sscanf(l + 2, "%llu %x %x", &t, &a, &b); lo = a; ld = b; }
			else {
				/* a lazy cog's pins go back to the core's trace as they last were */
				sscanf(l + 2, "%llu %x %x %x", &t, &a, &b, &c);
				no = (no & ~lm) | lo;
				nd = (nd & ~lm) | ld;
				lm = a;
				lo = b;
				ld = c;
			}
			/* one P line for the pins at the end of each cycle that changes them */
			if (k + 1 < nev && evs[k + 1].t == evs[k].t && strchr("NLM", evs[k + 1].line[0])) continue;
			{
				uint32_t o = (no & ~lm) | lo, d = (nd & ~lm) | ld;
				if (o != po || d != pd) printf("P %llu %08x %08x\n", (unsigned long long)evs[k].t, (unsigned)o, (unsigned)d);
				po = o;
				pd = d;
			}
		}
	}
	printf("E %llu\n", end);
	if (sleeps) fprintf(stderr, "p8run: %llu idle-loop sleeps\n", (unsigned long long)chip.sleeps);
	if (lazies) fprintf(stderr, "p8run: %llu lazy cogs\n", (unsigned long long)chip.lazies);
	if (dump) {
		FILE *f = fopen(dump, "wb");
		if (!f) { perror(dump); return 2; }
		fwrite(chip.hub, 1, 0x8000, f);
		fclose(f);
	}
	return 0;
}
