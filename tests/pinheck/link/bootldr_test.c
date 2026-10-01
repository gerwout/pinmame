#include "../../../src/wpc/pinheck/bootldr.h"
#include <stdio.h>
#include <string.h>

#define HOST_BIT 692u

static pic32_boot b;
static uint8_t flash[0x80000];
static uint64_t tx_t[200000];
static uint8_t tx_l[200000];
static int ntx, fails, nlog;
static uint64_t now;

#define CHECK(c) do { if (!(c)) { printf("BOOT FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void on_tx(void *ctx, uint64_t t, int level) { (void)ctx; tx_t[ntx] = t; tx_l[ntx++] = (uint8_t)level; }
static void on_log(void *ctx, const char *msg) { (void)ctx; (void)msg; nlog++; }

static void start(void)
{
	int i;
	for (i = 0; i < (int)sizeof(flash); i++) flash[i] = (uint8_t)(i * 7);
	ntx = nlog = 0;
	now = 1000;
	boot_init(&b, flash, sizeof(flash), on_tx, NULL);
	boot_set_log(&b, on_log, NULL);
	boot_reset(&b, now);
}

static void send(const uint8_t *body, int n, uint8_t seq, int corrupt)
{
	uint8_t f[700];
	int i, k, len = 0, level = 1;
	uint8_t x = 0;
	f[len++] = 0x1B; f[len++] = seq; f[len++] = (uint8_t)(n >> 8); f[len++] = (uint8_t)n; f[len++] = 0x0E;
	for (i = 0; i < n; i++) f[len++] = body[i];
	for (i = 0; i < len; i++) x ^= f[i];
	f[len++] = (uint8_t)(x ^ corrupt);
	for (i = 0; i < len; i++) {
		int bits[10];
		bits[0] = 0;
		for (k = 0; k < 8; k++) bits[k + 1] = f[i] >> k & 1;
		bits[9] = 1;
		for (k = 0; k < 10; k++) {
			if (bits[k] != level) { level = bits[k]; boot_rx(&b, now, level); }
			now += HOST_BIT;
		}
		if (i % 16 == 15) boot_advance(&b, now);
	}
	now += HOST_BIT;
}

static void run_until(uint64_t t)
{
	while (now < t) {
		uint64_t h = boot_hold(&b, now), step = h && h < 1000 ? h : 1000;
		boot_advance(&b, now);
		now += step < t - now ? step : t - now;
	}
	boot_advance(&b, now);
}

static int level_at(uint64_t t)
{
	int k, v = 1;
	for (k = 0; k < ntx && tx_t[k] <= t; k++) v = tx_l[k];
	return v;
}

static int decode(uint64_t from, uint8_t *out, int max)
{
	int n = 0, k, bit;
	uint64_t t = from;
	while (n < max) {
		uint64_t s = 0;
		for (k = 0; k < ntx; k++) if (tx_t[k] >= t && tx_l[k] == 0) { s = tx_t[k]; break; }
		if (!s) break;
		out[n] = 0;
		for (bit = 0; bit < 8; bit++) if (level_at(s + BOOT_BIT * (uint64_t)bit + BOOT_BIT * 3u / 2u)) out[n] |= (uint8_t)(1u << bit);
		n++;
		t = s + BOOT_BIT * 9u + BOOT_BIT / 2u;
	}
	return n;
}

static int exchange(const uint8_t *body, int n, uint8_t seq, uint8_t *reply, int max)
{
	int first = ntx;
	uint64_t from;
	send(body, n, seq, 0);
	run_until(now + 40000000u);
	if (ntx == first) return 0;
	from = tx_t[first];
	return decode(from, reply, max);
}

static void window_without_host(void)
{
	start();
	CHECK(tx_l[ntx - 1] == 1 && tx_t[ntx - 1] == 1000);
	CHECK(boot_hold(&b, now) > 0);
	run_until(1000 + BOOT_WINDOW - 1);
	CHECK(b.state == BOOT_WAIT && boot_hold(&b, now) > 0);
	run_until(1000 + BOOT_WINDOW);
	CHECK(b.state == BOOT_APP && boot_hold(&b, now) == 0);
	CHECK(tx_l[ntx - 1] == 0 && tx_t[ntx - 1] == 1000 + BOOT_WINDOW);
}

/* a game's own hold (America's Most Haunted: 5 s) applies from the next reset on */
static void window_set(void)
{
	start();
	boot_set_window(&b, 400000000u);
	boot_reset(&b, now);
	run_until(1000 + 400000000u - 1);
	CHECK(b.state == BOOT_WAIT && boot_hold(&b, now) > 0);
	run_until(1000 + 400000000u);
	CHECK(b.state == BOOT_APP && boot_hold(&b, now) == 0 && tx_t[ntx - 1] == 1000 + 400000000u);
}

static void sign_on_holds(void)
{
	static const uint8_t sign_on[1] = { 0x01 };
	uint8_t r[64];
	int n;
	start();
	run_until(130000000u);
	n = exchange(sign_on, 1, 0, r, 64);
	CHECK(n == 17);
	CHECK(r[0] == 0x1B && r[4] == 0x0E && r[5] == 0x01 && r[6] == 0x00 && r[7] == 8 && !memcmp(r + 8, "STK500_2", 8));
	CHECK(b.state == BOOT_HOST);
	run_until(1000 + BOOT_WINDOW + 80000000u);
	CHECK(b.state == BOOT_HOST && boot_hold(&b, now) > 0);
}

static void program_verify_leave(void)
{
	uint8_t body[600], r[600];
	int i, n;
	uint64_t app;
	start();
	body[0] = 0x01;
	exchange(body, 1, 0, r, 64);
	body[0] = 0x06; body[1] = 0; body[2] = 0; body[3] = 0x10; body[4] = 0x00;
	n = exchange(body, 5, 1, r, 64);
	CHECK(n == 8 && r[5] == 0x06 && r[6] == 0x00);
	body[0] = 0x13; body[1] = 0x01; body[2] = 0x00;
	for (i = 3; i < 10; i++) body[i] = 0xAA;
	for (i = 0; i < 256; i++) body[10 + i] = (uint8_t)(255 - i);
	n = exchange(body, 266, 2, r, 64);
	CHECK(n == 8 && r[5] == 0x13 && r[6] == 0x00);
	CHECK(flash[0x1000] == 255 && flash[0x10FF] == 0 && flash[0x1100] == 0xFF && flash[0x1FFF] == 0xFF);
	CHECK(flash[0x0FFF] == (uint8_t)(0x0FFF * 7) && flash[0x2000] == (uint8_t)(0x2000 * 7));
	body[0] = 0x06; body[1] = 0; body[2] = 0; body[3] = 0x10; body[4] = 0x00;
	exchange(body, 5, 3, r, 64);
	body[0] = 0x14; body[1] = 0x01; body[2] = 0x00; body[3] = 0x20;
	n = exchange(body, 4, 4, r, 600);
	CHECK(n == 256 + 9 && r[5] == 0x14 && r[6] == 0x00 && r[7] == 255 && r[7 + 255] == 0 && r[263] == 0x00);
	CHECK(b.state == BOOT_HOST);
	body[0] = 0x11; body[1] = 0x10; body[2] = 0x10;
	send(body, 3, 5, 0);
	while (!b.app_at) run_until(now + 1000);
	app = b.app_at;
	CHECK(app > 0 && b.state == BOOT_HOST);
	CHECK(boot_hold(&b, app - 1) > 0);
	run_until(app);
	CHECK(b.state == BOOT_APP && boot_hold(&b, now) == 0);
	CHECK(tx_l[ntx - 1] == 0 && tx_t[ntx - 1] == app);
	CHECK(tx_l[ntx - 2] == 1 && tx_t[ntx - 2] < app);
	CHECK(b.programmed == 256 && b.read == 256);
}

static void unknown_and_bad(void)
{
	uint8_t body[8], r[64];
	int n, before;
	start();
	body[0] = 0x01;
	exchange(body, 1, 0, r, 64);
	body[0] = 0x1D; body[1] = 0;
	n = exchange(body, 2, 1, r, 64);
	CHECK(n == 8 && r[5] == 0x1D && r[6] == 0xC0);
	exchange(body, 2, 2, r, 64);
	CHECK(nlog == 2);
	before = ntx;
	body[0] = 0x06; body[1] = body[2] = body[3] = body[4] = 0;
	send(body, 5, 3, 0x55);
	run_until(now + 40000000u);
	CHECK(ntx == before && b.errors == 1);
}

static void reset_redrives_idle(void)
{
	int before;
	start();
	run_until(1000 + BOOT_WINDOW);
	before = ntx;
	boot_reset(&b, now);
	CHECK(ntx == before + 1 && tx_l[ntx - 1] == 1 && tx_t[ntx - 1] == now);
	before = ntx;
	boot_reset(&b, now + 5);
	CHECK(ntx == before + 1 && tx_l[ntx - 1] == 1 && tx_t[ntx - 1] == now + 5);
}

static uint64_t signon_reply_at(uint64_t slice, uint64_t *last_start)
{
	uint64_t et[80], t0, t;
	uint8_t el[80], f[7] = { 0x1B, 0x01, 0x00, 0x01, 0x0E, 0x01, 0x00 };
	int i, k, n = 0, next = 0, level = 1, first;
	start();
	for (i = 0; i < 6; i++) f[6] ^= f[i];
	t0 = t = now + 5000;
	for (i = 0; i < 7; i++) {
		*last_start = t;
		for (k = 0; k < 10; k++) {
			int bit = k == 0 ? 0 : k == 9 ? 1 : f[i] >> (k - 1) & 1;
			if (bit != level) { level = bit; et[n] = t; el[n++] = (uint8_t)bit; }
			t += HOST_BIT;
		}
	}
	(void)t0;
	first = ntx;
	while (now < t + 40000000u) {
		uint64_t h = boot_hold(&b, now), step = h < slice ? h : slice;
		if (!step) step = slice;
		while (next < n && et[next] <= now + step) { boot_rx(&b, et[next], el[next]); next++; }
		now += step;
		boot_advance(&b, now);
		for (k = first; k < ntx; k++) if (tx_l[k] == 0) return tx_t[k];
	}
	return 0;
}

static void reply_timing_independent_of_step(void)
{
	static const uint64_t slices[3] = { 1000, 16000, 64000 };
	uint64_t last, at;
	int i;
	for (i = 0; i < 3; i++) {
		at = signon_reply_at(slices[i], &last);
		CHECK(at == last + BOOT_BIT * 10u + BOOT_LATENCY);
		if (at != last + BOOT_BIT * 10u + BOOT_LATENCY)
			printf("  slice %llu: reply %llu cycles after the last byte's stop bit\n", (unsigned long long)slices[i], (unsigned long long)(at - last - BOOT_BIT * 10u));
	}
}

int main(void)
{
	reply_timing_independent_of_step();
	window_without_host();
	window_set();
	sign_on_holds();
	program_verify_leave();
	unknown_and_bad();
	reset_redrives_idle();
	printf("boot: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
