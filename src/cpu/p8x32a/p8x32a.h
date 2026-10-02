#ifndef P8X32A_H
#define P8X32A_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P8X32A_NEVER 0xFFFFFFFFFFFFFFFFull
#define P8X32A_PEND_COG(n) (1u << (n))       /* pend_what: cog n's OUTA or DIRA */
#define P8X32A_PEND_CTR(j) (1u << (8 + (j))) /* pend_what: counter j (2 * cog + 0 for A, 1 for B) */

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
	void (*lazy)(void *ctx, uint64_t t, uint32_t mask, uint32_t out, uint32_t dir); /* from t a lazy cog has pins mask (0: none) */
	void (*lazy_pins)(void *ctx, uint64_t t, uint32_t out, uint32_t dir);   /* a change of the lazy cog's pins (only those); hub RAM at t: p8x32a_hub_at */
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

/* Translated runs of a cog's local instructions (p8x32ajit.cpp). fn runs its block and the blocks it leads to on
   st and returns how many instructions ran, updating in st: fl (Z bit 0, C bit 1), t2 (time of the next
   instruction), budget (instructions that may still start), and loop's dirty, nins and search state as run_local
   does; for the last instruction run, pc, px (address the next word was fetched from), nix (that word), jmp (it was a
   jump that ran), jc (a jump that did not jump), edge (a backward jump run_local must pass to loop_edge), w, s, d
   (its word and operands). A block ends after a jump that runs and before a slot whose word no longer fits
   (P8X32A_JDYN); the next block runs if it is valid, fits the budget and no cancel or code change intervenes. */
#define P8X32A_JMAX 32
#define P8X32A_JDYN 0x3FFFFu /* the S and D fields: a slot whose words differ only here is read at run time */

typedef struct p8x32a_jblk p8x32a_jblk;
struct p8x32a_jst;

/* what a block's exit needs of the block at a cog address: its entry for another block, the word it starts with
   under mask (~P8X32A_JDYN when slot 0 is read at run time), and its length (~0: none to run) */
typedef struct p8x32a_jlink {
	const void *body;
	uint32_t word, mask, len, part; /* part: a lazy cog's block */
	uint32_t (*fn)(struct p8x32a_jst *st); /* its entry from run_local */
} p8x32a_jlink;

typedef struct p8x32a_jst {
	uint32_t *ram;
	const uint8_t *code; /* bit s: slot s is a fixed word of a block; a write that changes it is reported in inv */
	p8x32a_jblk **tab;   /* the cog's blocks by address */
	const p8x32a_jlink *link; /* and their links */
	p8x32a_loop *loop;
	uint64_t t2;
	uint32_t budget, ix, fl, pc, px, nix, jmp, jc, edge, w, s, d; /* pc: of the last instruction run */
	uint32_t inv, inv_old; /* 0, or the slot + 1 and its old word; ~0: more than one */
	uint32_t on;           /* OUTA writes of a lazy cog's block: their times and values, in order */
	uint64_t *ot;
	uint32_t *ov;
	uint64_t tl, slot;     /* a lazy cog's blocks: the last time an instruction or hub read may start; its first hub slot */
	uint64_t latch;        /* and the slot of the last hub read they ran, 0 if none */
	const uint8_t *hub;
	const uint8_t *jmap;   /* hub longs with a journal entry (p8x32a.jmap): a block stops before reading one */
	uint32_t par;          /* the cog's PAR (CNT is the time) */
	/* a block's last hub read or write (not a lazy cog's) runs at once while run_local's event_run would run it: its
	   time h <= t, h's key below lim, latch + 5 < dis and *pgen == gen; hubfn runs it (st->latch its slot, hi the
	   instruction, hs and hd its operands, fl the flags) and returns fl. Else the block stops before it with hiss = 1,
	   hs and hd its operands and hlatch its slot. Blocks call C with st as the only argument */
	uint64_t t, lim, dis, hlatch;
	const unsigned *pgen;
	unsigned gen, n, hiss, hs, hd, hi;
	uint32_t (*hres)(struct p8x32a_jst *st); /* with hiss: the block's resume entry */
	void *chip;
	uint32_t (*hubfn)(struct p8x32a_jst *st);
	uint64_t *pnow;        /* p8x32a.now, set to h by a block's hub read as event_run sets it */
	uint32_t ca, csz;      /* test builds: chkfn checks a block's hub read of csz bytes at ca, at st->latch + 2 */
	void (*chkfn)(struct p8x32a_jst *st);
} p8x32a_jst;
#define P8X32A_JOUT 256 /* the OUTA writes a run of blocks may leave */
#define P8X32A_JN 64    /* journal entries */
#ifndef P8X32A_LZH
#define P8X32A_LZH 8    /* a lazy cog's pin changes after its catch-up end (at most 2: writes 4 cycles apart, in effect 1-5 cycles on) */
#endif

struct p8x32a_jblk {
	uint32_t (*fn)(p8x32a_jst *st);
	const void *body; /* entry for a block reached from another */
	unsigned len, valid, part; /* part: stops before the slot its budget does not reach (a lazy cog's block) */
	uint32_t words[P8X32A_JMAX], dyn; /* dyn: bit k set, slot k is read at run time */
};

/* translate the run at cog address a: ix, then the words after it in ram; var[k] holds the bits in which slot k
   has changed (0: fixed, only S/D: read at run time, else not translated); old is the block this one replaces;
   outa: OUTA writes are translated too, each left in st's ot/ov (a lazy cog) */
typedef p8x32a_jblk *(*p8x32a_jit_fn)(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var, int outa);

/* a decoded instruction word, valid while word matches the instruction being run */
typedef struct p8x32a_dec {
	uint32_t word;
	uint16_t src, dst;
	uint8_t kind, fl, cond, jh; /* jh: a hub read or write a translated block may end with */
} p8x32a_dec;

typedef struct p8x32a {
	p8x32a_bus bus;
	uint8_t hub[65536];
	p8x32a_cog cog[8];
	uint8_t cog_e, lock_e, lock_state, cfg, sys_q, sys_c;
	uint64_t now, horizon, flushed, slot_base;
	uint64_t pend[40];
	uint32_t pend_pins[40], pend_what[40]; /* the pins a pending point may change, and the registers that change there */
	int npend;
	uint32_t last_out, last_dir;
	uint32_t logged;
	int stop;
	int ctr_ok;
	uint64_t ctr_from, ctr_nt;
	uint16_t nco_mask, nco_ok; /* counters (2 * cog + ctr) with an NCO mode now or before their last write; cached next changes */
	uint8_t nco_n, nco_list[16];
	int pins_ok; /* reg_out, reg_dir, cog_dir, nco_lvl hold the pins since the last change point */
	uint32_t reg_out, reg_dir, cog_dir[8], cog_out[8], nco_lvl[16];
	uint64_t nco_from[16], nco_nt[16];
	uint8_t sleepers;
	uint8_t lz_on, lz, lz_nh;   /* the lazy cog; its pin changes waiting for their time */
	uint32_t lazy_ok;           /* pins a lazy cog may drive (host: only bus.lazy_pins watches them); 0 = none */
	uint32_t lz_pins, lz_out, lz_hout[P8X32A_LZH];
	p8x32a_reg lz_reg;          /* the lazy cog's OUTA */
	uint64_t lz_at, lz_to, lz_evt, lz_ht[P8X32A_LZH], lz_try[8]; /* lz_evt: its next event (its ev_t is out of the schedule) */
	uint32_t lz_wait[8];
	uint64_t lazies;            /* lazy cogs entered */
	/* the journal: other cogs' hub writes the lazy cog has not reached, with the bytes they replaced, in time order */
	uint8_t jn, jn_off;         /* entries; 1: none (the lazy cog catches up before each such write) */
	uint8_t jn_sz[P8X32A_JN], jn_old[P8X32A_JN][4];
	uint16_t jn_a[P8X32A_JN];
	uint64_t jn_t[P8X32A_JN];
	uint8_t jmap[2048];         /* bit a / 4: hub long a has an entry */
	uint64_t jn_full, jn_writes; /* catch-ups at a write because the journal was full; entries made */
	unsigned sched_gen; /* counts changes one cog makes to another cog's next event */
	uint64_t sleeps; /* idle loops entered */
	p8x32a_loop loop[8];
	p8x32a_dec dec[8][512];
	p8x32a_jit_fn jit_build; /* NULL: no translation */
	void *jit;
	p8x32a_jblk *jblk[8][512];
	p8x32a_jlink jlink[8][512];
	/* the resume entry of the block that issued cog n's hub read or write jres_i at jres_px - 1, while jep[n] (counts the
	   cog's blocks translated again, which frees their code) is jres_ep[n] */
	uint32_t (*jres[8])(p8x32a_jst *st);
	uint32_t jres_i[8];
	unsigned jres_px[8], jres_ep[8], jep[8];
	uint32_t jvar[8][512];
	uint64_t jit_refused; /* block lookups left to the interpreter: the slot's word changed beyond its S and D fields */
	uint8_t jcode[8][64];
	uint64_t jot[P8X32A_JOUT];
	uint32_t jov[P8X32A_JOUT];
} p8x32a;

void p8x32a_init(p8x32a *p, const p8x32a_bus *bus);
void p8x32a_reset(p8x32a *p, uint64_t t);
void p8x32a_run_until(p8x32a *p, uint64_t t);
uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir);
uint8_t p8x32a_hub_at(const p8x32a *p, uint32_t a, uint64_t t); /* hub RAM as the lazy cog reads it at t */
unsigned p8x32a_dasm(char *buf, uint32_t op);

#ifdef __cplusplus
}
#endif

#endif
