#include "driver.h"
#include "core.h"
#include "cpu/pic32mx/pic32mxcpu.h"
#include "pinheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PINHECK_CLOCK 80000000
#define PINHECK_LOG_MAX 64

static struct {
	FILE *uart1;
	uint32_t logged[PINHECK_LOG_MAX];
	int nlogged;
} locals;

static void pinheck_uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (uart == 1 && locals.uart1) fputc(byte, locals.uart1);
}

static uint32_t pinheck_port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx; (void)port; (void)cycle;
	return 0xFFFFu;
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

PINMAME_VIDEO_UPDATE(pinheck_video)
{
	(void)layout;
	fillbitmap(bitmap, 0, cliprect);
}

static INTERRUPT_GEN(pinheck_vblank)
{
	core_updateSw(0);
}

static MACHINE_INIT(pinheck)
{
	pic32mx_board board = { NULL, NULL, pinheck_port_read, pinheck_uart_tx, NULL, pinheck_unmapped, pinheck_exception };
	const char *log = getenv("PINHECK_UART1_LOG");

	if (locals.uart1) fclose(locals.uart1);
	memset(&locals, 0, sizeof(locals));
	if (log && (locals.uart1 = fopen(log, "wb")) != NULL) setvbuf(locals.uart1, NULL, _IONBF, 0);
	pic32cpu_set_board(&board);
}

static MACHINE_STOP(pinheck)
{
	if (locals.uart1) fclose(locals.uart1);
	locals.uart1 = NULL;
}

static MEMORY_READ32_START(pinheck_readmem)
	{ 0x00000000, 0x00000003, MRA32_NOP },
MEMORY_END

static MEMORY_WRITE32_START(pinheck_writemem)
	{ 0x00000000, 0x00000003, MWA32_NOP },
MEMORY_END

MACHINE_DRIVER_START(PINHECK)
	MDRV_IMPORT_FROM(PinMAME)
	MDRV_CORE_INIT_RESET_STOP(pinheck, NULL, pinheck)
	MDRV_CPU_ADD_TAG("mcpu", PIC32MX, PINHECK_CLOCK)
	MDRV_CPU_MEMORY(pinheck_readmem, pinheck_writemem)
	MDRV_CPU_VBLANK_INT(pinheck_vblank, 1)
	MDRV_SCREEN_SIZE(128, 32)
	MDRV_VISIBLE_AREA(0, 127, 0, 31)
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_RGB_DIRECT)
MACHINE_DRIVER_END
