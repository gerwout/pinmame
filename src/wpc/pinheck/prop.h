#ifndef PINHECK_PROP_H
#define PINHECK_PROP_H

#include "../../cpu/p8x32a/p8x32a.h"
#include "eeprom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROP_EDGES 4096
#define PROP_SEGS  32
#define PROP_PIC_PINS ((1u << 24) | (1u << 25) | (1u << 26))

typedef int (*prop_spi_fn)(void *ctx, int cs, int sclk, int mosi);
typedef void (*prop_log_fn)(void *ctx, const char *msg);
typedef void (*prop_tx_fn)(void *ctx, uint64_t pic_cycle, int level);
typedef void (*prop_ctr_fn)(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq);
typedef void (*prop_pins_fn)(void *ctx, uint64_t prop_cycle, uint32_t out, uint32_t dir);
typedef uint64_t (*prop_clock_fn)(void *ctx);

typedef struct prop_edge { uint64_t pic, prop; uint32_t pins; } prop_edge; /* prop = pic in Propeller cycles */
typedef struct prop_seg { uint64_t pic0, prop0; uint32_t num, den; } prop_seg;

typedef struct pinheck_prop {
	p8x32a chip;
	cat24m01 eeprom;
	uint8_t *eemem;
	prop_edge edge[PROP_EDGES];
	int head, count;
	uint32_t base_pins, last_pins;
	prop_seg seg[PROP_SEGS];
	int nseg;
	uint64_t pic_last;
	uint32_t ee_bits, sd_do;
	uint32_t po_out, po_dir; /* the pins the devices last saw; po_ok 0: all see the next change */
	int po_ok;
	int reset_pending;
	prop_spi_fn sd;
	void *sd_ctx;
	prop_log_fn log;
	void *log_ctx;
	prop_tx_fn tx;
	void *tx_ctx;
	int tx_level;
	prop_ctr_fn snd_ctr;
	prop_pins_fn snd_pins;
	void *snd_ctx;
	prop_pins_fn pins;
	void *pins_ctx;
	prop_clock_fn clock; /* the PIC32 cycle now */
	void *clock_ctx;
	uint64_t stamp; /* the PIC32 cycle at which the running call was made */
	void *worker; /* worker thread, NULL = calls run inline */
} pinheck_prop;

void prop_init(pinheck_prop *p, const uint8_t *rom32k, uint8_t *eemem);
void prop_attach_sd(pinheck_prop *p, prop_spi_fn fn, void *ctx);
void prop_set_log(pinheck_prop *p, prop_log_fn fn, void *ctx);
void prop_set_tx(pinheck_prop *p, prop_tx_fn fn, void *ctx);
void prop_set_sound(pinheck_prop *p, prop_ctr_fn ctr, prop_pins_fn pins, void *ctx);
void prop_set_pins(pinheck_prop *p, prop_pins_fn fn, void *ctx);
void prop_reset(pinheck_prop *p, uint64_t pic_cycle);
void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins);
void prop_catch_up(pinheck_prop *p, uint64_t pic_cycle);
int prop_p24(pinheck_prop *p, uint64_t pic_cycle);
void prop_set_clock(pinheck_prop *p, prop_clock_fn fn, void *ctx);
uint64_t prop_stamp(const pinheck_prop *p);
int prop_start_thread(pinheck_prop *p);
void prop_stop_thread(pinheck_prop *p);
void prop_sync(pinheck_prop *p);
uint64_t prop_time(pinheck_prop *p, uint64_t pic_cycle);

#ifdef __cplusplus
}
#endif

#endif
