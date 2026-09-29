#include "pic32mx.h"
#include "../../../src/wpc/pinheck/prop.h"
#include "../../../src/wpc/pinheck/rtc.h"
#include "../../../src/wpc/pinheck/bootldr.h"
#include "../../../src/wpc/pinheck/sd.h"
#include "../../../src/wpc/pinheck/zipsrc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RF5  (1u << 5)
#define RF12 (1u << 12)
#define RF13 (1u << 13)

static pic32mx soc;
static pinheck_prop prop;
static cat24m01 u13;
static ds1340 rtc;
static pic32_boot boot;
static uint64_t rtc_at;
static uint8_t u13mem[131072], propmem[131072], rom[0x8000];
static uint8_t flash[PIC32MX_FLASH_SIZE], prgcopy[PIC32MX_FLASH_SIZE];
static int verbose;
static uint64_t boot_at, first_tx;
static zipsrc zip;
static vfat vol;
static sd_card card;

static int blk_read(void *ctx, uint32_t lba, uint8_t *buf) { (void)ctx; return vfat_read(&vol, lba, buf); }
static int spi(void *ctx, int cs, int sclk, int mosi) { (void)ctx; return sd_update(&card, cs, sclk, mosi); }
static void boot_tx(void *ctx, uint64_t t, int level) { (void)ctx; prop_pic_pins(&prop, t, level ? 1u << 24 : 0); }
static void prop_tx(void *ctx, uint64_t t, int level) { (void)ctx; boot_rx(&boot, t, level); }

static uint64_t hold(void *ctx, uint64_t cycle)
{
	(void)ctx;
	prop_catch_up(&prop, cycle);
	boot_advance(&boot, cycle);
	return boot_hold(&boot, cycle);
}

static void port_write(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle)
{
	uint32_t drv = lat & ~tris;
	(void)ctx;
	if (port == PIC32MX_PORTF)
		prop_pic_pins(&prop, cycle, (drv & RF12 ? 1u << 25 : 0) | (drv & RF5 ? 1u << 26 : 0));
}

static uint32_t port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx;
	if (port != PIC32MX_PORTF) return 0xFFFFu;
	return (0xFFFFu & ~RF13) | (prop_p24(&prop, cycle) ? RF13 : 0);
}

static void uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
{
	(void)ctx;
	if (uart == 1) fputc(byte, stdout);
	if (uart == 1 && !first_tx) first_tx = cycle;
}

static int i2c_pins(void *ctx, int module, int scl, int sda, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (module != 1) return sda;
	ds1340_tick(&rtc, cycle - rtc_at);
	rtc_at = cycle;
	return cat24m01_update(&u13, scl, sda) & ds1340_update(&rtc, scl, sda);
}

static void exception(void *ctx, int code, uint32_t pc)
{
	(void)ctx;
	fprintf(stderr, "exception %d at %08x\n", code, (unsigned)pc);
}

static void proplog(void *ctx, const char *msg)
{
	(void)ctx;
	if (verbose) fprintf(stderr, "%s\n", msg);
}

static int load(const char *path, uint8_t *dst, size_t max, size_t *got)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return 0; }
	*got = fread(dst, 1, max, f);
	fclose(f);
	return 1;
}

int main(int argc, char **argv)
{
	pic32mx_board board = { NULL, port_write, port_read, uart_tx, i2c_pins, NULL, exception, hold };
	unsigned long long cycles = 800000000ull, first = 0, send_at = 0;
	const char *path = NULL, *rompath = NULL, *prp = NULL, *send = NULL, *zippath = NULL;
	int boots = 1, b, i, inservice = -1, updatecode = 0, stale = 0;
	size_t n, m;
	clock_t t0 = clock();

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) verbose = 1;
		else if (!strcmp(argv[i], "-c") && i + 1 < argc) cycles = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-first") && i + 1 < argc) first = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-boots") && i + 1 < argc) boots = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-rom") && i + 1 < argc) rompath = argv[++i];
		else if (!strcmp(argv[i], "-prp") && i + 1 < argc) prp = argv[++i];
		else if (!strcmp(argv[i], "-zip") && i + 1 < argc) zippath = argv[++i];
		else if (!strcmp(argv[i], "-inservice") && i + 1 < argc) inservice = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-updatecode")) updatecode = 1;
		else if (!strcmp(argv[i], "-stale")) stale = 1;
		else if (!strcmp(argv[i], "-send") && i + 2 < argc) { send_at = strtoull(argv[++i], NULL, 0); send = argv[++i]; }
		else path = argv[i];
	}
	if (!path || !rompath || !prp) {
		fprintf(stderr, "usage: linkmachine -rom p8x32a.rom -prp PRP_V008.BIN [-zip dominos.zip] [-inservice version] [-updatecode] [-stale] [-v] [-c cycles] [-first cycles] [-boots n] [-send cycle text] DOM_V006.PRG\n");
		return 2;
	}
	memset(propmem, 0xFF, sizeof(propmem));
	memset(u13mem, 0xFF, sizeof(u13mem));
	if (!load(rompath, rom, sizeof(rom), &n) || n != sizeof(rom)) { fprintf(stderr, "rom must be 32768 bytes\n"); return 2; }
	if (!load(prp, propmem, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "PRP image must be 32768 bytes\n"); return 2; }
	memset(flash, 0xFF, sizeof(flash));
	if (!load(path, flash, sizeof(flash), &m)) return 2;
	memcpy(prgcopy, flash, sizeof(flash));
	if (inservice >= 0) {
		uint32_t w0 = (uint32_t)inservice << 24 | 0xBAFAu, w1 = 0xABBA0002u;
		if (updatecode) w1 = 0xABBA0001u;
		for (i = 0; i < 4; i++) { propmem[0x8000 + i] = (uint8_t)(w0 >> (8 * i)); propmem[0x8004 + i] = (uint8_t)(w1 >> (8 * i)); }
	}
	if (stale) for (i = 0x1000; i < (int)m; i += 0x1000) flash[i] ^= 0xFF;
	pic32mx_init(&soc, &board, flash, (uint32_t)m);
	prop_init(&prop, rom, propmem);
	prop_set_log(&prop, proplog, NULL);
	prop_set_tx(&prop, prop_tx, NULL);
	boot_init(&boot, flash, sizeof(flash), boot_tx, NULL);
	boot_set_log(&boot, proplog, NULL);
	if (zippath) {
		sd_blockdev dev;
		if (zipsrc_open(&zip, zippath, 64u << 20) != 0 || vfat_init(&vol, zipsrc_source(&zip)) != 0) { fprintf(stderr, "%s: cannot build the SD volume\n", zippath); return 2; }
		dev.ctx = NULL;
		dev.sectors = vfat_sectors(&vol);
		dev.read = blk_read;
		sd_init(&card, &dev);
		prop_attach_sd(&prop, spi, NULL);
	}
	for (b = 1; b <= boots; b++) {
		unsigned long long done = 0;
		const char *s = b == boots ? send : NULL;
		unsigned long long limit = b == 1 && first ? first : cycles;
		printf("=== BOOT %d ===\n", b);
		if (b > 1) { pic32mx_reset(&soc); prop_reset(&prop, soc.cpu.cycles); }
		boot_reset(&boot, soc.cpu.cycles);
		boot_at = soc.cpu.cycles;
		first_tx = 0;
		cat24m01_init(&u13, u13mem, 0);
		ds1340_init(&rtc, 1483228800, 80000000u);
		rtc_at = soc.cpu.cycles;
		while (done < limit) {
			done += (unsigned long long)pic32mx_run(&soc, 100000);
			prop_catch_up(&prop, soc.cpu.cycles);
			if (s && *s && done >= send_at) {
				int k = soc.uart[0].rx_count;
				pic32mx_uart_rx(&soc, 0, (uint8_t)*s);
				if (soc.uart[0].rx_count > k) s++;
			}
		}
		printf("\n");
		if (verbose) fprintf(stderr, "boot %d: first UART1 byte at %.3fs\n", b, first_tx ? (double)(first_tx - boot_at) / 80e6 : -1.0);
		if (verbose && boot.app_at) fprintf(stderr, "boot %d: bootloader released the application at %.3fs\n", b, (double)(boot.app_at - boot_at) / 80e6);
	}
	fflush(stdout);
	if (verbose) {
		double secs = (double)(clock() - t0) / CLOCKS_PER_SEC, emu = (double)(soc.cpu.cycles) / 80e6;
		fprintf(stderr, "boot: frames=%lu programmed=%lu read=%lu errors=%lu flash_matches_prg=%d\n", boot.frames, boot.programmed, boot.read, boot.errors, !memcmp(flash, prgcopy, m));
		fprintf(stderr, "eeprom: $8000=%08x $8004=%08x\n",
			(unsigned)(propmem[0x8000] | propmem[0x8001] << 8 | propmem[0x8002] << 16 | (uint32_t)propmem[0x8003] << 24),
			(unsigned)(propmem[0x8004] | propmem[0x8005] << 8 | propmem[0x8006] << 16 | (uint32_t)propmem[0x8007] << 24));
		fprintf(stderr, "exceptions=%llu wall=%.1fs emulated=%.1fs speed=%.2fx\n", (unsigned long long)soc.exc_count, secs, emu, emu / secs);
	}
	return soc.exc_count ? 1 : 0;
}
