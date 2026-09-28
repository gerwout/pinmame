#include "driver.h"
#include "core.h"
#include "cpu/pic32mx/pic32mxcpu.h"
#include "pinheck/prop.h"
#include "pinheck/rtc.h"
#include "pinheck/bootldr.h"
#include "pinheck/sd.h"
#include "pinheck/zipsrc.h"
#include "pinheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PINHECK_CLOCK 80000000
#define PINHECK_LOG_MAX 64
#define PINHECK_ZIP_CACHE (64u << 20)
#define RF5  (1u << 5)
#define RF12 (1u << 12)
#define RF13 (1u << 13)

static pinheck_prop prop;
static cat24m01 u13;
static ds1340 rtc;
static pic32_boot boot;
static zipsrc zip;
static vfat vol;
static sd_card card;
static uint8_t u13mem[131072], propmem[131072];
static int reset_done, opened;

static struct {
	FILE *uart1, *proplog;
	const char *send;
	uint64_t send_at, rtc_at;
	double reset_at;
	int have_zip, have_vol, idle;
	uint32_t logged[PINHECK_LOG_MAX];
	int nlogged;
} locals;

static void pinheck_uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (uart == 1 && locals.uart1) fputc(byte, locals.uart1);
}

static void pinheck_port_write(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle)
{
	uint32_t drv = lat & ~tris;
	(void)ctx;
	if (port == PIC32MX_PORTF)
		prop_pic_pins(&prop, cycle, (drv & RF12 ? 1u << 25 : 0) | (drv & RF5 ? 1u << 26 : 0));
}

static uint32_t pinheck_port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx;
	if (port != PIC32MX_PORTF) return 0xFFFFu;
	return (0xFFFFu & ~RF13) | (prop_p24(&prop, cycle) ? RF13 : 0);
}

static int pinheck_i2c_pins(void *ctx, int module, int scl, int sda, uint64_t cycle)
{
	(void)ctx;
	if (module != 1) return sda;
	ds1340_tick(&rtc, cycle - locals.rtc_at);
	locals.rtc_at = cycle;
	return cat24m01_update(&u13, scl, sda) & ds1340_update(&rtc, scl, sda);
}

static void pinheck_unmapped(void *ctx, uint32_t pa, int write)
{
	(void)ctx;
	logerror("pinheck: unmodelled SFR %s %08x\n", write ? "write" : "read", (unsigned)(pa | 0xA0000000u));
}

static void pinheck_exception(void *ctx, int code, uint32_t pc)
{
	uint32_t key = pc ^ ((uint32_t)code << 27);
	int i;
	(void)ctx;
	for (i = 0; i < locals.nlogged; i++)
		if (locals.logged[i] == key) return;
	if (locals.nlogged < PINHECK_LOG_MAX) locals.logged[locals.nlogged++] = key;
	logerror("pinheck: PIC32 exception %d at %08x\n", code, (unsigned)pc);
}

static void pinheck_prop_log(void *ctx, const char *msg)
{
	(void)ctx;
	logerror("pinheck: %s\n", msg);
	if (locals.proplog) fprintf(locals.proplog, "%s\n", msg);
}

static int pinheck_blk_read(void *ctx, uint32_t lba, uint8_t *buf) { (void)ctx; return vfat_read(&vol, lba, buf); }
static int pinheck_spi(void *ctx, int cs, int sclk, int mosi) { (void)ctx; return sd_update(&card, cs, sclk, mosi); }
static void pinheck_boot_tx(void *ctx, uint64_t t, int level) { (void)ctx; prop_pic_pins(&prop, t, level ? 1u << 24 : 0); }
static void pinheck_prop_tx(void *ctx, uint64_t t, int level) { (void)ctx; boot_rx(&boot, t, level); }

static uint64_t pinheck_hold(void *ctx, uint64_t cycle)
{
	(void)ctx;
	prop_catch_up(&prop, cycle);
	boot_advance(&boot, cycle);
	return boot_hold(&boot, cycle);
}

static void pinheck_open_card(void)
{
	char path[1024];
	int i, n = osd_get_path_count(FILETYPE_ROM);
	sd_blockdev dev;

	for (i = 0; i < n && !locals.have_zip; i++) {
		sprintf(path, "%.1000s/%.16s.zip", osd_get_path(FILETYPE_ROM, i), Machine->gamedrv->name);
		if (zipsrc_open(&zip, path, PINHECK_ZIP_CACHE) == 0) locals.have_zip = 1;
	}
	if (!locals.have_zip) { logerror("pinheck: no %s.zip on the ROM path, no SD card\n", Machine->gamedrv->name); return; }
	if (vfat_init(&vol, zipsrc_source(&zip)) != 0) { logerror("pinheck: cannot build the SD volume\n"); return; }
	locals.have_vol = 1;
	dev.ctx = NULL;
	dev.sectors = vfat_sectors(&vol);
	dev.read = pinheck_blk_read;
	sd_init(&card, &dev);
	prop_attach_sd(&prop, pinheck_spi, NULL);
}

static void pinheck_in_service(uint8_t *mem, uint32_t version)
{
	uint32_t w0 = version << 24 | 0xBAFAu, w1 = 0xABBA0002u;
	int i;
	for (i = 0; i < 4; i++) {
		mem[0x8000 + i] = (uint8_t)(w0 >> (8 * i));
		mem[0x8004 + i] = (uint8_t)(w1 >> (8 * i));
	}
}

static int64_t pinheck_local_now(void)
{
	time_t t = time(NULL);
	struct tm u = *gmtime(&t);
	u.tm_isdst = -1;
	return (int64_t)t + (int64_t)difftime(t, mktime(&u));
}

static void pinheck_tick(int param)
{
	pic32mx *soc = pic32cpu_soc();
	(void)param;
	if (locals.idle) return;
	prop_catch_up(&prop, soc->cpu.cycles);
	if (locals.reset_at > 0.0 && timer_get_time() >= locals.reset_at) {
		locals.reset_at = 0.0;
		reset_done = 1;
		machine_reset();
		return;
	}
	if (locals.send && *locals.send && soc->cpu.cycles >= locals.send_at) {
		int n = soc->uart[0].rx_count;
		pic32mx_uart_rx(soc, 0, (uint8_t)*locals.send);
		if (soc->uart[0].rx_count > n) locals.send++;
	}
}

PINMAME_VIDEO_UPDATE(pinheck_video)
{
	(void)layout;
	fillbitmap(bitmap, 0, cliprect);
}

static INTERRUPT_GEN(pinheck_vblank)
{
	core_updateSw(0);
}

static int pinheck_system_only(void)
{
	return !memory_region(PINHECK_CPUREGION) || !memory_region(PINHECK_PROPREGION);
}

static MACHINE_INIT(pinheck)
{
	pic32mx_board board = { NULL, pinheck_port_write, pinheck_port_read, pinheck_uart_tx, pinheck_i2c_pins, pinheck_unmapped, pinheck_exception, pinheck_hold };
	const char *log = getenv("PINHECK_UART1_LOG"), *plog = getenv("PINHECK_PROP_LOG");

	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
	memset(&locals, 0, sizeof(locals));
	if (log && (locals.uart1 = fopen(log, opened ? "ab" : "wb")) != NULL) setvbuf(locals.uart1, NULL, _IONBF, 0);
	if (plog && (locals.proplog = fopen(plog, opened ? "a" : "w")) != NULL) setvbuf(locals.proplog, NULL, _IONBF, 0);
	opened = 1;
	locals.reset_at = !reset_done && getenv("PINHECK_RESET_AT") ? atof(getenv("PINHECK_RESET_AT")) : 0.0;
	if (pinheck_system_only()) {
		locals.idle = 1;
		fprintf(stderr, "pinheck: '%s' is the pinHeck system set, not a game; run a game such as dominos\n", Machine->gamedrv->name);
		logerror("pinheck: '%s' is the pinHeck system set, not a game\n", Machine->gamedrv->name);
		return;
	}
	memcpy(propmem, memory_region(PINHECK_PROPREGION), 0x8000);
	prop_init(&prop, memory_region(PINHECK_BIOSREGION), propmem);
	prop_set_log(&prop, pinheck_prop_log, NULL);
	prop_set_tx(&prop, pinheck_prop_tx, NULL);
	boot_init(&boot, memory_region(PINHECK_CPUREGION), memory_region_length(PINHECK_CPUREGION), pinheck_boot_tx, NULL);
	boot_set_log(&boot, pinheck_prop_log, NULL);
	pinheck_open_card();
	pic32cpu_set_board(&board);
}

static MACHINE_RESET(pinheck)
{
	const char *at = getenv("PINHECK_UART1_SEND_AT");
	if (locals.idle) return;
	prop_reset(&prop, 0);
	boot_reset(&boot, 0);
	cat24m01_init(&u13, u13mem, 0);
	ds1340_init(&rtc, pinheck_local_now(), PINHECK_CLOCK);
	locals.rtc_at = 0;
	locals.send = getenv("PINHECK_UART1_SEND");
	locals.send_at = (uint64_t)((at ? atof(at) : 0.0) * PINHECK_CLOCK);
}

static NVRAM_HANDLER(pinheck)
{
	const int first = !read_or_write && !file;
	if (locals.idle) return;
	core_nvram(file, read_or_write, u13mem, sizeof(u13mem), 0xFF);
	core_nvram(file, read_or_write, propmem + 0x8000, sizeof(propmem) - 0x8000, 0xFF);
	if (first && getenv("PINHECK_INSERVICE")) pinheck_in_service(propmem, core_gameData->hw.gameSpecific1);
}

static MACHINE_STOP(pinheck)
{
	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
	locals.uart1 = locals.proplog = NULL;
	if (locals.have_vol) vfat_free(&vol);
	if (locals.have_zip) zipsrc_close(&zip);
	locals.have_vol = locals.have_zip = 0;
}

static MEMORY_READ32_START(pinheck_readmem)
	{ 0x00000000, 0x00000003, MRA32_NOP },
MEMORY_END

static MEMORY_WRITE32_START(pinheck_writemem)
	{ 0x00000000, 0x00000003, MWA32_NOP },
MEMORY_END

MACHINE_DRIVER_START(PINHECK)
	MDRV_IMPORT_FROM(PinMAME)
	MDRV_CORE_INIT_RESET_STOP(pinheck, pinheck, pinheck)
	MDRV_CPU_ADD_TAG("mcpu", PIC32MX, PINHECK_CLOCK)
	MDRV_CPU_MEMORY(pinheck_readmem, pinheck_writemem)
	MDRV_CPU_VBLANK_INT(pinheck_vblank, 1)
	MDRV_TIMER_ADD(pinheck_tick, 1000)
	MDRV_NVRAM_HANDLER(pinheck)
	MDRV_SCREEN_SIZE(128, 32)
	MDRV_VISIBLE_AREA(0, 127, 0, 31)
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_RGB_DIRECT)
MACHINE_DRIVER_END
