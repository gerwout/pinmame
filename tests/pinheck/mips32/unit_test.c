#include "mips32.h"
#include <stdio.h>
#include <string.h>

static uint8_t kmem[0x10000], umem[0x10000];
static int fails;
static mips32_state *eic_cpu; /* a store to physical 0xFFF0 raises its EIC request */

#define CHECK(c) do { if (!(c)) { printf("UNIT FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t *map(uint32_t pa, int size)
{
	if (pa < sizeof(kmem) - (unsigned)size) return kmem + pa;
	if (pa >= 0x40000000u && pa - 0x40000000u < sizeof(umem) - (unsigned)size) return umem + (pa - 0x40000000u);
	return NULL;
}

static uint32_t rd(void *ctx, uint32_t pa, int size, int fetch, int *err)
{
	uint8_t *p = map(pa, size);
	uint32_t v = 0;
	int i;
	(void)ctx; (void)fetch;
	if (!p) { *err = 1; return 0; }
	for (i = 0; i < size; i++) v |= (uint32_t)p[i] << (8 * i);
	return v;
}

static void wr(void *ctx, uint32_t pa, uint32_t v, int size, int *err)
{
	uint8_t *p = map(pa, size);
	int i;
	(void)ctx;
	if (pa == 0xFFF0 && eic_cpu) { mips32_set_eic(eic_cpu, 1, 0, 0); return; }
	if (!p) { *err = 1; return; }
	for (i = 0; i < size; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put(uint8_t *m, uint32_t off, uint32_t w)
{
	m[off] = (uint8_t)w; m[off + 1] = (uint8_t)(w >> 8); m[off + 2] = (uint8_t)(w >> 16); m[off + 3] = (uint8_t)(w >> 24);
}

static void setup(mips32_state *s, uint32_t pc, uint32_t status)
{
	mips32_bus bus = { NULL, rd, wr, NULL, NULL };
	memset(kmem, 0, sizeof(kmem));
	memset(umem, 0, sizeof(umem));
	mips32_init(s, &bus, 2, 0x00018700u);
	s->status = status;
	s->pc = pc;
	s->npc = pc + 4;
}

static void irq_in_delay_slot(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0x00000001u);
	put(kmem, 0x1000, 0x10000002u);
	put(kmem, 0x1004, 0x25080001u);
	put(kmem, 0x1008, 0x25080064u);
	put(kmem, 0x100C, 0x00000000u);
	put(kmem, 0x0184, 0x42000018u);
	mips32_run(&s, 1);
	mips32_set_eic(&s, 1, 0, 0);
	mips32_run(&s, 1);
	CHECK(s.epc == 0x80001000u);
	CHECK(s.cause & 0x80000000u);
	CHECK(s.pc == 0x80000184u);
	mips32_set_eic(&s, 0, 0, 0);
	mips32_run(&s, 1);
	CHECK(s.pc == 0x80001000u);
	mips32_run(&s, 3);
	CHECK(mips32_regs(&s)[8] == 1);
	CHECK(s.pc == 0x80001010u);
}

static void exception_with_exl_set(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0x00000002u);
	s.epc = 0x11111111u;
	s.srsctl = 0x1000u;
	put(kmem, 0x1000, 0x10000001u);
	put(kmem, 0x1004, 0x0000000Du);
	mips32_run(&s, 2);
	CHECK(s.epc == 0x11111111u);
	CHECK(!(s.cause & 0x80000000u));
	CHECK(((s.cause >> 2) & 31) == MIPS32_EXC_BP);
	CHECK(s.pc == 0x80000180u);
	CHECK((s.srsctl & 15) == 0);
}

static void compare_crossed_by_div(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0);
	s.compare = 10;
	mips32_regs(&s)[9] = 3;
	put(kmem, 0x1000, 0x0109001Au);
	mips32_run(&s, 1);
	CHECK(s.count == 17);
	CHECK(mips32_timer_irq(&s));

	setup(&s, 0x80001000u, 0);
	s.count = 100;
	s.compare = 100;
	mips32_run(&s, 2);
	CHECK(s.count == 101);
	CHECK(!mips32_timer_irq(&s));
}

/* Count advances every other cycle: the Timer stops the run after the instruction on whose cycle Count reaches Compare */
static void timer_fires_on_its_cycle(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0);
	s.compare = 5;
	CHECK(mips32_run(&s, 100) == 10);
	CHECK(mips32_timer_irq(&s) && s.count == 5);

	setup(&s, 0x80001000u, 0);
	s.count_half = 1;
	s.compare = 5;
	CHECK(mips32_run(&s, 100) == 9);

	setup(&s, 0x80001000u, 0);
	put(kmem, 0x1000, 0x24080014u);
	put(kmem, 0x1004, 0x40885800u);
	CHECK(mips32_run(&s, 100) == 40);
	CHECK(mips32_timer_irq(&s) && s.count == 20);

	/* Compare written below Count: the Timer waits for Count to wrap */
	setup(&s, 0x80001000u, 0);
	s.count = 10;
	put(kmem, 0x1028, 0x2408000Cu);
	put(kmem, 0x102C, 0x40885800u);
	CHECK(mips32_run(&s, 100) == 100);
	CHECK(!mips32_timer_irq(&s) && s.count == 60);
}

/* an interrupt raised by a store's bus callback is taken before the next instruction */
static void eic_raised_by_a_store(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0x00000001u);
	put(kmem, 0x1000, 0x3C098001u);
	put(kmem, 0x1004, 0xAD20FFF0u);
	put(kmem, 0x1008, 0x24080001u);
	eic_cpu = &s;
	mips32_run(&s, 3);
	eic_cpu = NULL;
	CHECK(s.epc == 0x80001008u);
	CHECK(mips32_regs(&s)[8] == 0);
	CHECK(s.pc == 0x80000184u);
}

static void user_mode(void)
{
	mips32_state s;
	uint32_t pa = 0;

	setup(&s, 0x00001000u, 0x00000010u);
	CHECK(mips32_translate(&s, 0x00001000u, &pa) && pa == 0x40001000u);
	CHECK(!mips32_translate(&s, 0x80000000u, &pa));
	mips32_regs(&s)[9] = 0x80000000u;
	put(umem, 0x1000, 0x8D280000u);
	mips32_run(&s, 1);
	CHECK(((s.cause >> 2) & 31) == MIPS32_EXC_ADEL);
	CHECK(s.badvaddr == 0x80000000u);
	CHECK(s.epc == 0x00001000u);
	CHECK(s.pc == 0x80000180u);

	setup(&s, 0x00001000u, 0x00000010u);
	mips32_regs(&s)[9] = 0xA0000000u;
	put(umem, 0x1000, 0xAD280000u);
	mips32_run(&s, 1);
	CHECK(((s.cause >> 2) & 31) == MIPS32_EXC_ADES);

	setup(&s, 0x00001000u, 0x00000010u);
	put(umem, 0x1000, 0x40086000u);
	mips32_run(&s, 1);
	CHECK(((s.cause >> 2) & 31) == MIPS32_EXC_CPU);
	CHECK(((s.cause >> 28) & 3) == 0);

	setup(&s, 0x00001000u, 0x00000014u);
	CHECK(mips32_translate(&s, 0x00001000u, &pa) && pa == 0x00001000u);
}

static void state_copy_is_independent(void)
{
	mips32_state a, b;
	setup(&a, 0x80001000u, 0);
	put(kmem, 0x1000, 0x24080005u);
	memcpy(&b, &a, sizeof(a));
	mips32_run(&b, 1);
	CHECK(b.gpr[0][8] == 5);
	CHECK(a.gpr[0][8] == 0);
}

int main(void)
{
	state_copy_is_independent();
	irq_in_delay_slot();
	exception_with_exl_set();
	compare_crossed_by_div();
	timer_fires_on_its_cycle();
	eic_raised_by_a_store();
	user_mode();
	printf("unit: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
