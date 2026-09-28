#include "Vdig.h"
#include "Vdig___024root.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "eeprom.h"
#include "sd.h"
#include "vfat.h"
#include "zipsrc.h"

static uint8_t eemem[0x20000];

static bool load(const char *path, uint8_t *dst, size_t max, size_t *got)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return false; }
	*got = fread(dst, 1, max, f);
	fclose(f);
	return true;
}

int main(int argc, char **argv)
{
	const char *rom = NULL, *ram = NULL, *eep = NULL, *dump = NULL;
	unsigned long long limit = 1000000;
	uint32_t ext = 0;
	const char *sdzip = NULL;
	unsigned long long at_t[64];
	uint32_t at_v[64];
	int nat = 0, ati = 0;
	int stop_cog = -1, halt = 0;
	uint32_t stop_ptr = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-rom") && i + 1 < argc) rom = argv[++i];
		else if (!strcmp(argv[i], "-ram") && i + 1 < argc) ram = argv[++i];
		else if (!strcmp(argv[i], "-eeprom") && i + 1 < argc) eep = argv[++i];
		else if (!strcmp(argv[i], "-dump") && i + 1 < argc) dump = argv[++i];
		else if (!strcmp(argv[i], "-cycles") && i + 1 < argc) limit = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-ext") && i + 1 < argc) ext = (uint32_t)strtoul(argv[++i], NULL, 16);
		else if (!strcmp(argv[i], "-halt")) halt = 1;
		else if (!strcmp(argv[i], "-sd") && i + 1 < argc) sdzip = argv[++i];
		else if (!strcmp(argv[i], "-extat") && i + 2 < argc && nat < 64) { at_t[nat] = strtoull(argv[i + 1], NULL, 0); at_v[nat++] = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else if (!strcmp(argv[i], "-stop") && i + 2 < argc) { stop_cog = atoi(argv[++i]); stop_ptr = (uint32_t)strtoul(argv[++i], NULL, 16); }
		else { fprintf(stderr, "usage: p1rtl -rom f [-ram f] [-eeprom f] [-ext hex] [-sd romset.zip] [-cycles n] [-stop cog ptrhex] [-dump f]\n"); return 2; }
	}
	if (!rom) { fprintf(stderr, "p1rtl: -rom is required\n"); return 2; }

	Vdig *top = new Vdig;
	Vdig___024root *r = top->rootp;
	static uint8_t buf[32768];
	size_t n;
	if (!load(rom, buf, 32768, &n) || n != 32768) { fprintf(stderr, "p1rtl: rom must be 32768 bytes\n"); return 2; }
	for (int w = 0; w < 4096; w++) {
		r->dig__DOT__hub___DOT__hub_mem___DOT__rom_low[w] = buf[w * 4] | buf[w * 4 + 1] << 8 | buf[w * 4 + 2] << 16 | (uint32_t)buf[w * 4 + 3] << 24;
		int h = 16384 + w * 4;
		r->dig__DOT__hub___DOT__hub_mem___DOT__rom_high[w] = buf[h] | buf[h + 1] << 8 | buf[h + 2] << 16 | (uint32_t)buf[h + 3] << 24;
	}
	memset(buf, 0, sizeof(buf));
	if (ram && !load(ram, buf, 32768, &n)) return 2;
	for (int w = 0; w < 8192; w++) {
		r->dig__DOT__hub___DOT__hub_mem___DOT__ram0[w] = buf[w * 4];
		r->dig__DOT__hub___DOT__hub_mem___DOT__ram1[w] = buf[w * 4 + 1];
		r->dig__DOT__hub___DOT__hub_mem___DOT__ram2[w] = buf[w * 4 + 2];
		r->dig__DOT__hub___DOT__hub_mem___DOT__ram3[w] = buf[w * 4 + 3];
	}
	cat24m01 ee;
	if (eep) {
		memset(eemem, 0xFF, sizeof(eemem));
		if (!load(eep, eemem, sizeof(eemem), &n)) return 2;
		cat24m01_init(&ee, eemem, 0);
	}

	static zipsrc zs;
	static vfat vf;
	static sd_card sd;
	int sdbit = 1;
	if (sdzip) {
		sd_blockdev dev;
		if (zipsrc_open(&zs, sdzip, 64u << 20) || vfat_init(&vf, zipsrc_source(&zs))) { fprintf(stderr, "p1rtl: cannot open romset %s\n", sdzip); return 2; }
		dev.ctx = &vf;
		dev.sectors = vfat_sectors(&vf);
		dev.read = [](void *ctx, uint32_t lba, uint8_t *b) { return vfat_read((vfat *)ctx, lba, b); };
		sd_init(&sd, &dev);
	}
	top->nres = 0;
	top->pin_in = 0;
	for (int i = 0; i < 4; i++) {
		top->clk_cog = 1; top->clk_pll = 1; top->eval();
		top->clk_pll = 0; top->eval();
		top->clk_cog = 0; top->clk_pll = 1; top->eval();
		top->clk_pll = 0; top->eval();
	}
	top->nres = 1;
	top->eval();

	uint32_t last_out = ~0u, last_dir = ~0u;
	int last_cfg = -1;
	unsigned long long cyc;
	for (cyc = 0; cyc < limit; cyc++) {
		uint32_t out = top->pin_out, dir = top->pin_dir;
		while (ati < nat && at_t[ati] <= cyc) ext = at_v[ati++];
		uint32_t x = ext;
		if (eep) {
			int scl = (dir >> 28 & 1) ? (out >> 28 & 1) : 1;
			int sda = (dir >> 29 & 1) ? (out >> 29 & 1) : 1;
			int drv = cat24m01_update(&ee, scl, sda);
			x = (x & ~0x30000000u) | 0x10000000u | (uint32_t)drv << 29;
		}
		if (sdzip) {
			int cs = (dir >> 3 & 1) ? (out >> 3 & 1) : 1;
			int sclk = (dir >> 1 & 1) ? (out >> 1 & 1) : 0;
			int mosi = (dir >> 2 & 1) ? (out >> 2 & 1) : 1;
			sdbit = sd_update(&sd, cs, sclk, mosi);
			x = (x & ~1u) | (uint32_t)sdbit;
		}
		top->pin_in = (dir & out) | (~dir & x);
		if (out != last_out || dir != last_dir) { printf("P %llu %08x %08x\n", cyc, out, dir); last_out = out; last_dir = dir; }
		if (top->cfg != last_cfg) { printf("K %llu %02x\n", cyc, top->cfg); last_cfg = top->cfg; }
		top->eval();
		int stopping = 0;
		if (r->dig__DOT__ena_bus && r->dig__DOT__ptr_w) {
			uint32_t ptr = r->dig__DOT__hub___DOT__dc >> 4;
			for (int c = 0; c < 8; c++)
				if (r->dig__DOT__ptr_w >> c & 1) {
					printf("S %llu %d %07x\n", cyc, c, ptr);
					if (c == stop_cog && ptr == stop_ptr) stopping = 1;
				}
		}
		top->clk_cog = 1; top->clk_pll = 1; top->eval();
		top->clk_pll = 0; top->eval();
		top->clk_cog = 0; top->clk_pll = 1; top->eval();
		top->clk_pll = 0; top->eval();
		if (stopping) { cyc++; break; }
		if (halt && cyc >= 1 && r->dig__DOT__cog_ena == 0) { cyc++; break; }
	}
	printf("E %llu\n", cyc);
	if (dump) {
		FILE *f = fopen(dump, "wb");
		if (!f) { perror(dump); return 2; }
		for (int w = 0; w < 8192; w++) {
			fputc(r->dig__DOT__hub___DOT__hub_mem___DOT__ram0[w], f);
			fputc(r->dig__DOT__hub___DOT__hub_mem___DOT__ram1[w], f);
			fputc(r->dig__DOT__hub___DOT__hub_mem___DOT__ram2[w], f);
			fputc(r->dig__DOT__hub___DOT__hub_mem___DOT__ram3[w], f);
		}
		fclose(f);
	}
	delete top;
	return 0;
}
