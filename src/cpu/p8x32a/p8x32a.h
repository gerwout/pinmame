#ifndef P8X32A_H
#define P8X32A_H

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
} p8x32a_bus;

typedef struct p8x32a_reg {
	uint32_t prev, cur;
	uint64_t at;
} p8x32a_reg;

typedef struct p8x32a_cog {
	uint32_t ram[512];
	uint32_t ptr, ix, nix, i, s, d;
	uint16_t p, px;
	uint8_t c, z, cancel, run, cond;
	int ev;
	uint64_t ev_t, t0, latch, disable_at, restart_at;
	p8x32a_reg outa, dira;
	uint32_t ctr[2], frq[2], phs[2], vcfg, vscl;
	uint64_t phs_t[2];
} p8x32a_cog;

typedef struct p8x32a {
	p8x32a_bus bus;
	uint8_t hub[65536];
	p8x32a_cog cog[8];
	uint8_t cog_e, lock_e, lock_state, cfg, sys_q, sys_c;
	uint64_t now, horizon, slot_base, cnt_base;
	uint64_t pend[40];
	int npend;
	uint32_t last_out, last_dir;
	uint32_t logged;
	int stop;
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
