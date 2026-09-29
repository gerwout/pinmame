#include "display.h"
#include <stdio.h>
#include <string.h>

static display d;
static uint64_t now;
static uint32_t pins;
static uint8_t last_frame[DISPLAY_FRAME], last_cfg[DISPLAY_CFG_MAX];
static int frames, configs, cfg_n, logs;
static char last_log[128];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("DISPLAY FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void on_frame(void *ctx, const uint8_t *f, uint64_t t) { (void)ctx; (void)t; memcpy(last_frame, f, DISPLAY_FRAME); frames++; }
static void on_config(void *ctx, const uint8_t *b, int n, uint64_t t) { (void)ctx; (void)t; memcpy(last_cfg, b, (size_t)n); cfg_n = n; configs++; }
static void on_log(void *ctx, const char *m) { (void)ctx; strncpy(last_log, m, sizeof(last_log) - 1); logs++; }

static void set(uint32_t mask, int on)
{
	pins = on ? pins | mask : pins & ~mask;
	display_pins(&d, ++now, pins, 0xFFFFFFFFu);
}

static void byte(unsigned v)
{
	int b;
	for (b = 7; b >= 0; b--) {
		set(DISPLAY_P21, (v >> b) & 1);
		set(DISPLAY_P22, 1);
		set(DISPLAY_P22, 0);
	}
}

static void bits(int n)
{
	while (n--) { set(DISPLAY_P22, 1); set(DISPLAY_P22, 0); }
}

static void strobe(void)
{
	set(DISPLAY_P20, 1);
	set(DISPLAY_P22, 1);
	set(DISPLAY_P22, 0);
	set(DISPLAY_P20, 0);
}

static void reset(void)
{
	display_init(&d, NULL, on_frame, on_config, on_log);
	pins = 0;
	frames = configs = cfg_n = logs = 0;
	last_log[0] = 0;
	memset(last_frame, 0, sizeof(last_frame));
}

static void frame_and_bit_order(void)
{
	int i;
	reset();
	for (i = 0; i < DISPLAY_FRAME; i++) byte((unsigned)(i * 7 + 1) & 0xFF);
	CHECK(frames == 0);
	strobe();
	CHECK(frames == 1);
	for (i = 0; i < DISPLAY_FRAME && last_frame[i] == ((i * 7 + 1) & 0xFF); i++) ;
	CHECK(i == DISPLAY_FRAME);
	CHECK(logs == 0);
	reset();
	byte(0x80);
	bits((DISPLAY_FRAME - 1) * 8);
	strobe();
	CHECK(frames == 1 && last_frame[0] == 0x80 && last_frame[1] == 0x00);
}

static void config_packet(void)
{
	static const uint8_t seen[14] = { 0x00, 0xb1, 0x01, 0x54, 0x00, 0x00, 0x00, 0xff, 0x00, 0x80, 0x00, 0x20, 0x00, 0x3e };
	int i;
	reset();
	strobe();
	CHECK(frames == 0 && configs == 0 && logs == 0);
	set(DISPLAY_P17, 1);
	for (i = 0; i < 14; i++) byte(seen[i]);
	strobe();
	set(DISPLAY_P17, 0);
	CHECK(configs == 1 && cfg_n == 14 && memcmp(last_cfg, seen, 14) == 0);
	CHECK(frames == 0 && logs == 0);
}

static void short_and_long_frames(void)
{
	int i;
	reset();
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x55);
	strobe();
	for (i = 0; i < 100; i++) byte(0xAA);
	strobe();
	CHECK(frames == 1 && last_frame[0] == 0x55);
	CHECK(logs == 1 && strcmp(last_log, "display: frame of 100 bytes discarded") == 0);
	for (i = 0; i < 200; i++) byte(0xAA);
	strobe();
	CHECK(logs == 1);
	reset();
	for (i = 0; i < DISPLAY_FRAME + 1; i++) byte(0x11);
	strobe();
	CHECK(frames == 0 && strcmp(last_log, "display: frame of 4097 bytes discarded") == 0);
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x22);
	strobe();
	CHECK(frames == 1 && last_frame[0] == 0x22 && last_frame[DISPLAY_FRAME - 1] == 0x22);
}

static void partial_byte_and_mode_change(void)
{
	int i;
	reset();
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x33);
	bits(3);
	strobe();
	CHECK(frames == 0 && strcmp(last_log, "display: latch after 32771 bits, discarded") == 0);
	reset();
	for (i = 0; i < 8; i++) byte(0x44);
	set(DISPLAY_P17, 1);
	for (i = 0; i < 6; i++) byte(0x44);
	strobe();
	CHECK(configs == 0 && strcmp(last_log, "display: mode changed during a 14-byte transfer, discarded") == 0);
}

static void latch_clock_and_undriven(void)
{
	int i;
	reset();
	display_pins(&d, ++now, DISPLAY_P20 | DISPLAY_P22, 0);
	display_pins(&d, ++now, 0, DISPLAY_P20 | DISPLAY_P21 | DISPLAY_P22);
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x66);
	set(DISPLAY_P20, 1);
	set(DISPLAY_P21, 1);
	set(DISPLAY_P22, 1);
	set(DISPLAY_P22, 0);
	set(DISPLAY_P20, 0);
	CHECK(frames == 1 && logs == 0);
	for (i = 0; i < DISPLAY_FRAME; i++) {
		byte(0x77);
		display_pins(&d, ++now, pins | DISPLAY_P21 | DISPLAY_P22, DISPLAY_P20 | DISPLAY_P17);
		display_pins(&d, ++now, pins, 0xFFFFFFFFu);
	}
	strobe();
	CHECK(frames == 2 && last_frame[0] == 0x77 && logs == 0);
}

int main(void)
{
	frame_and_bit_order();
	config_packet();
	short_and_long_frames();
	partial_byte_and_mode_change();
	latch_clock_and_undriven();
	printf("display: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
