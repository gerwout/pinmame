#ifndef PINHECK_DISPLAY_H
#define PINHECK_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_W       128
#define DISPLAY_H       32
#define DISPLAY_FRAME   (DISPLAY_W * DISPLAY_H)
#define DISPLAY_CFG_MAX 64

#define DISPLAY_P17 (1u << 17)
#define DISPLAY_P20 (1u << 20)
#define DISPLAY_P21 (1u << 21)
#define DISPLAY_P22 (1u << 22)

typedef void (*display_frame_fn)(void *ctx, const uint8_t *frame4096, uint64_t t);
typedef void (*display_config_fn)(void *ctx, const uint8_t *bytes, int n, uint64_t t);
typedef void (*display_log_fn)(void *ctx, const char *msg);

typedef struct display {
	void *ctx;
	display_frame_fn on_frame;
	display_config_fn on_config;
	display_log_fn log;
	uint32_t level;
	uint8_t buf[DISPLAY_FRAME];
	long nbits;
	int mode;
	int logged_bits, logged_frame, logged_mixed, logged_cfg;
} display;

void display_init(display *d, void *ctx, display_frame_fn on_frame, display_config_fn on_config, display_log_fn log);
void display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir);

#ifdef __cplusplus
}
#endif

#endif
