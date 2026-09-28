#include "pic32mx.h"
#include <stdio.h>
#include <string.h>

static pic32mx soc;
static uint8_t flash[PIC32MX_FLASH_SIZE];
static int fails;
static struct {
	int port_writes, port_last, tx_count, tx_uart, unmapped, exc_count, exc_code;
	uint32_t port_lat, port_in, exc_pc;
	uint8_t tx_byte;
} rec;

#define CHECK(c) do { if (!(c)) { printf("SOC FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void b_port_write(void *c, int port, uint32_t lat, uint32_t tris, uint64_t cy) { (void)c; (void)tris; (void)cy; rec.port_writes++; rec.port_last = port; rec.port_lat = lat; }
static uint32_t b_port_read(void *c, int port, uint64_t cy) { (void)c; (void)port; (void)cy; return rec.port_in; }
static void b_uart_tx(void *c, int u, uint8_t b, uint64_t cy) { (void)c; (void)cy; rec.tx_count++; rec.tx_uart = u; rec.tx_byte = b; }
static void b_unmapped(void *c, uint32_t pa, int w) { (void)c; (void)pa; (void)w; rec.unmapped++; }
static void b_exception(void *c, int code, uint32_t pc) { (void)c; if (!rec.exc_count++) { rec.exc_code = code; rec.exc_pc = pc; } }

static uint32_t rd(uint32_t va)
{
	int err = 0;
	return soc.cpu.bus.read(soc.cpu.bus.ctx, va & 0x1FFFFFFFu, 4, 0, &err);
}

static void wr(uint32_t va, uint32_t v)
{
	int err = 0;
	soc.cpu.bus.write(soc.cpu.bus.ctx, va & 0x1FFFFFFFu, v, 4, &err);
}

static void put(uint32_t off, uint32_t w)
{
	flash[off] = (uint8_t)w; flash[off + 1] = (uint8_t)(w >> 8); flash[off + 2] = (uint8_t)(w >> 16); flash[off + 3] = (uint8_t)(w >> 24);
}

static void setup(void)
{
	pic32mx_board board = { NULL, b_port_write, b_port_read, b_uart_tx, NULL, b_unmapped, b_exception, NULL };
	memset(&rec, 0, sizeof(rec));
	memset(flash, 0, sizeof(flash));
	put(0x1000, 0x1000FFFFu);
	pic32mx_init(&soc, &board, flash, sizeof(flash));
}

static int ifs(int irq) { return (rd(0xBF881030u + (uint32_t)(irq >> 5) * 0x10) >> (irq & 31)) & 1; }

static void port_set_clr_inv(void)
{
	setup();
	wr(0xBF886000u, 0);
	wr(0xBF886028u, 0x0005);
	wr(0xBF886024u, 0x0001);
	wr(0xBF88602Cu, 0x0006);
	CHECK(rd(0xBF886020u) == 0x0002);
	CHECK(rd(0xBF886010u) == 0x0002);
	wr(0xBF886018u, 0x0008);
	CHECK(rd(0xBF886020u) == 0x000A);
	CHECK(rec.port_last == PIC32MX_PORTA && rec.port_lat == 0x000A);
}

static void port_input_mix(void)
{
	setup();
	wr(0xBF886040u, 0x00F0);
	wr(0xBF886060u, 0xFFFF);
	rec.port_in = 0x0050;
	CHECK(rd(0xBF886050u) == 0xFF5F);
}

static void timer_period(void)
{
	setup();
	wr(0xBF800820u, 99);
	wr(0xBF800800u, 0x8000);
	pic32mx_run(&soc, 1050);
	CHECK(ifs(8));
	CHECK(rd(0xBF800810u) == 50);
}

static void timer_prescale_pbdiv(void)
{
	setup();
	wr(0xBF80F000u, (rd(0xBF80F000u) & ~(3u << 19)) | (2u << 19));
	wr(0xBF800820u, 9);
	wr(0xBF800800u, 0x8030);
	pic32mx_run(&soc, 700);
	CHECK(ifs(8));
	CHECK(rd(0xBF800810u) == 1);
}

static void timer_t32(void)
{
	setup();
	wr(0xBF800820u, 0x0000);
	wr(0xBF800A20u, 0x0001);
	wr(0xBF800A00u, 0x8000);
	wr(0xBF800800u, 0x8008);
	pic32mx_run(&soc, 100);
	CHECK(!ifs(12));
	pic32mx_run(&soc, 69900);
	CHECK(ifs(12));
	CHECK(!ifs(8));
	CHECK(rd(0xBF800810u) == 70000 - 65537);
	CHECK(rd(0xBF800A10u) == 0);
}

static void intc_priority(void)
{
	setup();
	wr(0xBF881008u, 1u << 12);
	wr(0xBF881068u, (1u << 8) | (1u << 12));
	wr(0xBF8810B0u, (3u << 2) | 1u);
	wr(0xBF8810C0u, (3u << 2) | 2u);
	wr(0xBF881038u, (1u << 8) | (1u << 12));
	CHECK(soc.cpu.eic_ripl == 3 && soc.cpu.eic_vector == 12 && soc.cpu.eic_srs == 0);
	wr(0xBF8810B0u, (5u << 2));
	CHECK(soc.cpu.eic_ripl == 5 && soc.cpu.eic_vector == 8);
	wr(0xBF881004u, 1u << 12);
	CHECK(soc.cpu.eic_ripl == 5 && soc.cpu.eic_vector == 0);
	wr(0xBF8810B0u, (7u << 2));
	CHECK(soc.cpu.eic_ripl == 7 && soc.cpu.eic_srs == 1);
	wr(0xBF881034u, (1u << 8) | (1u << 12));
	CHECK(soc.cpu.eic_ripl == 0);
}

static void core_timer_level(void)
{
	setup();
	soc.cpu.compare = 100;
	pic32mx_run(&soc, 300);
	CHECK(ifs(0));
	wr(0xBF881034u, 1u);
	pic32mx_run(&soc, 10);
	CHECK(ifs(0));
	soc.cpu.compare = 100000;
	soc.cpu.cause &= ~0x40000000u;
	wr(0xBF881034u, 1u);
	pic32mx_run(&soc, 10);
	CHECK(!ifs(0));
}

static void uart_tx_rx(void)
{
	int i;
	setup();
	wr(0xBF806000u, 0x8000);
	wr(0xBF806010u, (1u << 10) | (1u << 12));
	wr(0xBF806020u, 'A');
	CHECK(rec.tx_count == 1 && rec.tx_uart == 1 && rec.tx_byte == 'A');
	CHECK(ifs(28));
	pic32mx_uart_rx(&soc, 0, 'x');
	CHECK((rd(0xBF806010u) & 1) == 1);
	CHECK(ifs(27));
	CHECK(rd(0xBF806030u) == 'x');
	CHECK((rd(0xBF806010u) & 1) == 0);
	for (i = 0; i < 9; i++) pic32mx_uart_rx(&soc, 0, (uint8_t)i);
	CHECK(rd(0xBF806010u) & 2u);
	wr(0xBF806800u, 0x8000);
	wr(0xBF806810u, 1u << 10);
	wr(0xBF806820u, 'B');
	CHECK(rec.tx_uart == 2 && rec.tx_byte == 'B');
	CHECK(ifs(42));
}

static void i2c_timing_nack(void)
{
	setup();
	wr(0xBF805340u, 10);
	wr(0xBF805300u, 0x8000);
	wr(0xBF805308u, 1u);
	CHECK(!ifs(31));
	CHECK(rd(0xBF805300u) & 1u);
	pic32mx_run(&soc, 30);
	CHECK(ifs(31));
	CHECK(!(rd(0xBF805300u) & 1u));
	CHECK(rd(0xBF805310u) & (1u << 3));
	wr(0xBF881034u, 1u << 31);
	wr(0xBF805350u, 0xA0);
	CHECK(rd(0xBF805310u) & (1u << 14));
	pic32mx_run(&soc, 100);
	CHECK(!ifs(31));
	pic32mx_run(&soc, 200);
	CHECK(ifs(31));
	CHECK(rd(0xBF805310u) & (1u << 15));
	CHECK(!(rd(0xBF805310u) & (1u << 14)));
}

static void unmapped_sfr(void)
{
	setup();
	CHECK(rd(0xBF800200u) == 0);
	CHECK(rd(0xBF800200u) == 0);
	wr(0xBF800200u, 0x1234);
	CHECK(rd(0xBF800200u) == 0);
	CHECK(rec.unmapped == 1);
	CHECK(soc.logged[0x200 >> 4] == 1);
}

static void reserved_instruction(void)
{
	setup();
	put(0x1000, 0x0000003Fu);
	pic32mx_run(&soc, 20);
	CHECK(rec.exc_count > 0);
	CHECK(rec.exc_code == MIPS32_EXC_RI && rec.exc_pc == 0x9D001000u);
}

static uint64_t hold_until;

static uint64_t b_hold(void *c, uint64_t cy) { (void)c; return cy < hold_until ? hold_until - cy : 0; }

static void board_hold(void)
{
	setup();
	put(0x1000, 0x0000003Fu);
	soc.board.hold = b_hold;
	hold_until = 1000;
	pic32mx_run(&soc, 900);
	CHECK(rec.exc_count == 0 && soc.cpu.cycles == 900);
	pic32mx_run(&soc, 200);
	CHECK(rec.exc_count > 0 && rec.exc_pc == 0x9D001000u);
}

int main(void)
{
	port_set_clr_inv();
	port_input_mix();
	timer_period();
	timer_prescale_pbdiv();
	timer_t32();
	intc_priority();
	core_timer_level();
	uart_tx_rx();
	i2c_timing_nack();
	unmapped_sfr();
	reserved_instruction();
	board_hold();
	printf("soc: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
