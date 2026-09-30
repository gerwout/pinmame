#include "prop.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

static void do_catch_up(pinheck_prop *p, uint64_t pic_cycle);
static void do_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins);

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
	if (p->pins && (ch & p->pins_mask)) p->pins(p->pins_ctx, t, out, dir);
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
	p->pins_mask = 0xFFFFFFFFu;
}

void prop_set_pins_mask(pinheck_prop *p, uint32_t mask)
{
	p->pins_mask = mask;
	p->po_ok = 0;
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
	prop_sync(p);
	p->stamp = p->clock ? p->clock(p->clock_ctx) : 0;
	do_catch_up(p, p->pic_last);
	p->pic_last = pic_cycle;
	p->head = p->count = 0;
	p->base_pins = p->last_pins = 0;
	restart(p, p->chip.now);
	p->seg[0].pic0 = pic_cycle;
	retime(p);
}

uint64_t prop_time(pinheck_prop *p, uint64_t pic_cycle)
{
	prop_sync(p);
	return to_prop(p, pic_cycle);
}

void prop_set_clock(pinheck_prop *p, prop_clock_fn fn, void *ctx)
{
	p->clock = fn;
	p->clock_ctx = ctx;
}

uint64_t prop_stamp(const pinheck_prop *p) { return p->stamp; }

static void do_catch_up(pinheck_prop *p, uint64_t pic_cycle)
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

static void do_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins)
{
	if (pic_cycle > p->pic_last) p->pic_last = pic_cycle;
	if (pins == p->last_pins) return;
	if (p->count == PROP_EDGES) do_catch_up(p, pic_cycle);
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
	prop_sync(p);
	out = p8x32a_pins(&p->chip, p->chip.now, &dir);
	return (dir & PIN_P24) ? (out & PIN_P24) != 0 : 0;
}

/* The calls from the PIC32 side can run on a worker thread: they queue in order and run there exactly as they
   would inline, each with the PIC32 cycle at which it was made. The caller waits for the queue to drain before
   it reads Propeller state (prop_sync). */
#define PROP_Q 4096
#define PROP_SPIN 20000

enum { CMD_PINS, CMD_CATCH_UP, CMD_QUIT };

typedef struct prop_cmd { uint64_t pic, stamp; uint32_t pins; int kind; } prop_cmd;

#ifdef PINHECK_NO_THREADS
int prop_start_thread(pinheck_prop *p) { (void)p; return -1; }
void prop_stop_thread(pinheck_prop *p) { (void)p; }
void prop_sync(pinheck_prop *p) { (void)p; }
static void post(pinheck_prop *p, int kind, uint64_t pic, uint32_t pins) { (void)p; (void)kind; (void)pic; (void)pins; }
#else
#ifdef _WIN32
#include <windows.h>
#include <process.h>
typedef struct prop_os { HANDLE th, ev, sev; } prop_os;
static unsigned get_acq(volatile unsigned *v) { unsigned r = *v; MemoryBarrier(); return r; }
static void put_rel(volatile unsigned *v, unsigned x) { MemoryBarrier(); *v = x; }
static unsigned xchg(volatile unsigned *v, unsigned x) { return (unsigned)InterlockedExchange((volatile LONG *)v, (LONG)x); }
static uint64_t self_id(void) { return GetCurrentThreadId(); }
#define CPU_RELAX_ANY() YieldProcessor()
static unsigned get_sc(volatile unsigned *v) { MemoryBarrier(); return *v; }
static void put_sc(volatile unsigned *v, unsigned x) { InterlockedExchange((volatile LONG *)v, (LONG)x); }
#else
#include <pthread.h>
typedef struct prop_os { pthread_t th; pthread_mutex_t m; pthread_cond_t c, sc; int wake; } prop_os;
static unsigned get_acq(volatile unsigned *v) { return __atomic_load_n(v, __ATOMIC_ACQUIRE); }
static void put_rel(volatile unsigned *v, unsigned x) { __atomic_store_n(v, x, __ATOMIC_RELEASE); }
static unsigned xchg(volatile unsigned *v, unsigned x) { return __atomic_exchange_n(v, x, __ATOMIC_SEQ_CST); }
#if defined(__x86_64__) || defined(__i386__)
#define CPU_RELAX() __asm__ __volatile__("pause")
#elif defined(__aarch64__) || (defined(__arm__) && (__ARM_ARCH >= 7 || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6KZ__)))
#define CPU_RELAX() __asm__ __volatile__("yield") /* ARMv6 before 6K has no yield */
#else
#define CPU_RELAX() ((void)0)
#endif
static uint64_t self_id(void)
{
	pthread_t t = pthread_self();
	uint64_t r = 0;
	memcpy(&r, &t, sizeof(t) < sizeof(r) ? sizeof(t) : sizeof(r));
	return r;
}
#define CPU_RELAX_ANY() CPU_RELAX()
static unsigned get_sc(volatile unsigned *v) { return __atomic_load_n(v, __ATOMIC_SEQ_CST); }
static void put_sc(volatile unsigned *v, unsigned x) { __atomic_store_n(v, x, __ATOMIC_SEQ_CST); }
#endif

typedef struct prop_worker {
	prop_os os;
	prop_cmd q[PROP_Q];
	volatile unsigned head, tail; /* head: next command the worker runs; tail: next free slot */
	volatile unsigned sleeping;
	volatile unsigned waiting, want; /* the poster blocks until head reaches want */
} prop_worker;

/* the worker, idle for a while, blocks until the next post */
static void idle_wait(prop_worker *w, unsigned h)
{
	if (xchg(&w->sleeping, 1) == 0 && get_acq(&w->tail) == h) {
#ifdef _WIN32
		WaitForSingleObject(w->os.ev, INFINITE);
#else
		pthread_mutex_lock(&w->os.m);
		while (!w->os.wake) pthread_cond_wait(&w->os.c, &w->os.m);
		w->os.wake = 0;
		pthread_mutex_unlock(&w->os.m);
#endif
	}
	xchg(&w->sleeping, 0);
}

static void wake(prop_worker *w)
{
	if (!xchg(&w->sleeping, 0)) return;
#ifdef _WIN32
	SetEvent(w->os.ev);
#else
	pthread_mutex_lock(&w->os.m);
	w->os.wake = 1;
	pthread_cond_signal(&w->os.c);
	pthread_mutex_unlock(&w->os.m);
#endif
}

/* the poster waits for head to reach want: a bounded spin, then it blocks until the worker gets there */
static void wait_head(prop_worker *w, unsigned want)
{
	int k;
	for (k = 0; k < PROP_SPIN; k++) {
		if ((int)(get_acq(&w->head) - want) >= 0) return;
		CPU_RELAX_ANY();
	}
#ifdef _WIN32
	w->want = want;
	xchg(&w->waiting, 1);
	while ((int)(get_sc(&w->head) - want) < 0) WaitForSingleObject(w->os.sev, INFINITE);
	xchg(&w->waiting, 0);
#else
	pthread_mutex_lock(&w->os.m);
	w->want = want;
	xchg(&w->waiting, 1);
	while ((int)(get_sc(&w->head) - want) < 0) pthread_cond_wait(&w->os.sc, &w->os.m);
	xchg(&w->waiting, 0);
	pthread_mutex_unlock(&w->os.m);
#endif
}

/* the worker moves head to h and wakes a poster waiting for it */
static void head_moved(prop_worker *w, unsigned h)
{
	put_sc(&w->head, h);
	if (!get_sc(&w->waiting) || (int)(h - w->want) < 0) return;
#ifdef _WIN32
	SetEvent(w->os.sev);
#else
	pthread_mutex_lock(&w->os.m);
	pthread_cond_signal(&w->os.sc);
	pthread_mutex_unlock(&w->os.m);
#endif
}

static void run_cmd(pinheck_prop *p, const prop_cmd *c)
{
	p->stamp = c->stamp;
	if (c->kind == CMD_CATCH_UP) do_catch_up(p, c->pic);
	else do_pic_pins(p, c->pic, c->pins);
}

#ifdef _WIN32
static unsigned __stdcall worker(void *arg)
#else
static void *worker(void *arg)
#endif
{
	pinheck_prop *p = (pinheck_prop *)arg;
	prop_worker *w = (prop_worker *)p->worker;
	unsigned h = w->head;
	int k = 0;
	for (;;) {
		unsigned t = get_acq(&w->tail);
		if (h == t) {
			if (++k < PROP_SPIN) CPU_RELAX_ANY();
			else { idle_wait(w, h); k = 0; }
			continue;
		}
		k = 0;
		if (w->q[h % PROP_Q].kind == CMD_QUIT) { put_rel(&w->head, h + 1); break; }
		run_cmd(p, &w->q[h % PROP_Q]);
		head_moved(w, ++h);
	}
	return 0;
}

static void post(pinheck_prop *p, int kind, uint64_t pic, uint32_t pins)
{
	prop_worker *w = (prop_worker *)p->worker;
	unsigned t = w->tail;
	prop_cmd *c;
	if (t - get_acq(&w->head) >= PROP_Q) wait_head(w, t - PROP_Q + 1);
	c = &w->q[t % PROP_Q];
	c->kind = kind;
	c->pic = pic;
	c->pins = pins;
	c->stamp = p->clock ? p->clock(p->clock_ctx) : pic;
	put_rel(&w->tail, t + 1);
	wake(w);
}

/* only the thread that owns the worker waits; another (a host reading NVRAM) reads the state as it is */
void prop_sync(pinheck_prop *p)
{
	prop_worker *w;
	if (p->owner != self_id()) return;
	w = (prop_worker *)p->worker;
	if (w) wait_head(w, w->tail);
}

int prop_start_thread(pinheck_prop *p)
{
	prop_worker *w;
	if (p->worker) return 0;
	w = (prop_worker *)calloc(1, sizeof(*w));
	if (!w) return -1;
	p->worker = w;
	p->owner = self_id();
#ifdef _WIN32
	w->os.ev = CreateEvent(NULL, FALSE, FALSE, NULL);
	w->os.sev = CreateEvent(NULL, FALSE, FALSE, NULL);
	/* the CRT's thread start (callbacks use it), at the priority of the thread that runs the emulation */
	w->os.th = w->os.ev && w->os.sev ? (HANDLE)_beginthreadex(NULL, 0, worker, p, CREATE_SUSPENDED, NULL) : NULL;
	if (!w->os.th) {
		if (w->os.ev) CloseHandle(w->os.ev);
		if (w->os.sev) CloseHandle(w->os.sev);
		free(w);
		p->worker = NULL;
		return -1;
	}
	SetThreadPriority(w->os.th, GetThreadPriority(GetCurrentThread()));
	ResumeThread(w->os.th);
#else
	pthread_mutex_init(&w->os.m, NULL);
	pthread_cond_init(&w->os.c, NULL);
	pthread_cond_init(&w->os.sc, NULL);
	if (pthread_create(&w->os.th, NULL, worker, p)) {
		pthread_cond_destroy(&w->os.c);
		pthread_cond_destroy(&w->os.sc);
		pthread_mutex_destroy(&w->os.m);
		free(w);
		p->worker = NULL;
		return -1;
	}
#endif
	return 0;
}

void prop_stop_thread(pinheck_prop *p)
{
	prop_worker *w = (prop_worker *)p->worker;
	if (!w) return;
	post(p, CMD_QUIT, 0, 0);
#ifdef _WIN32
	WaitForSingleObject(w->os.th, INFINITE);
	CloseHandle(w->os.th);
	CloseHandle(w->os.ev);
	CloseHandle(w->os.sev);
#else
	pthread_join(w->os.th, NULL);
	pthread_cond_destroy(&w->os.c);
	pthread_cond_destroy(&w->os.sc);
	pthread_mutex_destroy(&w->os.m);
#endif
	free(w);
	p->worker = NULL;
}
#endif

void prop_catch_up(pinheck_prop *p, uint64_t pic_cycle)
{
	if (p->worker) { post(p, CMD_CATCH_UP, pic_cycle, 0); return; }
	p->stamp = p->clock ? p->clock(p->clock_ctx) : pic_cycle;
	do_catch_up(p, pic_cycle);
}

void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins)
{
	pins &= PROP_PIC_PINS;
	if (p->worker) { post(p, CMD_PINS, pic_cycle, pins); return; }
	p->stamp = p->clock ? p->clock(p->clock_ctx) : pic_cycle;
	do_pic_pins(p, pic_cycle, pins);
}
