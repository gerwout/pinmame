/* lookdump shape,brightness,position,bar ...: a pseudo-random frame, then its rendering in each look, raw to stdout */
#include "display.h"
#include <stdio.h>

int main(int argc, char **argv)
{
	static const uint8_t colours[4] = { 0x00, 0xFF, 0xE0, 0x49 };
	static uint8_t frame[DISPLAY_FRAME], rgb[DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3];
	uint32_t seed = 1;
	int i;
	for (i = 0; i < DISPLAY_FRAME; i++) {
		seed = seed * 1103515245u + 12345u;
		frame[i] = colours[(seed >> 16) & 3];
	}
	fwrite(frame, 1, sizeof(frame), stdout);
	for (i = 1; i < argc; i++) {
		display_look lk;
		if (sscanf(argv[i], "%d,%d,%d,%d", &lk.shape, &lk.brightness, &lk.position, &lk.bar) != 4) return 2;
		pinheck_display_render(&lk, frame, rgb);
		fwrite(rgb, 1, sizeof(rgb), stdout);
	}
	return 0;
}
