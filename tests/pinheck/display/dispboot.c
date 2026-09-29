#include "p8x32a.h"
#include "eeprom.h"
#include "sd.h"
#include "vfat.h"
#include "zipsrc.h"
#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static p8x32a chip;
static cat24m01 ee;
static uint8_t eemem[0x20000];
static sd_card sd;
static vfat vf;
static zipsrc zs;
static display disp;
static uint32_t sdbit = 1, eebits = 0x30000000u;
static int frames, configs, missing, uniform;
static long fb_addr = -1;
static uint8_t last[DISPLAY_FRAME];

static long find(const uint8_t *f)
{
	long a;
	for (a = 0; a + DISPLAY_FRAME <= 0x8000; a++)
		if (chip.hub[a] == f[0] && !memcmp(chip.hub + a, f, DISPLAY_FRAME)) return a;
	return -1;
}

static void on_frame(void *ctx, const uint8_t *f, uint64_t t)
{
	long a;
	int k;
	(void)ctx;
	frames++;
	memcpy(last, f, DISPLAY_FRAME);
	for (k = 1; k < DISPLAY_FRAME && f[k] == f[0]; k++) ;
	if (k == DISPLAY_FRAME) { uniform++; printf("frame %d at %llu: uniform %02x\n", frames, (unsigned long long)t, f[0]); return; }
	a = find(f);
	if (a < 0 || (fb_addr >= 0 && a != fb_addr)) missing++;
	if (fb_addr < 0) fb_addr = a;
	printf("frame %d at %llu: hub %s%04lx\n", frames, (unsigned long long)t, a < 0 ? "none " : "$", a < 0 ? 0L : a);
}

static void on_config(void *ctx, const uint8_t *b, int n, uint64_t t)
{
	int k;
	(void)ctx;
	configs++;
	printf("config at %llu:", (unsigned long long)t);
	for (k = 0; k < n; k++) printf(" %02x", b[k]);
	printf("\n");
}

static void on_log(void *ctx, const char *m) { (void)ctx; printf("%s\n", m); }

static uint32_t pins_in(void *ctx, uint64_t t)
{
	(void)ctx; (void)t;
	return (sdbit & 1u) | eebits;
}

static uint64_t pins_next(void *ctx, uint64_t t) { (void)ctx; (void)t; return P8X32A_NEVER; }

static void pins_out(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	int scl = (dir >> 28 & 1) ? (int)(out >> 28 & 1) : 1;
	int sda = (dir >> 29 & 1) ? (int)(out >> 29 & 1) : 1;
	(void)ctx;
	eebits = 0x10000000u | (uint32_t)cat24m01_update(&ee, scl, sda) << 29;
	sdbit = (uint32_t)sd_update(&sd, (dir >> 3 & 1) ? (int)(out >> 3 & 1) : 1, (dir >> 1 & 1) ? (int)(out >> 1 & 1) : 0, (dir >> 2 & 1) ? (int)(out >> 2 & 1) : 1);
	display_pins(&disp, t, out, dir);
}

static int sd_read(void *ctx, uint32_t lba, uint8_t *buf) { (void)ctx; return vfat_read(&vf, lba, buf); }

static int load(const char *path, uint8_t *dst, size_t max, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return 0; }
	*n = fread(dst, 1, max, f);
	fclose(f);
	return 1;
}

int main(int argc, char **argv)
{
	p8x32a_bus bus;
	sd_blockdev dev;
	const char *rom = NULL, *prp = NULL, *zip = NULL;
	unsigned long long cycles = 60000000ull, t;
	size_t n;
	int i, y, x;

	for (i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "-rom")) rom = argv[i + 1];
		else if (!strcmp(argv[i], "-prp")) prp = argv[i + 1];
		else if (!strcmp(argv[i], "-zip")) zip = argv[i + 1];
		else if (!strcmp(argv[i], "-cycles")) cycles = strtoull(argv[i + 1], NULL, 0);
	}
	if (!rom || !prp || !zip) { fprintf(stderr, "usage: dispboot -rom p8x32a.rom -prp PRP_V008.BIN -zip dominos.zip [-cycles n]\n"); return 2; }
	memset(&bus, 0, sizeof(bus));
	bus.pins_in = pins_in;
	bus.pins_next = pins_next;
	bus.pins_out = pins_out;
	p8x32a_init(&chip, &bus);
	if (!load(rom, chip.hub + 0x8000, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "dispboot: rom must be 32768 bytes\n"); return 2; }
	memset(eemem, 0xFF, sizeof(eemem));
	if (!load(prp, eemem, 0x8000, &n)) return 2;
	cat24m01_init(&ee, eemem, 0);
	if (zipsrc_open(&zs, zip, 64u << 20) || vfat_init(&vf, zipsrc_source(&zs))) { fprintf(stderr, "dispboot: cannot open %s\n", zip); return 2; }
	dev.ctx = NULL;
	dev.sectors = vfat_sectors(&vf);
	dev.read = sd_read;
	sd_init(&sd, &dev);
	display_init(&disp, NULL, on_frame, on_config, on_log);
	for (t = 4095; t < cycles; t += 4096) p8x32a_run_until(&chip, t);
	if (frames) {
		for (y = 0; y < DISPLAY_H; y++) {
			for (x = 0; x < DISPLAY_W; x++) putchar(last[y * DISPLAY_W + x] ? '#' : '.');
			putchar('\n');
		}
	}
	printf("dispboot: %d frames (%d uniform), %d config packets, framebuffer $%04lx, %d frames not in hub RAM\n", frames, uniform, configs, fb_addr < 0 ? 0L : fb_addr, missing);
	return frames - uniform > 0 && configs > 0 && missing == 0 ? 0 : 1;
}
