#include "p8x32a.h"
#include <stdio.h>
#include <string.h>

enum { EV_NONE, EV_HUB, EV_EXEC, EV_DONE, EV_WAITPIN, EV_RESTART, EV_SLEEP };
enum { LOOP_SEARCH, LOOP_RECORD, LOOP_SLEEP };

#define OP(i)   ((unsigned)((i) >> 26))
#define FWZ(i)  (((i) >> 25) & 1)
#define FWC(i)  (((i) >> 24) & 1)
#define FWR(i)  (((i) >> 23) & 1)
#define FIM(i)  (((i) >> 22) & 1)
#define COND(i) (((i) >> 18) & 15)
#define DST(i)  (((i) >> 9) & 511)
#define SRC(i)  ((i) & 511)

enum {
	LOG_WAITVID = 1, LOG_CTR_MODE = 2, LOG_CTR_OUT = 4, LOG_REBOOT = 8
};

static const uint8_t unscr[32] = {
	10, 26, 24, 29, 27, 13, 22, 28, 2, 25, 18, 9, 5, 16, 31, 23,
	1, 30, 14, 0, 11, 8, 15, 20, 17, 4, 19, 6, 12, 21, 7, 3
};

static void log_once(p8x32a *p, uint32_t what, const char *msg)
{
	if (p->logged & what) return;
	p->logged |= what;
	if (p->bus.log) p->bus.log(p->bus.ctx, msg);
}

static uint32_t unscramble(uint32_t w)
{
	uint32_t r = 0;
	int k;
	for (k = 0; k < 32; k++) r |= ((w >> unscr[k]) & 1u) << k;
	return r;
}

static uint32_t rd32(const p8x32a *p, uint32_t a)
{
	a &= 0xFFFC;
	return p->hub[a] | (uint32_t)p->hub[a + 1] << 8 | (uint32_t)p->hub[a + 2] << 16 | (uint32_t)p->hub[a + 3] << 24;
}

static uint32_t regval(const p8x32a_reg *r, uint64_t t) { return t >= r->at ? r->cur : r->prev; }

static void loop_notify(p8x32a *p, uint64_t t, uint32_t pins);

/* a pin change at t; pins = the pins it may change */
static void add_pending(p8x32a *p, uint64_t t, uint32_t pins)
{
	int k;
	if (p->sleepers) loop_notify(p, t, pins);
	for (k = 0; k < p->npend; k++)
		if (p->pend[k] == t) { p->pend_pins[k] |= pins; return; }
	if (p->npend < (int)(sizeof(p->pend) / sizeof(p->pend[0]))) { p->pend_pins[p->npend] = pins; p->pend[p->npend++] = t; }
}

static void flush(p8x32a *p, uint64_t t);

static void regset(p8x32a *p, p8x32a_reg *r, uint32_t v, uint64_t at)
{
	uint32_t pins = (r->cur ^ v) | (r->prev ^ v);
	flush(p, p->now);
	if (at > r->at) r->prev = r->cur;
	r->cur = v;
	r->at = at;
	add_pending(p, at, pins);
}

static int nco(uint32_t ctr)
{
	unsigned m = (ctr >> 26) & 31;
	return m == 4 || m == 5;
}

static uint32_t nco_pins(uint32_t ctr)
{
	if (!nco(ctr)) return 0;
	return 1u << (ctr & 31) | (((ctr >> 26) & 31) == 5 ? 1u << ((ctr >> 9) & 31) : 0);
}

static uint32_t ctr_pins(const p8x32a_cog *c, int k, uint64_t t)
{
	int old = t < c->ctr_at[k];
	uint32_t ctr = old ? c->ctr_old[k] : c->ctr[k], x, a, r;
	uint32_t frq = old ? c->frq_old[k] : c->frq[k], ph = old ? c->phs_old[k] : c->phs[k];
	uint64_t pt = old ? c->phs_t_old[k] : c->phs_t[k];
	if (!nco(ctr)) return 0;
	x = t <= pt ? ph : ph + frq * (uint32_t)(t - pt);
	a = x >> 31;
	r = a << (ctr & 31);
	if (((ctr >> 26) & 31) == 5) r |= (a ^ 1) << ((ctr >> 9) & 31);
	return r;
}

static uint64_t nco_toggle(uint32_t ctr, uint32_t f, uint32_t ph, uint64_t pt, uint64_t t)
{
	uint32_t x;
	uint64_t dt;
	if (!nco(ctr)) return P8X32A_NEVER;
	if (t < pt) return pt;
	if (!f) return P8X32A_NEVER;
	x = (ph + f * (uint32_t)(t - pt)) & 0x7FFFFFFFu;
	if (f < 0x80000000u) dt = ((uint64_t)0x80000000u - x + f - 1) / f;
	else dt = (uint64_t)x / (0x100000000ull - f) + 1;
	return t + dt;
}

static uint64_t ctr_toggle(const p8x32a_cog *c, int k, uint64_t t)
{
	if (t < c->ctr_at[k]) {
		uint64_t o = nco_toggle(c->ctr_old[k], c->frq_old[k], c->phs_old[k], c->phs_t_old[k], t);
		return o < c->ctr_at[k] ? o : c->ctr_at[k];
	}
	return nco_toggle(c->ctr[k], c->frq[k], c->phs[k], c->phs_t[k], t);
}

/* the next counter pin change after t; cached until a counter changes or t reaches it */
static uint64_t ctr_next(p8x32a *p, uint64_t t)
{
	uint64_t nt = P8X32A_NEVER, e;
	int n, k;
	if (p->ctr_ok && t >= p->ctr_from && t < p->ctr_nt) return p->ctr_nt;
	for (n = 0; n < 8; n++)
		for (k = 0; k < 2; k++)
			if ((e = ctr_toggle(&p->cog[n], k, t)) < nt) nt = e;
	p->ctr_ok = 1;
	p->ctr_from = t;
	p->ctr_nt = nt;
	return nt;
}

uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir)
{
	uint32_t o = 0, d = 0;
	int n;
	for (n = 0; n < 8; n++) {
		p8x32a_cog *c = &p->cog[n];
		uint32_t dd = regval(&c->dira, t);
		if (!dd) continue;
		d |= dd;
		o |= (regval(&c->outa, t) | ctr_pins(c, 0, t) | ctr_pins(c, 1, t)) & dd;
	}
	*dir = d;
	return o;
}

static void flush(p8x32a *p, uint64_t t)
{
	for (;;) {
		int k, best = -1;
		uint32_t out, dir;
		uint64_t when = ctr_next(p, p->flushed);
		for (k = 0; k < p->npend; k++)
			if (p->pend[k] <= t && (best < 0 || p->pend[k] < p->pend[best])) best = k;
		if (best >= 0 && p->pend[best] < when) when = p->pend[best];
		if (when > t) break;
		out = p8x32a_pins(p, when, &dir);
		if ((out != p->last_out || dir != p->last_dir) && p->bus.pins_out) p->bus.pins_out(p->bus.ctx, when, out, dir);
		p->last_out = out;
		p->last_dir = dir;
		if (when > p->flushed) p->flushed = when;
		for (k = 0; k < p->npend; k++)
			if (p->pend[k] == when) { --p->npend; p->pend[k] = p->pend[p->npend]; p->pend_pins[k] = p->pend_pins[p->npend]; k--; }
	}
	if (t > p->flushed) p->flushed = t;
}

/* after flush(t) the pins at t are the last flushed state */
static uint32_t ina(p8x32a *p, uint64_t t)
{
	uint32_t ext;
	flush(p, t);
	ext = p->bus.pins_in ? p->bus.pins_in(p->bus.ctx, t) : 0;
	return (p->last_dir & p->last_out) | (~p->last_dir & ext);
}

static uint32_t cnt(const p8x32a *p, uint64_t t) { return (uint32_t)(t - p->cnt_base); }

static int ctr_free(uint32_t ctr)
{
	unsigned m = (ctr >> 26) & 31;
	return (m >= 1 && m <= 7) || m == 31;
}

static uint32_t phs_at(const p8x32a_cog *c, int k, uint64_t t)
{
	if (!ctr_free(c->ctr[k]) || t <= c->phs_t[k]) return c->phs[k];
	return c->phs[k] + c->frq[k] * (uint32_t)(t - c->phs_t[k]);
}

static void ctr_rebase(p8x32a_cog *c, int k, uint64_t t)
{
	c->phs[k] = phs_at(c, k, t);
	c->phs_t[k] = t;
}

static void ctr_check(p8x32a *p, uint32_t ctr)
{
	unsigned m = (ctr >> 26) & 31;
	if ((m >= 8 && m <= 15) || (m >= 17 && m <= 30)) log_once(p, LOG_CTR_MODE, "p8x32a: pin-sensing counter mode not modelled");
	if (m >= 2 && m <= 3) log_once(p, LOG_CTR_OUT, "p8x32a: counter PLL pin outputs not modelled (Plan 6)");
}

static void ctr_save(p8x32a *p, p8x32a_cog *c, int k, uint64_t e)
{
	flush(p, p->now);
	p->ctr_ok = 0;
	add_pending(p, e, nco_pins(c->ctr[k]));
	c->ctr_old[k] = c->ctr[k];
	c->frq_old[k] = c->frq[k];
	c->phs_old[k] = c->phs[k];
	c->phs_t_old[k] = c->phs_t[k];
	c->ctr_at[k] = e;
}

static void ctr_notify(p8x32a *p, int n, int k, uint64_t t)
{
	p8x32a_cog *c = &p->cog[n];
	if (c->ctr[k] == c->ctr_seen[k] && c->frq[k] == c->frq_seen[k]) return;
	c->ctr_seen[k] = c->ctr[k];
	c->frq_seen[k] = c->frq[k];
	if (p->bus.ctr_state) p->bus.ctr_state(p->bus.ctx, t, n, k, c->ctr[k], c->frq[k]);
}

static void special_write(p8x32a *p, int n, unsigned a, uint32_t v, uint64_t m3)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t e = m3 + 1;
	int k = a & 1;
	switch (a) {
	case 0x1F4: regset(p, &c->outa, v, e); break;
	case 0x1F6: if (e < c->disable_at) regset(p, &c->dira, v, e); break;
	case 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; add_pending(p, e, nco_pins(v)); ctr_check(p, v); ctr_notify(p, n, k, e); break;
	case 0x1FA: case 0x1FB: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->frq[k] = v; ctr_notify(p, n, k, e); break;
	case 0x1FC: case 0x1FD: ctr_save(p, c, k, e); c->phs[k] = v; c->phs_t[k] = e; break;
	case 0x1FE: c->vcfg = v; break;
	case 0x1FF: c->vscl = v; break;
	}
}

static uint32_t sread(p8x32a *p, int n, unsigned a, uint64_t t)
{
	p8x32a_cog *c = &p->cog[n];
	switch (a) {
	case 0x1F0: return (c->ptr >> 14) << 2;
	case 0x1F1: return cnt(p, t);
	case 0x1F2: return ina(p, t);
	case 0x1FC: return phs_at(c, 0, t);
	case 0x1FD: return phs_at(c, 1, t);
	}
	return c->ram[a];
}

static uint32_t bitrev(uint32_t x)
{
	x = (x >> 1 & 0x55555555u) | (x & 0x55555555u) << 1;
	x = (x >> 2 & 0x33333333u) | (x & 0x33333333u) << 2;
	x = (x >> 4 & 0x0F0F0F0Fu) | (x & 0x0F0F0F0Fu) << 4;
	x = (x >> 8 & 0x00FF00FFu) | (x & 0x00FF00FFu) << 8;
	return x >> 16 | x << 16;
}

static int parity(uint32_t x)
{
	x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
	return (int)(x & 1);
}

/* the P1 ALU: hub ops (group 0), rotates (1), logic (2-3) and adder (4-7) results with their C and write flags */
static uint32_t alu(unsigned i, uint32_t s, uint32_t d, unsigned pc, int run, int ci, int zi,
                    uint32_t bus_q, int bus_c, int *wr, int *co, int *zo)
{
	unsigned g = (i >> 3) & 7;
	uint32_t r, log_r = s;

	*wr = 1;
	if (g == 0) {
		r = (run || (pc >> 4) != 31) ? bus_q : 0;
		*co = bus_c;
	} else if (g == 1) {
		uint32_t dr = (i & 1) ? bitrev(d) : 0, fill;
		uint64_t rot;
		switch (i & 7) {
		case 0: fill = d & 0x7FFFFFFFu; break;
		case 1: fill = dr & 0x7FFFFFFFu; break;
		case 4: case 5: fill = ci ? 0x7FFFFFFFu : 0; break;
		case 6: fill = (d >> 31) ? 0x7FFFFFFFu : 0; break;
		default: fill = 0; break;
		}
		rot = (((uint64_t)fill << 32) | ((i & 1) ? dr : d)) >> (s & 31);
		r = (((i >> 1) & 3) != 3 && (i & 1)) ? bitrev((uint32_t)rot) : (uint32_t)rot;
		*co = (((i >> 1) & 3) != 3 && (i & 1)) ? (int)(dr & 1) : (int)(d & 1);
	} else {
		if (g <= 3) {
			if (i & 8) {
				unsigned ls = (i & 4) ? ((unsigned)(((i & 2) ? zi : ci) ^ (int)(i & 1))) << 1
				                      : (((i >> 1) & 1) << 1) | (unsigned)!(((i >> 1) ^ i) & 1);
				switch (ls) {
				case 0: log_r = d & ~s; break;
				case 1: log_r = d & s; break;
				case 2: log_r = d | s; break;
				default: log_r = d ^ s; break;
				}
			} else if (i & 4) {
				switch (i & 3) {
				case 0: log_r = (d & 0xFFFFFE00u) | (s & 511); break;
				case 1: log_r = (d & 0xFFFC01FFu) | ((s & 511) << 9); break;
				case 2: log_r = ((s & 511) << 23) | (d & 0x007FFFFFu); break;
				default: log_r = (d & 0xFFFFFE00u) | (pc & 511); break;
				}
			}
		}
		if (g == 3) {
			r = log_r;
			*co = parity(log_r);
		} else {
			uint32_t add_d, add_s, sum_lo, add_r;
			int add_sub, add_ci, add_co, add_cm, add_cs, add_c, cin, c30, b31;
			if (g == 4 || g == 5) {
				int ads[4];
				ads[0] = 0; ads[1] = (int)(s >> 31); ads[2] = ci; ads[3] = zi;
				add_sub = ads[(i >> 1) & 3] ^ (int)(i & 1);
			} else if (i == 0x32 || i == 0x34 || i == 0x36 || (i >> 2) == 0xF)
				add_sub = 0;
			else
				add_sub = 1;
			add_ci = ((g == 6 && ((i & 7) == 1 || (i & 2))) && ci) || (((i >> 3) & 3) == 3 && (i & 3) == 1);
			add_d = (((i >> 3) & 3) == 1) ? 0 : d;
			add_s = ((i & 31) == 0x19 || ((i >> 1) & 15) == 0xD) ? 0xFFFFFFFFu : add_sub ? ~s : s;
			cin = add_ci ^ add_sub;
			sum_lo = (add_d & 0x7FFFFFFFu) + (add_s & 0x7FFFFFFFu) + (uint32_t)cin;
			c30 = (int)(sum_lo >> 31);
			b31 = (int)(add_d >> 31) + (int)(add_s >> 31) + c30;
			add_r = (sum_lo & 0x7FFFFFFFu) | ((uint32_t)(b31 & 1) << 31);
			add_co = b31 >> 1;
			add_cm = c30;
			add_cs = add_co ^ (int)(add_d >> 31) ^ (int)(add_s >> 31);
			if (i == 0x38) add_c = add_co;
			else if (g == 5) add_c = (int)(s >> 31);
			else if ((i & 0x20) && ((i >> 2) & 3) == 1) add_c = add_co ^ add_cm;
			else if (((i >> 1) & 15) == 8) add_c = add_cs;
			else add_c = add_co ^ add_sub;
			if ((i >> 2) == 4) *wr = (int)(i & 1) ^ ((i & 2) ? !add_co : add_cs);
			else if (i == 0x38) *wr = add_co;
			r = (i & 0x20) ? add_r : log_r;
			*co = add_c;
		}
	}
	*zo = !r && (zi || !(g == 6 && ((i & 7) == 1 || (i & 2))));
	return r;
}

/* Idle loops: an iteration that changes nothing and ends in the state it began in repeats while its inputs
   stay the same; the cog sleeps, and the recorded iteration gives its state when an input may have changed. */
#define SNAP(c) ((unsigned char *)(c) + offsetof(p8x32a_cog, ptr))

static uint32_t ror32(uint32_t x, unsigned n) { return n ? x >> n | x << (32 - n) : x; }

/* the common ALU ops of a running cog (not hub ops); the others go to alu() */
static uint32_t alu_run(unsigned i, uint32_t s, uint32_t d, unsigned pc, int ci, int zi, int bus_c, int *wr, int *co, int *zo)
{
	uint32_t r;
	unsigned sh = s & 31;
	*wr = 1;
	switch (i) {
	case 0x08: r = ror32(d, sh); *co = (int)(d & 1); break;
	case 0x09: r = ror32(d, (32 - sh) & 31); *co = (int)(d >> 31); break;
	case 0x0A: r = d >> sh; *co = (int)(d & 1); break;
	case 0x0B: r = d << sh; *co = (int)(d >> 31); break;
	case 0x14: r = (d & 0xFFFFFE00u) | (s & 511); *co = d < s; break;
	case 0x15: r = (d & 0xFFFC01FFu) | ((s & 511) << 9); *co = d < s; break;
	case 0x17: r = (d & 0xFFFFFE00u) | (pc & 511); *co = d < s; break;
	case 0x18: r = d & s; *co = parity(r); break;
	case 0x19: r = d & ~s; *co = parity(r); break;
	case 0x1A: r = d | s; *co = parity(r); break;
	case 0x1B: r = d ^ s; *co = parity(r); break;
	case 0x1C: r = ci ? d | s : d & ~s; *co = parity(r); break;
	case 0x1D: r = ci ? d & ~s : d | s; *co = parity(r); break;
	case 0x1E: r = zi ? d | s : d & ~s; *co = parity(r); break;
	case 0x1F: r = zi ? d & ~s : d | s; *co = parity(r); break;
	case 0x20: r = d + s; *co = r < d; break;
	case 0x21: r = d - s; *co = d < s; break;
	case 0x28: r = s; *co = (int)(s >> 31); break;
	case 0x39: r = d - 1; *co = !d; break;
	default: return alu(i, s, d, pc, 1, ci, zi, 0, bus_c, wr, co, zo);
	}
	*zo = !r;
	return r;
}

static void loop_reset(p8x32a_loop *l)
{
	l->state = LOOP_SEARCH;
	l->edge = l->dirty = l->hub = 0;
	l->head = 0xFFFF;
	l->nins = 0;
}

/* a pin change at t reaches sleepers whose inputs it may touch */
static void loop_notify(p8x32a *p, uint64_t t, uint32_t pins)
{
	int n;
	for (n = 0; n < 8; n++)
		if ((p->sleepers >> n & 1) && (pins & p->loop[n].wake) && t < p->cog[n].ev_t) p->cog[n].ev_t = t;
}

static void loop_hub_write(p8x32a *p, int writer, unsigned a, unsigned sz, uint64_t h)
{
	int n, k;
	for (n = 0; n < 8; n++) {
		p8x32a_loop *l = &p->loop[n];
		if (!(p->sleepers >> n & 1) || n == writer) continue;
		for (k = 0; k < l->nhub; k++)
			if (a < (unsigned)l->hub_a[k] + l->hub_n[k] && l->hub_a[k] < a + sz && h < p->cog[n].ev_t) p->cog[n].ev_t = h;
	}
}

static void loop_hub_access(p8x32a *p, int n, uint32_t a, unsigned op)
{
	p8x32a_loop *l = &p->loop[n];
	unsigned sz = op == 2 ? 4 : op == 1 ? 2 : 1;
	if (l->nhub == 4) { l->dirty = 1; return; }
	l->hub_a[l->nhub] = (uint16_t)(a & ~(sz - 1));
	l->hub_n[l->nhub] = (uint8_t)sz;
	l->hub_v[l->nhub] = sz == 4 ? rd32(p, a) : sz == 2 ? (uint32_t)(p->hub[a & ~1u] | p->hub[(a & ~1u) + 1] << 8) : p->hub[a];
	l->nhub++;
	l->hub = 1;
}

static uint32_t loop_hub_now(const p8x32a *p, const p8x32a_loop *l, int k)
{
	unsigned a = l->hub_a[k];
	return l->hub_n[k] == 4 ? rd32(p, a) : l->hub_n[k] == 2 ? (uint32_t)(p->hub[a] | p->hub[a + 1] << 8) : p->hub[a];
}

/* do the sleeper's inputs at t still read as recorded? */
static int loop_same(p8x32a *p, int n, uint64_t t)
{
	p8x32a_loop *l = &p->loop[n];
	int k;
	if (l->nin) {
		uint32_t v = ina(p, t);
		for (k = 0; k < l->nin; k++)
			if ((v & l->in_mask[k]) != l->in_val[k]) return 0;
	}
	for (k = 0; k < l->nhub; k++)
		if (loop_hub_now(p, l, k) != l->hub_v[k]) return 0;
	return 1;
}

/* the earliest time after t at which a pin the sleeper reads may change, or the horizon */
static uint64_t loop_bound(p8x32a *p, int n, uint64_t t)
{
	uint32_t m = p->loop[n].wake;
	uint64_t w = p->horizon == P8X32A_NEVER ? P8X32A_NEVER : p->horizon + 1, e;
	int k, j;
	if (m) {
		for (k = 0; k < p->npend; k++)
			if (p->pend[k] > t && p->pend[k] < w && (p->pend_pins[k] & m)) w = p->pend[k];
		for (k = 0; k < 8; k++)
			for (j = 0; j < 2; j++) {
				const p8x32a_cog *c = &p->cog[k];
				if (!((nco_pins(c->ctr[j]) | (t < c->ctr_at[j] ? nco_pins(c->ctr_old[j]) : 0)) & m)) continue;
				if ((e = ctr_toggle(c, j, t)) > t && e < w) w = e;
			}
		if (p->bus.pins_next && (e = p->bus.pins_next(p->bus.ctx, t)) < w) w = e;
	}
	return w > t ? w : t + 1;
}

/* wake a sleeper at w: restore its state before its first event at or after w */
static void loop_resume(p8x32a *p, int n, uint64_t w)
{
	p8x32a_cog *c = &p->cog[n];
	p8x32a_loop *l = &p->loop[n];
	uint64_t rel = w > l->t0 ? w - l->t0 : 0, k = rel / l->period, off = rel % l->period, shift;
	int j = 0;
	while (j < l->nsnap && l->snap_t[j] - l->snap_t[0] < off) j++;
	if (j == l->nsnap) { j = 0; k++; }
	shift = l->t0 - l->snap_t[0] + k * l->period;
	memcpy(SNAP(c), l->snap[j], P8X32A_SNAP);
	c->ev_t += shift;
	c->t0 += shift;
	if (l->hub) c->latch += shift;
	p->sleepers &= (uint8_t)~(1u << n);
	loop_reset(l);
}

static void loop_wake(p8x32a *p, int n)
{
	uint64_t t = p->cog[n].ev_t;
	if (loop_same(p, n, t)) p->cog[n].ev_t = loop_bound(p, n, t);
	else loop_resume(p, n, t);
}

static void loop_in(p8x32a *p, int n, unsigned op, uint32_t s, uint32_t d)
{
	p8x32a_loop *l = &p->loop[n];
	uint32_t mask = (op == 0x18 || op == 0x19) ? d : 0xFFFFFFFFu;
	if (l->nin == 4) { l->dirty = 1; return; }
	l->in_mask[l->nin] = mask;
	l->in_val[l->nin++] = s & mask;
}

/* a taken backward jump to head at time t */
static void loop_edge(p8x32a *p, int n, unsigned head, uint64_t t)
{
	p8x32a_loop *l = &p->loop[n];
	if (l->state == LOOP_RECORD) {
		if (head == l->head || l->nins > P8X32A_PAT / 2) { l->edge = 1; l->edge_head = (uint16_t)head; l->edge_t = t; }
		return;
	}
	if (head == l->head && !l->dirty && l->nins <= P8X32A_PAT / 2 && t > l->head_t) {
		l->state = LOOP_RECORD;
		l->period = t - l->head_t;
		l->nins0 = l->nins;
		l->nsnap = l->nin = l->nhub = 0;
		l->edge = 0;
	} else if (head != l->head && !l->dirty && l->nins <= P8X32A_PAT / 2 && l->head != 0xFFFF)
		return;
	l->head = (uint16_t)head;
	l->head_t = t;
	l->dirty = l->hub = 0;
	l->nins = 0;
}

/* after each event of a recording cog: keep its state, or at the loop head decide whether it sleeps */
static void loop_post(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	p8x32a_loop *l = &p->loop[n];
	uint64_t per = l->period;
	uint32_t m = 0;
	int k, same;

	if (!l->edge) {
		if (l->dirty || l->nsnap == P8X32A_PAT || c->ev == EV_NONE || c->ev == EV_RESTART) { loop_reset(l); return; }
		l->snap_t[l->nsnap] = c->ev_t;
		memcpy(l->snap[l->nsnap++], SNAP(c), P8X32A_SNAP);
		return;
	}
	l->edge = 0;
	if (l->edge_head != l->head || l->dirty || l->edge_t - l->head_t != per || l->nins != l->nins0 ||
	    (l->hub && per % 16) || !l->nsnap || c->ev != EV_EXEC) {
		loop_reset(l);
		return;
	}
	c->ev_t -= per;
	c->t0 -= per;
	if (l->hub) c->latch -= per;
	same = !memcmp(SNAP(c), l->snap[0], P8X32A_SNAP);
	c->ev_t += per;
	c->t0 += per;
	if (l->hub) c->latch += per;
	if (!same || !loop_same(p, n, p->now)) { loop_reset(l); return; }
	for (k = 0; k < l->nin; k++) m |= l->in_mask[k];
	l->wake = (m & ~p->bus.pure_in) ? 0xFFFFFFFFu : m;
	l->t0 = c->ev_t;
	l->state = LOOP_SLEEP;
	p->sleepers |= (uint8_t)(1u << n);
	p->sleeps++;
	c->ev = EV_SLEEP;
	c->ev_t = loop_bound(p, n, p->now);
}

static void idle(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	if (c->restart_at != P8X32A_NEVER) { c->ev = EV_RESTART; c->ev_t = c->restart_at + 2; }
	else c->ev = EV_NONE;
}

static void next_instr(p8x32a *p, int n, uint64_t t0)
{
	p8x32a_cog *c = &p->cog[n];
	if (t0 >= c->disable_at) { idle(p, n); return; }
	c->t0 = t0;
	c->ev = EV_EXEC;
	c->ev_t = t0 + 2;
}

static void complete(p8x32a *p, int n, uint64_t m3, uint32_t q, int bus_c)
{
	p8x32a_cog *c = &p->cog[n];
	uint32_t i = c->i, r;
	unsigned op = OP(i);
	int wr, co, zo, jc = 0;

	r = alu(op, c->s, c->d, c->p, c->run, c->c, c->z, q, bus_c, &wr, &co, &zo);
	if (c->cond) {
		if (FWR(i)) {
			if (wr && c->ram[DST(i)] != r) { c->ram[DST(i)] = r; p->loop[n].dirty = 1; }
			if (DST(i) >= 0x1F0) { special_write(p, n, DST(i), r, m3); p->loop[n].dirty = 1; }
		}
		if (FWC(i)) c->c = (uint8_t)co;
		if (FWZ(i)) c->z = (uint8_t)zo;
	}
	if (c->cond) {
		int dz = !(c->d >> 1);
		if (op == 0x39) jc = dz && (c->d & 1);
		else if (op == 0x3A) jc = dz && !(c->d & 1);
		else if (op == 0x3B) jc = !(dz && !(c->d & 1));
	}
	p->loop[n].nins++;
	if (c->cond && !jc && (op == 0x17 || (op >= 0x39 && op <= 0x3B)) && c->px < c->p) loop_edge(p, n, c->px, m3);
	if (!jc) c->p = (uint16_t)((c->px + 1) & 511);
	c->cancel = (uint8_t)(jc || c->px == 511);
	if (c->px == 511) c->run = 1;
	c->ix = c->nix;
	next_instr(p, n, m3 + 1);
}

static uint64_t next_slot(const p8x32a *p, int n, uint64_t from)
{
	uint64_t base = p->slot_base + 3 + 2 * (uint64_t)n;
	if (from <= base) return base;
	return base + ((from - base + 15) / 16) * 16;
}

static void stop_cog(p8x32a *p, int n, uint64_t d)
{
	p8x32a_cog *c = &p->cog[n];
	if (d < c->disable_at) c->disable_at = d;
	regset(p, &c->dira, 0, d);
	ctr_save(p, c, 0, d);
	ctr_save(p, c, 1, d);
	ctr_rebase(c, 0, d);
	ctr_rebase(c, 1, d);
	c->ctr[0] = c->ctr[1] = 0;
	ctr_notify(p, n, 0, d);
	ctr_notify(p, n, 1, d);
}

static void sys(p8x32a *p, int n, uint64_t h)
{
	p8x32a_cog *c = &p->cog[n];
	uint32_t dc = c->d;
	unsigned op = c->s & 7, num, newx = 0;
	uint8_t enc = (op & 4) ? p->lock_e : p->cog_e, bit;
	int all = enc == 0xFF, old = 0;

	while (newx < 7 && (enc >> newx & 1)) newx++;
	num = ((op == 2 && (dc & 8)) || op == 4) ? newx : (dc & 7);
	bit = (uint8_t)(1u << num);
	if (op == 2 || op == 3) {
		if (p->sleepers >> num & 1) loop_resume(p, (int)num, h);
		loop_reset(&p->loop[num]);
	}
	switch (op) {
	case 0:
		p->cfg = (uint8_t)dc;
		if (p->bus.clkset) p->bus.clkset(p->bus.ctx, h + 1, p->cfg);
		/* the host may retime its queued edges: sleepers re-check */
		if (p->sleepers) loop_notify(p, h + 1, 0xFFFFFFFFu);
		if (dc & 0x80) log_once(p, LOG_REBOOT, "p8x32a: CLKSET reset bit not modelled");
		break;
	case 2:
		if (!((dc & 8) && all)) {
			p8x32a_cog *t = &p->cog[num];
			t->ptr = dc >> 4;
			if (p->bus.cog_start) p->bus.cog_start(p->bus.ctx, h, (int)num, t->ptr);
			stop_cog(p, (int)num, h + 1);
			t->restart_at = h + 4;
			if (!(t->ev == EV_HUB && t->latch <= h) && (int)num != n) idle(p, (int)num);
		}
		p->cog_e |= bit;
		break;
	case 3:
		p->cog_e &= (uint8_t)~bit;
		if (p->cog[num].ev != EV_NONE || p->cog[num].restart_at != P8X32A_NEVER) {
			stop_cog(p, (int)num, h + 3);
			if (p->cog[num].restart_at != P8X32A_NEVER) {
				p->cog[num].restart_at = P8X32A_NEVER;
				if (p->cog[num].ev == EV_RESTART) p->cog[num].ev = EV_NONE;
			}
		}
		break;
	case 4: p->lock_e |= bit; break;
	case 5: p->lock_e &= (uint8_t)~bit; break;
	case 6: old = p->lock_state >> (dc & 7) & 1; p->lock_state |= (uint8_t)(1u << (dc & 7)); break;
	case 7: old = p->lock_state >> (dc & 7) & 1; p->lock_state &= (uint8_t)~(1u << (dc & 7)); break;
	}
	p->sys_q = (uint8_t)(op == 1 ? (unsigned)n : num);
	p->sys_c = (uint8_t)(op >= 6 ? old : all);
}

static void do_hub(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t h = c->ev_t, m3 = c->latch + 4;
	uint32_t q, a, w;
	unsigned op = OP(c->i);

	if (c->latch >= c->disable_at) { idle(p, n); return; }
	if (op == 3) {
		sys(p, n, h);
		q = p->sys_q;
	} else {
		a = c->run ? (c->s & 0xFFFF) : (((((c->ptr & 0x3FFF) + c->p) & 0x3FFF) << 2) | (c->s & 3));
		w = rd32(p, a);
		if (!c->run && (a & 0x8000)) w = unscramble(w);
		if (!FWR(c->i) && a < 0x8000) {
			uint32_t v = c->d;
			unsigned sz = op == 2 ? 4 : op == 1 ? 2 : 1, ha = a & ~(sz - 1), k;
			int changed = 0;
			for (k = 0; k < sz; k++) {
				changed |= p->hub[ha + k] != (uint8_t)(v >> (8 * k));
				p->hub[ha + k] = (uint8_t)(v >> (8 * k));
			}
			if (changed) {
				p->loop[n].dirty = 1;
				if (p->sleepers) loop_hub_write(p, n, ha, sz, h);
			}
		}
		q = op == 2 ? w : op == 1 ? (w >> ((a & 2) * 8)) & 0xFFFF : (w >> ((a & 3) * 8)) & 0xFF;
		if (p->loop[n].state == LOOP_RECORD) loop_hub_access(p, n, a, op);
	}
	if (m3 >= c->disable_at) { idle(p, n); return; }
	complete(p, n, m3, q, p->sys_c);
}

static void wait_pins(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t t = c->ev_t, nt = P8X32A_NEVER;
	int k, match;

	if (t + 2 >= c->disable_at) { idle(p, n); return; }
	match = ((ina(p, t) & c->s) == c->d) ^ (OP(c->i) == 0x3D);
	if (match) { c->ev = EV_DONE; c->ev_t = t + 2; return; }
	for (k = 0; k < p->npend; k++)
		if (p->pend[k] > t && p->pend[k] < nt) nt = p->pend[k];
	if (ctr_next(p, t) < nt) nt = ctr_next(p, t);
	for (k = 0; k < 8; k++)
		if (k != n && p->cog[k].ev != EV_NONE && p->cog[k].ev_t + 1 < nt) nt = p->cog[k].ev_t + 1;
	if (p->bus.pins_next) {
		uint64_t e = p->bus.pins_next(p->bus.ctx, t);
		if (e < nt) nt = e;
	}
	if (p->horizon != P8X32A_NEVER && nt > p->horizon + 1) nt = p->horizon + 1;
	if (nt <= t) nt = t + 1;
	c->ev_t = nt;
}

static void exec(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t t2 = c->ev_t, t0 = t2 - 2;
	uint32_t i = c->run ? c->ix : ((0x02u << 26) | (1u << 23) | (1u << 18) | ((uint32_t)c->p << 9));
	unsigned op = OP(i);
	int jump = op == 0x17 || op == 0x39 || op == 0x3A || op == 0x3B;

	c->i = i;
	c->cond = (uint8_t)(((COND(i) >> ((c->c << 1) | c->z)) & 1) && !c->cancel);
	c->s = FIM(i) ? SRC(i) : sread(p, n, SRC(i), t2);
	c->d = c->ram[DST(i)];
	if (!c->run || (c->cond && (op == 3 || op >= 0x3C)) || (!FIM(i) && (SRC(i) == 0x1F1 || SRC(i) == 0x1FC || SRC(i) == 0x1FD)))
		p->loop[n].dirty = 1;
	else if (!FIM(i) && SRC(i) == 0x1F2 && p->loop[n].state == LOOP_RECORD)
		loop_in(p, n, op, c->s, c->d);
	c->px = (uint16_t)((c->cond && jump) ? (c->s & 511) : c->p);
	c->nix = c->ram[c->px];
	if (c->cond && op <= 3) {
		c->latch = next_slot(p, n, t0 + 3);
		c->ev = EV_HUB;
		c->ev_t = c->latch + 2;
		return;
	}
	if (c->cond && op == 0x3E) {
		uint64_t m = t0 + 3;
		c->ev = EV_DONE;
		c->ev_t = m + (uint32_t)(c->d - cnt(p, m)) + 2;
		return;
	}
	if (c->cond && (op == 0x3C || op == 0x3D)) {
		c->ev = EV_WAITPIN;
		c->ev_t = t0 + 3;
		return;
	}
	if (c->cond && op == 0x3F) log_once(p, LOG_WAITVID, "p8x32a: WAITVID not modelled");
	if (t0 + 3 >= c->disable_at) { idle(p, n); return; }
	complete(p, n, t0 + 3, 0, p->sys_c);
}

static void restart(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	loop_reset(&p->loop[n]);
	c->p = 0;
	c->c = c->z = c->cancel = c->run = 0;
	c->disable_at = P8X32A_NEVER;
	c->restart_at = P8X32A_NEVER;
	exec(p, n);
}

/* an instruction that neither reads nor writes anything another cog or the pins can see */
static int local(const p8x32a_cog *c)
{
	uint32_t i = c->ix;
	unsigned op = OP(i);
	if (!c->run || op <= 3 || op >= 0x3C) return 0;
	if (!FIM(i) && SRC(i) == 0x1F2) return 0;
	return !(FWR(i) && DST(i) >= 0x1F0);
}

/* exec + complete for local instructions of cog n, kept in locals, until one is not local or t is passed */
static void run_local(p8x32a *p, int n, uint64_t t)
{
	p8x32a_cog *c = &p->cog[n];
	p8x32a_loop *l = &p->loop[n];
	uint64_t t2 = c->ev_t;
	uint32_t ix = c->ix;
	unsigned pc = c->p;
	int cf = c->c, zf = c->z, cancel = c->cancel, idled = 0;

	while (t2 <= t) {
		uint32_t i = ix, s, d, r, nix;
		unsigned op = OP(i), px, a;
		int cond, jump, wr, co, zo, jc = 0;
		if (op <= 3 || op >= 0x3C || (FWR(i) && DST(i) >= 0x1F0)) break;
		if (FIM(i)) s = SRC(i);
		else if ((a = SRC(i)) < 0x1F0) s = c->ram[a];
		else if (a == 0x1F2) break;
		else {
			s = sread(p, n, a, t2);
			if (a == 0x1F1 || a == 0x1FC || a == 0x1FD) l->dirty = 1;
		}
		cond = ((COND(i) >> ((cf << 1) | zf)) & 1) && !cancel;
		d = c->ram[DST(i)];
		jump = op == 0x17 || (op >= 0x39 && op <= 0x3B);
		px = (cond && jump) ? (s & 511) : pc;
		nix = c->ram[px];
		if (t2 + 1 >= c->disable_at) { idled = 1; break; }
		r = alu_run(op, s, d, pc, cf, zf, p->sys_c, &wr, &co, &zo);
		if (cond) {
			if (FWR(i) && wr && c->ram[DST(i)] != r) { c->ram[DST(i)] = r; l->dirty = 1; }
			if (FWC(i)) cf = co;
			if (FWZ(i)) zf = zo;
			if (op == 0x39) jc = !(d >> 1) && (d & 1);
			else if (op == 0x3A) jc = !(d >> 1) && !(d & 1);
			else if (op == 0x3B) jc = !(!(d >> 1) && !(d & 1));
		}
		l->nins++;
		if (cond && !jc && jump && px < pc) {
			loop_edge(p, n, px, t2 + 1);
			if (l->state == LOOP_RECORD) {
				c->i = i; c->s = s; c->d = d; c->px = (uint16_t)px; c->nix = nix; c->cond = (uint8_t)cond;
				if (!jc) pc = (px + 1) & 511;
				cancel = jc || px == 511;
				ix = nix;
				t2 += 4;
				break;
			}
		}
		if (!jc) pc = (px + 1) & 511;
		cancel = jc || px == 511;
		ix = nix;
		t2 += 4;
		if (t2 - 2 >= c->disable_at) { idled = 2; break; }
	}
	c->p = (uint16_t)pc;
	c->ix = ix;
	c->c = (uint8_t)cf;
	c->z = (uint8_t)zf;
	c->cancel = (uint8_t)cancel;
	c->t0 = t2 - 2;
	c->ev_t = t2;
	c->ev = EV_EXEC;
	if (idled) idle(p, n);
}

void p8x32a_run_until(p8x32a *p, uint64_t t)
{
	p->horizon = t;
	for (;;) {
		int n, best = -1;
		p8x32a_cog *b;
		for (n = 0; n < 8; n++) {
			p8x32a_cog *c = &p->cog[n];
			if (c->ev == EV_NONE) continue;
			if (best < 0 || c->ev_t < p->cog[best].ev_t ||
			    (c->ev_t == p->cog[best].ev_t && c->ev == EV_HUB && p->cog[best].ev != EV_HUB)) best = n;
		}
		if (best < 0 || p->cog[best].ev_t > t || p->stop) break;
		b = &p->cog[best];
		p->now = b->ev_t;
		switch (b->ev) {
		case EV_HUB: do_hub(p, best); break;
		case EV_EXEC: exec(p, best); break;
		case EV_WAITPIN: wait_pins(p, best); break;
		case EV_RESTART: restart(p, best); break;
		case EV_DONE:
			if (b->ev_t >= b->disable_at) idle(p, best);
			else complete(p, best, b->ev_t, 0, p->sys_c);
			break;
		case EV_SLEEP: loop_wake(p, best); break;
		}
		if (p->loop[best].state == LOOP_RECORD) loop_post(p, best);
		/* local instructions run on ahead of the other cogs: their order against them is unobservable */
		while (b->ev == EV_EXEC && b->ev_t <= t && local(b)) {
			if (p->loop[best].state == LOOP_RECORD) exec(p, best);
			else run_local(p, best, t);
			if (p->loop[best].state == LOOP_RECORD) loop_post(p, best);
		}
	}
	flush(p, t);
	p->now = t;
}

void p8x32a_reset(p8x32a *p, uint64_t t)
{
	int n;
	for (n = 0; n < 8; n++) {
		p8x32a_cog *c = &p->cog[n];
		uint32_t ram[512];
		c->ctr[0] = c->ctr[1] = 0;
		ctr_notify(p, n, 0, t);
		ctr_notify(p, n, 1, t);
		memcpy(ram, c->ram, sizeof(ram));
		memset(c, 0, sizeof(*c));
		memcpy(c->ram, ram, sizeof(ram));
		c->disable_at = P8X32A_NEVER;
		c->restart_at = P8X32A_NEVER;
		c->outa.at = c->dira.at = t;
	}
	p->cog_e = 1;
	p->lock_e = p->lock_state = p->cfg = p->sys_q = p->sys_c = 0;
	p->slot_base = t;
	p->npend = 0;
	p->ctr_ok = 0;
	p->sleepers = 0;
	for (n = 0; n < 8; n++) loop_reset(&p->loop[n]);
	p->flushed = t;
	p->last_out = p->last_dir = 0;
	p->cog[0].ptr = 0x3E00;
	p->cog[0].restart_at = t + 3;
	idle(p, 0);
	p->now = t;
}

void p8x32a_init(p8x32a *p, const p8x32a_bus *bus)
{
	memset(p, 0, sizeof(*p));
	p->bus = *bus;
	p8x32a_reset(p, 0);
}
