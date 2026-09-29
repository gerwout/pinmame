#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define L (1u << 15)
#define R (1u << 14)
#define DUTY(pin) ((6u << 26) | (pin))

static int fails;
static char last_log[128];
static int nlog;

#define CHECK(c) do { if (!(c)) { printf("AUDIO FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void logmsg(void *ctx, const char *msg) { (void)ctx; strncpy(last_log, msg, sizeof(last_log) - 1); nlog++; }

static void fresh(audio *a)
{
	audio_init(a, 48000.0, logmsg, NULL);
	nlog = 0;
	last_log[0] = 0;
}

static void duty_level(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L | R);
	audio_ctr(&a, 0, 3, 0, DUTY(15), 0x40000000u);
	CHECK(audio_level(&a, 0, 0, 1000) == 0.25);
	CHECK(audio_level(&a, 1, 0, 1000) == 0.0);
}

static void step_inside_sample(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L);
	audio_ctr(&a, 0, 3, 0, DUTY(15), 0);
	audio_ctr(&a, 300, 3, 0, DUTY(15), 0x80000000u);
	CHECK(audio_level(&a, 0, 0, 1000) == 0.35);
	CHECK(audio_level(&a, 0, 1000, 2000) == 0.5);
}

static void outa_and_dira(void)
{
	audio a;
	fresh(&a);
	audio_ctr(&a, 0, 3, 0, DUTY(15), 0x40000000u);
	audio_pins(&a, 0, 0, 0);
	CHECK(audio_level(&a, 0, 0, 100) == 0.0);
	audio_pins(&a, 100, L, L);
	CHECK(audio_level(&a, 0, 100, 200) == 1.0);
	audio_pins(&a, 200, 0, L);
	CHECK(audio_level(&a, 0, 200, 300) == 0.25);
}

static void dc_blocker_step(void)
{
	audio a;
	int16_t out[2 * 6];
	double r = exp(-2.0 * 3.14159265358979323846 * 10.0 / 48000.0);
	int j;
	fresh(&a);
	audio_pins(&a, 0, L, L);
	audio_render(&a, out, 6, 6000);
	for (j = 0; j < 6; j++) {
		CHECK(out[2 * j] == (int16_t)floor(pow(r, j) * 32767.0 + 0.5));
		CHECK(out[2 * j + 1] == 0);
	}
}

static void unexpected_mode(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L);
	audio_ctr(&a, 0, 2, 1, (4u << 26) | 15, 0x40000000u);
	CHECK(audio_level(&a, 0, 0, 100) == 0.0);
	CHECK(nlog == 1);
	CHECK(strstr(last_log, "counter mode 4 on P15") != NULL);
	audio_ctr(&a, 100, 2, 1, (4u << 26) | 15, 0x50000000u);
	CHECK(nlog == 1);
}

static void two_drivers(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, R);
	audio_ctr(&a, 0, 1, 0, DUTY(14), 0x40000000u);
	audio_ctr(&a, 0, 5, 1, DUTY(14), 0x40000000u);
	CHECK(audio_level(&a, 1, 0, 100) == 0.5);
	CHECK(nlog == 1);
	CHECK(strstr(last_log, "2 counters drive P14") != NULL);
}

static void differential_on_bpin(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L);
	audio_ctr(&a, 0, 2, 0, (7u << 26) | (15u << 9) | 3, 0x40000000u);
	CHECK(audio_level(&a, 0, 0, 100) == 0.0);
	CHECK(strstr(last_log, "counter mode 7 on P15") != NULL);
}

static void other_pins_ignored(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L);
	audio_ctr(&a, 0, 4, 0, DUTY(15), 0x40000000u);
	audio_ctr(&a, 50, 3, 0, (4u << 26) | 1, 0x01000000u);
	CHECK(a.last_t == 0);
	CHECK(audio_level(&a, 0, 0, 100) == 0.25);
	CHECK(nlog == 0);
}

int main(void)
{
	differential_on_bpin();
	other_pins_ignored();
	duty_level();
	step_inside_sample();
	outa_and_dira();
	dc_blocker_step();
	unexpected_mode();
	two_drivers();
	printf("audio unit: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
