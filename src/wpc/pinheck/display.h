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

/* the look: the module's service-menu settings, from its 14-byte config packet */
#define DISPLAY_LOOK_W  (2 * DISPLAY_W)
#define DISPLAY_LOOK_H  (2 * DISPLAY_H)
#define DISPLAY_ROUND   0
#define DISPLAY_SQUARE  1
#define DISPLAY_HIGHREZ 2

typedef struct display_look {
	int shape;      /* PIXEL SHAPE: DISPLAY_ROUND, DISPLAY_SQUARE or DISPLAY_HIGHREZ */
	int brightness; /* BRIGHTNESS: 175-255 in the menu, 255 = the colours as sent */
	int position;   /* POSITION: 300-500 in the menu, 340 = aligned */
	int bar;        /* BAR BRIGHT: 0-62 in the menu, the light between round dots */
} display_look;

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

void pinheck_display_init(display *d, void *ctx, display_frame_fn on_frame, display_config_fn on_config, display_log_fn log);
void pinheck_display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir);
int pinheck_display_look(display_look *look, const uint8_t *cfg, int n);
void pinheck_display_render(const display_look *look, const uint8_t *frame, uint8_t *rgb);

#ifdef __cplusplus
}
#endif

#endif
