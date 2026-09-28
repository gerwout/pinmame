#include "pic32mx.h"
#include "linkstub.h"
#include "../../../src/wpc/pinheck/eeprom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pic32mx soc;
static linkstub link;
static cat24m01 u13;
static uint8_t u13mem[131072];
static uint8_t flash[PIC32MX_FLASH_SIZE];
static int verbose;

static void port_write(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle)
{
	(void)ctx; (void)tris; (void)cycle;
	if (port == PIC32MX_PORTF) linkstub_portf_write(&link, lat);
}

static uint32_t port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	return port == PIC32MX_PORTF ? linkstub_portf_read(&link) : 0xFFFFu;
}

static void uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (uart == 1) fputc(byte, stdout);
}

static int i2c_pins(void *ctx, int module, int scl, int sda, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	return module == 1 ? cat24m01_update(&u13, scl, sda) : sda;
}

static void unmapped(void *ctx, uint32_t pa, int write)
{
	(void)ctx;
	if (verbose) fprintf(stderr, "unmodelled SFR %s %08x\n", write ? "write" : "read", (unsigned)(pa | 0xA0000000u));
}

static void exception(void *ctx, int code, uint32_t pc)
{
	(void)ctx;
	fprintf(stderr, "exception %d at %08x\n", code, (unsigned)pc);
}

int main(int argc, char **argv)
{
	pic32mx_board board = { NULL, port_write, port_read, uart_tx, i2c_pins, unmapped, exception };
	unsigned long long cycles = 800000000ull;
	int boots = 1, b, i;
	const char *path = NULL;
	FILE *f;
	size_t n;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) verbose = 1;
		else if (!strcmp(argv[i], "-c") && i + 1 < argc) cycles = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-boots") && i + 1 < argc) boots = atoi(argv[++i]);
		else path = argv[i];
	}
	if (!path) { fprintf(stderr, "usage: pic32boot [-v] [-c cycles] [-boots n] DOM_V006.PRG\n"); return 2; }
	if (!(f = fopen(path, "rb"))) { perror(path); return 2; }
	n = fread(flash, 1, sizeof(flash), f);
	fclose(f);
	memset(u13mem, 0xFF, sizeof(u13mem));
	linkstub_init(&link);
	pic32mx_init(&soc, &board, flash, (uint32_t)n);
	for (b = 1; b <= boots; b++) {
		unsigned long long done = 0;
		printf("=== BOOT %d ===\n", b);
		if (b > 1) pic32mx_reset(&soc);
		linkstub_reset_link(&link);
		cat24m01_init(&u13, u13mem, 0);
		while (done < cycles) done += (unsigned long long)pic32mx_run(&soc, 1000000);
		printf("\n");
	}
	fflush(stdout);
	if (verbose) {
		fprintf(stderr, "pc=%08x exceptions=%llu link packets=%d\n", (unsigned)soc.cpu.pc, (unsigned long long)soc.exc_count, link.packets);
		for (i = 0; i < PIC32MX_VECTORS; i++)
			if (soc.vec_count[i]) fprintf(stderr, "vector %d: %llu\n", i, (unsigned long long)soc.vec_count[i]);
	}
	return soc.exc_count ? 1 : 0;
}
