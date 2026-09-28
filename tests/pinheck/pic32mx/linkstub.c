#include "linkstub.h"
#include <string.h>

#define RF5  (1u << 5)
#define RF12 (1u << 12)
#define RF13 (1u << 13)

void linkstub_reset_link(linkstub *l)
{
	l->clk = 0;
	l->bit = 0;
	l->rf13 = 1;
	memset(l->rx, 0, sizeof(l->rx));
	memset(l->reply, 0, sizeof(l->reply));
}

void linkstub_init(linkstub *l)
{
	memset(l, 0, sizeof(*l));
	memset(l->words, 0xFF, sizeof(l->words));
	linkstub_reset_link(l);
}

static void packet(linkstub *l)
{
	uint8_t *rx = l->rx;
	uint32_t a = (uint32_t)(rx[0] | rx[1] << 8) % LINKSTUB_WORDS, w;
	int i;

	l->packets++;
	if (rx[15] != 0xFF) memset(l->reply, 0, sizeof(l->reply));
	if (rx[15] == 0x23 && rx[0] == 0xAA) {
		for (i = 0; i < 14; i++) l->reply[i] = (uint8_t)(0x30 + i);
		l->reply[14] = l->reply[15] = 0xAA;
	} else if (rx[15] == 0x21) {
		w = l->words[a];
		for (i = 0; i < 4; i++) l->reply[i] = (uint8_t)(w >> (8 * i));
		l->reply[14] = (uint8_t)(rx[14] | 0x80);
		l->reply[15] = 0xAD;
	} else if (rx[15] == 0x20) {
		l->words[a] = (uint32_t)rx[4] | (uint32_t)rx[5] << 8 | (uint32_t)rx[6] << 16 | (uint32_t)rx[7] << 24;
		l->reply[15] = 0x42;
	}
	memset(l->rx, 0, sizeof(l->rx));
}

void linkstub_portf_write(linkstub *l, uint32_t latf)
{
	int clk = (latf & RF12) != 0;
	if (l->clk && !clk) {
		int k = l->bit++;
		if (latf & RF5) l->rx[k >> 3] |= (uint8_t)(1u << (k & 7));
		l->rf13 = (l->reply[k >> 3] >> (k & 7)) & 1;
		if (l->bit == 128) { l->bit = 0; packet(l); }
	}
	l->clk = clk;
}

uint32_t linkstub_portf_read(const linkstub *l)
{
	return (0xFFFFu & ~RF13) | (l->rf13 ? RF13 : 0);
}
