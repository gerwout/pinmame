#include "sd.h"
#include <stdio.h>
#include <string.h>

static sd_card card;
static int fails;
static uint32_t last_lba;

#define CHECK(c) do { if (!(c)) { printf("SD FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int dev_read(void *ctx, uint32_t lba, uint8_t *b)
{
	int k;
	(void)ctx;
	last_lba = lba;
	for (k = 0; k < 512; k++) b[k] = (uint8_t)(lba * 7 + k);
	return 0;
}

static uint8_t xfer(uint8_t out)
{
	uint8_t in = 0;
	int k;
	for (k = 7; k >= 0; k--) {
		int bit = (out >> k) & 1;
		sd_update(&card, 0, 0, bit);
		in = (uint8_t)(in << 1 | sd_update(&card, 0, 1, bit));
	}
	return in;
}

static void deselect(void)
{
	int k;
	for (k = 0; k < 8; k++) { sd_update(&card, 1, 0, 1); sd_update(&card, 1, 1, 1); }
	sd_update(&card, 1, 0, 1);
}

static uint8_t cmd(int idx, uint32_t arg, uint8_t crc)
{
	int k;
	uint8_t r = 0xFF;
	xfer((uint8_t)(0x40 | idx));
	xfer((uint8_t)(arg >> 24)); xfer((uint8_t)(arg >> 16)); xfer((uint8_t)(arg >> 8)); xfer((uint8_t)arg);
	xfer(crc);
	for (k = 0; k < 8 && r == 0xFF; k++) r = xfer(0xFF);
	return r;
}

static uint16_t crc16(const uint8_t *d, int n)
{
	uint16_t c = 0;
	int i, b;
	for (i = 0; i < n; i++) {
		c ^= (uint16_t)(d[i] << 8);
		for (b = 0; b < 8; b++) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1);
	}
	return c;
}

static int read_data(uint8_t *d, int n)
{
	int k;
	uint8_t t = 0xFF;
	uint16_t c;
	for (k = 0; k < 16 && t == 0xFF; k++) t = xfer(0xFF);
	if (t != 0xFE) return t;
	for (k = 0; k < n; k++) d[k] = xfer(0xFF);
	c = (uint16_t)(xfer(0xFF) << 8);
	c |= xfer(0xFF);
	return c == crc16(d, n) ? 0 : -2;
}

static void init_card(uint32_t sectors)
{
	sd_blockdev dev;
	dev.ctx = NULL;
	dev.sectors = sectors;
	dev.read = dev_read;
	sd_init(&card, &dev);
	deselect();
}

static void sdhc_path(void)
{
	uint8_t b[512], csd[16];
	int k, ok = 1;

	init_card(1584690);
	CHECK(sd_update(&card, 1, 0, 1) == 1);
	CHECK(cmd(0, 0, 0x95) == 0x01);
	CHECK(cmd(8, 0x1AA, 0x87) == 0x01);
	CHECK(xfer(0xFF) == 0x00 && xfer(0xFF) == 0x00 && xfer(0xFF) == 0x01 && xfer(0xFF) == 0xAA);
	CHECK(cmd(55, 0, 0x65) == 0x01);
	CHECK(cmd(41, 0x40000000u, 0x77) == 0x00);
	CHECK(cmd(58, 0, 0xFD) == 0x00);
	CHECK(xfer(0xFF) == 0xC0 && xfer(0xFF) == 0xFF && xfer(0xFF) == 0x80 && xfer(0xFF) == 0x00);
	CHECK(cmd(9, 0, 0xAF) == 0x00);
	CHECK(read_data(csd, 16) == 0);
	CHECK(csd[0] >> 6 == 1);
	CHECK((((uint32_t)(csd[7] & 0x3F) << 16) | (uint32_t)csd[8] << 8 | csd[9]) == 1584690u / 1024 - 1);
	CHECK(cmd(17, 12345, 0xFF) == 0x00);
	CHECK(read_data(b, 512) == 0 && last_lba == 12345);
	for (k = 0; k < 512; k++) ok &= b[k] == (uint8_t)(12345 * 7 + k);
	CHECK(ok);
	CHECK(cmd(18, 200, 0xFF) == 0x00);
	CHECK(read_data(b, 512) == 0 && b[0] == (uint8_t)(200 * 7));
	CHECK(read_data(b, 512) == 0 && b[0] == (uint8_t)(201 * 7));
	CHECK(cmd(12, 0, 0xFF) == 0x00);
	CHECK(xfer(0xFF) == 0xFF);
	CHECK(cmd(17, 1584690, 0xFF) == 0x00);
	CHECK(read_data(b, 512) == 0x08);
	CHECK(cmd(24, 7, 0xFF) == 0x00);
	xfer(0xFF); xfer(0xFE);
	for (k = 0; k < 514; k++) xfer(0x5A);
	CHECK((xfer(0xFF) & 0x1F) == 0x05);
	CHECK(card.writes == 1);
	CHECK(cmd(63, 0, 0xFF) == 0x04);
	deselect();
	CHECK(sd_update(&card, 1, 1, 0) == 1);
}

static void byte_addressed_path(void)
{
	uint8_t b[512];
	init_card(4096);
	CHECK(cmd(0, 0, 0x95) == 0x01);
	CHECK(cmd(55, 0, 0x65) == 0x01);
	CHECK(cmd(41, 0, 0xFF) == 0x00);
	CHECK(cmd(58, 0, 0xFD) == 0x00);
	CHECK((xfer(0xFF) & 0x40) == 0);
	xfer(0xFF); xfer(0xFF); xfer(0xFF);
	CHECK(cmd(17, 3 * 512, 0xFF) == 0x00);
	CHECK(read_data(b, 512) == 0 && last_lba == 3);
	CHECK(cmd(17, 3 * 512 + 1, 0xFF) == 0x20);
	CHECK(cmd(16, 1024, 0xFF) == 0x40);
}

static void clock_high_at_select(void)
{
	init_card(4096);
	sd_update(&card, 1, 1, 1);
	CHECK(cmd(0, 0, 0x95) == 0x01);
	CHECK(cmd(8, 0x1AA, 0x87) == 0x01);
	CHECK(xfer(0xFF) == 0x00 && xfer(0xFF) == 0x00 && xfer(0xFF) == 0x01 && xfer(0xFF) == 0xAA);
}

int main(void)
{
	clock_high_at_select();
	sdhc_path();
	byte_addressed_path();
	printf("sd: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
