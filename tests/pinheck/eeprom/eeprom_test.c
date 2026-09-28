#include "eeprom.h"
#include <stdio.h>
#include <string.h>

static uint8_t mem[0x20000];
static cat24m01 e;
static int scl = 1, sda = 1, dev = 1, fails;

#define CHECK(c) do { if (!(c)) { printf("EEPROM FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int bus(void) { return sda & dev; }
static void set(int c, int d) { scl = c; sda = d; dev = cat24m01_update(&e, scl, sda); }
static void start(void) { set(1, 1); set(1, 0); set(0, 0); }
static void stop(void) { set(0, 0); set(1, 0); set(1, 1); }

static int wbyte(int b)
{
	int i, ack;
	for (i = 7; i >= 0; i--) { set(0, (b >> i) & 1); set(1, (b >> i) & 1); set(0, (b >> i) & 1); }
	set(0, 1);
	set(1, 1);
	ack = !bus();
	set(0, 1);
	return ack;
}

static int rbyte(int ack)
{
	int i, b = 0;
	set(0, 1);
	for (i = 0; i < 8; i++) { set(1, 1); b = (b << 1) | bus(); set(0, 1); }
	set(0, !ack);
	set(1, !ack);
	set(0, !ack);
	set(0, 1);
	return b;
}

static int ctrl(int a16, int rd) { return 0xA0 | (0 << 2) | (a16 << 1) | rd; }

static void setaddr(uint32_t a)
{
	start();
	CHECK(wbyte(ctrl((int)(a >> 16), 0)));
	CHECK(wbyte((int)(a >> 8) & 0xFF));
	CHECK(wbyte((int)a & 0xFF));
}

static void write_bytes(uint32_t a, const uint8_t *d, int n)
{
	int i;
	setaddr(a);
	for (i = 0; i < n; i++) CHECK(wbyte(d[i]));
	stop();
}

static int random_read(uint32_t a)
{
	int v;
	setaddr(a);
	start();
	CHECK(wbyte(ctrl((int)(a >> 16), 1)));
	v = rbyte(0);
	stop();
	return v;
}

int main(void)
{
	static const uint8_t four[4] = { 0x11, 0x22, 0x33, 0x44 };
	uint8_t one = 0x5A;
	int i, v[3];

	cat24m01_init(&e, mem, 0);

	write_bytes(0x01234, &one, 1);
	CHECK(mem[0x01234] == 0x5A);
	CHECK(random_read(0x01234) == 0x5A);

	write_bytes(0x000FE, four, 4);
	CHECK(mem[0x000FE] == 0x11 && mem[0x000FF] == 0x22 && mem[0x00000] == 0x33 && mem[0x00001] == 0x44);
	CHECK(mem[0x00100] == 0x00);

	mem[0x100FF] = 0xA1; mem[0x10100] = 0xB2; mem[0x10101] = 0xC3;
	setaddr(0x100FF);
	start();
	CHECK(wbyte(ctrl(1, 1)));
	for (i = 0; i < 3; i++) v[i] = rbyte(i < 2);
	stop();
	CHECK(v[0] == 0xA1 && v[1] == 0xB2 && v[2] == 0xC3);

	start();
	CHECK(wbyte(ctrl(1, 1)));
	CHECK(rbyte(0) == mem[0x10102]);
	stop();

	mem[0x1FFFF] = 0x7E; mem[0x00000] = 0x33;
	setaddr(0x1FFFF);
	start();
	CHECK(wbyte(ctrl(1, 1)));
	v[0] = rbyte(1); v[1] = rbyte(0);
	stop();
	CHECK(v[0] == 0x7E && v[1] == 0x33);

	start();
	CHECK(!wbyte(0xA0 | (1 << 2)));
	stop();
	CHECK(bus() == 1);

	setaddr(0x00200);
	CHECK(wbyte(0x99));
	start();
	stop();
	CHECK(mem[0x00200] == 0x00);

	CHECK(dev == 1);
	printf("eeprom: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
