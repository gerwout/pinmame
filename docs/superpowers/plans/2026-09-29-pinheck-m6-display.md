# Milestone 6: display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the machine's 128×32 colour display in `sdl3pinmame`, pixel-exact, as the emulated Propeller running `PRP_V008.BIN` sends it.

**Architecture:** The Propeller does not use its video generator for the display. It sends finished 4096-byte RGB332 frames to a display module over a clocked serial link (P22 clock, P21 data, P20 latch, P17 config). `display.c` models that module from the Propeller's pin edges, as delivered by Plan 5's Propeller device through a new pin-observer hook. The driver's `CORE_VIDEO` renderer shows the last complete frame. Correctness is proven headless (unit tests and a Propeller-only boot whose frames must equal the firmware framebuffer) and in PinMAME (`[V00LT5]` must show that clip pixel-exact, in order).

**Tech Stack:** C99/C89-clean C, PinMAME 0.37-style driver (`CORE_VIDEO`, `PINMAME_VIDEO_UPDATE`), Python 3 for the frame checks, cmake / makefile / VS projects via Plan 5's `register_build.py`.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m6-m7-display-audio-design.md` (binding), parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` where the addendum is silent.

## Prerequisites

- Branch from `pinheck` at or after `f7eeb43c`, which contains Plan 5 merged as `d5ea4107`: `src/wpc/pinheck/prop.[ch]` (Propeller device), `src/wpc/pinheck.c` (driver with the black `pinheck_video` stub and the `PINHECK_UART1_LOG` / `PINHECK_PROP_LOG` / `PINHECK_UART1_SEND[_AT]` / `PINHECK_INSERVICE` hooks), `tests/pinheck/link/register_build.py`.
- Environment for every firmware step (the same as Plan 5's):

```sh
export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip
```

`TOOLS` and `P8X32A_ROM` point at Plan 3's oracle build and the user-supplied mask ROM (crc32 `f99b3070`). `PINHECK_ZIP` is a Domino's update zip; `PINHECK_UPDATE_DIR` the unzipped update (the clip check reads `.VID` files from it).

## Global Constraints

- `src/wpc/pinheck/display.[ch]` include only `<stdint.h>`, `<stdio.h>`, `<string.h>` and their own header; no PinMAME headers; C89-syntax-clean (`cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only`); `extern "C"` guards.
- API exactly as the addendum §4.1: `display_init(display *d, void *ctx, on_frame, on_config, log)`, `display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir)`, `on_frame(ctx, const uint8_t *frame4096, uint64_t t)`, `on_config(ctx, const uint8_t *bytes, int n, uint64_t t)`, `log(ctx, const char *msg)`.
- A pin counts only while driven: its level is `out & dir`.
- Frames are exactly 4096 bytes; anything else is discarded with one log line per kind, and the previous frame stays shown. Before the first frame the screen is black.
- Config packets are logged when they change and kept; they are never interpreted (addendum §3).
- The driver's changes stay inside the new `pinheck_disp_*` functions and one call each in `MACHINE_INIT`, `MACHINE_RESET` and `MACHINE_STOP`; Plan 7 changes the same file in other functions.
- Test hook: `PINHECK_FRAME_LOG=path` appends one record per frame: Propeller cycle (u64 LE), PIC32 cycle (u64 LE), hub address of the matching firmware framebuffer or `0xFFFFFFFF` (u32 LE), 4096 frame bytes.
- Nothing bulky in `/tmp`. Build PinMAME into the ignored `build/sdl3pinmame`, never into `build-dbg`, and never commit the root `CMakeLists.txt` copy.
- Short code comments.

## Findings this plan rests on

Measured by booting `PRP_V008.BIN` headless with the card (Plan 4 code) and in PinMAME with the firmware:

- The latch pulse on P20 carries one rising edge of P22 of its own; it is not data. A transfer is latched on the rising edge of P20, and P17 is high during a config packet.
- The config packet is 14 bytes, `00 b1 01 54 00 00 00 ff 00 80 00 20 00 3e`, sent once at start-up.
- Frames are sent only when the picture changes: every 33 ms while a clip plays at 30 fps, every 46 ms on the score screen.
- The firmware framebuffer is at hub `$5870`; every frame sent equals it when latched. (Plan 5 recorded `$57EE`–`$67ED` as the region; the buffer starts at `$5870`.)
- `[V00ABC]` calls `video(folder, clip0, clip1, attributes 0, progressBar 0, priority $FF)`. It shows the clip from frame 1 (frame 0 is never sent), then the firmware's score screen returns.
- After `[E96000]` the score screen covers a clip started by `[V00ABC]`, so the check sends `[V00ABC]` alone. The console answers only once the machine has initialised its settings, so the check runs two launches on one NVRAM directory.
- `_DL/LT5.VID` (34 distinct frames, 117 colours) is the check clip: `_DA/AB1.VID` repeats two images, so it cannot prove frame order.

## Review Focus

Each line is an input the firmware or its host produces; each is pinned by the named test.

- **The latch pulse's own clock edge.** Counting it as data shifts every later bit. `config_packet` and `short_and_long_frames` in Task 1 (the mutant fails at `display_test.c:86`).
- **Pins not yet driven.** Before `DIRA` is set, and whenever a cog releases the pins, `OUTA` levels must not clock bits. `latch_clock_and_undriven` in Task 1 (fails at `display_test.c:147`).
- **A transfer cut short or overrunning** (the firmware switches screens mid-send, or a frame and extra bytes run together): the previous frame stays, one log line. `short_and_long_frames` in Task 1.
- **P17 changing during a transfer** (config and frame data running together): discarded. `partial_byte_and_mode_change` in Task 1.
- **A clip start that skips frame 0, and the score screen interrupting a clip.** The check must accept the firmware's real behaviour and still prove order: frames from the first one shown (0 or 1) to the last, contiguous and pixel-exact. `frames.py --vid` in Task 3.

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/pinheck/display.[ch]` | display module model: bits, latch, frame and config delivery |
| `src/wpc/pinheck/prop.[ch]` (modify) | pin-observer hook `prop_set_pins` |
| `src/wpc/pinheck.c` (modify) | `pinheck_disp_*` block, RGB332 renderer, `PINHECK_FRAME_LOG` |
| `tests/pinheck/display/display_test.c` | unit tests from synthetic pin sequences |
| `tests/pinheck/display/dispboot.c` | headless Propeller boot with the card and the display attached |
| `tests/pinheck/display/frames.py` | frame-log reader: rates, framebuffer check, clip match |
| `tests/pinheck/display/pinmame_display.sh` | PinMAME check 3 |
| `tests/pinheck/display/check.sh`, `.gitignore` | suite driver |
| `tests/pinheck/link/register_build.py` (modify) | registers `display.[ch]` in every build file |
| parent spec (modify) | §4.3, §4.5, §5.5, §7 check 3, §8 row 6, §9 item 2 |

---
### Task 1: Display module model

**Files:**
- Create: `src/wpc/pinheck/display.h`, `src/wpc/pinheck/display.c`
- Test: `tests/pinheck/display/display_test.c`, `tests/pinheck/display/check.sh`, `tests/pinheck/display/.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: `display.h` as below. `DISPLAY_W` 128, `DISPLAY_H` 32, `DISPLAY_FRAME` 4096, `DISPLAY_CFG_MAX` 64, pin masks `DISPLAY_P17/P20/P21/P22`. Log texts: `display: frame of N bytes discarded`, `display: latch after N bits, discarded`, `display: mode changed during a N-byte transfer, discarded`, `display: N-byte config packet, first 64 kept`.

- [ ] **Step 1: Write the failing tests and the suite driver**

`tests/pinheck/display/display_test.c`:

```c
#include "display.h"
#include <stdio.h>
#include <string.h>

static display d;
static uint64_t now;
static uint32_t pins;
static uint8_t last_frame[DISPLAY_FRAME], last_cfg[DISPLAY_CFG_MAX];
static int frames, configs, cfg_n, logs;
static char last_log[128];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("DISPLAY FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void on_frame(void *ctx, const uint8_t *f, uint64_t t) { (void)ctx; (void)t; memcpy(last_frame, f, DISPLAY_FRAME); frames++; }
static void on_config(void *ctx, const uint8_t *b, int n, uint64_t t) { (void)ctx; (void)t; memcpy(last_cfg, b, (size_t)n); cfg_n = n; configs++; }
static void on_log(void *ctx, const char *m) { (void)ctx; strncpy(last_log, m, sizeof(last_log) - 1); logs++; }

static void set(uint32_t mask, int on)
{
	pins = on ? pins | mask : pins & ~mask;
	display_pins(&d, ++now, pins, 0xFFFFFFFFu);
}

static void byte(unsigned v)
{
	int b;
	for (b = 7; b >= 0; b--) {
		set(DISPLAY_P21, (v >> b) & 1);
		set(DISPLAY_P22, 1);
		set(DISPLAY_P22, 0);
	}
}

static void bits(int n)
{
	while (n--) { set(DISPLAY_P22, 1); set(DISPLAY_P22, 0); }
}

static void strobe(void)
{
	set(DISPLAY_P20, 1);
	set(DISPLAY_P22, 1);
	set(DISPLAY_P22, 0);
	set(DISPLAY_P20, 0);
}

static void reset(void)
{
	display_init(&d, NULL, on_frame, on_config, on_log);
	pins = 0;
	frames = configs = cfg_n = logs = 0;
	last_log[0] = 0;
	memset(last_frame, 0, sizeof(last_frame));
}

static void frame_and_bit_order(void)
{
	int i;
	reset();
	for (i = 0; i < DISPLAY_FRAME; i++) byte((unsigned)(i * 7 + 1) & 0xFF);
	CHECK(frames == 0);
	strobe();
	CHECK(frames == 1);
	for (i = 0; i < DISPLAY_FRAME && last_frame[i] == ((i * 7 + 1) & 0xFF); i++) ;
	CHECK(i == DISPLAY_FRAME);
	CHECK(logs == 0);
	reset();
	byte(0x80);
	bits((DISPLAY_FRAME - 1) * 8);
	strobe();
	CHECK(frames == 1 && last_frame[0] == 0x80 && last_frame[1] == 0x00);
}

static void config_packet(void)
{
	static const uint8_t seen[14] = { 0x00, 0xb1, 0x01, 0x54, 0x00, 0x00, 0x00, 0xff, 0x00, 0x80, 0x00, 0x20, 0x00, 0x3e };
	int i;
	reset();
	strobe();
	CHECK(frames == 0 && configs == 0 && logs == 0);
	set(DISPLAY_P17, 1);
	for (i = 0; i < 14; i++) byte(seen[i]);
	strobe();
	set(DISPLAY_P17, 0);
	CHECK(configs == 1 && cfg_n == 14 && memcmp(last_cfg, seen, 14) == 0);
	CHECK(frames == 0 && logs == 0);
}

static void short_and_long_frames(void)
{
	int i;
	reset();
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x55);
	strobe();
	for (i = 0; i < 100; i++) byte(0xAA);
	strobe();
	CHECK(frames == 1 && last_frame[0] == 0x55);
	CHECK(logs == 1 && strcmp(last_log, "display: frame of 100 bytes discarded") == 0);
	for (i = 0; i < 200; i++) byte(0xAA);
	strobe();
	CHECK(logs == 1);
	reset();
	for (i = 0; i < DISPLAY_FRAME + 1; i++) byte(0x11);
	strobe();
	CHECK(frames == 0 && strcmp(last_log, "display: frame of 4097 bytes discarded") == 0);
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x22);
	strobe();
	CHECK(frames == 1 && last_frame[0] == 0x22 && last_frame[DISPLAY_FRAME - 1] == 0x22);
}

static void partial_byte_and_mode_change(void)
{
	int i;
	reset();
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x33);
	bits(3);
	strobe();
	CHECK(frames == 0 && strcmp(last_log, "display: latch after 32771 bits, discarded") == 0);
	reset();
	for (i = 0; i < 8; i++) byte(0x44);
	set(DISPLAY_P17, 1);
	for (i = 0; i < 6; i++) byte(0x44);
	strobe();
	CHECK(configs == 0 && strcmp(last_log, "display: mode changed during a 14-byte transfer, discarded") == 0);
}

static void latch_clock_and_undriven(void)
{
	int i;
	reset();
	display_pins(&d, ++now, DISPLAY_P20 | DISPLAY_P22, 0);
	display_pins(&d, ++now, 0, DISPLAY_P20 | DISPLAY_P21 | DISPLAY_P22);
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x66);
	set(DISPLAY_P20, 1);
	set(DISPLAY_P21, 1);
	set(DISPLAY_P22, 1);
	set(DISPLAY_P22, 0);
	set(DISPLAY_P20, 0);
	CHECK(frames == 1 && logs == 0);
	for (i = 0; i < DISPLAY_FRAME; i++) {
		byte(0x77);
		display_pins(&d, ++now, pins | DISPLAY_P21 | DISPLAY_P22, DISPLAY_P20 | DISPLAY_P17);
		display_pins(&d, ++now, pins, 0xFFFFFFFFu);
	}
	strobe();
	CHECK(frames == 2 && last_frame[0] == 0x77 && logs == 0);
}

int main(void)
{
	frame_and_bit_order();
	config_packet();
	short_and_long_frames();
	partial_byte_and_mode_change();
	latch_clock_and_undriven();
	printf("display: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

`tests/pinheck/display/check.sh`:

```sh
#!/bin/sh
for v in PINHECK_UPDATE_DIR PINHECK_ZIP P8X32A_ROM SDL3PINMAME; do
	eval "x=\$$v"
	if [ -n "$x" ]; then x=$(realpath "$x") || exit 2; eval "$v=\$x"; fi
done
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic"
mkdir -p $B
fail=0
cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/display.c || fail=$((fail + 1))
cc $CF -I$S/wpc/pinheck -o $B/display_test display_test.c $S/wpc/pinheck/display.c || exit 2
./$B/display_test || fail=$((fail + 1))
if [ "$PINHECK_SKIP_FIRMWARE" != 1 ] && [ -n "$PINHECK_UPDATE_DIR" ] && [ -n "$P8X32A_ROM" ]; then
	zip=$PINHECK_ZIP
	if [ -z "$zip" ]; then
		zip=$PWD/$B/dominos-stored.zip
		[ -f "$zip" ] || (cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$zip" DOM_V006.PRG PRP_V008.BIN DMD SFX) || exit 2
	fi
	cc $CF -I$S/wpc/pinheck -I$S/cpu/p8x32a -o $B/dispboot dispboot.c $S/wpc/pinheck/display.c $S/cpu/p8x32a/p8x32a.c \
		$S/wpc/pinheck/eeprom.c $S/wpc/pinheck/sd.c $S/wpc/pinheck/vfat.c $S/wpc/pinheck/zipsrc.c -lz || exit 2
	if timeout 600 ./$B/dispboot -rom "$P8X32A_ROM" -prp "$PINHECK_UPDATE_DIR/PRP_V008.BIN" -zip "$zip" > $B/dispboot.txt; then
		grep "^dispboot:" $B/dispboot.txt
	else
		echo "DISPBOOT FAIL"; grep -v "^[#.]*$" $B/dispboot.txt | tail -8; fail=$((fail + 1))
	fi
	if [ -n "$SDL3PINMAME" ]; then
		PINHECK_ZIP=$zip sh ./pinmame_display.sh || fail=$((fail + 1))
	fi
elif [ "$PINHECK_SKIP_FIRMWARE" != 1 ]; then
	echo "FIRMWARE MISSING: set PINHECK_UPDATE_DIR and P8X32A_ROM, or PINHECK_SKIP_FIRMWARE=1"; fail=$((fail + 1))
fi
echo "display: $fail failed"
[ $fail -eq 0 ]
```

Make it executable: `chmod +x tests/pinheck/display/check.sh`

`tests/pinheck/display/.gitignore`:

```text
build/
```

- [ ] **Step 2: Run to confirm they fail**

Run: `PINHECK_SKIP_FIRMWARE=1 tests/pinheck/display/check.sh; echo "exit=$?"`
Expected:
```text
cc1: fatal error: ../../../src/wpc/pinheck/display.c: No such file or directory
compilation terminated.
display_test.c:1:10: fatal error: display.h: No such file or directory
    1 | #include "display.h"
      |          ^~~~~~~~~~~
compilation terminated.
cc1: fatal error: ../../../src/wpc/pinheck/display.c: No such file or directory
compilation terminated.
exit=2
```

- [ ] **Step 3: Write the display module model**

`src/wpc/pinheck/display.h`:

```c
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
```

`src/wpc/pinheck/display.c`:

```c
#include "display.h"
#include <stdio.h>
#include <string.h>

#define PINS (DISPLAY_P17 | DISPLAY_P20 | DISPLAY_P21 | DISPLAY_P22)

static void say(display *d, int *once, const char *msg)
{
	if (*once) return;
	*once = 1;
	if (d->log) d->log(d->ctx, msg);
}

static void latch(display *d, uint64_t t, int cfg)
{
	char msg[80];
	long n = d->nbits / 8;

	if (d->nbits == 0) return;
	if (d->nbits % 8) {
		sprintf(msg, "display: latch after %ld bits, discarded", d->nbits);
		say(d, &d->logged_bits, msg);
	} else if (d->mode != cfg) {
		sprintf(msg, "display: mode changed during a %ld-byte transfer, discarded", n);
		say(d, &d->logged_mixed, msg);
	} else if (cfg) {
		if (n > DISPLAY_CFG_MAX) {
			sprintf(msg, "display: %ld-byte config packet, first %d kept", n, DISPLAY_CFG_MAX);
			say(d, &d->logged_cfg, msg);
			n = DISPLAY_CFG_MAX;
		}
		if (d->on_config) d->on_config(d->ctx, d->buf, (int)n, t);
	} else if (n != DISPLAY_FRAME) {
		sprintf(msg, "display: frame of %ld bytes discarded", n);
		say(d, &d->logged_frame, msg);
	} else if (d->on_frame)
		d->on_frame(d->ctx, d->buf, t);
	d->nbits = 0;
}

void display_init(display *d, void *ctx, display_frame_fn on_frame, display_config_fn on_config, display_log_fn log)
{
	memset(d, 0, sizeof(*d));
	d->ctx = ctx;
	d->on_frame = on_frame;
	d->on_config = on_config;
	d->log = log;
}

void display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir)
{
	uint32_t now = out & dir & PINS, old = d->level;

	d->level = now;
	if ((now & DISPLAY_P20) && !(old & DISPLAY_P20)) {
		latch(d, t, (now & DISPLAY_P17) != 0);
		return;
	}
	if ((now & DISPLAY_P22) && !(old & DISPLAY_P22) && !(now & DISPLAY_P20)) {
		long i = d->nbits >> 3;
		if (d->nbits == 0) d->mode = (now & DISPLAY_P17) != 0;
		if (i < DISPLAY_FRAME) {
			if ((d->nbits & 7) == 0) d->buf[i] = 0;
			if (now & DISPLAY_P21) d->buf[i] |= (uint8_t)(0x80 >> (d->nbits & 7));
		}
		d->nbits++;
	}
}
```

- [ ] **Step 4: Run to verify they pass**

Run: `PINHECK_SKIP_FIRMWARE=1 tests/pinheck/display/check.sh`
Expected:
```text
display: ok
display: 0 failed
```

- [ ] **Step 5: Prove the tests bite**

For each row, make the replacement in `src/wpc/pinheck/display.c`, run Step 4's command, confirm the named failure, then restore with `git checkout src/wpc/pinheck/display.c` (commit first if the file is not yet tracked: do Step 6, then these).

| Replace | With | Must fail at |
|---|---|---|
| `d->buf[i] \|= (uint8_t)(0x80 >> (d->nbits & 7));` | `d->buf[i] \|= (uint8_t)(1 << (d->nbits & 7));` | `display_test.c:66` |
| `!(old & DISPLAY_P22) && !(now & DISPLAY_P20)` | `!(old & DISPLAY_P22)` | `display_test.c:86` |
| `} else if (n != DISPLAY_FRAME) {` | `} else if (n < DISPLAY_FRAME) {` | `display_test.c:106` |
| `\t} else if (d->mode != cfg) {` | `\t} else if (0) {` | `display_test.c:125` |
| `uint32_t now = out & dir & PINS` | `uint32_t now = (out \| (dir & 0)) & PINS` | `display_test.c:147` |

Then `git status --short src/` prints nothing.

- [ ] **Step 6: Commit**

```bash
git add src/wpc/pinheck/display.h src/wpc/pinheck/display.c tests/pinheck/display
git commit -m "pinheck: display module model for the Propeller's serial frame link"
```

### Task 2: Propeller boot with the display attached

**Files:**
- Create: `tests/pinheck/display/dispboot.c`

**Interfaces:**
- Consumes: `display.[ch]` (Task 1); Plan 3's `p8x32a.[ch]` and `eeprom.[ch]`; Plan 4's `sd`, `vfat`, `zipsrc`.
- Produces: `build/dispboot -rom p8x32a.rom -prp PRP_V008.BIN -zip dominos.zip [-cycles n]` (default 60,000,000 Propeller cycles). It prints each frame's hub address and each config packet, the last frame as `#`/`.`, and `dispboot: N frames (U uniform), C config packets, framebuffer $XXXX, M frames not in hub RAM`. It exits 0 only with at least one non-uniform frame, at least one config packet, and every non-uniform frame found at one hub address.

Without a PIC32 the Propeller reaches its update path and draws `FLASHING TO: / CONNECT PIC32: FATAL / PLEASE RESTART` about 0.5 s in. That screen is the evidence here.

- [ ] **Step 1: Run the firmware part of the suite to confirm it fails**

Run: `export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && tests/pinheck/display/check.sh; echo "exit=$?"`
Expected:
```text
display: ok
cc1: fatal error: dispboot.c: No such file or directory
compilation terminated.
exit=2
```

- [ ] **Step 2: Write the boot test**

`tests/pinheck/display/dispboot.c`:

```c
#include "p8x32a.h"
#include "eeprom.h"
#include "sd.h"
#include "vfat.h"
#include "zipsrc.h"
#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static p8x32a chip;
static cat24m01 ee;
static uint8_t eemem[0x20000];
static sd_card sd;
static vfat vf;
static zipsrc zs;
static display disp;
static uint32_t sdbit = 1, eebits = 0x30000000u;
static int frames, configs, missing, uniform;
static long fb_addr = -1;
static uint8_t last[DISPLAY_FRAME];

static long find(const uint8_t *f)
{
	long a;
	for (a = 0; a + DISPLAY_FRAME <= 0x8000; a++)
		if (chip.hub[a] == f[0] && !memcmp(chip.hub + a, f, DISPLAY_FRAME)) return a;
	return -1;
}

static void on_frame(void *ctx, const uint8_t *f, uint64_t t)
{
	long a;
	int k;
	(void)ctx;
	frames++;
	memcpy(last, f, DISPLAY_FRAME);
	for (k = 1; k < DISPLAY_FRAME && f[k] == f[0]; k++) ;
	if (k == DISPLAY_FRAME) { uniform++; printf("frame %d at %llu: uniform %02x\n", frames, (unsigned long long)t, f[0]); return; }
	a = find(f);
	if (a < 0 || (fb_addr >= 0 && a != fb_addr)) missing++;
	if (fb_addr < 0) fb_addr = a;
	printf("frame %d at %llu: hub %s%04lx\n", frames, (unsigned long long)t, a < 0 ? "none " : "$", a < 0 ? 0L : a);
}

static void on_config(void *ctx, const uint8_t *b, int n, uint64_t t)
{
	int k;
	(void)ctx;
	configs++;
	printf("config at %llu:", (unsigned long long)t);
	for (k = 0; k < n; k++) printf(" %02x", b[k]);
	printf("\n");
}

static void on_log(void *ctx, const char *m) { (void)ctx; printf("%s\n", m); }

static uint32_t pins_in(void *ctx, uint64_t t)
{
	(void)ctx; (void)t;
	return (sdbit & 1u) | eebits;
}

static uint64_t pins_next(void *ctx, uint64_t t) { (void)ctx; (void)t; return P8X32A_NEVER; }

static void pins_out(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	int scl = (dir >> 28 & 1) ? (int)(out >> 28 & 1) : 1;
	int sda = (dir >> 29 & 1) ? (int)(out >> 29 & 1) : 1;
	(void)ctx;
	eebits = 0x10000000u | (uint32_t)cat24m01_update(&ee, scl, sda) << 29;
	sdbit = (uint32_t)sd_update(&sd, (dir >> 3 & 1) ? (int)(out >> 3 & 1) : 1, (dir >> 1 & 1) ? (int)(out >> 1 & 1) : 0, (dir >> 2 & 1) ? (int)(out >> 2 & 1) : 1);
	display_pins(&disp, t, out, dir);
}

static int sd_read(void *ctx, uint32_t lba, uint8_t *buf) { (void)ctx; return vfat_read(&vf, lba, buf); }

static int load(const char *path, uint8_t *dst, size_t max, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return 0; }
	*n = fread(dst, 1, max, f);
	fclose(f);
	return 1;
}

int main(int argc, char **argv)
{
	p8x32a_bus bus;
	sd_blockdev dev;
	const char *rom = NULL, *prp = NULL, *zip = NULL;
	unsigned long long cycles = 60000000ull, t;
	size_t n;
	int i, y, x;

	for (i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "-rom")) rom = argv[i + 1];
		else if (!strcmp(argv[i], "-prp")) prp = argv[i + 1];
		else if (!strcmp(argv[i], "-zip")) zip = argv[i + 1];
		else if (!strcmp(argv[i], "-cycles")) cycles = strtoull(argv[i + 1], NULL, 0);
	}
	if (!rom || !prp || !zip) { fprintf(stderr, "usage: dispboot -rom p8x32a.rom -prp PRP_V008.BIN -zip dominos.zip [-cycles n]\n"); return 2; }
	memset(&bus, 0, sizeof(bus));
	bus.pins_in = pins_in;
	bus.pins_next = pins_next;
	bus.pins_out = pins_out;
	p8x32a_init(&chip, &bus);
	if (!load(rom, chip.hub + 0x8000, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "dispboot: rom must be 32768 bytes\n"); return 2; }
	memset(eemem, 0xFF, sizeof(eemem));
	if (!load(prp, eemem, 0x8000, &n)) return 2;
	cat24m01_init(&ee, eemem, 0);
	if (zipsrc_open(&zs, zip, 64u << 20) || vfat_init(&vf, zipsrc_source(&zs))) { fprintf(stderr, "dispboot: cannot open %s\n", zip); return 2; }
	dev.ctx = NULL;
	dev.sectors = vfat_sectors(&vf);
	dev.read = sd_read;
	sd_init(&sd, &dev);
	display_init(&disp, NULL, on_frame, on_config, on_log);
	for (t = 4095; t < cycles; t += 4096) p8x32a_run_until(&chip, t);
	if (frames) {
		for (y = 0; y < DISPLAY_H; y++) {
			for (x = 0; x < DISPLAY_W; x++) putchar(last[y * DISPLAY_W + x] ? '#' : '.');
			putchar('\n');
		}
	}
	printf("dispboot: %d frames (%d uniform), %d config packets, framebuffer $%04lx, %d frames not in hub RAM\n", frames, uniform, configs, fb_addr < 0 ? 0L : fb_addr, missing);
	return frames - uniform > 0 && configs > 0 && missing == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Run it**

Run: `tests/pinheck/display/check.sh` (environment as in Step 1)
Expected:
```text
display: ok
dispboot: 4 frames (0 uniform), 1 config packets, framebuffer $5870, 0 frames not in hub RAM
display: 0 failed
```

`tests/pinheck/display/build/dispboot.txt` holds the frame lines and the last frame drawn as `#`/`.`: the update screen, reading `FLASHING TO:`, `CONNECT PIC32: FATAL`, `PLEASE RESTART`.

- [ ] **Step 4: Commit**

```bash
git add tests/pinheck/display/dispboot.c
git commit -m "pinheck: headless Propeller boot with the display module attached"
```

### Task 3: PinMAME integration (machine check 3)

**Files:**
- Create: `tests/pinheck/display/frames.py`, `tests/pinheck/display/pinmame_display.sh`
- Modify: `src/wpc/pinheck/prop.h`, `src/wpc/pinheck/prop.c`, `src/wpc/pinheck.c`, `tests/pinheck/link/register_build.py`, and through it the cmake lists, `src/pinmame.mak` and the VS projects

**Interfaces:**
- Consumes: `display.[ch]` (Task 1); Plan 5's `pinheck_prop`, `pinheck_prop_log`, `pic32cpu_soc()`, `PINHECK_*` hooks.
- Produces:
  - `typedef void (*prop_pins_fn)(void *ctx, uint64_t prop_cycle, uint32_t out, uint32_t dir); void prop_set_pins(pinheck_prop *p, prop_pins_fn fn, void *ctx);` called after the device's own pin handling for every Propeller pin-output change.
  - Driver: `pinheck_disp_init()` (palette, `PINHECK_FRAME_LOG`, hook), `pinheck_disp_reset()`, `pinheck_disp_stop()`, and `pinheck_video` drawing the last complete frame into 32-bit or 15-bit bitmaps.
  - `python3 tests/pinheck/display/frames.py LOG [--after S] [--before S] [--show K] [--vid FILE.VID]`, with S in PIC32 seconds.
  - `tests/pinheck/display/pinmame_display.sh`, printing `pinmame display: ok`.

- [ ] **Step 1: Write the frame checker and the PinMAME check**

`tests/pinheck/display/frames.py`:

```python
#!/usr/bin/env python3
import argparse
import struct
import sys

FRAME = 4096
PROP_HZ = 104e6
PIC_HZ = 80e6


def read_log(path):
    try:
        d = open(path, 'rb').read()
    except OSError:
        sys.exit('frames: FAIL, no frame log %s' % path)
    rec = 20 + FRAME
    if len(d) % rec:
        sys.exit('frames: %s is not a whole number of records' % path)
    return [struct.unpack_from('<QQI', d, i) + (d[i + 20:i + rec],) for i in range(0, len(d), rec)]


def read_vid(path):
    d = open(path, 'rb').read()
    if len(d) < 512 or (len(d) - 512) % FRAME:
        sys.exit('frames: %s is not a .VID file' % path)
    return [d[i:i + FRAME] for i in range(512, len(d), FRAME)]


def ascii(f):
    return '\n'.join(''.join('#' if f[y * 128 + x] else '.' for x in range(128)) for y in range(32))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--vid')
    ap.add_argument('--after', type=float, default=0.0, help='ignore frames latched before this many seconds of PIC32 time')
    ap.add_argument('--before', type=float, default=1e9, help='ignore frames latched from this many seconds of PIC32 time on')
    ap.add_argument('--show', type=int)
    a = ap.parse_args()
    recs = [r for r in read_log(a.log) if a.after <= r[1] / PIC_HZ < a.before]
    log = [(t, f) for t, _, _, f in recs]
    if not log:
        sys.exit('frames: no frames after %.1fs' % a.after)
    span = (log[-1][0] - log[0][0]) / PROP_HZ
    print('frames: %d frames from %.2fs to %.2fs, %.1f per second' % (len(log), recs[0][1] / PIC_HZ, recs[-1][1] / PIC_HZ, (len(log) - 1) / span if span > 0 else 0.0))
    hub = sorted(set(at for _, _, at, f in recs if f.count(f[0]) != FRAME))
    if not hub:
        sys.exit('frames: FAIL, every frame is uniform')
    if hub != [hub[0]] or hub[0] == 0xFFFFFFFF:
        sys.exit('frames: FAIL, decoded frames are not the firmware framebuffer (hub addresses %s)' % ['none' if h == 0xFFFFFFFF else '$%04x' % h for h in hub])
    print('frames: every non-uniform frame equals the firmware framebuffer at hub $%04x when latched' % hub[0])
    if a.show is not None:
        print(ascii(log[a.show][1]))
    if a.vid:
        vid = read_vid(a.vid)
        frames = [f for _, f in log]
        first = next((k for k, f in enumerate(frames) if f in (vid[0], vid[1])), None)
        if first is None:
            sys.exit('frames: FAIL, neither frame 0 nor frame 1 of %s appears' % a.vid)
        start = 0 if frames[first] == vid[0] else 1
        want, k = start + 1, first + 1
        while k < len(frames) and want < len(vid):
            if frames[k] == vid[want]:
                want += 1
            elif frames[k] != vid[want - 1]:
                break
            k += 1
        print('frames: %s: frames %d..%d of %d shown pixel-exact, contiguous and in order (log frames %d..%d)' % (a.vid.split('/')[-1], start, want - 1, len(vid), first, k - 1))
        if want != len(vid):
            sys.exit('frames: FAIL, clip interrupted at frame %d' % want)


if __name__ == '__main__':
    main()
```

`tests/pinheck/display/pinmame_display.sh`:

```sh
#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/pinmame
CLIP=${PINHECK_CLIP:-LT5}
SEND_AT=${PINHECK_CLIP_AT:-12}
rm -rf $B && mkdir -p $B/roms $B/nvram || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
launch() {
	(cd $B && PINHECK_INSERVICE=6 PINHECK_FRAME_LOG=$PWD/frames$1.bin PINHECK_UART1_LOG=$PWD/uart$1.log PINHECK_PROP_LOG=$PWD/prop$1.log \
		timeout 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram -headless -frames_to_run $2 -skip_gamewarnings -nothrottle > run$1.out 2>&1) \
		|| { echo "PINMAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
launch 1 1200
PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND="[V00$CLIP]" launch 2 900
fail=0
grep -aq "PROPELLER SYNC CHECK\.*OK" $B/uart2.log || { echo "PINMAME FAIL: no sync check"; fail=1; }
grep -aq "Playing Video" $B/uart2.log || { echo "PINMAME FAIL: [V00$CLIP] not acknowledged"; fail=1; }
grep -q "^display: config" $B/prop1.log || { echo "PINMAME FAIL: no display config packet"; fail=1; }
grep "^display: \(frame\|latch\|mode\)" $B/prop1.log $B/prop2.log && { echo "PINMAME FAIL: malformed display transfers"; fail=1; }
python3 frames.py $B/frames1.bin || fail=1
dir=$(echo "$CLIP" | cut -c1)
python3 frames.py $B/frames2.bin --after "$SEND_AT" --vid "$PINHECK_UPDATE_DIR/DMD/_D$dir/$CLIP.VID" || fail=1
[ $fail -eq 0 ] || exit 1
echo "pinmame display: ok"
```

Make it executable: `chmod +x tests/pinheck/display/pinmame_display.sh`

- [ ] **Step 2: Build PinMAME as it is and confirm the check fails**

Run:
```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt && cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null && timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/build.log 2>&1; tail -1 build/sdl3pinmame/build.log
export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3000 tests/pinheck/display/pinmame_display.sh; echo "exit=$?"
```
Expected (the build is the unchanged driver, which has no `PINHECK_FRAME_LOG`):
```text
[100%] Built target sdl3pinmame
PINMAME FAIL: no display config packet
frames: FAIL, no frame log build/pinmame/frames1.bin
frames: FAIL, no frame log build/pinmame/frames2.bin
exit=1
```

- [ ] **Step 3: Add the pin-observer hook and register `display.[ch]` in the build**

```sh
python3 - <<'EOF'
import sys


def rep(path, a, b):
    s = open(path, newline='').read()
    if b in s:
        return
    if s.count(a) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(a), a[:60]))
    open(path, 'w', newline='').write(s.replace(a, b, 1))
    print('edited', path)


rep('src/wpc/pinheck/prop.h',
    'typedef void (*prop_tx_fn)(void *ctx, uint64_t pic_cycle, int level);\n',
    'typedef void (*prop_tx_fn)(void *ctx, uint64_t pic_cycle, int level);\n'
    'typedef void (*prop_pins_fn)(void *ctx, uint64_t prop_cycle, uint32_t out, uint32_t dir);\n')
rep('src/wpc/pinheck/prop.h',
    '\tint tx_level;\n} pinheck_prop;\n',
    '\tint tx_level;\n\tprop_pins_fn pins;\n\tvoid *pins_ctx;\n} pinheck_prop;\n')
rep('src/wpc/pinheck/prop.h',
    'void prop_set_tx(pinheck_prop *p, prop_tx_fn fn, void *ctx);\n',
    'void prop_set_tx(pinheck_prop *p, prop_tx_fn fn, void *ctx);\n'
    'void prop_set_pins(pinheck_prop *p, prop_pins_fn fn, void *ctx);\n')
rep('src/wpc/pinheck/prop.c',
    '\t\tp->sd_do = p->sd(p->sd_ctx, cs, sclk, mosi) != 0;\n\t}\n}\n',
    '\t\tp->sd_do = p->sd(p->sd_ctx, cs, sclk, mosi) != 0;\n\t}\n\tif (p->pins) p->pins(p->pins_ctx, t, out, dir);\n}\n')
rep('src/wpc/pinheck/prop.c',
    '\tp->tx_level = 1;\n}\n\nvoid prop_reset(',
    '\tp->tx_level = 1;\n}\n\nvoid prop_set_pins(pinheck_prop *p, prop_pins_fn fn, void *ctx)\n{\n\tp->pins = fn;\n\tp->pins_ctx = ctx;\n}\n\nvoid prop_reset(')
rep('tests/pinheck/link/register_build.py',
    "     'DRVLIBS += $(PINOBJ)/pinheck/sd.o $(PINOBJ)/pinheck/vfat.o $(PINOBJ)/pinheck/zipsrc.o\\n'),\n]\n",
    "     'DRVLIBS += $(PINOBJ)/pinheck/sd.o $(PINOBJ)/pinheck/vfat.o $(PINOBJ)/pinheck/zipsrc.o\\n'),\n"
    "    (['src/wpc/pinheck/display.c', 'src/wpc/pinheck/display.h'],\n"
    "     'DRVLIBS += $(PINOBJ)/pinheck/display.o\\n'),\n]\n")
EOF
```

Expected:
```text
edited src/wpc/pinheck/prop.h
edited src/wpc/pinheck/prop.h
edited src/wpc/pinheck/prop.h
edited src/wpc/pinheck/prop.c
edited src/wpc/pinheck/prop.c
edited tests/pinheck/link/register_build.py
```

Run: `python3 tests/pinheck/link/register_build.py`
Expected:
```text
updated cmake/libpinmame/CMakeLists.txt
updated cmake/pinmame/CMakeLists_win-x64.txt
updated cmake/pinmame/CMakeLists_win-x86.txt
updated cmake/pinmame32/CMakeLists_win-x64.txt
updated cmake/pinmame32/CMakeLists_win-x86.txt
updated cmake/sdl3pinmame/CMakeLists.txt
updated cmake/vpinmame/CMakeLists_win-x64.txt
updated cmake/vpinmame/CMakeLists_win-x86.txt
updated cmake/xpinmame/CMakeLists_linux-x64.txt
updated cmake/xpinmame/CMakeLists_osx-x64.txt
updated src/pinmame.mak
updated vcproj/LibPinMAME_VC2015.vcxproj
updated vcproj/LibPinMAME_VC2015.vcxproj.filters
updated vcproj/PinMAME32_VC2012.vcxproj
updated vcproj/PinMAME32_VC2012.vcxproj.filters
updated vcproj/PinMAME_VC2012.vcxproj
updated vcproj/PinMAME_VC2012.vcxproj.filters
updated vcproj/VPinMAME_VC2012.vcxproj
updated vcproj/VPinMAME_VC2012.vcxproj.filters
```

- [ ] **Step 4: Add the display block to the driver**

```sh
python3 - <<'EOF'
import sys

BLOCK = r'''static display disp;
static uint8_t disp_shown[DISPLAY_FRAME], disp_cfg[DISPLAY_CFG_MAX];
static int disp_cfg_n, disp_opened;
static FILE *disp_log;
static UINT32 disp_rgb32[256];
static UINT16 disp_rgb15[256];

static void pinheck_disp_frame(void *ctx, const uint8_t *frame, uint64_t t)
{
	uint8_t stamp[20];
	uint64_t pic = pic32cpu_soc()->cpu.cycles;
	uint32_t at = 0xFFFFFFFFu, a;
	int k;
	(void)ctx;
	memcpy(disp_shown, frame, DISPLAY_FRAME);
	if (!disp_log) return;
	for (a = 0; a + DISPLAY_FRAME <= 0x8000; a++)
		if (prop.chip.hub[a] == frame[0] && !memcmp(prop.chip.hub + a, frame, DISPLAY_FRAME)) { at = a; break; }
	for (k = 0; k < 8; k++) stamp[k] = (uint8_t)(t >> (8 * k));
	for (k = 0; k < 8; k++) stamp[8 + k] = (uint8_t)(pic >> (8 * k));
	for (k = 0; k < 4; k++) stamp[16 + k] = (uint8_t)(at >> (8 * k));
	fwrite(stamp, 1, 20, disp_log);
	fwrite(frame, 1, DISPLAY_FRAME, disp_log);
}

static void pinheck_disp_config(void *ctx, const uint8_t *bytes, int n, uint64_t t)
{
	char msg[16 + 3 * DISPLAY_CFG_MAX];
	int k, len;
	(void)ctx; (void)t;
	if (n == disp_cfg_n && !memcmp(bytes, disp_cfg, (size_t)n)) return;
	memcpy(disp_cfg, bytes, (size_t)n);
	disp_cfg_n = n;
	len = sprintf(msg, "display: config");
	for (k = 0; k < n; k++) len += sprintf(msg + len, " %02x", bytes[k]);
	pinheck_prop_log(NULL, msg);
}

static void pinheck_disp_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx;
	display_pins(&disp, t, out, dir);
}

static void pinheck_disp_init(void)
{
	const char *path = getenv("PINHECK_FRAME_LOG");
	int v;
	for (v = 0; v < 256; v++) {
		int r = ((v >> 5) & 7) * 255 / 7, g = ((v >> 2) & 7) * 255 / 7, b = (v & 3) * 255 / 3;
		disp_rgb32[v] = MAKE_RGB(r, g, b);
		disp_rgb15[v] = (UINT16)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
	}
	if (disp_log) fclose(disp_log);
	disp_log = path ? fopen(path, disp_opened ? "ab" : "wb") : NULL;
	disp_opened = 1;
	prop_set_pins(&prop, pinheck_disp_pins, NULL);
}

static void pinheck_disp_reset(void)
{
	display_init(&disp, NULL, pinheck_disp_frame, pinheck_disp_config, pinheck_prop_log);
	memset(disp_shown, 0, sizeof(disp_shown));
	disp_cfg_n = 0;
}

static void pinheck_disp_stop(void)
{
	if (disp_log) fclose(disp_log);
	disp_log = NULL;
}

'''


def rep(path, a, b):
    s = open(path, newline='').read()
    if b in s:
        return
    if s.count(a) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(a), a[:60]))
    open(path, 'w', newline='').write(s.replace(a, b, 1))
    print('edited', path, repr(a.splitlines()[0][:40]))


P = 'src/wpc/pinheck.c'
rep(P, '#include "pinheck/zipsrc.h"\n', '#include "pinheck/zipsrc.h"\n#include "pinheck/display.h"\n')
rep(P, '''PINMAME_VIDEO_UPDATE(pinheck_video)
{
	(void)layout;
	fillbitmap(bitmap, 0, cliprect);
}
''', BLOCK + '''PINMAME_VIDEO_UPDATE(pinheck_video)
{
	int x, y;
	(void)layout; (void)cliprect;
	for (y = 0; y < DISPLAY_H && y < bitmap->height; y++)
		for (x = 0; x < DISPLAY_W && x < bitmap->width; x++) {
			const uint8_t v = disp_shown[y * DISPLAY_W + x];
			if (bitmap->depth == 32) ((UINT32 *)bitmap->line[y])[x] = disp_rgb32[v];
			else ((UINT16 *)bitmap->line[y])[x] = disp_rgb15[v];
		}
}
''')
rep(P, '\tpinheck_open_card();\n\tpic32cpu_set_board(&board);\n', '\tpinheck_open_card();\n\tpinheck_disp_init();\n\tpic32cpu_set_board(&board);\n')
rep(P, '\tboot_reset(&boot, 0);\n\tcat24m01_init(&u13, u13mem, 0);\n', '\tboot_reset(&boot, 0);\n\tpinheck_disp_reset();\n\tcat24m01_init(&u13, u13mem, 0);\n')
rep(P, '\tlocals.have_vol = locals.have_zip = 0;\n}\n', '\tlocals.have_vol = locals.have_zip = 0;\n\tpinheck_disp_stop();\n}\n')
EOF
```

Expected:
```text
edited src/wpc/pinheck.c '#include "pinheck/zipsrc.h"'
edited src/wpc/pinheck.c 'PINMAME_VIDEO_UPDATE(pinheck_video)'
edited src/wpc/pinheck.c '\tpinheck_open_card();'
edited src/wpc/pinheck.c '\tboot_reset(&boot, 0);'
edited src/wpc/pinheck.c '\tlocals.have_vol = locals.have_zip = 0;'
```

- [ ] **Step 5: Rebuild and run check 3**

Run:
```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt && cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null && timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/build.log 2>&1; tail -1 build/sdl3pinmame/build.log; grep -E "(warning|error)" build/sdl3pinmame/build.log | grep -E "pinheck|display" | head -3
export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip && SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3000 tests/pinheck/display/pinmame_display.sh; echo "exit=$?"
```
The root `CMakeLists.txt` is copied again because Step 3 added `display.c` to `cmake/sdl3pinmame/CMakeLists.txt`; building from Step 2's copy fails to link with `undefined reference to 'display_init'`.

Expected (about 10 minutes):
```text
[100%] Built target sdl3pinmame
frames: 394 frames from 1.68s to 20.02s, 21.4 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $5870 when latched
frames: 75 frames from 12.02s to 14.99s, 24.9 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $5870 when latched
frames: LT5.VID: frames 1..33 of 34 shown pixel-exact, contiguous and in order (log frames 1..33)
pinmame display: ok
exit=0
```

The first `frames:` pair is launch 1, the attract run (the in-game confirmation: the same protocol, 30 fps while clips play, 21.7 fps on the score screen). The second pair is launch 2 from 12 s, after `[V00LT5]`.

- [ ] **Step 6: Run the other suites**

Run:
```sh
export TOOLS=$PWD/tests/pinheck/p8x32a/build/tools P8X32A_ROM=/path/to/p8x32a.rom PINHECK_UPDATE_DIR=/path/to/update PINHECK_ZIP=/path/to/dominos.zip
SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame tests/pinheck/display/check.sh
env -u PINHECK_UPDATE_DIR PINHECK_SKIP_FIRMWARE=1 tests/pinheck/pic32mx/check.sh
env -u PINHECK_UPDATE_DIR PINHECK_SKIP_FIRMWARE=1 tests/pinheck/link/check.sh
```
Expected: `display: 0 failed` (with the `dispboot:` and `pinmame display: ok` lines above), `makefile link: ok` and `pic32mx: 0 failed`, `link: 0 failed`. The link suite runs without the firmware here: with `PINHECK_UPDATE_DIR` set it would also run Plan 5's 15–45-minute machine suite, and the PinMAME check above already runs the whole machine through the changed `prop.c`.

- [ ] **Step 7: Commit**

```bash
git add src/wpc/pinheck.c src/wpc/pinheck/prop.h src/wpc/pinheck/prop.c tests/pinheck/display tests/pinheck/link/register_build.py src/pinmame.mak cmake vcproj
git commit -m "pinheck: display in PinMAME (CORE_VIDEO 128x32 RGB332) and machine check 3"
```

### Task 4: Record the findings in the parent spec

**Files:**
- Modify: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§4.3, §4.5, §5.5, §7 check 3, §8 row 6, §9 item 2)

- [ ] **Step 1: Apply the edits**

```sh
python3 - <<'EOF'
import sys

P = 'docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md'


def rep(a, b):
    s = open(P).read()
    if b in s:
        return
    if s.count(a) != 1:
        sys.exit('%s: anchor found %d times: %r' % (P, s.count(a), a[:60]))
    open(P, 'w').write(s.replace(a, b, 1))
    print('edited', repr(a[:50]))


rep('- Video generator: `VCFG`/`VSCL`/`WAITVID` modelled functionally, emitting pin-group values per pixel clock.\n',
    '- Video generator: not modelled. `PRP_V008.BIN` never executes `WAITVID`; the display is a serial frame link (§5.5). `WAITVID` is reported once, not modelled.\n')
rep('- sinks for pin-output edges, video runs and counter-state changes\n',
    '- sinks for pin-output edges and counter-state changes\n')
rep('- **Display**: a `CORE_VIDEO` layout of 128×32 with a custom renderer converting RGB332 to host colour, fed by the display decoder.\n',
    '- **Display**: a `CORE_VIDEO` layout of 128×32 with a custom renderer converting RGB332 to host colour, fed by the display module model (`display.c`, §5.5).\n')
rep('The video generator emits runs of 8-bit pin-group values with their pixel-clock timing. The display decoder reconstructs 128×32 RGB332 frames from those runs according to the signalling the video cog uses (open item 2) and presents each completed frame to the renderer.\n',
    'The Propeller sends finished frames to the display module over a clocked serial link: P22 (DMD_1) clock, data valid on the rising edge; P21 (DMD_3) data, MSB first; P20 (DMD_5) latch pulse; P17 (DMD_11) held high during a config packet. The latch pulse carries one clock edge of its own, which is not data. A frame is 4096 bytes, 128 × 32 pixels, RGB332, the `.VID` format; the firmware sends one only when the picture changes (every 33 ms while a clip plays at 30 fps, every 46 ms on the score screen). The config packet is 14 bytes (`00 b1 01 54 00 00 00 ff 00 80 00 20 00 3e` on a default machine), sent once at start-up. `display.c` models the receiving module from the Propeller\'s pin edges: it latches a transfer on the rising edge of P20, delivers exactly 4096 bytes as a frame, and discards anything else with one log line. The renderer shows the last complete frame. The firmware keeps its framebuffer at hub `$5870`; every frame it sends equals that buffer when latched. Starting a clip with `video()`, the firmware shows the clip from its frame 1: frame 0 is loaded but not sent.\n')
rep('| 6 | Video generator + display decoder | machine check 3 |\n',
    '| 6 | Display module model and renderer | machine check 3 |\n')
rep('2. **Display signalling on P16–P22** (needed by milestone 6). Disassemble the video cog (PASM around `0x3300–0x3E00` of `PRP_V008.BIN`): pin-group mapping, pixel clock, sync scheme, and how RGB332 source pixels map onto the seven connector lines.\n',
    '2. **Display signalling on P16–P22** (resolved by milestone 6; checked by `tests/pinheck/display/`). Not the video generator: a clocked serial frame link, P22 clock, P21 data, P20 latch, P17 config (§5.5).\n')
rep("3. `[V00ABC]` for a chosen clip produces display frames equal, pixel for pixel, to that `.VID`'s frames.\n",
    "3. `[V00ABC]` for a chosen clip produces display frames equal, pixel for pixel, to that `.VID`'s frames, contiguous and in order from the first frame shown (frame 1, §5.5) through the last. Each equals the firmware framebuffer at hub `$5870` when latched.\n")
EOF
```

Expected:
```text
edited '- Video generator: `VCFG`/`VSCL`/`WAITVID` modelle'
edited '- sinks for pin-output edges, video runs and count'
edited '- **Display**: a `CORE_VIDEO` layout of 128×32 wit'
edited 'The video generator emits runs of 8-bit pin-group '
edited '| 6 | Video generator + display decoder | machine '
edited '2. **Display signalling on P16–P22** (needed by mi'
edited '3. `[V00ABC]` for a chosen clip produces display f'
```

- [ ] **Step 2: Check nothing stale is left**

Run: `grep -nE "video runs|display decoder|Disassemble the video cog" docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md; echo "exit=$?"`
Expected: `exit=1` (no matches).

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md
git commit -m "Spec: display is a serial frame link (open item 2 resolved), check 3 as measured"
```
