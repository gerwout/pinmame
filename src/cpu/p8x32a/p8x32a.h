#ifndef P8X32A_H
#define P8X32A_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P8X32A_NEVER 0xFFFFFFFFFFFFFFFFull

typedef struct p8x32a_bus {
	void *ctx;
	uint32_t (*pins_in)(void *ctx, uint64_t t);
	uint64_t (*pins_next)(void *ctx, uint64_t t); /* P8X32A_NEVER if no edge is known yet; re-queried every run_until */
	void (*pins_out)(void *ctx, uint64_t t, uint32_t out, uint32_t dir);
	void (*cog_start)(void *ctx, uint64_t t, int cog, uint32_t ptr);
	void (*clkset)(void *ctx, uint64_t t, uint8_t cfg);
	void (*log)(void *ctx, const char *msg);
	void (*ctr_state)(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq); /* ctr: 0 = A, 1 = B; t = cycle the change takes effect */
	uint32_t pure_in; /* input pins that change only at pins_next edges, never inside pins_out; 0 = none */
} p8x32a_bus;

typedef struct p8x32a_reg {
	uint32_t prev, cur;
	uint64_t at;
} p8x32a_reg;

typedef struct p8x32a_cog {
	uint32_t ram[512];
	uint32_t ptr, ix, nix, i, s, d;
	uint16_t p, px;
	uint8_t c, z, cancel, run, cond, pad[3]; /* pad: snapshots are compared with memcmp */
	int ev;
	uint64_t ev_t, t0, latch, disable_at, restart_at;
	p8x32a_reg outa, dira;
	uint32_t ctr[2], frq[2], phs[2], vcfg, vscl;
	uint64_t phs_t[2];
	uint32_t ctr_old[2], frq_old[2], phs_old[2];
	uint64_t phs_t_old[2], ctr_at[2];
	uint32_t ctr_seen[2], frq_seen[2];
} p8x32a_cog;

#define P8X32A_PAT 32
#define P8X32A_SNAP (sizeof(p8x32a_cog) - offsetof(p8x32a_cog, ptr))

/* a cog polling in a loop that changes nothing: one recorded iteration replays it while its inputs stay the same */
typedef struct p8x32a_loop {
	uint8_t state, edge, dirty, hub;
	uint16_t head, edge_head, nins, nins0;
	uint64_t head_t, edge_t, period, t0;
	int nsnap, nin, nhub;
	uint32_t in_mask[4], in_val[4], hub_v[4], wake;
	uint16_t hub_a[4];
	uint8_t hub_n[4];
	uint64_t snap_t[P8X32A_PAT];
	unsigned char snap[P8X32A_PAT][P8X32A_SNAP];
} p8x32a_loop;

/* a decoded instruction word, valid while word matches the instruction being run */
typedef struct p8x32a_dec {
	uint32_t word;
	uint16_t src, dst;
	uint8_t kind, fl, cond, pad;
} p8x32a_dec;

typedef struct p8x32a {
	p8x32a_bus bus;
	uint8_t hub[65536];
	p8x32a_cog cog[8];
	uint8_t cog_e, lock_e, lock_state, cfg, sys_q, sys_c;
	uint64_t now, horizon, flushed, slot_base, cnt_base;
	uint64_t pend[40];
	uint32_t pend_pins[40];
	int npend;
	uint32_t last_out, last_dir;
	uint32_t logged;
	int stop;
	int ctr_ok;
	uint64_t ctr_from, ctr_nt;
	uint16_t nco_mask, nco_ok; /* counters (2 * cog + ctr) with an NCO mode now or before their last write; cached next changes */
	uint8_t nco_n, nco_list[16];
	int pins_ok; /* reg_out, reg_dir, cog_dir, nco_lvl hold the pins since the last change point */
	uint32_t reg_out, reg_dir, cog_dir[8], nco_lvl[16];
	uint64_t nco_from[16], nco_nt[16];
	uint8_t sleepers;
	unsigned sched_gen; /* counts changes one cog makes to another cog's next event */
	uint64_t sleeps; /* idle loops entered */
	p8x32a_loop loop[8];
	p8x32a_dec dec[8][512];
} p8x32a;

void p8x32a_init(p8x32a *p, const p8x32a_bus *bus);
void p8x32a_reset(p8x32a *p, uint64_t t);
void p8x32a_run_until(p8x32a *p, uint64_t t);
uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir);
unsigned p8x32a_dasm(char *buf, uint32_t op);

#ifdef __cplusplus
}
#endif

#endif
