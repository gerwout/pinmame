#include "p8x32a.h"
#include <stdio.h>
#include <string.h>

enum { EV_NONE, EV_HUB, EV_EXEC, EV_DONE, EV_WAITPIN, EV_RESTART };

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

static void add_pending(p8x32a *p, uint64_t t)
{
	int k;
	for (k = 0; k < p->npend; k++)
		if (p->pend[k] == t) return;
	if (p->npend < (int)(sizeof(p->pend) / sizeof(p->pend[0]))) p->pend[p->npend++] = t;
}

static void flush(p8x32a *p, uint64_t t);

static void regset(p8x32a *p, p8x32a_reg *r, uint32_t v, uint64_t at)
{
	flush(p, p->now);
	if (at > r->at) r->prev = r->cur;
	r->cur = v;
	r->at = at;
	add_pending(p, at);
}

static int nco(uint32_t ctr)
{
	unsigned m = (ctr >> 26) & 31;
	return m == 4 || m == 5;
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

static uint64_t ctr_next(const p8x32a *p, uint64_t t)
{
	uint64_t nt = P8X32A_NEVER, e;
	int n, k;
	for (n = 0; n < 8; n++)
		for (k = 0; k < 2; k++)
			if ((e = ctr_toggle(&p->cog[n], k, t)) < nt) nt = e;
	return nt;
}

uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir)
{
	uint32_t o = 0, d = 0;
	int n;
	for (n = 0; n < 8; n++) {
		p8x32a_cog *c = &p->cog[n];
		uint32_t dd = regval(&c->dira, t);
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
			if (p->pend[k] == when) { p->pend[k] = p->pend[--p->npend]; k--; }
	}
	if (t > p->flushed) p->flushed = t;
}

static uint32_t ina(p8x32a *p, uint64_t t)
{
	uint32_t dir, out, ext;
	flush(p, t);
	out = p8x32a_pins(p, t, &dir);
	ext = p->bus.pins_in ? p->bus.pins_in(p->bus.ctx, t) : 0;
	return (dir & out) | (~dir & ext);
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
	c->ctr_old[k] = c->ctr[k];
	c->frq_old[k] = c->frq[k];
	c->phs_old[k] = c->phs[k];
	c->phs_t_old[k] = c->phs_t[k];
	c->ctr_at[k] = e;
	add_pending(p, e);
}

static void special_write(p8x32a *p, int n, unsigned a, uint32_t v, uint64_t m3)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t e = m3 + 1;
	int k = a & 1;
	switch (a) {
	case 0x1F4: regset(p, &c->outa, v, e); break;
	case 0x1F6: if (e < c->disable_at) regset(p, &c->dira, v, e); break;
	case 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; ctr_check(p, v); break;
	case 0x1FA: case 0x1FB: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->frq[k] = v; break;
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
	uint32_t r = 0;
	int k;
	for (k = 0; k < 32; k++) r |= ((x >> k) & 1u) << (31 - k);
	return r;
}

static int parity(uint32_t x)
{
	x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
	return (int)(x & 1);
}

static uint32_t alu(unsigned i, uint32_t s, uint32_t d, unsigned pc, int run, int ci, int zi,
                    uint32_t bus_q, int bus_c, int *wr, int *co, int *zo)
{
	uint32_t r, dr = bitrev(d), rot_r, lo, fill, log_r, add_d, add_s, add_r, sum_lo;
	unsigned sh = s & 31, ls;
	int rot_c, log_c, add_sub, add_ci, add_co, add_cm, add_cs, add_c, cin, c30, b31;
	uint64_t rot;
	static const unsigned ads_sel[4] = { 0, 1, 2, 3 };

	switch (i & 7) {
	case 0: fill = d & 0x7FFFFFFFu; break;
	case 1: fill = dr & 0x7FFFFFFFu; break;
	case 4: case 5: fill = ci ? 0x7FFFFFFFu : 0; break;
	case 6: fill = (d >> 31) ? 0x7FFFFFFFu : 0; break;
	default: fill = 0; break;
	}
	lo = (i & 1) ? dr : d;
	rot = (((uint64_t)fill << 32) | lo) >> sh;
	rot_r = (((i >> 1) & 3) != 3 && (i & 1)) ? bitrev((uint32_t)rot) : (uint32_t)rot;
	rot_c = (((i >> 1) & 3) != 3 && (i & 1)) ? (int)(dr & 1) : (int)(d & 1);

	if (i & 4) ls = ((unsigned)(((i & 2) ? zi : ci) ^ (int)(i & 1))) << 1;
	else ls = (((i >> 1) & 1) << 1) | (unsigned)!(((i >> 1) ^ i) & 1);
	if (i & 8) {
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
	} else
		log_r = s;
	log_c = parity(log_r);

	(void)ads_sel;
	if (((i >> 4) & 3) == 2) {
		int ads[4];
		ads[0] = 0; ads[1] = (int)(s >> 31); ads[2] = ci; ads[3] = zi;
		add_sub = ads[(i >> 1) & 3] ^ (int)(i & 1);
	} else if (i == 0x32 || i == 0x34 || i == 0x36 || (i >> 2) == 0xF)
		add_sub = 0;
	else
		add_sub = 1;
	add_ci = ((((i >> 3) & 7) == 6 && ((i & 7) == 1 || (i & 2))) && ci) || (((i >> 3) & 3) == 3 && (i & 3) == 1);
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
	else if (((i >> 3) & 7) == 5) add_c = (int)(s >> 31);
	else if ((i & 0x20) && ((i >> 2) & 3) == 1) add_c = add_co ^ add_cm;
	else if (((i >> 1) & 15) == 8) add_c = add_cs;
	else add_c = add_co ^ add_sub;

	if ((i >> 2) == 4) *wr = (int)(i & 1) ^ ((i & 2) ? !add_co : add_cs);
	else if (i == 0x38) *wr = add_co;
	else *wr = 1;

	if (i & 0x20) r = add_r;
	else if (i & 0x10) r = log_r;
	else if (i & 8) r = rot_r;
	else r = (run || (pc >> 4) != 31) ? bus_q : 0;

	switch ((i >> 3) & 7) {
	case 0: *co = bus_c; break;
	case 1: *co = rot_c; break;
	case 3: *co = log_c; break;
	default: *co = add_c; break;
	}
	*zo = !r && (zi || !(((i >> 3) & 7) == 6 && ((i & 7) == 1 || (i & 2))));
	return r;
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
			if (wr) c->ram[DST(i)] = r;
			if (DST(i) >= 0x1F0) special_write(p, n, DST(i), r, m3);
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
	switch (op) {
	case 0:
		p->cfg = (uint8_t)dc;
		if (p->bus.clkset) p->bus.clkset(p->bus.ctx, h + 1, p->cfg);
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
			if (op == 2) { a &= 0xFFFC; p->hub[a] = (uint8_t)v; p->hub[a + 1] = (uint8_t)(v >> 8); p->hub[a + 2] = (uint8_t)(v >> 16); p->hub[a + 3] = (uint8_t)(v >> 24); }
			else if (op == 1) { a &= 0xFFFE; p->hub[a] = (uint8_t)v; p->hub[a + 1] = (uint8_t)(v >> 8); }
			else p->hub[a] = (uint8_t)v;
		}
		q = op == 2 ? w : op == 1 ? (w >> ((a & 2) * 8)) & 0xFFFF : (w >> ((a & 3) * 8)) & 0xFF;
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
	c->p = 0;
	c->c = c->z = c->cancel = c->run = 0;
	c->disable_at = P8X32A_NEVER;
	c->restart_at = P8X32A_NEVER;
	exec(p, n);
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
