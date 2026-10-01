#include "display.h"
#include <stdio.h>
#include <string.h>

static display d;
static uint64_t now;
static uint32_t pins;
static uint8_t last_frame[DISPLAY_FRAME_MAX], last_cfg[DISPLAY_CFG_MAX];
static int frames, configs, cfg_n, logs;
static char last_log[128], log_lines[24][64];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("DISPLAY FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void on_frame(void *ctx, const uint8_t *f, uint64_t t) { (void)ctx; (void)t; memcpy(last_frame, f, (size_t)d.frame); frames++; }
static void on_config(void *ctx, const uint8_t *b, int n, uint64_t t) { (void)ctx; (void)t; memcpy(last_cfg, b, (size_t)n); cfg_n = n; configs++; }
static void on_log(void *ctx, const char *m)
{
	(void)ctx;
	strncpy(last_log, m, sizeof(last_log) - 1);
	if (logs < 24) strncpy(log_lines[logs], m, sizeof(log_lines[0]) - 1);
	logs++;
}

static void set(uint32_t mask, int on)
{
	pins = on ? pins | mask : pins & ~mask;
	pinheck_display_pins(&d, ++now, pins, 0xFFFFFFFFu);
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
	pinheck_display_init(&d, NULL, on_frame, on_config, on_log);
	pins = 0;
	frames = configs = cfg_n = logs = 0;
	last_log[0] = 0;
	memset(log_lines, 0, sizeof(log_lines));
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

/* the 128x64 module: 8192-byte frames; a 128x32 frame or a 1024-byte burst on its own is discarded */
static void frame_128x64(void)
{
	int i;
	reset();
	CHECK(pinheck_display_size(&d, 128, 48) == 0 && pinheck_display_size(&d, 256, 32) == 0 && d.frame == DISPLAY_FRAME);
	CHECK(pinheck_display_size(&d, 128, 64) == 1 && d.frame == 8192);
	for (i = 0; i < 8192; i++) byte((unsigned)(i * 5 + 3) & 0xFF);
	strobe();
	CHECK(frames == 1 && logs == 0);
	for (i = 0; i < 8192 && last_frame[i] == ((i * 5 + 3) & 0xFF); i++) ;
	CHECK(i == 8192);
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x11);
	strobe();
	CHECK(frames == 1 && strcmp(last_log, "display: frame of 4096 bytes discarded") == 0);
	for (i = 0; i < 8193; i++) byte(0x22);
	strobe();
	CHECK(frames == 1);
	for (i = 0; i < 1024; i++) byte(0x33);
	strobe();
	for (i = 0; i < 8192; i++) byte(0x44);
	strobe();
	CHECK(frames == 2 && last_frame[0] == 0x44 && last_frame[8191] == 0x44);
	CHECK(pinheck_display_size(&d, 128, 32) == 1 && d.frame == DISPLAY_FRAME);
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
	CHECK(logs == 2 && strcmp(last_log, "display: frame of 200 bytes discarded") == 0);
	reset();
	for (i = 0; i < DISPLAY_FRAME + 1; i++) byte(0x11);
	strobe();
	CHECK(frames == 0 && strcmp(last_log, "display: frame of 4097 bytes discarded") == 0);
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x22);
	strobe();
	CHECK(frames == 1 && last_frame[0] == 0x22 && last_frame[DISPLAY_FRAME - 1] == 0x22);
}

static void burst(int n)
{
	int i;
	for (i = 0; i < n; i++) byte(0x5A);
	strobe();
}

/* The Jetsons' start: each discarded frame is logged, up to DISPLAY_LOG_FRAMES, then one line says so */
static void discards_logged(void)
{
	int i;
	reset();
	pinheck_display_size(&d, 128, 64);
	burst(1024);
	burst(7168);
	burst(8192);
	burst(1024);
	CHECK(frames == 1 && logs == 3);
	CHECK(strcmp(log_lines[0], "display: frame of 1024 bytes discarded") == 0);
	CHECK(strcmp(log_lines[1], "display: frame of 7168 bytes discarded") == 0);
	CHECK(strcmp(log_lines[2], "display: frame of 1024 bytes discarded") == 0);
	for (i = 3; i < DISPLAY_LOG_FRAMES + 5; i++) burst(10);
	CHECK(logs == DISPLAY_LOG_FRAMES + 1);
	CHECK(strcmp(log_lines[DISPLAY_LOG_FRAMES], "display: further discarded frames not logged") == 0);
	burst(8192);
	CHECK(frames == 2);
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
	pinheck_display_pins(&d, ++now, DISPLAY_P20 | DISPLAY_P22, 0);
	pinheck_display_pins(&d, ++now, 0, DISPLAY_P20 | DISPLAY_P21 | DISPLAY_P22);
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x66);
	set(DISPLAY_P20, 1);
	set(DISPLAY_P21, 1);
	set(DISPLAY_P22, 1);
	set(DISPLAY_P22, 0);
	set(DISPLAY_P20, 0);
	CHECK(frames == 1 && logs == 0);
	for (i = 0; i < DISPLAY_FRAME; i++) {
		byte(0x77);
		pinheck_display_pins(&d, ++now, pins | DISPLAY_P21 | DISPLAY_P22, DISPLAY_P20 | DISPLAY_P17);
		pinheck_display_pins(&d, ++now, pins, 0xFFFFFFFFu);
	}
	strobe();
	CHECK(frames == 2 && last_frame[0] == 0x77 && logs == 0);
}

static const uint8_t cfg_prop[14] = { 0x00, 0xb1, 0x01, 0x54, 0x00, 0x00, 0x00, 0xff, 0x00, 0x80, 0x00, 0x20, 0x00, 0x3e };
/* guard bytes after the frame: a read past it shows as white, not as black */
static uint8_t framebuf[DISPLAY_FRAME + 256], img[DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3 + 16];
#define frame framebuf

static int is(int x, int y, int r, int g, int b)
{
	const uint8_t *p = img + (y * DISPLAY_LOOK_W + x) * 3;
	if (p[0] == r && p[1] == g && p[2] == b) return 1;
	printf("  pixel (%d,%d) = %d,%d,%d, want %d,%d,%d\n", x, y, p[0], p[1], p[2], r, g, b);
	return 0;
}

static void draw(int shape, int brightness, int position, int bar)
{
	display_look lk;
	lk.shape = shape;
	lk.brightness = brightness;
	lk.position = position;
	lk.bar = bar;
	memset(img, 0xA5, sizeof(img));
	memset(framebuf + DISPLAY_FRAME, 0xFF, sizeof(framebuf) - DISPLAY_FRAME);
	pinheck_display_render(&lk, frame, img);
}

static void look_decode(void)
{
	static const uint8_t pic[14] = { 0x00, 0xfa, 0x01, 0xf4, 0x00, 0x02, 0x00, 0xaf, 0x00, 0x80, 0x00, 0x20, 0x00, 0x00 };
	uint8_t odd[14];
	display_look lk;
	CHECK(pinheck_display_look(&lk, cfg_prop, 14) == 1);
	CHECK(lk.shape == DISPLAY_ROUND && lk.position == 340 && lk.brightness == 255 && lk.bar == 62);
	CHECK(pinheck_display_look(&lk, pic, 14) == 1);
	CHECK(lk.shape == DISPLAY_HIGHREZ && lk.position == 500 && lk.brightness == 175 && lk.bar == 0);
	CHECK(pinheck_display_look(&lk, cfg_prop, 13) == 0);
	CHECK(lk.shape == DISPLAY_SQUARE && lk.position == 340 && lk.brightness == 255 && lk.bar == 62);
	memcpy(odd, pic, 14);
	odd[8] = 0x01;
	CHECK(pinheck_display_look(&lk, odd, 14) == 0 && lk.shape == DISPLAY_SQUARE && lk.brightness == 255);
	CHECK(pinheck_display_look(&lk, NULL, 0) == 0 && lk.shape == DISPLAY_SQUARE);
	memcpy(odd, pic, 14);
	odd[2] = odd[3] = 0; odd[5] = 3; odd[6] = 2; odd[13] = 64;
	CHECK(pinheck_display_look(&lk, odd, 14) == 1);
	CHECK(lk.shape == DISPLAY_SQUARE && lk.position == 0 && lk.brightness == 255 && lk.bar == 62);
}

static void look_square(void)
{
	int i, x, y, ok = 1;
	for (i = 0; i < DISPLAY_FRAME; i++) frame[i] = (uint8_t)(i * 7 + 1);
	frame[0] = 0xFF; frame[1] = 0xE0; frame[2] = 0x1C; frame[3] = 0x03; frame[4] = 0x49;
	draw(DISPLAY_SQUARE, 255, 340, 62);
	CHECK(is(0, 0, 255, 255, 255) && is(1, 1, 255, 255, 255) && is(2, 0, 255, 0, 0) && is(4, 1, 0, 255, 0));
	CHECK(is(7, 0, 0, 0, 255) && is(8, 0, 72, 72, 85) && is(9, 1, 72, 72, 85));
	for (y = 0; y < DISPLAY_LOOK_H; y++)
		for (x = 0; x < DISPLAY_LOOK_W; x++) {
			const uint8_t v = frame[(y / 2) * DISPLAY_W + x / 2], *p = img + (y * DISPLAY_LOOK_W + x) * 3;
			ok &= p[0] == ((v >> 5) & 7) * 255 / 7 && p[1] == ((v >> 2) & 7) * 255 / 7 && p[2] == (v & 3) * 255 / 3;
		}
	CHECK(ok);
	CHECK(img[sizeof(img) - 16] == 0xA5);
}

static void look_round(void)
{
	memset(frame, 0, sizeof(frame));
	frame[5 * DISPLAY_W + 10] = 0xFF;
	frame[DISPLAY_FRAME - 1] = 0xE0;
	draw(DISPLAY_ROUND, 255, 340, 62);
	CHECK(is(20, 10, 255, 255, 255) && is(21, 10, 63, 63, 63) && is(20, 11, 63, 63, 63) && is(21, 11, 31, 31, 31));
	CHECK(is(19, 10, 63, 63, 63) && is(19, 9, 31, 31, 31) && is(22, 10, 0, 0, 0) && is(18, 10, 0, 0, 0));
	CHECK(is(254, 62, 255, 0, 0) && is(255, 62, 63, 0, 0) && is(254, 63, 63, 0, 0) && is(255, 63, 31, 0, 0));
	draw(DISPLAY_ROUND, 255, 340, 0);
	CHECK(is(20, 10, 255, 255, 255) && is(21, 10, 0, 0, 0) && is(21, 11, 0, 0, 0) && is(19, 9, 0, 0, 0));
	CHECK(img[sizeof(img) - 16] == 0xA5);
}

static void look_highrez(void)
{
	memset(frame, 0, sizeof(frame));
	frame[1] = 0xFF;
	frame[DISPLAY_W] = 0xFF;
	draw(DISPLAY_HIGHREZ, 255, 340, 62);
	CHECK(is(0, 0, 0, 0, 0) && is(1, 0, 0, 0, 0) && is(0, 1, 0, 0, 0) && is(1, 1, 255, 255, 255));
	CHECK(is(2, 0, 255, 255, 255) && is(3, 1, 255, 255, 255) && is(0, 2, 255, 255, 255) && is(1, 3, 255, 255, 255));
	CHECK(is(2, 2, 255, 255, 255) && is(3, 2, 0, 0, 0) && is(2, 3, 0, 0, 0) && is(3, 3, 0, 0, 0));
	memset(frame, 0x49, sizeof(frame));
	draw(DISPLAY_HIGHREZ, 255, 340, 62);
	CHECK(is(0, 0, 72, 72, 85) && is(255, 63, 72, 72, 85) && is(101, 30, 72, 72, 85));
}

static void look_brightness(void)
{
	memset(frame, 0, sizeof(frame));
	frame[0] = 0xFF;
	frame[1] = 0x49;
	draw(DISPLAY_SQUARE, 175, 340, 62);
	CHECK(is(0, 0, 175, 175, 175) && is(1, 1, 175, 175, 175) && is(2, 0, 49, 49, 58));
	draw(DISPLAY_ROUND, 175, 340, 62);
	CHECK(is(0, 0, 175, 175, 175) && is(1, 0, 55, 55, 58));
}

static void look_position(void)
{
	int x, ok = 1;
	memset(frame, 0, sizeof(frame));
	memset(frame, 0xFF, DISPLAY_W);
	draw(DISPLAY_SQUARE, 255, 344, 62);
	CHECK(is(0, 0, 0, 0, 0) && is(0, 1, 255, 255, 255) && is(255, 2, 255, 255, 255) && is(0, 3, 0, 0, 0));
	draw(DISPLAY_SQUARE, 255, 343, 62);
	CHECK(is(0, 0, 255, 255, 255) && is(0, 1, 255, 255, 255) && is(0, 2, 0, 0, 0));
	draw(DISPLAY_SQUARE, 255, 339, 62);
	CHECK(is(0, 0, 255, 255, 255) && is(0, 1, 0, 0, 0) && is(0, 63, 0, 0, 0));
	draw(DISPLAY_SQUARE, 255, 500, 62);
	CHECK(is(0, 39, 0, 0, 0) && is(0, 40, 255, 255, 255) && is(0, 41, 255, 255, 255) && is(0, 42, 0, 0, 0));
	CHECK(img[sizeof(img) - 16] == 0xA5);
	draw(DISPLAY_SQUARE, 255, 300, 62);
	for (x = 0; x < DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3; x++) ok &= img[x] == 0;
	CHECK(ok);
	draw(DISPLAY_SQUARE, 255, 65535, 62);
	for (x = 0; x < DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3; x++) ok &= img[x] == 0;
	CHECK(ok && img[sizeof(img) - 16] == 0xA5);
}

int main(void)
{
	frame_and_bit_order();
	frame_128x64();
	config_packet();
	short_and_long_frames();
	discards_logged();
	partial_byte_and_mode_change();
	latch_clock_and_undriven();
	look_decode();
	look_square();
	look_round();
	look_highrez();
	look_brightness();
	look_position();
	printf("display: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
