/* pic32mxcpu.c through PinMAME's CPU interface: a register shown or read holds its settled value */
#include "driver.h"
#include "cpuintrf.h"
#include "pic32mxcpu.h"
#include <stdio.h>
#include <string.h>

static uint8_t flash[PIC32MX_FLASH_SIZE];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("CPU FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

unsigned char *memory_region(int num) { (void)num; return flash; }
unsigned memory_region_length(int num) { (void)num; return sizeof(flash); }

static void put(uint32_t off, uint32_t w)
{
	flash[off] = (uint8_t)w; flash[off + 1] = (uint8_t)(w >> 8); flash[off + 2] = (uint8_t)(w >> 16); flash[off + 3] = (uint8_t)(w >> 24);
}

/* PORTF reads 0 with RF13 uncertain; its true value is 1 */
static uint32_t b_port_read(void *c, int port, uint64_t cy)
{
	(void)c; (void)cy;
	if (port == PIC32MX_PORTF) pic32mx_uncertain(pic32cpu_soc(), 1u << 13, 7);
	return 0;
}

static int b_port_settle(void *c, uint32_t token, int wait, uint32_t *bits)
{
	(void)c; (void)token;
	if (!wait) return 0;
	*bits = 1u << 13;
	return 1;
}

static void setup(void)
{
	pic32mx_board board;
	memset(&board, 0, sizeof(board));
	board.port_read = b_port_read;
	board.port_settle = b_port_settle;
	memset(flash, 0, sizeof(flash));
	/* lui t0, 0xBF88; lbu t1, 0x6151(t0) (PORTF byte 1); b .; nop */
	put(0x1000, 0x3C08BF88u); put(0x1004, 0x91096151u); put(0x1008, 0x1000FFFFu); put(0x100C, 0);
	pic32cpu_set_board(&board);
	pic32cpu_init();
	pic32cpu_execute(40);
}

int main(void)
{
	setup();
	CHECK(!strcmp(pic32cpu_info(NULL, CPU_INFO_REG + PIC32CPU_R0 + 9), "R9:00000020"));
	setup();
	CHECK(pic32cpu_get_reg(PIC32CPU_R0 + 9) == 0x20);
	printf("cpu: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
