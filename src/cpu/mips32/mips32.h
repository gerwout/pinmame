#ifndef MIPS32_H
#define MIPS32_H

#include <stdint.h>

enum {
	MIPS32_EXC_INT  = 0,
	MIPS32_EXC_ADEL = 4,
	MIPS32_EXC_ADES = 5,
	MIPS32_EXC_IBE  = 6,
	MIPS32_EXC_DBE  = 7,
	MIPS32_EXC_SYS  = 8,
	MIPS32_EXC_BP   = 9,
	MIPS32_EXC_RI   = 10,
	MIPS32_EXC_CPU  = 11,
	MIPS32_EXC_OV   = 12,
	MIPS32_EXC_TR   = 13
};

enum { MIPS32_HOOK_DELIVER = 0, MIPS32_HOOK_SKIP = 1, MIPS32_HOOK_STOP = 2 };

typedef struct mips32_state mips32_state;

typedef struct mips32_bus {
	void *ctx;
	uint32_t (*read)(void *ctx, uint32_t pa, int size, int fetch, int *err);
	void (*write)(void *ctx, uint32_t pa, uint32_t data, int size, int *err);
	int (*exc_hook)(void *ctx, mips32_state *s, int exccode);
	void (*irq_taken)(void *ctx, int vector);
} mips32_bus;

struct mips32_state {
	uint32_t gpr[8][32];
	uint32_t pc, npc, hi, lo;
	int delay;
	uint32_t cur_pc, skip_pc;
	int cur_delay;
	int llbit;
	int waiting;
	int stop;
	int shadow_sets;
	uint32_t status, cause, epc, errorepc, badvaddr, count, compare, ebase;
	uint32_t intctl, srsctl, srsmap, hwrena, config0, prid;
	int count_half;
	int eic_ripl, eic_vector, eic_srs;
	uint64_t cycles;
	mips32_bus bus;
};

void mips32_init(mips32_state *s, const mips32_bus *bus, int shadow_sets, uint32_t prid);
void mips32_reset(mips32_state *s);
int mips32_run(mips32_state *s, int cycles);
uint32_t *mips32_regs(mips32_state *s);
void mips32_set_eic(mips32_state *s, int ripl, int vector, int srs);
int mips32_timer_irq(const mips32_state *s);
int mips32_soft_irq(const mips32_state *s);
int mips32_translate(const mips32_state *s, uint32_t va, uint32_t *pa);
uint32_t mips32_get_cp0(const mips32_state *s, int reg, int sel);
unsigned mips32_dasm(char *buf, uint32_t pc, uint32_t op);

#endif
