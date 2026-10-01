#include "hexload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0x1D000000u
#define SIZE 0x80000u

static uint8_t flash[SIZE];
static int fails;

static uint32_t crc32(const uint8_t *p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	int k;
	while (n--) {
		c ^= *p++;
		for (k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

/* converts text; want: bytes written, or -1 with err starting with why */
static void expect(const char *name, const char *text, long want, const char *why)
{
	char err[128] = "";
	long got;
	memset(flash, 0xFF, SIZE);
	got = pinheck_hex_flash((const uint8_t *)text, strlen(text), flash, SIZE, BASE, err);
	if (got != want || (want < 0 && strncmp(err, why, strlen(why)))) {
		printf("HEX FAIL %s: got %ld '%s', want %ld '%s'\n", name, got, err, want, why);
		fails++;
	}
}

int main(void)
{
	const char *path = getenv("AMH_HEX");
	const char *good = ":020000041D00DD\n:0400F8000000009D67\n:00000001FF\n";
	expect("records", good, 4, "");
	if (memcmp(flash + 0xF8, "\x00\x00\x00\x9d", 4) || flash[0xF7] != 0xFF || flash[0xFC] != 0xFF) { printf("HEX FAIL records: bytes\n"); fails++; }
	expect("crlf and lower case", ":020000041d00dd\r\n:0400f8000000009d67\r\n:00000001ff\r\n", 4, "");
	expect("start address", ":020000041D00DD\n:040000059D0010004A\n:00000001FF\n", 0, "");
	expect("checksum", ":020000041D00DD\n:0400F8000000009D66\n:00000001FF\n", -1, "line 2: checksum");
	expect("no end", ":020000041D00DD\n:0400F8000000009D67\n", -1, "no end record");
	expect("after end", ":00000001FF\n:00000001FF\n", -1, "line 2: data after");
	expect("below flash", ":0400F8000000009D67\n:00000001FF\n", -1, "line 1: address 000000f8 outside");
	expect("past flash", ":020000041D07D6\n:04FFFE0001020304F5\n:00000001FF\n", -1, "line 2: address 1d07fffe outside");
	expect("boot flash", ":020000041FC01B\n:0400000000000000FC\n:00000001FF\n", -1, "line 2: address 1fc00000 outside");
	expect("segment record", ":020000021000EC\n:00000001FF\n", -1, "line 1: record type 02");
	expect("short", ":0400F8000000\n", -1, "line 1: short record");
	expect("digit", ":0400F80000G0009D67\n:00000001FF\n", -1, "line 1: not a hex digit");
	expect("mark", "0400F8000000009D67\n", -1, "line 1: no record mark");
	if (path) {
		static uint8_t text[1 << 20];
		char err[128] = "";
		FILE *f = fopen(path, "rb");
		size_t n = f ? fread(text, 1, sizeof(text), f) : 0;
		long got;
		if (f) fclose(f);
		memset(flash, 0xFF, SIZE);
		got = pinheck_hex_flash(text, n, flash, SIZE, BASE, err);
		if (n != 705004 || got != 243952 || crc32(flash, 0x3C1D8) != 0x09D9072Fu) {
			printf("HEX FAIL AMH_V023.hex: %lu bytes read, %ld written (%s), image crc %08x\n", (unsigned long)n, got, err, (unsigned)crc32(flash, 0x3C1D8));
			fails++;
		} else {
			size_t k;
			for (k = 0x3C1D8; k < SIZE && flash[k] == 0xFF; k++) ;
			if (k != SIZE) { printf("HEX FAIL AMH_V023.hex: byte %lx past the image is set\n", (unsigned long)k); fails++; }
			else printf("hex: AMH_V023.hex -> %ld bytes, image 0x9D000000-0x9D03C1D7 crc 09d9072f, entry %02x%02x%02x%02x\n", got, flash[0x1003], flash[0x1002], flash[0x1001], flash[0x1000]);
		}
	}
	printf("hex: %d failed\n", fails);
	return fails != 0;
}
