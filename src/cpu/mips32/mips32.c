#include "mips32.h"
#include <string.h>

#define ST_IE    0x00000001u
#define ST_EXL   0x00000002u
#define ST_ERL   0x00000004u
#define ST_UM    0x00000010u
#define ST_BEV   0x00400000u
#define ST_CU0   0x10000000u
#define ST_WMASK 0x1A48FF17u
#define ST_IPL(v) (((v) >> 10) & 0x3F)

#define CA_BD    0x80000000u
#define CA_TI    0x40000000u
#define CA_IV    0x00800000u
#define CA_WMASK 0x08C00300u

#define RS(op)    (((op) >> 21) & 31)
#define RT(op)    (((op) >> 16) & 31)
#define RD(op)    (((op) >> 11) & 31)
#define SA(op)    (((op) >> 6) & 31)
#define FUNCT(op) ((op) & 63)
#define SIMM(op)  ((uint32_t)(int32_t)(int16_t)((op) & 0xFFFF))
#define UIMM(op)  ((op) & 0xFFFFu)

#define USER(s) (((s)->status & (ST_UM | ST_EXL | ST_ERL)) == ST_UM)
#define SET(n, v) do { uint32_t v_ = (v); int n_ = (n); if (n_) s->r[n_] = v_; } while (0)

static uint32_t ror32(uint32_t x, unsigned n) { n &= 31; return n ? (x >> n) | (x << (32 - n)) : x; }
static uint32_t sra32(uint32_t x, unsigned n) { n &= 31; return n ? (x >> n) | ((x & 0x80000000u) ? ~(0xFFFFFFFFu >> n) : 0) : x; }
static uint32_t mask32(unsigned size) { return size >= 32 ? 0xFFFFFFFFu : (1u << size) - 1; }

static uint32_t clz32(uint32_t x)
{
	uint32_t n = 0;
	if (!x) return 32;
	while (!(x & 0x80000000u)) { x <<= 1; n++; }
	return n;
}

int mips32_translate(const mips32_state *s, uint32_t va, uint32_t *pa)
{
	if (va < 0x80000000u) {
		*pa = (s->status & ST_ERL) ? va : va + 0x40000000u;
		return 1;
	}
	if (USER(s)) return 0;
	*pa = va < 0xC0000000u ? (va & 0x1FFFFFFFu) : va;
	return 1;
}

static void take_exception(mips32_state *s, int code, int ce)
{
	int is_int = code == MIPS32_EXC_INT;
	uint32_t off = 0x180, base;

	if (s->bus.exc_hook) {
		int h = s->bus.exc_hook(s->bus.ctx, s, code);
		if (h == MIPS32_HOOK_SKIP) { s->pc = s->skip_pc; s->npc = s->pc + 4; s->delay = 0; return; }
		if (h == MIPS32_HOOK_STOP) { s->stop = 1; return; }
	}
	if (!(s->status & ST_EXL)) {
		s->epc = s->cur_delay ? s->cur_pc - 4 : s->cur_pc;
		if (s->cur_delay) s->cause |= CA_BD; else s->cause &= ~CA_BD;
		if (is_int && (s->cause & CA_IV))
			off = (s->status & ST_BEV) ? 0x200 : 0x200 + (uint32_t)s->eic_vector * (((s->intctl >> 5) & 0x1F) << 5);
		if (s->shadow_sets > 1 && !(s->status & ST_BEV)) {
			uint32_t css = s->srsctl & 15;
			uint32_t nss = (is_int ? (uint32_t)s->eic_srs : (s->srsctl >> 12)) & 7;
			s->srsctl = (s->srsctl & ~0x3CFu) | (css << 6) | nss;
			s->r = s->gpr[nss];
		}
	}
	s->cause = (s->cause & ~0x3000007Cu) | ((uint32_t)code << 2) | ((uint32_t)ce << 28);
	if (is_int) s->cause = (s->cause & ~0xFC00u) | ((uint32_t)s->eic_ripl << 10);
	s->status |= ST_EXL;
	base = (s->status & ST_BEV) ? 0xBFC00200u : (s->ebase & 0xFFFFF000u);
	s->pc = base + off;
	s->npc = s->pc + 4;
	s->delay = 0;
	s->waiting = 0;
	if (is_int && s->bus.irq_taken) s->bus.irq_taken(s->bus.ctx, s->eic_vector);
}

static int load(mips32_state *s, uint32_t va, int size, uint32_t *out)
{
	uint32_t pa;
	int err = 0;
	if ((va & (uint32_t)(size - 1)) || !mips32_translate(s, va, &pa)) {
		s->badvaddr = va;
		take_exception(s, MIPS32_EXC_ADEL, 0);
		return 0;
	}
	*out = s->bus.read(s->bus.ctx, pa, size, 0, &err);
	if (err) { take_exception(s, MIPS32_EXC_DBE, 0); return 0; }
	return 1;
}

static int store(mips32_state *s, uint32_t va, uint32_t v, int size)
{
	uint32_t pa;
	int err = 0;
	if ((va & (uint32_t)(size - 1)) || !mips32_translate(s, va, &pa)) {
		s->badvaddr = va;
		take_exception(s, MIPS32_EXC_ADES, 0);
		return 0;
	}
	s->bus.write(s->bus.ctx, pa, v, size, &err);
	if (err) { take_exception(s, MIPS32_EXC_DBE, 0); return 0; }
	return 1;
}

uint32_t mips32_get_cp0(const mips32_state *s, int reg, int sel)
{
	switch (reg * 8 + sel) {
	case 7 * 8:      return s->hwrena;
	case 8 * 8:      return s->badvaddr;
	case 9 * 8:      return s->count;
	case 11 * 8:     return s->compare;
	case 12 * 8:     return s->status;
	case 12 * 8 + 1: return s->intctl;
	case 12 * 8 + 2: return s->srsctl | ((uint32_t)(s->shadow_sets - 1) << 26) | (((uint32_t)s->eic_srs & 15) << 18);
	case 12 * 8 + 3: return s->srsmap;
	case 13 * 8:     return s->cause;
	case 14 * 8:     return s->epc;
	case 15 * 8:     return s->prid;
	case 15 * 8 + 1: return s->ebase;
	case 16 * 8:     return s->config0;
	case 16 * 8 + 1: return 0x80000000u;
	case 16 * 8 + 2: return 0x80000000u;
	case 16 * 8 + 3: return 0x00000060u;
	case 30 * 8:     return s->errorepc;
	}
	return 0;
}

static void set_cp0(mips32_state *s, int reg, int sel, uint32_t v)
{
	switch (reg * 8 + sel) {
	case 7 * 8:      s->hwrena = v & 0xFu; break;
	case 9 * 8:      s->count = v; s->count_half = 0; break;
	case 11 * 8:     s->compare = v; s->cause &= ~CA_TI; break;
	case 12 * 8:     s->status = (s->status & ~ST_WMASK) | (v & ST_WMASK); break;
	case 12 * 8 + 1: s->intctl = v & 0x3E0u; break;
	case 12 * 8 + 2: s->srsctl = (s->srsctl & ~0xF3C0u) | (v & 0xF3C0u); break;
	case 12 * 8 + 3: s->srsmap = v; break;
	case 13 * 8:     s->cause = (s->cause & ~CA_WMASK) | (v & CA_WMASK); break;
	case 14 * 8:     s->epc = v; break;
	case 15 * 8 + 1: s->ebase = 0x80000000u | (v & 0x3FFFF000u); break;
	case 16 * 8:     s->config0 = (s->config0 & ~7u) | (v & 7u); break;
	case 30 * 8:     s->errorepc = v; break;
	}
}

static void branch(mips32_state *s, int cond, uint32_t op)
{
	if (cond) s->npc = s->cur_pc + 4 + (SIMM(op) << 2);
	s->delay = 1;
}

static void branch_likely(mips32_state *s, int cond, uint32_t op)
{
	if (cond) {
		s->npc = s->cur_pc + 4 + (SIMM(op) << 2);
		s->delay = 1;
	} else {
		s->pc = s->npc;
		s->npc += 4;
	}
}

static void jump(mips32_state *s, uint32_t target)
{
	s->npc = target;
	s->delay = 1;
}

static void eret(mips32_state *s)
{
	if (s->status & ST_ERL) {
		s->pc = s->errorepc;
		s->status &= ~ST_ERL;
	} else {
		s->pc = s->epc;
		s->status &= ~ST_EXL;
		if (s->shadow_sets > 1 && !(s->status & ST_BEV)) {
			uint32_t pss = (s->srsctl >> 6) & 7;
			s->srsctl = (s->srsctl & ~15u) | pss;
			s->r = s->gpr[pss];
		}
	}
	s->npc = s->pc + 4;
	s->delay = 0;
	s->llbit = 0;
}

static void exec_special(mips32_state *s, uint32_t op)
{
	uint32_t rs = s->r[RS(op)], rt = s->r[RT(op)], res;
	int64_t p;
	uint64_t acc;

	switch (FUNCT(op)) {
	case 0x00: SET(RD(op), rt << SA(op)); break;
	case 0x02: SET(RD(op), (op & (1u << 21)) ? ror32(rt, SA(op)) : rt >> SA(op)); break;
	case 0x03: SET(RD(op), sra32(rt, SA(op))); break;
	case 0x04: SET(RD(op), rt << (rs & 31)); break;
	case 0x06: SET(RD(op), (op & (1u << 6)) ? ror32(rt, rs) : rt >> (rs & 31)); break;
	case 0x07: SET(RD(op), sra32(rt, rs)); break;
	case 0x08: jump(s, rs); break;
	case 0x09: SET(RD(op), s->cur_pc + 8); jump(s, rs); break;
	case 0x0A: if (!rt) SET(RD(op), rs); break;
	case 0x0B: if (rt) SET(RD(op), rs); break;
	case 0x0C: take_exception(s, MIPS32_EXC_SYS, 0); break;
	case 0x0D: take_exception(s, MIPS32_EXC_BP, 0); break;
	case 0x0F: break;
	case 0x10: SET(RD(op), s->hi); break;
	case 0x11: s->hi = rs; break;
	case 0x12: SET(RD(op), s->lo); break;
	case 0x13: s->lo = rs; break;
	case 0x18:
		p = (int64_t)(int32_t)rs * (int32_t)rt;
		s->lo = (uint32_t)p; s->hi = (uint32_t)((uint64_t)p >> 32);
		break;
	case 0x19:
		acc = (uint64_t)rs * rt;
		s->lo = (uint32_t)acc; s->hi = (uint32_t)(acc >> 32);
		break;
	case 0x1A:
		if (rt) {
			if (rs == 0x80000000u && rt == 0xFFFFFFFFu) { s->lo = 0x80000000u; s->hi = 0; }
			else { s->lo = (uint32_t)((int32_t)rs / (int32_t)rt); s->hi = (uint32_t)((int32_t)rs % (int32_t)rt); }
		}
		s->cycles += 34;
		break;
	case 0x1B:
		if (rt) { s->lo = rs / rt; s->hi = rs % rt; }
		s->cycles += 34;
		break;
	case 0x20:
		res = rs + rt;
		if (~(rs ^ rt) & (rs ^ res) & 0x80000000u) take_exception(s, MIPS32_EXC_OV, 0);
		else SET(RD(op), res);
		break;
	case 0x21: SET(RD(op), rs + rt); break;
	case 0x22:
		res = rs - rt;
		if ((rs ^ rt) & (rs ^ res) & 0x80000000u) take_exception(s, MIPS32_EXC_OV, 0);
		else SET(RD(op), res);
		break;
	case 0x23: SET(RD(op), rs - rt); break;
	case 0x24: SET(RD(op), rs & rt); break;
	case 0x25: SET(RD(op), rs | rt); break;
	case 0x26: SET(RD(op), rs ^ rt); break;
	case 0x27: SET(RD(op), ~(rs | rt)); break;
	case 0x2A: SET(RD(op), (int32_t)rs < (int32_t)rt); break;
	case 0x2B: SET(RD(op), rs < rt); break;
	case 0x30: if ((int32_t)rs >= (int32_t)rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x31: if (rs >= rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x32: if ((int32_t)rs < (int32_t)rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x33: if (rs < rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x34: if (rs == rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x36: if (rs != rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void exec_regimm(mips32_state *s, uint32_t op)
{
	int32_t rs = (int32_t)s->r[RS(op)];
	uint32_t imm = SIMM(op);
	int cond;

	switch (RT(op)) {
	case 0x00: branch(s, rs < 0, op); break;
	case 0x01: branch(s, rs >= 0, op); break;
	case 0x02: branch_likely(s, rs < 0, op); break;
	case 0x03: branch_likely(s, rs >= 0, op); break;
	case 0x08: if (rs >= (int32_t)imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x09: if ((uint32_t)rs >= imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x0A: if (rs < (int32_t)imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x0B: if ((uint32_t)rs < imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x0C: if ((uint32_t)rs == imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x0E: if ((uint32_t)rs != imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x10: cond = rs < 0; SET(31, s->cur_pc + 8); branch(s, cond, op); break;
	case 0x11: cond = rs >= 0; SET(31, s->cur_pc + 8); branch(s, cond, op); break;
	case 0x12: cond = rs < 0; SET(31, s->cur_pc + 8); branch_likely(s, cond, op); break;
	case 0x13: cond = rs >= 0; SET(31, s->cur_pc + 8); branch_likely(s, cond, op); break;
	case 0x1F: break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void exec_cop0(mips32_state *s, uint32_t op)
{
	uint32_t v;

	if (USER(s) && !(s->status & ST_CU0)) { take_exception(s, MIPS32_EXC_CPU, 0); return; }
	if (op & (1u << 25)) {
		switch (FUNCT(op)) {
		case 0x18: eret(s); break;
		case 0x20: s->waiting = 1; break;
		default: take_exception(s, MIPS32_EXC_RI, 0); break;
		}
		return;
	}
	switch (RS(op)) {
	case 0x00: SET(RT(op), mips32_get_cp0(s, RD(op), op & 7)); break;
	case 0x04: set_cp0(s, RD(op), op & 7, s->r[RT(op)]); break;
	case 0x0A: SET(RD(op), s->gpr[(s->srsctl >> 6) & 7][RT(op)]); break;
	case 0x0B:
		v = s->status;
		if (op & 0x20) s->status |= ST_IE; else s->status &= ~ST_IE;
		SET(RT(op), v);
		break;
	case 0x0E: if (RD(op)) s->gpr[(s->srsctl >> 6) & 7][RD(op)] = s->r[RT(op)]; break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void exec_special2(mips32_state *s, uint32_t op)
{
	uint32_t rs = s->r[RS(op)], rt = s->r[RT(op)];
	uint64_t acc = ((uint64_t)s->hi << 32) | s->lo;

	switch (FUNCT(op)) {
	case 0x00: acc += (uint64_t)((int64_t)(int32_t)rs * (int32_t)rt); break;
	case 0x01: acc += (uint64_t)rs * rt; break;
	case 0x02: SET(RD(op), (uint32_t)((int64_t)(int32_t)rs * (int32_t)rt)); s->cycles += 1; return;
	case 0x04: acc -= (uint64_t)((int64_t)(int32_t)rs * (int32_t)rt); break;
	case 0x05: acc -= (uint64_t)rs * rt; break;
	case 0x20: SET(RD(op), clz32(rs)); return;
	case 0x21: SET(RD(op), clz32(~rs)); return;
	default: take_exception(s, MIPS32_EXC_RI, 0); return;
	}
	s->lo = (uint32_t)acc;
	s->hi = (uint32_t)(acc >> 32);
}

static void exec_special3(mips32_state *s, uint32_t op)
{
	uint32_t rs = s->r[RS(op)], rt = s->r[RT(op)], m;
	unsigned lsb = SA(op), msb = RD(op);

	switch (FUNCT(op)) {
	case 0x00: SET(RT(op), (rs >> lsb) & mask32(msb + 1)); break;
	case 0x04:
		m = mask32(msb - lsb + 1) << lsb;
		SET(RT(op), (rt & ~m) | ((rs << lsb) & m));
		break;
	case 0x20:
		switch (SA(op)) {
		case 0x02: SET(RD(op), ((rt & 0x00FF00FFu) << 8) | ((rt >> 8) & 0x00FF00FFu)); break;
		case 0x10: SET(RD(op), (uint32_t)(int32_t)(int8_t)rt); break;
		case 0x18: SET(RD(op), (uint32_t)(int32_t)(int16_t)rt); break;
		default: take_exception(s, MIPS32_EXC_RI, 0); break;
		}
		break;
	case 0x3B:
		if (USER(s) && !(s->status & ST_CU0) && !(s->hwrena & (1u << RD(op)))) { take_exception(s, MIPS32_EXC_RI, 0); break; }
		switch (RD(op)) {
		case 0: SET(RT(op), s->ebase & 0x3FFu); break;
		case 1: SET(RT(op), 0); break;
		case 2: SET(RT(op), s->count); break;
		case 3: SET(RT(op), 2); break;
		default: take_exception(s, MIPS32_EXC_RI, 0); break;
		}
		break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void exec_mem(mips32_state *s, uint32_t op)
{
	uint32_t ea = s->r[RS(op)] + SIMM(op), rt = s->r[RT(op)], v;
	unsigned b = ea & 3, i, sh;

	switch (op >> 26) {
	case 0x20: if (load(s, ea, 1, &v)) SET(RT(op), (uint32_t)(int32_t)(int8_t)v); break;
	case 0x21: if (load(s, ea, 2, &v)) SET(RT(op), (uint32_t)(int32_t)(int16_t)v); break;
	case 0x22:
		if (load(s, ea & ~3u, 4, &v)) {
			sh = (3 - b) * 8;
			SET(RT(op), sh ? (rt & ((1u << sh) - 1)) | (v << sh) : v);
		}
		break;
	case 0x23: if (load(s, ea, 4, &v)) SET(RT(op), v); break;
	case 0x24: if (load(s, ea, 1, &v)) SET(RT(op), v & 0xFFu); break;
	case 0x25: if (load(s, ea, 2, &v)) SET(RT(op), v & 0xFFFFu); break;
	case 0x26:
		if (load(s, ea & ~3u, 4, &v)) {
			sh = b * 8;
			SET(RT(op), sh ? (rt & ~(0xFFFFFFFFu >> sh)) | (v >> sh) : v);
		}
		break;
	case 0x28: store(s, ea, rt & 0xFFu, 1); break;
	case 0x29: store(s, ea, rt & 0xFFFFu, 2); break;
	case 0x2A:
		for (i = 0; i <= b; i++)
			if (!store(s, (ea & ~3u) + i, (rt >> (8 * (3 - b + i))) & 0xFFu, 1)) break;
		break;
	case 0x2B: store(s, ea, rt, 4); break;
	case 0x2E:
		for (i = 0; i < 4 - b; i++)
			if (!store(s, ea + i, (rt >> (8 * i)) & 0xFFu, 1)) break;
		break;
	case 0x30: if (load(s, ea, 4, &v)) { SET(RT(op), v); s->llbit = 1; } break;
	case 0x38:
		if (s->llbit) { if (store(s, ea, rt, 4)) SET(RT(op), 1); }
		else SET(RT(op), 0);
		break;
	}
}

static void execute(mips32_state *s, uint32_t op)
{
	uint32_t rs = s->r[RS(op)], rt = s->r[RT(op)], res;

	switch (op >> 26) {
	case 0x00: exec_special(s, op); break;
	case 0x01: exec_regimm(s, op); break;
	case 0x02: jump(s, ((s->cur_pc + 4) & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2)); break;
	case 0x03: SET(31, s->cur_pc + 8); jump(s, ((s->cur_pc + 4) & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2)); break;
	case 0x04: branch(s, rs == rt, op); break;
	case 0x05: branch(s, rs != rt, op); break;
	case 0x06: branch(s, (int32_t)rs <= 0, op); break;
	case 0x07: branch(s, (int32_t)rs > 0, op); break;
	case 0x08:
		res = rs + SIMM(op);
		if (~(rs ^ SIMM(op)) & (rs ^ res) & 0x80000000u) take_exception(s, MIPS32_EXC_OV, 0);
		else SET(RT(op), res);
		break;
	case 0x09: SET(RT(op), rs + SIMM(op)); break;
	case 0x0A: SET(RT(op), (int32_t)rs < (int32_t)SIMM(op)); break;
	case 0x0B: SET(RT(op), rs < SIMM(op)); break;
	case 0x0C: SET(RT(op), rs & UIMM(op)); break;
	case 0x0D: SET(RT(op), rs | UIMM(op)); break;
	case 0x0E: SET(RT(op), rs ^ UIMM(op)); break;
	case 0x0F: SET(RT(op), UIMM(op) << 16); break;
	case 0x10: exec_cop0(s, op); break;
	case 0x11: case 0x13: case 0x31: case 0x35: case 0x39: case 0x3D:
		take_exception(s, MIPS32_EXC_CPU, 1); break;
	case 0x12: case 0x32: case 0x36: case 0x3A: case 0x3E:
		take_exception(s, MIPS32_EXC_CPU, 2); break;
	case 0x14: branch_likely(s, rs == rt, op); break;
	case 0x15: branch_likely(s, rs != rt, op); break;
	case 0x16: branch_likely(s, (int32_t)rs <= 0, op); break;
	case 0x17: branch_likely(s, (int32_t)rs > 0, op); break;
	case 0x1C: exec_special2(s, op); break;
	case 0x1F: exec_special3(s, op); break;
	case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
	case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2E: case 0x30: case 0x38:
		exec_mem(s, op); break;
	case 0x2F:
		if (USER(s) && !(s->status & ST_CU0)) take_exception(s, MIPS32_EXC_CPU, 0);
		break;
	case 0x33: break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void step(mips32_state *s)
{
	uint32_t pa, op;
	int err = 0;

	s->cur_pc = s->pc;
	s->cur_delay = s->delay;
	s->skip_pc = s->npc;
	s->cycles++;
	if ((s->pc & 3) || !mips32_translate(s, s->pc, &pa)) {
		s->badvaddr = s->pc;
		take_exception(s, MIPS32_EXC_ADEL, 0);
		return;
	}
	op = s->bus.read(s->bus.ctx, pa, 4, 1, &err);
	if (err) { take_exception(s, MIPS32_EXC_IBE, 0); return; }
	s->pc = s->npc;
	s->npc += 4;
	s->delay = 0;
	execute(s, op);
}

static void tick(mips32_state *s, uint64_t n)
{
	uint64_t t = n + (uint64_t)s->count_half;
	uint32_t inc = (uint32_t)(t >> 1), old;

	s->count_half = (int)(t & 1);
	if (!inc) return;
	old = s->count;
	s->count += inc;
	if (s->compare - old - 1 < inc && !(s->cause & CA_TI)) {
		s->cause |= CA_TI;
		s->stop = 1;
	}
}

static int irq_pending(const mips32_state *s)
{
	return s->eic_ripl > (int)ST_IPL(s->status) && (s->status & (ST_IE | ST_EXL | ST_ERL)) == ST_IE;
}

int mips32_run(mips32_state *s, int cycles)
{
	uint64_t start = s->cycles, end = start + (uint64_t)(cycles > 0 ? cycles : 0);

	s->stop = 0;
	while (s->cycles < end && !s->stop) {
		uint64_t c0 = s->cycles;
		if (irq_pending(s)) {
			s->cur_pc = s->pc;
			s->cur_delay = s->delay;
			s->skip_pc = s->pc;
			take_exception(s, MIPS32_EXC_INT, 0);
			if (s->stop) break;
		}
		if (s->waiting) {
			uint64_t burn = end - s->cycles;
			if (!(s->cause & CA_TI)) {
				uint64_t need = 2 * (uint64_t)(uint32_t)(s->compare - s->count);
				need = need > (uint64_t)s->count_half ? need - (uint64_t)s->count_half : 0;
				if (need && need < burn) burn = need;
			}
			s->cycles += burn;
		} else
			step(s);
		tick(s, s->cycles - c0);
	}
	return (int)(s->cycles - start);
}

void mips32_set_eic(mips32_state *s, int ripl, int vector, int srs)
{
	s->eic_ripl = ripl;
	s->eic_vector = vector;
	s->eic_srs = srs;
}

int mips32_timer_irq(const mips32_state *s) { return (s->cause & CA_TI) != 0; }
int mips32_soft_irq(const mips32_state *s) { return (int)((s->cause >> 8) & 3); }

void mips32_reset(mips32_state *s)
{
	s->r = s->gpr[0];
	s->pc = 0xBFC00000u;
	s->npc = s->pc + 4;
	s->delay = 0;
	s->status = ST_BEV | ST_ERL;
	s->cause = 0;
	s->srsctl = 0;
	s->srsmap = 0;
	s->intctl = 0;
	s->hwrena = 0;
	s->ebase = 0x80000000u;
	s->config0 = 0x80000582u;
	s->llbit = 0;
	s->waiting = 0;
	s->count_half = 0;
	s->eic_ripl = 0;
	s->eic_vector = 0;
	s->eic_srs = 0;
}

void mips32_init(mips32_state *s, const mips32_bus *bus, int shadow_sets, uint32_t prid)
{
	memset(s, 0, sizeof(*s));
	s->bus = *bus;
	s->shadow_sets = shadow_sets < 1 ? 1 : shadow_sets > 8 ? 8 : shadow_sets;
	s->prid = prid;
	mips32_reset(s);
}
