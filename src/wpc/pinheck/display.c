#include "display.h"
#include <stdio.h>
#include <string.h>

#define PINS (DISPLAY_P17 | DISPLAY_P20 | DISPLAY_P21 | DISPLAY_P22)

static void say(display *d, int *once, const char *msg)
{
	if (*once) return;
	*once = 1;
	if (d->log) d->log(d->ctx, msg);
}

static void latch(display *d, uint64_t t, int cfg)
{
	char msg[80];
	long n = d->nbits / 8;

	if (d->nbits == 0) return;
	if (d->nbits % 8) {
		sprintf(msg, "display: latch after %ld bits, discarded", d->nbits);
		say(d, &d->logged_bits, msg);
	} else if (d->mode != cfg) {
		sprintf(msg, "display: mode changed during a %ld-byte transfer, discarded", n);
		say(d, &d->logged_mixed, msg);
	} else if (cfg) {
		if (n > DISPLAY_CFG_MAX) {
			sprintf(msg, "display: %ld-byte config packet, first %d kept", n, DISPLAY_CFG_MAX);
			say(d, &d->logged_cfg, msg);
			n = DISPLAY_CFG_MAX;
		}
		if (d->on_config) d->on_config(d->ctx, d->buf, (int)n, t);
	} else if (n != DISPLAY_FRAME) {
		sprintf(msg, "display: frame of %ld bytes discarded", n);
		say(d, &d->logged_frame, msg);
	} else if (d->on_frame)
		d->on_frame(d->ctx, d->buf, t);
	d->nbits = 0;
}

void pinheck_display_init(display *d, void *ctx, display_frame_fn on_frame, display_config_fn on_config, display_log_fn log)
{
	memset(d, 0, sizeof(*d));
	d->ctx = ctx;
	d->on_frame = on_frame;
	d->on_config = on_config;
	d->log = log;
}

void pinheck_display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir)
{
	uint32_t now = out & dir & PINS, old = d->level;

	d->level = now;
	if ((now & DISPLAY_P20) && !(old & DISPLAY_P20)) {
		latch(d, t, (now & DISPLAY_P17) != 0);
		return;
	}
	if ((now & DISPLAY_P22) && !(old & DISPLAY_P22) && !(now & DISPLAY_P20)) {
		long i = d->nbits >> 3;
		if (d->nbits == 0) d->mode = (now & DISPLAY_P17) != 0;
		if (i < DISPLAY_FRAME) {
			if ((d->nbits & 7) == 0) d->buf[i] = 0;
			if (now & DISPLAY_P21) d->buf[i] |= (uint8_t)(0x80 >> (d->nbits & 7));
		}
		d->nbits++;
	}
}
