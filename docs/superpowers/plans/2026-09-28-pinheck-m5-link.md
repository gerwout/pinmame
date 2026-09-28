# Milestone 5: PIC32 ↔ Propeller link in PinMAME Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Both CPUs run together inside PinMAME. On blank NVRAM the Propeller's own update path runs against a stand-in for the PIC32 bootloader, reprograms the PIC32 flash from the card and stops at `PLEASE RESTART`; after a restart the PIC32 firmware passes `PROPELLER SYNC CHECK`, answers commands on its UART1 console, and keeps its settings across runs.

**Architecture:** `src/wpc/pinheck/prop.c` wraps the `p8x32a` core as a device the driver advances: PIC32 writes to P24–P26 go into a timestamped edge log, the Propeller is caught up before every PIC32 read of RF13, on a 1 kHz timer and while the PIC32 is held, and PIC32 cycles map to Propeller cycles through a piecewise clock map that follows the Propeller's `CLKSET`. `src/wpc/pinheck/bootldr.c` stands in for the PIC32 bootloader: after every PIC32 reset it holds the core through a new `pic32mx_board.hold` callback, speaks STK500v2 on RF12/RF13 against the emulated program flash, and then starts the application at `0x9D001000`. The driver wires U13 and a new DS1340 model onto I2C1, the Propeller's CAT24M01 onto P28/P29 and Plan 4's SD card onto P0–P3 (now exposing the romset's root files), persists both EEPROMs as NVRAM, and declares the Propeller mask ROM as a `pinheck` parent set.

**Tech Stack:** C99 (C89-syntax-clean for `prop.c`/`rtc.c`/`bootldr.c`), PinMAME 0.37 driver interfaces, CMake/makefiles/VS projects, OpenSpin (external test tool), Python 3, POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§2.3, §4.2, §4.4–§4.6, §5.1–§5.3, §6, §7 check 2, §10). Roadmap: `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`.

## Prerequisites

- **Plan 4 executed and merged** on `pinheck`: `9c485e8c` (romset zip reader), `c4efd760` (FAT32 volume), `77601190` (SD card), `aad0e1d0` (NCO counter outputs), `ef619f48` (SD boot test), `5f040038` (spec §4.3/§4.4). This plan uses `src/wpc/pinheck/sd.[ch]` and `zipsrc.[ch]` unchanged, the NCO outputs in `src/cpu/p8x32a/p8x32a.[ch]` (the firmware's SD clock and its serial TX), and changes one line of `vfat.c` (Task 4). `tests/pinheck/link/check.sh` stops with `PLAN 4 MISSING` otherwise.
- The mask ROM `p8x32a.rom` (CRC32 `f99b3070`, GPL 3.0, never committed), the unzipped Domino's update, and the OpenSpin build from `tests/pinheck/p8x32a/tools.sh`. The commands below use:

```sh
export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip
```

`PINHECK_ZIP` is optional: a Domino's update zip (stored or deflated). Without it the tests build a stored one from `PINHECK_UPDATE_DIR` once (about 800 MB, under the ignored `build/`).

## Decision D1: the PIC32 bootloader (decided: stand-in)

Established by emulating `PRP_V008.BIN` with the card attached, reading its Spin bytecode (spinsim's disassembler), its framebuffer and the STK500v2 traffic:

- **What the Propeller does.** After mounting the card it reads EEPROM long `$8000`. If the low 16 bits are not `$BAFA`, or P13 (`PROP_CONFIG`, pulled up) reads low, or long `$8004` is `$ABBA0001` (the PIC32's `UPDATE CODE` action writes it through `writeEEPROM`, word 1), it shows `FLASHING TO:` and starts a full-duplex serial driver with RX on P24 (PIC32 RF13) and TX on P25 (PIC32 RF12) at 115,200 baud. About 1.7 s after power-on it sends `CMD_SIGN_ON` (`1B 00 00 01 0E 01 15`) and waits for a reply without any timeout. A reply with status 0 and a signature starting `ST` gets `CONNECT PIC32:OK`; `AVRISP_2` gets `CONNECT PIC32:FAIL`. It then clears `$8004`, loads address 0, programs the card's `DOM_V006.PRG` with `CMD_PROGRAM_FLASH_ISP` (`$13`), reads it back with `CMD_READ_FLASH_ISP` (`$14`), reprograms its own EEPROM from the card's `PRP_V008.BIN`, writes `$8004 = $ABBA0002` and `$8000 = (v << 24) | $BAFA` (v = the digits after `_V`, 6 for `DOM_V006.PRG`), sends `CMD_LEAVE_PROGMODE_ISP` (`$11`) and shows `PLEASE RESTART`, about 135 s after power-on. It never serves the game link in that power cycle. It sends no other commands.
- **With the record present** the Propeller skips all of this and leaves P24/P25 to the link, and the PIC32 prints `PROPELLER SYNC CHECK......OK`.
- **Reset line.** The Propeller drives neither MCLR nor RG9 (`PIC_PROGRAM_SW`). Both chips leave power-on reset together, so the PIC32 bootloader must still be listening 1.7 s later.
- **Link pins.** RF12 is U3BRX and RF13 is U3BTX, both UART5 on the PIC32MX795.
- **No bootloader binary is available.** Spooky's production bootloader has not been dumped (no hardware access). The open-source `Bootloader_Max32.hex` (LonghornEngineer/Pinheck_Pinball_System) talks on UART1, not UART5, and is CC BY-NC-SA 4.0.

**Decision (user): option (b), a high-level stand-in.** `bootldr.c` answers the five commands above. It needs no ROM, and the Propeller's update path runs unmodified: first run on blank NVRAM, and `UPDATE CODE`. Option (c) (seeding the EEPROM record in blank NVRAM) is kept only as a test fixture (`-inservice`, `PINHECK_INSERVICE`).

## Decision D2: program flash written by an update (ruling)

- The stand-in writes into the program flash PinMAME loaded from the romset (`DOM_V006.PRG` in `REGION_CPU1`). The writes persist across PinMAME resets within a session. Every launch reloads the romset's `DOM_V006.PRG`; nothing is written back to disk.
- The card is built from the same romset zip, so the card's `.PRG` is the romset's `.PRG` and a reload loses nothing. If they differ (a stale flash image, or a romset edited between launches), the update reprograms every page it writes to the card's image. `-stale` in Task 5 corrupts one byte in every 4 KB page before an `UPDATE CODE` run and checks that the flash equals `DOM_V006.PRG` afterwards.
- A romset replaced by a later version with NVRAM from the old one keeps its record, so the Propeller does not update. The PIC32 runs the new `.PRG`, loaded from the romset at launch.

## Global Constraints

- `src/wpc/pinheck/prop.c`, `rtc.c` and `bootldr.c` include only standard C headers and their own/core headers (no PinMAME headers) and pass `cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only`.
- Propeller clock map: RCFAST = 12 MHz nominal (3/20 of PIC32 cycles), RCSLOW 20 kHz (1/4000), XINPUT/XTAL×1–×16 from the 6.5 MHz crystal; `CLKSET $6F` (PLL16) is exactly 13/10. A `CLKSET` with bit 7 restarts the Propeller.
- Propeller pin inputs: P24/P25/P26 from the PIC32 side (the stand-in's TX, RF12, RF5), P0 from the SD card, P28/P29 from its CAT24M01; SD `DO`, `CS`, P13 and SCL/SDA read high when undriven; P31 reads 0.
- Link timing rule: a PIC32-side pin write logged at PIC32 cycle P is seen by the Propeller at ⌊map(P)⌋; catching up for PIC32 cycle R advances the Propeller to map(R) − 1.
- The card exposes the romset zip's root files as well as `DMD/` and `SFX/`; other directories stay excluded.
- The `pinheck` set is `NOT_A_DRIVER` with `p8x32a.rom` (CRC32 `f99b3070`, SHA1 `b7b4fdf4f096db7d18bda6355725cb42ae4a9378`); `dominos` is its clone with `gameSpecific1` = 6 (its code version).
- NVRAM: U13 (131,072 bytes) then the Propeller EEPROM from `$8000` (98,304 bytes); DS1340 seeded from host local time at every reset, not persisted.
- **Bootloader stand-in assumptions** (not evidenced: no bootloader binary; each is a constant or one rule in `bootldr.[ch]`):
  - A1. The boot window is 3 s (`BOOT_WINDOW`) after every PIC32 reset, including resets after the update. It must exceed the 1.7 s sign-on delay. Without a sign-on the application starts at the end of the window, so every boot of an installed machine starts 3 s later than without a bootloader (the firmware's first UART1 byte comes 3.3 s after reset instead of 0.3 s).
  - A2. UART5 runs at BRG 42 on an 80 MHz peripheral clock: 688 PIC32 cycles per bit (116,279 baud, 1 % from the Propeller's 115,200). 8N1, sampled mid-bit.
  - A3. Each reply starts 100 µs after the frame's stop bit (`BOOT_LATENCY`). `PROGRAM_FLASH_ISP` adds 2 ms (`BOOT_PROGRAM`).
  - A4. `CMD_SIGN_ON` answers `STK500_2` (the Propeller checks only the `ST`).
  - A5. `LOAD_ADDRESS` carries a byte offset into program flash (`0x9D000000` + offset). Consistent with the traffic: the programmed image equals `DOM_V006.PRG` byte for byte.
  - A6. A 4 KB page (the PIC32MX795 erase page) is erased to `$FF` on its first write after a reset. The Propeller sends no erase command.
  - A7. Commands other than `$01`, `$06`, `$11`, `$13`, `$14` answer `STATUS_CMD_FAILED` (`$C0`) and are logged once each. Frames with a bad checksum or header are dropped and counted.
  - A8. After `LEAVE_PROGMODE_ISP` the application starts as soon as the reply's stop bit has been sent.
  - A9. The PIC32 sees an undriven P25 as high (UART idle).
  - A10. The stand-in drives RF13 only while it holds the core; at hand-over it releases it (Propeller bit 24 = 0), and the application uses RF12/RF13 as GPIO.
- Test-only fixtures: `linkmachine -inservice V` (seeds the EEPROM record, option (c)), `-updatecode` (seeds `$8004 = $ABBA0001`, as `UPDATE CODE` does), `-stale` (corrupts the flash before the run); driver environment hooks `PINHECK_UART1_LOG`, `PINHECK_PROP_LOG`, `PINHECK_UART1_SEND` / `PINHECK_UART1_SEND_AT` (seconds), `PINHECK_RESET_AT` (seconds, once per process) and `PINHECK_INSERVICE` (version; seeds blank NVRAM with the record).
- PinMAME is built into the ignored `build/sdl3pinmame`, never into `build-dbg`.
- Short code comments only.

## Review Focus

- **First boot on blank NVRAM goes through the update to `PLEASE RESTART`**: the Propeller signs on, the stand-in programs and verifies all 204,288 bytes, the Propeller writes `$8000 = $0600BAFA`, `$8004 = $ABBA0002` and does not serve the link. `machine_check.sh` (Task 5, fresh run, boot 1) and `pinmame_check.sh` launch 1 (Task 6).
- **The next boot runs the game**: sync check, banner and `[E97000]` after a reset (boots 2–3 of the fresh run; `PINHECK_RESET_AT=138`) and after a relaunch with persisted NVRAM (`pinmame_check.sh` launch 2, which must not sign on again).
- **`UPDATE CODE` triggers the update again**: `$8004 = $ABBA0001` on an installed machine with a corrupted flash (`-inservice 6 -updatecode -stale`, Task 5) ends with the record rewritten and the flash equal to `DOM_V006.PRG`.
- **The application does not run while the boot window is open**: `window_without_host` and `sign_on_holds` in Task 3; the first UART1 byte of fresh boot 1 comes after the update (≥ 135.0 s: release at 134.7 s plus the 0.3 s the firmware takes to print), not at 3.3 s.
- **A Propeller that never signs on lets the application start at the right time**: boots 2–3 (record present) print their first UART1 byte 3.25–3.35 s after reset (3 s window + 0.3 s); `window_without_host` checks the hand-over cycle exactly.
- **A reset re-drives RF13**: `prop_reset` clears the PIC32-side pins, so `boot_reset` must drive RF13 idle high again; otherwise the Propeller reads the stand-in's replies on a line idling low and the update stalls after the sign-on. `reset_redrives_idle` in Task 3; the update run in Task 5 updates in its second boot; `pinmame_check.sh` (every PinMAME start resets both chips).
- **PIC32 edges logged before the Propeller runs, across a `CLKSET`**, and **a full edge log**: `echo_exact`, `clkset_lazy`, `full_log` in Task 1.
- **A PinMAME reset** (PIC32 cycles restart at 0, Propeller time must not go backwards): `reset_rebases`, `clkset_reset_bit` in Task 1; `PINHECK_RESET_AT` in Task 6.
- **No RTC answer** on I2C1: the firmware then stops on `SET DATETIME`. `rtc_test.c` in Task 2 and Task 5, step 5.

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/pinheck/prop.[ch]` | Propeller device: clock map, edge log, catch-up, P24 read, P25 TX callback, CAT24M01 on P28/P29, SD attach |
| `src/wpc/pinheck/rtc.[ch]` | DS1340 on I²C |
| `src/wpc/pinheck/bootldr.[ch]` | PIC32 bootloader stand-in: boot window, STK500v2 on UART5, flash programming |
| `src/cpu/pic32mx/pic32mx.[ch]` | `hold` board callback |
| `src/wpc/pinheck/vfat.c` | root files on the card |
| `src/wpc/pinheck.c`, `pinheck.h`, `pinheckgames.c`, `src/wpc/driver.c` | driver wiring, NVRAM, `pinheck` parent set, test hooks |
| `src/osdepend.h`, `src/{unix,libpinmame,windows,ios}/fileio.c` | `osd_get_path()` so the driver can open the romset zip for the card |
| build files | registration of `prop`, `rtc`, `bootldr`, `eeprom`, `sd`, `vfat`, `zipsrc`, `p8x32a` |
| `tests/pinheck/link/*` | device tests, headless two-CPU machine, build registration |
| `tests/pinheck/pic32mx/*`, `tests/pinheck/storage/*` | updated checks |

---
### Task 1: Propeller device

**Files:**
- Create: `src/wpc/pinheck/prop.h`, `src/wpc/pinheck/prop.c`
- Create: `tests/pinheck/link/echo.spin`, `prop_test.c`, `check.sh`, `.gitignore`

**Interfaces:**
- Consumes: `p8x32a_init/reset/run_until/pins` and `p8x32a_bus` (Plan 3), `cat24m01_init/update` (Plan 3).
- Produces: `prop_init(pinheck_prop *p, const uint8_t *rom32k, uint8_t *eemem)` (`eemem` is 131,072 bytes; its first 32 KB must hold `PRP_V008.BIN`), `prop_attach_sd(p, int (*fn)(void *ctx, int cs, int sclk, int mosi), ctx)`, `prop_set_log(p, void (*fn)(void *ctx, const char *msg), ctx)`, `prop_set_tx(p, void (*fn)(void *ctx, uint64_t pic_cycle, int level), ctx)` (every change of the P25 level the PIC32 sees, undriven = 1), `prop_reset(p, uint64_t pic_cycle)`, `prop_pic_pins(p, uint64_t pic_cycle, uint32_t pins)` (pins as Propeller bits 24–26), `prop_catch_up(p, uint64_t pic_cycle)`, `int prop_p24(p, uint64_t pic_cycle)`, `uint64_t prop_time(p, uint64_t pic_cycle)`. Log messages include `prop: CLKSET xx` for every `CLKSET`.

`echo.spin` is the Propeller side of the tests: it switches to PLL16, waits for each rising edge on P25, copies P26 to P24, and records `CNT` of every wake in hub RAM, so a test can see exactly when each PIC32 edge reached the Propeller. The P25 TX callback is exercised end to end by Task 5 (the stand-in only receives the sign-on through it; step 6 there).

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
for f in prop.c rtc.c bootldr.c; do [ -f $S/wpc/pinheck/$f ] || continue; cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/$f || { echo "C89 FAIL $f"; fail=$((fail + 1)); }; done
[ -x "$TOOLS/openspin/build/openspin" ] || { echo "TOOLS MISSING: run ../p8x32a/tools.sh or set TOOLS"; exit 2; }
"$TOOLS/openspin/build/openspin" -q echo.spin -o $B/echo.binary > $B/echo.log 2>&1 || { cat $B/echo.log; exit 2; }
python3 ../p8x32a/mkrom.py $B/echo.binary $B/echo.rom $B/echo.ram || exit 2
cc $CF -o $B/prop_test prop_test.c $CORE || exit 2
./$B/prop_test $B/echo.rom $B/echo.ram || fail=$((fail + 1))
if [ -f bootldr_test.c ]; then
	cc $CF -o $B/bootldr_test bootldr_test.c $S/wpc/pinheck/bootldr.c || exit 2
	./$B/bootldr_test || fail=$((fail + 1))
fi
if [ -f rtc_test.c ]; then
	cc $CF -o $B/rtc_test rtc_test.c $S/wpc/pinheck/rtc.c || exit 2
	./$B/rtc_test || fail=$((fail + 1))
fi
if [ -f machine.c ]; then
	if [ -n "$PINHECK_UPDATE_DIR" ] && [ -n "$P8X32A_ROM" ]; then
		cc $CF -o $B/linkmachine machine.c $CORE $S/wpc/pinheck/rtc.c $S/wpc/pinheck/bootldr.c $S/cpu/pic32mx/pic32mx.c $S/cpu/mips32/mips32.c $SDSRC || exit 2
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
#define PROP_PIC_PINS ((1u << 24) | (1u << 25) | (1u << 26))

typedef int (*prop_spi_fn)(void *ctx, int cs, int sclk, int mosi);
typedef void (*prop_log_fn)(void *ctx, const char *msg);
typedef void (*prop_tx_fn)(void *ctx, uint64_t pic_cycle, int level);

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
} pinheck_prop;

void prop_init(pinheck_prop *p, const uint8_t *rom32k, uint8_t *eemem);
void prop_attach_sd(pinheck_prop *p, prop_spi_fn fn, void *ctx);
void prop_set_log(pinheck_prop *p, prop_log_fn fn, void *ctx);
void prop_set_tx(pinheck_prop *p, prop_tx_fn fn, void *ctx);
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
#define PIN_P25  (1u << 25)
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
	int tx = (dir & PIN_P25) ? (out & PIN_P25) != 0 : 1;
	if (p->tx && tx != p->tx_level) {
		p->tx_level = tx;
		p->tx(p->tx_ctx, to_pic(p, t), tx);
	}
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

void prop_set_tx(pinheck_prop *p, prop_tx_fn fn, void *ctx)
{
	p->tx = fn;
	p->tx_ctx = ctx;
	p->tx_level = 1;
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

The firmware probes device `$68` at every boot; without an answer it stops on the `SET DATETIME` screen on every boot after the first and never reads its console. Register layout per the DS1340 datasheet: seconds (bit 7 = /EOSC), minutes, hours, day 1–7, date, month, year (BCD), control, trickle, flags.

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

### Task 3: SoC hold and the bootloader stand-in

**Files:**
- Modify: `src/cpu/pic32mx/pic32mx.h`, `src/cpu/pic32mx/pic32mx.c`, `tests/pinheck/pic32mx/soc_test.c`, `tests/pinheck/pic32mx/boot.c` (scripted)
- Create: `tests/pinheck/link/bootldr_test.c`, `src/wpc/pinheck/bootldr.h`, `src/wpc/pinheck/bootldr.c`

**Interfaces:**
- Produces: `pic32mx_board.hold`, `uint64_t (*hold)(void *ctx, uint64_t cycle)`, a new last member: before each slice `pic32mx_run` asks it how many cycles the core must stay held; it advances time by that (at most to the end of the run) without executing, 0 = run. Positional board initialisers gain a trailing `NULL`.
- Produces: `boot_init(pic32_boot *b, uint8_t *flash, uint32_t flash_size, boot_tx_fn tx, void *ctx)` (`tx(ctx, pic_cycle, level)` reports every RF13 level change), `boot_set_log(b, fn, ctx)`, `boot_reset(b, uint64_t pic_cycle)` (call after every PIC32 reset, after `prop_reset`: opens the window and re-drives RF13 idle high, since `prop_reset` clears the PIC32-side pins), `boot_rx(b, pic_cycle, level)` (every P25 change seen by RF12), `boot_advance(b, uint64_t now)` (decodes received bytes up to `now`, answers frames, ends the window or releases the application), `uint64_t boot_hold(b, now)` (cycles to hold, at most `BOOT_POLL`; 0 once the application runs). Log lines: `boot: sign-on 0`, `boot: leave, N bytes programmed`, `boot: unsupported STK500v2 command xx`. Counters `frames`, `programmed`, `read`, `errors`.

The owner of the PIC32 calls `prop_catch_up`, then `boot_advance`, then returns `boot_hold` from its `hold` callback, so the Propeller's TX edges up to the current cycle are decoded before each hold decision (spec §5.3).

- [ ] **Step 1: Write the hold test**

Adds `board_hold` to `soc_test.c` (a reserved instruction at the reset vector shows when the core first executes) and the trailing `NULL` to both board initialisers:

```sh
python3 - <<'EOF'
import sys
TEST = '''static uint64_t hold_until;

static uint64_t b_hold(void *c, uint64_t cy) { (void)c; return cy < hold_until ? hold_until - cy : 0; }

static void board_hold(void)
{
	setup();
	put(0x1000, 0x0000003Fu);
	soc.board.hold = b_hold;
	hold_until = 1000;
	pic32mx_run(&soc, 900);
	CHECK(rec.exc_count == 0 && soc.cpu.cycles == 900);
	pic32mx_run(&soc, 200);
	CHECK(rec.exc_count > 0 && rec.exc_pc == 0x9D001000u);
}

int main(void)
'''
edits = [
    ('tests/pinheck/pic32mx/soc_test.c', '{ NULL, b_port_write, b_port_read, b_uart_tx, NULL, b_unmapped, b_exception };', '{ NULL, b_port_write, b_port_read, b_uart_tx, NULL, b_unmapped, b_exception, NULL };'),
    ('tests/pinheck/pic32mx/soc_test.c', 'int main(void)\n', TEST),
    ('tests/pinheck/pic32mx/soc_test.c', '\treserved_instruction();\n', '\treserved_instruction();\n\tboard_hold();\n'),
    ('tests/pinheck/pic32mx/boot.c', '{ NULL, port_write, port_read, uart_tx, i2c_pins, unmapped, exception };', '{ NULL, port_write, port_read, uart_tx, i2c_pins, unmapped, exception, NULL };'),
]
for p, a, b in edits:
    s = open(p).read()
    if b in s: continue
    if s.count(a) != 1: sys.exit('%s: anchor not found' % p)
    open(p, 'w').write(s.replace(a, b, 1)); print('updated', p)
EOF
```

Expected: three `updated tests/pinheck/pic32mx/soc_test.c` lines and `updated tests/pinheck/pic32mx/boot.c`; running it again prints nothing.

- [ ] **Step 2: Run to confirm it fails**

Run: `PINHECK_SKIP_FIRMWARE=1 tests/pinheck/pic32mx/check.sh`
Expected: exit 2 with `error: excess elements in struct initializer` and `'pic32mx_board' has no member named 'hold'`.

- [ ] **Step 3: Add the callback**

```sh
python3 - <<'EOF'
import sys
edits = [
    ('src/cpu/pic32mx/pic32mx.h', '\tvoid (*exception)(void *ctx, int code, uint32_t pc);\n} pic32mx_board;', '\tvoid (*exception)(void *ctx, int code, uint32_t pc);\n\tuint64_t (*hold)(void *ctx, uint64_t cycle); /* cycles the core must stay held, 0 = run */\n} pic32mx_board;'),
    ('src/cpu/pic32mx/pic32mx.c', '\t\tuint64_t slice = end - p->cpu.cycles, ev = next_timer_event(p), e2 = next_i2c_event(p);\n', '\t\tuint64_t slice = end - p->cpu.cycles, ev = next_timer_event(p), e2 = next_i2c_event(p);\n\t\tuint64_t h = p->board.hold ? p->board.hold(p->board.ctx, p->cpu.cycles) : 0;\n\t\tif (h) {\n\t\t\tp->cpu.cycles += h < slice ? h : slice;\n\t\t\tcontinue;\n\t\t}\n'),
]
for p, a, b in edits:
    s = open(p).read()
    if b in s: continue
    if s.count(a) != 1: sys.exit('%s: anchor not found' % p)
    open(p, 'w').write(s.replace(a, b, 1)); print('updated', p)
EOF
```

Expected: two `updated` lines.

- [ ] **Step 4: Run to verify**

Same command. Expected: `soc: ok`, `makefile link: ok`, `pic32mx: 0 failed`.

- [ ] **Step 5: Write the stand-in test**

`tests/pinheck/link/bootldr_test.c`:

```c
#include "../../../src/wpc/pinheck/bootldr.h"
#include <stdio.h>
#include <string.h>

#define HOST_BIT 692u

static pic32_boot b;
static uint8_t flash[0x80000];
static uint64_t tx_t[200000];
static uint8_t tx_l[200000];
static int ntx, fails, nlog;
static uint64_t now;

#define CHECK(c) do { if (!(c)) { printf("BOOT FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void on_tx(void *ctx, uint64_t t, int level) { (void)ctx; tx_t[ntx] = t; tx_l[ntx++] = (uint8_t)level; }
static void on_log(void *ctx, const char *msg) { (void)ctx; (void)msg; nlog++; }

static void start(void)
{
	int i;
	for (i = 0; i < (int)sizeof(flash); i++) flash[i] = (uint8_t)(i * 7);
	ntx = nlog = 0;
	now = 1000;
	boot_init(&b, flash, sizeof(flash), on_tx, NULL);
	boot_set_log(&b, on_log, NULL);
	boot_reset(&b, now);
}

static void send(const uint8_t *body, int n, uint8_t seq, int corrupt)
{
	uint8_t f[700];
	int i, k, len = 0, level = 1;
	uint8_t x = 0;
	f[len++] = 0x1B; f[len++] = seq; f[len++] = (uint8_t)(n >> 8); f[len++] = (uint8_t)n; f[len++] = 0x0E;
	for (i = 0; i < n; i++) f[len++] = body[i];
	for (i = 0; i < len; i++) x ^= f[i];
	f[len++] = (uint8_t)(x ^ corrupt);
	for (i = 0; i < len; i++) {
		int bits[10];
		bits[0] = 0;
		for (k = 0; k < 8; k++) bits[k + 1] = f[i] >> k & 1;
		bits[9] = 1;
		for (k = 0; k < 10; k++) {
			if (bits[k] != level) { level = bits[k]; boot_rx(&b, now, level); }
			now += HOST_BIT;
		}
		if (i % 16 == 15) boot_advance(&b, now);
	}
	now += HOST_BIT;
}

static void run_until(uint64_t t)
{
	while (now < t) {
		uint64_t h = boot_hold(&b, now), step = h && h < 1000 ? h : 1000;
		boot_advance(&b, now);
		now += step < t - now ? step : t - now;
	}
	boot_advance(&b, now);
}

static int level_at(uint64_t t)
{
	int k, v = 1;
	for (k = 0; k < ntx && tx_t[k] <= t; k++) v = tx_l[k];
	return v;
}

static int decode(uint64_t from, uint8_t *out, int max)
{
	int n = 0, k, bit;
	uint64_t t = from;
	while (n < max) {
		uint64_t s = 0;
		for (k = 0; k < ntx; k++) if (tx_t[k] >= t && tx_l[k] == 0) { s = tx_t[k]; break; }
		if (!s) break;
		out[n] = 0;
		for (bit = 0; bit < 8; bit++) if (level_at(s + BOOT_BIT * (uint64_t)bit + BOOT_BIT * 3u / 2u)) out[n] |= (uint8_t)(1u << bit);
		n++;
		t = s + BOOT_BIT * 9u + BOOT_BIT / 2u;
	}
	return n;
}

static int exchange(const uint8_t *body, int n, uint8_t seq, uint8_t *reply, int max)
{
	int first = ntx;
	uint64_t from;
	send(body, n, seq, 0);
	run_until(now + 40000000u);
	if (ntx == first) return 0;
	from = tx_t[first];
	return decode(from, reply, max);
}

static void window_without_host(void)
{
	start();
	CHECK(tx_l[ntx - 1] == 1 && tx_t[ntx - 1] == 1000);
	CHECK(boot_hold(&b, now) > 0);
	run_until(1000 + BOOT_WINDOW - 1);
	CHECK(b.state == BOOT_WAIT && boot_hold(&b, now) > 0);
	run_until(1000 + BOOT_WINDOW);
	CHECK(b.state == BOOT_APP && boot_hold(&b, now) == 0);
	CHECK(tx_l[ntx - 1] == 0 && tx_t[ntx - 1] == 1000 + BOOT_WINDOW);
}

static void sign_on_holds(void)
{
	static const uint8_t sign_on[1] = { 0x01 };
	uint8_t r[64];
	int n;
	start();
	run_until(130000000u);
	n = exchange(sign_on, 1, 0, r, 64);
	CHECK(n == 17);
	CHECK(r[0] == 0x1B && r[4] == 0x0E && r[5] == 0x01 && r[6] == 0x00 && r[7] == 8 && !memcmp(r + 8, "STK500_2", 8));
	CHECK(b.state == BOOT_HOST);
	run_until(1000 + BOOT_WINDOW + 80000000u);
	CHECK(b.state == BOOT_HOST && boot_hold(&b, now) > 0);
}

static void program_verify_leave(void)
{
	uint8_t body[600], r[600];
	int i, n;
	uint64_t app;
	start();
	body[0] = 0x01;
	exchange(body, 1, 0, r, 64);
	body[0] = 0x06; body[1] = 0; body[2] = 0; body[3] = 0x10; body[4] = 0x00;
	n = exchange(body, 5, 1, r, 64);
	CHECK(n == 8 && r[5] == 0x06 && r[6] == 0x00);
	body[0] = 0x13; body[1] = 0x01; body[2] = 0x00;
	for (i = 3; i < 10; i++) body[i] = 0xAA;
	for (i = 0; i < 256; i++) body[10 + i] = (uint8_t)(255 - i);
	n = exchange(body, 266, 2, r, 64);
	CHECK(n == 8 && r[5] == 0x13 && r[6] == 0x00);
	CHECK(flash[0x1000] == 255 && flash[0x10FF] == 0 && flash[0x1100] == 0xFF && flash[0x1FFF] == 0xFF);
	CHECK(flash[0x0FFF] == (uint8_t)(0x0FFF * 7) && flash[0x2000] == (uint8_t)(0x2000 * 7));
	body[0] = 0x06; body[1] = 0; body[2] = 0; body[3] = 0x10; body[4] = 0x00;
	exchange(body, 5, 3, r, 64);
	body[0] = 0x14; body[1] = 0x01; body[2] = 0x00; body[3] = 0x20;
	n = exchange(body, 4, 4, r, 600);
	CHECK(n == 256 + 9 && r[5] == 0x14 && r[6] == 0x00 && r[7] == 255 && r[7 + 255] == 0 && r[263] == 0x00);
	CHECK(b.state == BOOT_HOST);
	body[0] = 0x11; body[1] = 0x10; body[2] = 0x10;
	send(body, 3, 5, 0);
	while (!b.app_at) run_until(now + 1000);
	app = b.app_at;
	CHECK(app > 0 && b.state == BOOT_HOST);
	CHECK(boot_hold(&b, app - 1) > 0);
	run_until(app);
	CHECK(b.state == BOOT_APP && boot_hold(&b, now) == 0);
	CHECK(tx_l[ntx - 1] == 0 && tx_t[ntx - 1] == app);
	CHECK(tx_l[ntx - 2] == 1 && tx_t[ntx - 2] < app);
	CHECK(b.programmed == 256 && b.read == 256);
}

static void unknown_and_bad(void)
{
	uint8_t body[8], r[64];
	int n, before;
	start();
	body[0] = 0x01;
	exchange(body, 1, 0, r, 64);
	body[0] = 0x1D; body[1] = 0;
	n = exchange(body, 2, 1, r, 64);
	CHECK(n == 8 && r[5] == 0x1D && r[6] == 0xC0);
	exchange(body, 2, 2, r, 64);
	CHECK(nlog == 2);
	before = ntx;
	body[0] = 0x06; body[1] = body[2] = body[3] = body[4] = 0;
	send(body, 5, 3, 0x55);
	run_until(now + 40000000u);
	CHECK(ntx == before && b.errors == 1);
}

static void reset_redrives_idle(void)
{
	int before;
	start();
	run_until(1000 + BOOT_WINDOW);
	before = ntx;
	boot_reset(&b, now);
	CHECK(ntx == before + 1 && tx_l[ntx - 1] == 1 && tx_t[ntx - 1] == now);
	before = ntx;
	boot_reset(&b, now + 5);
	CHECK(ntx == before + 1 && tx_l[ntx - 1] == 1 && tx_t[ntx - 1] == now + 5);
}

int main(void)
{
	window_without_host();
	sign_on_holds();
	program_verify_leave();
	unknown_and_bad();
	reset_redrives_idle();
	printf("boot: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

Run: `env TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_SKIP_FIRMWARE=1 tests/pinheck/link/check.sh`
Expected: exit 2 with `bootldr.c: No such file or directory`.

- [ ] **Step 6: Write the stand-in**

`src/wpc/pinheck/bootldr.h`:

```c
#ifndef PINHECK_BOOTLDR_H
#define PINHECK_BOOTLDR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_BIT      688u
#define BOOT_WINDOW   240000000ull
#define BOOT_POLL     80000u
#define BOOT_LATENCY  8000u
#define BOOT_PROGRAM  160000u
#define BOOT_EDGES    1024
#define BOOT_FRAME    600

enum { BOOT_WAIT, BOOT_HOST, BOOT_APP };

typedef void (*boot_tx_fn)(void *ctx, uint64_t pic_cycle, int level);
typedef void (*boot_log_fn)(void *ctx, const char *msg);

typedef struct pic32_boot {
	uint8_t *flash;
	uint32_t flash_size;
	int state;
	uint64_t window_end, app_at, tx_free;
	int line;
	uint64_t edge_t[BOOT_EDGES];
	uint8_t edge_l[BOOT_EDGES];
	int nedge;
	uint64_t scan_from;
	uint8_t frame[BOOT_FRAME];
	int flen;
	uint32_t addr;
	uint8_t erased[128];
	boot_tx_fn tx;
	void *tx_ctx;
	int tx_level;
	boot_log_fn log;
	void *log_ctx;
	uint32_t unknown_logged[8];
	unsigned long frames, programmed, read, errors;
} pic32_boot;

void boot_init(pic32_boot *b, uint8_t *flash, uint32_t flash_size, boot_tx_fn tx, void *tx_ctx);
void boot_set_log(pic32_boot *b, boot_log_fn fn, void *ctx);
void boot_reset(pic32_boot *b, uint64_t pic_cycle);
void boot_rx(pic32_boot *b, uint64_t pic_cycle, int level);
void boot_advance(pic32_boot *b, uint64_t pic_cycle);
uint64_t boot_hold(const pic32_boot *b, uint64_t pic_cycle);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/bootldr.c`:

```c
#include "bootldr.h"
#include <stdio.h>
#include <string.h>

#define PAGE 4096u

static void logf1(pic32_boot *b, const char *fmt, unsigned long v)
{
	char msg[80];
	if (!b->log) return;
	sprintf(msg, fmt, v);
	b->log(b->log_ctx, msg);
}

static void tx_level(pic32_boot *b, uint64_t t, int level)
{
	if (level == b->tx_level) return;
	b->tx_level = level;
	if (b->tx) b->tx(b->tx_ctx, t, level);
}

void boot_init(pic32_boot *b, uint8_t *flash, uint32_t flash_size, boot_tx_fn tx, void *tx_ctx)
{
	memset(b, 0, sizeof(*b));
	b->flash = flash;
	b->flash_size = flash_size;
	b->tx = tx;
	b->tx_ctx = tx_ctx;
	boot_reset(b, 0);
}

void boot_set_log(pic32_boot *b, boot_log_fn fn, void *ctx)
{
	b->log = fn;
	b->log_ctx = ctx;
}

void boot_reset(pic32_boot *b, uint64_t t)
{
	b->state = BOOT_WAIT;
	b->window_end = t + BOOT_WINDOW;
	b->app_at = 0;
	b->tx_free = t;
	b->line = 1;
	b->nedge = 0;
	b->scan_from = t;
	b->flen = 0;
	b->addr = 0;
	memset(b->erased, 0, sizeof(b->erased));
	b->tx_level = -1;
	tx_level(b, t, 1);
}

void boot_rx(pic32_boot *b, uint64_t t, int level)
{
	level = level != 0;
	if (level == b->line) return;
	b->line = level;
	if (b->nedge == BOOT_EDGES) { b->errors++; return; }
	b->edge_t[b->nedge] = t;
	b->edge_l[b->nedge] = (uint8_t)level;
	b->nedge++;
}

static int level_at(const pic32_boot *b, uint64_t t, int base)
{
	int k, v = base;
	for (k = 0; k < b->nedge && b->edge_t[k] <= t; k++) v = b->edge_l[k];
	return v;
}

static void send(pic32_boot *b, uint64_t t, const uint8_t *body, int n, uint8_t seq)
{
	uint8_t f[BOOT_FRAME + 8];
	int i, k, len = 0;
	uint8_t x = 0;
	f[len++] = 0x1B; f[len++] = seq; f[len++] = (uint8_t)(n >> 8); f[len++] = (uint8_t)n; f[len++] = 0x0E;
	for (i = 0; i < n; i++) f[len++] = body[i];
	for (i = 0; i < len; i++) x ^= f[i];
	f[len++] = x;
	if (t < b->tx_free) t = b->tx_free;
	for (i = 0; i < len; i++) {
		tx_level(b, t, 0);
		for (k = 0; k < 8; k++) tx_level(b, t + BOOT_BIT * (uint64_t)(k + 1), f[i] >> k & 1);
		tx_level(b, t + BOOT_BIT * 9u, 1);
		t += BOOT_BIT * 10u;
	}
	b->tx_free = t;
}

static void program(pic32_boot *b, uint32_t a, uint8_t v)
{
	uint32_t page = a / PAGE;
	if (a >= b->flash_size) return;
	if (!(b->erased[page >> 3] & (1u << (page & 7)))) {
		b->erased[page >> 3] |= (uint8_t)(1u << (page & 7));
		memset(b->flash + page * PAGE, 0xFF, PAGE);
	}
	b->flash[a] = v;
}

static void handle(pic32_boot *b, uint64_t t, uint64_t now)
{
	uint8_t *f = b->frame, out[BOOT_FRAME + 8];
	uint8_t cmd = f[5];
	uint32_t cnt, i;
	uint64_t delay = BOOT_LATENCY;
	int len = 2, leave = 0;

	out[0] = cmd;
	out[1] = 0x00;
	switch (cmd) {
	case 0x01:
		out[2] = 8;
		memcpy(out + 3, "STK500_2", 8);
		len = 11;
		if (b->state == BOOT_WAIT) logf1(b, "boot: sign-on %lu", 0);
		b->state = BOOT_HOST;
		break;
	case 0x06:
		b->addr = (uint32_t)f[6] << 24 | (uint32_t)f[7] << 16 | (uint32_t)f[8] << 8 | f[9];
		break;
	case 0x13:
		cnt = (uint32_t)f[6] << 8 | f[7];
		for (i = 0; i < cnt; i++) program(b, b->addr + i, f[15 + i]);
		b->addr += cnt;
		b->programmed += cnt;
		delay += BOOT_PROGRAM;
		break;
	case 0x14:
		cnt = (uint32_t)f[6] << 8 | f[7];
		if (cnt + 3 > BOOT_FRAME) cnt = BOOT_FRAME - 3;
		for (i = 0; i < cnt; i++) out[2 + i] = b->addr + i < b->flash_size ? b->flash[b->addr + i] : 0xFF;
		out[2 + cnt] = 0x00;
		len = (int)cnt + 3;
		b->addr += cnt;
		b->read += cnt;
		break;
	case 0x11:
		leave = 1;
		break;
	default:
		out[1] = 0xC0;
		if (!(b->unknown_logged[cmd >> 5] & (1u << (cmd & 31)))) {
			b->unknown_logged[cmd >> 5] |= 1u << (cmd & 31);
			logf1(b, "boot: unsupported STK500v2 command %02lx", cmd);
		}
		break;
	}
	b->frames++;
	t += delay;
	send(b, t > now ? t : now, out, len, f[1]);
	if (leave) {
		b->app_at = b->tx_free;
		logf1(b, "boot: leave, %lu bytes programmed", b->programmed);
	}
}

static void feed(pic32_boot *b, uint8_t v, uint64_t t, uint64_t now)
{
	uint32_t size;
	int i;
	uint8_t x = 0;
	if (b->flen == 0 && v != 0x1B) { b->errors++; return; }
	b->frame[b->flen++] = v;
	if (b->flen < 5) return;
	size = (uint32_t)b->frame[2] << 8 | b->frame[3];
	if (size + 6 > BOOT_FRAME || b->frame[4] != 0x0E) { b->errors++; b->flen = 0; return; }
	if ((uint32_t)b->flen < size + 6) return;
	for (i = 0; i < b->flen; i++) x ^= b->frame[i];
	b->flen = 0;
	if (x || size == 0) { b->errors++; return; }
	if (b->state == BOOT_APP) return;
	handle(b, t, now);
}

void boot_advance(pic32_boot *b, uint64_t now)
{
	for (;;) {
		int k, base, s = -1;
		uint64_t start;
		uint32_t v = 0;
		for (k = 0; k < b->nedge; k++)
			if (!b->edge_l[k] && b->edge_t[k] >= b->scan_from) { s = k; break; }
		if (s < 0) {
			if (b->nedge) { b->edge_l[0] = b->edge_l[b->nedge - 1]; b->edge_t[0] = b->edge_t[b->nedge - 1]; b->nedge = 1; }
			break;
		}
		start = b->edge_t[s];
		if (now < start + BOOT_BIT * 9u + BOOT_BIT / 2u) break;
		base = s ? b->edge_l[s - 1] : 1;
		for (k = 0; k < 8; k++)
			if (level_at(b, start + BOOT_BIT * (uint64_t)k + BOOT_BIT * 3u / 2u, base)) v |= 1u << k;
		b->scan_from = start + BOOT_BIT * 9u + BOOT_BIT / 2u;
		if (!level_at(b, b->scan_from, base)) b->errors++;
		else feed(b, (uint8_t)v, start + BOOT_BIT * 10u, now);
		for (k = 0; k < b->nedge && b->edge_t[k] < b->scan_from; k++);
		if (k > 1) {
			memmove(b->edge_t, b->edge_t + k - 1, sizeof(b->edge_t[0]) * (size_t)(b->nedge - k + 1));
			memmove(b->edge_l, b->edge_l + k - 1, (size_t)(b->nedge - k + 1));
			b->nedge -= k - 1;
		}
	}
	if (b->state == BOOT_WAIT && now >= b->window_end) {
		b->state = BOOT_APP;
		tx_level(b, b->window_end, 0);
	} else if (b->state == BOOT_HOST && b->app_at && now >= b->app_at) {
		b->state = BOOT_APP;
		tx_level(b, b->app_at, 0);
	}
}

uint64_t boot_hold(const pic32_boot *b, uint64_t now)
{
	uint64_t end;
	if (b->state == BOOT_APP) return 0;
	end = b->state == BOOT_WAIT ? b->window_end : b->app_at ? b->app_at : now + BOOT_POLL;
	if (end <= now) return 1;
	return end - now < BOOT_POLL ? end - now : BOOT_POLL;
}
```

- [ ] **Step 7: Run to verify**

Same command. Expected: `prop: ok`, `boot: ok`, `rtc: ok`, `link: 0 failed`.

- [ ] **Step 8: Prove the tests bite**

Apply each replacement, run the named suite, confirm the failing line, then `git checkout` the file:

| File | Replace → with | Fails at |
|---|---|---|
| `pic32mx.c` | `p->cpu.cycles += h < slice ? h : slice;` → `p->cpu.cycles += h;` | `soc_test.c:222` |
| `pic32mx.c` | `uint64_t h = p->board.hold ? p->board.hold(p->board.ctx, p->cpu.cycles) : 0;` → `uint64_t h = 0;` | `soc_test.c:222` |
| `bootldr.c` | `if (b->state == BOOT_WAIT && now >= b->window_end) {` → `if (b->state == BOOT_WAIT && now + 1 >= b->window_end) {` | `bootldr_test.c:103` |
| `bootldr.c` | `\t\tb->state = BOOT_HOST;` → `\t\tb->state = b->state;` | `bootldr_test.c:119` |
| `bootldr.c` | `memset(b->flash + page * PAGE, 0xFF, PAGE);` → `(void)0;` | `bootldr_test.c:140` |
| `bootldr.c` | `if (x \|\| size == 0) {` → `if (size == 0) {` | `bootldr_test.c:177` |
| `bootldr.c` | `b->app_at = b->tx_free;` → `b->app_at = t;` | `bootldr_test.c:157` |
| `bootldr.c` | `out[1] = 0xC0;` → `out[1] = 0x00;` | `bootldr_test.c:170` |
| `bootldr.c` | `if (b->state == BOOT_APP) return 0;` → `if (b->state != BOOT_WAIT) return 0;` | `bootldr_test.c:121` |
| `bootldr.c` | `\tb->tx_level = -1;` (in `boot_reset`) → deleted | `bootldr_test.c:100` and `:190` |

(`pic32mx.c` rows: `PINHECK_SKIP_FIRMWARE=1 tests/pinheck/pic32mx/check.sh`; `bootldr.c` rows: Step 5's command.)

- [ ] **Step 9: Commit**

```bash
git add src/cpu/pic32mx/pic32mx.h src/cpu/pic32mx/pic32mx.c tests/pinheck/pic32mx/soc_test.c tests/pinheck/pic32mx/boot.c \
        src/wpc/pinheck/bootldr.h src/wpc/pinheck/bootldr.c tests/pinheck/link/bootldr_test.c
git commit -m "pinheck: PIC32 bootloader stand-in (STK500v2 on UART5) and SoC hold callback"
```

### Task 4: Root files on the card

**Files:**
- Modify: `src/wpc/pinheck/vfat.c`, `tests/pinheck/storage/vfat_test.c`, `cmpzip.py`, `check.sh`, `README.md` (scripted)

The Propeller's update opens `DOM_V006.PRG` and `PRP_V008.BIN` in the card's root; Plan 4's volume holds only `DMD/` and `SFX/`. Files in the zip's root now appear in the volume's root; other directories stay excluded.

- [ ] **Step 1: Update the tests**

```sh
python3 - <<'EOF'
import sys
edits = [
    ('tests/pinheck/storage/vfat_test.c', 'CHECK(!entry(root, "DOM_V006PRG") && !entry(root, "OTHER      "));', 'CHECK(entry(root, "DOM_V006PRG") && !entry(root, "OTHER      "));'),
    ('tests/pinheck/storage/cmpzip.py', "if parts[0] not in ('DMD', 'SFX') or", "if (len(parts) > 1 and parts[0] not in ('DMD', 'SFX')) or"),
    ('tests/pinheck/storage/check.sh', 'mcopy -s -n -i $B/vol.img ::/DMD ::/SFX $B/extract/', "mcopy -s -n -i $B/vol.img '::/*' $B/extract/"),
    ('tests/pinheck/storage/README.md', 'directory entries, 8.3 names, exclusions,', 'directory entries, 8.3 names, root files, exclusions,'),
]
for p, a, b in edits:
    s = open(p).read()
    if b in s: continue
    if s.count(a) != 1: sys.exit('%s: anchor not found' % p)
    open(p, 'w').write(s.replace(a, b, 1)); print('updated', p)
EOF
```

Expected: four `updated` lines; running it again prints nothing. `mcopy` now extracts the whole volume and `cmpzip.py` expects root files too.

- [ ] **Step 2: Run to confirm they fail**

Run: `tests/pinheck/storage/check.sh`
Expected: `VFAT FAIL vfat_test.c:75: entry(root, "DOM_V006PRG") && !entry(root, "OTHER      ")`, `mount: FAIL, 1 missing or different, 0 extra: ['DOM_V006.PRG']`, `storage: 2 failed`.

- [ ] **Step 3: Expose root files**

```sh
python3 - <<'EOF'
import sys
p = 'src/wpc/pinheck/vfat.c'
a = "\tif (!((path[0] == 'D' ||"
b = "\tif (strchr(path, '/') && !((path[0] == 'D' ||"
s = open(p).read()
if b not in s:
    if s.count(a) != 1: sys.exit('%s: anchor not found' % p)
    open(p, 'w').write(s.replace(a, b, 1)); print('updated', p)
EOF
```

Expected: `updated src/wpc/pinheck/vfat.c`.

- [ ] **Step 4: Run to verify**

Run: `DOMINOS_ZIP=$PINHECK_ZIP tests/pinheck/storage/check.sh` (without `PINHECK_ZIP`, leave `DOMINOS_ZIP` unset)
Expected: `vfat: ok`, `mount: 6 files byte-exact against test.zip`, with a Domino's zip `mount: 1002 files byte-exact against dominos.zip`, `sd: ok`, `storage: 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck/vfat.c tests/pinheck/storage
git commit -m "pinheck: expose the romset's root files on the card"
```

### Task 5: Headless two-CPU machine

**Files:**
- Create: `tests/pinheck/link/machine.c`, `tests/pinheck/link/machine_check.sh`
- Modify: `tests/pinheck/pic32mx/banner.py` (the sync line becomes a regex; an optional first application boot)

**Interfaces:**
- Consumes: Tasks 1–4, `pic32mx_*` (Plan 2), `sd_*`, `vfat_*`, `zipsrc_*` (Plan 4).
- Produces: `build/linkmachine -rom ROM -prp PRP_V008.BIN [-zip ZIP] [-inservice V] [-updatecode] [-stale] [-v] [-c cycles] [-first cycles] [-boots n] [-send cycle text] DOM_V006.PRG`, printing UART1 to stdout with `=== BOOT n ===` markers. The EEPROM starts blank above `PRP_V008.BIN` and persists across boots; the flash persists too. `-first` sets the length of boot 1; `-send` applies to the last boot. With `-v`, stderr has the Propeller and stand-in log, `boot n: first UART1 byte at …s` per boot, `boot: frames=… programmed=… read=… errors=… flash_matches_prg=…`, `eeprom: $8000=… $8004=…` and `exceptions=… speed=…x`.

`machine_check.sh` runs two machines in parallel. **fresh**: blank EEPROM, boot 1 for 137.5 s (the update), then two 13 s boots. **update**: an installed machine (`-inservice 6`) whose PIC32 has run `UPDATE CODE` (`-updatecode`) and whose flash is corrupted (`-stale`); it is reset after 1 s, as a restart after `UPDATE CODE`, and updates in boot 2.

- [ ] **Step 1: Write the machine, its check and the banner update**

`tests/pinheck/link/machine.c`:

```c
#include "pic32mx.h"
#include "../../../src/wpc/pinheck/prop.h"
#include "../../../src/wpc/pinheck/rtc.h"
#include "../../../src/wpc/pinheck/bootldr.h"
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
static pic32_boot boot;
static uint64_t rtc_at;
static uint8_t u13mem[131072], propmem[131072], rom[0x8000];
static uint8_t flash[PIC32MX_FLASH_SIZE], prgcopy[PIC32MX_FLASH_SIZE];
static int verbose;
static uint64_t boot_at, first_tx;
static zipsrc zip;
static vfat vol;
static sd_card card;

static int blk_read(void *ctx, uint32_t lba, uint8_t *buf) { (void)ctx; return vfat_read(&vol, lba, buf); }
static int spi(void *ctx, int cs, int sclk, int mosi) { (void)ctx; return sd_update(&card, cs, sclk, mosi); }
static void boot_tx(void *ctx, uint64_t t, int level) { (void)ctx; prop_pic_pins(&prop, t, level ? 1u << 24 : 0); }
static void prop_tx(void *ctx, uint64_t t, int level) { (void)ctx; boot_rx(&boot, t, level); }

static uint64_t hold(void *ctx, uint64_t cycle)
{
	(void)ctx;
	prop_catch_up(&prop, cycle);
	boot_advance(&boot, cycle);
	return boot_hold(&boot, cycle);
}

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
	(void)ctx;
	if (uart == 1) fputc(byte, stdout);
	if (uart == 1 && !first_tx) first_tx = cycle;
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
	pic32mx_board board = { NULL, port_write, port_read, uart_tx, i2c_pins, NULL, exception, hold };
	unsigned long long cycles = 800000000ull, first = 0, send_at = 0;
	const char *path = NULL, *rompath = NULL, *prp = NULL, *send = NULL, *zippath = NULL;
	int boots = 1, b, i, inservice = -1, updatecode = 0, stale = 0;
	size_t n, m;
	clock_t t0 = clock();

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) verbose = 1;
		else if (!strcmp(argv[i], "-c") && i + 1 < argc) cycles = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-first") && i + 1 < argc) first = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-boots") && i + 1 < argc) boots = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-rom") && i + 1 < argc) rompath = argv[++i];
		else if (!strcmp(argv[i], "-prp") && i + 1 < argc) prp = argv[++i];
		else if (!strcmp(argv[i], "-zip") && i + 1 < argc) zippath = argv[++i];
		else if (!strcmp(argv[i], "-inservice") && i + 1 < argc) inservice = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-updatecode")) updatecode = 1;
		else if (!strcmp(argv[i], "-stale")) stale = 1;
		else if (!strcmp(argv[i], "-send") && i + 2 < argc) { send_at = strtoull(argv[++i], NULL, 0); send = argv[++i]; }
		else path = argv[i];
	}
	if (!path || !rompath || !prp) {
		fprintf(stderr, "usage: linkmachine -rom p8x32a.rom -prp PRP_V008.BIN [-zip dominos.zip] [-inservice version] [-updatecode] [-stale] [-v] [-c cycles] [-first cycles] [-boots n] [-send cycle text] DOM_V006.PRG\n");
		return 2;
	}
	memset(propmem, 0xFF, sizeof(propmem));
	memset(u13mem, 0xFF, sizeof(u13mem));
	if (!load(rompath, rom, sizeof(rom), &n) || n != sizeof(rom)) { fprintf(stderr, "rom must be 32768 bytes\n"); return 2; }
	if (!load(prp, propmem, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "PRP image must be 32768 bytes\n"); return 2; }
	memset(flash, 0xFF, sizeof(flash));
	if (!load(path, flash, sizeof(flash), &m)) return 2;
	memcpy(prgcopy, flash, sizeof(flash));
	if (inservice >= 0) {
		uint32_t w0 = (uint32_t)inservice << 24 | 0xBAFAu, w1 = 0xABBA0002u;
		if (updatecode) w1 = 0xABBA0001u;
		for (i = 0; i < 4; i++) { propmem[0x8000 + i] = (uint8_t)(w0 >> (8 * i)); propmem[0x8004 + i] = (uint8_t)(w1 >> (8 * i)); }
	}
	if (stale) for (i = 0x1000; i < (int)m; i += 0x1000) flash[i] ^= 0xFF;
	pic32mx_init(&soc, &board, flash, (uint32_t)m);
	prop_init(&prop, rom, propmem);
	prop_set_log(&prop, proplog, NULL);
	prop_set_tx(&prop, prop_tx, NULL);
	boot_init(&boot, flash, sizeof(flash), boot_tx, NULL);
	boot_set_log(&boot, proplog, NULL);
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
		const char *s = b == boots ? send : NULL;
		unsigned long long limit = b == 1 && first ? first : cycles;
		printf("=== BOOT %d ===\n", b);
		if (b > 1) { pic32mx_reset(&soc); prop_reset(&prop, soc.cpu.cycles); }
		boot_reset(&boot, soc.cpu.cycles);
		boot_at = soc.cpu.cycles;
		first_tx = 0;
		cat24m01_init(&u13, u13mem, 0);
		ds1340_init(&rtc, 1483228800, 80000000u);
		rtc_at = soc.cpu.cycles;
		while (done < limit) {
			done += (unsigned long long)pic32mx_run(&soc, 100000);
			prop_catch_up(&prop, soc.cpu.cycles);
			if (s && *s && done >= send_at) {
				int k = soc.uart[0].rx_count;
				pic32mx_uart_rx(&soc, 0, (uint8_t)*s);
				if (soc.uart[0].rx_count > k) s++;
			}
		}
		printf("\n");
		if (verbose) fprintf(stderr, "boot %d: first UART1 byte at %.3fs\n", b, first_tx ? (double)(first_tx - boot_at) / 80e6 : -1.0);
	}
	fflush(stdout);
	if (verbose) {
		double secs = (double)(clock() - t0) / CLOCKS_PER_SEC, emu = (double)(soc.cpu.cycles) / 80e6;
		fprintf(stderr, "boot: frames=%lu programmed=%lu read=%lu errors=%lu flash_matches_prg=%d\n", boot.frames, boot.programmed, boot.read, boot.errors, !memcmp(flash, prgcopy, m));
		fprintf(stderr, "eeprom: $8000=%08x $8004=%08x\n",
			(unsigned)(propmem[0x8000] | propmem[0x8001] << 8 | propmem[0x8002] << 16 | (uint32_t)propmem[0x8003] << 24),
			(unsigned)(propmem[0x8004] | propmem[0x8005] << 8 | propmem[0x8006] << 16 | (uint32_t)propmem[0x8007] << 24));
		fprintf(stderr, "exceptions=%llu wall=%.1fs emulated=%.1fs speed=%.2fx\n", (unsigned long long)soc.exc_count, secs, emu, emu / secs);
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
args="-zip $zip -rom $rom -prp $upd/PRP_V008.BIN $upd/DOM_V006.PRG"
timeout 3000 $run -v -boots 3 -first 11000000000 -c 1040000000 -send 900000000 '[E97000]' $args > $B/fresh.txt 2> $B/fresh.err &
timeout 3000 $run -v -inservice 6 -updatecode -stale -boots 2 -first 80000000 -c 11000000000 $args > $B/update.txt 2> $B/update.err &
wait
fail=0
for f in fresh update; do
	for want in "boot: sign-on" "boot: leave, 204288 bytes programmed" "eeprom: \$8000=0600bafa \$8004=abba0002" "flash_matches_prg=1" "exceptions=0" "prop: CLKSET 6f"; do
		grep -qF "$want" $B/$f.err || { echo "MACHINE FAIL ($f): stderr lacks '$want'"; fail=$((fail + 1)); }
	done
done
awk '/=== BOOT 1 ===/{f=1;next} /=== BOOT 2 ===/{f=0} f' $B/fresh.txt | grep -aq "PROPELLER SYNC CHECK" || { echo "MACHINE FAIL: the application did not start after the update"; fail=$((fail + 1)); }
awk '/=== BOOT 1 ===/{f=1;next} /=== BOOT 2 ===/{f=0} f' $B/fresh.txt | grep -aq "CHECK\.*OK" && { echo "MACHINE FAIL: the Propeller served the link during its update"; fail=$((fail + 1)); }
t1=$(sed -n 's/^boot 1: first UART1 byte at //p' $B/fresh.err | tr -d s)
awk -v t="$t1" 'BEGIN { exit !(t >= 135.0) }' || { echo "MACHINE FAIL: the application printed at ${t1}s, before the update released it"; fail=$((fail + 1)); }
for n in 2 3; do
	t=$(sed -n "s/^boot $n: first UART1 byte at //p" $B/fresh.err | tr -d s)
	awk -v t="$t" 'BEGIN { exit !(t >= 3.25 && t < 3.35) }' || { echo "MACHINE FAIL: boot $n printed at ${t}s, not 0.3 s after the 3 s window"; fail=$((fail + 1)); }
done
python3 ../pic32mx/banner.py $B/fresh.txt 2 || fail=$((fail + 1))
grep -aq "Ball Search: DISABLED" $B/fresh.txt || { echo "MACHINE FAIL: no reply to [E97000]"; fail=$((fail + 1)); }
tail -1 $B/fresh.err
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
    o = int(sys.argv[2]) - 1 if len(sys.argv) > 2 else 0
    want = [
        (1, 'PROPELLER SYNC CHECK.*OK'),
        (1, 'NAMING GAME'),
        (2, 'NAME ALREADY EXISTS'),
        (2, 'pinHeck System 2011-2016'),
        (2, 'Game: DOM - DOMINOS'),
        (2, 'Version: 006'),
    ]
    want = [(n + o, s) for n, s in want]
    bad = [(n, s) for n, s in want if not re.search(s, b.get(n, ''))]
    for n, s in bad:
        print('BANNER FAIL boot %d lacks %r' % (n, s))
    if 'pinHeck System' in b.get(1 + o, ''):
        print('BANNER FAIL boot %d printed the banner on a blank EEPROM' % (1 + o))
        bad.append((1 + o, 'banner'))
    print('banner: %s' % ('FAIL' if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 2: Run the suite**

Run: `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && tests/pinheck/link/check.sh`
Expected (about 15 minutes): `prop: ok`, `boot: ok`, `rtc: ok`, `banner: ok`, a line `exceptions=0 … speed=0.1…x`, `machine: ok`, `link: 0 failed`.

`build/fresh.err` shows `boot: sign-on 0`, `boot: leave, 204288 bytes programmed`, `flash_matches_prg=1`, `eeprom: $8000=0600bafa $8004=abba0002`, boot 1's first UART1 byte at about 135.03 s and boots 2–3 at about 3.30 s. `build/fresh.txt` boot 1 ends in `PROPELLER SYNC CHECK……` without `OK` (the Propeller shows `PLEASE RESTART`); boots 2–3 pass the sync check, boot 3 prints `pinHeck System 2011-2016`, `Game: DOM - DOMINOS`, `Version: 006` and `Ball Search: DISABLED` in reply to `[E97000]`. `build/update.err` has the same stand-in and EEPROM lines.

- [ ] **Step 3: Confirm the Milestone 2 suite still passes with the new banner.py**

Run: `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && tests/pinheck/pic32mx/check.sh`
Expected: `banner: ok` … `pic32mx: 0 failed`.

- [ ] **Step 4: Show the RTC is required (Review Focus)**

Build a copy of `machine.c` with `return cat24m01_update(&u13, scl, sda) & ds1340_update(&rtc, scl, sda);` replaced by `(void)rtc; return cat24m01_update(&u13, scl, sda);` into `build/linkmachine_nortc` (same `cc` line as `check.sh`), run it with `-inservice 6 -boots 2 -c 1040000000 -send 900000000 '[E97000]'` and the arguments `machine_check.sh` uses, and confirm `grep -a "Ball Search"` finds nothing while `PROPELLER SYNC CHECK……OK` still appears. Delete the copy.

- [ ] **Step 5: Show the Propeller's TX reaches the stand-in**

In `src/wpc/pinheck/prop.c` replace `p->tx(p->tx_ctx, to_pic(p, t), tx);` with `(void)t;`, rebuild `build/linkmachine` with the `cc` line from `check.sh`, run `build/linkmachine -v -boots 1 -c 400000000` with the arguments `machine_check.sh` uses, and confirm stderr has no `boot: sign-on` and the application starts when the window ends (`boot 1: first UART1 byte at 3.300s`). Restore with `git checkout src/wpc/pinheck/prop.c` and rebuild; the same run then shows `boot: sign-on 0` and `boot 1: first UART1 byte at -1.000s` (the stand-in still holds the core at 5 s).

- [ ] **Step 6: Commit**

```bash
git add tests/pinheck/link/machine.c tests/pinheck/link/machine_check.sh tests/pinheck/pic32mx/banner.py
git commit -m "pinheck: headless PIC32 + Propeller machine: update on blank EEPROM, then the game"
```

### Task 6: PinMAME integration

**Files:**
- Modify: `src/osdepend.h`, `src/unix/fileio.c`, `src/libpinmame/fileio.c`, `src/windows/fileio.c`, `src/ios/fileio.c`, `src/wpc/driver.c` (scripted, Step 3)
- Replace: `src/wpc/pinheck.h`, `src/wpc/pinheck.c`, `src/wpc/pinheckgames.c`, `tests/pinheck/pic32mx/makefile_link_check.sh`, `tests/pinheck/pic32mx/pinmame_check.sh`
- Create: `tests/pinheck/link/register_build.py`; it edits `src/pinmame.mak`, 10 cmake lists, 4 VS projects and their filters

**Interfaces:**
- Consumes: Tasks 1–5, Plan 4's sources.
- Produces: `const char *osd_get_path(int pathtype, int pathindex)` on every platform; parent set `pinheck` (`NOT_A_DRIVER`) and clone `dominos`; `PINHECK_BIOSREGION` (`REGION_USER2`). The stand-in writes into `REGION_CPU1` (decision D2) and holds the PIC32 through `pinheck_hold`.

`pinmame_check.sh` launches PinMAME twice on an empty NVRAM directory. Launch 1 (9,000 frames, 150 s) runs the update, is reset at 138 s by `PINHECK_RESET_AT` and boots the game. Launch 2 (900 frames) starts from the saved NVRAM, must not sign on again, and answers `[E97000]` injected at 11.5 s.

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
launch() {
	(cd $B && PINHECK_UART1_LOG=$PWD/uart$1.log PINHECK_PROP_LOG=$PWD/prop$1.log timeout 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram \
		-headless -frames_to_run $2 -skip_gamewarnings -nothrottle > run$1.out 2>&1) || { echo "PINMAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
PINHECK_RESET_AT=138 launch 1 9000
PINHECK_UART1_SEND_AT=11.5 PINHECK_UART1_SEND='[E97000]' launch 2 900
fail=0
for want in "boot: sign-on 0" "boot: leave, 204288 bytes programmed" "prop: CLKSET 6f"; do
	grep -qF "$want" $B/prop1.log || { echo "PINMAME FAIL: launch 1 lacks '$want'"; fail=1; }
done
grep -q "boot: sign-on" $B/prop2.log && { echo "PINMAME FAIL: launch 2 ran the update again"; fail=1; }
grep -aq "PROPELLER SYNC CHECK\.*OK" $B/uart1.log || { echo "PINMAME FAIL: no sync after the update and reset"; fail=1; }
for want in "PROPELLER SYNC CHECK" "pinHeck System 2011-2016" "Version: 006" "Ball Search: DISABLED"; do
	grep -aqF "$want" $B/uart2.log || { echo "PINMAME FAIL: launch 2 UART1 log lacks '$want'"; fail=1; }
done
grep -aq "PROPELLER SYNC CHECK\.*OK" $B/uart2.log || { echo "PINMAME FAIL: launch 2 sync check did not pass"; fail=1; }
[ -s $B/nvram/dominos.nv ] || { echo "PINMAME FAIL: no NVRAM written"; fail=1; }
if [ $fail -ne 0 ]; then cat -v $B/uart2.log | grep -av "EEPROM\|Checksum\|Timeout\|Read OK\|Write\|Exchanging" | tail -20; exit 1; fi
echo "pinmame: ok"
```

- [ ] **Step 2: Run it against the current build to see it fail**

Run (any `sdl3pinmame` built before this task): `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && SDL3PINMAME=build/sdl3pinmame/sdl3pinmame tests/pinheck/pic32mx/pinmame_check.sh`
Expected: a `PINMAME FAIL` line: `launch 1 exited 1` with a build older than Plan 2 (no `dominos` driver), or `launch 1 lacks 'boot: sign-on 0'` with a Plan 2–4 build (no Propeller).

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
#include "pinheck/bootldr.h"
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
static pic32_boot boot;
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
static void pinheck_boot_tx(void *ctx, uint64_t t, int level) { (void)ctx; prop_pic_pins(&prop, t, level ? 1u << 24 : 0); }
static void pinheck_prop_tx(void *ctx, uint64_t t, int level) { (void)ctx; boot_rx(&boot, t, level); }

static uint64_t pinheck_hold(void *ctx, uint64_t cycle)
{
	(void)ctx;
	prop_catch_up(&prop, cycle);
	boot_advance(&boot, cycle);
	return boot_hold(&boot, cycle);
}

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
	pic32mx_board board = { NULL, pinheck_port_write, pinheck_port_read, pinheck_uart_tx, pinheck_i2c_pins, pinheck_unmapped, pinheck_exception, pinheck_hold };
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
	prop_set_tx(&prop, pinheck_prop_tx, NULL);
	boot_init(&boot, memory_region(PINHECK_CPUREGION), memory_region_length(PINHECK_CPUREGION), pinheck_boot_tx, NULL);
	boot_set_log(&boot, pinheck_prop_log, NULL);
	pinheck_open_card();
	pic32cpu_set_board(&board);
}

static MACHINE_RESET(pinheck)
{
	const char *at = getenv("PINHECK_UART1_SEND_AT");
	prop_reset(&prop, 0);
	boot_reset(&boot, 0);
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
	if (first && getenv("PINHECK_INSERVICE")) pinheck_in_service(propmem, core_gameData->hw.gameSpecific1);
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
      'src/wpc/pinheck/prop.c', 'src/wpc/pinheck/prop.h', 'src/wpc/pinheck/rtc.c', 'src/wpc/pinheck/rtc.h',
      'src/wpc/pinheck/bootldr.c', 'src/wpc/pinheck/bootldr.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/prop.o $(PINOBJ)/pinheck/eeprom.o $(PINOBJ)/pinheck/rtc.o $(PINOBJ)/pinheck/bootldr.o $(OBJ)/cpu/p8x32a/p8x32a.o\n'),
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
grep -cE '(pinheck|prop|rtc|bootldr|sd|vfat|zipsrc|eeprom|p8x32a)\.c:[0-9]+:[0-9]+: (warning|error)' build/sdl3pinmame/build.log
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 7: Run the PinMAME check**

Run: `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && SDL3PINMAME=build/sdl3pinmame/sdl3pinmame tests/pinheck/pic32mx/pinmame_check.sh`
Expected (about 15 minutes): `pinmame: ok`. In `tests/pinheck/pic32mx/build/pinmame/`: `prop1.log` has `boot: sign-on 0` and `boot: leave, 204288 bytes programmed` once, `prop2.log` has neither; `uart1.log` has `PROPELLER SYNC CHECK……OK` after the reset; `uart2.log` has the sync check, the banner, `Version: 006`, today's local date and time, and `Ball Search: DISABLED`; `nvram/dominos.nv` is 229,468 bytes.

- [ ] **Step 8: Full regression**

Run: `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && tests/pinheck/mips32/check.sh && tests/pinheck/pic32mx/check.sh && tests/pinheck/storage/check.sh && tests/pinheck/link/check.sh && tests/pinheck/p8x32a/check.sh`
Expected: `mips32: 208 passed, 0 failed`, `pic32mx: 0 failed` (with `makefile link: ok`), `storage: 0 failed`, `link: 0 failed`, `p8x32a: … passed, 0 failed`.

- [ ] **Step 9: Commit**

```bash
git add src/osdepend.h src/unix/fileio.c src/libpinmame/fileio.c src/windows/fileio.c src/ios/fileio.c src/wpc/driver.c \
        src/wpc/pinheck.h src/wpc/pinheck.c src/wpc/pinheckgames.c src/pinmame.mak cmake vcproj \
        tests/pinheck/link/register_build.py tests/pinheck/pic32mx/makefile_link_check.sh tests/pinheck/pic32mx/pinmame_check.sh
git commit -m "pinheck: Propeller, bootloader stand-in, SD card, RTC and NVRAM in PinMAME; pinheck parent set"
```

### Task 7: Record the findings

**Files:**
- Modify: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§2.3, §4.2, §4.4–§4.6, §5.1–§5.3, §6, §7, §10), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`

- [ ] **Step 1: Update the spec and roadmap**

```sh
python3 - docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md <<'EOF'
import sys
prev = {"minus": ["- **`prop.c`** owns the Propeller instance and the shared-pin edge log (timestamped PIC32 writes to Propeller-input pins). It converts PIC32 cycles to Propeller cycles by ×13/10 and implements the catch-up rules in §5.", "- `pinheck`: a BIOS set holding `p8x32a.rom` (32 KB, hub `$8000–$FFFF`), CRC-audited.", "PinMAME schedules the PIC32 at 80 MHz. Propeller cycle = PIC32 cycle × 13 / 10, exactly. `prop_run_until(t)` never moves backwards.", "Every PIC32 write that changes a pin wired to a Propeller input is appended to the edge log with its PIC32 timestamp. The Propeller's `INA` is evaluated at the reading cog's local cycle from the log. `WAITPEQ`/`WAITPNE` resolve to the cycle of the first matching edge. Appending to a full log first runs `prop_run_until(now)`, so edges are never dropped. Entries older than the Propeller's current cycle are discarded.", "- at every periodic quantum timer, so the Propeller keeps producing video and audio while the PIC32 is not reading it"], "plus": ["- **`prop.c`** owns the Propeller instance, its CAT24M01 on P28/P29 and the shared-pin edge log (PIC32 writes to P25/P26, stored in PIC32 cycles). It converts PIC32 cycles to Propeller cycles through the clock map of §5.1 and implements the catch-up rules of §5.2–§5.3. Undriven pins read 0 except where the board pulls them up: SD `DO` and `CS`, P13 and SCL/SDA; P31 (RX) floats and reads 0. The SD card attaches through a `(cs, sclk, mosi) → DO` callback. A `CLKSET` with the reset bit restarts the chip at the end of the catch-up in which it executed.", "- `pinheck`: a `NOT_A_DRIVER` parent set holding `p8x32a.rom` (32 KB, hub `$8000–$FFFF`, CRC32 `f99b3070`, SHA1 `b7b4fdf4f096db7d18bda6355725cb42ae4a9378`). It is GPL 3.0 and user-supplied, never committed. `dominos` is its clone, so PinMAME finds the ROM in `dominos.zip` or `pinheck.zip`.", "PinMAME schedules the PIC32 at 80 MHz. The Propeller clock follows its own `CLKSET`: after reset it runs on RCFAST, taken as the nominal 12 MHz (3/20 of the PIC32 rate); XIN and PLL1×–16× derive from the board's 6.5 MHz crystal, so PLL16× (`CLKSET $6F`, 104 MHz) is exactly 13/10. `prop.c` keeps a piecewise-linear map from PIC32 cycles to Propeller cycles with one segment per `CLKSET`. PIC32 cycles are offset by an epoch that grows by the PIC32's cycle count at each PinMAME reset, so Propeller time never moves backwards.", "Every PIC32 write that changes a pin wired to a Propeller input is appended to the edge log with its PIC32 timestamp; it becomes visible at Propeller cycle ⌊map(P)⌋, converted when the Propeller reads it, so a `CLKSET` between logging and use is honoured. The Propeller's `INA` is evaluated at the reading cog's local cycle from the log. `WAITPEQ`/`WAITPNE` resolve to the cycle of the first matching edge. A catch-up for PIC32 cycle R runs the Propeller to map(R) − 1, so a later PIC32 write always lands on a Propeller cycle not yet evaluated; results are then identical whether the Propeller is caught up every cycle or once. Appending to a full log first catches the Propeller up to that write, so edges are never dropped. Entries at or before the Propeller's current cycle are folded into its base pin state.", "- at every tick of a 1 kHz PinMAME timer, so the Propeller keeps producing video and audio while the PIC32 is not reading it\n- at every step of a bootloader hold (§4.4), before the stand-in decodes the Propeller's P25 edges"]}
spec_pairs = list(zip(prev['minus'], prev['plus']))
spec_pairs += [
    ("Per the wiki: the PIC32 keeps its non-volatile data in the top half of the Propeller's EEPROM, and the PIC32 main loop runs at about 10 kHz.",
     "Per the wiki: the PIC32 keeps its non-volatile data in the top half of the Propeller's EEPROM, and the PIC32 main loop runs at about 10 kHz. It also keeps its settings in U13 on I2C1, next to the DS1340.\n\n"
     "Before serving the link, the Propeller checks its EEPROM long `$8000`. If its low 16 bits are not `$BAFA`, or P13 (`PROP_CONFIG`) is held low, or the long at `$8004` is `$ABBA0001` (written by the PIC32's `UPDATE CODE` action through `writeEEPROM`, word 1), it runs its update: `FLASHING TO:`, then a serial driver on P24 (RX) / P25 (TX) at 115,200 baud sends an STK500v2 `CMD_SIGN_ON` (`1B 00 00 01 0E 01 15`) to the PIC32 bootloader about 1.7 s after power-on and waits without timeout for an answer with status 0 and a signature starting `ST`. It then clears `$8004`, loads address 0 and programs the card's `DOM_V006.PRG` with `CMD_PROGRAM_FLASH_ISP` in 128/128/256-byte blocks, reads it back with `CMD_READ_FLASH_ISP` (256 bytes), reprograms and verifies its own EEPROM from the card's `PRP_V008.BIN` (`PRP V8 FOUND`, `PROGRAM AV EEPROM`, `VERIFY AV EEPROM`), writes `$8004 = $ABBA0002` and `$8000 = (v << 24) | $BAFA` (v = the digits after `_V`, 6 for `DOM_V006.PRG`), sends `CMD_LEAVE_PROGMODE_ISP` and shows `PLEASE RESTART` (about 135 s after power-on in total); it does not serve the game link in that power cycle. The Propeller drives neither MCLR nor RG9. Link pins RF12/RF13 are U3BRX/U3BTX (UART5) on the PIC32MX795. The open-source `Bootloader_Max32.hex` talks on UART1 and is CC BY-NC-SA 4.0, so it is not used."),
    ("Memory map: 128 KB data RAM `0x00000000` (KSEG0 `0x80000000`, KSEG1 `0xA0000000`); 512 KB program flash `0x1D000000`; boot flash `0x1FC00000` holding a synthesised stub that jumps to `0x9D001000`;",
     "Memory map: 128 KB data RAM `0x00000000` (KSEG0 `0x80000000`, KSEG1 `0xA0000000`); 512 KB program flash `0x1D000000`; boot flash `0x1FC00000` holding a synthesised stub that jumps to `0x9D001000`, preceded by the bootloader stand-in of §4.4, which holds the core through the board's `hold` callback;"),
    ("- **`rtc.c`** is a DS1340 seeded from the host clock.",
     "- **`rtc.c`** is a DS1340 on I2C1 (device `$68`), sharing the bus with U13 (wired-AND). It is seeded from the host's local time at every reset and advances with PIC32 cycles; writes set the clock. Without it the firmware stops on the `SET DATETIME` screen on every boot after the first.\n"
     "- **`bootldr.c`** is a high-level stand-in for the PIC32 bootloader, whose binary is not available. After every PIC32 reset it holds the core for a boot window and listens on UART5 (RF12 in, RF13 out, 8N1, BRG 42) for STK500v2 frames. `CMD_SIGN_ON` answers `STK500_2` and keeps the core held; `LOAD_ADDRESS`, `PROGRAM_FLASH_ISP` (byte addresses from `0x9D000000`, erasing each 4 KB page on its first write) and `READ_FLASH_ISP` act on the emulated program flash; `LEAVE_PROGMODE_ISP` starts the application once its reply has been sent. Other commands answer `STATUS_CMD_FAILED` (`$C0`) and are logged once. Without a sign-on the window ends and the application starts at `0x9D001000`."),
    ("builds a FAT32 volume from the romset zip's `DMD/` and `SFX/` entries:",
     "builds a FAT32 volume from the romset zip's root files and its `DMD/` and `SFX/` entries (the Propeller's update reads `DOM_V006.PRG` and `PRP_V008.BIN` from the card root):"),
    ("- **NVRAM**: both EEPROM images. The DS1340 is not persisted.",
     "- **NVRAM**: U13 (128 KB) and the Propeller EEPROM above the 32 KB `PRP_V008.BIN` image, through PinMAME's NVRAM handler. A first run starts blank, so the Propeller runs its update against `bootldr.c` and stops at `PLEASE RESTART`; the next reset or launch boots the game. Program flash written by the update persists across resets within a session and is reloaded from the romset's `DOM_V006.PRG` at every launch (decision D2, §10). The DS1340 is not persisted."),
    ("2. `PROPELLER SYNC CHECK` succeeds, and `[E00000]` returns the version.",
     "2. On blank NVRAM the Propeller's update runs to `PLEASE RESTART` against the bootloader stand-in, programming and verifying the card's `DOM_V006.PRG` and writing its EEPROM record; after a restart `PROPELLER SYNC CHECK` succeeds against the emulated Propeller, the banner appears, and `[E97000]` injected on UART1 returns `Ball Search: DISABLED`. (`[E00000]`, which the help text documents as returning the version, prints an empty line in `DOM_V006`.)"),
    ("There is no Propeller→PIC32 UART. The return path is RF13",
     "The game link has no Propeller→PIC32 UART. Its return path is RF13"),
    ("The first rule above therefore makes the return path exact.",
     "The first rule above therefore makes the return path exact. The update path (§2.3) is a UART in both directions: every change of the Propeller's P25 output reaches the bootloader stand-in (§4.4) with its PIC32 timestamp, and the stand-in's replies on RF13 enter the edge log as P24, like PIC32 pin writes."),
    ("- **SD write commands**: accepted, data discarded, logged.",
     "- **SD write commands**: accepted, data discarded, logged.\n- **Unsupported STK500v2 command** (bootloader stand-in): answered with `STATUS_CMD_FAILED`, logged once per command."),
    ("- The PIC32 boot-flash bootloader and the firmware-update path (`UPDATE CODE`, Propeller-driven PIC32 reflash).",
     "- The PIC32 bootloader binary itself. Decision D1: the bootloader is emulated at protocol level by `bootldr.c` (§4.4), so the Propeller's update path and `UPDATE CODE` work unmodified. Decision D2: program flash written by an update persists until the next launch, which reloads the romset's `DOM_V006.PRG`; since the card is the same romset zip, the images are normally identical."),
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
     "| `2026-09-28-pinheck-m5-link.md` | 5: link: edge log, catch-up on reads of RF13, bootloader stand-in, DS1340, EEPROM NVRAM, `pinheck` BIOS set | Plans 2, 3, 4 (executed) | needs **1**; decisions **D1** (bootloader stand-in), **D2** (flash persistence) | machine check 2: update to `PLEASE RESTART` on blank NVRAM, then `PROPELLER SYNC CHECK`, banner and `[E97000]` | written |"),
    ("- NVRAM for both CAT24M01 images; without it every PinMAME launch is a first boot and the banner never appears.\n",
     ""),
    ("## Carried into Plan 7\n",
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
git commit -m "Spec and roadmap: Propeller update path, bootloader stand-in (D1), flash persistence (D2), link timing, RTC, NVRAM"
```
