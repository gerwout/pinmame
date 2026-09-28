#ifndef PINHECK_LINKSTUB_H
#define PINHECK_LINKSTUB_H

#include <stdint.h>

#define LINKSTUB_WORDS 0x4000

typedef struct linkstub {
	int clk, bit, rf13, packets;
	uint8_t rx[16], reply[16];
	uint32_t words[LINKSTUB_WORDS];
} linkstub;

void linkstub_init(linkstub *l);
void linkstub_reset_link(linkstub *l);
void linkstub_portf_write(linkstub *l, uint32_t latf);
uint32_t linkstub_portf_read(const linkstub *l);

#endif
