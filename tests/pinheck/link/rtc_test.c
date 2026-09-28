#include "../../../src/wpc/pinheck/rtc.h"
#include <stdio.h>

static ds1340 d;
static int fails, sda_in;

#define CHECK(c) do { if (!(c)) { printf("RTC FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void line(int scl, int sda) { sda_in = ds1340_update(&d, scl, sda); }
static void start(void) { line(1, 1); line(1, 0); line(0, 0); }
static void stop(void) { line(0, 0); line(1, 0); line(1, 1); }

static int put(int b)
{
	int k, ack;
	for (k = 7; k >= 0; k--) { line(0, b >> k & 1); line(1, b >> k & 1); line(0, b >> k & 1); }
	line(0, 1); line(1, 1); ack = !sda_in; line(0, 1);
	return ack;
}

static int get(int ack)
{
	int k, v = 0;
	for (k = 0; k < 8; k++) { line(0, 1); line(1, 1); v = v << 1 | sda_in; }
	line(0, ack ? 0 : 1); line(1, ack ? 0 : 1); line(0, 1);
	return v;
}

static void read_time(int *r)
{
	int k;
	start(); put(0xD0); put(0x00); start(); put(0xD1);
	for (k = 0; k < 7; k++) r[k] = get(k < 6);
	stop();
}

int main(void)
{
	int r[7];
	ds1340_init(&d, 1483228800, 1000);
	read_time(r);
	CHECK(r[0] == 0x00 && r[1] == 0x00 && r[2] == 0x00);
	CHECK(r[3] == 1 && r[4] == 0x01 && r[5] == 0x01 && r[6] == 0x17);
	ds1340_tick(&d, 3723000);
	read_time(r);
	CHECK(r[0] == 0x03 && r[1] == 0x02 && r[2] == 0x01);
	start();
	CHECK(put(0xD0) && put(0x04));
	CHECK(put(0x29) && put(0x02) && put(0x24));
	stop();
	read_time(r);
	CHECK(r[4] == 0x29 && r[5] == 0x02 && r[6] == 0x24 && r[3] == 5);
	ds1340_tick(&d, 86400000);
	read_time(r);
	CHECK(r[4] == 0x01 && r[5] == 0x03 && r[6] == 0x24);
	start();
	CHECK(!put(0xA0));
	stop();
	printf("rtc: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
