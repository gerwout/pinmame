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
typedef void (*prop_pins_fn)(void *ctx, uint64_t prop_cycle, uint32_t out, uint32_t dir);

typedef struct prop_edge { uint64_t pic; uint32_t pins; } prop_edge;
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
	int reset_pending;
	prop_spi_fn sd;
	void *sd_ctx;
	prop_log_fn log;
	void *log_ctx;
	prop_tx_fn tx;
	void *tx_ctx;
	int tx_level;
	prop_pins_fn pins;
	void *pins_ctx;
} pinheck_prop;

void prop_init(pinheck_prop *p, const uint8_t *rom32k, uint8_t *eemem);
void prop_attach_sd(pinheck_prop *p, prop_spi_fn fn, void *ctx);
void prop_set_log(pinheck_prop *p, prop_log_fn fn, void *ctx);
void prop_set_tx(pinheck_prop *p, prop_tx_fn fn, void *ctx);
void prop_set_pins(pinheck_prop *p, prop_pins_fn fn, void *ctx);
void prop_reset(pinheck_prop *p, uint64_t pic_cycle);
void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins);
void prop_catch_up(pinheck_prop *p, uint64_t pic_cycle);
int prop_p24(pinheck_prop *p, uint64_t pic_cycle);
uint64_t prop_time(const pinheck_prop *p, uint64_t pic_cycle);

#ifdef __cplusplus
}
#endif

#endif
