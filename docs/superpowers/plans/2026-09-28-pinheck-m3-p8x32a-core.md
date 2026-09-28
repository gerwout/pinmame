# Milestone 3: `p8x32a` Propeller core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone, cycle-exact P8X32A Propeller core with disassembler and a CAT24M01 I2C EEPROM model, proven against Parallax's own RTL, that boots the Domino's `PRP_V008.BIN` through the real mask-ROM booter until cog 0 starts the Spin interpreter.

**Architecture:** `src/cpu/p8x32a/` is pure C99 with no PinMAME dependency. It is an instruction-granular, event-driven model whose phase timings (S/D read, fetch, write, hub slot latch, wait-match) and quirks (special-register shadows, hub-op flags, cog load scrambling, jump-cancel, cog start/stop latency) are taken from Parallax's published Verilog, so the host sees exactly the pin changes the silicon would, at exactly the same cycles. The chip talks to its host through callbacks: external pin levels (`pins_in`/`pins_next`), pin drive changes (`pins_out`), cog starts, CLKSET and log lines. `src/wpc/pinheck/eeprom.c` is the edge-driven CAT24M01 used both on the Propeller's boot EEPROM here and on the PIC32's I2C bus in Plan 2. Verification is differential: Parallax's RTL under Verilator is the cycle-exact arbiter (full pin trace plus hub RAM), spinsim is a second semantic oracle, and the exit test boots the real game image through the real booter.

**Tech Stack:** C99 (gcc/clang, MSVC-compatible), Verilator 5 (the RTL testbench is C++), OpenSpin (assembler for test programs), spinsim, Python 3, POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§2.1, §4.3, §4.4 `eeprom.c`, §6, §7 core level, §8 milestone 3, §9 open item 4). Roadmap: `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`.

## Global Constraints

- `src/cpu/p8x32a/` and `src/wpc/pinheck/eeprom.*` include only `<stdint.h>`, `<string.h>`, `<stdio.h>` and their own headers; no PinMAME headers; headers carry `extern "C"` guards.
- Every C file compiles warning-free with `-std=c99 -Wall -Wextra -Werror -pedantic` and passes the C89 syntax gate `-std=c89 -pedantic-errors -Wno-long-long` (PinMAME's VS2012 project).
- CAT24M01 API, fixed with Plan 2: `typedef struct cat24m01 { ... } cat24m01; void cat24m01_init(cat24m01 *e, uint8_t *mem, int addr_pins); int cat24m01_update(cat24m01 *e, int scl, int sda);` with `mem` 131072 bytes, `addr_pins` the A2:A1 strap, `scl`/`sda` the master-driven levels (1 = released), return value the level the device drives on SDA.
- Time is Propeller cycles. Cycle 0 is the first clock after reset; `CNT` equals the cycle number since power-on; cog n's hub slot latches on cycles congruent to 3+2n mod 16 counted from the reset cycle; a register write at the end of an instruction's last cycle is visible to pins from the next cycle.
- Converting cycles to wall time is the host's job: the chip reports every `CLKSET` value. Spec §5.1's exact 13/10 PIC32 ratio holds only once `CLKSET $6F` (xtal1 + pll16x) is active; the booter runs on RCFAST until cycle 16139078.
- Not modelled in this milestone, each reported once through the log callback, owned by a later plan: `WAITVID` and the video generator output (Plan 6), counter pin outputs and pin-sensing counter modes (Plans 6/7), the `CLKSET` reset bit (Plan 5). Counter accumulation for the free-running modes (PLL, NCO, DUTY, logic-always) and live `PHSx` reads are modelled.
- The P8X32A mask ROM is Parallax's, GPL 3.0, and is never committed to this repository: tests take it from `P8X32A_ROM` and skip the boot test when it is absent. The chosen image is the silicon dump, 32768 bytes, crc32 `f99b3070`, sha1 `b7b4fdf4f096db7d18bda6355725cb42ae4a9378` (open item 4, Task 1).
- Oracles (Verilator + Parallax RTL, spinsim, OpenSpin) are external test tools built by `tests/pinheck/p8x32a/tools.sh` at pinned commits; nothing under `tests/` is linked into PinMAME.
- Code comments stay short.

## Review Focus

Inputs the spec implies and the firmware or its host will produce, most likely to bite first; each is pinned by the test named in its owning task.

- **A cog is restarted or stopped by another cog in the middle of an instruction** (hub access in flight, long `WAITCNT` pending): the in-flight hub access still completes, the register write is dropped, and the cog restarts on the exact cycle. `chip/restart.spin` restarts a busy worker at 16 consecutive phases, then stops it inside a long `WAITCNT` (Task 3).
- **An external pin changes while a cog sits in `WAITPEQ`/`WAITPNE`** (the PIC32 link in Plan 5): the wait must end on the exact cycle, found through `pins_next` without per-cycle polling. `chip/waitext.spin` with scheduled `-extat` edges (Task 3).
- **The booter's host-detect path when P31 (RX) idles high**, as it may on the real board: the booter times out and still boots from EEPROM. Boot case `80000000` (Task 6).
- **FRQ, CTR and PHS written while a counter runs**, including the PHS-as-destination shadow quirk: live PHS reads must stay cycle-exact. `chip/special.spin` (Task 3).
- **A program selects a counter mode this milestone does not model**: it must be reported, never silently wrong. `chip/ctrlog.spin` with `EXPECT-LOG` (Task 3).

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/p8x32a/p8x32a.h` | public API: chip state, host callbacks, run/reset/pins/dasm |
| `src/cpu/p8x32a/p8x32a.c` | cogs, ALU (ported from `cog_alu.v`), hub slots, sys ops, locks, waits, counters, cog load with ROM unscrambling |
| `src/cpu/p8x32a/p8x32adasm.c` | disassembler |
| `src/wpc/pinheck/eeprom.h`, `eeprom.c` | CAT24M01 edge-driven I2C model |
| `tests/pinheck/eeprom/eeprom_test.c` | CAT24M01 datasheet transactions |
| `tests/pinheck/p8x32a/tools.sh`, `spinsim-dump.patch`, `rtl/tb.cpp` | build the oracles at pinned commits; RTL testbench |
| `tests/pinheck/p8x32a/romimage.py` | extract and verify the mask ROM image (open item 4) |
| `tests/pinheck/p8x32a/run.c` | our runner, same command line and trace format as the RTL testbench |
| `tests/pinheck/p8x32a/mkrom.py`, `gen.py`, `cmpwin.py` | test ROM builder, random program generator, window comparator |
| `tests/pinheck/p8x32a/chip/*.spin`, `isa/*.spin` | directed tests (RTL only / both oracles) |
| `tests/pinheck/p8x32a/check.sh`, `README.md`, `.gitignore` | test driver and documentation |

No PinMAME build files change in this milestone (Plan 2 owns them; the Propeller joins the machine in Plan 5).

---
### Task 1: Mask ROM image (open item 4) and the first oracles

**Files:**
- Create: `tests/pinheck/p8x32a/tools.sh`, `tests/pinheck/p8x32a/spinsim-dump.patch`, `tests/pinheck/p8x32a/romimage.py`, `tests/pinheck/p8x32a/.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: `tests/pinheck/p8x32a/build/tools/{openspin/build/openspin,spinsim/build/spinsim}`; later tasks also get `build/tools/p1rtl/p1rtl` from the same script. `romimage.py spinsim/rom.h out.rom` writes the 32 KB mask ROM and refuses any image whose crc32/sha1 differ.

Open item 4, resolved (facts gathered before writing this plan):
- The P8X32A hub ROM (`$8000-$FFFF`) holds the font, log/antilog/sine tables, the booter (`$F800`) and the Spin interpreter (`$F004`). The booter and interpreter are stored **scrambled**: the hub unscrambles a word by a fixed bit permutation only while a cog is being *loaded* from an address at or above `$8000` (`hub.v`, `ramq = !rd ? mem_q : {mem_q[03], mem_q[07], ...}` with `rd = !run && a[15]`). Normal `RDxxxx` of ROM returns the raw scrambled bits.
- Two independent emulator projects ship the same 32 KB silicon image: spinsim's `rom.h` and Gear's `StartupROM.bin` are byte-identical (crc32 `f99b3070`). That is the image this project uses.
- Parallax's FPGA release (`hub_rom_low.hex`/`hub_rom_high.hex`, word-addressed Intel HEX) builds a different image (crc32 `72fac199`): the font and tables are identical, but 930 bytes in `$F002-$FFFF` differ. It is not used.
- Licence: all of it is Parallax's GPL 3.0 material, so the image is derived at test time and never committed.

- [ ] **Step 1: Install the build prerequisites**

Run: `sudo apt-get install -y git make g++ patch verilator python3`
Then: `verilator --version`
Expected: `Verilator 5.` followed by the version.

- [ ] **Step 2: Write the tool builder, the spinsim patch and the ignore file**

`spinsim-dump.patch` adds one thing to spinsim: when `SPINSIM_DUMP=addr,len,file` is set, it writes that hub window to `file` at exit (spinsim prints nothing machine-readable otherwise). The patch keeps spinsim's CRLF line endings.

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
verilator --cc --exe --build -j 8 -O3 -Wno-fatal --x-initial 0 --x-assign 0 --public-flat-rw -I"$R" \
	-CFLAGS "-O2 -I$D" "$R/dig.v" "$HERE/rtl/tb.cpp" "$D/eeprom.c" --Mdir "$TOOLS/p1rtl" -o p1rtl \
	> "$TOOLS/p1rtl.log" 2>&1 || { echo "p1rtl build failed, see $TOOLS/p1rtl.log"; exit 2; }
echo "tools ready in $TOOLS"
```

Make it executable: `chmod +x tests/pinheck/p8x32a/tools.sh`

`tests/pinheck/p8x32a/spinsim-dump.patch`:

```diff
--- spinsim.c.orig
+++ spinsim.c
@@ -931,6 +931,16 @@
 #ifndef __MINGW32__
     restore_console_io();
 #endif
+    {
+        char *d = getenv("SPINSIM_DUMP"), path[256];
+        unsigned a, n;
+        FILE *f;
+        if (d && sscanf(d, "%x,%x,%255s", &a, &n, path) == 3 && (f = fopen(path, "wb")))
+        {
+            fwrite(hubram + a, 1, n, f);
+            fclose(f);
+        }
+    }
     if (eeprom) EEPromClose();
     if (profile) PrintStats();
     return 0;
```

`tests/pinheck/p8x32a/.gitignore`:

```text
build/
```

- [ ] **Step 3: Build the tools**

Run: `tests/pinheck/p8x32a/tools.sh`
Expected: `tools ready in .../build/tools (p1rtl needs rtl/tb.cpp and eeprom.c)` (p1rtl follows in Task 2).

- [ ] **Step 4: Write the ROM extractor**

`tests/pinheck/p8x32a/romimage.py`:

```python
#!/usr/bin/env python3
import hashlib
import re
import sys
import zlib

CRC32 = 0xF99B3070
SHA1 = 'b7b4fdf4f096db7d18bda6355725cb42ae4a9378'


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: romimage.py spinsim/rom.h out.rom')
    src = open(sys.argv[1]).read()
    data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', src.split('romdata', 1)[1])[:32768])
    crc, sha = zlib.crc32(data) & 0xFFFFFFFF, hashlib.sha1(data).hexdigest()
    if len(data) != 32768 or crc != CRC32 or sha != SHA1:
        raise SystemExit('romimage: got %d bytes crc32 %08x sha1 %s, expected crc32 %08x sha1 %s' % (len(data), crc, sha, CRC32, SHA1))
    open(sys.argv[2], 'wb').write(data)
    print('p8x32a.rom: 32768 bytes, crc32 %08x, sha1 %s' % (crc, sha))


if __name__ == '__main__':
    main()
```

- [ ] **Step 5: Extract and verify the image**

Run: `cd tests/pinheck/p8x32a && python3 romimage.py build/tools/spinsim/rom.h build/p8x32a.rom; cd -`
Expected: `p8x32a.rom: 32768 bytes, crc32 f99b3070, sha1 b7b4fdf4f096db7d18bda6355725cb42ae4a9378`

Export it for the later tasks' boot tests: `export P8X32A_ROM=$PWD/tests/pinheck/p8x32a/build/p8x32a.rom`

- [ ] **Step 6: Prove spinsim and the dump patch work**

Run:
```sh
cd tests/pinheck/p8x32a && mkdir -p build/probe && cat > build/probe/t.spin <<'SPIN'
PUB main
  cognew(@entry, 0)
  repeat until long[$6FFC]
DAT
        org     0
entry   mov     t, #5
        add     t, #7
        wrlong  t, p
        wrlong  t, done
        cogid   t
        cogstop t
t       long    0
p       long    $6000
done    long    $6FFC
SPIN
build/tools/openspin/build/openspin -q build/probe/t.spin -o build/probe/t.binary && \
SPINSIM_DUMP=6000,4,build/probe/t.dump build/tools/spinsim/build/spinsim build/probe/t.binary && xxd build/probe/t.dump; cd -
```
Expected: `00000000: 0c00 0000                                ....`

- [ ] **Step 7: Commit**

```bash
git add tests/pinheck/p8x32a/tools.sh tests/pinheck/p8x32a/spinsim-dump.patch tests/pinheck/p8x32a/romimage.py tests/pinheck/p8x32a/.gitignore
git commit -m "p8x32a: mask ROM image (open item 4) and oracle tooling"
```

### Task 2: CAT24M01 model and the RTL oracle

**Files:**
- Create: `tests/pinheck/eeprom/eeprom_test.c`
- Create: `src/wpc/pinheck/eeprom.h`, `src/wpc/pinheck/eeprom.c`
- Create: `tests/pinheck/p8x32a/rtl/tb.cpp`

**Interfaces:**
- Consumes: `tools.sh` from Task 1 (it builds p1rtl once `rtl/tb.cpp` and `eeprom.c` exist).
- Produces: the contracted CAT24M01 API (see Global Constraints) for this plan's runner and for Plan 2; `build/tools/p1rtl/p1rtl -rom f [-ram f] [-eeprom f] [-ext hex] [-extat cycle hex]... [-cycles n] [-stop cog ptrhex] [-halt] [-dump f]` printing `P <cycle> <out> <dir>`, `K <cycle> <cfg>`, `S <cycle> <cog> <ptr>` and `E <cycle>` lines, each at the first cycle the change is visible.

CAT24M01 behaviour implemented (datasheet): 1010 A2 A1 a16 R/W control byte, NACK and ignore on a strap mismatch, two address bytes, 256-byte page writes that wrap inside the page and commit at STOP (a START without STOP discards them), sequential reads that roll over the full 17-bit space, an address counter that points past the last byte read (also after the master's NACK). Not modelled: the write-cycle time tWR (ACK polling succeeds immediately).

- [ ] **Step 1: Write the failing datasheet test**

`tests/pinheck/eeprom/eeprom_test.c`:

```c
#include "eeprom.h"
#include <stdio.h>
#include <string.h>

static uint8_t mem[0x20000];
static cat24m01 e;
static int scl = 1, sda = 1, dev = 1, fails;

#define CHECK(c) do { if (!(c)) { printf("EEPROM FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int bus(void) { return sda & dev; }
static void set(int c, int d) { scl = c; sda = d; dev = cat24m01_update(&e, scl, sda); }
static void start(void) { set(1, 1); set(1, 0); set(0, 0); }
static void stop(void) { set(0, 0); set(1, 0); set(1, 1); }

static int wbyte(int b)
{
	int i, ack;
	for (i = 7; i >= 0; i--) { set(0, (b >> i) & 1); set(1, (b >> i) & 1); set(0, (b >> i) & 1); }
	set(0, 1);
	set(1, 1);
	ack = !bus();
	set(0, 1);
	return ack;
}

static int rbyte(int ack)
{
	int i, b = 0;
	set(0, 1);
	for (i = 0; i < 8; i++) { set(1, 1); b = (b << 1) | bus(); set(0, 1); }
	set(0, !ack);
	set(1, !ack);
	set(0, !ack);
	set(0, 1);
	return b;
}

static int ctrl(int a16, int rd) { return 0xA0 | (0 << 2) | (a16 << 1) | rd; }

static void setaddr(uint32_t a)
{
	start();
	CHECK(wbyte(ctrl((int)(a >> 16), 0)));
	CHECK(wbyte((int)(a >> 8) & 0xFF));
	CHECK(wbyte((int)a & 0xFF));
}

static void write_bytes(uint32_t a, const uint8_t *d, int n)
{
	int i;
	setaddr(a);
	for (i = 0; i < n; i++) CHECK(wbyte(d[i]));
	stop();
}

static int random_read(uint32_t a)
{
	int v;
	setaddr(a);
	start();
	CHECK(wbyte(ctrl((int)(a >> 16), 1)));
	v = rbyte(0);
	stop();
	return v;
}

int main(void)
{
	static const uint8_t four[4] = { 0x11, 0x22, 0x33, 0x44 };
	uint8_t one = 0x5A;
	int i, v[3];

	cat24m01_init(&e, mem, 0);

	write_bytes(0x01234, &one, 1);
	CHECK(mem[0x01234] == 0x5A);
	CHECK(random_read(0x01234) == 0x5A);

	write_bytes(0x000FE, four, 4);
	CHECK(mem[0x000FE] == 0x11 && mem[0x000FF] == 0x22 && mem[0x00000] == 0x33 && mem[0x00001] == 0x44);
	CHECK(mem[0x00100] == 0x00);

	mem[0x100FF] = 0xA1; mem[0x10100] = 0xB2; mem[0x10101] = 0xC3;
	setaddr(0x100FF);
	start();
	CHECK(wbyte(ctrl(1, 1)));
	for (i = 0; i < 3; i++) v[i] = rbyte(i < 2);
	stop();
	CHECK(v[0] == 0xA1 && v[1] == 0xB2 && v[2] == 0xC3);

	start();
	CHECK(wbyte(ctrl(1, 1)));
	CHECK(rbyte(0) == mem[0x10102]);
	stop();

	mem[0x1FFFF] = 0x7E; mem[0x00000] = 0x33;
	setaddr(0x1FFFF);
	start();
	CHECK(wbyte(ctrl(1, 1)));
	v[0] = rbyte(1); v[1] = rbyte(0);
	stop();
	CHECK(v[0] == 0x7E && v[1] == 0x33);

	start();
	CHECK(!wbyte(0xA0 | (1 << 2)));
	stop();
	CHECK(bus() == 1);

	setaddr(0x00200);
	CHECK(wbyte(0x99));
	start();
	stop();
	CHECK(mem[0x00200] == 0x00);

	CHECK(dev == 1);
	printf("eeprom: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cc -std=c99 -Isrc/wpc/pinheck -o /dev/null tests/pinheck/eeprom/eeprom_test.c`
Expected: `fatal error: eeprom.h: No such file or directory`

- [ ] **Step 3: Write the model**

`src/wpc/pinheck/eeprom.h`:

```c
#ifndef PINHECK_EEPROM_H
#define PINHECK_EEPROM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cat24m01 {
	uint8_t *mem;
	int addr_pins;
	int scl, sda;
	int state, phase, bit, out, ack;
	uint8_t shift;
	uint32_t addr;
	uint8_t page[256];
	uint32_t page_base;
	int page_count;
	uint16_t page_mask[16];
} cat24m01;

void cat24m01_init(cat24m01 *e, uint8_t *mem, int addr_pins);
int cat24m01_update(cat24m01 *e, int scl, int sda);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/eeprom.c`:

```c
#include "eeprom.h"
#include <string.h>

enum { IDLE, CTRL, ADDR_HI, ADDR_LO, WRITE, READ, IGNORE };
enum { DATA, ACK_NEXT, ACK_OUT, MACK };

#define SIZE 0x20000u

void cat24m01_init(cat24m01 *e, uint8_t *mem, int addr_pins)
{
	memset(e, 0, sizeof(*e));
	e->mem = mem;
	e->addr_pins = addr_pins & 3;
	e->scl = e->sda = e->out = 1;
}

static void commit(cat24m01 *e)
{
	int i;
	for (i = 0; i < 256; i++)
		if (e->page_mask[i >> 4] & (1u << (i & 15)))
			e->mem[e->page_base | (uint32_t)i] = e->page[i];
	memset(e->page_mask, 0, sizeof(e->page_mask));
	e->page_count = 0;
}

static void put_bit(cat24m01 *e)
{
	e->out = (e->mem[e->addr] >> (7 - e->bit)) & 1;
	e->bit++;
}

static void byte_in(cat24m01 *e)
{
	uint8_t b = e->shift;
	e->ack = 1;
	switch (e->state) {
	case CTRL:
		if ((b >> 4) != 0xA || ((b >> 2) & 3) != e->addr_pins) { e->ack = 0; e->state = IGNORE; break; }
		e->addr = (e->addr & 0xFFFFu) | ((uint32_t)(b >> 1) & 1u) << 16;
		e->state = (b & 1) ? READ : ADDR_HI;
		break;
	case ADDR_HI:
		e->addr = (e->addr & 0x100FFu) | (uint32_t)b << 8;
		e->state = ADDR_LO;
		break;
	case ADDR_LO:
		e->addr = (e->addr & 0x1FF00u) | b;
		e->page_base = e->addr & 0x1FF00u;
		e->state = WRITE;
		break;
	case WRITE:
		e->page[e->addr & 0xFF] = b;
		e->page_mask[(e->addr & 0xFF) >> 4] |= (uint16_t)(1u << (e->addr & 15));
		e->page_count++;
		e->addr = e->page_base | ((e->addr + 1) & 0xFF);
		break;
	}
}

int cat24m01_update(cat24m01 *e, int scl, int sda)
{
	scl = scl != 0;
	sda = sda != 0;
	if (scl && e->scl && sda != e->sda) {
		if (!sda) {
			memset(e->page_mask, 0, sizeof(e->page_mask));
			e->page_count = 0;
			e->state = CTRL;
		} else {
			if (e->state == WRITE && e->page_count) commit(e);
			e->state = IDLE;
		}
		e->phase = DATA;
		e->bit = 0;
		e->shift = 0;
		e->out = 1;
	} else if (scl && !e->scl) {
		if (e->state == READ) {
			if (e->phase == MACK) {
				e->addr = (e->addr + 1) & (SIZE - 1);
				if (sda) e->state = IGNORE;
			}
		} else if (e->state != IDLE && e->state != IGNORE && e->phase == DATA) {
			e->shift = (uint8_t)((e->shift << 1) | sda);
			if (++e->bit == 8) {
				byte_in(e);
				e->phase = ACK_NEXT;
			}
		}
	} else if (!scl && e->scl) {
		switch (e->phase) {
		case ACK_NEXT:
			e->phase = ACK_OUT;
			e->out = e->ack ? 0 : 1;
			break;
		case ACK_OUT:
			e->phase = DATA;
			e->bit = 0;
			e->shift = 0;
			e->out = 1;
			if (e->state == READ) put_bit(e);
			break;
		case DATA:
			if (e->state != READ) break;
			if (e->bit < 8) put_bit(e);
			else { e->out = 1; e->phase = MACK; }
			break;
		case MACK:
			if (e->state == READ) { e->phase = DATA; e->bit = 0; put_bit(e); }
			else e->out = 1;
			break;
		}
	}
	e->scl = scl;
	e->sda = sda;
	return e->out;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -Isrc/wpc/pinheck -o tests/pinheck/p8x32a/build/eeprom_test tests/pinheck/eeprom/eeprom_test.c src/wpc/pinheck/eeprom.c && tests/pinheck/p8x32a/build/eeprom_test && cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only src/wpc/pinheck/eeprom.c && echo c89-ok`
Expected: `eeprom: ok` then `c89-ok`

- [ ] **Step 5: Write the RTL testbench**

It preloads hub RAM and ROM into the verilated `dig` module, drives `pin_in = (dir & out) | (~dir & ext)` every cycle, attaches the CAT24M01 to P28 (SCL) / P29 (SDA) with pull-ups when `-eeprom` is given, and prints the trace.

`tests/pinheck/p8x32a/rtl/tb.cpp`:

```cpp
#include "Vdig.h"
#include "Vdig___024root.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "eeprom.h"

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
		else if (!strcmp(argv[i], "-extat") && i + 2 < argc && nat < 64) { at_t[nat] = strtoull(argv[i + 1], NULL, 0); at_v[nat++] = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else if (!strcmp(argv[i], "-stop") && i + 2 < argc) { stop_cog = atoi(argv[++i]); stop_ptr = (uint32_t)strtoul(argv[++i], NULL, 16); }
		else { fprintf(stderr, "usage: p1rtl -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-dump f]\n"); return 2; }
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

- [ ] **Step 6: Build p1rtl**

Run: `tests/pinheck/p8x32a/tools.sh`
Expected: `tools ready in .../build/tools`

- [ ] **Step 7: Boot the real firmware on the RTL**

Run:
```sh
cd tests/pinheck/p8x32a && python3 -c "import sys; d=open(sys.argv[1],'rb').read(); open('build/probe/eeprom.bin','wb').write(d+b'\xff'*(131072-len(d)))" /path/to/PRP_V008.BIN && \
build/tools/p1rtl/p1rtl -rom "$P8X32A_ROM" -eeprom build/probe/eeprom.bin -cycles 60000000 -stop 0 7c01 -dump build/probe/hub.bin | grep -v '^P'; \
cmp <(head -c 32160 build/probe/hub.bin) <(head -c 32160 /path/to/PRP_V008.BIN) && echo hub-ok; cd -
```
(`/path/to/PRP_V008.BIN` is the Propeller image from the Domino's update package; the run takes about two minutes.)
Expected:
```text
K 0 00
K 15739046 68
K 16139078 6f
S 16139093 0 0007c01
E 16139094
hub-ok
```
That is the real booter reading the 32 KB image over I2C from the model, switching to xtal+PLL, and restarting cog 0 with the Spin interpreter at `$F004`, PAR `$0004`.

- [ ] **Step 8: Commit**

```bash
git add src/wpc/pinheck/eeprom.h src/wpc/pinheck/eeprom.c tests/pinheck/eeprom/eeprom_test.c tests/pinheck/p8x32a/rtl/tb.cpp
git commit -m "pinheck: CAT24M01 I2C EEPROM model; p8x32a RTL oracle testbench"
```

### Task 3: The core, the runner and the directed chip tests

**Files:**
- Create: `tests/pinheck/p8x32a/run.c`, `tests/pinheck/p8x32a/mkrom.py`, `tests/pinheck/p8x32a/check.sh`
- Create: `tests/pinheck/p8x32a/chip/pins.spin`, `cogs.spin`, `waits.spin`, `special.spin`, `restart.spin`, `waitext.spin`, `ctrlog.spin`
- Create: `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`

**Interfaces:**
- Consumes: the CAT24M01 API; p1rtl.
- Produces (used by Plan 5): `p8x32a_init(p8x32a *p, const p8x32a_bus *bus)`, `p8x32a_reset(p8x32a *p, uint64_t t)` (cycle `t` is the first clock after reset), `p8x32a_run_until(p8x32a *p, uint64_t t)` (processes every event up to and including cycle `t`, then flushes pin changes up to `t`; stops early when a callback sets `p->stop`), `uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir)`, `p->hub[65536]` (RAM `$0000-$7FFF`, raw ROM `$8000-$FFFF`). Callbacks in `p8x32a_bus`: `uint32_t pins_in(ctx, t)` external pin levels at cycle `t`; `uint64_t pins_next(ctx, t)` next cycle after `t` at which they may change (`P8X32A_NEVER` if none); `void pins_out(ctx, t, out, dir)` chip drive state from cycle `t`, delivered in time order; `void cog_start(ctx, t, cog, ptr)`; `void clkset(ctx, t, cfg)`; `void log(ctx, msg)`.
- The runner `build/p8run` accepts the same options as p1rtl and prints the same trace.

How the core gets cycle-exact results (from `cog.v`, `cog_alu.v`, `hub.v`, `dig.v`):
- An instruction's states are read-S (m0), read-D (m1), fetch-next (m2), write (m3), plus wait states. Special-register *sources* read live at m2 (PAR, CNT, INA, PHSA, PHSB); everything else, and every *destination*, is the RAM shadow. The next instruction is fetched at m2, before this one writes, so an instruction cannot modify its immediate successor.
- Hub operations wait for the cog's slot: first latch cycle `c >= m0+3` with `c = 3+2n (mod 16)`; memory is accessed at `c+2`, the result written at `c+4` (8-23 cycles). Hub-op C is the global sys-C latch; a write's Z reflects the *old* hub value.
- `WAITCNT`/`WAITPEQ`/`WAITPNE` match on the first cycle at or after m2+1 and write two cycles later. `DJNZ`/`TJZ`/`TJNZ` fetch the target speculatively and cancel it when not taken (8 cycles); an instruction fetched from `$1FF` is always cancelled.
- A cog load is 512 synthetic `RDLONG`s through its hub slot (the last 16 write zero into `$1F0-$1FF`, clearing OUTA/DIRA/CTR/FRQ/PHS/VCFG/VSCL). ROM words are unscrambled only during loads. After `COGINIT` at hub cycle h the target is disabled at h+1 and starts loading at h+4; after `COGSTOP` it is disabled at h+3. A disabled cog drops any register write at or after that cycle, but an access already latched by the hub still completes.
- The scheduler processes one cog event at a time in cycle order (hub events first on ties), keeps one pending OUTA/DIRA change per cog with its effective cycle, and delivers chip-level pin changes in cycle order.

- [ ] **Step 1: Write the runner, the ROM builder and the test driver**

`mkrom.py` places the PASM between the markers `$C0DE5EED`/`$C0DEE0D0` scrambled at ROM `$F800`, where cog 0 loads from at reset, so these tests need no mask ROM.

`tests/pinheck/p8x32a/run.c`:

```c
#include "p8x32a.h"
#include "eeprom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static p8x32a chip;
static cat24m01 ee;
static uint8_t eemem[0x20000];
static int have_ee;
static uint32_t ext, eebits;
static uint64_t at_t[64];
static uint32_t at_v[64];
static int nat;
static int stop_cog = -1;
static uint32_t stop_ptr;
static uint64_t stop_at = P8X32A_NEVER;
static struct ev { uint64_t t; size_t seq; char line[48]; } *evs;
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
	return have_ee ? (v & ~0x30000000u) | eebits : v;
}

static uint64_t pins_next(void *ctx, uint64_t t)
{
	int k;
	(void)ctx;
	for (k = 0; k < nat; k++)
		if (at_t[k] > t) return at_t[k];
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
	char b[48];
	sprintf(b, "P %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
	emit(t, b);
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
	unsigned long long limit = 1000000, t, end;
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
		else if (!strcmp(argv[i], "-extat") && i + 2 < argc && nat < 64) { at_t[nat] = strtoull(argv[i + 1], NULL, 0); at_v[nat++] = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else { fprintf(stderr, "usage: p8run -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-halt] [-extat cycle hex]... [-dump f]\n"); return 2; }
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
	for (t = 0; t < limit; t += 4096) {
		unsigned long long to = t + 4095 < limit - 1 ? t + 4095 : limit - 1;
		uint64_t h;
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

`tests/pinheck/p8x32a/mkrom.py`:

```python
#!/usr/bin/env python3
import struct
import sys

START, END, WORKER, PATCH = 0xC0DE5EED, 0xC0DEE0D0, 0xC0DE0B0B, 0xC0DEADD1
UNSCR = [10, 26, 24, 29, 27, 13, 22, 28, 2, 25, 18, 9, 5, 16, 31, 23,
         1, 30, 14, 0, 11, 8, 15, 20, 17, 4, 19, 6, 12, 21, 7, 3]


def scramble(w):
    r = 0
    for k in range(32):
        if w >> k & 1:
            r |= 1 << UNSCR[k]
    return r


def pasm(binary):
    words = [struct.unpack_from('<I', binary, i)[0] for i in range(0, len(binary) - 3, 4)]
    a = words.index(START) + 1
    b = words.index(END, a)
    if b - a > 496:
        raise SystemExit('mkrom: PASM block longer than 496 longs')
    code = words[a:b]
    if WORKER in words:
        addr = (words.index(WORKER) + 1) * 4
        code = [addr if w == PATCH else w for w in code]
    return code


def launcher(binary):
    words = [struct.unpack_from('<I', binary, i)[0] for i in range(0, len(binary) - 3, 4)]
    entry = (words.index(START) + 1) * 4
    return [(3 << 26) | (1 << 22) | (15 << 18) | (3 << 9) | 2,
            (3 << 26) | (1 << 23) | (1 << 22) | (15 << 18) | (4 << 9) | 1,
            (3 << 26) | (1 << 22) | (15 << 18) | (4 << 9) | 3,
            ((entry >> 2) << 4) | 1,
            0]


def main():
    args = [a for a in sys.argv[1:] if a != '--launch']
    if len(args) != 3:
        raise SystemExit('usage: mkrom.py [--launch] prog.binary out.rom out.ram')
    binary = open(args[0], 'rb').read()
    code = launcher(binary) if '--launch' in sys.argv else pasm(binary)
    rom = bytearray(32768)
    for k, w in enumerate(code):
        struct.pack_into('<I', rom, 0x7800 + 4 * k, scramble(w))
    open(args[1], 'wb').write(rom)
    open(args[2], 'wb').write(binary[:32768] + bytes(32768 - min(len(binary), 32768)))


if __name__ == '__main__':
    main()
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

for c in $CORE/*.c $DEV/eeprom.c; do
	[ -e "$c" ] || continue
	cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only -I$CORE -I$DEV "$c" || { echo "C89 FAIL $c"; fail=$((fail + 1)); }
done
mkdir -p $B/rtl $B/spin $B/boot
if [ -f ../eeprom/eeprom_test.c ]; then
	$CC -I$DEV -o $B/eeprom_test ../eeprom/eeprom_test.c $DEV/eeprom.c || exit 2
	./$B/eeprom_test || fail=$((fail + 1))
fi
[ -f $CORE/p8x32a.c ] || { echo "p8x32a: core not present yet"; exit 2; }
$CC -I$CORE -I$DEV -o $B/p8run run.c $CORE/p8x32a.c $DEV/eeprom.c || exit 2
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
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args -dump "$o.ourhub" > "$o.our" 2> "$o.log"
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

if [ -n "$P8X32A_ROM" ] && [ -n "$DOMINOS_PRP" ]; then
	crc=$(python3 -c "import zlib,sys; print('%08x' % (zlib.crc32(open(sys.argv[1],'rb').read()) & 0xffffffff))" "$P8X32A_ROM")
	[ "$crc" = f99b3070 ] || echo "note: P8X32A_ROM crc32 $crc, expected f99b3070"
	boot_case 0
	boot_case 80000000
else
	echo "boot: skipped (set P8X32A_ROM and DOMINOS_PRP to run it)"
fi
echo "p8x32a: $pass passed, $fail failed"
[ $fail -eq 0 ]
```

Make them executable: `chmod +x tests/pinheck/p8x32a/mkrom.py tests/pinheck/p8x32a/check.sh`

- [ ] **Step 2: Write the directed chip tests**

Each is compared cycle-for-cycle with the RTL. `restart`, `waitext`, `special` and `ctrlog` pin Review Focus items 1, 2, 4 and 5.

`tests/pinheck/p8x32a/chip/pins.spin`:

```text
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     dira, #$FF
        mov     outa, #1
        mov     outa, #2
        rdlong  t, #0
        mov     outa, #3
        mov     t, cnt
        add     t, #40
        waitcnt t, #0
        mov     outa, #4
        wrlong  t, hubres
        cogid   t
        cogstop t
t       long    0
hubres  long    $6000
        long    $C0DEE0D0
```

`tests/pinheck/p8x32a/chip/cogs.spin`:

```text
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        mov     par1, #$60
        shl     par1, #8
        mov     n, #7
:start  mov     y, par1
        shl     y, #16
        or      y, x
        coginit y wc, wr
        muxc    f, #1
        wrlong  y, ptr
        add     ptr, #4
        add     par1, #16
        djnz    n, #:start
        mov     y, x
        coginit y wc, wr
        wrlong  y, ptr
        add     ptr, #4
        muxc    f, #2
        mov     n, #9
:locks  locknew y wc
        wrlong  y, ptr
        add     ptr, #4
        muxc    f, #4
        djnz    n, #:locks
        mov     y, #3
        lockset y wc
        muxc    f, #8
        lockset y wc
        muxc    f, #16
        lockclr y wc
        muxc    f, #32
        lockret y
        locknew y wc
        wrlong  y, ptr
        add     ptr, #4
        mov     y, #5
        cogstop y
        mov     y, #6
        cogstop y
        mov     t, cnt
        add     t, #200
        waitcnt t, #0
        cogid   y
        wrlong  y, ptr
        add     ptr, #4
        wrlong  f, ptr
        cogstop y
x       long    0
y       long    0
t       long    0
n       long    0
f       long    0
par1    long    0
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  cogid   id
        mov     b, #1
        shl     b, id
        or      dira, b
        or      outa, b
        mov     a, par
        add     a, #4
        wrlong  id, par
        wrlong  cnt, a
        mov     w, cnt
        add     w, #400
        waitcnt w, #0
        andn    outa, b
        cogstop id
id      long    0
b       long    0
a       long    0
w       long    0
```

`tests/pinheck/p8x32a/chip/waits.spin`:

```text
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        coginit x wr
        mov     dira, #%100000
        mov     t, cnt
        add     t, delay
        waitcnt t, #37
        or      outa, #%100000
        waitcnt t, #100
        andn    outa, #%100000
        waitcnt t, #0
        mov     t, cnt
        waitpeq pin6, pin6
        mov     u, cnt
        sub     u, t
        wrlong  u, ptr
        add     ptr, #4
        mov     t, cnt
        add     t, #9
        waitcnt t, #0
        mov     u, cnt
        sub     u, t
        wrlong  u, ptr
        cogstop x
        cogid   t
        cogstop t
x       long    0
t       long    0
u       long    0
pin6    long    %1000000
delay   long    10000
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, #%1000000
        waitpeq p5, p5
        mov     s, cnt
        waitpne p5, p5
        mov     e, cnt
        or      outa, #%1000000
        sub     e, s
        mov     a, #$60
        shl     a, #8
        add     a, #16
        wrlong  e, a
:spin   jmp     #:spin
p5      long    %100000
s       long    0
e       long    0
a       long    0
```

`tests/pinheck/p8x32a/chip/special.spin`:

```text
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     t, par
        call    #put
        mov     ina, #123
        cmp     ina, #123 wz
        muxz    f, #1
        add     cnt, #5
        mov     t, #0
        cmp     cnt, #5 wz
        muxz    f, #2
        mov     ctra, nco
        mov     frqa, #7
        mov     t, phsa
        nop
        nop
        mov     u, phsa
        sub     u, t
        mov     t, u
        call    #put
        mov     t, phsa
        call    #put
        add     phsa, #1
        mov     t, phsa
        call    #put
        mov     frqa, #3
        mov     t, phsa
        nop
        mov     u, phsa
        sub     u, t
        mov     t, u
        call    #put
        mov     ctra, duty
        mov     t, phsa
        mov     u, phsa
        sub     u, t
        mov     t, u
        call    #put
        mov     ctra, #0
        mov     t, phsa
        nop
        mov     u, phsa
        sub     u, t
        mov     t, u
        call    #put
        movs    :next, #99
:next   mov     t, #1
        call    #put
        movs    :nx2, #77
        nop
:nx2    mov     t, #1
        call    #put
        rdlong  t, romw
        call    #put
        rdbyte  t, romb
        call    #put
        rdword  t, romw2
        call    #put
        mov     dira, #$F
        mov     outa, #3
        max     outa, #1 wc
        muxc    f, #4
        mov     t, outa
        call    #put
        mov     outa, #3
        max     outa, #5 wc
        muxc    f, #8
        mov     t, outa
        call    #put
        mov     t, #0
        wrlong  t, ptr
        add     ptr, #4
        wrlong  f, ptr
        cogid   t
        cogstop t
put     wrlong  t, ptr
        add     ptr, #4
put_ret ret
t       long    0
u       long    0
f       long    0
nco     long    %00100 << 26
duty    long    %00110 << 26
romw    long    $F004
romb    long    $8021
romw2   long    $E002
ptr     long    $6000
        long    $C0DEE0D0
```

`tests/pinheck/p8x32a/chip/restart.spin`:

```text
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        coginit x wr
        mov     n, #16
        mov     d, dstart
:loop   mov     t, cnt
        add     t, d
        waitcnt t, #0
        mov     y, wk
        shl     y, #2
        mov     z, n
        shl     z, #2
        add     z, pbase
        shl     z, #16
        or      y, z
        or      y, x
        coginit y
        add     d, #1
        djnz    n, #:loop
        mov     y, wk
        shl     y, #2
        mov     z, sleepv
        shl     z, #16
        or      y, z
        or      y, x
        coginit y
        mov     t, cnt
        add     t, long1
        waitcnt t, #0
        cogstop x
        mov     t, cnt
        add     t, #200
        waitcnt t, #0
        cogid   t
        cogstop t
x       long    0
y       long    0
z       long    0
t       long    0
n       long    0
d       long    0
pbase   long    $6000
sleepv  long    $6400
long1   long    12000
dstart  long    9000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, #%10
        cmp     par, sleepv2 wz
if_z    jmp     #:sleep
:l      mov     v, cnt
        wrlong  v, par
        xor     outa, #%10
        jmp     #:l
:sleep  mov     w, cnt
        add     w, big
        waitcnt w, #0
        wrlong  w, par
sleepv2 long    $6400
big     long    $10000000
w       long    0
v       long    0
```

`tests/pinheck/p8x32a/chip/waitext.spin`:

```text
' ARGS: -extat 20000 80 -extat 25000 0 -extat 30001 80
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   waitpeq p7, p7
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
        waitpne p7, p7
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
        waitpeq p7, p7
        mov     t, cnt
        wrlong  t, ptr
        cogid   t
        cogstop t
p7      long    %10000000
t       long    0
ptr     long    $6000
        long    $C0DEE0D0
```

`tests/pinheck/p8x32a/chip/ctrlog.spin`:

```text
' EXPECT-LOG: p8x32a: pin-sensing counter mode not modelled
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     ctra, pos
        mov     frqa, #1
        mov     t, cnt
        add     t, #100
        waitcnt t, #0
        cogid   t
        cogstop t
pos     long    %01010 << 26 | 5
t       long    0
        long    $C0DEE0D0
```

- [ ] **Step 3: Run to verify it fails**

Run: `tests/pinheck/p8x32a/check.sh`
Expected: `eeprom: ok`, then `p8x32a: core not present yet`, exit 2.

- [ ] **Step 4: Write the core**

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
	uint64_t (*pins_next)(void *ctx, uint64_t t);
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
} p8x32a_cog;

typedef struct p8x32a {
	p8x32a_bus bus;
	uint8_t hub[65536];
	p8x32a_cog cog[8];
	uint8_t cog_e, lock_e, lock_state, cfg, sys_q, sys_c;
	uint64_t now, slot_base, cnt_base;
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

uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir)
{
	uint32_t o = 0, d = 0;
	int n;
	for (n = 0; n < 8; n++) {
		uint32_t dd = regval(&p->cog[n].dira, t);
		d |= dd;
		o |= regval(&p->cog[n].outa, t) & dd;
	}
	*dir = d;
	return o;
}

static void flush(p8x32a *p, uint64_t t)
{
	for (;;) {
		int k, best = -1;
		uint32_t out, dir;
		for (k = 0; k < p->npend; k++)
			if (p->pend[k] <= t && (best < 0 || p->pend[k] < p->pend[best])) best = k;
		if (best < 0) return;
		out = p8x32a_pins(p, p->pend[best], &dir);
		if ((out != p->last_out || dir != p->last_dir) && p->bus.pins_out) p->bus.pins_out(p->bus.ctx, p->pend[best], out, dir);
		p->last_out = out;
		p->last_dir = dir;
		p->pend[best] = p->pend[--p->npend];
	}
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
	if (m >= 2 && m <= 15) log_once(p, LOG_CTR_OUT, "p8x32a: counter pin outputs not modelled");
}

static void special_write(p8x32a *p, int n, unsigned a, uint32_t v, uint64_t m3)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t e = m3 + 1;
	int k = a & 1;
	switch (a) {
	case 0x1F4: regset(p, &c->outa, v, e); break;
	case 0x1F6: if (e < c->disable_at) regset(p, &c->dira, v, e); break;
	case 0x1F8: case 0x1F9: ctr_rebase(c, k, e); c->ctr[k] = v; ctr_check(p, v); break;
	case 0x1FA: case 0x1FB: ctr_rebase(c, k, e); c->frq[k] = v; break;
	case 0x1FC: case 0x1FD: c->phs[k] = v; c->phs_t[k] = e; break;
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
	for (k = 0; k < 8; k++)
		if (k != n && p->cog[k].ev != EV_NONE && p->cog[k].ev_t + 1 < nt) nt = p->cog[k].ev_t + 1;
	if (p->bus.pins_next) {
		uint64_t e = p->bus.pins_next(p->bus.ctx, t);
		if (e < nt) nt = e;
	}
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

- [ ] **Step 5: Run to verify it passes**

Run: `tests/pinheck/p8x32a/check.sh`
Expected: `eeprom: ok`, `boot: skipped (set P8X32A_ROM and DOMINOS_PRP to run it)` unless both are set, and `p8x32a: 7 passed, 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c tests/pinheck/p8x32a/run.c tests/pinheck/p8x32a/mkrom.py tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/chip
git commit -m "p8x32a: cycle-exact Propeller core with RTL-compared chip tests"
```

### Task 4: Random and edge-value differential suites

**Files:**
- Create: `tests/pinheck/p8x32a/gen.py`, `tests/pinheck/p8x32a/cmpwin.py`, `tests/pinheck/p8x32a/isa/alu_edges.spin`

**Interfaces:**
- Consumes: `check.sh` (runs `gen.py` and `isa/*.spin` when present), `mkrom.py --launch`.
- Produces: `python3 gen.py --out DIR [--count N] [--first SEED] [--length L] [--hubflags]` writing `rNNNNN.spin`; a seed always produces the same program. Programs: 12 random registers, conditional ALU operations of every kind with random WZ/WC/NR/WR, hub reads/writes of all sizes and alignments inside `$6200-$62FF`, forward `DJNZ`/`TJZ`/`TJNZ`/`JMP` blocks; results written to `$6000`.

Two corpora, for a stated reason: the RTL corpus (`--hubflags`) also randomises flags on hub operations, where the silicon returns the stale global sys-C and the *old* hub value for Z on writes; spinsim does not model either, so its corpus leaves hub-op flags off. Every program in both corpora is also compared for timing and the full 32 KB hub against the RTL (spinsim is not cycle-accurate).

- [ ] **Step 1: Write the generator, the comparator and the edge test**

`tests/pinheck/p8x32a/gen.py`:

```python
#!/usr/bin/env python3
import argparse
import os
import random

CONDS = ['if_never', 'if_nc_and_nz', 'if_nc_and_z', 'if_nc', 'if_c_and_nz', 'if_nz', 'if_c_ne_z',
         'if_nc_or_nz', 'if_c_and_z', 'if_c_eq_z', 'if_z', 'if_nc_or_z', 'if_c', 'if_c_or_nz', 'if_c_or_z', '']
WRITES = ['ror', 'rol', 'shr', 'shl', 'rcr', 'rcl', 'sar', 'rev', 'mins', 'maxs', 'min', 'max', 'movs', 'movd',
          'movi', 'and', 'andn', 'or', 'xor', 'muxc', 'muxnc', 'muxz', 'muxnz', 'add', 'sub', 'addabs', 'subabs',
          'sumc', 'sumnc', 'sumz', 'sumnz', 'mov', 'neg', 'abs', 'absneg', 'negc', 'negnc', 'negz', 'negnz',
          'addx', 'subx', 'adds', 'subs', 'addsx', 'subsx', 'cmpsub']
TESTS = ['test', 'testn', 'cmp', 'cmpx', 'cmps', 'cmpsx']
REGS = ['r%d' % k for k in range(12)]
HUB = ['h%d' % k for k in range(4)]


class Gen:
    def __init__(self, rnd, hubflags):
        self.r = rnd
        self.hubflags = hubflags
        self.out = []
        self.label = 0

    def emit(self, s):
        self.out.append('        ' + s)

    def cond(self):
        return self.r.choice(CONDS) if self.r.random() < 0.3 else ''

    def src(self):
        return '#%d' % self.r.randrange(512) if self.r.random() < 0.4 else self.r.choice(REGS)

    def flags(self, writes):
        f = []
        if self.r.random() < 0.5:
            f.append('wz')
        if self.r.random() < 0.5:
            f.append('wc')
        if writes and self.r.random() < 0.15:
            f.append('nr')
        if not writes and self.r.random() < 0.15:
            f.append('wr')
        return ' ' + ', '.join(f) if f else ''

    def alu(self):
        if self.r.random() < 0.8:
            op, w = self.r.choice(WRITES), True
        else:
            op, w = self.r.choice(TESTS), False
        return '%-12s %-7s %s, %s%s' % (self.cond(), op, self.r.choice(REGS), self.src(), self.flags(w))

    def hub(self):
        op = self.r.choice(['rdlong', 'rdword', 'rdbyte', 'wrlong', 'wrword', 'wrbyte'])
        fl = self.flags(False).replace(', wr', '').replace(' wr', '') if self.hubflags else ''
        fl = '' if fl.strip() in ('', ',') else fl
        self.emit('%-12s %-7s %s, %s%s' % (self.cond(), op, self.r.choice(REGS), self.r.choice(HUB), fl))
        if self.r.random() < 0.3:
            h = self.r.choice(HUB)
            self.emit('add     %s, #%d' % (h, self.r.randrange(1, 4)))
            self.emit('and     %s, hmask' % h)
            self.emit('or      %s, hbase' % h)

    def branch(self):
        lab = 'L%d' % self.label
        self.label += 1
        k = self.r.randrange(4)
        if k == 0:
            self.emit('%-12s djnz    %s, #%s' % (self.cond(), self.r.choice(REGS), lab))
        elif k == 1:
            self.emit('%-12s tjz     %s, #%s' % (self.cond(), self.r.choice(REGS), lab))
        elif k == 2:
            self.emit('%-12s tjnz    %s, #%s' % (self.cond(), self.r.choice(REGS), lab))
        else:
            self.emit('%-12s jmp     #%s' % (self.cond(), lab))
        for _ in range(self.r.randrange(4)):
            self.emit(self.alu())
        self.out.append(lab)

    def program(self, n):
        r = self.r
        self.out += ['PUB main', '  cognew(@entry, 0)', '  repeat until long[$6FFC]', 'DAT',
                     '        long    $C0DE5EED', '        org     0']
        self.out.append('entry')
        self.emit('test    %s, #%d wz, wc' % (r.choice(REGS), r.randrange(512)))
        for _ in range(n):
            k = r.randrange(10)
            if k < 7:
                self.emit(self.alu())
            elif k < 8:
                self.hub()
            else:
                self.branch()
        self.emit('muxc    fl, #1')
        self.emit('muxz    fl, #2')
        for reg in REGS + ['fl']:
            self.emit('wrlong  %s, ptr' % reg)
            self.emit('add     ptr, #4')
        self.emit('wrlong  one, done')
        self.emit('cogid   ptr')
        self.emit('cogstop ptr')
        for reg in REGS:
            self.out.append('%-7s long    $%08X' % (reg, r.getrandbits(32)))
        for k, reg in enumerate(HUB):
            self.out.append('%-7s long    $%04X' % (reg, 0x6200 + r.randrange(0, 0x100, 4) + k))
        self.out += ['fl      long    0', 'ptr     long    $6000', 'done    long    $6FFC', 'one     long    1',
                     'hmask   long    $FF', 'hbase   long    $6200', '        long    $C0DEE0D0']
        return '\n'.join(self.out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--count', type=int, default=100)
    ap.add_argument('--first', type=int, default=1)
    ap.add_argument('--length', type=int, default=150)
    ap.add_argument('--hubflags', action='store_true')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for seed in range(a.first, a.first + a.count):
        with open(os.path.join(a.out, 'r%05d.spin' % seed), 'w') as f:
            f.write(Gen(random.Random(seed), a.hubflags).program(a.length))


if __name__ == '__main__':
    main()
```

`tests/pinheck/p8x32a/cmpwin.py`:

```python
#!/usr/bin/env python3
import struct
import sys

a, b = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
if len(a) != len(b):
    print('  length differs: %d vs %d' % (len(a), len(b)))
for k in range(0, min(len(a), len(b)) & ~3, 4):
    x, y = struct.unpack_from('<I', a, k)[0], struct.unpack_from('<I', b, k)[0]
    if x != y:
        print('  $%04X  spinsim %08x  ours %08x' % (0x6000 + k, x, y))
```

`isa/alu_edges.spin` exercises shifts and rotates by 0 and 31, carry-through `RCL`/`RCR`, `ABS`/`NEG` of `$80000000`, signed and unsigned `MIN`/`MAX` boundaries, extended `ADDX`/`SUBX`/`CMPSX`/`ADDSX`/`SUBSX` with carry in, `SUMC`/`SUMNC`/`SUMZ`, `CMPSUB` hit and miss, `MOVS`/`MOVD`/`MOVI`, `REV` and the muxes, each from a known C/Z state:

`tests/pinheck/p8x32a/isa/alu_edges.spin`:

```text
PUB main
  cognew(@entry, 0)
  repeat until long[$6FFC]
DAT
        long    $C0DE5EED
        org     0
entry
        test    zero, zero wz, wc
        mov     a, m1
        shl     a, #0 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, m1
        shl     a, #31 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        shr     a, #31 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        sar     a, #31 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        ror     a, #1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        rol     a, #1 wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        rcl     a, #4 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, m1
        rcl     a, #4 wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        rcr     a, #4 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        rev     a, #0 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        rev     a, #16 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        abs     a, hi wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        neg     a, hi wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        absneg  a, one wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        negc    a, one wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        negz    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        mins    a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        maxs    a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        min     a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        max     a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        min     a, #0 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, m1
        add     a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, mx
        adds    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        sub     a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        subs    a, one wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, m1
        addx    a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        subx    a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        cmpsx   a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, mx
        addsx   a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, hi
        subsx   a, zero wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        sumc    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        sumnc   a, one wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, hi
        sumz    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        addabs  a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        subabs  a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        cmpsub  a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        cmpsub  a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, hi
        cmps    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        movs    a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        movd    a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, zero
        movi    a, m1 wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, zero
        muxc    a, hi wz, wc
        call    #put
        test    one, one wz, wc
        test    zero, zero wz
        cmp     zero, one wc
        mov     a, m1
        muxnz   a, hi wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, one
        xor     a, m1 wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, m1
        andn    a, one wz, wc
        call    #put
        test    zero, zero wz, wc
        mov     a, mx
        test    a, m1 wz, wc
        call    #put
        wrlong  one, done
        cogid   a
        cogstop a
put     wrlong  a, ptr
        add     ptr, #4
        mov     f, #0
        muxc    f, #1
        muxz    f, #2
        wrlong  f, ptr
        add     ptr, #4
put_ret ret
a       long    0
f       long    0
zero    long    0
one     long    1
m1      long    $FFFFFFFF
hi      long    $80000000
mx      long    $7FFFFFFF
ptr     long    $6000
done    long    $6FFC
        long    $C0DEE0D0
```

Make them executable: `chmod +x tests/pinheck/p8x32a/gen.py tests/pinheck/p8x32a/cmpwin.py`

- [ ] **Step 2: Run the suites**

Run: `tests/pinheck/p8x32a/check.sh`
Expected: `p8x32a: 209 passed, 0 failed` (7 chip + 1 edge + 100 random against the RTL; 100 random + 1 edge against spinsim).

A `SPINSIM MISMATCH` prints lines like `  $6004  spinsim 0000000c  ours 0000000d`; an `RTL MISMATCH` prints the first differing trace lines.

- [ ] **Step 3: Commit**

```bash
git add tests/pinheck/p8x32a/gen.py tests/pinheck/p8x32a/cmpwin.py tests/pinheck/p8x32a/isa
git commit -m "p8x32a: random and edge-value differential suites (RTL and spinsim)"
```

### Task 5: Disassembler

**Files:**
- Create: `tests/pinheck/p8x32a/dasm_test.c`
- Create: `src/cpu/p8x32a/p8x32adasm.c`

**Interfaces:**
- Consumes: `unsigned p8x32a_dasm(char *buf, uint32_t op)` declared in `p8x32a.h`.
- Produces: the disassembler (returns 4; `buf` at least 64 bytes). Format: condition column (12 characters, blank for always), mnemonic (7), then `D,S`: registers as `$xxx` or their special names (`par` ... `vscl`), immediates as `#$xxx`, flag suffixes ` wz`, ` wc`, and ` wr`/` nr` only where they differ from the mnemonic's default. `R=0` forms print as `test`, `testn`, `cmp`, `cmpx`, `jmp`; hub system operations with an immediate 0-7 print as `clkset` ... `lockclr`; opcodes `$04-$07` print as `long`.

Every encoding in the table was produced by OpenSpin from the listed source and checked against it.

- [ ] **Step 1: Write the failing test**

`tests/pinheck/p8x32a/dasm_test.c`:

```c
#include "p8x32a.h"
#include <stdio.h>
#include <string.h>

static const struct { uint32_t op; const char *text; } cases[] = {
	{ 0xA0FC0000U, "             mov     $000,#$000" },
	{ 0x08BC001BU, "             rdlong  $000,$01B" },
	{ 0x007C001FU, "             wrbyte  $000,#$01F" },
	{ 0x0CFC3601U, "             cogid   $01B" },
	{ 0x0DFC3602U, "             coginit $01B wc wr" },
	{ 0x0DFC0004U, "             locknew $000 wc" },
	{ 0x0C7C3607U, "             lockclr $01B" },
	{ 0x0C7C0000U, "             clkset  $000" },
	{ 0x5C7C0000U, "             jmp     #$000" },
	{ 0x5CFC381BU, "             jmpret  $01C,#$01B" },
	{ 0x627C0001U, "             test    $000,#$001 wz" },
	{ 0x603C001BU, "             test    $000,$01B" },
	{ 0x873C001BU, "             cmp     $000,$01B wz wc" },
	{ 0x84FC0003U, "             sub     $000,#$003" },
	{ 0xC13C001BU, "             cmps    $000,$01B wc" },
	{ 0xE4D40000U, "if_nz        djnz    $000,#$000" },
	{ 0xEC7C001BU, "             tjz     $000,#$01B" },
	{ 0xF8BC001BU, "             waitcnt $000,$01B" },
	{ 0xF03C001BU, "             waitpeq $000,$01B" },
	{ 0xA0FFE8FFU, "             mov     outa,#$0FF" },
	{ 0xA0BC01FCU, "             mov     $000,phsa" },
	{ 0x80A001F1U, "if_c_and_z   add     $000,cnt" },
	{ 0x29FC0007U, "             shr     $000,#$007 wc" },
	{ 0x3CFC0010U, "             rev     $000,#$010" },
	{ 0x50FC0155U, "             movs    $000,#$155" },
	{ 0x74BC001BU, "             muxnc   $000,$01B" },
	{ 0x4CBC001BU, "             max     $000,$01B" },
	{ 0xE1FC0005U, "             cmpsub  $000,#$005 wc" },
	{ 0x5C7C0000U, "             jmp     #$000" },
	{ 0x00000000U, "nop" },
	{ 0x10000000U, "             long    $10000000" },
};

int main(void)
{
	char buf[80];
	unsigned i, bad = 0;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(buf, 0, sizeof(buf));
		if (p8x32a_dasm(buf, cases[i].op) != 4 || strcmp(buf, cases[i].text)) {
			printf("DASM FAIL %08x: got \"%s\", want \"%s\"\n", (unsigned)cases[i].op, buf, cases[i].text);
			bad++;
		}
	}
	printf("dasm: %u/%u\n", i - bad, i);
	return bad != 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `tests/pinheck/p8x32a/check.sh`
Expected: exit 2 with `p8x32adasm.c: No such file or directory`.

- [ ] **Step 3: Write the disassembler**

`src/cpu/p8x32a/p8x32adasm.c`:

```c
#include "p8x32a.h"
#include <stdio.h>
#include <string.h>

static const char *const conds[16] = {
	"if_never", "if_nc_and_nz", "if_nc_and_z", "if_nc", "if_c_and_nz", "if_nz", "if_c_ne_z", "if_nc_or_nz",
	"if_c_and_z", "if_c_eq_z", "if_z", "if_nc_or_z", "if_c", "if_c_or_nz", "if_c_or_z", ""
};

static const char *const ops[64] = {
	"wrbyte", "wrword", "wrlong", "hubop", NULL, NULL, NULL, NULL,
	"ror", "rol", "shr", "shl", "rcr", "rcl", "sar", "rev",
	"mins", "maxs", "min", "max", "movs", "movd", "movi", "jmpret",
	"and", "andn", "or", "xor", "muxc", "muxnc", "muxz", "muxnz",
	"add", "sub", "addabs", "subabs", "sumc", "sumnc", "sumz", "sumnz",
	"mov", "neg", "abs", "absneg", "negc", "negnc", "negz", "negnz",
	"cmps", "cmpsx", "addx", "subx", "adds", "subs", "addsx", "subsx",
	"cmpsub", "djnz", "tjnz", "tjz", "waitpeq", "waitpne", "waitcnt", "waitvid"
};

static const char *const sysops[8] = { "clkset", "cogid", "coginit", "cogstop", "locknew", "lockret", "lockset", "lockclr" };

static const char *const sregs[16] = {
	"par", "cnt", "ina", "inb", "outa", "outb", "dira", "dirb",
	"ctra", "ctrb", "frqa", "frqb", "phsa", "phsb", "vcfg", "vscl"
};

static void reg(char *b, unsigned r)
{
	if (r >= 0x1F0) strcpy(b, sregs[r - 0x1F0]);
	else sprintf(b, "$%03X", r);
}

unsigned p8x32a_dasm(char *buf, uint32_t op)
{
	unsigned o = op >> 26, cond = (op >> 18) & 15, d = (op >> 9) & 511, s = op & 511;
	int wz = (op >> 25) & 1, wc = (op >> 24) & 1, wr = (op >> 23) & 1, im = (op >> 22) & 1, defwr = 1, sys = 0;
	const char *name = ops[o];
	char db[16], sb[16], fl[16];

	if (op == 0) { strcpy(buf, "nop"); return 4; }
	if (!name) { sprintf(buf, "%-12s %-7s $%08X", "", "long", (unsigned)op); return 4; }
	switch (o) {
	case 0x00: case 0x01: case 0x02:
		if (wr) name = o == 0 ? "rdbyte" : o == 1 ? "rdword" : "rdlong";
		defwr = wr;
		break;
	case 0x03:
		if (im && s < 8) { name = sysops[s]; sys = 1; defwr = s == 1 || s == 4; }
		else defwr = 0;
		break;
	case 0x17: if (!wr) name = "jmp"; defwr = wr; break;
	case 0x18: if (!wr) name = "test"; defwr = wr; break;
	case 0x19: if (!wr) name = "testn"; defwr = wr; break;
	case 0x21: if (!wr) name = "cmp"; defwr = wr; break;
	case 0x33: if (!wr) name = "cmpx"; defwr = wr; break;
	case 0x30: case 0x31: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3F: defwr = 0; break;
	}
	reg(db, d);
	if (im) sprintf(sb, "#$%03X", s);
	else reg(sb, s);
	fl[0] = 0;
	if (wz) strcat(fl, " wz");
	if (wc) strcat(fl, " wc");
	if (wr != defwr) strcat(fl, wr ? " wr" : " nr");
	if (sys) sprintf(buf, "%-12s %-7s %s%s", conds[cond], name, db, fl);
	else if (o == 0x17 && !wr) sprintf(buf, "%-12s %-7s %s%s", conds[cond], name, sb, fl);
	else sprintf(buf, "%-12s %-7s %s,%s%s", conds[cond], name, db, sb, fl);
	return 4;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tests/pinheck/p8x32a/check.sh`
Expected: `dasm: 31/31` and `p8x32a: 209 passed, 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/cpu/p8x32a/p8x32adasm.c tests/pinheck/p8x32a/dasm_test.c
git commit -m "p8x32a: disassembler"
```

### Task 6: Exit criterion: booting the game image, and proof the tests bite

**Files:**
- Create: `tests/pinheck/p8x32a/README.md`

**Interfaces:**
- Consumes: everything above; `P8X32A_ROM` (Task 1) and the Domino's `PRP_V008.BIN`.
- Produces: the milestone's exit evidence (spec §7/§8 milestone 3).

- [ ] **Step 1: Write the README**

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
```

- [ ] **Step 2: Run the exit criterion**

Run: `P8X32A_ROM=$PWD/tests/pinheck/p8x32a/build/p8x32a.rom DOMINOS_PRP=/path/to/PRP_V008.BIN tests/pinheck/p8x32a/check.sh`
Expected (the first run spends about two minutes per boot case building the cached RTL references):
```text
boot ext=0: 655460 trace lines match, interpreter start at 16139093
boot ext=80000000: 655460 trace lines match, interpreter start at 19139109
p8x32a: 211 passed, 0 failed
```
Both cases boot `PRP_V008.BIN` from the EEPROM model through the real booter; every pin edge of the I2C transfer, both `CLKSET`s and the interpreter `COGINIT` land on the RTL's exact cycles, and hub RAM is identical at that moment. Case `80000000` holds P31 high, so the booter first waits for a host and times out (Review Focus item 3).

- [ ] **Step 3: Prove the suites can fail**

Apply each mutation to `src/cpu/p8x32a/p8x32a.c`, run `SEEDS=20 tests/pinheck/p8x32a/check.sh`, confirm a failure of the named kind, then restore with `git checkout src/cpu/p8x32a/p8x32a.c`:

| Mutation (replace → with) | Must fail |
|---|---|
| `uint64_t base = p->slot_base + 3 + 2 * (uint64_t)n;` → `uint64_t base = p->slot_base + 5 + 2 * (uint64_t)n;` | RTL MISMATCH chip/cogs.spin (hub slot phase) |
| `if (DST(i) >= 0x1F0) special_write(p, n, DST(i), r, m3);` → `if (wr && DST(i) >= 0x1F0) special_write(p, n, DST(i), r, m3);` | RTL MISMATCH chip/special.spin (write-cancelled MAX still drives OUTA) |
| `w = rd32(p, a);` → `w = FWR(c->i) ? rd32(p, a) : c->d;` | RTL MISMATCH build/rtl/rand/r00001.spin (hub write returns the old value) |
| `c->ev_t = m + (uint32_t)(c->d - cnt(p, m)) + 2;` → `c->ev_t = m + (uint32_t)(c->d - cnt(p, m)) + 3;` | RTL MISMATCH chip/cogs.spin (WAITCNT latency) |
| `w = unscramble(w);` → `w = unscramble(0) ^ w;` | RTL MISMATCH on every test (cog loads from ROM are unscrambled) |
| `if (!(t->ev == EV_HUB && t->latch <= h) && (int)num != n) idle(p, (int)num);` → `if ((int)num != n) idle(p, (int)num);` | RTL MISMATCH chip/restart.spin (latched hub write completes across COGINIT) |
| `case 0x1FA: case 0x1FB: ctr_rebase(c, k, e); c->frq[k] = v; break;` → `case 0x1FA: case 0x1FB: c->frq[k] = v; break;` | RTL MISMATCH chip/special.spin (PHS rebase on FRQ write) |
| `if (op == 0x39) jc = dz && (c->d & 1);` → `if (op == 0x39) jc = 0;` | RTL MISMATCH chip/cogs.spin (DJNZ not-taken cancel) |
| `if (p->bus.pins_next) {` → `if (0) {` | RTL MISMATCH chip/waitext.spin (external edge ends a pin wait) |

Then `git status --short src/` must print nothing.

- [ ] **Step 4: Record throughput and commit**

Run: `python3 -c "import subprocess, sys, time; t = time.time(); subprocess.run(sys.argv[1:], stdout=subprocess.DEVNULL); print('%.2fs' % (time.time() - t))" tests/pinheck/p8x32a/build/p8run -rom "$P8X32A_ROM" -eeprom tests/pinheck/p8x32a/build/boot/eeprom.bin -cycles 60000000 -stop 0 7c01`
Expected: about 0.3 s for the 16.1-million-cycle boot (one cog busy, others idle and fast-forwarded); the RTL needs about two minutes.

```bash
git add tests/pinheck/p8x32a/README.md
git commit -m "p8x32a: boot exit test documentation"
```
