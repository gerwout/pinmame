# Milestone 4: counter pin outputs, SD card and virtual FAT32 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The real `PRP_V008.BIN` boots through the booter and EEPROM model with an emulated SD card on P0–P3, mounts a FAT32 volume synthesised from the romset zip, and opens a file. The whole run is cycle-exact against Parallax's RTL.

**Architecture:** Three pure-C units stack under the Propeller: `zipsrc` reads romset zip entries (stored by range, deflated through an LRU cache), `vfat` presents the zip's `DMD/` and `SFX/` trees as a read-only FAT32 card image, and `sd` is an SPI-mode SD card over any block device. The `p8x32a` core gains cycle-exact NCO counter pin outputs, which the firmware's SD driver uses for its clock and data. The existing test runner and the Verilator RTL testbench attach the same SD card, so the real firmware's boot and mount are compared cycle-exact against the RTL.

**Tech Stack:** C99 (C89-syntax-clean, MSVC-compatible), system zlib (PinMAME links zlib via `find_package(ZLIB)`), Python 3, `fsck.vfat` (dosfstools 4.2), `mcopy` (mtools 4.0.49), Verilator 5.048, the pinned oracles from Milestone 3.

**Spec:** `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§4.3, §4.4, §6, §7 `vfat`/`sd`, §8 milestone 4). Roadmap: `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` ("Obligations carried into Plan 4").

## Global Constraints

- `src/wpc/pinheck/{sd,vfat,zipsrc}.[ch]` include only standard headers, zlib (`zipsrc.c` only) and each other; no PinMAME headers; headers carry `extern "C"` guards; C89-syntax-clean under `-std=c89 -pedantic-errors -Wno-long-long`; warning-free under `-std=c99 -Wall -Wextra -Werror -pedantic`.
- The APIs below are fixed with Plan 5, which is written against them. Names, types and semantics must not change:
  - `sd_blockdev { void *ctx; uint32_t sectors; int (*read)(void *ctx, uint32_t lba, uint8_t *buf512); }`, `void sd_init(sd_card *s, const sd_blockdev *dev)`, `int sd_update(sd_card *s, int cs, int sclk, int mosi)`. The return value is the level the card drives on DO, 1 = high/released.
  - `vfat_source { void *ctx; int count; const char *(*name)(void *ctx, int i); uint32_t (*size)(void *ctx, int i); int (*read)(void *ctx, int i, uint32_t off, uint8_t *buf, uint32_t len); }`, `int vfat_init(vfat *v, const vfat_source *src)`, `void vfat_free(vfat *v)`, `uint32_t vfat_sectors(const vfat *v)`, `int vfat_read(vfat *v, uint32_t lba, uint8_t *buf512)`. Only entries under `DMD/` and `SFX/` are exposed; 0 = ok.
  - `int zipsrc_open(zipsrc *z, const char *zip_path, uint32_t cache_bytes)`, `void zipsrc_close(zipsrc *z)`, `const vfat_source *zipsrc_source(zipsrc *z)`. Names are zip paths such as `DMD/_DA/AA0.VID`; directory entries keep their trailing `/` and have size 0.
- This milestone is headless. No PinMAME build file is touched; Plan 5 wires `sd.c`, `vfat.c`, `zipsrc.c` and `src/cpu/p8x32a/` into the build.
- The P8X32A mask ROM (GPL 3.0) and the romset zip are never committed. Tests take them from `P8X32A_ROM`, `DOMINOS_PRP`, `DOMINOS_ZIP` (or `PINHECK_UPDATE_DIR` to build the zip) and skip with a message when absent. All test output lives in git-ignored `build/` directories.
- Counters: NCO pin outputs are cycle-exact. DUTY outputs are deliberately not driven onto pins (spec §5.4; Plan 7 integrates `FRQ` for audio). PLL pin outputs and pin-sensing modes stay "reported once, not modelled" (Plan 6 owns PLL).
- Code comments stay short.

## Review Focus

These inputs occur on the real hardware and are the most likely to break a working-looking implementation. Each is pinned by the named test.

- **Chip-select asserted while SCLK is high.** The firmware clocks the card from an NCO counter, so SCLK's phase at chip-select is arbitrary. The card must not shift its output until it has sampled a bit; otherwise every response arrives one bit early (CMD0 answers `FE 03` instead of `FF 01`). Test `clock_high_at_select` in `sd_test.c` (Task 3).
- **A counter write or `COGSTOP` lands while an old-state toggle is still due.** An ordinary instruction's counter write takes effect at `m3+1`, and cycle `m3` still belongs to the old state. That toggle must still appear. Test `chip/nco.spin` against the RTL (Task 4).
- **A cog waits in `WAITPEQ`/`WAITPNE` on a counter-driven pin while nothing else in the chip runs.** The firmware's SD driver does exactly this. The wait must wake on the counter edge, not at the next unrelated event. `chip/nco.spin`'s worker waits three times on its own NCO pin while cog 0 idles in a 20,000-cycle `WAITCNT` (Task 4).
- **A release zip whose entries exceed the cache, and empty folders that exist only as zip directory entries.** Music tracks reach 41 MB; `_DQ`, `_DX`, `_DY`, `_FC`, `_FQ`, `_FX` and `_FY` are empty. `zipsrc_test.c` covers an entry larger than the cache and directory entries (Task 1); `vfat_test.c` checks that `DMD/_DQ/` becomes a directory (Task 2).
- **The firmware's fixed cluster size.** It reads the boot sector's reserved-sector and FAT-size fields but assumes 64 sectors per cluster. With any other cluster size it reads the wrong sectors (it streams the middle of `AB0.VID`). `vfat_test.c` checks byte 13 = 64 (Task 2), and the SD boot in Task 5 fails otherwise.

## Findings this plan is built on

Measured by booting `PRP_V008.BIN` in the prototype; recorded in the spec by Task 6:

- The firmware programs seven counters at boot: NCO single on P1 (SD clock, `FRQA` = `$0100_0000`, about 406 kHz for card init), P2 (SD data, `FRQ` = 0, data shifted through `PHSB`), P21/P22 (display, `FRQ` = 0) and P25 (serial TX), and DUTY single on P14/P15 (audio). Nothing else.
- Its SD driver negotiates SDHC (CMD0, CMD8 `0x1AA`, CMD55/ACMD41 with HCS, CMD58, CMD9), then uses CMD17 single-block reads.
- Its FAT code reads the MBR, the boot sector and directories by name, and assumes 64 sectors per cluster.
- With no PIC32 attached, after mounting it reads root, `SFX`, `DMD`, `DMD/_DZ` and the first frame of `DMD/_DZ/ZMB.VID`. It then sends an STK500v2 `CMD_SIGN_ON` (`1B 00 00 01 0E 01 15`) at 115200 baud on P25 (receiving on P24) and waits for the PIC32's bootloader to answer. That handshake belongs to Plan 5; this plan's exit test covers the boot up to it.

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/pinheck/zipsrc.[ch]` | romset zip reader behind a `vfat_source` |
| `src/wpc/pinheck/vfat.[ch]` | read-only FAT32 card image over a `vfat_source`; owns the `vfat_source` type |
| `src/wpc/pinheck/sd.[ch]` | SD card in SPI mode over an `sd_blockdev` |
| `src/cpu/p8x32a/p8x32a.[ch]` | adds cycle-exact NCO counter pin outputs |
| `tests/pinheck/storage/*` | zipsrc, vfat and sd tests; `fsck.vfat` and `mcopy` checks |
| `tests/pinheck/p8x32a/chip/nco.spin` | RTL-compared counter-output test |
| `tests/pinheck/p8x32a/run.c`, `rtl/tb.cpp`, `tools.sh` | runner and RTL testbench attach the SD card |
| `tests/pinheck/p8x32a/check.sh`, `sdcheck.py`, `nodac.py` | SD boot exit test |

---
### Task 1: Romset zip reader

**Files:**
- Create: `src/wpc/pinheck/vfat.h`, `src/wpc/pinheck/zipsrc.h`, `src/wpc/pinheck/zipsrc.c`
- Create: `tests/pinheck/storage/check.sh`, `.gitignore`, `mkzip.py`, `zipsrc_test.c`

**Interfaces:**
- Consumes: nothing.
- Produces: the `vfat_source` type (in `vfat.h`, which also declares the whole vfat API implemented in Task 2) and `zipsrc_open`/`zipsrc_close`/`zipsrc_source`. `zipsrc` exposes every zip entry except empty names; directory entries keep their trailing `/` and have size 0. `read` returns -1 for a range past the entry, an unsupported method (only 0 and 8), or an inflate error.

`check.sh` builds each test only when its test file exists, but once a test exists its implementation must build. That is what makes each task's first run fail.

- [ ] **Step 1: Write the test, the fixture generator, the headers and the driver**

`mkzip.py` writes the fixture with Python's own `zipfile`, so the reader is checked against an independent implementation.

`tests/pinheck/storage/mkzip.py`:

```python
#!/usr/bin/env python3
import sys
import zipfile

d = bytes((i * 7 + 3) & 0xFF for i in range(70000))
z = zipfile.ZipFile(sys.argv[1], 'w')
z.writestr(zipfile.ZipInfo('DOM_V006.PRG'), b'firmware')
z.writestr('DMD/', b'')
z.writestr('DMD/_DA/', b'')
z.writestr('DMD/_DQ/', b'')
z.writestr(zipfile.ZipInfo('DMD/_DA/AA0.VID'), d[:40000], compress_type=zipfile.ZIP_DEFLATED)
z.writestr(zipfile.ZipInfo('DMD/_DA/AB0.VID'), d[:512], compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo('DMD/_DZ/ZMS.spr'), d[:1000], compress_type=zipfile.ZIP_DEFLATED)
z.writestr(zipfile.ZipInfo('DMD/_DZ/EMPTY.FNT'), b'', compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo('SFX/_FA/A01.wav'), d[:70000], compress_type=zipfile.ZIP_DEFLATED)
z.writestr(zipfile.ZipInfo('SFX/_FA/TOOLONGNAME.wav'), d[:10], compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo('SFX/_FA/two.dots.wav'), d[:10], compress_type=zipfile.ZIP_STORED)
z.writestr(zipfile.ZipInfo('OTHER/X.BIN'), d[:10], compress_type=zipfile.ZIP_STORED)
z.close()
```

`tests/pinheck/storage/zipsrc_test.c`:

```c
#include "zipsrc.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("ZIPSRC FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t pat(uint32_t i) { return (uint8_t)(i * 7 + 3); }

static int find(zipsrc *z, const char *name)
{
	const vfat_source *s = zipsrc_source(z);
	int k;
	for (k = 0; k < s->count; k++)
		if (!strcmp(s->name(s->ctx, k), name)) return k;
	return -1;
}

static int same(const uint8_t *b, uint32_t off, uint32_t n)
{
	uint32_t k;
	for (k = 0; k < n; k++)
		if (b[k] != pat(off + k)) return 0;
	return 1;
}

int main(int argc, char **argv)
{
	zipsrc z;
	const vfat_source *s;
	uint8_t b[512];
	int aa0, ab0, zms, a01, dq;

	CHECK(zipsrc_open(&z, "does-not-exist.zip", 50000) != 0);
	if (argc != 2 || zipsrc_open(&z, argv[1], 50000)) { printf("zipsrc: cannot open test zip\n"); return 1; }
	s = zipsrc_source(&z);
	CHECK(s->count == 12);
	aa0 = find(&z, "DMD/_DA/AA0.VID");
	ab0 = find(&z, "DMD/_DA/AB0.VID");
	zms = find(&z, "DMD/_DZ/ZMS.spr");
	a01 = find(&z, "SFX/_FA/A01.wav");
	dq = find(&z, "DMD/_DQ/");
	CHECK(aa0 >= 0 && ab0 >= 0 && zms >= 0 && a01 >= 0 && dq >= 0);
	CHECK(s->size(s->ctx, dq) == 0);
	CHECK(s->size(s->ctx, aa0) == 40000);
	CHECK(s->read(s->ctx, aa0, 39000, b, 512) == 0 && same(b, 39000, 512));
	CHECK(s->read(s->ctx, ab0, 0, b, 512) == 0 && same(b, 0, 512));
	CHECK(s->read(s->ctx, zms, 500, b, 500) == 0 && same(b, 500, 500));
	CHECK(z.cache_used == 41000);
	CHECK(s->read(s->ctx, a01, 69990, b, 10) == 0 && same(b, 69990, 10));
	CHECK(z.cache_used == 70000 && !z.e[aa0].data && !z.e[zms].data);
	CHECK(s->read(s->ctx, zms, 0, b, 512) == 0 && same(b, 0, 512));
	CHECK(z.cache_used == 1000 && !z.e[a01].data);
	CHECK(s->read(s->ctx, aa0, 39900, b, 101) != 0);
	CHECK(s->read(s->ctx, aa0, 0, b, 512) == 0 && same(b, 0, 512));
	zipsrc_close(&z);
	printf("zipsrc: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

`src/wpc/pinheck/vfat.h`:

```c
#ifndef PINHECK_VFAT_H
#define PINHECK_VFAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vfat_source {
	void *ctx;
	int count;
	const char *(*name)(void *ctx, int i);
	uint32_t (*size)(void *ctx, int i);
	int (*read)(void *ctx, int i, uint32_t off, uint8_t *buf, uint32_t len);
} vfat_source;

typedef struct vfat_node {
	char name[11];
	int parent, src, is_dir;
	uint32_t size, first, nclus;
	uint8_t *dir;
} vfat_node;

typedef struct vfat {
	const vfat_source *src;
	vfat_node *node;
	int *alloc;
	int nnodes, nalloc, skipped;
	uint32_t part_start, vol_sectors, fat_sectors, data_start, clusters, used;
} vfat;

#define VFAT_PART_START 8192u
#define VFAT_SPC 64u

int vfat_init(vfat *v, const vfat_source *src);
void vfat_free(vfat *v);
uint32_t vfat_sectors(const vfat *v);
int vfat_read(vfat *v, uint32_t lba, uint8_t *buf512);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/zipsrc.h`:

```c
#ifndef PINHECK_ZIPSRC_H
#define PINHECK_ZIPSRC_H

#include <stdio.h>
#include <stdint.h>
#include "vfat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zipsrc_entry {
	char *name;
	uint32_t csize, usize, hdr_off, data_off;
	uint16_t method;
	uint8_t *data;
	uint32_t stamp;
} zipsrc_entry;

typedef struct zipsrc {
	FILE *f;
	int count;
	zipsrc_entry *e;
	uint32_t cache_bytes, cache_used, clock;
	vfat_source src;
} zipsrc;

int zipsrc_open(zipsrc *z, const char *zip_path, uint32_t cache_bytes);
void zipsrc_close(zipsrc *z);
const vfat_source *zipsrc_source(zipsrc *z);

#ifdef __cplusplus
}
#endif

#endif
```

`tests/pinheck/storage/check.sh`:

```sh
#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src/wpc/pinheck
CC="cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I$S"
fail=0
mkdir -p $B
for c in $S/sd.c $S/vfat.c $S/zipsrc.c; do
	[ -e "$c" ] || continue
	cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only -I$S "$c" || { echo "C89 FAIL $c"; fail=$((fail + 1)); }
done
python3 mkzip.py $B/test.zip || exit 2
if [ -f zipsrc_test.c ]; then
	$CC -o $B/zipsrc_test zipsrc_test.c $S/zipsrc.c -lz || exit 2
	./$B/zipsrc_test $B/test.zip || fail=$((fail + 1))
fi
if [ -f vfat_test.c ]; then
	$CC -o $B/vfat_test vfat_test.c $S/vfat.c || exit 2
	./$B/vfat_test || fail=$((fail + 1))
	$CC -o $B/vfatimg vfatimg.c $S/vfat.c $S/zipsrc.c -lz || exit 2
	for z in $B/test.zip ${DOMINOS_ZIP:+"$DOMINOS_ZIP"}; do
		./$B/vfatimg "$z" volume $B/vol.img > /dev/null || { fail=$((fail + 1)); continue; }
		if fsck.vfat -n $B/vol.img > $B/fsck.log 2>&1; then echo "fsck: clean ($z)"; else echo "FSCK FAIL $z"; cat $B/fsck.log; fail=$((fail + 1)); fi
		rm -rf $B/extract && mkdir $B/extract
		MTOOLS_SKIP_CHECK=1 mcopy -s -n -i $B/vol.img ::/DMD ::/SFX $B/extract/ 2> /dev/null
		python3 cmpzip.py "$z" $B/extract || fail=$((fail + 1))
	done
fi
if [ -f sd_test.c ]; then
	$CC -o $B/sd_test sd_test.c $S/sd.c || exit 2
	./$B/sd_test || fail=$((fail + 1))
fi
echo "storage: $fail failed"
[ $fail -eq 0 ]
```

Make them executable: `chmod +x tests/pinheck/storage/check.sh tests/pinheck/storage/mkzip.py`

`tests/pinheck/storage/.gitignore`:

```text
build/
```

- [ ] **Step 2: Run to verify it fails**

Run: `tests/pinheck/storage/check.sh`
Expected: exit 2 with `zipsrc.c: No such file or directory`.

- [ ] **Step 3: Write the reader**

`src/wpc/pinheck/zipsrc.c`:

```c
#include "zipsrc.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static int read_at(FILE *f, uint32_t off, void *buf, uint32_t len)
{
	if (fseek(f, (long)off, SEEK_SET)) return -1;
	return fread(buf, 1, len, f) == len ? 0 : -1;
}

static const char *zs_name(void *ctx, int i) { return ((zipsrc *)ctx)->e[i].name; }
static uint32_t zs_size(void *ctx, int i) { return ((zipsrc *)ctx)->e[i].usize; }

static int data_offset(zipsrc *z, zipsrc_entry *e)
{
	uint8_t h[30];
	if (e->data_off) return 0;
	if (read_at(z->f, e->hdr_off, h, 30) || le32(h) != 0x04034B50u) return -1;
	e->data_off = e->hdr_off + 30 + le16(h + 26) + le16(h + 28);
	return 0;
}

static void evict(zipsrc *z, uint32_t need, int keep)
{
	while (z->cache_used && z->cache_used + need > z->cache_bytes) {
		int k, old = -1;
		for (k = 0; k < z->count; k++)
			if (k != keep && z->e[k].data && (old < 0 || z->e[k].stamp < z->e[old].stamp)) old = k;
		if (old < 0) return;
		free(z->e[old].data);
		z->e[old].data = NULL;
		z->cache_used -= z->e[old].usize;
	}
}

static int inflate_entry(zipsrc *z, int i)
{
	zipsrc_entry *e = &z->e[i];
	z_stream s;
	uint8_t in[16384];
	uint32_t left = e->csize;
	int r = Z_OK;

	evict(z, e->usize, i);
	e->data = (uint8_t *)malloc(e->usize ? e->usize : 1);
	if (!e->data) return -1;
	memset(&s, 0, sizeof(s));
	if (inflateInit2(&s, -MAX_WBITS) != Z_OK) { free(e->data); e->data = NULL; return -1; }
	s.next_out = e->data;
	s.avail_out = e->usize;
	if (fseek(z->f, (long)e->data_off, SEEK_SET)) r = Z_ERRNO;
	while (r == Z_OK && left) {
		uint32_t n = left < sizeof(in) ? left : (uint32_t)sizeof(in);
		if (fread(in, 1, n, z->f) != n) { r = Z_ERRNO; break; }
		left -= n;
		s.next_in = in;
		s.avail_in = n;
		r = inflate(&s, Z_NO_FLUSH);
	}
	inflateEnd(&s);
	if (r != Z_STREAM_END || s.total_out != e->usize) { free(e->data); e->data = NULL; return -1; }
	z->cache_used += e->usize;
	return 0;
}

static int zs_read(void *ctx, int i, uint32_t off, uint8_t *buf, uint32_t len)
{
	zipsrc *z = (zipsrc *)ctx;
	zipsrc_entry *e;

	if (i < 0 || i >= z->count) return -1;
	e = &z->e[i];
	if (off > e->usize || len > e->usize - off) return -1;
	if (data_offset(z, e)) return -1;
	if (e->method == 0) return read_at(z->f, e->data_off + off, buf, len);
	if (e->method != 8) return -1;
	if (!e->data && inflate_entry(z, i)) return -1;
	e->stamp = ++z->clock;
	memcpy(buf, e->data + off, len);
	return 0;
}

int zipsrc_open(zipsrc *z, const char *zip_path, uint32_t cache_bytes)
{
	uint8_t tail[65557], *p, *cd = NULL;
	long size;
	uint32_t n, cd_size, cd_off, i, pos;
	int k;

	memset(z, 0, sizeof(*z));
	z->cache_bytes = cache_bytes;
	z->f = fopen(zip_path, "rb");
	if (!z->f) return -1;
	if (fseek(z->f, 0, SEEK_END) || (size = ftell(z->f)) < 22) goto fail;
	n = size < (long)sizeof(tail) ? (uint32_t)size : (uint32_t)sizeof(tail);
	if (read_at(z->f, (uint32_t)size - n, tail, n)) goto fail;
	for (p = tail + n - 22; p >= tail && le32(p) != 0x06054B50u; p--) ;
	if (p < tail) goto fail;
	z->count = (int)le16(p + 10);
	cd_size = le32(p + 12);
	cd_off = le32(p + 16);
	if (cd_off == 0xFFFFFFFFu || le16(p + 10) == 0xFFFF) goto fail;
	cd = (uint8_t *)malloc(cd_size ? cd_size : 1);
	z->e = (zipsrc_entry *)calloc((size_t)z->count + 1, sizeof(zipsrc_entry));
	if (!cd || !z->e || read_at(z->f, cd_off, cd, cd_size)) goto fail;
	for (i = 0, pos = 0, k = 0; i < (uint32_t)z->count; i++) {
		uint8_t *h = cd + pos;
		uint32_t nl, el, cl;
		char *name;
		if (pos + 46 > cd_size || le32(h) != 0x02014B50u) goto fail;
		nl = le16(h + 28); el = le16(h + 30); cl = le16(h + 32);
		if (pos + 46 + nl > cd_size) goto fail;
		name = (char *)malloc(nl + 1);
		if (!name) goto fail;
		memcpy(name, h + 46, nl);
		name[nl] = 0;
		pos += 46 + nl + el + cl;
		if (!nl) { free(name); continue; }
		{
			char *c;
			for (c = name; *c; c++) if (*c == '\\') *c = '/';
		}
		z->e[k].name = name;
		z->e[k].method = (uint16_t)le16(h + 10);
		z->e[k].csize = le32(h + 20);
		z->e[k].usize = le32(h + 24);
		z->e[k].hdr_off = le32(h + 42);
		k++;
	}
	z->count = k;
	free(cd);
	z->src.ctx = z;
	z->src.count = z->count;
	z->src.name = zs_name;
	z->src.size = zs_size;
	z->src.read = zs_read;
	return 0;
fail:
	free(cd);
	zipsrc_close(z);
	return -1;
}

void zipsrc_close(zipsrc *z)
{
	int k;
	if (z->e) {
		for (k = 0; k < z->count; k++) { free(z->e[k].name); free(z->e[k].data); }
		free(z->e);
	}
	if (z->f) fclose(z->f);
	memset(z, 0, sizeof(*z));
}

const vfat_source *zipsrc_source(zipsrc *z) { return &z->src; }
```

- [ ] **Step 4: Run to verify it passes**

Run: `tests/pinheck/storage/check.sh`
Expected: `zipsrc: ok` and `storage: 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck/vfat.h src/wpc/pinheck/zipsrc.h src/wpc/pinheck/zipsrc.c tests/pinheck/storage
git commit -m "pinheck: romset zip reader with an LRU inflate cache"
```

### Task 2: Virtual FAT32 volume

**Files:**
- Create: `src/wpc/pinheck/vfat.c`
- Create: `tests/pinheck/storage/vfat_test.c`, `vfatimg.c`, `cmpzip.py`

**Interfaces:**
- Consumes: `vfat.h` and `zipsrc` from Task 1.
- Produces: the vfat implementation.
  - Layout: partition at LBA 8192 (`VFAT_PART_START`), 64 sectors per cluster (`VFAT_SPC`), 32 reserved sectors (boot sector at 0 and 6, FSInfo at 1 and 7), two FATs, root at cluster 2, at least 65,536 clusters.
  - Allocation order: root, then directories in path order, then files in path order.
  - Directory entries date 2016-07-01 12:00, and the root carries the volume label `DOMINOS`.
  - Names that are not valid 8.3, or that collide after uppercasing, are skipped and counted in `vfat.skipped`.

- [ ] **Step 1: Write the tests**

`vfat_test.c` checks the structures directly through an in-memory source. `vfatimg.c` writes the volume so that `fsck.vfat -n` and `mcopy`, independent FAT implementations, can judge it, and `cmpzip.py` compares what `mcopy` extracts with the zip.

`tests/pinheck/storage/vfat_test.c`:

```c
#include "vfat.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("VFAT FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const struct { const char *name; uint32_t size; } files[] = {
	{ "DOM_V006.PRG", 8 }, { "DMD/_DA/AA0.VID", 40000 }, { "DMD/_DA/aa1.vid", 512 }, { "DMD/_DZ/ZMS.spr", 1000 },
	{ "DMD/_DZ/EMPTY.FNT", 0 }, { "DMD/_DQ/", 0 }, { "SFX/_FA/A01.wav", 70000 },
	{ "SFX/_FA/TOOLONGNAME.wav", 10 }, { "SFX/_FA/two.dots.wav", 10 }, { "OTHER/X.BIN", 10 }
};

static const char *src_name(void *ctx, int i) { (void)ctx; return files[i].name; }
static uint32_t src_size(void *ctx, int i) { (void)ctx; return files[i].size; }
static int src_read(void *ctx, int i, uint32_t off, uint8_t *buf, uint32_t len)
{
	uint32_t k;
	(void)ctx;
	if (off + len > files[i].size) return -1;
	for (k = 0; k < len; k++) buf[k] = (uint8_t)(i * 31 + off + k);
	return 0;
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static vfat v;

static uint32_t cl_lba(uint32_t cl) { return v.part_start + v.data_start + (cl - 2) * VFAT_SPC; }

static const uint8_t *entry(const uint8_t *dir, const char *name)
{
	int k;
	for (k = 0; k < 512 / 32; k++)
		if (!memcmp(dir + 32 * k, name, 11)) return dir + 32 * k;
	return NULL;
}

static uint32_t fat_entry(uint32_t cl)
{
	uint8_t b[512];
	vfat_read(&v, v.part_start + 32 + cl / 128, b);
	return le32(b + 4 * (cl % 128));
}

int main(void)
{
	vfat_source src;
	uint8_t b[512], root[512], dmd[512], dz[512], fa[512];
	const uint8_t *e;
	uint32_t cl;
	int k, ok;

	src.ctx = NULL;
	src.count = (int)(sizeof(files) / sizeof(files[0]));
	src.name = src_name;
	src.size = src_size;
	src.read = src_read;
	CHECK(vfat_init(&v, &src) == 0);
	CHECK(v.skipped == 2);
	CHECK(v.clusters >= 65525);

	CHECK(vfat_read(&v, 0, b) == 0);
	CHECK(b[446 + 4] == 0x0C && le32(b + 446 + 8) == 8192 && le32(b + 446 + 12) == v.vol_sectors && b[510] == 0x55 && b[511] == 0xAA);
	CHECK(vfat_read(&v, 8192, b) == 0);
	CHECK(le16(b + 11) == 512 && b[13] == 64 && le32(b + 32) == v.vol_sectors && le32(b + 36) == v.fat_sectors && le32(b + 44) == 2);
	CHECK(!memcmp(b + 82, "FAT32   ", 8) && b[510] == 0x55);
	CHECK(vfat_read(&v, 8193, b) == 0);
	CHECK(le32(b) == 0x41615252u && le32(b + 484) == 0x61417272u && le32(b + 488) == v.clusters - v.used);

	CHECK(vfat_read(&v, cl_lba(2), root) == 0);
	CHECK(root[11] == 0x08);
	CHECK(entry(root, "DMD        ") && entry(root, "SFX        "));
	CHECK(!entry(root, "DOM_V006PRG") && !entry(root, "OTHER      "));
	e = entry(root, "DMD        ");
	CHECK(e && e[11] == 0x10);
	cl = le16(e + 26) | le16(e + 20) << 16;
	CHECK(vfat_read(&v, cl_lba(cl), dmd) == 0);
	CHECK(!memcmp(dmd, ".          ", 11) && !memcmp(dmd + 32, "..         ", 11) && le16(dmd + 32 + 26) == 0);
	CHECK(!memcmp(dmd + 64, "_DA        ", 11) && !memcmp(dmd + 96, "_DQ        ", 11) && !memcmp(dmd + 128, "_DZ        ", 11));

	e = entry(dmd, "_DZ        ");
	CHECK(e != NULL);
	CHECK(vfat_read(&v, cl_lba(le16(e + 26)), dz) == 0);
	e = entry(dz, "ZMS     SPR");
	CHECK(e && e[11] == 0x20 && le32(e + 28) == 1000);
	cl = e ? le16(e + 26) : 2;
	CHECK(vfat_read(&v, cl_lba(cl) + 1, b) == 0);
	for (k = 0, ok = 1; k < 512; k++) ok &= b[k] == (k < 488 ? (uint8_t)(3 * 31 + 512 + k) : 0);
	CHECK(ok);
	e = entry(dz, "EMPTY   FNT");
	CHECK(e && le16(e + 26) == 0 && le32(e + 28) == 0);

	e = entry(root, "SFX        ");
	CHECK(e && vfat_read(&v, cl_lba(le16(e + 26)), b) == 0);
	e = entry(b, "_FA        ");
	CHECK(e && vfat_read(&v, cl_lba(le16(e + 26)), fa) == 0);
	CHECK(!entry(fa, "TOOLONGNWAV"));
	e = entry(fa, "A01     WAV");
	CHECK(e && le32(e + 28) == 70000);
	cl = e ? le16(e + 26) : 2;
	CHECK(fat_entry(cl) == cl + 1 && fat_entry(cl + 1) == cl + 2 && fat_entry(cl + 2) == 0x0FFFFFFFu);
	CHECK(fat_entry(0) == 0x0FFFFFF8u && fat_entry(1) == 0x0FFFFFFFu && fat_entry(2 + v.used) == 0);

	CHECK(vfat_read(&v, vfat_sectors(&v), b) != 0);
	vfat_free(&v);
	printf("vfat: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

`tests/pinheck/storage/vfatimg.c`:

```c
#include "zipsrc.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	zipsrc z;
	vfat v;
	uint8_t b[512];
	uint32_t lba, end;
	FILE *f;

	if (argc != 4) { fprintf(stderr, "usage: vfatimg romset.zip card|volume out.img\n"); return 2; }
	if (zipsrc_open(&z, argv[1], 64u << 20)) { fprintf(stderr, "vfatimg: cannot open %s\n", argv[1]); return 2; }
	if (vfat_init(&v, zipsrc_source(&z))) { fprintf(stderr, "vfatimg: vfat_init failed\n"); return 2; }
	f = fopen(argv[3], "wb");
	if (!f) { perror(argv[3]); return 2; }
	lba = argv[2][0] == 'v' ? v.part_start : 0;
	end = vfat_sectors(&v);
	for (; lba < end; lba++) {
		if (vfat_read(&v, lba, b)) { fprintf(stderr, "vfatimg: read error at lba %u\n", (unsigned)lba); return 1; }
		if (fwrite(b, 1, 512, f) != 512) { perror(argv[3]); return 1; }
	}
	fclose(f);
	printf("vfatimg: %d nodes, %d skipped, %u clusters used of %u, %u card sectors\n",
	       v.nnodes, v.skipped, (unsigned)v.used, (unsigned)v.clusters, (unsigned)vfat_sectors(&v));
	vfat_free(&v);
	zipsrc_close(&z);
	return 0;
}
```

`tests/pinheck/storage/cmpzip.py`:

```python
#!/usr/bin/env python3
import os
import re
import sys
import zipfile

z = zipfile.ZipFile(sys.argv[1])
ext = sys.argv[2]
ok83 = re.compile(r'^[A-Z0-9$%\'\-_@~`!(){}^#&]{1,8}(\.[A-Z0-9$%\'\-_@~`!(){}^#&]{1,3})?$')
want = {}
for n in z.namelist():
    parts = n.upper().rstrip('/').split('/')
    if parts[0] not in ('DMD', 'SFX') or n.endswith('/') or not all(ok83.match(p) for p in parts):
        continue
    want['/'.join(parts)] = n
got = {}
for dp, dn, fn in os.walk(ext):
    for f in fn:
        p = os.path.join(dp, f)
        got[os.path.relpath(p, ext).upper()] = p
bad = [k for k in want if k not in got or open(got[k], 'rb').read() != z.read(want[k])]
extra = [k for k in got if k not in want]
if bad or extra:
    print('mount: FAIL, %d missing or different, %d extra: %s' % (len(bad), len(extra), (bad + extra)[:5]))
    sys.exit(1)
print('mount: %d files byte-exact against %s' % (len(want), os.path.basename(sys.argv[1])))
```

Make it executable: `chmod +x tests/pinheck/storage/cmpzip.py`

- [ ] **Step 2: Run to verify it fails**

Run: `tests/pinheck/storage/check.sh`
Expected: exit 2 with `vfat.c: No such file or directory`.

- [ ] **Step 3: Write the volume**

`src/wpc/pinheck/vfat.c`:

```c
#include "vfat.h"
#include <stdlib.h>
#include <string.h>

#define RESERVED 32u
#define CLUSTER_BYTES (VFAT_SPC * 512u)
#define MIN_CLUSTERS 65536u
#define FTIME 0x6000u
#define FDATE 0x48E1u

typedef struct rec { char key[64]; char part[8][11]; int nparts, src, is_dir; } rec;

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

static int valid83(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr("$%'-_@~`!(){}^#&", c));
}

static int to83(const char *s, size_t n, char *out)
{
	size_t i, dot = n, b = 0, e = 0;
	memset(out, ' ', 11);
	for (i = 0; i < n; i++)
		if (s[i] == '.') { if (dot != n) return -1; dot = i; }
	if (dot == 0 || dot > 8 || (dot < n && (n - dot - 1 < 1 || n - dot - 1 > 3))) return -1;
	for (i = 0; i < n; i++) {
		char c = s[i];
		if (i == dot) continue;
		if (c >= 'a' && c <= 'z') c = (char)(c - 32);
		if (!valid83(c)) return -1;
		if (i < dot) out[b++] = c; else out[8 + e++] = c;
	}
	return 0;
}

static int split(const char *path, rec *r)
{
	const char *p = path, *q;
	int k = 0;
	size_t kl = 0;
	if (!((path[0] == 'D' || path[0] == 'd') && (path[1] == 'M' || path[1] == 'm') && (path[2] == 'D' || path[2] == 'd') && path[3] == '/') &&
	    !((path[0] == 'S' || path[0] == 's') && (path[1] == 'F' || path[1] == 'f') && (path[2] == 'X' || path[2] == 'x') && path[3] == '/'))
		return 1;
	r->is_dir = 0;
	for (;;) {
		q = strchr(p, '/');
		if (!q) q = p + strlen(p);
		if (*q == '/' && !q[1]) r->is_dir = 1;
		if (k == 8 || to83(p, (size_t)(q - p), r->part[k])) return -1;
		if (kl + 12 >= sizeof(r->key)) return -1;
		memcpy(r->key + kl, r->part[k], 11);
		r->key[kl + 11] = '/';
		kl += 12;
		k++;
		if (!*q || !q[1]) break;
		p = q + 1;
	}
	r->key[kl] = 0;
	r->nparts = k;
	return 0;
}

static int keycmp(const void *a, const void *b) { return strcmp(((const rec *)a)->key, ((const rec *)b)->key); }

static int find_child(const vfat *v, int parent, const char *name, int is_dir)
{
	int k;
	for (k = 1; k < v->nnodes; k++)
		if (v->node[k].parent == parent && v->node[k].is_dir == is_dir && !memcmp(v->node[k].name, name, 11)) return k;
	return -1;
}

static int add_node(vfat *v, int parent, const char *name, int is_dir, int src, uint32_t size)
{
	vfat_node *n = &v->node[v->nnodes];
	memset(n, 0, sizeof(*n));
	memcpy(n->name, name, 11);
	n->parent = parent;
	n->is_dir = is_dir;
	n->src = src;
	n->size = size;
	return v->nnodes++;
}

static void dirent(uint8_t *d, const char *name, uint8_t attr, uint32_t cl, uint32_t size)
{
	memcpy(d, name, 11);
	d[11] = attr;
	put16(d + 14, FTIME); put16(d + 16, FDATE); put16(d + 18, FDATE);
	put16(d + 20, cl >> 16); put16(d + 22, FTIME); put16(d + 24, FDATE);
	put16(d + 26, cl & 0xFFFF); put32(d + 28, size);
}

int vfat_init(vfat *v, const vfat_source *src)
{
	rec *r = NULL;
	int i, k, nr = 0, ndirs;
	uint32_t cl;

	memset(v, 0, sizeof(*v));
	v->src = src;
	v->part_start = VFAT_PART_START;
	r = (rec *)calloc((size_t)src->count + 1, sizeof(rec));
	v->node = (vfat_node *)calloc((size_t)src->count * 8 + 1, sizeof(vfat_node));
	v->alloc = (int *)calloc((size_t)src->count * 8 + 1, sizeof(int));
	if (!r || !v->node || !v->alloc) goto fail;
	for (i = 0; i < src->count; i++) {
		int s = split(src->name(src->ctx, i), &r[nr]);
		if (s < 0) v->skipped++;
		if (s) continue;
		r[nr++].src = i;
	}
	qsort(r, (size_t)nr, sizeof(rec), keycmp);
	add_node(v, -1, "DOMINOS    ", 1, -1, 0);
	for (i = 0; i < nr; i++) {
		int parent = 0, depth = r[i].nparts - (r[i].is_dir ? 0 : 1);
		for (k = 0; k < depth; k++) {
			int c = find_child(v, parent, r[i].part[k], 1);
			parent = c >= 0 ? c : add_node(v, parent, r[i].part[k], 1, -1, 0);
		}
	}
	ndirs = v->nnodes;
	for (i = 0; i < nr; i++) {
		int parent = 0;
		if (r[i].is_dir) continue;
		for (k = 0; k < r[i].nparts - 1; k++) parent = find_child(v, parent, r[i].part[k], 1);
		if (find_child(v, parent, r[i].part[r[i].nparts - 1], 0) >= 0 || find_child(v, parent, r[i].part[r[i].nparts - 1], 1) >= 0) { v->skipped++; continue; }
		add_node(v, parent, r[i].part[r[i].nparts - 1], 0, r[i].src, src->size(src->ctx, r[i].src));
	}
	free(r);
	r = NULL;
	for (k = 0; k < ndirs; k++) {
		uint32_t n = k ? 2 : 1;
		for (i = 1; i < v->nnodes; i++) if (v->node[i].parent == k) n++;
		v->node[k].size = n * 32;
	}
	for (k = 0, cl = 2; k < v->nnodes; k++) {
		vfat_node *n = &v->node[k];
		n->nclus = (n->size + CLUSTER_BYTES - 1) / CLUSTER_BYTES;
		if (!n->nclus) continue;
		n->first = cl;
		cl += n->nclus;
		v->alloc[v->nalloc++] = k;
	}
	v->used = cl - 2;
	v->clusters = v->used < MIN_CLUSTERS ? MIN_CLUSTERS : v->used;
	v->fat_sectors = ((v->clusters + 2) * 4 + 511) / 512;
	v->data_start = RESERVED + 2 * v->fat_sectors;
	v->vol_sectors = v->data_start + v->clusters * VFAT_SPC;
	for (k = 0; k < ndirs; k++) {
		vfat_node *n = &v->node[k];
		uint8_t *d;
		n->dir = (uint8_t *)calloc(n->nclus, CLUSTER_BYTES);
		if (!n->dir) goto fail;
		d = n->dir;
		if (k) {
			dirent(d, ".          ", 0x10, n->first, 0);
			dirent(d + 32, "..         ", 0x10, n->parent ? v->node[n->parent].first : 0, 0);
			d += 64;
		} else {
			memcpy(d, n->name, 11);
			d[11] = 0x08;
			put16(d + 22, FTIME); put16(d + 24, FDATE);
			d += 32;
		}
		for (i = 1; i < v->nnodes; i++) {
			vfat_node *c = &v->node[i];
			if (c->parent != k) continue;
			dirent(d, c->name, c->is_dir ? 0x10 : 0x20, c->first, c->is_dir ? 0 : c->size);
			d += 32;
		}
	}
	return 0;
fail:
	free(r);
	vfat_free(v);
	return -1;
}

void vfat_free(vfat *v)
{
	int k;
	if (v->node) for (k = 0; k < v->nnodes; k++) free(v->node[k].dir);
	free(v->node);
	free(v->alloc);
	memset(v, 0, sizeof(*v));
}

uint32_t vfat_sectors(const vfat *v) { return v->part_start + v->vol_sectors; }

static int owner(const vfat *v, uint32_t cl)
{
	int lo = 0, hi = v->nalloc - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		const vfat_node *n = &v->node[v->alloc[mid]];
		if (cl < n->first) hi = mid - 1;
		else if (cl >= n->first + n->nclus) lo = mid + 1;
		else return v->alloc[mid];
	}
	return -1;
}

static void boot_sector(const vfat *v, uint8_t *b)
{
	b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;
	memcpy(b + 3, "MSWIN4.1", 8);
	put16(b + 11, 512); b[13] = (uint8_t)VFAT_SPC; put16(b + 14, RESERVED); b[16] = 2;
	b[21] = 0xF8; put16(b + 24, 63); put16(b + 26, 255);
	put32(b + 28, v->part_start); put32(b + 32, v->vol_sectors); put32(b + 36, v->fat_sectors);
	put32(b + 44, 2); put16(b + 48, 1); put16(b + 50, 6);
	b[64] = 0x80; b[66] = 0x29; put32(b + 67, 0x20160701u);
	memcpy(b + 71, "DOMINOS    ", 11); memcpy(b + 82, "FAT32   ", 8);
	b[510] = 0x55; b[511] = 0xAA;
}

static void fsinfo(const vfat *v, uint8_t *b)
{
	put32(b, 0x41615252u); put32(b + 484, 0x61417272u);
	put32(b + 488, v->clusters - v->used); put32(b + 492, 2 + v->used);
	put32(b + 508, 0xAA550000u);
}

static void fat_sector(const vfat *v, uint32_t s, uint8_t *b)
{
	uint32_t j;
	for (j = 0; j < 128; j++) {
		uint32_t cl = s * 128 + j, e = 0;
		if (cl == 0) e = 0x0FFFFFF8u;
		else if (cl == 1) e = 0x0FFFFFFFu;
		else if (cl < 2 + v->used) {
			int o = owner(v, cl);
			if (o >= 0) e = cl == v->node[o].first + v->node[o].nclus - 1 ? 0x0FFFFFFFu : cl + 1;
		}
		put32(b + 4 * j, e);
	}
}

int vfat_read(vfat *v, uint32_t lba, uint8_t *buf512)
{
	uint32_t r;
	memset(buf512, 0, 512);
	if (lba >= vfat_sectors(v)) return -1;
	if (lba < v->part_start) {
		if (lba == 0) {
			buf512[446 + 1] = 0xFE; buf512[446 + 2] = 0xFF; buf512[446 + 3] = 0xFF;
			buf512[446 + 4] = 0x0C;
			buf512[446 + 5] = 0xFE; buf512[446 + 6] = 0xFF; buf512[446 + 7] = 0xFF;
			put32(buf512 + 446 + 8, v->part_start);
			put32(buf512 + 446 + 12, v->vol_sectors);
			put32(buf512 + 440, 0x20160701u);
			buf512[510] = 0x55; buf512[511] = 0xAA;
		}
		return 0;
	}
	r = lba - v->part_start;
	if (r == 0 || r == 6) { boot_sector(v, buf512); return 0; }
	if (r == 1 || r == 7) { fsinfo(v, buf512); return 0; }
	if (r < RESERVED) return 0;
	if (r < v->data_start) { fat_sector(v, (r - RESERVED) % v->fat_sectors, buf512); return 0; }
	r -= v->data_start;
	{
		uint32_t cl = 2 + r / VFAT_SPC, off;
		int o;
		vfat_node *n;
		if (cl >= 2 + v->used || (o = owner(v, cl)) < 0) return 0;
		n = &v->node[o];
		off = ((cl - n->first) * VFAT_SPC + r % VFAT_SPC) * 512;
		if (n->is_dir) { memcpy(buf512, n->dir + off, 512); return 0; }
		if (off >= n->size) return 0;
		return v->src->read(v->src->ctx, n->src, off, buf512, n->size - off < 512 ? n->size - off : 512);
	}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `sudo apt-get install -y dosfstools mtools` (if `fsck.vfat` or `mcopy` is missing), then `tests/pinheck/storage/check.sh`
Expected:
```text
zipsrc: ok
vfat: ok
fsck: clean (build/test.zip)
mount: 5 files byte-exact against test.zip
storage: 0 failed
```

- [ ] **Step 5: Check the real romset**

Build a romset zip from the update folder, then run the suite with it:
```sh
export DOMINOS_ZIP=$PWD/tests/pinheck/storage/build/dominos.zip
(cd "$PINHECK_UPDATE_DIR" && rm -f "$DOMINOS_ZIP" && zip -q -r -X "$DOMINOS_ZIP" DOM_V006.PRG PRP_V008.BIN DMD SFX)
tests/pinheck/storage/check.sh
```
Expected, in addition to Step 4's lines:
```text
fsck: clean (/…/tests/pinheck/storage/build/dominos.zip)
mount: 1000 files byte-exact against dominos.zip
```

- [ ] **Step 6: Commit**

```bash
git add src/wpc/pinheck/vfat.c tests/pinheck/storage/vfat_test.c tests/pinheck/storage/vfatimg.c tests/pinheck/storage/cmpzip.py
git commit -m "pinheck: read-only FAT32 volume synthesised from the romset zip"
```

### Task 3: SD card (SPI mode)

**Files:**
- Create: `src/wpc/pinheck/sd.h`, `src/wpc/pinheck/sd.c`
- Create: `tests/pinheck/storage/sd_test.c`, `README.md`

**Interfaces:**
- Consumes: nothing (any `sd_blockdev`).
- Produces: `sd_init`/`sd_update`.
  - **Sampling:** the card samples MOSI on SCLK's rising edge. It shifts DO on a falling edge only after a rising edge since chip-select or the previous shift. DO idles at 1, and reads 1 while chip-select is high.
  - **Commands and responses:**
    - `CMD0/1/8/9/10/12/13/16/17/18/24/25/55/58/59`, plus `ACMD41`;
    - R1 after one `0xFF` byte;
    - data blocks as `FF FE` + data + CRC16;
    - error tokens `0x08` (out of range) and `0x01` (read failure);
    - unknown commands answer `0x04`.
  - **Writes** get `0x05`, then busy, and are counted in `sd_card.writes`.

- [ ] **Step 1: Write the test**

`tests/pinheck/storage/sd_test.c`:

```c
#include "sd.h"
#include <stdio.h>
#include <string.h>

static sd_card card;
static int fails;
static uint32_t last_lba;

#define CHECK(c) do { if (!(c)) { printf("SD FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int dev_read(void *ctx, uint32_t lba, uint8_t *b)
{
	int k;
	(void)ctx;
	last_lba = lba;
	for (k = 0; k < 512; k++) b[k] = (uint8_t)(lba * 7 + k);
	return 0;
}

static uint8_t xfer(uint8_t out)
{
	uint8_t in = 0;
	int k;
	for (k = 7; k >= 0; k--) {
		int bit = (out >> k) & 1;
		sd_update(&card, 0, 0, bit);
		in = (uint8_t)(in << 1 | sd_update(&card, 0, 1, bit));
	}
	return in;
}

static void deselect(void)
{
	int k;
	for (k = 0; k < 8; k++) { sd_update(&card, 1, 0, 1); sd_update(&card, 1, 1, 1); }
	sd_update(&card, 1, 0, 1);
}

static uint8_t cmd(int idx, uint32_t arg, uint8_t crc)
{
	int k;
	uint8_t r = 0xFF;
	xfer((uint8_t)(0x40 | idx));
	xfer((uint8_t)(arg >> 24)); xfer((uint8_t)(arg >> 16)); xfer((uint8_t)(arg >> 8)); xfer((uint8_t)arg);
	xfer(crc);
	for (k = 0; k < 8 && r == 0xFF; k++) r = xfer(0xFF);
	return r;
}

static uint16_t crc16(const uint8_t *d, int n)
{
	uint16_t c = 0;
	int i, b;
	for (i = 0; i < n; i++) {
		c ^= (uint16_t)(d[i] << 8);
		for (b = 0; b < 8; b++) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1);
	}
	return c;
}

static int read_data(uint8_t *d, int n)
{
	int k;
	uint8_t t = 0xFF;
	uint16_t c;
	for (k = 0; k < 16 && t == 0xFF; k++) t = xfer(0xFF);
	if (t != 0xFE) return t;
	for (k = 0; k < n; k++) d[k] = xfer(0xFF);
	c = (uint16_t)(xfer(0xFF) << 8);
	c |= xfer(0xFF);
	return c == crc16(d, n) ? 0 : -2;
}

static void init_card(uint32_t sectors)
{
	sd_blockdev dev;
	dev.ctx = NULL;
	dev.sectors = sectors;
	dev.read = dev_read;
	sd_init(&card, &dev);
	deselect();
}

static void sdhc_path(void)
{
	uint8_t b[512], csd[16];
	int k, ok = 1;

	init_card(1584690);
	CHECK(sd_update(&card, 1, 0, 1) == 1);
	CHECK(cmd(0, 0, 0x95) == 0x01);
	CHECK(cmd(8, 0x1AA, 0x87) == 0x01);
	CHECK(xfer(0xFF) == 0x00 && xfer(0xFF) == 0x00 && xfer(0xFF) == 0x01 && xfer(0xFF) == 0xAA);
	CHECK(cmd(55, 0, 0x65) == 0x01);
	CHECK(cmd(41, 0x40000000u, 0x77) == 0x00);
	CHECK(cmd(58, 0, 0xFD) == 0x00);
	CHECK(xfer(0xFF) == 0xC0 && xfer(0xFF) == 0xFF && xfer(0xFF) == 0x80 && xfer(0xFF) == 0x00);
	CHECK(cmd(9, 0, 0xAF) == 0x00);
	CHECK(read_data(csd, 16) == 0);
	CHECK(csd[0] >> 6 == 1);
	CHECK((((uint32_t)(csd[7] & 0x3F) << 16) | (uint32_t)csd[8] << 8 | csd[9]) == 1584690u / 1024 - 1);
	CHECK(cmd(17, 12345, 0xFF) == 0x00);
	CHECK(read_data(b, 512) == 0 && last_lba == 12345);
	for (k = 0; k < 512; k++) ok &= b[k] == (uint8_t)(12345 * 7 + k);
	CHECK(ok);
	CHECK(cmd(18, 200, 0xFF) == 0x00);
	CHECK(read_data(b, 512) == 0 && b[0] == (uint8_t)(200 * 7));
	CHECK(read_data(b, 512) == 0 && b[0] == (uint8_t)(201 * 7));
	CHECK(cmd(12, 0, 0xFF) == 0x00);
	CHECK(xfer(0xFF) == 0xFF);
	CHECK(cmd(17, 1584690, 0xFF) == 0x00);
	CHECK(read_data(b, 512) == 0x08);
	CHECK(cmd(24, 7, 0xFF) == 0x00);
	xfer(0xFF); xfer(0xFE);
	for (k = 0; k < 514; k++) xfer(0x5A);
	CHECK((xfer(0xFF) & 0x1F) == 0x05);
	CHECK(card.writes == 1);
	CHECK(cmd(63, 0, 0xFF) == 0x04);
	deselect();
	CHECK(sd_update(&card, 1, 1, 0) == 1);
}

static void byte_addressed_path(void)
{
	uint8_t b[512];
	init_card(4096);
	CHECK(cmd(0, 0, 0x95) == 0x01);
	CHECK(cmd(55, 0, 0x65) == 0x01);
	CHECK(cmd(41, 0, 0xFF) == 0x00);
	CHECK(cmd(58, 0, 0xFD) == 0x00);
	CHECK((xfer(0xFF) & 0x40) == 0);
	xfer(0xFF); xfer(0xFF); xfer(0xFF);
	CHECK(cmd(17, 3 * 512, 0xFF) == 0x00);
	CHECK(read_data(b, 512) == 0 && last_lba == 3);
	CHECK(cmd(17, 3 * 512 + 1, 0xFF) == 0x20);
	CHECK(cmd(16, 1024, 0xFF) == 0x40);
}

static void clock_high_at_select(void)
{
	init_card(4096);
	sd_update(&card, 1, 1, 1);
	CHECK(cmd(0, 0, 0x95) == 0x01);
	CHECK(cmd(8, 0x1AA, 0x87) == 0x01);
	CHECK(xfer(0xFF) == 0x00 && xfer(0xFF) == 0x00 && xfer(0xFF) == 0x01 && xfer(0xFF) == 0xAA);
}

int main(void)
{
	clock_high_at_select();
	sdhc_path();
	byte_addressed_path();
	printf("sd: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `tests/pinheck/storage/check.sh`
Expected: exit 2 with `sd.c: No such file or directory`.

- [ ] **Step 3: Write the card**

`src/wpc/pinheck/sd.h`:

```c
#ifndef PINHECK_SD_H
#define PINHECK_SD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sd_blockdev {
	void *ctx;
	uint32_t sectors;
	int (*read)(void *ctx, uint32_t lba, uint8_t *buf512);
} sd_blockdev;

typedef struct sd_card {
	sd_blockdev dev;
	int cs, sclk, dout;
	int idle, v2, hc, acmd, multi, wstate;
	uint32_t next_lba, wcount, writes, reads;
	uint8_t cmd[6];
	int ncmd;
	uint8_t in, inbits, risen;
	uint8_t out[640];
	int olen, opos, obit;
} sd_card;

void sd_init(sd_card *s, const sd_blockdev *dev);
int sd_update(sd_card *s, int cs, int sclk, int mosi);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/sd.c`:

```c
#include "sd.h"
#include <string.h>

static uint8_t crc7(const uint8_t *d, int n)
{
	uint8_t c = 0;
	int i, b;
	for (i = 0; i < n; i++)
		for (b = 7; b >= 0; b--) {
			int x = ((c >> 6) ^ (d[i] >> b)) & 1;
			c = (uint8_t)((c << 1) & 0x7F);
			if (x) c ^= 0x09;
		}
	return c;
}

static uint16_t crc16(const uint8_t *d, int n)
{
	uint16_t c = 0;
	int i, b;
	for (i = 0; i < n; i++) {
		c ^= (uint16_t)(d[i] << 8);
		for (b = 0; b < 8; b++) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1);
	}
	return c;
}

static void bits(uint8_t *r, int hi, int width, uint32_t v)
{
	int k;
	for (k = 0; k < width; k++) {
		int bit = hi - width + 1 + k, byte = 15 - bit / 8;
		if ((v >> k) & 1) r[byte] |= (uint8_t)(1 << (bit % 8));
		else r[byte] &= (uint8_t)~(1 << (bit % 8));
	}
}

static void push(sd_card *s, uint8_t b)
{
	if (s->olen == (int)sizeof(s->out)) {
		memmove(s->out, s->out + s->opos, (size_t)(s->olen - s->opos));
		s->olen -= s->opos;
		s->opos = 0;
	}
	if (s->olen < (int)sizeof(s->out)) s->out[s->olen++] = b;
}

static void r1(sd_card *s, uint8_t v)
{
	push(s, 0xFF);
	push(s, (uint8_t)(v | (s->idle ? 0x01 : 0x00)));
}

static void data_block(sd_card *s, const uint8_t *d, int n)
{
	uint16_t c = crc16(d, n);
	int k;
	push(s, 0xFF);
	push(s, 0xFE);
	for (k = 0; k < n; k++) push(s, d[k]);
	push(s, (uint8_t)(c >> 8));
	push(s, (uint8_t)c);
}

static int read_block(sd_card *s, uint32_t lba)
{
	uint8_t b[512];
	if (lba >= s->dev.sectors) { push(s, 0xFF); push(s, 0x08); return -1; }
	if (s->dev.read(s->dev.ctx, lba, b)) { push(s, 0xFF); push(s, 0x01); return -1; }
	s->reads++;
	data_block(s, b, 512);
	return 0;
}

static void reg16(sd_card *s, int cid)
{
	uint8_t r[16];
	memset(r, 0, sizeof(r));
	if (cid) {
		bits(r, 127, 8, 0x03);
		bits(r, 119, 16, ('S' << 8) | 'D');
		bits(r, 103, 32, ('P' << 24) | ('I' << 16) | ('N' << 8) | 'H');
		bits(r, 71, 8, 'K');
		bits(r, 63, 8, 0x10);
		bits(r, 55, 32, 0x20160701u);
		bits(r, 19, 12, (16 << 4) | 7);
	} else if (s->hc) {
		bits(r, 127, 2, 1);
		bits(r, 119, 8, 0x0E); bits(r, 103, 8, 0x32); bits(r, 95, 12, 0x5B5); bits(r, 83, 4, 9);
		bits(r, 69, 22, s->dev.sectors / 1024 ? s->dev.sectors / 1024 - 1 : 0);
		bits(r, 46, 1, 1); bits(r, 45, 7, 0x7F); bits(r, 25, 4, 9);
	} else {
		uint32_t c = s->dev.sectors / 512 ? s->dev.sectors / 512 - 1 : 0;
		bits(r, 119, 8, 0x0E); bits(r, 103, 8, 0x32); bits(r, 95, 12, 0x5B5); bits(r, 83, 4, 9);
		bits(r, 79, 1, 1); bits(r, 73, 12, c > 4095 ? 4095 : c); bits(r, 49, 3, 7);
		bits(r, 46, 1, 1); bits(r, 45, 7, 0x7F); bits(r, 25, 4, 9);
	}
	r[15] = (uint8_t)(crc7(r, 15) << 1 | 1);
	r1(s, 0x00);
	data_block(s, r, 16);
}

static void command(sd_card *s)
{
	uint32_t arg = (uint32_t)s->cmd[1] << 24 | (uint32_t)s->cmd[2] << 16 | (uint32_t)s->cmd[3] << 8 | s->cmd[4];
	int idx = s->cmd[0] & 0x3F, acmd = s->acmd;

	s->acmd = 0;
	if (acmd && idx == 41) {
		if (s->v2 && (arg & 0x40000000u)) s->hc = 1;
		s->idle = 0;
		r1(s, 0x00);
		return;
	}
	switch (idx) {
	case 0: s->idle = 1; s->v2 = s->hc = s->multi = s->wstate = 0; r1(s, 0x00); break;
	case 1: s->idle = 0; r1(s, 0x00); break;
	case 8: s->v2 = 1; r1(s, 0x00); push(s, 0x00); push(s, 0x00); push(s, (uint8_t)((arg >> 8) & 0x0F)); push(s, (uint8_t)arg); break;
	case 9: reg16(s, 0); break;
	case 10: reg16(s, 1); break;
	case 12: s->multi = 0; s->olen = s->opos = 0; r1(s, 0x00); break;
	case 13: r1(s, 0x00); push(s, 0x00); break;
	case 16: r1(s, arg == 512 ? 0x00 : 0x40); break;
	case 17: case 18: {
		uint32_t lba = s->hc ? arg : arg >> 9;
		if (!s->hc && (arg & 511)) { r1(s, 0x20); break; }
		r1(s, 0x00);
		if (read_block(s, lba) == 0 && idx == 18) { s->multi = 1; s->next_lba = lba + 1; }
		break;
	}
	case 24: case 25: r1(s, 0x00); s->wstate = idx == 25 ? 3 : 1; break;
	case 55: r1(s, 0x00); s->acmd = 1; break;
	case 58: r1(s, 0x00); push(s, (uint8_t)((s->idle ? 0 : 0x80) | (s->hc ? 0x40 : 0))); push(s, 0xFF); push(s, 0x80); push(s, 0x00); break;
	case 59: r1(s, 0x00); break;
	default: r1(s, 0x04); break;
	}
}

static void write_byte(sd_card *s, uint8_t b)
{
	int multi = s->wstate >= 3;
	int st = multi ? s->wstate - 2 : s->wstate;
	if (st == 1) {
		if (multi && b == 0xFD) { push(s, 0xFF); push(s, 0x00); push(s, 0xFF); s->wstate = 0; return; }
		if (b == (multi ? 0xFC : 0xFE)) { s->wcount = 0; s->wstate = multi ? 4 : 2; }
		return;
	}
	if (++s->wcount < 514) return;
	s->writes++;
	push(s, 0x05); push(s, 0x00); push(s, 0x00); push(s, 0xFF);
	s->wstate = multi ? 3 : 0;
}

static void in_byte(sd_card *s, uint8_t b)
{
	if (s->wstate) { write_byte(s, b); return; }
	if (!s->ncmd && (b & 0xC0) != 0x40) return;
	s->cmd[s->ncmd++] = b;
	if (s->ncmd == 6) { s->ncmd = 0; command(s); }
}

static uint8_t pop(sd_card *s)
{
	if (s->opos >= s->olen && s->multi) {
		s->olen = s->opos = 0;
		if (read_block(s, s->next_lba++)) s->multi = 0;
	}
	if (s->opos >= s->olen) { s->olen = s->opos = 0; return 0xFF; }
	return s->out[s->opos++];
}

void sd_init(sd_card *s, const sd_blockdev *dev)
{
	memset(s, 0, sizeof(*s));
	s->dev = *dev;
	s->cs = 1;
	s->dout = 0xFF;
	s->idle = 1;
}

int sd_update(sd_card *s, int cs, int sclk, int mosi)
{
	cs = cs != 0; sclk = sclk != 0; mosi = mosi != 0;
	if (cs) {
		if (!s->cs) { s->inbits = 0; s->ncmd = 0; s->olen = s->opos = 0; s->obit = 0; s->dout = 0xFF; s->multi = 0; }
		s->cs = 1;
		s->sclk = sclk;
		return 1;
	}
	if (s->cs) { s->cs = 0; s->inbits = 0; s->obit = 0; s->dout = 0xFF; s->risen = 0; }
	if (sclk && !s->sclk) {
		s->risen = 1;
		s->in = (uint8_t)(s->in << 1 | mosi);
		if (++s->inbits == 8) { s->inbits = 0; in_byte(s, s->in); }
	} else if (!sclk && s->sclk && s->risen) {
		s->risen = 0;
		if (++s->obit == 8) { s->obit = 0; s->dout = pop(s); }
	}
	s->sclk = sclk;
	return (s->dout >> (7 - s->obit)) & 1;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tests/pinheck/storage/check.sh`
Expected: `zipsrc: ok`, `vfat: ok`, `fsck: clean (build/test.zip)`, `mount: 5 files byte-exact against test.zip`, `sd: ok`, `storage: 0 failed`.

- [ ] **Step 5: Document the suite and commit**

`tests/pinheck/storage/README.md`:

```markdown
# SD card storage tests

`./check.sh` builds and runs everything. Set `DOMINOS_ZIP` to a real romset zip to add it to the volume checks. Needs `zlib`, `fsck.vfat` (dosfstools), `mcopy` (mtools) and Python 3.

- `zipsrc_test.c` checks the romset zip reader against a zip written by Python's `zipfile` (`mkzip.py`): stored and deflated entries, directory entries, range errors, and the LRU cache, including an entry larger than the whole cache.
- `vfat_test.c` checks the synthesised FAT32 volume structure by structure: MBR, boot sector (32 KB clusters), FSInfo, directory entries, 8.3 names, exclusions, FAT chains and zero padding.
- `vfatimg.c` writes the volume to a file. `fsck.vfat -n` must accept it, and `mcopy` (an independent FAT reader) must extract every eligible zip entry byte-identical (`cmpzip.py`).
- `sd_test.c` drives the SD card model through a bit-banged SPI host: init handshake, SDHC and byte addressing, CSD, single and multi-block reads with CRC16, writes, error tokens, and chip-select asserted while SCLK is high.
```

```bash
git add src/wpc/pinheck/sd.h src/wpc/pinheck/sd.c tests/pinheck/storage/sd_test.c tests/pinheck/storage/README.md
git commit -m "pinheck: SD card model in SPI mode"
```

### Task 4: Cycle-exact NCO counter pin outputs

**Files:**
- Create: `tests/pinheck/p8x32a/chip/nco.spin`
- Modify (replace whole files): `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`

**Interfaces:**
- Consumes: the Milestone 3 core and its RTL-compared chip-test harness (`tests/pinheck/p8x32a/check.sh` runs every `chip/*.spin` against `p1rtl`).
- Produces: no API change. NCO single (mode 4) drives `phs[31]` on APIN; NCO differential (mode 5) also drives its inverse on BPIN. Both are OR-ed with `OUTA`, gated by `DIRA`, and visible in `pins_out`, `INA` and `WAITPEQ`/`WAITPNE`. `p8x32a_cog` gains `ctr_old`/`frq_old`/`phs_old`/`phs_t_old`/`ctr_at`, the state before a pending counter write. `p8x32a` gains `flushed`, the time up to which pins have been emitted.

How it works, all visible in the code:
- **Next toggle, O(1).** From `x' = phs & 0x7FFFFFFF` the next toggle is `ceil((2^31 - x')/F)` cycles away when `F < 2^31`, and `floor(x'/(2^32 - F)) + 1` when `F >= 2^31`. With `F = 0` the pin changes only on a `PHS` write.
- **`flush()`** interleaves those toggle times with the pending pin events.
- **Pin-waits.** `wait_pins()` counts the next toggle as a wake candidate.
- **Counter writes.** A write or `COGSTOP` keeps the old state until it takes effect, and the old state's own toggles before then still happen.

- [ ] **Step 1: Write the failing test**

Cog 0 exercises both frequency regimes, the differential output, the SD driver's `FRQ` = 0 / `PHS` shift trick, `DIRA` gating, a mode switch and turning a counter off. The worker waits on cog 0's counter pin and, during cog 0's final 20,000-cycle idle, on its own.

`tests/pinheck/p8x32a/chip/nco.spin`:

```text
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        coginit x wr
        mov     dira, #%111110
        mov     ctra, ncoa
        mov     frqa, fa
        mov     ctrb, ncob
        mov     frqb, fb
        mov     t, cnt
        add     t, #400
        waitcnt t, #0
        mov     frqa, fhi
        mov     frqb, fhalf
        mov     t, cnt
        add     t, #200
        waitcnt t, #0
        mov     frqb, #0
        mov     phsb, data
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        mov     u, ina
        wrlong  u, ptr
        add     ptr, #4
        andn    dira, #%10
        mov     t, cnt
        add     t, #60
        waitcnt t, #0
        or      dira, #%10
        mov     frqa, fa
        mov     ctrb, ncob1
        mov     frqb, fb
        mov     t, cnt
        add     t, #300
        waitcnt t, #0
        mov     u, ina
        wrlong  u, ptr
        add     ptr, #4
        mov     t, cnt
        add     t, idle
        waitcnt t, #0
        mov     ctra, #0
        mov     ctrb, ncob
        mov     t, cnt
        add     t, #100
        waitcnt t, #0
        mov     u, phsb
        wrlong  u, ptr
        cogstop x
        cogid   t
        cogstop t
x       long    0
t       long    0
u       long    0
ncoa    long    %00100 << 26 | 1
ncob    long    %00101 << 26 | 3 << 9 | 2
ncob1   long    %00100 << 26 | 2
fa      long    $0100_0000
fb      long    $4000_0000
fhi     long    $C000_0000
fhalf   long    $7FFF_FFFF
data    long    $A5C3_0000
ptr     long    $6000
idle    long    20000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, #%10000
        mov     ctra, nco4
        mov     frqa, f4
        mov     a, #$60
        shl     a, #8
        add     a, #32
        mov     s, cnt
        mov     n, #3
:own    waitpne p4, p4
        waitpeq p4, p4
        mov     e, cnt
        sub     e, s
        wrlong  e, a
        add     a, #4
        djnz    n, #:own
        waitpeq p1, p1
        mov     e, cnt
        sub     e, s
        wrlong  e, a
        add     a, #4
        waitpne p1, p1
        mov     e, cnt
        sub     e, s
        wrlong  e, a
        add     a, #4
        mov     e, ina
        wrlong  e, a
:spin   jmp     #:spin
nco4    long    %00100 << 26 | 4
f4      long    $0080_0000
p1      long    %10
p4      long    %10000
n       long    0
s       long    0
e       long    0
a       long    0
```

- [ ] **Step 2: Run to verify it fails**

Run: `env -u P8X32A_ROM -u DOMINOS_PRP tests/pinheck/p8x32a/check.sh`
Expected: `RTL MISMATCH chip/nco.spin` and a non-zero `failed` count.

- [ ] **Step 3: Replace the core**

`src/cpu/p8x32a/p8x32a.h`:

```c
#ifndef P8X32A_H
#define P8X32A_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P8X32A_NEVER 0xFFFFFFFFFFFFFFFFull

typedef struct p8x32a_bus {
	void *ctx;
	uint32_t (*pins_in)(void *ctx, uint64_t t);
	uint64_t (*pins_next)(void *ctx, uint64_t t); /* P8X32A_NEVER if no edge is known yet; re-queried every run_until */
	void (*pins_out)(void *ctx, uint64_t t, uint32_t out, uint32_t dir);
	void (*cog_start)(void *ctx, uint64_t t, int cog, uint32_t ptr);
	void (*clkset)(void *ctx, uint64_t t, uint8_t cfg);
	void (*log)(void *ctx, const char *msg);
} p8x32a_bus;

typedef struct p8x32a_reg {
	uint32_t prev, cur;
	uint64_t at;
} p8x32a_reg;

typedef struct p8x32a_cog {
	uint32_t ram[512];
	uint32_t ptr, ix, nix, i, s, d;
	uint16_t p, px;
	uint8_t c, z, cancel, run, cond;
	int ev;
	uint64_t ev_t, t0, latch, disable_at, restart_at;
	p8x32a_reg outa, dira;
	uint32_t ctr[2], frq[2], phs[2], vcfg, vscl;
	uint64_t phs_t[2];
	uint32_t ctr_old[2], frq_old[2], phs_old[2];
	uint64_t phs_t_old[2], ctr_at[2];
} p8x32a_cog;

typedef struct p8x32a {
	p8x32a_bus bus;
	uint8_t hub[65536];
	p8x32a_cog cog[8];
	uint8_t cog_e, lock_e, lock_state, cfg, sys_q, sys_c;
	uint64_t now, horizon, flushed, slot_base, cnt_base;
	uint64_t pend[40];
	int npend;
	uint32_t last_out, last_dir;
	uint32_t logged;
	int stop;
} p8x32a;

void p8x32a_init(p8x32a *p, const p8x32a_bus *bus);
void p8x32a_reset(p8x32a *p, uint64_t t);
void p8x32a_run_until(p8x32a *p, uint64_t t);
uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir);
unsigned p8x32a_dasm(char *buf, uint32_t op);

#ifdef __cplusplus
}
#endif

#endif
```

`src/cpu/p8x32a/p8x32a.c`:

```c
#include "p8x32a.h"
#include <stdio.h>
#include <string.h>

enum { EV_NONE, EV_HUB, EV_EXEC, EV_DONE, EV_WAITPIN, EV_RESTART };

#define OP(i)   ((unsigned)((i) >> 26))
#define FWZ(i)  (((i) >> 25) & 1)
#define FWC(i)  (((i) >> 24) & 1)
#define FWR(i)  (((i) >> 23) & 1)
#define FIM(i)  (((i) >> 22) & 1)
#define COND(i) (((i) >> 18) & 15)
#define DST(i)  (((i) >> 9) & 511)
#define SRC(i)  ((i) & 511)

enum {
	LOG_WAITVID = 1, LOG_CTR_MODE = 2, LOG_CTR_OUT = 4, LOG_REBOOT = 8
};

static const uint8_t unscr[32] = {
	10, 26, 24, 29, 27, 13, 22, 28, 2, 25, 18, 9, 5, 16, 31, 23,
	1, 30, 14, 0, 11, 8, 15, 20, 17, 4, 19, 6, 12, 21, 7, 3
};

static void log_once(p8x32a *p, uint32_t what, const char *msg)
{
	if (p->logged & what) return;
	p->logged |= what;
	if (p->bus.log) p->bus.log(p->bus.ctx, msg);
}

static uint32_t unscramble(uint32_t w)
{
	uint32_t r = 0;
	int k;
	for (k = 0; k < 32; k++) r |= ((w >> unscr[k]) & 1u) << k;
	return r;
}

static uint32_t rd32(const p8x32a *p, uint32_t a)
{
	a &= 0xFFFC;
	return p->hub[a] | (uint32_t)p->hub[a + 1] << 8 | (uint32_t)p->hub[a + 2] << 16 | (uint32_t)p->hub[a + 3] << 24;
}

static uint32_t regval(const p8x32a_reg *r, uint64_t t) { return t >= r->at ? r->cur : r->prev; }

static void add_pending(p8x32a *p, uint64_t t)
{
	int k;
	for (k = 0; k < p->npend; k++)
		if (p->pend[k] == t) return;
	if (p->npend < (int)(sizeof(p->pend) / sizeof(p->pend[0]))) p->pend[p->npend++] = t;
}

static void flush(p8x32a *p, uint64_t t);

static void regset(p8x32a *p, p8x32a_reg *r, uint32_t v, uint64_t at)
{
	flush(p, p->now);
	if (at > r->at) r->prev = r->cur;
	r->cur = v;
	r->at = at;
	add_pending(p, at);
}

static int nco(uint32_t ctr)
{
	unsigned m = (ctr >> 26) & 31;
	return m == 4 || m == 5;
}

static uint32_t ctr_pins(const p8x32a_cog *c, int k, uint64_t t)
{
	int old = t < c->ctr_at[k];
	uint32_t ctr = old ? c->ctr_old[k] : c->ctr[k], x, a, r;
	uint32_t frq = old ? c->frq_old[k] : c->frq[k], ph = old ? c->phs_old[k] : c->phs[k];
	uint64_t pt = old ? c->phs_t_old[k] : c->phs_t[k];
	if (!nco(ctr)) return 0;
	x = t <= pt ? ph : ph + frq * (uint32_t)(t - pt);
	a = x >> 31;
	r = a << (ctr & 31);
	if (((ctr >> 26) & 31) == 5) r |= (a ^ 1) << ((ctr >> 9) & 31);
	return r;
}

static uint64_t nco_toggle(uint32_t ctr, uint32_t f, uint32_t ph, uint64_t pt, uint64_t t)
{
	uint32_t x;
	uint64_t dt;
	if (!nco(ctr)) return P8X32A_NEVER;
	if (t < pt) return pt;
	if (!f) return P8X32A_NEVER;
	x = (ph + f * (uint32_t)(t - pt)) & 0x7FFFFFFFu;
	if (f < 0x80000000u) dt = ((uint64_t)0x80000000u - x + f - 1) / f;
	else dt = (uint64_t)x / (0x100000000ull - f) + 1;
	return t + dt;
}

static uint64_t ctr_toggle(const p8x32a_cog *c, int k, uint64_t t)
{
	if (t < c->ctr_at[k]) {
		uint64_t o = nco_toggle(c->ctr_old[k], c->frq_old[k], c->phs_old[k], c->phs_t_old[k], t);
		return o < c->ctr_at[k] ? o : c->ctr_at[k];
	}
	return nco_toggle(c->ctr[k], c->frq[k], c->phs[k], c->phs_t[k], t);
}

static uint64_t ctr_next(const p8x32a *p, uint64_t t)
{
	uint64_t nt = P8X32A_NEVER, e;
	int n, k;
	for (n = 0; n < 8; n++)
		for (k = 0; k < 2; k++)
			if ((e = ctr_toggle(&p->cog[n], k, t)) < nt) nt = e;
	return nt;
}

uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir)
{
	uint32_t o = 0, d = 0;
	int n;
	for (n = 0; n < 8; n++) {
		p8x32a_cog *c = &p->cog[n];
		uint32_t dd = regval(&c->dira, t);
		d |= dd;
		o |= (regval(&c->outa, t) | ctr_pins(c, 0, t) | ctr_pins(c, 1, t)) & dd;
	}
	*dir = d;
	return o;
}

static void flush(p8x32a *p, uint64_t t)
{
	for (;;) {
		int k, best = -1;
		uint32_t out, dir;
		uint64_t when = ctr_next(p, p->flushed);
		for (k = 0; k < p->npend; k++)
			if (p->pend[k] <= t && (best < 0 || p->pend[k] < p->pend[best])) best = k;
		if (best >= 0 && p->pend[best] < when) when = p->pend[best];
		if (when > t) break;
		out = p8x32a_pins(p, when, &dir);
		if ((out != p->last_out || dir != p->last_dir) && p->bus.pins_out) p->bus.pins_out(p->bus.ctx, when, out, dir);
		p->last_out = out;
		p->last_dir = dir;
		if (when > p->flushed) p->flushed = when;
		for (k = 0; k < p->npend; k++)
			if (p->pend[k] == when) { p->pend[k] = p->pend[--p->npend]; k--; }
	}
	if (t > p->flushed) p->flushed = t;
}

static uint32_t ina(p8x32a *p, uint64_t t)
{
	uint32_t dir, out, ext;
	flush(p, t);
	out = p8x32a_pins(p, t, &dir);
	ext = p->bus.pins_in ? p->bus.pins_in(p->bus.ctx, t) : 0;
	return (dir & out) | (~dir & ext);
}

static uint32_t cnt(const p8x32a *p, uint64_t t) { return (uint32_t)(t - p->cnt_base); }

static int ctr_free(uint32_t ctr)
{
	unsigned m = (ctr >> 26) & 31;
	return (m >= 1 && m <= 7) || m == 31;
}

static uint32_t phs_at(const p8x32a_cog *c, int k, uint64_t t)
{
	if (!ctr_free(c->ctr[k]) || t <= c->phs_t[k]) return c->phs[k];
	return c->phs[k] + c->frq[k] * (uint32_t)(t - c->phs_t[k]);
}

static void ctr_rebase(p8x32a_cog *c, int k, uint64_t t)
{
	c->phs[k] = phs_at(c, k, t);
	c->phs_t[k] = t;
}

static void ctr_check(p8x32a *p, uint32_t ctr)
{
	unsigned m = (ctr >> 26) & 31;
	if ((m >= 8 && m <= 15) || (m >= 17 && m <= 30)) log_once(p, LOG_CTR_MODE, "p8x32a: pin-sensing counter mode not modelled");
	if (m >= 2 && m <= 3) log_once(p, LOG_CTR_OUT, "p8x32a: counter PLL pin outputs not modelled (Plan 6)");
}

static void ctr_save(p8x32a *p, p8x32a_cog *c, int k, uint64_t e)
{
	flush(p, p->now);
	c->ctr_old[k] = c->ctr[k];
	c->frq_old[k] = c->frq[k];
	c->phs_old[k] = c->phs[k];
	c->phs_t_old[k] = c->phs_t[k];
	c->ctr_at[k] = e;
	add_pending(p, e);
}

static void special_write(p8x32a *p, int n, unsigned a, uint32_t v, uint64_t m3)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t e = m3 + 1;
	int k = a & 1;
	switch (a) {
	case 0x1F4: regset(p, &c->outa, v, e); break;
	case 0x1F6: if (e < c->disable_at) regset(p, &c->dira, v, e); break;
	case 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; ctr_check(p, v); break;
	case 0x1FA: case 0x1FB: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->frq[k] = v; break;
	case 0x1FC: case 0x1FD: ctr_save(p, c, k, e); c->phs[k] = v; c->phs_t[k] = e; break;
	case 0x1FE: c->vcfg = v; break;
	case 0x1FF: c->vscl = v; break;
	}
}

static uint32_t sread(p8x32a *p, int n, unsigned a, uint64_t t)
{
	p8x32a_cog *c = &p->cog[n];
	switch (a) {
	case 0x1F0: return (c->ptr >> 14) << 2;
	case 0x1F1: return cnt(p, t);
	case 0x1F2: return ina(p, t);
	case 0x1FC: return phs_at(c, 0, t);
	case 0x1FD: return phs_at(c, 1, t);
	}
	return c->ram[a];
}

static uint32_t bitrev(uint32_t x)
{
	uint32_t r = 0;
	int k;
	for (k = 0; k < 32; k++) r |= ((x >> k) & 1u) << (31 - k);
	return r;
}

static int parity(uint32_t x)
{
	x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
	return (int)(x & 1);
}

static uint32_t alu(unsigned i, uint32_t s, uint32_t d, unsigned pc, int run, int ci, int zi,
                    uint32_t bus_q, int bus_c, int *wr, int *co, int *zo)
{
	uint32_t r, dr = bitrev(d), rot_r, lo, fill, log_r, add_d, add_s, add_r, sum_lo;
	unsigned sh = s & 31, ls;
	int rot_c, log_c, add_sub, add_ci, add_co, add_cm, add_cs, add_c, cin, c30, b31;
	uint64_t rot;
	static const unsigned ads_sel[4] = { 0, 1, 2, 3 };

	switch (i & 7) {
	case 0: fill = d & 0x7FFFFFFFu; break;
	case 1: fill = dr & 0x7FFFFFFFu; break;
	case 4: case 5: fill = ci ? 0x7FFFFFFFu : 0; break;
	case 6: fill = (d >> 31) ? 0x7FFFFFFFu : 0; break;
	default: fill = 0; break;
	}
	lo = (i & 1) ? dr : d;
	rot = (((uint64_t)fill << 32) | lo) >> sh;
	rot_r = (((i >> 1) & 3) != 3 && (i & 1)) ? bitrev((uint32_t)rot) : (uint32_t)rot;
	rot_c = (((i >> 1) & 3) != 3 && (i & 1)) ? (int)(dr & 1) : (int)(d & 1);

	if (i & 4) ls = ((unsigned)(((i & 2) ? zi : ci) ^ (int)(i & 1))) << 1;
	else ls = (((i >> 1) & 1) << 1) | (unsigned)!(((i >> 1) ^ i) & 1);
	if (i & 8) {
		switch (ls) {
		case 0: log_r = d & ~s; break;
		case 1: log_r = d & s; break;
		case 2: log_r = d | s; break;
		default: log_r = d ^ s; break;
		}
	} else if (i & 4) {
		switch (i & 3) {
		case 0: log_r = (d & 0xFFFFFE00u) | (s & 511); break;
		case 1: log_r = (d & 0xFFFC01FFu) | ((s & 511) << 9); break;
		case 2: log_r = ((s & 511) << 23) | (d & 0x007FFFFFu); break;
		default: log_r = (d & 0xFFFFFE00u) | (pc & 511); break;
		}
	} else
		log_r = s;
	log_c = parity(log_r);

	(void)ads_sel;
	if (((i >> 4) & 3) == 2) {
		int ads[4];
		ads[0] = 0; ads[1] = (int)(s >> 31); ads[2] = ci; ads[3] = zi;
		add_sub = ads[(i >> 1) & 3] ^ (int)(i & 1);
	} else if (i == 0x32 || i == 0x34 || i == 0x36 || (i >> 2) == 0xF)
		add_sub = 0;
	else
		add_sub = 1;
	add_ci = ((((i >> 3) & 7) == 6 && ((i & 7) == 1 || (i & 2))) && ci) || (((i >> 3) & 3) == 3 && (i & 3) == 1);
	add_d = (((i >> 3) & 3) == 1) ? 0 : d;
	add_s = ((i & 31) == 0x19 || ((i >> 1) & 15) == 0xD) ? 0xFFFFFFFFu : add_sub ? ~s : s;
	cin = add_ci ^ add_sub;
	sum_lo = (add_d & 0x7FFFFFFFu) + (add_s & 0x7FFFFFFFu) + (uint32_t)cin;
	c30 = (int)(sum_lo >> 31);
	b31 = (int)(add_d >> 31) + (int)(add_s >> 31) + c30;
	add_r = (sum_lo & 0x7FFFFFFFu) | ((uint32_t)(b31 & 1) << 31);
	add_co = b31 >> 1;
	add_cm = c30;
	add_cs = add_co ^ (int)(add_d >> 31) ^ (int)(add_s >> 31);
	if (i == 0x38) add_c = add_co;
	else if (((i >> 3) & 7) == 5) add_c = (int)(s >> 31);
	else if ((i & 0x20) && ((i >> 2) & 3) == 1) add_c = add_co ^ add_cm;
	else if (((i >> 1) & 15) == 8) add_c = add_cs;
	else add_c = add_co ^ add_sub;

	if ((i >> 2) == 4) *wr = (int)(i & 1) ^ ((i & 2) ? !add_co : add_cs);
	else if (i == 0x38) *wr = add_co;
	else *wr = 1;

	if (i & 0x20) r = add_r;
	else if (i & 0x10) r = log_r;
	else if (i & 8) r = rot_r;
	else r = (run || (pc >> 4) != 31) ? bus_q : 0;

	switch ((i >> 3) & 7) {
	case 0: *co = bus_c; break;
	case 1: *co = rot_c; break;
	case 3: *co = log_c; break;
	default: *co = add_c; break;
	}
	*zo = !r && (zi || !(((i >> 3) & 7) == 6 && ((i & 7) == 1 || (i & 2))));
	return r;
}

static void idle(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	if (c->restart_at != P8X32A_NEVER) { c->ev = EV_RESTART; c->ev_t = c->restart_at + 2; }
	else c->ev = EV_NONE;
}

static void next_instr(p8x32a *p, int n, uint64_t t0)
{
	p8x32a_cog *c = &p->cog[n];
	if (t0 >= c->disable_at) { idle(p, n); return; }
	c->t0 = t0;
	c->ev = EV_EXEC;
	c->ev_t = t0 + 2;
}

static void complete(p8x32a *p, int n, uint64_t m3, uint32_t q, int bus_c)
{
	p8x32a_cog *c = &p->cog[n];
	uint32_t i = c->i, r;
	unsigned op = OP(i);
	int wr, co, zo, jc = 0;

	r = alu(op, c->s, c->d, c->p, c->run, c->c, c->z, q, bus_c, &wr, &co, &zo);
	if (c->cond) {
		if (FWR(i)) {
			if (wr) c->ram[DST(i)] = r;
			if (DST(i) >= 0x1F0) special_write(p, n, DST(i), r, m3);
		}
		if (FWC(i)) c->c = (uint8_t)co;
		if (FWZ(i)) c->z = (uint8_t)zo;
	}
	if (c->cond) {
		int dz = !(c->d >> 1);
		if (op == 0x39) jc = dz && (c->d & 1);
		else if (op == 0x3A) jc = dz && !(c->d & 1);
		else if (op == 0x3B) jc = !(dz && !(c->d & 1));
	}
	if (!jc) c->p = (uint16_t)((c->px + 1) & 511);
	c->cancel = (uint8_t)(jc || c->px == 511);
	if (c->px == 511) c->run = 1;
	c->ix = c->nix;
	next_instr(p, n, m3 + 1);
}

static uint64_t next_slot(const p8x32a *p, int n, uint64_t from)
{
	uint64_t base = p->slot_base + 3 + 2 * (uint64_t)n;
	if (from <= base) return base;
	return base + ((from - base + 15) / 16) * 16;
}

static void stop_cog(p8x32a *p, int n, uint64_t d)
{
	p8x32a_cog *c = &p->cog[n];
	if (d < c->disable_at) c->disable_at = d;
	regset(p, &c->dira, 0, d);
	ctr_save(p, c, 0, d);
	ctr_save(p, c, 1, d);
	ctr_rebase(c, 0, d);
	ctr_rebase(c, 1, d);
	c->ctr[0] = c->ctr[1] = 0;
}

static void sys(p8x32a *p, int n, uint64_t h)
{
	p8x32a_cog *c = &p->cog[n];
	uint32_t dc = c->d;
	unsigned op = c->s & 7, num, newx = 0;
	uint8_t enc = (op & 4) ? p->lock_e : p->cog_e, bit;
	int all = enc == 0xFF, old = 0;

	while (newx < 7 && (enc >> newx & 1)) newx++;
	num = ((op == 2 && (dc & 8)) || op == 4) ? newx : (dc & 7);
	bit = (uint8_t)(1u << num);
	switch (op) {
	case 0:
		p->cfg = (uint8_t)dc;
		if (p->bus.clkset) p->bus.clkset(p->bus.ctx, h + 1, p->cfg);
		if (dc & 0x80) log_once(p, LOG_REBOOT, "p8x32a: CLKSET reset bit not modelled");
		break;
	case 2:
		if (!((dc & 8) && all)) {
			p8x32a_cog *t = &p->cog[num];
			t->ptr = dc >> 4;
			if (p->bus.cog_start) p->bus.cog_start(p->bus.ctx, h, (int)num, t->ptr);
			stop_cog(p, (int)num, h + 1);
			t->restart_at = h + 4;
			if (!(t->ev == EV_HUB && t->latch <= h) && (int)num != n) idle(p, (int)num);
		}
		p->cog_e |= bit;
		break;
	case 3:
		p->cog_e &= (uint8_t)~bit;
		if (p->cog[num].ev != EV_NONE || p->cog[num].restart_at != P8X32A_NEVER) {
			stop_cog(p, (int)num, h + 3);
			if (p->cog[num].restart_at != P8X32A_NEVER) {
				p->cog[num].restart_at = P8X32A_NEVER;
				if (p->cog[num].ev == EV_RESTART) p->cog[num].ev = EV_NONE;
			}
		}
		break;
	case 4: p->lock_e |= bit; break;
	case 5: p->lock_e &= (uint8_t)~bit; break;
	case 6: old = p->lock_state >> (dc & 7) & 1; p->lock_state |= (uint8_t)(1u << (dc & 7)); break;
	case 7: old = p->lock_state >> (dc & 7) & 1; p->lock_state &= (uint8_t)~(1u << (dc & 7)); break;
	}
	p->sys_q = (uint8_t)(op == 1 ? (unsigned)n : num);
	p->sys_c = (uint8_t)(op >= 6 ? old : all);
}

static void do_hub(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t h = c->ev_t, m3 = c->latch + 4;
	uint32_t q, a, w;
	unsigned op = OP(c->i);

	if (c->latch >= c->disable_at) { idle(p, n); return; }
	if (op == 3) {
		sys(p, n, h);
		q = p->sys_q;
	} else {
		a = c->run ? (c->s & 0xFFFF) : (((((c->ptr & 0x3FFF) + c->p) & 0x3FFF) << 2) | (c->s & 3));
		w = rd32(p, a);
		if (!c->run && (a & 0x8000)) w = unscramble(w);
		if (!FWR(c->i) && a < 0x8000) {
			uint32_t v = c->d;
			if (op == 2) { a &= 0xFFFC; p->hub[a] = (uint8_t)v; p->hub[a + 1] = (uint8_t)(v >> 8); p->hub[a + 2] = (uint8_t)(v >> 16); p->hub[a + 3] = (uint8_t)(v >> 24); }
			else if (op == 1) { a &= 0xFFFE; p->hub[a] = (uint8_t)v; p->hub[a + 1] = (uint8_t)(v >> 8); }
			else p->hub[a] = (uint8_t)v;
		}
		q = op == 2 ? w : op == 1 ? (w >> ((a & 2) * 8)) & 0xFFFF : (w >> ((a & 3) * 8)) & 0xFF;
	}
	if (m3 >= c->disable_at) { idle(p, n); return; }
	complete(p, n, m3, q, p->sys_c);
}

static void wait_pins(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t t = c->ev_t, nt = P8X32A_NEVER;
	int k, match;

	if (t + 2 >= c->disable_at) { idle(p, n); return; }
	match = ((ina(p, t) & c->s) == c->d) ^ (OP(c->i) == 0x3D);
	if (match) { c->ev = EV_DONE; c->ev_t = t + 2; return; }
	for (k = 0; k < p->npend; k++)
		if (p->pend[k] > t && p->pend[k] < nt) nt = p->pend[k];
	if (ctr_next(p, t) < nt) nt = ctr_next(p, t);
	for (k = 0; k < 8; k++)
		if (k != n && p->cog[k].ev != EV_NONE && p->cog[k].ev_t + 1 < nt) nt = p->cog[k].ev_t + 1;
	if (p->bus.pins_next) {
		uint64_t e = p->bus.pins_next(p->bus.ctx, t);
		if (e < nt) nt = e;
	}
	if (p->horizon != P8X32A_NEVER && nt > p->horizon + 1) nt = p->horizon + 1;
	if (nt <= t) nt = t + 1;
	c->ev_t = nt;
}

static void exec(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t t2 = c->ev_t, t0 = t2 - 2;
	uint32_t i = c->run ? c->ix : ((0x02u << 26) | (1u << 23) | (1u << 18) | ((uint32_t)c->p << 9));
	unsigned op = OP(i);
	int jump = op == 0x17 || op == 0x39 || op == 0x3A || op == 0x3B;

	c->i = i;
	c->cond = (uint8_t)(((COND(i) >> ((c->c << 1) | c->z)) & 1) && !c->cancel);
	c->s = FIM(i) ? SRC(i) : sread(p, n, SRC(i), t2);
	c->d = c->ram[DST(i)];
	c->px = (uint16_t)((c->cond && jump) ? (c->s & 511) : c->p);
	c->nix = c->ram[c->px];
	if (c->cond && op <= 3) {
		c->latch = next_slot(p, n, t0 + 3);
		c->ev = EV_HUB;
		c->ev_t = c->latch + 2;
		return;
	}
	if (c->cond && op == 0x3E) {
		uint64_t m = t0 + 3;
		c->ev = EV_DONE;
		c->ev_t = m + (uint32_t)(c->d - cnt(p, m)) + 2;
		return;
	}
	if (c->cond && (op == 0x3C || op == 0x3D)) {
		c->ev = EV_WAITPIN;
		c->ev_t = t0 + 3;
		return;
	}
	if (c->cond && op == 0x3F) log_once(p, LOG_WAITVID, "p8x32a: WAITVID not modelled");
	if (t0 + 3 >= c->disable_at) { idle(p, n); return; }
	complete(p, n, t0 + 3, 0, p->sys_c);
}

static void restart(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	c->p = 0;
	c->c = c->z = c->cancel = c->run = 0;
	c->disable_at = P8X32A_NEVER;
	c->restart_at = P8X32A_NEVER;
	exec(p, n);
}

void p8x32a_run_until(p8x32a *p, uint64_t t)
{
	p->horizon = t;
	for (;;) {
		int n, best = -1;
		p8x32a_cog *b;
		for (n = 0; n < 8; n++) {
			p8x32a_cog *c = &p->cog[n];
			if (c->ev == EV_NONE) continue;
			if (best < 0 || c->ev_t < p->cog[best].ev_t ||
			    (c->ev_t == p->cog[best].ev_t && c->ev == EV_HUB && p->cog[best].ev != EV_HUB)) best = n;
		}
		if (best < 0 || p->cog[best].ev_t > t || p->stop) break;
		b = &p->cog[best];
		p->now = b->ev_t;
		switch (b->ev) {
		case EV_HUB: do_hub(p, best); break;
		case EV_EXEC: exec(p, best); break;
		case EV_WAITPIN: wait_pins(p, best); break;
		case EV_RESTART: restart(p, best); break;
		case EV_DONE:
			if (b->ev_t >= b->disable_at) idle(p, best);
			else complete(p, best, b->ev_t, 0, p->sys_c);
			break;
		}
	}
	flush(p, t);
	p->now = t;
}

void p8x32a_reset(p8x32a *p, uint64_t t)
{
	int n;
	for (n = 0; n < 8; n++) {
		p8x32a_cog *c = &p->cog[n];
		uint32_t ram[512];
		memcpy(ram, c->ram, sizeof(ram));
		memset(c, 0, sizeof(*c));
		memcpy(c->ram, ram, sizeof(ram));
		c->disable_at = P8X32A_NEVER;
		c->restart_at = P8X32A_NEVER;
		c->outa.at = c->dira.at = t;
	}
	p->cog_e = 1;
	p->lock_e = p->lock_state = p->cfg = p->sys_q = p->sys_c = 0;
	p->slot_base = t;
	p->npend = 0;
	p->flushed = t;
	p->last_out = p->last_dir = 0;
	p->cog[0].ptr = 0x3E00;
	p->cog[0].restart_at = t + 3;
	idle(p, 0);
	p->now = t;
}

void p8x32a_init(p8x32a *p, const p8x32a_bus *bus)
{
	memset(p, 0, sizeof(*p));
	p->bus = *bus;
	p8x32a_reset(p, 0);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `env -u P8X32A_ROM -u DOMINOS_PRP tests/pinheck/p8x32a/check.sh`
Expected: `eeprom: ok`, `dasm: 31/31`, `p8x32a: 211 passed, 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c tests/pinheck/p8x32a/chip/nco.spin
git commit -m "p8x32a: cycle-exact NCO counter pin outputs"
```

### Task 5: SD card on the Propeller, boot exit test

**Files:**
- Create: `tests/pinheck/p8x32a/sdcheck.py`, `tests/pinheck/p8x32a/nodac.py`
- Modify (replace whole files): `tests/pinheck/p8x32a/check.sh`, `run.c`, `rtl/tb.cpp`, `tools.sh`, `README.md`

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces:
  - **Card wiring:** `run.c -sd romset.zip` and `p1rtl -sd romset.zip` attach the card on P0 (DO), P1 (SCLK), P2 (DI) and P3 (CS). Undriven, CS reads high, SCLK low and DI high.
  - **Read log:** `run.c` logs each sector read as `D cycle lba crc32 label`.
  - **`-notrace`:** drops `P` lines.
  - **`-uart cycle hexbytes`:** sends 115200-baud 8N1 frames on P24.
  - **Exit test:** `check.sh` runs the SD boot when `P8X32A_ROM`, `DOMINOS_PRP` and `DOMINOS_ZIP` are set.

- [ ] **Step 1: Write the exit test**

`nodac.py` drops only P14/P15 (the audio DUTY pins, spec §5.4) from a trace. `sdcheck.py` requires the exact boot access sequence and checks every file sector against the zip entry through Python's `zipfile`.

`tests/pinheck/p8x32a/nodac.py`:

```python
#!/usr/bin/env python3
import sys

MASK = ~(3 << 14) & 0xFFFFFFFF
last = None
for line in open(sys.argv[1]):
    if line.startswith('P '):
        _, t, o, d = line.split()
        o, d = int(o, 16) & MASK, int(d, 16) & MASK
        if (o, d) == last:
            continue
        last = (o, d)
        print('P %s %08x %08x' % (t, o, d))
    elif not line.startswith('D '):
        print(line.rstrip('\n'))
```

`tests/pinheck/p8x32a/sdcheck.py`:

```python
#!/usr/bin/env python3
import sys
import zipfile
import zlib

EXPECT = ['MBR', 'BOOT', 'DIR:/+0', 'DIR:SFX+0', 'DIR:SFX+512', 'DIR:/+0', 'DIR:DMD+0', 'DIR:DMD+512', 'DIR:DMD/_DZ+0'] + \
         ['FILE:DMD/_DZ/ZMB.VID+%d' % (512 * k) for k in range(1, 9)] + ['DIR:/+0']


def main():
    z = zipfile.ZipFile(sys.argv[2])
    names = {n.upper(): n for n in z.namelist()}
    reads = [l.split(None, 4) for l in open(sys.argv[1]) if l.startswith('D ')]
    labels = [r[4].strip() for r in reads]
    if labels != EXPECT:
        print('sd: FAIL, boot reads were:\n  ' + '\n  '.join(labels[:40]))
        return 1
    for r in reads:
        lab = r[4].strip()
        if not lab.startswith('FILE:'):
            continue
        path, off = lab[5:].rsplit('+', 1)
        data = z.read(names[path.upper()])[int(off):int(off) + 512]
        data += b'\0' * (512 - len(data))
        if '%08x' % zlib.crc32(data) != r[3]:
            print('sd: FAIL, sector %s (%s) differs from the zip entry' % (r[2], lab))
            return 1
    print('sd: boot mounted the card and read DMD/_DZ/ZMB.VID frame 0 (%d sectors, file sectors match the zip)' % len(reads))
    return 0


sys.exit(main())
```

`tests/pinheck/p8x32a/check.sh`:

```sh
#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
TOOLS=${TOOLS:-$PWD/build/tools}
SEEDS=${SEEDS:-100}
OPENSPIN=$TOOLS/openspin/build/openspin
SPINSIM=$TOOLS/spinsim/build/spinsim
P1RTL=$TOOLS/p1rtl/p1rtl
CORE=../../../src/cpu/p8x32a
DEV=../../../src/wpc/pinheck
CC="cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic"
fail=0 pass=0

for c in $CORE/*.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c; do
	[ -e "$c" ] || continue
	cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only -I$CORE -I$DEV "$c" || { echo "C89 FAIL $c"; fail=$((fail + 1)); }
done
mkdir -p $B/rtl $B/spin $B/boot
if [ -f ../eeprom/eeprom_test.c ]; then
	$CC -I$DEV -o $B/eeprom_test ../eeprom/eeprom_test.c $DEV/eeprom.c || exit 2
	./$B/eeprom_test || fail=$((fail + 1))
fi
[ -f $CORE/p8x32a.c ] || { echo "p8x32a: core not present yet"; exit 2; }
$CC -I$CORE -I$DEV -o $B/p8run run.c $CORE/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
if [ -f dasm_test.c ]; then
	$CC -I$CORE -o $B/dasm_test dasm_test.c $CORE/p8x32adasm.c || exit 2
	./$B/dasm_test || fail=$((fail + 1))
fi
for t in "$OPENSPIN" "$SPINSIM" "$P1RTL"; do
	[ -x "$t" ] || { echo "missing $t: run tools.sh first"; exit 2; }
done

compile() {
	$OPENSPIN -q "$1" -o "$2.binary" > "$2.clog" 2>&1 || { echo "COMPILE FAIL $1"; cat "$2.clog"; return 1; }
}

rtl_case() {
	o=$2/$(basename "$1" .spin)
	args=$(sed -n "s/^' ARGS: //p" "$1")
	compile "$1" "$o" || { fail=$((fail + 1)); return; }
	python3 mkrom.py "$o.binary" "$o.rom" "$o.ram"
	$P1RTL -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args -dump "$o.rtlhub" > "$o.rtl"
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 -dump "$o.ourhub" > "$o.our" 2> "$o.log"
	if cmp -s "$o.rtl" "$o.our" && cmp -s "$o.rtlhub" "$o.ourhub"; then pass=$((pass + 1))
	else echo "RTL MISMATCH $1"; diff "$o.rtl" "$o.our" | head -6; fail=$((fail + 1)); fi
	exp=$(sed -n "s/^' EXPECT-LOG: //p" "$1")
	if [ -n "$exp" ] && ! grep -qF "$exp" "$o.log"; then echo "LOG MISSING $1: $exp"; fail=$((fail + 1)); fi
}

spin_case() {
	o=$2/$(basename "$1" .spin)
	compile "$1" "$o" || { fail=$((fail + 1)); return; }
	python3 mkrom.py --launch "$o.binary" "$o.lrom" "$o.lram"
	./$B/p8run -rom "$o.lrom" -ram "$o.lram" -halt -cycles 400000 -dump "$o.lhub" > /dev/null 2>&1
	rm -f "$o.sdump"
	SPINSIM_DUMP=6000,400,$o.sdump timeout 60 $SPINSIM "$o.binary" > /dev/null 2>&1
	dd if="$o.lhub" of="$o.lwin" bs=1024 skip=24 count=1 2> /dev/null
	if cmp -s "$o.sdump" "$o.lwin"; then pass=$((pass + 1))
	else echo "SPINSIM MISMATCH $1"; python3 cmpwin.py "$o.sdump" "$o.lwin"; fail=$((fail + 1)); fi
}

for f in chip/*.spin isa/*.spin; do [ -e "$f" ] && rtl_case "$f" $B/rtl; done
mkdir -p $B/rtl-q && rtl_case chip/waitext.spin $B/rtl-q "-quantum 1000"
if [ -f gen.py ]; then
	rm -rf $B/rtl/rand $B/spin/rand
	python3 gen.py --out $B/rtl/rand --count "$SEEDS" --hubflags
	for f in $B/rtl/rand/*.spin; do rtl_case "$f" $B/rtl/rand; done
	python3 gen.py --out $B/spin/rand --count "$SEEDS"
	for f in $B/spin/rand/*.spin; do spin_case "$f" $B/spin/rand; done
fi
for f in isa/*.spin; do [ -e "$f" ] && spin_case "$f" $B/spin; done

boot_case() {
	ext=$1
	python3 -c "import sys; d=open(sys.argv[1],'rb').read(); open(sys.argv[2],'wb').write(d+b'\xff'*(131072-len(d)))" "$DOMINOS_PRP" $B/boot/eeprom.bin
	key=$(cat "$P8X32A_ROM" $B/boot/eeprom.bin | sha1sum | cut -c1-16)-$ext
	ref=$B/boot/$key
	if [ ! -s "$ref.rtl" ]; then
		echo "boot: building RTL reference for ext=$ext (about 2 minutes, cached afterwards)"
		$P1RTL -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext "$ext" -cycles 60000000 -stop 0 7c01 -dump "$ref.rtlhub" > "$ref.tmp" && mv "$ref.tmp" "$ref.rtl"
	fi
	./$B/p8run -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext "$ext" -cycles 60000000 -stop 0 7c01 -dump "$ref.ourhub" > "$ref.our"
	if ! grep -q "^S [0-9]* 0 0007c01$" "$ref.our"; then echo "BOOT FAIL ext=$ext: interpreter never started"; fail=$((fail + 1))
	elif cmp -s "$ref.rtl" "$ref.our" && cmp -s "$ref.rtlhub" "$ref.ourhub"; then
		pass=$((pass + 1)); echo "boot ext=$ext: $(wc -l < "$ref.our") trace lines match, interpreter start at $(sed -n 's/^S \([0-9]*\) 0 0007c01$/\1/p' "$ref.our")"
	else echo "BOOT MISMATCH ext=$ext"; diff "$ref.rtl" "$ref.our" | head -6; fail=$((fail + 1)); fi
}

sd_case() {
	python3 -c "import sys; d=open(sys.argv[1],'rb').read(); open(sys.argv[2],'wb').write(d+b'\xff'*(131072-len(d)))" "$DOMINOS_PRP" $B/boot/eeprom.bin
	ref=$B/boot/sd-$(cat "$P8X32A_ROM" $B/boot/eeprom.bin "$DOMINOS_ZIP" | sha1sum | cut -c1-16)
	if [ ! -s "$ref.rtl" ]; then
		echo "sdboot: building RTL reference to cycle 48100000 (about 6 minutes, cached afterwards)"
		$P1RTL -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext 01000000 -cycles 48100000 -sd "$DOMINOS_ZIP" -dump "$ref.rtlhub" > "$ref.tmp" &&
			python3 nodac.py "$ref.tmp" > "$ref.rtl" && rm -f "$ref.tmp"
	fi
	./$B/p8run -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext 01000000 -cycles 48100000 -sd "$DOMINOS_ZIP" -dump "$ref.ourhub" > "$ref.raw"
	python3 nodac.py "$ref.raw" > "$ref.our"
	if cmp -s "$ref.rtl" "$ref.our" && cmp -s "$ref.rtlhub" "$ref.ourhub"; then
		pass=$((pass + 1)); echo "sdboot: $(wc -l < "$ref.our") trace lines and hub RAM match the RTL to cycle 48100000 (P14/P15 audio DUTY masked)"
	else echo "SDBOOT MISMATCH"; diff "$ref.rtl" "$ref.our" | head -6; fail=$((fail + 1)); fi
	./$B/p8run -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext 01000000 -cycles 520000000 -notrace -sd "$DOMINOS_ZIP" > $B/boot/sd5s.trace 2> $B/boot/sd5s.log
	if [ -s $B/boot/sd5s.log ]; then echo "SDBOOT LOG:"; cat $B/boot/sd5s.log; fail=$((fail + 1))
	elif python3 sdcheck.py $B/boot/sd5s.trace "$DOMINOS_ZIP"; then pass=$((pass + 1))
	else fail=$((fail + 1)); fi
}

if [ -n "$P8X32A_ROM" ] && [ -n "$DOMINOS_PRP" ]; then
	crc=$(python3 -c "import zlib,sys; print('%08x' % (zlib.crc32(open(sys.argv[1],'rb').read()) & 0xffffffff))" "$P8X32A_ROM")
	[ "$crc" = f99b3070 ] || echo "note: P8X32A_ROM crc32 $crc, expected f99b3070"
	boot_case 0
	boot_case 80000000
	if [ -n "$DOMINOS_ZIP" ]; then sd_case; else echo "sdboot: skipped (set DOMINOS_ZIP to a romset zip to run it)"; fi
else
	echo "boot: skipped (set P8X32A_ROM and DOMINOS_PRP to run it)"
fi
echo "p8x32a: $pass passed, $fail failed"
[ $fail -eq 0 ]
```

Make them executable: `chmod +x tests/pinheck/p8x32a/nodac.py tests/pinheck/p8x32a/sdcheck.py tests/pinheck/p8x32a/check.sh`

- [ ] **Step 2: Run to verify it fails**

`DOMINOS_ZIP` is the zip from Task 2 Step 5; `P8X32A_ROM` is the mask ROM from Milestone 3.
Run:
```sh
export P8X32A_ROM=$PWD/tests/pinheck/p8x32a/build/p8x32a.rom DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN DOMINOS_ZIP=$PWD/tests/pinheck/storage/build/dominos.zip
tests/pinheck/p8x32a/check.sh
```
Expected: exit 1. The existing boot cases still pass; the SD case fails because neither `p1rtl` nor `p8run` knows `-sd` yet:
```text
usage: p1rtl -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-dump f]
usage: p8run -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-halt] [-quantum n] [-extat cycle hex]... [-dump f]
SDBOOT MISMATCH
SDBOOT LOG:
p8x32a: 213 passed, 2 failed
```

- [ ] **Step 3: Attach the card to the runner and the RTL testbench**

`tests/pinheck/p8x32a/run.c`:

```c
#include "p8x32a.h"
#include "eeprom.h"
#include "sd.h"
#include "vfat.h"
#include "zipsrc.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static p8x32a chip;
static cat24m01 ee;
static uint8_t eemem[0x20000];
static int have_ee, have_sd, notrace;
static sd_card sd;
static vfat vf;
static zipsrc zs;
static uint32_t sdbit = 1;
static uint64_t cur_t;
static uint32_t ext, eebits;
static uint64_t at_t[8192];
static uint32_t at_v[8192];
static int nat;
static int stop_cog = -1;
static uint32_t stop_ptr;
static uint64_t stop_at = P8X32A_NEVER;
static uint64_t known_to = P8X32A_NEVER;
static struct ev { uint64_t t; size_t seq; char line[96]; } *evs;
static size_t nev, cap;

static void emit(uint64_t t, const char *line)
{
	if (nev == cap) {
		cap = cap ? cap * 2 : 4096;
		evs = realloc(evs, cap * sizeof(*evs));
		if (!evs) { fprintf(stderr, "p8run: out of memory\n"); exit(2); }
	}
	evs[nev].t = t;
	evs[nev].seq = nev;
	strcpy(evs[nev].line, line);
	nev++;
}

static uint32_t pins_in(void *ctx, uint64_t t)
{
	uint32_t v = ext;
	int k;
	(void)ctx;
	for (k = 0; k < nat && at_t[k] <= t; k++) v = at_v[k];
	if (have_sd) v = (v & ~1u) | sdbit;
	return have_ee ? (v & ~0x30000000u) | eebits : v;
}

static uint64_t pins_next(void *ctx, uint64_t t)
{
	int k;
	(void)ctx;
	for (k = 0; k < nat; k++)
		if (at_t[k] > t) return at_t[k] <= known_to ? at_t[k] : P8X32A_NEVER;
	return P8X32A_NEVER;
}

static int rank(const struct ev *e) { return e->line[0] == 'P' ? 0 : e->line[0] == 'K' ? 1 : 2; }

static int evcmp(const void *a, const void *b)
{
	const struct ev *x = a, *y = b;
	if (x->t != y->t) return x->t < y->t ? -1 : 1;
	if (rank(x) != rank(y)) return rank(x) - rank(y);
	return x->seq < y->seq ? -1 : 1;
}

static void pins_out(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx;
	if (have_ee) {
		int scl = (dir >> 28 & 1) ? (int)(out >> 28 & 1) : 1;
		int sda = (dir >> 29 & 1) ? (int)(out >> 29 & 1) : 1;
		int drv = cat24m01_update(&ee, scl, sda);
		eebits = 0x10000000u | (uint32_t)drv << 29;
	}
	cur_t = t;
	if (have_sd) {
		int cs = (dir >> 3 & 1) ? (int)(out >> 3 & 1) : 1;
		int sclk = (dir >> 1 & 1) ? (int)(out >> 1 & 1) : 0;
		int mosi = (dir >> 2 & 1) ? (int)(out >> 2 & 1) : 1;
		sdbit = (uint32_t)sd_update(&sd, cs, sclk, mosi);
	}
	if (notrace) return;
	{
		char b[48];
		sprintf(b, "P %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
		emit(t, b);
	}
}

static void node_path(int n, char *out)
{
	char part[13];
	int k, j;
	out[0] = 0;
	if (n <= 0) return;
	node_path(vf.node[n].parent, out);
	for (k = 0, j = 0; k < 8 && vf.node[n].name[k] != ' '; k++) part[j++] = vf.node[n].name[k];
	if (vf.node[n].name[8] != ' ') {
		part[j++] = '.';
		for (k = 8; k < 11 && vf.node[n].name[k] != ' '; k++) part[j++] = vf.node[n].name[k];
	}
	part[j] = 0;
	if (out[0]) strcat(out, "/");
	strcat(out, part);
}

static void sector_label(uint32_t lba, char *out)
{
	uint32_t r;
	if (lba < vf.part_start) { strcpy(out, lba ? "GAP" : "MBR"); return; }
	r = lba - vf.part_start;
	if (r < 32) { strcpy(out, r == 0 || r == 6 ? "BOOT" : r == 1 || r == 7 ? "FSINFO" : "RESERVED"); return; }
	if (r < vf.data_start) { sprintf(out, "FAT+%u", (unsigned)(r - 32)); return; }
	{
		uint32_t cl = 2 + (r - vf.data_start) / VFAT_SPC;
		int k;
		for (k = 0; k < vf.nalloc; k++) {
			vfat_node *n = &vf.node[vf.alloc[k]];
			if (cl >= n->first && cl < n->first + n->nclus) {
				char path[64];
				node_path(vf.alloc[k], path);
				sprintf(out, "%s:%s+%u", n->is_dir ? "DIR" : "FILE", path[0] ? path : "/",
				        (unsigned)(((cl - n->first) * VFAT_SPC + (r - vf.data_start) % VFAT_SPC) * 512));
				return;
			}
		}
	}
	strcpy(out, "FREE");
}

static int sd_read(void *ctx, uint32_t lba, uint8_t *buf)
{
	char line[96], label[72];
	int r;
	(void)ctx;
	r = vfat_read(&vf, lba, buf);
	sector_label(lba, label);
	sprintf(line, "D %llu %u %08lx %s", (unsigned long long)cur_t, (unsigned)lba, crc32(0L, buf, 512), label);
	emit(cur_t, line);
	return r;
}

static void cog_start(void *ctx, uint64_t t, int cog, uint32_t ptr)
{
	(void)ctx;
	char b[48];
	sprintf(b, "S %llu %d %07x", (unsigned long long)t, cog, (unsigned)ptr);
	emit(t, b);
	if (cog == stop_cog && ptr == stop_ptr) { stop_at = t; chip.stop = 1; }
}

static void clkset(void *ctx, uint64_t t, uint8_t cfg)
{
	(void)ctx;
	char b[48];
	sprintf(b, "K %llu %02x", (unsigned long long)t, cfg);
	emit(t, b);
}

static void logmsg(void *ctx, const char *msg)
{
	(void)ctx;
	fprintf(stderr, "%s\n", msg);
}

static int load(const char *path, uint8_t *dst, size_t max, size_t *got)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return 0; }
	*got = fread(dst, 1, max, f);
	fclose(f);
	return 1;
}

static uint64_t halted(void)
{
	uint64_t last = 0;
	int n;
	for (n = 0; n < 8; n++) {
		const p8x32a_cog *c = &chip.cog[n];
		if (c->ev != 0) return P8X32A_NEVER;
		if (c->disable_at != P8X32A_NEVER && c->disable_at > last) last = c->disable_at;
	}
	return last;
}

int main(int argc, char **argv)
{
	p8x32a_bus bus = { NULL, pins_in, pins_next, pins_out, cog_start, clkset, logmsg };
	const char *rom = NULL, *ram = NULL, *eep = NULL, *dump = NULL;
	unsigned long long limit = 1000000, t, end, quantum = 4096;
	int halt = 0, i;
	size_t n;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-rom") && i + 1 < argc) rom = argv[++i];
		else if (!strcmp(argv[i], "-ram") && i + 1 < argc) ram = argv[++i];
		else if (!strcmp(argv[i], "-eeprom") && i + 1 < argc) eep = argv[++i];
		else if (!strcmp(argv[i], "-dump") && i + 1 < argc) dump = argv[++i];
		else if (!strcmp(argv[i], "-cycles") && i + 1 < argc) limit = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-ext") && i + 1 < argc) ext = (uint32_t)strtoul(argv[++i], NULL, 16);
		else if (!strcmp(argv[i], "-stop") && i + 2 < argc) { stop_cog = atoi(argv[i + 1]); stop_ptr = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else if (!strcmp(argv[i], "-halt")) halt = 1;
		else if (!strcmp(argv[i], "-notrace")) notrace = 1;
		else if (!strcmp(argv[i], "-uart") && i + 2 < argc) {
			uint64_t t0 = strtoull(argv[i + 1], NULL, 0);
			size_t nb = strlen(argv[i + 2]) / 2, k;
			int b;
			for (k = 0; k < nb && nat + 11 <= 8192; k++) {
				char hx[3];
				uint32_t byte;
				hx[0] = argv[i + 2][2 * k]; hx[1] = argv[i + 2][2 * k + 1]; hx[2] = 0;
				byte = (uint32_t)strtoul(hx, NULL, 16) | 0x100u;
				at_t[nat] = t0; at_v[nat++] = ext & ~(1u << 24);
				for (b = 0; b < 9; b++) { at_t[nat] = t0 + 903 * (uint64_t)(b + 1); at_v[nat++] = (ext & ~(1u << 24)) | ((byte >> b) & 1) << 24; }
				t0 += 903 * 11;
			}
			i += 2;
		}
		else if (!strcmp(argv[i], "-sd") && i + 1 < argc) {
			sd_blockdev dev;
			if (zipsrc_open(&zs, argv[++i], 64u << 20) || vfat_init(&vf, zipsrc_source(&zs))) { fprintf(stderr, "p8run: cannot open romset %s\n", argv[i]); return 2; }
			dev.ctx = NULL;
			dev.sectors = vfat_sectors(&vf);
			dev.read = sd_read;
			sd_init(&sd, &dev);
			have_sd = 1;
		}
		else if (!strcmp(argv[i], "-quantum") && i + 1 < argc) quantum = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-extat") && i + 2 < argc && nat < 8192) { at_t[nat] = strtoull(argv[i + 1], NULL, 0); at_v[nat++] = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else { fprintf(stderr, "usage: p8run -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-halt] [-quantum n] [-sd romset.zip] [-notrace] [-uart cycle hexbytes] [-extat cycle hex]... [-dump f]\n"); return 2; }
	}
	if (!rom) { fprintf(stderr, "p8run: -rom is required\n"); return 2; }
	p8x32a_init(&chip, &bus);
	if (!load(rom, chip.hub + 0x8000, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "p8run: rom must be 32768 bytes\n"); return 2; }
	if (ram && !load(ram, chip.hub, 0x8000, &n)) return 2;
	if (eep) {
		memset(eemem, 0xFF, sizeof(eemem));
		if (!load(eep, eemem, sizeof(eemem), &n)) return 2;
		cat24m01_init(&ee, eemem, 0);
		have_ee = 1;
		eebits = 0x30000000u;
	}
	printf("P 0 00000000 00000000\nK 0 00\n");
	end = limit;
	for (t = 0; t < limit; t += quantum) {
		unsigned long long to = t + quantum - 1 < limit - 1 ? t + quantum - 1 : limit - 1;
		uint64_t h;
		if (quantum != 4096) known_to = to;
		p8x32a_run_until(&chip, to);
		if (chip.stop) { end = stop_at + 1; break; }
		if (halt && (h = halted()) != P8X32A_NEVER && h >= 2) { end = h; break; }
	}
	{
		size_t k;
		qsort(evs, nev, sizeof(*evs), evcmp);
		for (k = 0; k < nev; k++)
			if (evs[k].t < end) printf("%s\n", evs[k].line);
	}
	printf("E %llu\n", end);
	if (dump) {
		FILE *f = fopen(dump, "wb");
		if (!f) { perror(dump); return 2; }
		fwrite(chip.hub, 1, 0x8000, f);
		fclose(f);
	}
	return 0;
}
```

`tests/pinheck/p8x32a/rtl/tb.cpp`:

```cpp
#include "Vdig.h"
#include "Vdig___024root.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "eeprom.h"
#include "sd.h"
#include "vfat.h"
#include "zipsrc.h"

static uint8_t eemem[0x20000];

static bool load(const char *path, uint8_t *dst, size_t max, size_t *got)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return false; }
	*got = fread(dst, 1, max, f);
	fclose(f);
	return true;
}

int main(int argc, char **argv)
{
	const char *rom = NULL, *ram = NULL, *eep = NULL, *dump = NULL;
	unsigned long long limit = 1000000;
	uint32_t ext = 0;
	const char *sdzip = NULL;
	unsigned long long at_t[64];
	uint32_t at_v[64];
	int nat = 0, ati = 0;
	int stop_cog = -1, halt = 0;
	uint32_t stop_ptr = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-rom") && i + 1 < argc) rom = argv[++i];
		else if (!strcmp(argv[i], "-ram") && i + 1 < argc) ram = argv[++i];
		else if (!strcmp(argv[i], "-eeprom") && i + 1 < argc) eep = argv[++i];
		else if (!strcmp(argv[i], "-dump") && i + 1 < argc) dump = argv[++i];
		else if (!strcmp(argv[i], "-cycles") && i + 1 < argc) limit = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-ext") && i + 1 < argc) ext = (uint32_t)strtoul(argv[++i], NULL, 16);
		else if (!strcmp(argv[i], "-halt")) halt = 1;
		else if (!strcmp(argv[i], "-sd") && i + 1 < argc) sdzip = argv[++i];
		else if (!strcmp(argv[i], "-extat") && i + 2 < argc && nat < 64) { at_t[nat] = strtoull(argv[i + 1], NULL, 0); at_v[nat++] = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else if (!strcmp(argv[i], "-stop") && i + 2 < argc) { stop_cog = atoi(argv[++i]); stop_ptr = (uint32_t)strtoul(argv[++i], NULL, 16); }
		else { fprintf(stderr, "usage: p1rtl -rom f [-ram f] [-eeprom f] [-ext hex] [-sd romset.zip] [-cycles n] [-stop cog ptrhex] [-dump f]\n"); return 2; }
	}
	if (!rom) { fprintf(stderr, "p1rtl: -rom is required\n"); return 2; }

	Vdig *top = new Vdig;
	Vdig___024root *r = top->rootp;
	static uint8_t buf[32768];
	size_t n;
	if (!load(rom, buf, 32768, &n) || n != 32768) { fprintf(stderr, "p1rtl: rom must be 32768 bytes\n"); return 2; }
	for (int w = 0; w < 4096; w++) {
		r->dig__DOT__hub___DOT__hub_mem___DOT__rom_low[w] = buf[w * 4] | buf[w * 4 + 1] << 8 | buf[w * 4 + 2] << 16 | (uint32_t)buf[w * 4 + 3] << 24;
		int h = 16384 + w * 4;
		r->dig__DOT__hub___DOT__hub_mem___DOT__rom_high[w] = buf[h] | buf[h + 1] << 8 | buf[h + 2] << 16 | (uint32_t)buf[h + 3] << 24;
	}
	memset(buf, 0, sizeof(buf));
	if (ram && !load(ram, buf, 32768, &n)) return 2;
	for (int w = 0; w < 8192; w++) {
		r->dig__DOT__hub___DOT__hub_mem___DOT__ram0[w] = buf[w * 4];
		r->dig__DOT__hub___DOT__hub_mem___DOT__ram1[w] = buf[w * 4 + 1];
		r->dig__DOT__hub___DOT__hub_mem___DOT__ram2[w] = buf[w * 4 + 2];
		r->dig__DOT__hub___DOT__hub_mem___DOT__ram3[w] = buf[w * 4 + 3];
	}
	cat24m01 ee;
	if (eep) {
		memset(eemem, 0xFF, sizeof(eemem));
		if (!load(eep, eemem, sizeof(eemem), &n)) return 2;
		cat24m01_init(&ee, eemem, 0);
	}

	static zipsrc zs;
	static vfat vf;
	static sd_card sd;
	int sdbit = 1;
	if (sdzip) {
		sd_blockdev dev;
		if (zipsrc_open(&zs, sdzip, 64u << 20) || vfat_init(&vf, zipsrc_source(&zs))) { fprintf(stderr, "p1rtl: cannot open romset %s\n", sdzip); return 2; }
		dev.ctx = &vf;
		dev.sectors = vfat_sectors(&vf);
		dev.read = [](void *ctx, uint32_t lba, uint8_t *b) { return vfat_read((vfat *)ctx, lba, b); };
		sd_init(&sd, &dev);
	}
	top->nres = 0;
	top->pin_in = 0;
	for (int i = 0; i < 4; i++) {
		top->clk_cog = 1; top->clk_pll = 1; top->eval();
		top->clk_pll = 0; top->eval();
		top->clk_cog = 0; top->clk_pll = 1; top->eval();
		top->clk_pll = 0; top->eval();
	}
	top->nres = 1;
	top->eval();

	uint32_t last_out = ~0u, last_dir = ~0u;
	int last_cfg = -1;
	unsigned long long cyc;
	for (cyc = 0; cyc < limit; cyc++) {
		uint32_t out = top->pin_out, dir = top->pin_dir;
		while (ati < nat && at_t[ati] <= cyc) ext = at_v[ati++];
		uint32_t x = ext;
		if (eep) {
			int scl = (dir >> 28 & 1) ? (out >> 28 & 1) : 1;
			int sda = (dir >> 29 & 1) ? (out >> 29 & 1) : 1;
			int drv = cat24m01_update(&ee, scl, sda);
			x = (x & ~0x30000000u) | 0x10000000u | (uint32_t)drv << 29;
		}
		if (sdzip) {
			int cs = (dir >> 3 & 1) ? (out >> 3 & 1) : 1;
			int sclk = (dir >> 1 & 1) ? (out >> 1 & 1) : 0;
			int mosi = (dir >> 2 & 1) ? (out >> 2 & 1) : 1;
			sdbit = sd_update(&sd, cs, sclk, mosi);
			x = (x & ~1u) | (uint32_t)sdbit;
		}
		top->pin_in = (dir & out) | (~dir & x);
		if (out != last_out || dir != last_dir) { printf("P %llu %08x %08x\n", cyc, out, dir); last_out = out; last_dir = dir; }
		if (top->cfg != last_cfg) { printf("K %llu %02x\n", cyc, top->cfg); last_cfg = top->cfg; }
		top->eval();
		int stopping = 0;
		if (r->dig__DOT__ena_bus && r->dig__DOT__ptr_w) {
			uint32_t ptr = r->dig__DOT__hub___DOT__dc >> 4;
			for (int c = 0; c < 8; c++)
				if (r->dig__DOT__ptr_w >> c & 1) {
					printf("S %llu %d %07x\n", cyc, c, ptr);
					if (c == stop_cog && ptr == stop_ptr) stopping = 1;
				}
		}
		top->clk_cog = 1; top->clk_pll = 1; top->eval();
		top->clk_pll = 0; top->eval();
		top->clk_cog = 0; top->clk_pll = 1; top->eval();
		top->clk_pll = 0; top->eval();
		if (stopping) { cyc++; break; }
		if (halt && cyc >= 1 && r->dig__DOT__cog_ena == 0) { cyc++; break; }
	}
	printf("E %llu\n", cyc);
	if (dump) {
		FILE *f = fopen(dump, "wb");
		if (!f) { perror(dump); return 2; }
		for (int w = 0; w < 8192; w++) {
			fputc(r->dig__DOT__hub___DOT__hub_mem___DOT__ram0[w], f);
			fputc(r->dig__DOT__hub___DOT__hub_mem___DOT__ram1[w], f);
			fputc(r->dig__DOT__hub___DOT__hub_mem___DOT__ram2[w], f);
			fputc(r->dig__DOT__hub___DOT__hub_mem___DOT__ram3[w], f);
		}
		fclose(f);
	}
	delete top;
	return 0;
}
```

`tests/pinheck/p8x32a/tools.sh`:

```sh
#!/bin/sh
cd "$(dirname "$0")" || exit 2
TOOLS=${TOOLS:-$PWD/build/tools}
HERE=$PWD
mkdir -p "$TOOLS"

fetch() {
	[ -d "$TOOLS/$1/.git" ] || git clone -q "$2" "$TOOLS/$1" || exit 2
	git -C "$TOOLS/$1" checkout -q "$3" || exit 2
}

fetch openspin https://github.com/parallaxinc/OpenSpin.git d1991aa821179209cbd21396dfd1d88f226eb99d
fetch spinsim https://github.com/parallaxinc/spinsim.git 77f7331974de88e18c69a90f450feb70a979979c
fetch p1 https://github.com/parallaxinc/Propeller_1_Design.git 499b248c62bf09191ff11ba5f762fd68d13151fd

make -C "$TOOLS/openspin" > "$TOOLS/openspin.log" 2>&1 || { echo "openspin build failed, see $TOOLS/openspin.log"; exit 2; }
grep -q SPINSIM_DUMP "$TOOLS/spinsim/spinsim.c" || (cd "$TOOLS/spinsim" && sed -i 's/\r$//' spinsim.c && patch -s spinsim.c < "$HERE/spinsim-dump.patch") || exit 2
make -C "$TOOLS/spinsim" > "$TOOLS/spinsim.log" 2>&1 || { echo "spinsim build failed, see $TOOLS/spinsim.log"; exit 2; }
R=$TOOLS/p1/P8X32A_DE2_115
D=$HERE/../../../src/wpc/pinheck
[ -f "$HERE/rtl/tb.cpp" ] && [ -f "$D/eeprom.c" ] || { echo "tools ready in $TOOLS (p1rtl needs rtl/tb.cpp and eeprom.c)"; exit 0; }
rm -rf "$TOOLS/p1rtl"
verilator --cc --exe --build -j 8 -O3 -Wno-fatal --x-initial 0 --x-assign 0 --public-flat-rw -I"$R" \
	-CFLAGS "-O2 -I$D" -LDFLAGS -lz "$R/dig.v" "$HERE/rtl/tb.cpp" "$D/eeprom.c" "$D/sd.c" "$D/vfat.c" "$D/zipsrc.c" --Mdir "$TOOLS/p1rtl" -o p1rtl \
	> "$TOOLS/p1rtl.log" 2>&1 || { echo "p1rtl build failed, see $TOOLS/p1rtl.log"; exit 2; }
echo "tools ready in $TOOLS"
```

`tests/pinheck/p8x32a/README.md`:

```markdown
# p8x32a core tests

`./tools.sh` builds the three external oracles at pinned commits into `build/tools` (needs `git`, `make`, `g++`, `verilator`, `patch`). `./check.sh` then runs everything; `SEEDS=N` changes the random corpus size (default 100 per oracle).

Oracles are test tools only and are never linked into PinMAME:

- **p1rtl**: Parallax's P8X32A RTL (GPL 3.0, github.com/parallaxinc/Propeller_1_Design) under Verilator, driven by `rtl/tb.cpp`. It is the arbiter: every `chip/`, `isa/` and random program must produce a byte-identical trace (`P` pin out/dir, `K` CLKSET, `S` cog start, `E` end, each at the cycle it first becomes visible) and a byte-identical 32 KB hub RAM dump.
- **spinsim** (MIT, patched by `spinsim-dump.patch` to dump a hub window at exit): instruction semantics only, compared on the `$6000-$63FF` result window. It is not cycle-accurate and does not model the hub-op flag quirks, so its random corpus omits flags on hub ops.
- **openspin** (MIT) assembles the test programs.

Test programs are Spin files whose PASM sits between the marker longs `$C0DE5EED` and `$C0DEE0D0`. `mkrom.py` scrambles that block into a ROM image at `$F800`, where cog 0 loads from at reset, so RTL tests run without the Parallax mask ROM. A block after `$C0DE0B0B` is a worker whose hub address replaces the placeholder `$C0DEADD1`. `mkrom.py --launch` instead builds a launcher that starts the block in cog 1, which is how spinsim's `cognew` runs it. A first line `' ARGS: ...` adds runner arguments (for example `-extat cycle hex` external pin changes); `' EXPECT-LOG: ...` requires a core log line.

The boot test needs the mask ROM (GPL 3.0, never committed) and the game's Propeller image: set `P8X32A_ROM` to the 32 KB silicon image (crc32 `f99b3070`) and `DOMINOS_PRP` to `PRP_V008.BIN`. It boots through the real booter and EEPROM model with P31 low and with P31 high (host-detect timeout path) until cog 0 is restarted with the Spin interpreter, and compares against a cached RTL run (about two minutes each the first time).

The SD boot test also needs `DOMINOS_ZIP`, a romset zip with `DOM_V006.PRG`, `PRP_V008.BIN`, `DMD/` and `SFX/`. `run.c` and `rtl/tb.cpp` then attach the same SD card model (`sd.c` over `vfat.c` over `zipsrc.c`) on P0 (DO), P1 (SCLK), P2 (DI) and P3 (CS), with P24 held high as an idle UART line. The test compares both runs to cycle 48,100,000 (through booting, SD init and mounting), ignoring only P14/P15, whose DUTY bitstream the core deliberately does not drive (spec §5.4). `nodac.py` masks those pins. The RTL reference takes about 6 minutes the first time. The test then runs 5 emulated seconds, and `sdcheck.py` requires the exact boot access sequence ending in `DMD/_DZ/ZMB.VID` frame 0, with every file sector served matching the zip entry. `run.c -sd` logs each sector read as `D cycle lba crc32 label`. `run.c -uart cycle hexbytes` sends 115200-baud 8N1 frames on P24, the line the firmware's serial driver listens on.
```

Rebuild the RTL oracle with the card: `tests/pinheck/p8x32a/tools.sh`
Expected: `tools ready in …/build/tools`.

- [ ] **Step 4: Run to verify it passes**

Run: `tests/pinheck/p8x32a/check.sh` (with the three variables from Step 2; the RTL reference takes about 6 minutes the first time)
Expected:
```text
eeprom: ok
dasm: 31/31
boot ext=0: 655460 trace lines match, interpreter start at 16139093
boot ext=80000000: 655460 trace lines match, interpreter start at 19139109
sdboot: 804329 trace lines and hub RAM match the RTL to cycle 48100000 (P14/P15 audio DUTY masked)
sd: boot mounted the card and read DMD/_DZ/ZMB.VID frame 0 (18 sectors, file sectors match the zip)
p8x32a: 215 passed, 0 failed
```
This is Milestone 4's exit criterion, and the 5-second run behind the `sd:` line logs nothing, so no unmodelled counter mode is in use.

- [ ] **Step 5: Commit**

```bash
git add tests/pinheck/p8x32a/nodac.py tests/pinheck/p8x32a/sdcheck.py tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/run.c tests/pinheck/p8x32a/rtl/tb.cpp tests/pinheck/p8x32a/tools.sh tests/pinheck/p8x32a/README.md
git commit -m "p8x32a: boot PRP_V008.BIN with the SD card, cycle-exact against the RTL"
```

### Task 6: Record the findings; prove the tests bite

**Files:**
- Modify: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§4.3, §4.4)

**Interfaces:**
- Consumes: Tasks 1–5.
- Produces: the spec text Plans 5–7 read.

- [ ] **Step 1: Update the spec**

Run:
```sh
python3 - docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
for a, b in (
    ('- Counters: all modes the firmware uses, modelled functionally.',
     "- Counters: NCO single and differential outputs drive pins cycle-exactly. The firmware uses NCO on P1/P2 (SD clock and data), P21/P22 (display) and P25 (serial TX), mostly with `FRQ` = 0 and data shifted through `PHS`. DUTY modes (the firmware's audio DAC on P14/P15) are not driven onto pins: the audio device integrates `FRQ` instead (§5.4). PLL pin outputs and pin-sensing modes are reported once, not modelled."),
    ('with high-capacity addressing. It is read-only: writes are accepted and discarded, and logged.',
     "with high-capacity addressing when the host negotiates it and byte addressing otherwise. The card shifts its output only after it has sampled a bit, because the firmware's NCO clock can be high when chip-select asserts. It is read-only: writes are accepted, discarded and counted."),
    ("- **`vfat.c`** builds a FAT32 volume from the romset zip's `DMD/` and `SFX/` entries: MBR, boot sector, FSInfo, two FATs, root and subdirectories with 8.3 names stored uppercase, and one contiguous cluster run per file. Data-sector reads map to (entry, offset). Stored entries are read by range; deflated entries are inflated whole into a bounded LRU cache, and an entry larger than the cache is held only while in use.",
     "- **`vfat.c`** builds a FAT32 volume from the romset zip's `DMD/` and `SFX/` entries: an MBR partition at LBA 8192, boot sector, FSInfo, two FATs, root and subdirectories with 8.3 names stored uppercase, and one contiguous cluster run per file. Zip directory entries (for example empty folders) become empty directories. Clusters are 32 KB (64 sectors), because the firmware reads the reserved-sector and FAT-size fields from the boot sector but assumes 64 sectors per cluster. The volume has at least 65,536 clusters, as FAT32 requires, so it is padded with free space to about 2 GB. Data-sector reads map to (entry, offset).\n- **`zipsrc.c`** reads the romset zip. Stored entries are read by range. Deflated entries are inflated whole into a bounded LRU cache, and an entry larger than the cache is held only until another entry is read."),
):
    assert a in s, a[:60]
    s = s.replace(a, b, 1)
open(p, 'w').write(s)
EOF
git diff --stat docs/superpowers/specs
```
Expected: `1 file changed`.

- [ ] **Step 2: Prove the Review Focus tests can fail**

For each row, apply the replacement in the named file. Run the command, confirm the named failure, then restore with `git checkout <file>`:

| File | Replace → with | Run | Must fail |
|---|---|---|---|
| `src/wpc/pinheck/sd.c` | `} else if (!sclk && s->sclk && s->risen) {` → `} else if (!sclk && s->sclk) {` | `tests/pinheck/storage/check.sh` | `SD FAIL sd_test.c:143` |
| `src/cpu/p8x32a/p8x32a.c` | `return o < c->ctr_at[k] ? o : c->ctr_at[k];` → `(void)o; return c->ctr_at[k];` (the `(void)o;` keeps `-Werror` from rejecting the mutant) | `env -u P8X32A_ROM SEEDS=2 tests/pinheck/p8x32a/check.sh` | `RTL MISMATCH chip/nco.spin` |
| `src/cpu/p8x32a/p8x32a.c` | `	if (ctr_next(p, t) < nt) nt = ctr_next(p, t);` → (delete the line) | `env -u P8X32A_ROM SEEDS=2 tests/pinheck/p8x32a/check.sh` | `RTL MISMATCH chip/nco.spin` |
| `src/wpc/pinheck/vfat.h` | `#define VFAT_SPC 64u` → `#define VFAT_SPC 8u` | `tests/pinheck/storage/check.sh` | `VFAT FAIL vfat_test.c:67` |
| `src/wpc/pinheck/zipsrc.c` | `if (!nl) { free(name); continue; }` → `if (!nl \|\| name[nl - 1] == '/') { free(name); continue; }` | `tests/pinheck/storage/check.sh` | `ZIPSRC FAIL zipsrc_test.c:37` |
| `src/wpc/pinheck/zipsrc.c` | `	evict(z, e->usize, i);` → `	if (e->usize > z->cache_bytes) return -1;` + newline + `	evict(z, e->usize, i);` | `tests/pinheck/storage/check.sh` | `ZIPSRC FAIL zipsrc_test.c:50` |

Then `git status --short src/` must print nothing.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md
git commit -m "Spec §4.3/§4.4: counter outputs, 32 KB clusters, SD shift rule, zipsrc"
```
