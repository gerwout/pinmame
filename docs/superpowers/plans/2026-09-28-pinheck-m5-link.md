# Milestone 5: PIC32 ↔ Propeller link in PinMAME Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Both CPUs run together inside PinMAME: the PIC32 firmware passes `PROPELLER SYNC CHECK` against the emulated Propeller on every boot, answers commands on its UART1 console, and keeps its settings across runs.

**Architecture:** `src/wpc/pinheck/prop.c` wraps the `p8x32a` core as a device the driver advances: PIC32 writes to P25/P26 go into a timestamped edge log, the Propeller is caught up before every PIC32 read of RF13 and on a 1 kHz timer, and PIC32 cycles map to Propeller cycles through a piecewise clock map that follows the Propeller's `CLKSET`. The driver wires U13 and a new DS1340 model onto I2C1, the Propeller's CAT24M01 onto P28/P29 and Plan 4's SD card onto P0–P3, persists both EEPROMs as NVRAM, and declares the Propeller mask ROM as a `pinheck` parent set.

**Tech Stack:** C99 (C89-syntax-clean for `prop.c`/`rtc.c`), PinMAME 0.37 driver interfaces, CMake/makefiles/VS projects, OpenSpin (external test tool), Python 3, POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§2.3, §4.4–§4.6, §5.1–§5.3, §7 check 2, §10). Roadmap: `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`.

## Prerequisites

- **Plan 4 executed** (`2026-09-28-pinheck-m4-sd-vfat.md`, all six tasks). This plan uses, unchanged: `src/wpc/pinheck/sd.[ch]`, `vfat.[ch]`, `zipsrc.[ch]`, Plan 4's NCO counter outputs in `src/cpu/p8x32a/p8x32a.[ch]` (the firmware's SD clock and its serial TX), and Plan 4's spec edits (Task 5 here edits the spec after them). `tests/pinheck/link/check.sh` stops with `PLAN 4 MISSING` otherwise.
- **Decision D1 taken** (see below). Tasks 3 and 4 implement option (c); Tasks 1, 2 and 5 do not depend on D1.
- The mask ROM `p8x32a.rom` (CRC32 `f99b3070`, GPL 3.0, never committed) and the unzipped Domino's update. The commands below use:

```sh
export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip
```

`PINHECK_ZIP` is optional: a Domino's update zip (stored or deflated). Without it the tests build a stored one from `PINHECK_UPDATE_DIR` once (about 800 MB, under the ignored `build/`).

## Decision D1: the PIC32 bootloader handshake

Established by emulating `PRP_V008.BIN` with the card attached, reading its Spin bytecode (spinsim's disassembler) and its framebuffer:

- **What the Propeller does.** After mounting the card it reads EEPROM long `$8000`. If the low 16 bits are not `$BAFA`, or P13 (`PROP_CONFIG`, pulled up) reads low, or long `$8004` is `$ABBA0001` (the PIC32's `UPDATE CODE` menu writes it), it shows `FLASHING TO:` and starts a full-duplex serial driver (cog 7) with RX on P24 (COMM_IN_TX = PIC32 RF13/U3BTX) and TX on P25 (COMM_CLK_RX = PIC32 RF12/U3BRX) at 115,200 baud (900 Propeller cycles per bit). It sends one STK500v2 `CMD_SIGN_ON` (`1B 00 00 01 0E 01 15`) and **waits for a reply without any timeout** (checked for 60 emulated seconds). A reply with status 0 and a signature starting `ST` (`STK500_2`) gets `CONNECT PIC32:OK` and `UPDATE IN 5 SECONDS`, after which it reprograms the PIC32 flash from the card's `.PRG` (`PROGRAM FLASH`/`VERIFY FLASH`, sector by sector), writes `$8004 = $ABBA0002` and `$8000 = (v << 24) | $BAFA` (v = digits after `_V`, 6 for `DOM_V006.PRG`), and shows `PLEASE RESTART`; it never serves the game link in that power cycle. The answer `AVRISP_2` gives `CONNECT PIC32:FAIL`. It is not a version compare: when the record is missing it always reprograms.
- **Reset line.** The Propeller drives neither MCLR (a reset button network) nor RG9 (`PIC_PROGRAM_SW`). On the real board both chips leave power-on reset together and the PIC32 bootloader is still listening when the sign-on arrives about 0.5 s later.
- **With the record present** (a machine that has run its update once) the Propeller skips all of this, leaves P24/P25 to the link, and the PIC32 prints `PROPELLER SYNC CHECK......OK`.
- **The open-source bootloader does not fit.** `Bootloader_Max32.hex` and the boot half of `PIC32_Combined_Code.hex` in LonghornEngineer/Pinheck_Pinball_System are "Explorer stk500V2 by MLS V1.0" on **UART1** (the USB-serial pins), not on RF12/RF13. Licence: CC BY-NC-SA 4.0, which is not compatible with PinMAME's BSD-3-Clause, so it could only be a user-supplied ROM. Spooky's production bootloader (on U3B) is not available.

Options, with cost:

| | Option | Cost | Consequence |
|---|---|---|---|
| (a) | LLE: real bootloader in boot flash | needs a dump of Spooky's U3B bootloader (not available; no hardware access); plus NVM controller, U3B UART, reset-to-`0xBFC00000` | exact, including `UPDATE CODE` |
| (b) | HLE: minimal STK500v2 responder standing in for the bootloader | about 150 lines plus tests: sign-on, `LOAD_ADDRESS`, `PROGRAM_FLASH`/`READ_FLASH` against the emulated flash, then start the application | first run shows the real update screens and reprograms the emulated flash (a few emulated seconds), then asks for a restart; `UPDATE CODE` works |
| (c) | **Start in-service**: blank NVRAM initialised with the record `$8000 = (v << 24) | $BAFA`, `$8004 = $ABBA0002` (v from the game definition) | 10 lines in the driver | boots straight to the game like an installed machine; `UPDATE CODE` then hangs on the missing bootloader (spec §10 already puts the update path out of scope) |

This plan implements **(c)**, recommended because it matches spec §10 and costs nothing; (b) can be added later without changing anything here. **If D1 is decided differently, do not execute Tasks 3 and 4 as written.**

## Global Constraints

- `src/wpc/pinheck/prop.c` and `rtc.c` include only standard C headers and their own/core headers (no PinMAME headers) and pass `cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only`.
- Propeller clock map: RCFAST = 12 MHz nominal (3/20 of PIC32 cycles), RCSLOW 20 kHz (1/4000), XINPUT/XTAL×1–×16 from the 6.5 MHz crystal; `CLKSET $6F` (PLL16) is exactly 13/10. A `CLKSET` with bit 7 restarts the Propeller.
- Propeller pin inputs: P25/P26 from the PIC32 (RF12/RF5 driven bits), P0 from the SD card, P28/P29 from its CAT24M01; SD `DO`, `CS`, P13 and SCL/SDA read high when undriven; P31 reads 0.
- Link timing rule: a PIC32 pin write logged at PIC32 cycle P is seen by the Propeller at ⌊map(P)⌋; catching up for PIC32 cycle R advances the Propeller to map(R) − 1.
- The `pinheck` set is `NOT_A_DRIVER` with `p8x32a.rom` (CRC32 `f99b3070`, SHA1 `b7b4fdf4f096db7d18bda6355725cb42ae4a9378`); `dominos` is its clone with `gameSpecific1` = 6 (its code version).
- NVRAM: U13 (131,072 bytes) then the Propeller EEPROM from `$8000` (98,304 bytes); DS1340 seeded from host local time at every reset, not persisted.
- Test-only environment hooks in the driver: `PINHECK_UART1_LOG`, `PINHECK_PROP_LOG`, `PINHECK_UART1_SEND` / `PINHECK_UART1_SEND_AT` (seconds), `PINHECK_RESET_AT` (seconds, once per process).
- PinMAME is built into the ignored `build/sdl3pinmame`, never into `build-dbg`.
- Short code comments only.

## Review Focus

- **PIC32 edges logged before the Propeller runs, across a `CLKSET`** (the firmware bit-bangs whole packets inside one slice while the Propeller is still on RCFAST): each edge must land on the same Propeller cycle whether the Propeller is caught up per edge or once. `echo_exact` and `clkset_lazy` in Task 1.
- **The edge log fills** (thousands of bit-bang edges between catch-ups): no edge may be dropped. `full_log` in Task 1.
- **A PinMAME reset** (PIC32 cycles restart at 0, Propeller time must not go backwards) and a Propeller `CLKSET` reset: `reset_rebases(1)`, `reset_rebases(0)`, `clkset_reset_bit` in Task 1; `PINHECK_RESET_AT` in `pinmame_check.sh` (Task 4).
- **First run on blank NVRAM** (D1): without the in-service record the sync check never passes. The `-inservice`-less run in Task 3, step 4, and the empty NVRAM directory in `pinmame_check.sh` (Task 4).
- **No RTC answer** on I2C1: the firmware then stops on `SET DATETIME` and never reads its console. `rtc_test.c` in Task 2 and Task 3, step 5.

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/pinheck/prop.[ch]` | Propeller device: clock map, edge log, catch-up, P24 read, CAT24M01 on P28/P29, SD attach |
| `src/wpc/pinheck/rtc.[ch]` | DS1340 on I²C |
| `src/wpc/pinheck.c`, `pinheck.h`, `pinheckgames.c`, `src/wpc/driver.c` | driver wiring, NVRAM, `pinheck` parent set, test hooks |
| `src/osdepend.h`, `src/{unix,libpinmame,windows,ios}/fileio.c` | `osd_get_path()` so the driver can open the romset zip for the card |
| build files | registration of `prop`, `rtc`, `eeprom`, `sd`, `vfat`, `zipsrc`, `p8x32a` |
| `tests/pinheck/link/*` | device tests, headless two-CPU machine, build registration |
| `tests/pinheck/pic32mx/{banner.py,makefile_link_check.sh,pinmame_check.sh}` | updated checks |

---
### Task 1: Propeller device

**Files:**
- Create: `src/wpc/pinheck/prop.h`, `src/wpc/pinheck/prop.c`
- Create: `tests/pinheck/link/echo.spin`, `prop_test.c`, `check.sh`, `.gitignore`

**Interfaces:**
- Consumes: `p8x32a_init/reset/run_until/pins` and `p8x32a_bus` (Plan 3), `cat24m01_init/update` (Plan 3).
- Produces: `prop_init(pinheck_prop *p, const uint8_t *rom32k, uint8_t *eemem)` (`eemem` is 131,072 bytes; its first 32 KB must hold `PRP_V008.BIN`), `prop_attach_sd(p, int (*fn)(void *ctx, int cs, int sclk, int mosi), ctx)`, `prop_set_log(p, void (*fn)(void *ctx, const char *msg), ctx)`, `prop_reset(p, uint64_t pic_cycle)`, `prop_pic_pins(p, uint64_t pic_cycle, uint32_t pins)` (pins as Propeller bits 25/26), `prop_catch_up(p, uint64_t pic_cycle)`, `int prop_p24(p, uint64_t pic_cycle)`, `uint64_t prop_time(p, uint64_t pic_cycle)`. Log messages include `prop: CLKSET xx` for every `CLKSET`.

`echo.spin` is the Propeller side of the tests: it switches to PLL16, waits for each rising edge on P25, copies P26 to P24, and records `CNT` of every wake in hub RAM, so a test can see exactly when each PIC32 edge reached the Propeller.

- [ ] **Step 1: Write the tests**

`tests/pinheck/link/echo.spin`:

```text
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   clkset  pll
        mov     dira, p24
loop    waitpeq p25, p25
        mov     t, cnt
        test    p26, ina wc
        muxc    outa, p24
        wrlong  t, ptr
        add     ptr, #4
        add     count, #1
        wrlong  count, addr
        waitpne p25, p25
        jmp     #loop
pll     long    $6F
p24     long    1 << 24
p25     long    1 << 25
p26     long    1 << 26
count   long    0
addr    long    $7FF0
ptr     long    $1000
t       long    0
        long    $C0DEE0D0
```

`tests/pinheck/link/prop_test.c`:

```c
#include "../../../src/wpc/pinheck/prop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLK  (1u << 25)
#define DATA (1u << 26)
#define BITS 64

static pinheck_prop p;
static uint8_t rom[0x8000], ram[0x8000], eemem[131072];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("PROP FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t hub32(uint32_t a)
{
	const uint8_t *h = p.chip.hub + a;
	return (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
}

static uint32_t count(void) { return hub32(0x7FF0); }

static void boot(void)
{
	memset(eemem, 0xFF, sizeof(eemem));
	prop_init(&p, rom, eemem);
	memcpy(p.chip.hub, ram, sizeof(ram));
}

static void pulse(uint64_t *pic, int bit)
{
	prop_pic_pins(&p, *pic, CLK | (bit ? DATA : 0));
	prop_pic_pins(&p, *pic + 200, bit ? DATA : 0);
	*pic += 400;
}

static void rates(void)
{
	boot();
	CHECK(prop_time(&p, 20000) == 3000);
	prop_catch_up(&p, 200000);
	CHECK(p.nseg == 2 && p.seg[1].num == 13 && p.seg[1].den == 10);
	CHECK(prop_time(&p, 200010) - prop_time(&p, 200000) == 13);
	CHECK(prop_time(&p, p.seg[1].pic0) == p.seg[1].prop0);
}

static void echo_exact(void)
{
	uint32_t rec[BITS], off = 0;
	uint64_t pic = 200000, at[BITS];
	int k, prev = 0;
	boot();
	prop_catch_up(&p, pic);
	for (k = 0; k < BITS; k++) {
		int bit = (k * 7 + 3) % 5 < 2;
		at[k] = pic;
		prop_pic_pins(&p, pic, CLK | (bit ? DATA : 0));
		CHECK(prop_p24(&p, pic + 1) == prev);
		CHECK(prop_p24(&p, pic + 100) == bit);
		prop_pic_pins(&p, pic + 200, bit ? DATA : 0);
		pic += 400;
		prev = bit;
	}
	prop_catch_up(&p, pic);
	CHECK(count() == BITS);
	for (k = 0; k < BITS; k++) {
		rec[k] = hub32(0x1000 + 4 * (uint32_t)k);
		if (k == 0) off = rec[0] - (uint32_t)prop_time(&p, at[0]);
		CHECK(rec[k] - (uint32_t)prop_time(&p, at[k]) == off);
	}
	boot();
	pic = 200000;
	for (k = 0; k < BITS; k++) pulse(&pic, (k * 7 + 3) % 5 < 2);
	prop_catch_up(&p, pic);
	CHECK(count() == BITS);
	for (k = 0; k < BITS; k++) CHECK(hub32(0x1000 + 4 * (uint32_t)k) == rec[k]);
}

static void clkset_lazy(void)
{
	static const int at[] = { 0, 10, 100, 999 };
	uint64_t pic = 60000;
	int k;
	boot();
	for (k = 0; k < 1000; k++) pulse(&pic, 0);
	CHECK(p.nseg == 1);
	for (k = 0; k < 4; k++) {
		prop_catch_up(&p, 60000 + 400 * (uint64_t)at[k] + 150);
		CHECK(count() == (uint32_t)at[k] + 1);
	}
	CHECK(p.nseg == 2);
}

static void full_log(void)
{
	uint64_t pic = 60000;
	int k;
	boot();
	for (k = 0; k < 3000; k++) pulse(&pic, k & 1);
	CHECK(p.count <= PROP_EDGES);
	prop_catch_up(&p, pic);
	CHECK(count() == 3000);
}

static void reset_rebases(int restart_at_zero)
{
	uint64_t now, pic = 200000;
	boot();
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	pulse(&pic, 0);
	prop_catch_up(&p, pic);
	CHECK(count() == 2);
	now = p.chip.now;
	if (restart_at_zero) pic = 0;
	prop_reset(&p, pic);
	CHECK(p.chip.now >= now);
	CHECK(prop_time(&p, pic) == p.chip.now);
	CHECK(prop_time(&p, pic + 1000) > prop_time(&p, pic));
	pic += 200000;
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	prop_catch_up(&p, pic);
	CHECK(count() == 1);
}

static void clkset_reset_bit(void)
{
	uint64_t pic = 200000;
	boot();
	prop_catch_up(&p, pic);
	pulse(&pic, 1);
	pulse(&pic, 0);
	prop_catch_up(&p, pic);
	CHECK(count() == 2);
	p.chip.bus.clkset(p.chip.bus.ctx, p.chip.now, 0x80);
	pic += 1000;
	prop_catch_up(&p, pic);
	CHECK(p.nseg == 1 && p.seg[0].num == 3);
	pic += 400000;
	prop_catch_up(&p, pic);
	CHECK(p.nseg == 2 && p.seg[1].num == 13);
	pulse(&pic, 1);
	prop_catch_up(&p, pic);
	CHECK(count() == 1);
}

static int load(const char *path, uint8_t *dst)
{
	FILE *f = fopen(path, "rb");
	size_t n;
	if (!f) { perror(path); return 0; }
	n = fread(dst, 1, 0x8000, f);
	fclose(f);
	return n == 0x8000;
}

int main(int argc, char **argv)
{
	if (argc != 3 || !load(argv[1], rom) || !load(argv[2], ram)) {
		fprintf(stderr, "usage: prop_test echo.rom echo.ram\n");
		return 2;
	}
	rates();
	echo_exact();
	clkset_lazy();
	full_log();
	reset_rebases(1);
	reset_rebases(0);
	clkset_reset_bit();
	printf("prop: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

`tests/pinheck/link/check.sh`:

```sh
#!/bin/sh
if [ -n "$PINHECK_UPDATE_DIR" ]; then PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2; fi
if [ -n "$P8X32A_ROM" ]; then P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2; fi
if [ -n "$TOOLS" ]; then TOOLS=$(realpath "$TOOLS") || exit 2; fi
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
TOOLS=${TOOLS:-$PWD/../p8x32a/build/tools}
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic -I$S/cpu/pic32mx"
CORE="$S/wpc/pinheck/prop.c $S/wpc/pinheck/eeprom.c $S/cpu/p8x32a/p8x32a.c"
SDSRC="$S/wpc/pinheck/sd.c $S/wpc/pinheck/vfat.c $S/wpc/pinheck/zipsrc.c -lz"
[ -f $S/wpc/pinheck/zipsrc.c ] || { echo "PLAN 4 MISSING: src/wpc/pinheck/sd.c, vfat.c, zipsrc.c must exist (execute Plan 4 first)"; exit 2; }
mkdir -p $B
fail=0
for f in prop.c rtc.c; do [ -f $S/wpc/pinheck/$f ] || continue; cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/$f || { echo "C89 FAIL $f"; fail=$((fail + 1)); }; done
[ -x "$TOOLS/openspin/build/openspin" ] || { echo "TOOLS MISSING: run ../p8x32a/tools.sh or set TOOLS"; exit 2; }
"$TOOLS/openspin/build/openspin" -q echo.spin -o $B/echo.binary > $B/echo.log 2>&1 || { cat $B/echo.log; exit 2; }
python3 ../p8x32a/mkrom.py $B/echo.binary $B/echo.rom $B/echo.ram || exit 2
cc $CF -o $B/prop_test prop_test.c $CORE || exit 2
./$B/prop_test $B/echo.rom $B/echo.ram || fail=$((fail + 1))
if [ -f rtc_test.c ]; then
	cc $CF -o $B/rtc_test rtc_test.c $S/wpc/pinheck/rtc.c || exit 2
	./$B/rtc_test || fail=$((fail + 1))
fi
if [ -f machine.c ]; then
	if [ -n "$PINHECK_UPDATE_DIR" ] && [ -n "$P8X32A_ROM" ]; then
		cc $CF -o $B/linkmachine machine.c $CORE $S/wpc/pinheck/rtc.c $S/cpu/pic32mx/pic32mx.c $S/cpu/mips32/mips32.c $SDSRC || exit 2
		sh ./machine_check.sh "$B/linkmachine" "$P8X32A_ROM" "$PINHECK_UPDATE_DIR" || fail=$((fail + 1))
	elif [ "$PINHECK_SKIP_FIRMWARE" != 1 ]; then
		echo "FIRMWARE MISSING: set PINHECK_UPDATE_DIR and P8X32A_ROM, or PINHECK_SKIP_FIRMWARE=1"
		fail=$((fail + 1))
	fi
fi
echo "link: $fail failed"
[ $fail -eq 0 ]
```

`chmod +x tests/pinheck/link/check.sh`

`tests/pinheck/link/.gitignore`:

```text
build/
```

- [ ] **Step 2: Run to confirm they fail**

Run: `env TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_SKIP_FIRMWARE=1 tests/pinheck/link/check.sh`
Expected: exit 2 with `prop.c: No such file or directory`.

- [ ] **Step 3: Write the device**

`src/wpc/pinheck/prop.h`:

```c
#ifndef PINHECK_PROP_H
#define PINHECK_PROP_H

#include "../../cpu/p8x32a/p8x32a.h"
#include "eeprom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROP_EDGES 4096
#define PROP_SEGS  32
#define PROP_PIC_PINS ((1u << 25) | (1u << 26))

typedef int (*prop_spi_fn)(void *ctx, int cs, int sclk, int mosi);
typedef void (*prop_log_fn)(void *ctx, const char *msg);

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
} pinheck_prop;

void prop_init(pinheck_prop *p, const uint8_t *rom32k, uint8_t *eemem);
void prop_attach_sd(pinheck_prop *p, prop_spi_fn fn, void *ctx);
void prop_set_log(pinheck_prop *p, prop_log_fn fn, void *ctx);
void prop_reset(pinheck_prop *p, uint64_t pic_cycle);
void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins);
void prop_catch_up(pinheck_prop *p, uint64_t pic_cycle);
int prop_p24(pinheck_prop *p, uint64_t pic_cycle);
uint64_t prop_time(const pinheck_prop *p, uint64_t pic_cycle);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/prop.c`:

```c
#include "prop.h"
#include <stdio.h>
#include <string.h>

#define PIN_DO   (1u << 0)
#define PIN_SCLK (1u << 1)
#define PIN_DI   (1u << 2)
#define PIN_CS   (1u << 3)
#define PIN_PGM  (1u << 13)
#define PIN_P24  (1u << 24)
#define PIN_SCL  (1u << 28)
#define PIN_SDA  (1u << 29)

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
}

static uint32_t pins_in(void *ctx, uint64_t t)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	uint32_t v = p->base_pins;
	int k;
	for (k = 0; k < p->count; k++) {
		const prop_edge *e = &p->edge[(p->head + k) % PROP_EDGES];
		if (to_prop(p, e->pic) > t) break;
		v = e->pins;
	}
	return v | p->ee_bits | (p->sd_do ? PIN_DO : 0) | PIN_CS | PIN_PGM;
}

static uint64_t pins_next(void *ctx, uint64_t t)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	int k;
	for (k = 0; k < p->count; k++) {
		uint64_t e = to_prop(p, p->edge[(p->head + k) % PROP_EDGES].pic);
		if (e > t) return e;
	}
	return P8X32A_NEVER;
}

static void pins_out(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	int scl = (dir & PIN_SCL) ? (out & PIN_SCL) != 0 : 1;
	int sda = (dir & PIN_SDA) ? (out & PIN_SDA) != 0 : 1;
	(void)t;
	p->ee_bits = PIN_SCL | (cat24m01_update(&p->eeprom, scl, sda) ? PIN_SDA : 0);
	if (p->sd) {
		int cs = (dir & PIN_CS) ? (out & PIN_CS) != 0 : 1;
		int sclk = (dir & PIN_SCLK) ? (out & PIN_SCLK) != 0 : 0;
		int mosi = (dir & PIN_DI) ? (out & PIN_DI) != 0 : 1;
		p->sd_do = p->sd(p->sd_ctx, cs, sclk, mosi) != 0;
	}
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
	p->reset_pending = 0;
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
}

void prop_set_log(pinheck_prop *p, prop_log_fn fn, void *ctx)
{
	p->log = fn;
	p->log_ctx = ctx;
}

void prop_reset(pinheck_prop *p, uint64_t pic_cycle)
{
	prop_catch_up(p, p->pic_last);
	p->pic_last = pic_cycle;
	p->head = p->count = 0;
	p->base_pins = p->last_pins = 0;
	restart(p, p->chip.now);
	p->seg[0].pic0 = pic_cycle;
}

uint64_t prop_time(const pinheck_prop *p, uint64_t pic_cycle)
{
	return to_prop(p, pic_cycle);
}

void prop_catch_up(pinheck_prop *p, uint64_t pic_cycle)
{
	uint64_t t;
	if (pic_cycle > p->pic_last) p->pic_last = pic_cycle;
	while ((t = to_prop(p, pic_cycle)) > p->chip.now + 1) {
		p8x32a_run_until(&p->chip, t - 1);
		if (p->reset_pending) restart(p, p->chip.now);
	}
	while (p->count && to_prop(p, p->edge[p->head].pic) <= p->chip.now) {
		p->base_pins = p->edge[p->head].pins;
		p->head = (p->head + 1) % PROP_EDGES;
		p->count--;
	}
}

void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins)
{
	pins &= PROP_PIC_PINS;
	if (pic_cycle > p->pic_last) p->pic_last = pic_cycle;
	if (pins == p->last_pins) return;
	if (p->count == PROP_EDGES) prop_catch_up(p, pic_cycle);
	p->edge[(p->head + p->count) % PROP_EDGES].pic = pic_cycle;
	p->edge[(p->head + p->count) % PROP_EDGES].pins = pins;
	p->count++;
	p->last_pins = pins;
}

int prop_p24(pinheck_prop *p, uint64_t pic_cycle)
{
	uint32_t dir, out;
	prop_catch_up(p, pic_cycle);
	out = p8x32a_pins(&p->chip, p->chip.now, &dir);
	return (dir & PIN_P24) ? (out & PIN_P24) != 0 : 0;
}
```

- [ ] **Step 4: Run to verify they pass**

Same command. Expected: `prop: ok`, `link: 0 failed`.

- [ ] **Step 5: Prove the tests bite**

Apply each replacement to `src/wpc/pinheck/prop.c`, run Step 2's command, confirm the failing line, then `git checkout src/wpc/pinheck/prop.c`:

| Replace → with | Fails at |
|---|---|
| `while ((t = to_prop(p, pic_cycle)) > p->chip.now + 1) {` and the next line's `t - 1` → `> p->chip.now) {` / `t` | `rec[k] - (uint32_t)prop_time(&p, at[k]) == off` |
| `\tadd_seg(p, t, cfg);` → `\tif (0) add_seg(p, t, cfg);` | `p.nseg == 2 && p.seg[1].num == 13 && p.seg[1].den == 10` |
| `if (p->count == PROP_EDGES) prop_catch_up(p, pic_cycle);` → `if (p->count == PROP_EDGES) return;` | `count() == 3000` |
| `\tp->seg[0].pic0 = pic_cycle;` → `\t(void)pic_cycle;` | `prop_time(&p, pic + 1000) > prop_time(&p, pic)` |
| `if (e > t) return e;` → `if (e > t) return P8X32A_NEVER;` | `prop_p24(&p, pic + 100) == bit` |

- [ ] **Step 6: Commit**

```bash
git add src/wpc/pinheck/prop.h src/wpc/pinheck/prop.c tests/pinheck/link
git commit -m "pinheck: Propeller device with clock map, edge log and catch-up"
```

### Task 2: DS1340 real-time clock

**Files:**
- Create: `tests/pinheck/link/rtc_test.c`, `src/wpc/pinheck/rtc.h`, `src/wpc/pinheck/rtc.c`

**Interfaces:**
- Produces: `ds1340_init(ds1340 *d, int64_t unix_seconds, uint64_t ticks_per_second)`, `ds1340_tick(d, uint64_t ticks)`, `int ds1340_update(d, int scl, int sda)` (returns the level the device drives on SDA, 1 = released; wire-AND it with other devices).

The firmware probes device `$68` at every boot; without an answer it stops on the `SET DATETIME` screen on every boot after the first and never reads its console (found while bringing up Task 3). Register layout per the DS1340 datasheet: seconds (bit 7 = /EOSC), minutes, hours, day 1–7, date, month, year (BCD), control, trickle, flags.

- [ ] **Step 1: Write the test**

`tests/pinheck/link/rtc_test.c`:

```c
#include "../../../src/wpc/pinheck/rtc.h"
#include <stdio.h>

static ds1340 d;
static int fails, sda_in;

#define CHECK(c) do { if (!(c)) { printf("RTC FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void line(int scl, int sda) { sda_in = ds1340_update(&d, scl, sda); }
static void start(void) { line(1, 1); line(1, 0); line(0, 0); }
static void stop(void) { line(0, 0); line(1, 0); line(1, 1); }

static int put(int b)
{
	int k, ack;
	for (k = 7; k >= 0; k--) { line(0, b >> k & 1); line(1, b >> k & 1); line(0, b >> k & 1); }
	line(0, 1); line(1, 1); ack = !sda_in; line(0, 1);
	return ack;
}

static int get(int ack)
{
	int k, v = 0;
	for (k = 0; k < 8; k++) { line(0, 1); line(1, 1); v = v << 1 | sda_in; }
	line(0, ack ? 0 : 1); line(1, ack ? 0 : 1); line(0, 1);
	return v;
}

static void read_time(int *r)
{
	int k;
	start(); put(0xD0); put(0x00); start(); put(0xD1);
	for (k = 0; k < 7; k++) r[k] = get(k < 6);
	stop();
}

int main(void)
{
	int r[7];
	ds1340_init(&d, 1483228800, 1000);
	read_time(r);
	CHECK(r[0] == 0x00 && r[1] == 0x00 && r[2] == 0x00);
	CHECK(r[3] == 1 && r[4] == 0x01 && r[5] == 0x01 && r[6] == 0x17);
	ds1340_tick(&d, 3723000);
	read_time(r);
	CHECK(r[0] == 0x03 && r[1] == 0x02 && r[2] == 0x01);
	start();
	CHECK(put(0xD0) && put(0x04));
	CHECK(put(0x29) && put(0x02) && put(0x24));
	stop();
	read_time(r);
	CHECK(r[4] == 0x29 && r[5] == 0x02 && r[6] == 0x24 && r[3] == 5);
	ds1340_tick(&d, 86400000);
	read_time(r);
	CHECK(r[4] == 0x01 && r[5] == 0x03 && r[6] == 0x24);
	start();
	CHECK(!put(0xA0));
	stop();
	printf("rtc: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

- [ ] **Step 2: Run to confirm it fails**

Run: `env TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_SKIP_FIRMWARE=1 tests/pinheck/link/check.sh`
Expected: exit 2 with `rtc.c: No such file or directory`.

- [ ] **Step 3: Write the model**

`src/wpc/pinheck/rtc.h`:

```c
#ifndef PINHECK_RTC_H
#define PINHECK_RTC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds1340 {
	uint8_t reg[10];
	int scl, sda;
	int state, bit, out, ack, ptr, sent;
	uint8_t shift;
	int64_t base;
	uint64_t ticks, ticks_per_s;
} ds1340;

void ds1340_init(ds1340 *d, int64_t unix_seconds, uint64_t ticks_per_second);
void ds1340_tick(ds1340 *d, uint64_t ticks);
int ds1340_update(ds1340 *d, int scl, int sda);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/rtc.c`:

```c
#include "rtc.h"
#include <string.h>

enum { IDLE, CTRL, PTR, WRITE, READ, IGNORE };

static uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

static int64_t days_from_civil(int y, int m, int d)
{
	int64_t era, yoe, doy, doe;
	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
	int64_t era, doe, yoe, doy, mp;
	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yoe + era * 400 + (*m <= 2));
}

static int64_t now(const ds1340 *d)
{
	return d->base + (int64_t)(d->ticks / (d->ticks_per_s ? d->ticks_per_s : 1));
}

static void latch(ds1340 *d)
{
	int64_t t = now(d), days = t >= 0 ? t / 86400 : (t - 86399) / 86400, s = t - days * 86400;
	int y, m, dd;
	civil_from_days(days, &y, &m, &dd);
	d->reg[0] = (uint8_t)((d->reg[0] & 0x80) | bcd((int)(s % 60)));
	d->reg[1] = bcd((int)(s / 60 % 60));
	d->reg[2] = (uint8_t)((d->reg[2] & 0xC0) | bcd((int)(s / 3600)));
	d->reg[3] = (uint8_t)(((days + 4) % 7 + 7) % 7 + 1);
	d->reg[4] = bcd(dd);
	d->reg[5] = bcd(m);
	d->reg[6] = bcd(y % 100);
}

static void store_time(ds1340 *d)
{
	int y = 2000 + unbcd(d->reg[6]);
	int64_t t = days_from_civil(y, unbcd(d->reg[5] & 0x1F), unbcd(d->reg[4] & 0x3F)) * 86400 +
		unbcd(d->reg[2] & 0x3F) * 3600 + unbcd(d->reg[1] & 0x7F) * 60 + unbcd(d->reg[0] & 0x7F);
	d->base = t - (int64_t)(d->ticks / (d->ticks_per_s ? d->ticks_per_s : 1));
}

void ds1340_init(ds1340 *d, int64_t unix_seconds, uint64_t ticks_per_second)
{
	memset(d, 0, sizeof(*d));
	d->base = unix_seconds;
	d->ticks_per_s = ticks_per_second;
	d->scl = d->sda = 1;
	d->out = 1;
	d->reg[7] = 0x80;
	latch(d);
}

void ds1340_tick(ds1340 *d, uint64_t ticks) { d->ticks += ticks; }

static void byte_done(ds1340 *d)
{
	uint8_t b = d->shift;
	switch (d->state) {
	case CTRL:
		if ((b >> 1) != 0x68) { d->ack = 0; d->state = IGNORE; return; }
		d->ack = 1;
		d->state = (b & 1) ? READ : PTR;
		if (d->state == READ) latch(d);
		return;
	case PTR:
		d->ptr = b % 10;
		d->ack = 1;
		d->state = WRITE;
		latch(d);
		return;
	case WRITE:
		d->reg[d->ptr] = b;
		if (d->ptr <= 6) store_time(d);
		d->ptr = (d->ptr + 1) % 10;
		d->ack = 1;
		return;
	}
	d->ack = 0;
}

int ds1340_update(ds1340 *d, int scl, int sda)
{
	int pscl = d->scl, psda = d->sda;
	d->scl = scl;
	d->sda = sda;
	if (pscl && scl && psda != sda) {
		if (!sda) { d->state = CTRL; d->bit = 0; d->shift = 0; d->out = 1; d->sent = 0; }
		else { d->state = IDLE; d->out = 1; }
		return d->out;
	}
	if (d->state == IDLE || d->state == IGNORE) return d->out = 1;
	if (!pscl && scl) {
		if (d->bit < 8) {
			if (d->state != READ) d->shift = (uint8_t)(d->shift << 1 | (sda & 1));
			d->bit++;
		} else {
			if (d->sent && sda) { d->state = IGNORE; d->out = 1; return d->out; }
			d->bit = 9;
		}
		return d->out;
	}
	if (pscl && !scl) {
		if (d->bit == 8) {
			if (d->sent) d->out = 1;
			else { byte_done(d); d->out = d->ack ? 0 : 1; }
		} else if (d->bit == 9) {
			d->bit = 0;
			d->shift = 0;
			if (d->state == READ) { d->shift = d->reg[d->ptr]; d->ptr = (d->ptr + 1) % 10; d->out = d->shift >> 7 & 1; d->sent = 1; }
			else d->out = 1;
		} else if (d->sent && d->bit > 0) d->out = d->shift >> (7 - d->bit) & 1;
	}
	return d->out;
}
```

- [ ] **Step 4: Run to verify**

Same command. Expected: `prop: ok`, `rtc: ok`, `link: 0 failed`.

- [ ] **Step 5: Prove the test bites**

Apply each to `src/wpc/pinheck/rtc.c`, run, then `git checkout src/wpc/pinheck/rtc.c`:

| Replace → with | Fails at |
|---|---|
| `if (d->sent && sda) {` → `if (sda) {` | line 42 (first read) |
| `if (d->ptr <= 6) store_time(d);` → `if (0) store_time(d);` | line 52 (set date) |
| `((days + 4) % 7 + 7) % 7 + 1` → `((days + 3) % 7 + 7) % 7 + 1` | line 43 (weekday) |

- [ ] **Step 6: Commit**

```bash
git add src/wpc/pinheck/rtc.h src/wpc/pinheck/rtc.c tests/pinheck/link/rtc_test.c
git commit -m "pinheck: DS1340 real-time clock model"
```

### Task 3: Headless two-CPU machine (gated on D1)

**Files:**
- Create: `tests/pinheck/link/machine.c`, `tests/pinheck/link/machine_check.sh`
- Modify: `tests/pinheck/pic32mx/banner.py` (sync line becomes a regex: the real Propeller answers after a few dots)

**Interfaces:**
- Consumes: Tasks 1–2, `pic32mx_*` (Plan 2), `sd_*`, `vfat_*`, `zipsrc_*` (Plan 4).
- Produces: `build/linkmachine -rom ROM -prp PRP_V008.BIN [-zip ZIP] [-inservice V] [-v] [-c cycles] [-boots n] [-send cycle text] DOM_V006.PRG` printing UART1 to stdout with `=== BOOT n ===` markers and, with `-v`, `exceptions=… speed=…x` on stderr. `-inservice V` is D1 (c).

- [ ] **Step 1: Write the machine, its check and the banner update**

`tests/pinheck/link/machine.c`:

```c
#include "pic32mx.h"
#include "../../../src/wpc/pinheck/prop.h"
#include "../../../src/wpc/pinheck/rtc.h"
#include "../../../src/wpc/pinheck/sd.h"
#include "../../../src/wpc/pinheck/zipsrc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RF5  (1u << 5)
#define RF12 (1u << 12)
#define RF13 (1u << 13)

static pic32mx soc;
static pinheck_prop prop;
static cat24m01 u13;
static ds1340 rtc;
static uint64_t rtc_at;
static uint8_t u13mem[131072], propmem[131072], rom[0x8000];
static uint8_t flash[PIC32MX_FLASH_SIZE];
static int verbose;
static zipsrc zip;
static vfat vol;
static sd_card card;

static int blk_read(void *ctx, uint32_t lba, uint8_t *buf) { (void)ctx; return vfat_read(&vol, lba, buf); }
static int spi(void *ctx, int cs, int sclk, int mosi) { (void)ctx; return sd_update(&card, cs, sclk, mosi); }

static void port_write(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle)
{
	uint32_t drv = lat & ~tris;
	(void)ctx;
	if (port == PIC32MX_PORTF)
		prop_pic_pins(&prop, cycle, (drv & RF12 ? 1u << 25 : 0) | (drv & RF5 ? 1u << 26 : 0));
}

static uint32_t port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx;
	if (port != PIC32MX_PORTF) return 0xFFFFu;
	return (0xFFFFu & ~RF13) | (prop_p24(&prop, cycle) ? RF13 : 0);
}

static void uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (uart == 1) fputc(byte, stdout);
}

static int i2c_pins(void *ctx, int module, int scl, int sda, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (module != 1) return sda;
	ds1340_tick(&rtc, cycle - rtc_at);
	rtc_at = cycle;
	return cat24m01_update(&u13, scl, sda) & ds1340_update(&rtc, scl, sda);
}

static void exception(void *ctx, int code, uint32_t pc)
{
	(void)ctx;
	fprintf(stderr, "exception %d at %08x\n", code, (unsigned)pc);
}

static void proplog(void *ctx, const char *msg)
{
	(void)ctx;
	if (verbose) fprintf(stderr, "%s\n", msg);
}

static int load(const char *path, uint8_t *dst, size_t max, size_t *got)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return 0; }
	*got = fread(dst, 1, max, f);
	fclose(f);
	return 1;
}

int main(int argc, char **argv)
{
	pic32mx_board board = { NULL, port_write, port_read, uart_tx, i2c_pins, NULL, exception };
	unsigned long long cycles = 800000000ull, send_at = 0;
	const char *path = NULL, *rompath = NULL, *prp = NULL, *send = NULL, *zippath = NULL;
	int boots = 1, b, i, inservice = -1;
	size_t n, m;
	clock_t t0 = clock();

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) verbose = 1;
		else if (!strcmp(argv[i], "-c") && i + 1 < argc) cycles = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-boots") && i + 1 < argc) boots = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-rom") && i + 1 < argc) rompath = argv[++i];
		else if (!strcmp(argv[i], "-prp") && i + 1 < argc) prp = argv[++i];
		else if (!strcmp(argv[i], "-zip") && i + 1 < argc) zippath = argv[++i];
		else if (!strcmp(argv[i], "-inservice") && i + 1 < argc) inservice = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-send") && i + 2 < argc) { send_at = strtoull(argv[++i], NULL, 0); send = argv[++i]; }
		else path = argv[i];
	}
	if (!path || !rompath || !prp) {
		fprintf(stderr, "usage: linkmachine -rom p8x32a.rom -prp PRP_V008.BIN [-zip dominos.zip] [-inservice version] [-v] [-c cycles] [-boots n] [-send cycle text] DOM_V006.PRG\n");
		return 2;
	}
	memset(propmem, 0xFF, sizeof(propmem));
	memset(u13mem, 0xFF, sizeof(u13mem));
	if (!load(rompath, rom, sizeof(rom), &n) || n != sizeof(rom)) { fprintf(stderr, "rom must be 32768 bytes\n"); return 2; }
	if (!load(prp, propmem, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "PRP image must be 32768 bytes\n"); return 2; }
	if (!load(path, flash, sizeof(flash), &m)) return 2;
	if (inservice >= 0) {
		uint32_t w0 = (uint32_t)inservice << 24 | 0xBAFAu, w1 = 0xABBA0002u;
		for (i = 0; i < 4; i++) { propmem[0x8000 + i] = (uint8_t)(w0 >> (8 * i)); propmem[0x8004 + i] = (uint8_t)(w1 >> (8 * i)); }
	}
	pic32mx_init(&soc, &board, flash, (uint32_t)m);
	prop_init(&prop, rom, propmem);
	prop_set_log(&prop, proplog, NULL);
	if (zippath) {
		sd_blockdev dev;
		if (zipsrc_open(&zip, zippath, 64u << 20) != 0 || vfat_init(&vol, zipsrc_source(&zip)) != 0) { fprintf(stderr, "%s: cannot build the SD volume\n", zippath); return 2; }
		dev.ctx = NULL;
		dev.sectors = vfat_sectors(&vol);
		dev.read = blk_read;
		sd_init(&card, &dev);
		prop_attach_sd(&prop, spi, NULL);
	}
	for (b = 1; b <= boots; b++) {
		unsigned long long done = 0;
		const char *s = send;
		printf("=== BOOT %d ===\n", b);
		if (b > 1) { pic32mx_reset(&soc); prop_reset(&prop, soc.cpu.cycles); }
		cat24m01_init(&u13, u13mem, 0);
		ds1340_init(&rtc, 1483228800, 80000000u);
		rtc_at = soc.cpu.cycles;
		while (done < cycles) {
			done += (unsigned long long)pic32mx_run(&soc, 100000);
			prop_catch_up(&prop, soc.cpu.cycles);
			if (s && *s && done >= send_at) {
				int k = soc.uart[0].rx_count;
				pic32mx_uart_rx(&soc, 0, (uint8_t)*s);
				if (soc.uart[0].rx_count > k) s++;
			}
		}
		printf("\n");
	}
	fflush(stdout);
	if (verbose) {
		double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
		fprintf(stderr, "exceptions=%llu prop_now=%llu wall=%.1fs emulated=%.1fs speed=%.2fx\n",
			(unsigned long long)soc.exc_count, (unsigned long long)prop.chip.now, secs,
			(double)cycles * boots / 80e6, (double)cycles * boots / 80e6 / secs);
	}
	return soc.exc_count ? 1 : 0;
}
```

`tests/pinheck/link/machine_check.sh`:

```sh
#!/bin/sh
run=$1 rom=$2 upd=$3
B=build
zip=${PINHECK_ZIP:-$B/dominos-stored.zip}
if [ ! -f "$zip" ]; then
	echo "machine: building $zip from $upd (stored, once)"
	(cd "$upd" && zip -q -0 -r "$OLDPWD/$zip" DOM_V006.PRG PRP_V008.BIN DMD SFX) || { echo "MACHINE FAIL: cannot build $zip"; exit 1; }
fi
fail=0
$run -v -inservice 6 -boots 2 -c 800000000 -send 600000000 '[E97000]' -zip "$zip" -rom "$rom" -prp "$upd/PRP_V008.BIN" "$upd/DOM_V006.PRG" > $B/machine.txt 2> $B/machine.err || { echo "MACHINE FAIL: CPU exceptions"; fail=$((fail + 1)); }
for want in "prop: CLKSET 6f" "exceptions=0"; do
	grep -qF "$want" $B/machine.err || { echo "MACHINE FAIL: stderr lacks '$want'"; fail=$((fail + 1)); }
done
python3 ../pic32mx/banner.py $B/machine.txt || fail=$((fail + 1))
grep -aq "Ball Search: DISABLED" $B/machine.txt || { echo "MACHINE FAIL: no reply to [E97000]"; fail=$((fail + 1)); }
tail -1 $B/machine.err
[ $fail -eq 0 ] && echo "machine: ok"
[ $fail -eq 0 ]
```

`chmod +x tests/pinheck/link/machine_check.sh`

`tests/pinheck/pic32mx/banner.py`:

```python
#!/usr/bin/env python3
import re
import sys

TRACE = re.compile(r'READ EEPROM\r?\n(Checksum Number: \d+\r?\n)?(Timeout Counter: \d+\r?\n)*(Read OK!\r?\n)?')


def boots(text):
    parts = re.split(r'=== BOOT (\d+) ===\n', text)
    return {int(parts[i]): TRACE.sub('', parts[i + 1]).replace('\r', '') for i in range(1, len(parts), 2)}


def main():
    b = boots(open(sys.argv[1], encoding='latin-1').read())
    want = [
        (1, 'PROPELLER SYNC CHECK.*OK'),
        (1, 'NAMING GAME'),
        (2, 'NAME ALREADY EXISTS'),
        (2, 'pinHeck System 2011-2016'),
        (2, 'Game: DOM - DOMINOS'),
        (2, 'Version: 006'),
    ]
    bad = [(n, s) for n, s in want if not re.search(s, b.get(n, ''))]
    for n, s in bad:
        print('BANNER FAIL boot %d lacks %r' % (n, s))
    if 'pinHeck System' in b.get(1, ''):
        print('BANNER FAIL boot 1 printed the banner on a blank EEPROM')
        bad.append((1, 'banner'))
    print('banner: %s' % ('FAIL' if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 2: Run the suite**

Run: `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && tests/pinheck/link/check.sh`
Expected (about 2–4 minutes): `prop: ok`, `rtc: ok`, `banner: ok`, a line `exceptions=0 … speed=0.1…x`, `machine: ok`, `link: 0 failed`.

`build/machine.txt` shows both boots passing `PROPELLER SYNC CHECK......OK`, boot 2 printing `pinHeck System 2011-2016`, `Game: DOM - DOMINOS`, `Version: 006`, the RTC date `2017/1/1 0:0:5`, and `Ball Search: DISABLED` in reply to the injected `[E97000]`.

- [ ] **Step 3: Confirm the Milestone 2 suite still passes with the new banner.py**

Run: `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && tests/pinheck/pic32mx/check.sh`
Expected: `banner: ok` … `pic32mx: 0 failed`.

- [ ] **Step 4: Show the D1 gate (Review Focus 4)**

Run: `cd tests/pinheck/link && ./build/linkmachine -boots 2 -c 800000000 -zip "$PINHECK_ZIP" -rom "$P8X32A_ROM" -prp "$PINHECK_UPDATE_DIR/PRP_V008.BIN" "$PINHECK_UPDATE_DIR/DOM_V006.PRG" | grep -a SYNC; cd -` (no `-inservice`; if `PINHECK_ZIP` is unset use `build/dominos-stored.zip`).
Expected: two lines `PROPELLER SYNC CHECK..................` with no `OK`: the Propeller is waiting for the bootloader.

- [ ] **Step 5: Show the RTC is required (Review Focus 5)**

Build a copy of `machine.c` with `return cat24m01_update(&u13, scl, sda) & ds1340_update(&rtc, scl, sda);` replaced by `(void)rtc; return cat24m01_update(&u13, scl, sda);` into `build/linkmachine_nortc`, run it like `machine_check.sh` does (with `-inservice 6`), and confirm `grep -a "Ball Search"` finds nothing while `PROPELLER SYNC CHECK......OK` still appears. Delete the copy.

- [ ] **Step 6: Commit**

```bash
git add tests/pinheck/link/machine.c tests/pinheck/link/machine_check.sh tests/pinheck/pic32mx/banner.py
git commit -m "pinheck: headless PIC32 + Propeller machine passing the sync check (D1: in-service EEPROM)"
```

### Task 4: PinMAME integration (gated on D1)

**Files:**
- Modify: `src/osdepend.h`, `src/unix/fileio.c`, `src/libpinmame/fileio.c`, `src/windows/fileio.c`, `src/ios/fileio.c`, `src/wpc/driver.c` (scripted, Step 3)
- Replace: `src/wpc/pinheck.h`, `src/wpc/pinheck.c`, `src/wpc/pinheckgames.c`, `tests/pinheck/pic32mx/makefile_link_check.sh`, `tests/pinheck/pic32mx/pinmame_check.sh`
- Create: `tests/pinheck/link/register_build.py`; it edits `src/pinmame.mak`, 10 cmake lists, 4 VS projects and their filters

**Interfaces:**
- Consumes: Tasks 1–3, Plan 4's sources.
- Produces: `const char *osd_get_path(int pathtype, int pathindex)` on every platform; parent set `pinheck` (`NOT_A_DRIVER`) and clone `dominos`; `PINHECK_BIOSREGION` (`REGION_USER2`).

- [ ] **Step 1: Write the new PinMAME check**

`tests/pinheck/pic32mx/pinmame_check.sh`:

```sh
#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
[ -n "$PINHECK_ZIP" ] && { PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2; }
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms $B/nvram || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
if [ -n "$PINHECK_ZIP" ]; then
	ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
else
	(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$OLDPWD/$B/roms/dominos.zip" DOM_V006.PRG PRP_V008.BIN DMD SFX) || exit 2
fi
(cd $B && PINHECK_UART1_LOG=$PWD/uart1.log PINHECK_PROP_LOG=$PWD/prop.log PINHECK_RESET_AT=9 PINHECK_UART1_SEND_AT=7.5 PINHECK_UART1_SEND='[E97000]' \
	"$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram -headless -frames_to_run 1080 -skip_gamewarnings -nothrottle > run.out 2>&1) || { echo "PINMAME FAIL: sdl3pinmame exited $?"; tail -5 $B/run.out; exit 1; }
fail=0
[ "$(grep -ac 'PROPELLER SYNC CHECK\.*OK' $B/uart1.log)" -eq 2 ] || { echo "PINMAME FAIL: expected the sync check to pass on both boots"; fail=1; }
for want in "pinHeck System 2011-2016" "Version: 006" "Ball Search: DISABLED"; do
	grep -aqF "$want" $B/uart1.log || { echo "PINMAME FAIL: UART1 log lacks '$want'"; fail=1; }
done
grep -qx "prop: CLKSET 6f" $B/prop.log || { echo "PINMAME FAIL: the Propeller did not switch to its 104 MHz clock"; fail=1; }
[ -s $B/nvram/dominos.nv ] || { echo "PINMAME FAIL: no NVRAM written"; fail=1; }
if [ $fail -ne 0 ]; then cat -v $B/uart1.log | grep -av "EEPROM\|Checksum\|Timeout\|Read OK\|Write\|Exchanging" | tail -20; exit 1; fi
echo "pinmame: ok"
```

- [ ] **Step 2: Run it against the current build to see it fail**

Run (any `sdl3pinmame` built before this task, e.g. Plan 2's): `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && SDL3PINMAME=build/sdl3pinmame/sdl3pinmame tests/pinheck/pic32mx/pinmame_check.sh`
Expected: a `PINMAME FAIL` line: `expected the sync check to pass on both boots` with a Plan 2 build (no Propeller), or `sdl3pinmame exited 1` with `fuzzy name compare, running domino2` in the output with a build older than Plan 2.

- [ ] **Step 3: Add `osd_get_path` and the driver entry**

```sh
python3 - <<'EOF'
import sys
def rd(p): return open(p, newline='').read()
def wr(p, s): open(p, 'w', newline='').write(s)
p = 'src/osdepend.h'; s = rd(p); nl = '\r\n' if '\r\n' in s else '\n'
a = 'int osd_get_path_count(int pathtype);'
if 'osd_get_path(int' not in s:
    assert s.count(a) == 1; s = s.replace(a, a + nl + nl + '/* Return the directory of a given path entry */' + nl + 'const char *osd_get_path(int pathtype, int pathindex);', 1); wr(p, s); print('updated', p)
for p in ['src/unix/fileio.c', 'src/libpinmame/fileio.c', 'src/windows/fileio.c', 'src/ios/fileio.c']:
    s = rd(p); nl = '\r\n' if '\r\n' in s else '\n'
    if 'const char *osd_get_path(int pathtype' in s: continue
    i = s.find('int osd_get_path_count(int pathtype)'); assert i >= 0, p
    j = s.find(nl + '}' + nl, i) + len(nl + '}' + nl)
    s = s[:j] + nl + 'const char *osd_get_path(int pathtype, int pathindex)' + nl + '{' + nl + '\treturn get_path_for_filetype(pathtype, pathindex, NULL);' + nl + '}' + nl + s[j:]
    wr(p, s); print('updated', p)
p = 'src/wpc/driver.c'; s = rd(p)
a = "DRIVERNV(dominos)       //pinHeck 07/16 Domino's Spectacular Pinball Adventure"
b = "DRIVERNV(pinheck)       //pinHeck System (not a game)"
if b not in s:
    i = s.find(a); assert i >= 0
    nl = '\r\n' if s[i + len(a):i + len(a) + 2] == '\r\n' else '\n'
    s = s.replace(a, b + nl + a, 1); wr(p, s); print('updated', p)
EOF
```
Expected: six `updated` lines; running it again prints nothing.

- [ ] **Step 4: Replace the driver files**

`src/wpc/pinheck.h`:

```c
#ifndef INC_PINHECK
#define INC_PINHECK

#include "core.h"
#include "sim.h"

#define PINHECK_CPUREGION  REGION_CPU1
#define PINHECK_PROPREGION REGION_USER1
#define PINHECK_BIOSREGION REGION_USER2

#define PINHECK_INPUT_PORTS_START(name, balls) \
  INPUT_PORTS_START(name) \
    CORE_PORTS \
    SIM_PORTS(balls)

#define PINHECK_INPUT_PORTS_END INPUT_PORTS_END

#define PINHECK_BIOS_ROMSTART(name) \
  ROM_START(name) \
    ROM_REGION(0x8000, PINHECK_BIOSREGION, 0) \
      ROM_LOAD("p8x32a.rom", 0x0000, 0x8000, CRC(f99b3070) SHA1(b7b4fdf4f096db7d18bda6355725cb42ae4a9378))

#define PINHECK_ROMSTART(name, prg, prgsize, prghash, prp, prphash) \
  PINHECK_BIOS_ROMSTART(name) \
    ROM_REGION(0x80000, PINHECK_CPUREGION, ROMREGION_ERASEFF) \
      ROM_LOAD(prg, 0x0000, prgsize, prghash) \
    ROM_REGION(0x8000, PINHECK_PROPREGION, 0) \
      ROM_LOAD(prp, 0x0000, 0x8000, prphash)

#define PINHECK_ROMEND ROM_END

extern PINMAME_VIDEO_UPDATE(pinheck_video);
extern MACHINE_DRIVER_EXTERN(PINHECK);
#define gl_mPINHECK PINHECK

#endif
```

`src/wpc/pinheckgames.c`:

```c
#include "driver.h"
#include "sim.h"
#include "sndbrd.h"
#include "pinheck.h"

static core_tLCDLayout pinheck_disp[] = {
  {0, 0, 32, 128, CORE_VIDEO, (genf *)pinheck_video, NULL}, {0}
};

#define INIT_PINHECK(name, balls, version) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
static core_tGameData name##GameData = { GEN_PINHECK, pinheck_disp, {0, 0, 0, 0, SNDBRD_NONE, 0, version} }; \
static void init_##name(void) { core_gameData = &name##GameData; }

/*-------------------------------------------------------------------
/ pinHeck system: the Parallax Propeller mask ROM (not a game)
/-------------------------------------------------------------------*/
INIT_PINHECK(pinheck, 3, 0)
PINHECK_BIOS_ROMSTART(pinheck)
PINHECK_ROMEND
GAMEX(2014,pinheck,0,PINHECK,pinheck,pinheck,ROT0,"Spooky Pinball","pinHeck System",NOT_A_DRIVER)

/*-------------------------------------------------------------------
/ Domino's Spectacular Pinball Adventure (2016)
/-------------------------------------------------------------------*/
INIT_PINHECK(dominos, 3, 6)
PINHECK_ROMSTART(dominos, "DOM_V006.PRG", 0x31990, CRC(750e27a4) SHA1(3fbebce7f885563e61dd2e4f5d7f0a8a54c53b23),
                 "PRP_V008.BIN", CRC(a51ee28d) SHA1(0556d88b6f0c7cb15e648e1f76f4d47890ddb858))
PINHECK_ROMEND
CORE_CLONEDEFNV(dominos, pinheck, "Domino's Spectacular Pinball Adventure", 2016, "Spooky Pinball", gl_mPINHECK, GAME_NOT_WORKING)
```

`src/wpc/pinheck.c`:

```c
#include "driver.h"
#include "core.h"
#include "cpu/pic32mx/pic32mxcpu.h"
#include "pinheck/prop.h"
#include "pinheck/rtc.h"
#include "pinheck/sd.h"
#include "pinheck/zipsrc.h"
#include "pinheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PINHECK_CLOCK 80000000
#define PINHECK_LOG_MAX 64
#define PINHECK_ZIP_CACHE (64u << 20)
#define RF5  (1u << 5)
#define RF12 (1u << 12)
#define RF13 (1u << 13)

static pinheck_prop prop;
static cat24m01 u13;
static ds1340 rtc;
static zipsrc zip;
static vfat vol;
static sd_card card;
static uint8_t u13mem[131072], propmem[131072];
static int reset_done, opened;

static struct {
	FILE *uart1, *proplog;
	const char *send;
	uint64_t send_at, rtc_at;
	double reset_at;
	int have_zip, have_vol;
	uint32_t logged[PINHECK_LOG_MAX];
	int nlogged;
} locals;

static void pinheck_uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (uart == 1 && locals.uart1) fputc(byte, locals.uart1);
}

static void pinheck_port_write(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle)
{
	uint32_t drv = lat & ~tris;
	(void)ctx;
	if (port == PIC32MX_PORTF)
		prop_pic_pins(&prop, cycle, (drv & RF12 ? 1u << 25 : 0) | (drv & RF5 ? 1u << 26 : 0));
}

static uint32_t pinheck_port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx;
	if (port != PIC32MX_PORTF) return 0xFFFFu;
	return (0xFFFFu & ~RF13) | (prop_p24(&prop, cycle) ? RF13 : 0);
}

static int pinheck_i2c_pins(void *ctx, int module, int scl, int sda, uint64_t cycle)
{
	(void)ctx;
	if (module != 1) return sda;
	ds1340_tick(&rtc, cycle - locals.rtc_at);
	locals.rtc_at = cycle;
	return cat24m01_update(&u13, scl, sda) & ds1340_update(&rtc, scl, sda);
}

static void pinheck_unmapped(void *ctx, uint32_t pa, int write)
{
	(void)ctx;
	logerror("pinheck: unmodelled SFR %s %08x\n", write ? "write" : "read", (unsigned)(pa | 0xA0000000u));
}

static void pinheck_exception(void *ctx, int code, uint32_t pc)
{
	uint32_t key = pc ^ ((uint32_t)code << 27);
	int i;
	(void)ctx;
	for (i = 0; i < locals.nlogged; i++)
		if (locals.logged[i] == key) return;
	if (locals.nlogged < PINHECK_LOG_MAX) locals.logged[locals.nlogged++] = key;
	logerror("pinheck: PIC32 exception %d at %08x\n", code, (unsigned)pc);
}

static void pinheck_prop_log(void *ctx, const char *msg)
{
	(void)ctx;
	logerror("pinheck: %s\n", msg);
	if (locals.proplog) fprintf(locals.proplog, "%s\n", msg);
}

static int pinheck_blk_read(void *ctx, uint32_t lba, uint8_t *buf) { (void)ctx; return vfat_read(&vol, lba, buf); }
static int pinheck_spi(void *ctx, int cs, int sclk, int mosi) { (void)ctx; return sd_update(&card, cs, sclk, mosi); }

static void pinheck_open_card(void)
{
	char path[1024];
	int i, n = osd_get_path_count(FILETYPE_ROM);
	sd_blockdev dev;

	for (i = 0; i < n && !locals.have_zip; i++) {
		sprintf(path, "%.1000s/%.16s.zip", osd_get_path(FILETYPE_ROM, i), Machine->gamedrv->name);
		if (zipsrc_open(&zip, path, PINHECK_ZIP_CACHE) == 0) locals.have_zip = 1;
	}
	if (!locals.have_zip) { logerror("pinheck: no %s.zip on the ROM path, no SD card\n", Machine->gamedrv->name); return; }
	if (vfat_init(&vol, zipsrc_source(&zip)) != 0) { logerror("pinheck: cannot build the SD volume\n"); return; }
	locals.have_vol = 1;
	dev.ctx = NULL;
	dev.sectors = vfat_sectors(&vol);
	dev.read = pinheck_blk_read;
	sd_init(&card, &dev);
	prop_attach_sd(&prop, pinheck_spi, NULL);
}

static void pinheck_in_service(uint8_t *mem, uint32_t version)
{
	uint32_t w0 = version << 24 | 0xBAFAu, w1 = 0xABBA0002u;
	int i;
	for (i = 0; i < 4; i++) {
		mem[0x8000 + i] = (uint8_t)(w0 >> (8 * i));
		mem[0x8004 + i] = (uint8_t)(w1 >> (8 * i));
	}
}

static int64_t pinheck_local_now(void)
{
	time_t t = time(NULL);
	struct tm u = *gmtime(&t);
	u.tm_isdst = -1;
	return (int64_t)t + (int64_t)difftime(t, mktime(&u));
}

static void pinheck_tick(int param)
{
	pic32mx *soc = pic32cpu_soc();
	(void)param;
	prop_catch_up(&prop, soc->cpu.cycles);
	if (locals.reset_at > 0.0 && timer_get_time() >= locals.reset_at) {
		locals.reset_at = 0.0;
		reset_done = 1;
		machine_reset();
		return;
	}
	if (locals.send && *locals.send && soc->cpu.cycles >= locals.send_at) {
		int n = soc->uart[0].rx_count;
		pic32mx_uart_rx(soc, 0, (uint8_t)*locals.send);
		if (soc->uart[0].rx_count > n) locals.send++;
	}
}

PINMAME_VIDEO_UPDATE(pinheck_video)
{
	(void)layout;
	fillbitmap(bitmap, 0, cliprect);
}

static INTERRUPT_GEN(pinheck_vblank)
{
	core_updateSw(0);
}

static MACHINE_INIT(pinheck)
{
	pic32mx_board board = { NULL, pinheck_port_write, pinheck_port_read, pinheck_uart_tx, pinheck_i2c_pins, pinheck_unmapped, pinheck_exception };
	const char *log = getenv("PINHECK_UART1_LOG"), *plog = getenv("PINHECK_PROP_LOG");

	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
	memset(&locals, 0, sizeof(locals));
	if (log && (locals.uart1 = fopen(log, opened ? "ab" : "wb")) != NULL) setvbuf(locals.uart1, NULL, _IONBF, 0);
	if (plog && (locals.proplog = fopen(plog, opened ? "a" : "w")) != NULL) setvbuf(locals.proplog, NULL, _IONBF, 0);
	opened = 1;
	locals.reset_at = !reset_done && getenv("PINHECK_RESET_AT") ? atof(getenv("PINHECK_RESET_AT")) : 0.0;
	memcpy(propmem, memory_region(PINHECK_PROPREGION), 0x8000);
	prop_init(&prop, memory_region(PINHECK_BIOSREGION), propmem);
	prop_set_log(&prop, pinheck_prop_log, NULL);
	pinheck_open_card();
	pic32cpu_set_board(&board);
}

static MACHINE_RESET(pinheck)
{
	const char *at = getenv("PINHECK_UART1_SEND_AT");
	prop_reset(&prop, 0);
	cat24m01_init(&u13, u13mem, 0);
	ds1340_init(&rtc, pinheck_local_now(), PINHECK_CLOCK);
	locals.rtc_at = 0;
	locals.send = getenv("PINHECK_UART1_SEND");
	locals.send_at = (uint64_t)((at ? atof(at) : 0.0) * PINHECK_CLOCK);
}

static NVRAM_HANDLER(pinheck)
{
	const int first = !read_or_write && !file;
	core_nvram(file, read_or_write, u13mem, sizeof(u13mem), 0xFF);
	core_nvram(file, read_or_write, propmem + 0x8000, sizeof(propmem) - 0x8000, 0xFF);
	if (first) pinheck_in_service(propmem, core_gameData->hw.gameSpecific1);
}

static MACHINE_STOP(pinheck)
{
	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
	locals.uart1 = locals.proplog = NULL;
	if (locals.have_vol) vfat_free(&vol);
	if (locals.have_zip) zipsrc_close(&zip);
	locals.have_vol = locals.have_zip = 0;
}

static MEMORY_READ32_START(pinheck_readmem)
	{ 0x00000000, 0x00000003, MRA32_NOP },
MEMORY_END

static MEMORY_WRITE32_START(pinheck_writemem)
	{ 0x00000000, 0x00000003, MWA32_NOP },
MEMORY_END

MACHINE_DRIVER_START(PINHECK)
	MDRV_IMPORT_FROM(PinMAME)
	MDRV_CORE_INIT_RESET_STOP(pinheck, pinheck, pinheck)
	MDRV_CPU_ADD_TAG("mcpu", PIC32MX, PINHECK_CLOCK)
	MDRV_CPU_MEMORY(pinheck_readmem, pinheck_writemem)
	MDRV_CPU_VBLANK_INT(pinheck_vblank, 1)
	MDRV_TIMER_ADD(pinheck_tick, 1000)
	MDRV_NVRAM_HANDLER(pinheck)
	MDRV_SCREEN_SIZE(128, 32)
	MDRV_VISIBLE_AREA(0, 127, 0, 31)
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_RGB_DIRECT)
MACHINE_DRIVER_END
```

- [ ] **Step 5: Register the sources in every build**

`tests/pinheck/link/register_build.py`:

```python
#!/usr/bin/env python3
import glob
import os
import sys

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))

GROUPS = [
    (['src/cpu/p8x32a/p8x32a.c', 'src/cpu/p8x32a/p8x32a.h', 'src/wpc/pinheck/eeprom.c', 'src/wpc/pinheck/eeprom.h',
      'src/wpc/pinheck/prop.c', 'src/wpc/pinheck/prop.h', 'src/wpc/pinheck/rtc.c', 'src/wpc/pinheck/rtc.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/prop.o $(PINOBJ)/pinheck/eeprom.o $(PINOBJ)/pinheck/rtc.o $(OBJ)/cpu/p8x32a/p8x32a.o\n'),
    (['src/wpc/pinheck/sd.c', 'src/wpc/pinheck/sd.h', 'src/wpc/pinheck/vfat.c', 'src/wpc/pinheck/vfat.h',
      'src/wpc/pinheck/zipsrc.c', 'src/wpc/pinheck/zipsrc.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/sd.o $(PINOBJ)/pinheck/vfat.o $(PINOBJ)/pinheck/zipsrc.o\n'),
]
changed = []


def edit(path, anchor, insert):
    s = open(path, newline='').read()
    if insert.strip() in s:
        return
    if s.count(anchor) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(anchor), anchor))
    s = s.replace(anchor, anchor + insert, 1)
    open(path, 'w', newline='').write(s)
    changed.append(path)


def line_span(s, i):
    start = s.rfind('\n', 0, i) + 1
    end = s.find('\n', i)
    end = len(s) if end < 0 else end + 1
    nl = '\r\n' if s[start:end].endswith('\r\n') else '\n'
    return start, end, nl


def line_after(path, needle, lines):
    s = open(path, newline='').read()
    if s.count(needle) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(needle), needle))
    start, end, nl = line_span(s, s.find(needle))
    indent = s[start:s.find(needle)]
    lines = [l for l in lines if indent + l + nl not in s]
    if not lines:
        return
    s = s[:end] + ''.join(indent + l + nl for l in lines) + s[end:]
    open(path, 'w', newline='').write(s)
    changed.append(path)


def win(p):
    return '..\\' + p.replace('/', '\\')


srcs = []
for files, drvlibs in GROUPS:
    if not all(os.path.exists(f) for f in files):
        continue
    srcs += files
    edit('src/pinmame.mak', 'DRVLIBS += $(PINOBJ)/pinheck.o\n', drvlibs)
edit('src/pinmame.mak', 'OBJDIRS += $(PINOBJ)\n', 'OBJDIRS += $(PINOBJ)/pinheck $(OBJ)/cpu/p8x32a\n')

for path in sorted(glob.glob('cmake/*/CMakeLists*.txt')):
    if 'src/wpc/pinheckgames.c' in open(path).read():
        line_after(path, 'src/wpc/pinheckgames.c', srcs)

for path in sorted(glob.glob('vcproj/*.vcxproj')):
    s = open(path).read()
    if 'pinheckgames.c' not in s:
        continue
    line_after(path, '<ClCompile Include="..\\src\\wpc\\pinheckgames.c" />',
               ['<ClCompile Include="%s" />' % win(p) for p in srcs if p.endswith('.c')])
    line_after(path, '<ClInclude Include="..\\src\\wpc\\pinheck.h" />',
               ['<ClInclude Include="%s" />' % win(p) for p in srcs if p.endswith('.h')])

for path in sorted(glob.glob('vcproj/*.vcxproj.filters')):
    s = open(path, newline='').read()
    anchor = '<ClCompile Include="..\\src\\wpc\\pinheckgames.c">'
    if anchor not in s:
        continue
    if s.count(anchor) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(anchor), anchor))
    start, end, nl = line_span(s, s.find(anchor))
    items = ''.join('    <ClCompile Include="%s">%s      <Filter>Source Files\\PinMAME</Filter>%s    </ClCompile>%s'
                    % (win(p), nl, nl, nl) for p in srcs
                    if p.endswith('.c') and '<ClCompile Include="%s">' % win(p) not in s)
    if not items:
        continue
    s = s[:start] + items + s[start:]
    open(path, 'w', newline='').write(s)
    changed.append(path)

for p in sorted(set(changed)):
    print('updated', p)
```

`tests/pinheck/pic32mx/makefile_link_check.sh`:

```sh
#!/bin/sh
cd "$(dirname "$0")/../../.." || exit 2
B=tests/pinheck/pic32mx/build/mklink
rm -rf $B && mkdir -p $B
objs=$(sed -n '/findstring PIC32MX@/,/^else/p' src/rules.mak | sed -n 's/^CPUOBJS += //p')
[ -n "$objs" ] || { echo "makefile link: no PIC32MX CPUOBJS in src/rules.mak"; exit 1; }
drv=$(sed -n 's/^DRVLIBS += //p' src/pinmame.mak | tr ' ' '\n' | grep -E 'pinheck|p8x32a')
for o in $objs $drv; do
	src=$(echo "$o" | sed 's|\$(PINOBJ)/|src/wpc/|; s|\$(OBJ)/|src/|; s|\.o$|.c|')
	cc -c -std=gnu99 -DPINMAME -DMAMEVER=7300 -DLSB_FIRST '-DINLINE=static __inline__' -DPI=M_PI -DHAS_PIC32MX=1 -Isrc -Isrc/wpc -Isrc/unix -Isrc/unix/sysdep -o $B/$(echo "$src" | tr / _ | sed 's/\.c$/.o/') "$src" || { echo "makefile link: cannot compile $src"; exit 1; }
done
nm --defined-only $B/*.o | awk 'NF == 3 { print $3 }' | sort -u > $B/defined
nm -u $B/*.o | awk '{ print $NF }' | grep -E '^(mips32_|pic32mx_|pic32cpu_|prop_|cat24m01_|p8x32a_|sd_|vfat_|zipsrc_)' | sort -u > $B/needed
missing=$(comm -23 $B/needed $B/defined)
if [ -n "$missing" ]; then echo "makefile link: FAIL, pinHeck objects leave undefined: $missing"; exit 1; fi
echo "makefile link: ok"
```

Run: `python3 tests/pinheck/link/register_build.py | wc -l` → `19`; again → `0`. Then `tests/pinheck/pic32mx/makefile_link_check.sh` → `makefile link: ok`.

- [ ] **Step 6: Build**

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
cmake --build build/sdl3pinmame -j"$(nproc)" 2>&1 | tee build/sdl3pinmame/build.log | tail -1
grep -cE '(pinheck|prop|rtc|sd|vfat|zipsrc|eeprom|p8x32a)\.c:[0-9]+:[0-9]+: (warning|error)' build/sdl3pinmame/build.log
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 7: Run the PinMAME check**

Run: `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && SDL3PINMAME=build/sdl3pinmame/sdl3pinmame tests/pinheck/pic32mx/pinmame_check.sh`
Expected (about 95 s): `pinmame: ok`. `tests/pinheck/pic32mx/build/pinmame/uart1.log` holds two `PROPELLER SYNC CHECK......OK`, the banner, `Version: 006`, today's local date and time, and `Ball Search: DISABLED`; `nvram/dominos.nv` is 229,468 bytes.

- [ ] **Step 8: Full regression**

Run: `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && tests/pinheck/mips32/check.sh && tests/pinheck/pic32mx/check.sh && tests/pinheck/link/check.sh && tests/pinheck/p8x32a/check.sh`
Expected: `mips32: 208 passed, 0 failed`, `pic32mx: 0 failed` (with `makefile link: ok`), `link: 0 failed`, `p8x32a: … passed, 0 failed`.

- [ ] **Step 9: Commit**

```bash
git add src/osdepend.h src/unix/fileio.c src/libpinmame/fileio.c src/windows/fileio.c src/ios/fileio.c src/wpc/driver.c \
        src/wpc/pinheck.h src/wpc/pinheck.c src/wpc/pinheckgames.c src/pinmame.mak cmake vcproj \
        tests/pinheck/link/register_build.py tests/pinheck/pic32mx/makefile_link_check.sh tests/pinheck/pic32mx/pinmame_check.sh
git commit -m "pinheck: Propeller, SD card, RTC and NVRAM in PinMAME; pinheck parent set"
```

### Task 5: Record the findings

**Files:**
- Modify: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§2.3, §4.4–§4.6, §5.1–§5.3, §7, §10), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`

- [ ] **Step 1: Update the spec and roadmap**

Run after Plan 4's Task 6 (it edits other lines of the same sections):

```sh
python3 - docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md <<'EOF'
import sys
prev = {"minus": ["- **`prop.c`** owns the Propeller instance and the shared-pin edge log (timestamped PIC32 writes to Propeller-input pins). It converts PIC32 cycles to Propeller cycles by ×13/10 and implements the catch-up rules in §5.", "- `pinheck`: a BIOS set holding `p8x32a.rom` (32 KB, hub `$8000–$FFFF`), CRC-audited.", "PinMAME schedules the PIC32 at 80 MHz. Propeller cycle = PIC32 cycle × 13 / 10, exactly. `prop_run_until(t)` never moves backwards.", "Every PIC32 write that changes a pin wired to a Propeller input is appended to the edge log with its PIC32 timestamp. The Propeller's `INA` is evaluated at the reading cog's local cycle from the log. `WAITPEQ`/`WAITPNE` resolve to the cycle of the first matching edge. Appending to a full log first runs `prop_run_until(now)`, so edges are never dropped. Entries older than the Propeller's current cycle are discarded.", "- at every periodic quantum timer, so the Propeller keeps producing video and audio while the PIC32 is not reading it"], "plus": ["- **`prop.c`** owns the Propeller instance, its CAT24M01 on P28/P29 and the shared-pin edge log (PIC32 writes to P25/P26, stored in PIC32 cycles). It converts PIC32 cycles to Propeller cycles through the clock map of §5.1 and implements the catch-up rules of §5.2–§5.3. Undriven pins read 0 except where the board pulls them up: SD `DO` and `CS`, P13 and SCL/SDA; P31 (RX) floats and reads 0. The SD card attaches through a `(cs, sclk, mosi) → DO` callback. A `CLKSET` with the reset bit restarts the chip at the end of the catch-up in which it executed.", "- `pinheck`: a `NOT_A_DRIVER` parent set holding `p8x32a.rom` (32 KB, hub `$8000–$FFFF`, CRC32 `f99b3070`, SHA1 `b7b4fdf4f096db7d18bda6355725cb42ae4a9378`). It is GPL 3.0 and user-supplied, never committed. `dominos` is its clone, so PinMAME finds the ROM in `dominos.zip` or `pinheck.zip`.", "PinMAME schedules the PIC32 at 80 MHz. The Propeller clock follows its own `CLKSET`: after reset it runs on RCFAST, taken as the nominal 12 MHz (3/20 of the PIC32 rate); XIN and PLL1×–16× derive from the board's 6.5 MHz crystal, so PLL16× (`CLKSET $6F`, 104 MHz) is exactly 13/10. `prop.c` keeps a piecewise-linear map from PIC32 cycles to Propeller cycles with one segment per `CLKSET`. PIC32 cycles are offset by an epoch that grows by the PIC32's cycle count at each PinMAME reset, so Propeller time never moves backwards.", "Every PIC32 write that changes a pin wired to a Propeller input is appended to the edge log with its PIC32 timestamp; it becomes visible at Propeller cycle ⌊map(P)⌋, converted when the Propeller reads it, so a `CLKSET` between logging and use is honoured. The Propeller's `INA` is evaluated at the reading cog's local cycle from the log. `WAITPEQ`/`WAITPNE` resolve to the cycle of the first matching edge. A catch-up for PIC32 cycle R runs the Propeller to map(R) − 1, so a later PIC32 write always lands on a Propeller cycle not yet evaluated; results are then identical whether the Propeller is caught up every cycle or once. Appending to a full log first catches the Propeller up to that write, so edges are never dropped. Entries at or before the Propeller's current cycle are folded into its base pin state.", "- at every tick of a 1 kHz PinMAME timer, so the Propeller keeps producing video and audio while the PIC32 is not reading it"]}
spec_pairs = list(zip(prev['minus'], prev['plus']))
spec_pairs += [
    ("Per the wiki: the PIC32 keeps its non-volatile data in the top half of the Propeller's EEPROM, and the PIC32 main loop runs at about 10 kHz.",
     "Per the wiki: the PIC32 keeps its non-volatile data in the top half of the Propeller's EEPROM, and the PIC32 main loop runs at about 10 kHz. It also keeps its settings in U13 on I2C1, next to the DS1340.\n\n"
     "Before serving the link, the Propeller checks its EEPROM long `$8000`. If its low 16 bits are not `$BAFA`, or P13 (`PROP_CONFIG`) is held low, or the long at `$8004` is `$ABBA0001` (written by the PIC32's `UPDATE CODE` menu), it runs its PIC32 update: `FLASHING TO:`, then a serial driver on P24 (RX) / P25 (TX) at 115,200 baud sends an STK500v2 `CMD_SIGN_ON` (`1B 00 00 01 0E 01 15`) to the PIC32 bootloader and waits without timeout for an answer with status 0 and a signature starting `ST` (`STK500_2`). It then flashes the `.PRG` from the card, writes `$8004 = $ABBA0002` and `$8000 = (v << 24) | $BAFA` (v = the three digits after `_V` in the file name, 6 for `DOM_V006.PRG`), shows `PLEASE RESTART` and waits for a power cycle. The Propeller does not drive MCLR or RG9. The open-source `Bootloader_Max32.hex` (\"Explorer stk500V2 by MLS\") talks on UART1, not on RF12/RF13, so it cannot answer this path."),
    ("- **`rtc.c`** is a DS1340 seeded from the host clock.",
     "- **`rtc.c`** is a DS1340 on I2C1 (device `$68`), sharing the bus with U13 (wired-AND). It is seeded from the host's local time at every reset and advances with PIC32 cycles; writes set the clock. Without it the firmware stops on the `SET DATETIME` screen on every boot after the first."),
    ("- **NVRAM**: both EEPROM images. The DS1340 is not persisted.",
     "- **NVRAM**: U13 (128 KB) and the Propeller EEPROM above the 32 KB `PRP_V008.BIN` image, through PinMAME's NVRAM handler. A first run starts with U13 blank and the Propeller EEPROM blank except for the in-service record `$8000 = (v << 24) | $BAFA`, `$8004 = $ABBA0002`, `v` = `gameSpecific1` (decision D1, §10). The DS1340 is not persisted. The first boot on a blank U13 stores defaults and halts on the firmware's factory-restart screen, exactly as the real machine does; the next boot is the normal one."),
    ("2. `PROPELLER SYNC CHECK` succeeds, and `[E00000]` returns the version.",
     "2. `PROPELLER SYNC CHECK` succeeds against the emulated Propeller on both boots, and `[E97000]` injected on UART1 returns `Ball Search: DISABLED`. (`[E00000]`, which the help text documents as returning the version, prints an empty line in `DOM_V006`: its handler prints the empty string at `0x9D00D9D0`.)"),
    ("- The PIC32 boot-flash bootloader and the firmware-update path (`UPDATE CODE`, Propeller-driven PIC32 reflash).",
     "- The PIC32 boot-flash bootloader and the firmware-update path (`UPDATE CODE`, Propeller-driven PIC32 reflash). Decision D1: the emulated machine starts in the in-service state (§4.5), as a machine that has already run its update, so the Propeller skips the update path (§2.3). Taking `UPDATE CODE` in the service menu writes `$ABBA0001` and the next boot will then wait forever on the missing bootloader."),
]
p = sys.argv[1]
s = open(p).read()
for a, b in spec_pairs:
    if a not in s:
        sys.exit('spec anchor missing: ' + a[:70])
    s = s.replace(a, b, 1)
open(p, 'w').write(s)
print('spec updated')

roadmap = [
    ("| m5 | 5: link: edge log, catch-up on reads of RF13 | Plans 2, 3 | needs **1** | machine check 2: `PROPELLER SYNC CHECK`, `[E00000]` round trip | after Plans 2–4 |",
     "| `2026-09-28-pinheck-m5-link.md` | 5: link: edge log, catch-up on reads of RF13, DS1340, EEPROM NVRAM, `pinheck` BIOS set | Plans 2, 3, 4 (executed) | needs **1**; decision **D1** | machine check 2: `PROPELLER SYNC CHECK` on both boots, `[E97000]` → `Ball Search: DISABLED` | written |"),
    ("- NVRAM for both CAT24M01 images; without it every PinMAME launch is a first boot and the banner never appears.\n",
     ""),
    ("## Carried into Plan 7\n",
     "## Decision D1 (Plan 5): initial Propeller EEPROM state\n\n"
     "The Propeller runs its PIC32 update path, which needs the PIC32 bootloader, unless EEPROM `$8000` holds `$BAFA` (spec §2.3). Plan 5 starts the emulated machine in the state of a machine that has already run its update. The alternatives were to emulate the missing bootloader (its binary is not available; the open-source one talks on UART1, not on the link) or a minimal STK500v2 responder that lets the Propeller reflash the PIC32 on every first run.\n\n"
     "## Carried into Plan 6\n\n"
     "- The Propeller's framebuffer is readable in hub RAM around `$57EE`–`$67ED` (128×32 RGB332, row-wrapped); it shows the boot and update screens, which is a useful cross-check for the display decoder.\n\n"
     "## Carried into Plan 7\n"),
]
p = sys.argv[2]
if p:
    s = open(p).read()
    for a, b in roadmap:
        if a not in s:
            sys.exit('roadmap anchor missing: ' + a[:70])
        s = s.replace(a, b, 1)
    open(p, 'w').write(s)
    print('roadmap updated')
EOF
```
Expected: `spec updated`, `roadmap updated`.

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "Spec and roadmap: Propeller update path, decision D1, link timing, RTC, NVRAM"
```
