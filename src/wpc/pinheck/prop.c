#include "prop.h"
#include <stdio.h>
#include <string.h>

#define PIN_DO   (1u << 0)
#define PIN_SCLK (1u << 1)
#define PIN_DI   (1u << 2)
#define PIN_CS   (1u << 3)
#define PIN_PGM  (1u << 13)
#define PIN_P24  (1u << 24)
#define PIN_P25  (1u << 25)
#define PIN_SCL  (1u << 28)
#define PIN_SDA  (1u << 29)
#define PIN_SND  ((1u << 14) | (1u << 15))

static const uint32_t rate_num[8] = { 3, 1, 13, 13, 13, 13, 13, 13 };
static const uint32_t rate_den[8] = { 20, 4000, 160, 160, 80, 40, 20, 10 };

static const prop_seg *seg_pic(const pinheck_prop *p, uint64_t a)
{
	int k = p->nseg - 1;
	while (k > 0 && p->seg[k].pic0 > a) k--;
	return &p->seg[k];
}

static uint64_t to_prop(const pinheck_prop *p, uint64_t a)
{
	const prop_seg *s = seg_pic(p, a);
	if (a < s->pic0) return s->prop0;
	return s->prop0 + (a - s->pic0) * s->num / s->den;
}

static uint64_t to_pic(const pinheck_prop *p, uint64_t t)
{
	int k = p->nseg - 1;
	const prop_seg *s;
	while (k > 0 && p->seg[k].prop0 > t) k--;
	s = &p->seg[k];
	if (t < s->prop0) return s->pic0;
	return s->pic0 + ((t - s->prop0) * s->den + s->num - 1) / s->num;
}

/* the clock segments changed: convert the queued edges again */
static void retime(pinheck_prop *p)
{
	int k;
	for (k = 0; k < p->count; k++) {
		prop_edge *e = &p->edge[(p->head + k) % PROP_EDGES];
		e->prop = to_prop(p, e->pic);
	}
}

static void add_seg(pinheck_prop *p, uint64_t t, uint8_t cfg)
{
	uint64_t a = to_pic(p, t);
	if (p->nseg == PROP_SEGS) {
		memmove(p->seg, p->seg + 1, sizeof(p->seg[0]) * (PROP_SEGS - 1));
		p->nseg--;
	}
	p->seg[p->nseg].pic0 = a;
	p->seg[p->nseg].prop0 = t;
	p->seg[p->nseg].num = rate_num[cfg & 7];
	p->seg[p->nseg].den = rate_den[cfg & 7];
	p->nseg++;
	retime(p);
}

static uint32_t pins_in(void *ctx, uint64_t t)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	uint32_t v = p->base_pins;
	int k;
	for (k = 0; k < p->count; k++) {
		const prop_edge *e = &p->edge[(p->head + k) % PROP_EDGES];
		if (e->prop > t) break;
		v = e->pins;
	}
	return v | p->ee_bits | (p->sd_do ? PIN_DO : 0) | PIN_CS | PIN_PGM;
}

static uint64_t pins_next(void *ctx, uint64_t t)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	int k;
	for (k = 0; k < p->count; k++) {
		uint64_t e = p->edge[(p->head + k) % PROP_EDGES].prop;
		if (e > t) return e;
	}
	return P8X32A_NEVER;
}

/* each device sees a change of its own pins; the EEPROM, SD card, UART and sound only act on those */
static void pins_out(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	uint32_t ch = p->po_ok ? (out ^ p->po_out) | (dir ^ p->po_dir) : 0xFFFFFFFFu;
	p->po_out = out;
	p->po_dir = dir;
	p->po_ok = 1;
	if (ch & PIN_P25) {
		int tx = (dir & PIN_P25) ? (out & PIN_P25) != 0 : 1;
		if (p->tx && tx != p->tx_level) {
			p->tx_level = tx;
			p->tx(p->tx_ctx, to_pic(p, t), tx);
		}
	}
	if (p->snd_pins && (ch & PIN_SND)) p->snd_pins(p->snd_ctx, t, out, dir);
	if (ch & (PIN_SCL | PIN_SDA)) {
		int scl = (dir & PIN_SCL) ? (out & PIN_SCL) != 0 : 1;
		int sda = (dir & PIN_SDA) ? (out & PIN_SDA) != 0 : 1;
		p->ee_bits = PIN_SCL | (cat24m01_update(&p->eeprom, scl, sda) ? PIN_SDA : 0);
	}
	if (p->sd && (ch & (PIN_CS | PIN_SCLK | PIN_DI))) {
		int cs = (dir & PIN_CS) ? (out & PIN_CS) != 0 : 1;
		int sclk = (dir & PIN_SCLK) ? (out & PIN_SCLK) != 0 : 0;
		int mosi = (dir & PIN_DI) ? (out & PIN_DI) != 0 : 1;
		p->sd_do = p->sd(p->sd_ctx, cs, sclk, mosi) != 0;
	}
	if (p->pins) p->pins(p->pins_ctx, t, out, dir);
}

static void ctr_state(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	if (p->snd_ctr) p->snd_ctr(p->snd_ctx, t, cog, ctr, ctr_reg, frq);
}

static void clkset(void *ctx, uint64_t t, uint8_t cfg)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	char msg[24];
	add_seg(p, t, cfg);
	if (p->log) {
		sprintf(msg, "prop: CLKSET %02x", (unsigned)cfg);
		p->log(p->log_ctx, msg);
	}
	if (cfg & 0x80) p->reset_pending = 1;
}

static void logmsg(void *ctx, const char *msg)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	if (p->log) p->log(p->log_ctx, msg);
}

static void restart(pinheck_prop *p, uint64_t t)
{
	uint64_t a = to_pic(p, t);
	p8x32a_reset(&p->chip, t);
	p->nseg = 1;
	p->seg[0].pic0 = a;
	p->seg[0].prop0 = t;
	p->seg[0].num = rate_num[0];
	p->seg[0].den = rate_den[0];
	cat24m01_init(&p->eeprom, p->eemem, 0);
	p->ee_bits = PIN_SCL | PIN_SDA;
	p->sd_do = 1;
	p->po_ok = 0;
	p->reset_pending = 0;
	retime(p);
}

void prop_init(pinheck_prop *p, const uint8_t *rom32k, uint8_t *eemem)
{
	p8x32a_bus bus;
	memset(p, 0, sizeof(*p));
	bus.ctx = p;
	bus.pins_in = pins_in;
	bus.pins_next = pins_next;
	bus.pins_out = pins_out;
	bus.cog_start = NULL;
	bus.clkset = clkset;
	bus.log = logmsg;
	bus.ctr_state = ctr_state;
	bus.pure_in = ~(PIN_DO | PIN_SDA); /* SD DO and EEPROM SDA answer inside pins_out */
	p8x32a_init(&p->chip, &bus);
	memcpy(p->chip.hub + 0x8000, rom32k, 0x8000);
	p->eemem = eemem;
	p->nseg = 1;
	p->seg[0].num = rate_num[0];
	p->seg[0].den = rate_den[0];
	cat24m01_init(&p->eeprom, eemem, 0);
	p->ee_bits = PIN_SCL | PIN_SDA;
	p->sd_do = 1;
}

void prop_attach_sd(pinheck_prop *p, prop_spi_fn fn, void *ctx)
{
	p->sd = fn;
	p->sd_ctx = ctx;
	p->sd_do = 1;
	p->po_ok = 0;
}

void prop_set_log(pinheck_prop *p, prop_log_fn fn, void *ctx)
{
	p->log = fn;
	p->log_ctx = ctx;
}

void prop_set_tx(pinheck_prop *p, prop_tx_fn fn, void *ctx)
{
	p->tx = fn;
	p->tx_ctx = ctx;
	p->po_ok = 0;
	p->tx_level = 1;
}

void prop_set_pins(pinheck_prop *p, prop_pins_fn fn, void *ctx)
{
	p->pins = fn;
	p->pins_ctx = ctx;
}

void prop_set_sound(pinheck_prop *p, prop_ctr_fn ctr, prop_pins_fn pins, void *ctx)
{
	p->snd_ctr = ctr;
	p->snd_pins = pins;
	p->snd_ctx = ctx;
	p->po_ok = 0;
}

void prop_reset(pinheck_prop *p, uint64_t pic_cycle)
{
	prop_catch_up(p, p->pic_last);
	p->pic_last = pic_cycle;
	p->head = p->count = 0;
	p->base_pins = p->last_pins = 0;
	restart(p, p->chip.now);
	p->seg[0].pic0 = pic_cycle;
	retime(p);
}

uint64_t prop_time(const pinheck_prop *p, uint64_t pic_cycle)
{
	return to_prop(p, pic_cycle);
}

void prop_catch_up(pinheck_prop *p, uint64_t pic_cycle)
{
	uint64_t t;
	if (pic_cycle > p->pic_last) p->pic_last = pic_cycle;
	while ((t = to_prop(p, pic_cycle)) > p->chip.now + 1) {
		p8x32a_run_until(&p->chip, t - 1);
		if (p->reset_pending) restart(p, p->chip.now);
	}
	while (p->count && p->edge[p->head].prop <= p->chip.now) {
		p->base_pins = p->edge[p->head].pins;
		p->head = (p->head + 1) % PROP_EDGES;
		p->count--;
	}
}

void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins)
{
	pins &= PROP_PIC_PINS;
	if (pic_cycle > p->pic_last) p->pic_last = pic_cycle;
	if (pins == p->last_pins) return;
	if (p->count == PROP_EDGES) prop_catch_up(p, pic_cycle);
	p->edge[(p->head + p->count) % PROP_EDGES].pic = pic_cycle;
	p->edge[(p->head + p->count) % PROP_EDGES].pins = pins;
	p->edge[(p->head + p->count) % PROP_EDGES].prop = to_prop(p, pic_cycle);
	p->count++;
	p->last_pins = pins;
}

int prop_p24(pinheck_prop *p, uint64_t pic_cycle)
{
	uint32_t dir, out;
	prop_catch_up(p, pic_cycle);
	out = p8x32a_pins(&p->chip, p->chip.now, &dir);
	return (dir & PIN_P24) ? (out & PIN_P24) != 0 : 0;
}
