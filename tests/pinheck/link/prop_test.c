#define _POSIX_C_SOURCE 200809L
#include "../../../src/wpc/pinheck/prop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define CLK  (1u << 25)
#define DATA (1u << 26)
#define BITS 64

static pinheck_prop p;
static uint8_t rom[0x8000], ram[0x8000], eemem[131072];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("PROP FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t hub32(uint32_t a)
{
	const uint8_t *h = p.chip.hub + a;
	return (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
}

static uint32_t count(void) { return hub32(0x7FF0); }

static void boot(void)
{
	memset(eemem, 0xFF, sizeof(eemem));
	prop_init(&p, rom, eemem);
	memcpy(p.chip.hub, ram, sizeof(ram));
}

static void pulse(uint64_t *pic, int bit)
{
	prop_pic_pins(&p, *pic, CLK | (bit ? DATA : 0));
	prop_pic_pins(&p, *pic + 200, bit ? DATA : 0);
	*pic += 400;
}

static void rates(void)
{
	boot();
	CHECK(prop_time(&p, 20000) == 3000);
	prop_catch_up(&p, 200000);
	CHECK(p.nseg == 2 && p.seg[1].num == 13 && p.seg[1].den == 10);
	CHECK(prop_time(&p, 200010) - prop_time(&p, 200000) == 13);
	CHECK(prop_time(&p, p.seg[1].pic0) == p.seg[1].prop0);
}

static void echo_exact(void)
{
	uint32_t rec[BITS], off = 0;
	uint64_t pic = 200000, at[BITS];
	int k, prev = 0;
	boot();
	prop_catch_up(&p, pic);
	for (k = 0; k < BITS; k++) {
		int bit = (k * 7 + 3) % 5 < 2;
		at[k] = pic;
		prop_pic_pins(&p, pic, CLK | (bit ? DATA : 0));
		CHECK(prop_p24(&p, pic + 1) == prev);
		CHECK(prop_p24(&p, pic + 100) == bit);
		prop_pic_pins(&p, pic + 200, bit ? DATA : 0);
		pic += 400;
		prev = bit;
	}
	prop_catch_up(&p, pic);
	CHECK(count() == BITS);
	for (k = 0; k < BITS; k++) {
		rec[k] = hub32(0x1000 + 4 * (uint32_t)k);
		if (k == 0) off = rec[0] - (uint32_t)prop_time(&p, at[0]);
		CHECK(rec[k] - (uint32_t)prop_time(&p, at[k]) == off);
	}
	boot();
	pic = 200000;
	for (k = 0; k < BITS; k++) pulse(&pic, (k * 7 + 3) % 5 < 2);
	prop_catch_up(&p, pic);
	CHECK(count() == BITS);
	for (k = 0; k < BITS; k++) CHECK(hub32(0x1000 + 4 * (uint32_t)k) == rec[k]);
}

static void clkset_lazy(void)
{
	static const int at[] = { 0, 10, 100, 999 };
	uint64_t pic = 60000;
	int k;
	boot();
	for (k = 0; k < 1000; k++) pulse(&pic, 0);
	CHECK(p.nseg == 1);
	for (k = 0; k < 4; k++) {
		prop_catch_up(&p, 60000 + 400 * (uint64_t)at[k] + 150);
		CHECK(count() == (uint32_t)at[k] + 1);
	}
	CHECK(p.nseg == 2);
}

static void full_log(void)
{
	uint64_t pic = 60000;
	int k;
	boot();
	for (k = 0; k < 3000; k++) pulse(&pic, k & 1);
	CHECK(p.count <= PROP_EDGES);
	prop_catch_up(&p, pic);
	CHECK(count() == 3000);
}

static void reset_rebases(int restart_at_zero)
{
	uint64_t now, pic = 200000;
	boot();
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	pulse(&pic, 0);
	prop_catch_up(&p, pic);
	CHECK(count() == 2);
	now = p.chip.now;
	if (restart_at_zero) pic = 0;
	prop_reset(&p, pic);
	CHECK(p.chip.now >= now);
	CHECK(prop_time(&p, pic) == p.chip.now);
	CHECK(prop_time(&p, pic + 1000) > prop_time(&p, pic));
	pic += 200000;
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	prop_catch_up(&p, pic);
	CHECK(count() == 1);
}

static void clkset_reset_bit(void)
{
	uint64_t pic = 200000;
	boot();
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	pulse(&pic, 0);
	prop_catch_up(&p, pic);
	CHECK(count() == 2);
	p.chip.bus.clkset(p.chip.bus.ctx, p.chip.now, 0x80);
	pic += 1000;
	prop_catch_up(&p, pic);
	CHECK(p.nseg == 1 && p.seg[0].num == 3);
	pic += 400000;
	prop_catch_up(&p, pic);
	CHECK(p.nseg == 2 && p.seg[1].num == 13);
	pulse(&pic, 1);
	prop_catch_up(&p, pic);
	CHECK(count() == 1);
}

static uint64_t clock_now;
static uint64_t stamps[4 * BITS];
static int nstamps;

static uint64_t test_clock(void *ctx) { (void)ctx; return clock_now; }

static void stamp_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx; (void)t; (void)out; (void)dir;
	if (nstamps < 4 * BITS) stamps[nstamps++] = prop_stamp(&p);
}

/* the same calls on the worker thread give the same Propeller, pin changes and stamps (the clock runs ahead of
   the calls' cycles, so a stamp taken from the wrong one differs) */
static void threaded(void)
{
	static uint8_t hub[65536];
	static uint64_t seq[4 * BITS];
	uint64_t pic, now = 0;
	int k, pass, n = 0, p24[BITS];
	for (pass = 0; pass < 2; pass++) {
		boot();
		prop_set_clock(&p, test_clock, NULL);
		prop_set_pins(&p, stamp_pins, NULL);
		nstamps = 0;
		if (pass) CHECK(prop_start_thread(&p) == 0);
		pic = 200000;
		clock_now = pic + 1000;
		prop_catch_up(&p, pic);
		for (k = 0; k < BITS; k++) {
			int bit = (k * 7 + 3) % 5 < 2, v;
			clock_now = pic + 1000;
			prop_pic_pins(&p, pic, CLK | (bit ? DATA : 0));
			clock_now = pic + 1100;
			v = prop_p24(&p, pic + 100);
			if (!pass) p24[k] = v;
			else CHECK(v == p24[k]);
			clock_now = pic + 1200;
			prop_pic_pins(&p, pic + 200, bit ? DATA : 0);
			clock_now = pic + 1300;
			prop_catch_up(&p, pic + 300);
			pic += 400;
		}
		clock_now = pic + 1000;
		prop_catch_up(&p, pic);
		prop_sync(&p);
		CHECK(count() == BITS);
		if (!pass) {
			memcpy(hub, p.chip.hub, sizeof(hub));
			memcpy(seq, stamps, sizeof(seq));
			n = nstamps;
			now = p.chip.now;
		} else {
			CHECK(!memcmp(hub, p.chip.hub, sizeof(hub)));
			CHECK(nstamps == n && !memcmp(seq, stamps, sizeof(seq)));
			CHECK(p.chip.now == now);
			prop_stop_thread(&p);
			CHECK(p.worker == NULL);
		}
	}
	CHECK(n > 0);
}

static int npins;

static void count_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx; (void)t; (void)out; (void)dir;
	npins++;
}

/* the pins callback runs on a change of the pins in its mask (and once for the first change, which every device
   sees); echo drives P24 only */
static void pins_mask(void)
{
	const uint32_t masks[3] = { 0xFFFFFFFFu, 1u << 24, ~(1u << 24) };
	int k, calls[3];
	for (k = 0; k < 3; k++) {
		uint64_t pic = 200000;
		int b;
		boot();
		prop_set_pins(&p, count_pins, NULL);
		if (k) prop_set_pins_mask(&p, masks[k]);
		npins = 0;
		prop_catch_up(&p, pic);
		for (b = 0; b < BITS; b++) pulse(&pic, (b * 7 + 3) % 5 < 2);
		prop_catch_up(&p, pic);
		calls[k] = npins;
	}
	CHECK(calls[0] > 2 && calls[1] == calls[0] && calls[2] == 1);
}

static volatile int host_go;

static void *host_sync(void *arg)
{
	(void)arg;
	while (host_go) prop_sync(&p);
	return NULL;
}

/* another thread's prop_sync (a host reading NVRAM) while the emulation thread starts and stops the worker */
static void foreign_sync(void)
{
	int round, k;
	for (round = 0; round < 100; round++) {
		pthread_t h;
		uint64_t pic = 200000;
		boot();
		CHECK(prop_start_thread(&p) == 0);
		host_go = 1;
		CHECK(pthread_create(&h, NULL, host_sync, NULL) == 0);
		for (k = 0; k < 200; k++) { prop_catch_up(&p, pic); pulse(&pic, k & 1); }
		prop_stop_thread(&p);
		host_go = 0;
		pthread_join(h, NULL);
	}
}

static double wall_s(void)
{
#ifdef _WIN32
	LARGE_INTEGER f, c;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart / (double)f.QuadPart;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

static double cpu_s(void)
{
#ifdef _WIN32
	FILETIME a, b, k, u;
	GetThreadTimes(GetCurrentThread(), &a, &b, &k, &u);
	return (((uint64_t)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((uint64_t)u.dwHighDateTime << 32 | u.dwLowDateTime)) * 1e-7;
#else
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

static void nap_ms(int ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
#endif
}

static int slow_calls;
static volatile int slow_done;
#ifdef _WIN32
static int worker_prio;
#endif

static void slow_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx; (void)t; (void)out; (void)dir;
#ifdef _WIN32
	worker_prio = GetThreadPriority(GetCurrentThread());
#endif
	if (slow_calls++ == 0) { nap_ms(300); slow_done = 1; }
}

/* a sync that waits longer than the spin bound blocks instead of spinning, returns once the worker is done, and the
   worker keeps running afterwards */
static void blocking_sync(void)
{
	uint64_t pic = 200000;
	double c0, w0;
	int b;
	boot();
	prop_set_pins(&p, slow_pins, NULL);
	slow_calls = slow_done = 0;
	CHECK(prop_start_thread(&p) == 0);
	prop_catch_up(&p, pic);
	for (b = 0; b < 8; b++) pulse(&pic, b & 1);
	prop_catch_up(&p, pic);
	c0 = cpu_s();
	w0 = wall_s();
	prop_sync(&p);
	CHECK(slow_done && count() == 8);
	CHECK(wall_s() - w0 > 0.15);
	CHECK(cpu_s() - c0 < 0.05);
	for (b = 0; b < 8; b++) pulse(&pic, b & 1);
	prop_catch_up(&p, pic);
	prop_sync(&p);
	CHECK(count() == 16);
	prop_stop_thread(&p);
}

#ifdef _WIN32
/* the worker runs at the priority of the thread that started it */
static void priority(void)
{
	HANDLE me = GetCurrentThread();
	int old = GetThreadPriority(me);
	uint64_t pic = 200000;
	CHECK(SetThreadPriority(me, THREAD_PRIORITY_ABOVE_NORMAL));
	boot();
	prop_set_pins(&p, slow_pins, NULL);
	slow_calls = 1;
	worker_prio = -99;
	CHECK(prop_start_thread(&p) == 0);
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	prop_catch_up(&p, pic);
	prop_sync(&p);
	CHECK(worker_prio == THREAD_PRIORITY_ABOVE_NORMAL);
	prop_stop_thread(&p);
	SetThreadPriority(me, old);
}
#endif

static int load(const char *path, uint8_t *dst)
{
	FILE *f = fopen(path, "rb");
	size_t n;
	if (!f) { perror(path); return 0; }
	n = fread(dst, 1, 0x8000, f);
	fclose(f);
	return n == 0x8000;
}

int main(int argc, char **argv)
{
	if (argc != 3 || !load(argv[1], rom) || !load(argv[2], ram)) {
		fprintf(stderr, "usage: prop_test echo.rom echo.ram\n");
		return 2;
	}
	rates();
	echo_exact();
	clkset_lazy();
	full_log();
	reset_rebases(1);
	reset_rebases(0);
	clkset_reset_bit();
	threaded();
	pins_mask();
	foreign_sync();
	blocking_sync();
#ifdef _WIN32
	priority();
#endif
	printf("prop: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
