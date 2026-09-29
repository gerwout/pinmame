# Milestone 9: performance, stages 1 and 2 (profile, interpreter fast paths) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A headless benchmark and profile of Domino's in `sdl3pinmame` (stage 1), and exact interpreter fast paths in the Propeller and MIPS32 cores (stage 2) that make the emulation about 5× faster in attract mode and about 4.8× faster with video and four sound channels, while the UART1 log, display frames, sound and NVRAM stay byte-identical to the reference build's; the measured result, below real time, is handed to stage 3 with the numbers it must deliver.

**Architecture:** `tests/pinheck/perf/bench.sh` runs fixed workloads headless (attract mode; a video with music and three sound effects) with the DS1340 seeded from a fixed clock, times a window of each run and, given a reference build, requires byte-identical UART1, frame, sound and NVRAM output; `bench.sh profile` records one workload with `perf` and `components.py` groups the samples by source file. The profile blames the Propeller core (about 85% of the time): its gate-level ALU, one scheduler event per instruction and cogs that spend most of their time polling. Stage 2 makes the `p8x32a` core compute only the ALU unit an instruction uses, read `INA` from the pins it has already flushed, run instructions that touch nothing shared ahead of the other cogs in a tight loop, and put a cog to sleep while it polls a loop that changes nothing, waking it at the exact cycle an input may change and restoring its state from one recorded iteration. The `mips32` core fetches, loads and stores RAM and flash directly, keeps Count lazily and checks interrupts only when their inputs change.

**Tech Stack:** C (C89-syntax-clean cores, `-std=c99 -Wall -Wextra -Werror -pedantic` test builds), `perf` 7.1, Python 3 (no third-party modules), POSIX sh, the existing Verilator P1 RTL, spinsim and QEMU oracles.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §4 (binding), parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` §5 and §7 (binding where the addendum is silent). Stages 3 (JIT) and 4 (threading) are not part of this plan (Ruling 1).

## Prerequisites

- Plans 1–8a executed and merged: `pinheck` at `3d9971c3` (Plan 8b's plan text on top of Plan 8a and its fixes) or later. Proven and replayed on exactly that commit. This plan edits Plan 1's `mips32.[ch]`, Plan 2's `pic32mx.c`, Plan 3's `p8x32a.[ch]` and test runners, Plan 5's `prop.[ch]`, and adds `tests/pinheck/perf/`. Nothing in `pinheck.c`, the board, display or audio blocks changes.
- Plan 8b (the playfield simulator and the scripted game) is not needed for Tasks 1–4 and 6. Task 5 adds the scripted game as a workload once Plan 8b is merged; until then it is skipped and the plan is complete without it.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export: `TOOLS=$PWD/tests/pinheck/p8x32a/build/tools` (or the main checkout's, where the oracles are built); `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = the romset zip; `DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN`; `DOMINOS_ZIP=$PINHECK_ZIP`. Every command below uses these.
- `perf` is installed (`sudo apt install linux-perf`) and may count user-space events: `sudo sysctl kernel.perf_event_paranoid=1` (Kali's default, 3, refuses them).
- The p8x32a suite builds its RTL boot references once (about 10 minutes); copying `tests/pinheck/p8x32a/build/boot/*.rtl*` from a checkout that already has them skips that.
- The reference machine is shared: other emulators and builds run beside these steps. Wall-clock speeds below were measured at the load averages stated with them; `perf stat`'s instruction counts do not depend on load and are the figures to compare. Every PinMAME run has a `timeout -k 30`.

## Global Constraints

- Exactness: every optimisation leaves the machine's behaviour unchanged, cycle for cycle. The RTL, spinsim and QEMU differential suites stay byte-identical, and `bench.sh` with `REFERENCE` requires the UART1 log, the display frame log, the sound capture and the NVRAM of each workload to equal the reference build's byte for byte.
- The reference build is `pinheck` at the commit this plan starts from, built Release exactly as below, and copied to `build/reference/sdl3pinmame` before any source changes.
- Cores stay pure C with no PinMAME headers, C89-syntax-clean (`-std=c89 -pedantic-errors -Wno-long-long`, which `check.sh` gates) and warning-free in the test builds; the core structs stay free of pointers into themselves, so a state copy is independent (`unit_test.c`'s `state_copy_is_independent`). Code comments stay short.
- Test hooks stay inert unless set; the benchmark's clock shim is a test file (`tests/pinheck/perf/fixtime.c`, `LD_PRELOAD`), never linked into PinMAME.
- Every sdl3pinmame launch uses private `-nvram_directory` and `-cfg_directory` and a `timeout -k 30`.
- Every existing suite stays green: `tests/pinheck/{mips32,pic32mx,storage,link,p8x32a,display,audio,board}/check.sh`, `pic32mx/pinmame_check.sh` (with `romset_check.sh`), `display/pinmame_display.sh`, `audio/pinmame_audio.sh`, `board/pinmame_board.sh`.

## Rulings

Decisions the spec left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it).

1. **Scope: stages 1 and 2, measured; no JIT or threading yet.** Spec §4 takes the stages "only as far as needed". With stage 2 complete the emulation runs at 0.45–0.53× in attract mode and 0.40–0.47× with video and four sound channels (Task 4), short of 1.0×. Stages 3 and 4 were not prototyped, so they are not in this plan (the brief: include them only if proven); the Result section states what stage 3 must deliver. Cost if wrong: none for correctness; stage 3 gets its own plan.
2. **A fixed clock for the benchmark: an `LD_PRELOAD` shim of `time()`.** The DS1340 is seeded from host time at every reset and the firmware prints and uses it (two runs of the same build a minute apart differ in UART1 and in the frames). `pinheck` has no clock hook (Plan 8b adds `PINHECK_RTC`, not merged), so `bench.sh` preloads `fixtime.c`, which makes `time()` return `PINHECK_FIXTIME` (2026-09-29 12:00:00 UTC). With it two runs are byte-identical (Task 1 Step 3); without it they are not (Step 4). The scripted game (Task 5) uses Plan 8b's `PINHECK_RTC` as `pinmame_game.sh` does. Cost if wrong: the shim fixes every `time()` call in the process, which PinMAME uses only for seeds and file dates.
3. **Workloads and window.** Each workload starts from NVRAM written by a first boot in service (`PINHECK_INSERVICE=6`, 1,200 frames, as check 5), so no run repeats the update. The timed window starts at a key-script `mark` (whose file the harness stamps) and ends when PinMAME exits: attract mode from 10 s to 40 s; video from 16 s to 40 s, after `[E96000]`, `[V00AT9]` (a 29 s clip), `[F00ZM0]` (music) and the effects `[F00BWI]`, `[F00IF0]`, `[F00NBI]` sent a second apart from 10 s (UART1 answers `Playing Video`, `Playing Music` and `Playing SFX` for each), with `NBI` and `IF0` sent again at 25 s and 29 s so four channels stay busy. The sound is captured through `PINHECK_WAV` at 48 kHz with `-fakesound`, as `pinmame_audio.sh` does.
4. **Profile by source file.** The Release build uses link-time optimisation, which inlines across files (the Propeller's scheduler shows up as `prop_catch_up.constprop.0`), so `bench.sh profile` expects a Release build with `-g` and `components.py` groups `perf report --sort srcfile` by component.
5. **The Propeller's `INA` is the last flushed pin state.** `ina()` flushes every pin change up to `t` and then called `p8x32a_pins(t)` again; after the flush the outputs at `t` are exactly `last_out`/`last_dir` (flush visits every output change point: pending register changes and counter toggles), so `ina()` uses them.
6. **Counter change points are cached.** `ctr_next(t)` scanned all 16 counters for every flush step. The next change after `t` is also the next change after any later `t'` before it, until a counter is written (`ctr_save`, which every counter write, `COGSTOP` and reset go through), so the result is cached with its validity interval. A cache per counter was measured and rejected: it executed more instructions than the single cache.
7. **The ALU computes only the unit an instruction uses.** The Milestone 3 ALU evaluated the rotator (with a 32-step bit reversal), the logic unit and the adder for every instruction, and then selected one. `alu()` keeps its exact formulas but evaluates only the selected unit; `alu_run()` adds direct cases for the common ops of a running cog. `alu_test.c` keeps the Milestone 3 ALU verbatim as the reference and compares both over 4,000,000 random and edge-value cases per run; a mutation of one case fails it.
8. **Local instructions run ahead.** An instruction that reads no `INA`, is not a hub op or a wait, and writes no special register (`OUTA`, `DIRA`, counters, video) touches nothing another cog or the pins can see; `CNT` and `PHS` are functions of time and of the cog's own counters. Such instructions run in `run_local`, a tight loop of the core's own exec/complete steps, up to the run horizon, before the scheduler picks the next cog. Their order against other cogs' events is unobservable; a cog stopped or restarted by another cog while it ran ahead restarts from a full reload of its RAM or never runs again, so the instructions it ran past its stop are unobservable too.
9. **Idle loops sleep.** In attract mode three of the six running cogs spent most of their instructions polling: the link receiver (cog 7, a two-instruction `INA` loop, 20 M instructions/s), a hub-polling loop (cog 6, `RDWORD`/`TJZ`), a nine-instruction loop that copies a pin into hub RAM and polls a command byte (cog 3). A loop iteration from a backward jump back to its target that changes no cog RAM, flag, pin, counter or hub content, reads no `CNT` or `PHS`, performs no wait or hubop, has at most 16 instructions, 4 `INA` reads and 4 hub accesses, and ends in exactly the state it started in one period `P` later (with `P` a multiple of 16 if it has hub ops, so the hub slots repeat) repeats itself as long as the pins it reads (under the masks its `AND`/`TEST`/`ANDN` apply) and the hub bytes it reads or writes stay the same. The core records one iteration (the cog's state before each of its events) and puts the cog to sleep until the earliest time an input may change: a pending pin change or counter toggle on the pins it reads, the next external edge (`pins_next`), a hub write by another cog to its bytes, or the run horizon plus one (edges the host appends later are never earlier). On waking it compares the inputs: unchanged, it sleeps again; changed, its state is restored from the recorded event at or after the wake time, shifted by whole periods. A `COGINIT` or `COGSTOP` of a sleeping cog restores it first. The pins whose external value can change inside `pins_out` (SD `DO`, EEPROM `SDA`) are not `pure_in`, and a loop reading them wakes on every output change. The RTL tests `idle_ina`, `idle_hub` and `idle_ctr` exercise each wake path and pin their sleep counts (`EXPECT-SLEEPS`).
10. **MIPS32 memory is served directly.** `mips32_direct()` registers RAM (read-write), flash and boot flash (read-only) as host memory; fetches, loads and stores inside them bypass the bus callbacks, which keep every SFR access and every error case. The last fetch region is cached by virtual address until the translation mode (Status) or the regions change. `mips32run` maps its memory except the test MMIO page, so the QEMU differential suite runs the fast path; `unit_test.c` keeps the callbacks.
11. **MIPS32 Count is lazy and interrupts are checked on change.** Count advanced by `tick()` after every instruction. It is now computed from the cycle counter (`count`/`count_half` hold it at `count_at`, synchronised at every run exit, so callers that read or set the fields between runs see the same values), and the timer interrupt fires after the instruction whose cycles reach `ti_at`, the first cycle at which Count equals Compare, computed when Count or Compare is written or a run starts. `irq_pending()` is evaluated only after a change of its inputs: `mips32_set_eic`, Status writes, `EI`/`DI`, `ERET`, an exception, or a run start.
12. **Spin interpreters are busy, not idle.** Cogs 0 and 5 run the ROM Spin interpreter (16 M instructions/s each); a histogram of their bytecode addresses shows work spread over hundreds of bytecodes, not a polling loop. They, and the MIPS32 core, are what stage 3 must speed up.
13. **No table of pre-decoded instructions.** Spec §4 lists pre-decoded instructions among the fast paths. The Propeller's cog code rewrites itself constantly (`MOVS`/`MOVD`/`MOVI`/`JMPRET` in the ROM Spin interpreter's inner loop), so a decoded copy would be invalidated by most writes; `run_local` and `alu_run` decode the few fields they need straight from the instruction word held in a register. The MIPS32 fetch reads a cached host pointer and decodes with a switch; a decoded table would need invalidating on flash programming (the bootloader stand-in) and RAM writes. Stage 3's JIT subsumes both. Cost if wrong: some interpreter speed left for stage 3.

## Review Focus

- **A sleeping cog whose input changes while it sleeps** (another cog drives the polled pin, a counter toggles it, the host adds an edge, a hub byte it reads or writes is overwritten): it must resume at exactly the cycle the hardware would have seen the change, or the link, the SD card and the display desynchronise. The RTL tests `idle_ina` (external edges, another cog's `OUTA` on the polled pin, `OUTA` toggles on other pins that must not wake it, a hub write to a byte the loop writes), `idle_ctr` (an NCO counter on the polled pin) and `idle_hub` (hub writes; it also stops and restarts a sleeping cog, but a lost resume on `COGSTOP` or `COGINIT` of a sleeper fails `nco` and `waits`, not `idle_hub`) in Task 3, each byte-identical to the RTL with a pinned sleep count; mutations of the wake paths fail them (Task 6).
- **The timer interrupt and EIC requests after the lazy Count and the change-driven interrupt check** (Compare written above or below Count, Count written, a `DIV` crossing Compare, `WAIT`, an interrupt raised by an SFR store): the firmware's scheduler runs on them. `unit_test.c`'s new `timer_fires_on_its_cycle` and `eic_raised_by_a_store` (Task 4 Step 2; the mutations of Task 6 fail them), the golden `cp0` and `eic` programs, `compare_crossed_by_div`, and the machine checks, which boot, update, sync and run the service menu on them.
- **Code running from memory that changes** (the bootloader stand-in reprograms flash; RAM functions): direct fetches read live memory, and the fetch cache holds a pointer, not instructions. Machine check 2 (`pinmame_check.sh`: update from blank NVRAM, reset, sync) in Task 4.
- **Long runs against the reference** (drift that only shows after seconds: frame timing, sound phase, NVRAM): `bench.sh` with `REFERENCE` compares whole workloads byte for byte after each of Tasks 2–4, and Task 5 adds the 315 s scripted game.
- **A loaded machine skewing the numbers**: the benchmark reports `perf stat` instructions and CPU seconds per emulated second next to wall-clock speed and the load average, and the Result section quotes both.

## File Structure

| File | Responsibility |
|---|---|
| `tests/pinheck/perf/bench.sh` | headless workloads, timed window, determinism against a reference build, `profile` mode |
| `tests/pinheck/perf/fixtime.c` | `LD_PRELOAD` shim: `time()` returns `PINHECK_FIXTIME` |
| `tests/pinheck/perf/components.py` | `perf report --sort srcfile` grouped into components |
| `tests/pinheck/perf/.gitignore` | the benchmark's run directories |
| `src/cpu/p8x32a/p8x32a.[ch]` | ALU by unit, `alu_run`, `INA` from flushed pins, cached counter change points, local run-ahead (`run_local`), idle-loop sleep (modified) |
| `src/wpc/pinheck/prop.[ch]` | Propeller times of queued PIC32 edges computed once; `pure_in` (modified) |
| `tests/pinheck/p8x32a/alu_test.c` | the core's ALUs against the Milestone 3 ALU |
| `tests/pinheck/p8x32a/chip/idle_ina.spin`, `idle_hub.spin`, `idle_ctr.spin` | RTL tests of every wake path, with pinned sleep counts |
| `tests/pinheck/p8x32a/check.sh`, `run.c` | the ALU test; `EXPECT-SLEEPS` and `-sleeps`; `pure_in` (modified) |
| `src/cpu/mips32/mips32.[ch]` | direct memory regions, fetch region cache, lazy Count, interrupt checks on change (modified) |
| `src/cpu/pic32mx/pic32mx.c` | registers RAM, flash and boot flash as direct regions (modified) |
| `tests/pinheck/mips32/run.c` | maps its memory directly (modified) |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | measured results and stage 3's target (modified) |

---
### Task 1: Benchmark and profile (stage 1)

**Files:**
- Create: `tests/pinheck/perf/bench.sh`, `tests/pinheck/perf/fixtime.c`, `tests/pinheck/perf/components.py`, `tests/pinheck/perf/.gitignore`

**Interfaces:**
- Consumes: `sdl3pinmame`'s `-headless -frames_to_run N` (N−1 frames are emulated), `-key_script` `mark` lines (written to `<script>.marks` when reached), `-samplefreq 48000 -fakesound`; the driver's hooks `PINHECK_INSERVICE`, `PINHECK_UART1_LOG`, `PINHECK_FRAME_LOG`, `PINHECK_WAV`, `PINHECK_UART1_SEND`, `PINHECK_UART1_SEND_AT`, `PINHECK_UART1_SEND_GAP`.
- Produces: `bench.sh [attract] [video]` with `SDL3PINMAME` (and optional `REFERENCE`), printing per workload and build `bench <workload> <opt|ref>: <speed>x over <s> s emulated (<s> s wall), whole run <G> G instructions and <s> s CPU per emulated s, load <l>` and, with `REFERENCE`, `determinism <workload>: identical uart.log frames.bin snd.wav nvram/dominos.nv` or `DETERMINISM FAIL <workload>: <file> differs from the reference build's` (exit 1); `bench.sh profile <workload>` printing the component table; run directories `tests/pinheck/perf/build/<opt|ref|prof>/<workload>/`.

- [ ] **Step 1: Build the reference**

Build `pinheck` as it stands (Release, `PLATFORM=linux`, `ARCH=x64`, never the user's `build-dbg`), keep the binary as the reference, and build the same tree with `-g` for profiles:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m9.log 2>&1; tail -1 build/sdl3pinmame/m9.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
cmake -S . -B build/sdl3pinmame-prof -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DCMAKE_C_FLAGS=-g -DCMAKE_CXX_FLAGS=-g > /dev/null
timeout 2700 cmake --build build/sdl3pinmame-prof -j"$(nproc)" > build/sdl3pinmame-prof/m9.log 2>&1; tail -1 build/sdl3pinmame-prof/m9.log
```
Expected: `[100%] Built target sdl3pinmame` twice.

- [ ] **Step 2: Write the benchmark**

`tests/pinheck/perf/fixtime.c`:

```c
/* LD_PRELOAD shim for benchmark runs: time() returns PINHECK_FIXTIME, so the DS1340 starts at the same second in every run */
#include <stdlib.h>
#include <time.h>

time_t time(time_t *t)
{
	const char *v = getenv("PINHECK_FIXTIME");
	time_t r = v ? (time_t)strtoll(v, NULL, 10) : 0;
	if (t) *t = r;
	return r;
}
```

`tests/pinheck/perf/bench.sh`:

```sh
#!/bin/sh
# Headless benchmark and determinism check (Milestone 9).
#   bench.sh [attract] [video]     time each workload with $SDL3PINMAME; with $REFERENCE set, run it too
#                                  and require byte-identical UART1, frame, sound and NVRAM output
#   bench.sh profile WORKLOAD      perf record one workload with $SDL3PINMAME, then components.py
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
[ -n "$REFERENCE" ] && { REFERENCE=$(realpath "$REFERENCE") || exit 2; }
cd "$(dirname "$0")" || exit 2
B=$PWD/build
FIXTIME=1790683200   # 2026-09-29 12:00:00 UTC
mkdir -p $B && cc -O2 -shared -fPIC -o $B/fixtime.so fixtime.c || exit 2
PERF=$(command -v perf)

# workload: frames to run, frame the timed window starts at, UART1 commands
spec() {
	case $1 in
	attract) FRAMES=2400 MARK=600 SEND_AT=0 SEND= ;;
	video) FRAMES=2400 MARK=960 SEND_AT=10 SEND='[E96000]~[V00AT9]~[F00ZM0]~[F00BWI]~[F00IF0]~[F00NBI]~~~~~~~~~~[F00NBI]~~~~[F00IF0]' ;;
	*) echo "bench: unknown workload $1"; exit 2 ;;
	esac
}

# machine directory for one build: romset, and NVRAM after a first boot in service (kept while the binary is the same)
machine() {
	M=$B/$1
	sum=$(sha1sum < "$2")
	[ -s $M/nvram.base/dominos.nv ] && [ "$(cat $M/binary.sha 2> /dev/null)" = "$sum" ] && return
	rm -rf $M && mkdir -p $M/roms $M/cfg $M/nvram.base || exit 2
	cp "$P8X32A_ROM" $M/p8x32a.rom && (cd $M && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
	ln -s "$PINHECK_ZIP" $M/roms/dominos.zip || exit 2
	(cd $M && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 timeout -k 30 3000 "$2" dominos -rompath roms \
		-nvram_directory nvram.base -cfg_directory cfg -headless -frames_to_run 1200 -skip_gamewarnings -nothrottle > boot.out 2>&1) \
		|| { echo "BENCH FAIL: first boot with $2 exited $?"; exit 1; }
	echo "$sum" > $M/binary.sha
}

# run one workload with one build under perf stat, perf record (mode "record") or neither
run() {
	label=$1 bin=$2 w=$3
	spec $w
	machine $label $bin
	D=$B/$label/$w
	# the reference's runs are kept while its binary is the same
	if [ $label = ref ] && [ -s $D/bench.txt ]; then cat $D/bench.txt; return; fi
	rm -rf $D && mkdir -p $D && cp -r $B/$label/nvram.base $D/nvram || exit 2
	wrap=
	[ -n "$PERF" ] && wrap="$PERF stat -e task-clock,instructions:u,cycles:u -o $D/perf.txt"
	[ -n "$PERF" ] && [ "$4" = record ] && wrap="$PERF record -F 999 -o $D/perf.data --"
	echo "$MARK mark window" > $D/keys.txt
	t0=$(date +%s.%N)
	(cd $D && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 PINHECK_UART1_LOG=$D/uart.log \
		PINHECK_FRAME_LOG=$D/frames.bin PINHECK_WAV=$D/snd.wav PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND_GAP=1 PINHECK_UART1_SEND="$SEND" \
		timeout -k 30 5400 $wrap "$bin" dominos -rompath ../roms -nvram_directory nvram -cfg_directory ../cfg -headless \
		-frames_to_run $FRAMES -skip_gamewarnings -nothrottle -samplefreq 48000 -fakesound -key_script keys.txt > run.out 2>&1) \
		|| { echo "BENCH FAIL: $w with $bin exited $?"; tail -3 $D/run.out; exit 1; }
	t1=$(date +%s.%N)
	[ -s $D/keys.txt.marks ] || { echo "BENCH FAIL: $w never reached frame $MARK"; exit 1; }
	python3 - "$D" "$label" "$w" "$t0" "$t1" "$MARK" "$FRAMES" <<'PY'
import os, re, sys
d, label, w, t0, t1, mark, frames = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), float(sys.argv[5]), int(sys.argv[6]), int(sys.argv[7])
emu = (frames - 1 - mark) / 60.0          # -frames_to_run N emulates N-1 frames
wall = t1 - os.stat(d + '/keys.txt.marks').st_mtime
line = 'bench %s %s: %.3fx over %.1f s emulated (%.1f s wall)' % (w, label, emu / wall, emu, wall)
try:
    st = open(d + '/perf.txt').read()
    ins = float(re.search(r'([\d,]+)\s+instructions', st).group(1).replace(',', ''))
    task = float(re.search(r'([\d,.]+) msec task-clock', st).group(1).replace(',', '')) / 1000
    line += ', whole run %.1f G instructions and %.1f s CPU per emulated s' % (ins / 1e9 / ((frames - 1) / 60.0), task / ((frames - 1) / 60.0))
except (OSError, AttributeError):
    pass
line += ', load %s' % open('/proc/loadavg').read().split()[0]
print(line)
open(d + '/bench.txt', 'w').write(line + '\n')
PY
}

if [ "$1" = profile ]; then
	[ -n "$PERF" ] || { echo "bench: perf not found"; exit 2; }
	run prof "$SDL3PINMAME" "${2:-attract}" record || exit 1
	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort srcfile 2> /dev/null | python3 components.py
	exit 0
fi
[ $# -eq 0 ] && set -- attract video
fail=0
if [ -n "$REFERENCE" ]; then
	machine opt "$SDL3PINMAME"
	machine ref "$REFERENCE"
	if cmp -s $B/opt/nvram.base/dominos.nv $B/ref/nvram.base/dominos.nv; then echo "determinism first boot: identical nvram.base/dominos.nv"
	else echo "DETERMINISM FAIL first boot: nvram.base/dominos.nv differs from the reference build's"; fail=1; fi
fi
for w in "$@"; do
	run opt "$SDL3PINMAME" $w || exit 1
	[ -n "$REFERENCE" ] || continue
	run ref "$REFERENCE" $w || exit 1
	same=
	for f in uart.log frames.bin snd.wav nvram/dominos.nv; do
		if cmp -s $B/opt/$w/$f $B/ref/$w/$f; then same="$same $f"; else echo "DETERMINISM FAIL $w: $f differs from the reference build's"; fail=1; fi
	done
	echo "determinism $w: identical$same"
done
[ $fail -eq 0 ]
```

`tests/pinheck/perf/components.py`:

```python
#!/usr/bin/env python3
"""Group 'perf report --sort srcfile' output (on stdin) into the machine's components."""
import re
import sys

GROUPS = [
    ('PIC32 (mips32, pic32mx)', ('mips32.c', 'pic32mx.c', 'pic32mxcpu.c')),
    ('Propeller core (p8x32a)', ('p8x32a.c',)),
    ('Propeller stepping and link (prop.c)', ('prop.c', 'bootldr.c')),
    ('SD card and EEPROM (sd, vfat, zipsrc, eeprom, zlib)', ('sd.c', 'vfat.c', 'zipsrc.c', 'eeprom.c', 'inflate.c', 'inffast.c', 'crc32.c', 'adler32.c')),
    ('display and audio (display.c, audio.c)', ('display.c', 'audio.c')),
    ('board and driver (board.c, pinheck.c, rtc.c)', ('board.c', 'pinheck.c', 'rtc.c')),
]
total = {}
for line in sys.stdin:
    m = re.match(r'\s*([\d.]+)%\s+(\S*)\s*$', line)
    if not m:
        continue
    pct, src = float(m.group(1)), m.group(2)
    name = 'other (PinMAME, kernel, libraries)'
    for g, files in GROUPS:
        if src in files:
            name = g
            break
    total[name] = total.get(name, 0.0) + pct
for name, pct in sorted(total.items(), key=lambda x: -x[1]):
    print('%6.1f%%  %s' % (pct, name))
```

`tests/pinheck/perf/.gitignore`:

```text
build/
```

`chmod +x tests/pinheck/perf/bench.sh tests/pinheck/perf/components.py`

- [ ] **Step 3: The reference is repeatable under the fixed clock**

Run: `SDL3PINMAME=build/reference/sdl3pinmame REFERENCE=build/reference/sdl3pinmame timeout 7200 tests/pinheck/perf/bench.sh attract` (about 20 minutes at 0.13×)
Expected (speeds vary with the load):
```text
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.071x over 30.0 s emulated (419.7 s wall), whole run 111.4 G instructions and 14.3 s CPU per emulated s, load 8.67
bench attract ref: 0.095x over 30.0 s emulated (316.7 s wall), whole run 111.4 G instructions and 10.3 s CPU per emulated s, load 6.36
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 4: Without the fixed clock it is not**

Boot the reference twice from Step 3's NVRAM as the harness does, but without the shim, and compare the UART1 logs:

```sh
REF=$PWD/build/reference/sdl3pinmame M=$PWD/tests/pinheck/perf/build/ref
for n in 1 2; do
	rm -rf $M/noshim$n && cp -r $M/nvram.base $M/noshim$n
	(cd $M && PINHECK_INSERVICE=6 PINHECK_UART1_LOG=$M/noshim$n.log timeout -k 30 1800 "$REF" dominos -rompath roms -nvram_directory noshim$n \
		-cfg_directory cfg -headless -frames_to_run 400 -skip_gamewarnings -nothrottle > /dev/null 2>&1)
done
python3 - $M/noshim1.log $M/noshim2.log <<'EOF'
import re, sys
a, b = (open(p, 'rb').read().decode('latin-1').splitlines() for p in sys.argv[1:])
diff = [x for x, y in zip(a, b) if x != y]
clock = all(re.match(r'\d{4}/\d+/\d+ \d+:\d+:\d+$', x) for x in diff)
print('noshim: %d of %d UART1 lines differ, %s' % (len(diff), len(a), 'each of them the clock' if clock else 'not only the clock'))
sys.exit(1 if diff else 0)
EOF
```
Expected (the two runs print different dates; exit 1):
```text
noshim: 1 of 56 UART1 lines differ, each of them the clock
```

- [ ] **Step 5: Profile the reference**

Run: `SDL3PINMAME=build/sdl3pinmame-prof/sdl3pinmame timeout 5400 tests/pinheck/perf/bench.sh profile attract | tail -8`
Expected (percentages vary by about a point):
```text
bench attract prof: 0.070x over 30.0 s emulated (426.9 s wall), load 11.71
  85.2%  Propeller core (p8x32a)
  10.3%  PIC32 (mips32, pic32mx)
   2.7%  other (PinMAME, kernel, libraries)
   1.2%  Propeller stepping and link (prop.c)
   0.3%  SD card and EEPROM (sd, vfat, zipsrc, eeprom, zlib)
   0.2%  display and audio (display.c, audio.c)
   0.1%  board and driver (board.c, pinheck.c, rtc.c)
```

The Propeller core is the stage 2 target; within it (`perf report --sort srcline`) the ALU's bit-reversal loop, the scheduler's search for the next cog event and `INA`'s pin evaluation lead, and an event count per cog (Ruling 9) shows three of the six running cogs polling.

- [ ] **Step 6: Commit**

```bash
git add tests/pinheck/perf
git commit -m "pinheck: headless benchmark with a determinism check, and a profile by component"
```

### Task 2: Propeller interpreter fast paths (stage 2a)

**Files:**
- Create: `tests/pinheck/p8x32a/alu_test.c`
- Modify: `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32a.h`, `src/wpc/pinheck/prop.c`, `src/wpc/pinheck/prop.h`, `tests/pinheck/p8x32a/check.sh`

**Interfaces:**
- Consumes: Plan 3's core (`alu`, `exec`, `complete`, `ina`, `flush`, `ctr_next`, `p8x32a_run_until`), Plan 5's `prop.c` edge queue.
- Produces: `static uint32_t alu_run(unsigned i, uint32_t s, uint32_t d, unsigned pc, int ci, int zi, int bus_c, int *wr, int *co, int *zo)` (a running cog's non-hub ALU; `alu_test.c` calls it); `p8x32a` gains `int ctr_ok; uint64_t ctr_from, ctr_nt;` (the cached next counter change); `static void run_local(p8x32a *p, int n, uint64_t t)` and `static int local(const p8x32a_cog *c)`, which Task 3 extends; `prop_edge` gains `uint64_t prop` (the edge's Propeller time).

- [ ] **Step 1: Write the ALU test**

The reference in it is the Milestone 3 ALU, copied verbatim.

`tests/pinheck/p8x32a/alu_test.c`:

```c
/* The core's ALUs (alu, alu_run, bitrev) against the Milestone 3 ALU, kept here verbatim as the reference. */
#include "p8x32a.c"
#include <stdlib.h>

static uint32_t ref_bitrev(uint32_t x)
{
	uint32_t r = 0;
	int k;
	for (k = 0; k < 32; k++) r |= ((x >> k) & 1u) << (31 - k);
	return r;
}

static uint32_t ref_alu(unsigned i, uint32_t s, uint32_t d, unsigned pc, int run, int ci, int zi,
                    uint32_t bus_q, int bus_c, int *wr, int *co, int *zo)
{
	uint32_t r, dr = ref_bitrev(d), rot_r, lo, fill, log_r, add_d, add_s, add_r, sum_lo;
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
	rot_r = (((i >> 1) & 3) != 3 && (i & 1)) ? ref_bitrev((uint32_t)rot) : (uint32_t)rot;
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

static uint32_t rnd_state = 12345;

static uint32_t rnd(void)
{
	rnd_state ^= rnd_state << 13;
	rnd_state ^= rnd_state >> 17;
	rnd_state ^= rnd_state << 5;
	return rnd_state;
}

static uint32_t operand(void)
{
	static const uint32_t edge[] = { 0, 1, 2, 31, 32, 511, 0x55555555u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xAAAAAAAAu, 0xFFFFFFFEu, 0xFFFFFFFFu };
	switch (rnd() % 4) {
	case 0: return edge[rnd() % (sizeof(edge) / sizeof(edge[0]))];
	case 1: return rnd() & 0x3F;
	default: return rnd();
	}
}

int main(int argc, char **argv)
{
	unsigned long n = argc > 1 ? strtoul(argv[1], NULL, 0) : 4000000, k, bad = 0;
	for (k = 0; k < n; k++) {
		unsigned i = (unsigned)(k & 63), pc = rnd() & 511;
		uint32_t s = operand(), d = operand(), q = rnd(), r1, r2;
		int run = (int)(rnd() & 1), ci = (int)(rnd() & 1), zi = (int)(rnd() & 1), bc = (int)(rnd() & 1);
		int w1, c1, z1, w2, c2, z2;
		if (ref_bitrev(d) != bitrev(d) && bad++ < 10) printf("ALU FAIL: bitrev %08x\n", (unsigned)d);
		r1 = ref_alu(i, s, d, pc, run, ci, zi, q, bc, &w1, &c1, &z1);
		r2 = alu(i, s, d, pc, run, ci, zi, q, bc, &w2, &c2, &z2);
		if ((r1 != r2 || w1 != w2 || c1 != c2 || z1 != z2) && bad++ < 10)
			printf("ALU FAIL: alu op %02x s %08x d %08x pc %03x run %d c %d z %d: ref %08x w%d c%d z%d, got %08x w%d c%d z%d\n",
			       i, (unsigned)s, (unsigned)d, pc, run, ci, zi, (unsigned)r1, w1, c1, z1, (unsigned)r2, w2, c2, z2);
		/* alu_run: a running cog's non-hub, non-wait ops, as the core calls it (no hub result) */
		if (i < 4 || i >= 0x3C) continue;
		r1 = ref_alu(i, s, d, pc, 1, ci, zi, 0, bc, &w1, &c1, &z1);
		r2 = alu_run(i, s, d, pc, ci, zi, bc, &w2, &c2, &z2);
		if ((r1 != r2 || w1 != w2 || c1 != c2 || z1 != z2) && bad++ < 10)
			printf("ALU FAIL: alu_run op %02x s %08x d %08x pc %03x c %d z %d: ref %08x w%d c%d z%d, got %08x w%d c%d z%d\n",
			       i, (unsigned)s, (unsigned)d, pc, ci, zi, (unsigned)r1, w1, c1, z1, (unsigned)r2, w2, c2, z2);
	}
	printf("alu: %lu cases, %lu mismatches\n", n, bad);
	return bad != 0;
}
```

- [ ] **Step 2: Confirm it fails before the fast ALU**

Run: `cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -Isrc/cpu/p8x32a -o build/alu_test tests/pinheck/p8x32a/alu_test.c 2>&1 | grep -m1 alu_run`
Expected:
```text
tests/pinheck/p8x32a/alu_test.c:136:22: error: implicit declaration of function ‘alu_run’ [-Wimplicit-function-declaration]
```

- [ ] **Step 3: The fast paths**

The ALU evaluates only the unit whose result it returns, `alu_run` handles the common ops directly, `bitrev` is five swap steps, `INA` uses the flushed pins, the next counter change is cached, `p8x32a_pins` skips cogs that drive nothing, and local instructions run in `run_local`. `prop.c` converts each queued PIC32 edge to Propeller time once (and again when a `CLKSET` or reset changes the clock segments) instead of on every `INA`.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.c', [
(r'''static uint64_t ctr_next(const p8x32a *p, uint64_t t)
{
	uint64_t nt = P8X32A_NEVER, e;
	int n, k;
	for (n = 0; n < 8; n++)
		for (k = 0; k < 2; k++)
			if ((e = ctr_toggle(&p->cog[n], k, t)) < nt) nt = e;
''',
 r'''/* the next counter pin change after t; cached until a counter changes or t reaches it */
static uint64_t ctr_next(p8x32a *p, uint64_t t)
{
	uint64_t nt = P8X32A_NEVER, e;
	int n, k;
	if (p->ctr_ok && t >= p->ctr_from && t < p->ctr_nt) return p->ctr_nt;
	for (n = 0; n < 8; n++)
		for (k = 0; k < 2; k++)
			if ((e = ctr_toggle(&p->cog[n], k, t)) < nt) nt = e;
	p->ctr_ok = 1;
	p->ctr_from = t;
	p->ctr_nt = nt;
'''),
(r'''		uint32_t dd = regval(&c->dira, t);
		d |= dd;
''',
 r'''		uint32_t dd = regval(&c->dira, t);
		if (!dd) continue;
		d |= dd;
'''),
(r'''static uint32_t ina(p8x32a *p, uint64_t t)
{
	uint32_t dir, out, ext;
	flush(p, t);
	out = p8x32a_pins(p, t, &dir);
	ext = p->bus.pins_in ? p->bus.pins_in(p->bus.ctx, t) : 0;
	return (dir & out) | (~dir & ext);
''',
 r'''/* after flush(t) the pins at t are the last flushed state */
static uint32_t ina(p8x32a *p, uint64_t t)
{
	uint32_t ext;
	flush(p, t);
	ext = p->bus.pins_in ? p->bus.pins_in(p->bus.ctx, t) : 0;
	return (p->last_dir & p->last_out) | (~p->last_dir & ext);
'''),
(r'''	flush(p, p->now);
	c->ctr_old[k] = c->ctr[k];
''',
 r'''	flush(p, p->now);
	p->ctr_ok = 0;
	c->ctr_old[k] = c->ctr[k];
'''),
(r'''	uint32_t r = 0;
	int k;
	for (k = 0; k < 32; k++) r |= ((x >> k) & 1u) << (31 - k);
	return r;
''',
 r'''	x = (x >> 1 & 0x55555555u) | (x & 0x55555555u) << 1;
	x = (x >> 2 & 0x33333333u) | (x & 0x33333333u) << 2;
	x = (x >> 4 & 0x0F0F0F0Fu) | (x & 0x0F0F0F0Fu) << 4;
	x = (x >> 8 & 0x00FF00FFu) | (x & 0x00FF00FFu) << 8;
	return x >> 16 | x << 16;
'''),
(r'''static uint32_t alu(unsigned i, uint32_t s, uint32_t d, unsigned pc, int run, int ci, int zi,
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
''',
 r'''/* the P1 ALU: hub ops (group 0), rotates (1), logic (2-3) and adder (4-7) results with their C and write flags */
static uint32_t alu(unsigned i, uint32_t s, uint32_t d, unsigned pc, int run, int ci, int zi,
                    uint32_t bus_q, int bus_c, int *wr, int *co, int *zo)
{
	unsigned g = (i >> 3) & 7;
	uint32_t r, log_r = s;

	*wr = 1;
	if (g == 0) {
		r = (run || (pc >> 4) != 31) ? bus_q : 0;
		*co = bus_c;
	} else if (g == 1) {
		uint32_t dr = (i & 1) ? bitrev(d) : 0, fill;
		uint64_t rot;
		switch (i & 7) {
		case 0: fill = d & 0x7FFFFFFFu; break;
		case 1: fill = dr & 0x7FFFFFFFu; break;
		case 4: case 5: fill = ci ? 0x7FFFFFFFu : 0; break;
		case 6: fill = (d >> 31) ? 0x7FFFFFFFu : 0; break;
		default: fill = 0; break;
		}
		rot = (((uint64_t)fill << 32) | ((i & 1) ? dr : d)) >> (s & 31);
		r = (((i >> 1) & 3) != 3 && (i & 1)) ? bitrev((uint32_t)rot) : (uint32_t)rot;
		*co = (((i >> 1) & 3) != 3 && (i & 1)) ? (int)(dr & 1) : (int)(d & 1);
	} else {
		if (g <= 3) {
			if (i & 8) {
				unsigned ls = (i & 4) ? ((unsigned)(((i & 2) ? zi : ci) ^ (int)(i & 1))) << 1
				                      : (((i >> 1) & 1) << 1) | (unsigned)!(((i >> 1) ^ i) & 1);
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
			}
		}
		if (g == 3) {
			r = log_r;
			*co = parity(log_r);
		} else {
			uint32_t add_d, add_s, sum_lo, add_r;
			int add_sub, add_ci, add_co, add_cm, add_cs, add_c, cin, c30, b31;
			if (g == 4 || g == 5) {
				int ads[4];
				ads[0] = 0; ads[1] = (int)(s >> 31); ads[2] = ci; ads[3] = zi;
				add_sub = ads[(i >> 1) & 3] ^ (int)(i & 1);
			} else if (i == 0x32 || i == 0x34 || i == 0x36 || (i >> 2) == 0xF)
				add_sub = 0;
			else
				add_sub = 1;
			add_ci = ((g == 6 && ((i & 7) == 1 || (i & 2))) && ci) || (((i >> 3) & 3) == 3 && (i & 3) == 1);
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
			else if (g == 5) add_c = (int)(s >> 31);
			else if ((i & 0x20) && ((i >> 2) & 3) == 1) add_c = add_co ^ add_cm;
			else if (((i >> 1) & 15) == 8) add_c = add_cs;
			else add_c = add_co ^ add_sub;
			if ((i >> 2) == 4) *wr = (int)(i & 1) ^ ((i & 2) ? !add_co : add_cs);
			else if (i == 0x38) *wr = add_co;
			r = (i & 0x20) ? add_r : log_r;
			*co = add_c;
		}
	}
	*zo = !r && (zi || !(g == 6 && ((i & 7) == 1 || (i & 2))));
	return r;
}

static uint32_t ror32(uint32_t x, unsigned n) { return n ? x >> n | x << (32 - n) : x; }

/* the common ALU ops of a running cog (not hub ops); the others go to alu() */
static uint32_t alu_run(unsigned i, uint32_t s, uint32_t d, unsigned pc, int ci, int zi, int bus_c, int *wr, int *co, int *zo)
{
	uint32_t r;
	unsigned sh = s & 31;
	*wr = 1;
	switch (i) {
	case 0x08: r = ror32(d, sh); *co = (int)(d & 1); break;
	case 0x09: r = ror32(d, (32 - sh) & 31); *co = (int)(d >> 31); break;
	case 0x0A: r = d >> sh; *co = (int)(d & 1); break;
	case 0x0B: r = d << sh; *co = (int)(d >> 31); break;
	case 0x14: r = (d & 0xFFFFFE00u) | (s & 511); *co = d < s; break;
	case 0x15: r = (d & 0xFFFC01FFu) | ((s & 511) << 9); *co = d < s; break;
	case 0x17: r = (d & 0xFFFFFE00u) | (pc & 511); *co = d < s; break;
	case 0x18: r = d & s; *co = parity(r); break;
	case 0x19: r = d & ~s; *co = parity(r); break;
	case 0x1A: r = d | s; *co = parity(r); break;
	case 0x1B: r = d ^ s; *co = parity(r); break;
	case 0x1C: r = ci ? d | s : d & ~s; *co = parity(r); break;
	case 0x1D: r = ci ? d & ~s : d | s; *co = parity(r); break;
	case 0x1E: r = zi ? d | s : d & ~s; *co = parity(r); break;
	case 0x1F: r = zi ? d & ~s : d | s; *co = parity(r); break;
	case 0x20: r = d + s; *co = r < d; break;
	case 0x21: r = d - s; *co = d < s; break;
	case 0x28: r = s; *co = (int)(s >> 31); break;
	case 0x39: r = d - 1; *co = !d; break;
	default: return alu(i, s, d, pc, 1, ci, zi, 0, bus_c, wr, co, zo);
	}
	*zo = !r;
'''),
(r'''
void p8x32a_run_until(p8x32a *p, uint64_t t)
''',
 r'''
/* an instruction that neither reads nor writes anything another cog or the pins can see */
static int local(const p8x32a_cog *c)
{
	uint32_t i = c->ix;
	unsigned op = OP(i);
	if (!c->run || op <= 3 || op >= 0x3C) return 0;
	if (!FIM(i) && SRC(i) == 0x1F2) return 0;
	return !(FWR(i) && DST(i) >= 0x1F0);
}

/* exec + complete for local instructions of cog n, kept in locals, until one is not local or t is passed */
static void run_local(p8x32a *p, int n, uint64_t t)
{
	p8x32a_cog *c = &p->cog[n];
	uint64_t t2 = c->ev_t;
	uint32_t ix = c->ix;
	unsigned pc = c->p;
	int cf = c->c, zf = c->z, cancel = c->cancel, idled = 0;

	while (t2 <= t) {
		uint32_t i = ix, s, d, r, nix;
		unsigned op = OP(i), px, a;
		int cond, jump, wr, co, zo, jc = 0;
		if (op <= 3 || op >= 0x3C || (FWR(i) && DST(i) >= 0x1F0)) break;
		if (FIM(i)) s = SRC(i);
		else if ((a = SRC(i)) < 0x1F0) s = c->ram[a];
		else if (a == 0x1F2) break;
		else s = sread(p, n, a, t2);
		cond = ((COND(i) >> ((cf << 1) | zf)) & 1) && !cancel;
		d = c->ram[DST(i)];
		jump = op == 0x17 || (op >= 0x39 && op <= 0x3B);
		px = (cond && jump) ? (s & 511) : pc;
		nix = c->ram[px];
		if (t2 + 1 >= c->disable_at) { idled = 1; break; }
		r = alu_run(op, s, d, pc, cf, zf, p->sys_c, &wr, &co, &zo);
		if (cond) {
			if (FWR(i) && wr) c->ram[DST(i)] = r;
			if (FWC(i)) cf = co;
			if (FWZ(i)) zf = zo;
			if (op == 0x39) jc = !(d >> 1) && (d & 1);
			else if (op == 0x3A) jc = !(d >> 1) && !(d & 1);
			else if (op == 0x3B) jc = !(!(d >> 1) && !(d & 1));
		}
		if (!jc) pc = (px + 1) & 511;
		cancel = jc || px == 511;
		ix = nix;
		t2 += 4;
		if (t2 - 2 >= c->disable_at) { idled = 2; break; }
	}
	c->p = (uint16_t)pc;
	c->ix = ix;
	c->c = (uint8_t)cf;
	c->z = (uint8_t)zf;
	c->cancel = (uint8_t)cancel;
	c->t0 = t2 - 2;
	c->ev_t = t2;
	c->ev = EV_EXEC;
	if (idled) idle(p, n);
}

void p8x32a_run_until(p8x32a *p, uint64_t t)
'''),
(r'''			break;
		}
	}
	flush(p, t);
''',
 r'''			break;
		}
		/* local instructions run on ahead of the other cogs: their order against them is unobservable */
		if (b->ev == EV_EXEC && b->ev_t <= t && local(b)) run_local(p, best, t);
	}
	flush(p, t);
'''),
(r'''	p->npend = 0;
	p->flushed = t;
''',
 r'''	p->npend = 0;
	p->ctr_ok = 0;
	p->flushed = t;
'''),
])
edit('src/cpu/p8x32a/p8x32a.h', [
(r'''	int stop;
} p8x32a;
''',
 r'''	int stop;
	int ctr_ok;
	uint64_t ctr_from, ctr_nt;
} p8x32a;
'''),
])
edit('src/wpc/pinheck/prop.c', [
(r'''
static void add_seg(pinheck_prop *p, uint64_t t, uint8_t cfg)
''',
 r'''
/* the clock segments changed: convert the queued edges again */
static void retime(pinheck_prop *p)
{
	int k;
	for (k = 0; k < p->count; k++) {
		prop_edge *e = &p->edge[(p->head + k) % PROP_EDGES];
		e->prop = to_prop(p, e->pic);
	}
}

static void add_seg(pinheck_prop *p, uint64_t t, uint8_t cfg)
'''),
(r'''	p->nseg++;
}
''',
 r'''	p->nseg++;
	retime(p);
}
'''),
(r'''		if (to_prop(p, e->pic) > t) break;
''',
 r'''		if (e->prop > t) break;
'''),
(r'''		uint64_t e = to_prop(p, p->edge[(p->head + k) % PROP_EDGES].pic);
''',
 r'''		uint64_t e = p->edge[(p->head + k) % PROP_EDGES].prop;
'''),
(r'''	p->reset_pending = 0;
}
''',
 r'''	p->reset_pending = 0;
	retime(p);
}
'''),
(r'''	p->seg[0].pic0 = pic_cycle;
}
''',
 r'''	p->seg[0].pic0 = pic_cycle;
	retime(p);
}
'''),
(r'''	while (p->count && to_prop(p, p->edge[p->head].pic) <= p->chip.now) {
''',
 r'''	while (p->count && p->edge[p->head].prop <= p->chip.now) {
'''),
(r'''	p->edge[(p->head + p->count) % PROP_EDGES].pins = pins;
	p->count++;
''',
 r'''	p->edge[(p->head + p->count) % PROP_EDGES].pins = pins;
	p->edge[(p->head + p->count) % PROP_EDGES].prop = to_prop(p, pic_cycle);
	p->count++;
'''),
])
edit('src/wpc/pinheck/prop.h', [
(r'''typedef struct prop_edge { uint64_t pic; uint32_t pins; } prop_edge;
''',
 r'''typedef struct prop_edge { uint64_t pic, prop; uint32_t pins; } prop_edge; /* prop = pic in Propeller cycles */
'''),
])
edit('tests/pinheck/p8x32a/check.sh', [
(r'''	./$B/dasm_test || fail=$((fail + 1))
fi
''',
 r'''	./$B/dasm_test || fail=$((fail + 1))
fi
if [ -f alu_test.c ]; then
	$CC -I$CORE -o $B/alu_test alu_test.c || exit 2
	./$B/alu_test || fail=$((fail + 1))
fi
'''),
])
EOF
```

- [ ] **Step 4: The core suites pass**

Run: `tests/pinheck/p8x32a/check.sh | grep -v ' match' | tail -4; for s in link display audio; do tests/pinheck/$s/check.sh | tail -1; done` (about 15 minutes)
Expected:
```text
eeprom: ok
dasm: 31/31
alu: 4000000 cases, 0 mismatches
p8x32a: 215 passed, 0 failed
link: 0 failed
display: 0 failed
audio: 0 failed
```

- [ ] **Step 5: Rebuild and compare with the reference**

```sh
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m9.log 2>&1; tail -1 build/sdl3pinmame/m9.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame timeout 9000 tests/pinheck/perf/bench.sh attract video
```
Expected (about 20 minutes; speeds vary with the load):
```text
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.174x over 30.0 s emulated (172.8 s wall), whole run 43.2 G instructions and 5.3 s CPU per emulated s, load 10.02
bench attract ref: 0.095x over 30.0 s emulated (316.7 s wall), whole run 111.4 G instructions and 10.3 s CPU per emulated s, load 6.36
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 0.204x over 24.0 s emulated (117.8 s wall), whole run 44.6 G instructions and 5.4 s CPU per emulated s, load 8.77
bench video ref: 0.072x over 24.0 s emulated (335.0 s wall), whole run 115.1 G instructions and 13.7 s CPU per emulated s, load 8.24
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32a.h src/wpc/pinheck/prop.c src/wpc/pinheck/prop.h tests/pinheck/p8x32a/alu_test.c tests/pinheck/p8x32a/check.sh
git commit -m "p8x32a: ALU by unit, INA from flushed pins, cached counter changes, local instructions run ahead"
```

### Task 3: Idle loops sleep (stage 2b)

**Files:**
- Create: `tests/pinheck/p8x32a/chip/idle_ina.spin`, `tests/pinheck/p8x32a/chip/idle_hub.spin`, `tests/pinheck/p8x32a/chip/idle_ctr.spin`
- Modify: `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32a.h`, `src/wpc/pinheck/prop.c`, `tests/pinheck/p8x32a/run.c`, `tests/pinheck/p8x32a/check.sh`

**Interfaces:**
- Consumes: Task 2's `run_local`, `local`, `alu_run`, the pending pin-change list (`add_pending`) and `ctr_toggle`.
- Produces: `p8x32a_bus.pure_in` (last field: input pins that change only at `pins_next` edges; 0 = none, the safe default for existing initialisers), `p8x32a.sleeps` (idle loops entered), `p8x32a_loop` and `P8X32A_PAT` (32 recorded events); `p8run -sleeps` prints `p8run: <n> idle-loop sleeps` on stderr; `check.sh` reads `' EXPECT-SLEEPS: <n>` from a chip test and requires that line.

The tests run a cog's polling loops against the RTL: `idle_ina` waits for external edges on P7 (a 2-instruction loop, a 3-instruction loop that also writes a hub long its worker overwrites, and one while the worker toggles P2 and runs an NCO on P3), then for P5, which the worker drives; `idle_hub` has a worker poll a hub long (written in one byte), then poll through a `CALL` while writing a long cog 0 overwrites, is stopped and restarted while it sleeps; `idle_ctr` polls a pin its own NCO counter drives.

- [ ] **Step 1: Write the idle-loop tests**

`tests/pinheck/p8x32a/chip/idle_ina.spin`:

```text
' ARGS: -extat 30000 80 -extat 45001 0 -extat 60002 80
' EXPECT-SLEEPS: 5
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        mov     y, #$61
        shl     y, #24
        or      x, y
        coginit x
:a      test    p7, ina wz
  if_z  jmp     #:a
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:b      wrlong  zero, scratch
        test    p7, ina wz
  if_nz jmp     #:b
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:c      test    p7, ina wz
  if_z  jmp     #:c
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:d      test    p5, ina wc
  if_nc jmp     #:d
        mov     t, cnt
        wrlong  t, ptr
        cogid   t
        cogstop t
p7      long    %10000000
p5      long    %100000
zero    long    0
scratch long    $6200
t       long    0
x       long    0
y       long    0
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, pins
        movi    ctra, #%00100_000
        movs    ctra, #3
        mov     frqa, frq
        mov     w, cnt
        add     w, d20000
        mov     n, #5
:tog    waitcnt w, d700
        xor     outa, #%100
        djnz    n, #:tog
        wrlong  one, scr
        mov     n, #10
:tog2   waitcnt w, d700
        xor     outa, #%100
        djnz    n, #:tog2
        add     w, d20000
        waitcnt w, #0
        or      outa, #%100000
        mov     w, #200
        add     w, cnt
        waitcnt w, #0
        cogid   n
        cogstop n
pins    long    %100100
frq     long    $1000
d700    long    700
d20000  long    20000
one     long    1
scr     long    $6200
w       long    0
n       long    0
        long    $C0DEE0D0
```

`tests/pinheck/p8x32a/chip/idle_hub.spin`:

```text
' EXPECT-SLEEPS: 5
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        mov     y0, #$61
        shl     y0, #24
        or      y0, x
        mov     y, y0
        coginit y wc, wr
        mov     t, cnt
        add     t, d20000
        waitcnt t, d1500
        wrlong  zero, flag
        waitcnt t, d1500
        wrbyte  one, flag3
        waitcnt t, d3000
        wrlong  zero, flag
        wrlong  zero, mark
        waitcnt t, d3000
        cogstop y
        waitcnt t, #500
        andn    y0, #%1111
        or      y0, y
        coginit y0
        waitcnt t, d12000
        waitcnt t, d12000
        coginit y0
        waitcnt t, d12000
        waitcnt t, d3000
        wrlong  two, flag
        waitcnt t, d3000
        cogid   t
        cogstop t
zero    long    0
d20000  long    20000
d12000  long    12000
d1500   long    1500
d3000   long    3000
one     long    1
two     long    2
flag    long    $6100
flag3   long    $6103
mark    long    $6108
t       long    0
x       long    0
y       long    0
y0      long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     a, par
        add     a, #4
        mov     m, par
        add     m, #8
:w1     rdlong  v, par wz
  if_z  jmp     #:w1
        mov     v, cnt
        wrlong  v, a
        add     a, #4
        add     a, #4
:w2     call    #chk
        wrlong  five, m
  if_nz jmp     #:w2
        mov     v, cnt
        wrlong  v, a
        cogid   v
        cogstop v
chk     rdlong  v, par
        cmp     v, #2 wz
chk_ret ret
a       long    0
m       long    0
v       long    0
five    long    5
        long    $C0DEE0D0
```

`tests/pinheck/p8x32a/chip/idle_ctr.spin`:

```text
' EXPECT-SLEEPS: 8
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     dira, p4
        movi    ctra, #%00100_000
        movs    ctra, #4
        mov     frqa, frq
        mov     n, #4
:hi     test    p4, ina wz
  if_z  jmp     #:hi
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:lo     test    p4, ina wz
  if_nz jmp     #:lo
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
        djnz    n, #:hi
        mov     frqa, #0
        cogid   t
        cogstop t
p4      long    %10000
frq     long    $00100000
t       long    0
n       long    0
ptr     long    $6000
        long    $C0DEE0D0
```

- [ ] **Step 2: Confirm they fail before the idle-loop core**

Add the `EXPECT-SLEEPS` check to `check.sh` first; Task 2's `p8run` does not know `-sleeps` yet:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/p8x32a/check.sh', [
(r'''	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 -dump "$o.ourhub" > "$o.our" 2> "$o.log"
	if cmp -s "$o.rtl" "$o.our" && cmp -s "$o.rtlhub" "$o.ourhub"; then pass=$((pass + 1))
	else echo "RTL MISMATCH $1"; diff "$o.rtl" "$o.our" | head -6; fail=$((fail + 1)); fi
	exp=$(sed -n "s/^' EXPECT-LOG: //p" "$1")
	if [ -n "$exp" ] && ! grep -qF "$exp" "$o.log"; then echo "LOG MISSING $1: $exp"; fail=$((fail + 1)); fi
''',
 r'''	sleeps=$(sed -n "s/^' EXPECT-SLEEPS: //p" "$1")
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 ${sleeps:+-sleeps} -dump "$o.ourhub" > "$o.our" 2> "$o.log"
	if cmp -s "$o.rtl" "$o.our" && cmp -s "$o.rtlhub" "$o.ourhub"; then pass=$((pass + 1))
	else echo "RTL MISMATCH $1"; diff "$o.rtl" "$o.our" | head -6; fail=$((fail + 1)); fi
	exp=$(sed -n "s/^' EXPECT-LOG: //p" "$1")
	if [ -n "$exp" ] && ! grep -qF "$exp" "$o.log"; then echo "LOG MISSING $1: $exp"; fail=$((fail + 1)); fi
	if [ -n "$sleeps" ] && ! grep -qxF "p8run: $sleeps idle-loop sleeps" "$o.log"; then echo "SLEEPS $1: $(grep -F idle-loop "$o.log"), expected $sleeps"; fail=$((fail + 1)); fi
'''),
])
EOF
```

Run: `tests/pinheck/p8x32a/check.sh | grep -a 'SLEEPS\|p8x32a:'`
Expected (each idle test also reports `RTL MISMATCH`, as `p8run` rejects `-sleeps`):
```text
SLEEPS chip/idle_ctr.spin: , expected 8
SLEEPS chip/idle_hub.spin: , expected 5
SLEEPS chip/idle_ina.spin: , expected 5
p8x32a: 215 passed, 6 failed
```

- [ ] **Step 3: The idle-loop core**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.c', [
(r'''enum { EV_NONE, EV_HUB, EV_EXEC, EV_DONE, EV_WAITPIN, EV_RESTART };
''',
 r'''enum { EV_NONE, EV_HUB, EV_EXEC, EV_DONE, EV_WAITPIN, EV_RESTART, EV_SLEEP };
enum { LOOP_SEARCH, LOOP_RECORD, LOOP_SLEEP };
'''),
(r'''static void add_pending(p8x32a *p, uint64_t t)
{
	int k;
	for (k = 0; k < p->npend; k++)
		if (p->pend[k] == t) return;
	if (p->npend < (int)(sizeof(p->pend) / sizeof(p->pend[0]))) p->pend[p->npend++] = t;
''',
 r'''static void loop_notify(p8x32a *p, uint64_t t, uint32_t pins);

/* a pin change at t; pins = the pins it may change */
static void add_pending(p8x32a *p, uint64_t t, uint32_t pins)
{
	int k;
	if (p->sleepers) loop_notify(p, t, pins);
	for (k = 0; k < p->npend; k++)
		if (p->pend[k] == t) { p->pend_pins[k] |= pins; return; }
	if (p->npend < (int)(sizeof(p->pend) / sizeof(p->pend[0]))) { p->pend_pins[p->npend] = pins; p->pend[p->npend++] = t; }
'''),
(r'''	flush(p, p->now);
	if (at > r->at) r->prev = r->cur;
	r->cur = v;
	r->at = at;
	add_pending(p, at);
''',
 r'''	uint32_t pins = (r->cur ^ v) | (r->prev ^ v);
	flush(p, p->now);
	if (at > r->at) r->prev = r->cur;
	r->cur = v;
	r->at = at;
	add_pending(p, at, pins);
'''),
(r'''	return m == 4 || m == 5;
}
''',
 r'''	return m == 4 || m == 5;
}

static uint32_t nco_pins(uint32_t ctr)
{
	if (!nco(ctr)) return 0;
	return 1u << (ctr & 31) | (((ctr >> 26) & 31) == 5 ? 1u << ((ctr >> 9) & 31) : 0);
}
'''),
(r'''			if (p->pend[k] == when) { p->pend[k] = p->pend[--p->npend]; k--; }
''',
 r'''			if (p->pend[k] == when) { --p->npend; p->pend[k] = p->pend[p->npend]; p->pend_pins[k] = p->pend_pins[p->npend]; k--; }
'''),
(r'''	p->ctr_ok = 0;
	c->ctr_old[k] = c->ctr[k];
''',
 r'''	p->ctr_ok = 0;
	add_pending(p, e, nco_pins(c->ctr[k]));
	c->ctr_old[k] = c->ctr[k];
'''),
(r'''	add_pending(p, e);
''',
 r''''''),
(r'''	case 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; ctr_check(p, v); ctr_notify(p, n, k, e); break;
''',
 r'''	case 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; add_pending(p, e, nco_pins(v)); ctr_check(p, v); ctr_notify(p, n, k, e); break;
'''),
(r'''
static uint32_t ror32(uint32_t x, unsigned n) { return n ? x >> n | x << (32 - n) : x; }
''',
 r'''
/* Idle loops: an iteration that changes nothing and ends in the state it began in repeats while its inputs
   stay the same; the cog sleeps, and the recorded iteration gives its state when an input may have changed. */
#define SNAP(c) ((unsigned char *)(c) + offsetof(p8x32a_cog, ptr))

static uint32_t ror32(uint32_t x, unsigned n) { return n ? x >> n | x << (32 - n) : x; }
'''),
(r'''
static void idle(p8x32a *p, int n)
''',
 r'''
static void loop_reset(p8x32a_loop *l)
{
	l->state = LOOP_SEARCH;
	l->edge = l->dirty = l->hub = 0;
	l->head = 0xFFFF;
	l->nins = 0;
}

/* a pin change at t reaches sleepers whose inputs it may touch */
static void loop_notify(p8x32a *p, uint64_t t, uint32_t pins)
{
	int n;
	for (n = 0; n < 8; n++)
		if ((p->sleepers >> n & 1) && (pins & p->loop[n].wake) && t < p->cog[n].ev_t) p->cog[n].ev_t = t;
}

static void loop_hub_write(p8x32a *p, int writer, unsigned a, unsigned sz, uint64_t h)
{
	int n, k;
	for (n = 0; n < 8; n++) {
		p8x32a_loop *l = &p->loop[n];
		if (!(p->sleepers >> n & 1) || n == writer) continue;
		for (k = 0; k < l->nhub; k++)
			if (a < (unsigned)l->hub_a[k] + l->hub_n[k] && l->hub_a[k] < a + sz && h < p->cog[n].ev_t) p->cog[n].ev_t = h;
	}
}

static void loop_hub_access(p8x32a *p, int n, uint32_t a, unsigned op)
{
	p8x32a_loop *l = &p->loop[n];
	unsigned sz = op == 2 ? 4 : op == 1 ? 2 : 1;
	if (l->nhub == 4) { l->dirty = 1; return; }
	l->hub_a[l->nhub] = (uint16_t)(a & ~(sz - 1));
	l->hub_n[l->nhub] = (uint8_t)sz;
	l->hub_v[l->nhub] = sz == 4 ? rd32(p, a) : sz == 2 ? (uint32_t)(p->hub[a & ~1u] | p->hub[(a & ~1u) + 1] << 8) : p->hub[a];
	l->nhub++;
	l->hub = 1;
}

static uint32_t loop_hub_now(const p8x32a *p, const p8x32a_loop *l, int k)
{
	unsigned a = l->hub_a[k];
	return l->hub_n[k] == 4 ? rd32(p, a) : l->hub_n[k] == 2 ? (uint32_t)(p->hub[a] | p->hub[a + 1] << 8) : p->hub[a];
}

/* do the sleeper's inputs at t still read as recorded? */
static int loop_same(p8x32a *p, int n, uint64_t t)
{
	p8x32a_loop *l = &p->loop[n];
	int k;
	if (l->nin) {
		uint32_t v = ina(p, t);
		for (k = 0; k < l->nin; k++)
			if ((v & l->in_mask[k]) != l->in_val[k]) return 0;
	}
	for (k = 0; k < l->nhub; k++)
		if (loop_hub_now(p, l, k) != l->hub_v[k]) return 0;
	return 1;
}

/* the earliest time after t at which a pin the sleeper reads may change, or the horizon */
static uint64_t loop_bound(p8x32a *p, int n, uint64_t t)
{
	uint32_t m = p->loop[n].wake;
	uint64_t w = p->horizon == P8X32A_NEVER ? P8X32A_NEVER : p->horizon + 1, e;
	int k, j;
	if (m) {
		for (k = 0; k < p->npend; k++)
			if (p->pend[k] > t && p->pend[k] < w && (p->pend_pins[k] & m)) w = p->pend[k];
		for (k = 0; k < 8; k++)
			for (j = 0; j < 2; j++) {
				const p8x32a_cog *c = &p->cog[k];
				if (!((nco_pins(c->ctr[j]) | (t < c->ctr_at[j] ? nco_pins(c->ctr_old[j]) : 0)) & m)) continue;
				if ((e = ctr_toggle(c, j, t)) > t && e < w) w = e;
			}
		if (p->bus.pins_next && (e = p->bus.pins_next(p->bus.ctx, t)) < w) w = e;
	}
	return w > t ? w : t + 1;
}

/* wake a sleeper at w: restore its state before its first event at or after w */
static void loop_resume(p8x32a *p, int n, uint64_t w)
{
	p8x32a_cog *c = &p->cog[n];
	p8x32a_loop *l = &p->loop[n];
	uint64_t rel = w > l->t0 ? w - l->t0 : 0, k = rel / l->period, off = rel % l->period, shift;
	int j = 0;
	while (j < l->nsnap && l->snap_t[j] - l->snap_t[0] < off) j++;
	if (j == l->nsnap) { j = 0; k++; }
	shift = l->t0 - l->snap_t[0] + k * l->period;
	memcpy(SNAP(c), l->snap[j], P8X32A_SNAP);
	c->ev_t += shift;
	c->t0 += shift;
	if (l->hub) c->latch += shift;
	p->sleepers &= (uint8_t)~(1u << n);
	loop_reset(l);
}

static void loop_wake(p8x32a *p, int n)
{
	uint64_t t = p->cog[n].ev_t;
	if (loop_same(p, n, t)) p->cog[n].ev_t = loop_bound(p, n, t);
	else loop_resume(p, n, t);
}

static void loop_in(p8x32a *p, int n, unsigned op, uint32_t s, uint32_t d)
{
	p8x32a_loop *l = &p->loop[n];
	uint32_t mask = (op == 0x18 || op == 0x19) ? d : 0xFFFFFFFFu;
	if (l->nin == 4) { l->dirty = 1; return; }
	l->in_mask[l->nin] = mask;
	l->in_val[l->nin++] = s & mask;
}

/* a taken backward jump to head at time t */
static void loop_edge(p8x32a *p, int n, unsigned head, uint64_t t)
{
	p8x32a_loop *l = &p->loop[n];
	if (l->state == LOOP_RECORD) {
		if (head == l->head || l->nins > P8X32A_PAT / 2) { l->edge = 1; l->edge_head = (uint16_t)head; l->edge_t = t; }
		return;
	}
	if (head == l->head && !l->dirty && l->nins <= P8X32A_PAT / 2 && t > l->head_t) {
		l->state = LOOP_RECORD;
		l->period = t - l->head_t;
		l->nins0 = l->nins;
		l->nsnap = l->nin = l->nhub = 0;
		l->edge = 0;
	} else if (head != l->head && !l->dirty && l->nins <= P8X32A_PAT / 2 && l->head != 0xFFFF)
		return;
	l->head = (uint16_t)head;
	l->head_t = t;
	l->dirty = l->hub = 0;
	l->nins = 0;
}

/* after each event of a recording cog: keep its state, or at the loop head decide whether it sleeps */
static void loop_post(p8x32a *p, int n)
{
	p8x32a_cog *c = &p->cog[n];
	p8x32a_loop *l = &p->loop[n];
	uint64_t per = l->period;
	uint32_t m = 0;
	int k, same;

	if (!l->edge) {
		if (l->dirty || l->nsnap == P8X32A_PAT || c->ev == EV_NONE || c->ev == EV_RESTART) { loop_reset(l); return; }
		l->snap_t[l->nsnap] = c->ev_t;
		memcpy(l->snap[l->nsnap++], SNAP(c), P8X32A_SNAP);
		return;
	}
	l->edge = 0;
	if (l->edge_head != l->head || l->dirty || l->edge_t - l->head_t != per || l->nins != l->nins0 ||
	    (l->hub && per % 16) || !l->nsnap || c->ev != EV_EXEC) {
		loop_reset(l);
		return;
	}
	c->ev_t -= per;
	c->t0 -= per;
	if (l->hub) c->latch -= per;
	same = !memcmp(SNAP(c), l->snap[0], P8X32A_SNAP);
	c->ev_t += per;
	c->t0 += per;
	if (l->hub) c->latch += per;
	if (!same || !loop_same(p, n, p->now)) { loop_reset(l); return; }
	for (k = 0; k < l->nin; k++) m |= l->in_mask[k];
	l->wake = (m & ~p->bus.pure_in) ? 0xFFFFFFFFu : m;
	l->t0 = c->ev_t;
	l->state = LOOP_SLEEP;
	p->sleepers |= (uint8_t)(1u << n);
	p->sleeps++;
	c->ev = EV_SLEEP;
	c->ev_t = loop_bound(p, n, p->now);
}

static void idle(p8x32a *p, int n)
'''),
(r'''			if (wr) c->ram[DST(i)] = r;
			if (DST(i) >= 0x1F0) special_write(p, n, DST(i), r, m3);
''',
 r'''			if (wr && c->ram[DST(i)] != r) { c->ram[DST(i)] = r; p->loop[n].dirty = 1; }
			if (DST(i) >= 0x1F0) { special_write(p, n, DST(i), r, m3); p->loop[n].dirty = 1; }
'''),
(r'''	}
	if (!jc) c->p = (uint16_t)((c->px + 1) & 511);
''',
 r'''	}
	p->loop[n].nins++;
	if (c->cond && !jc && (op == 0x17 || (op >= 0x39 && op <= 0x3B)) && c->px < c->p) loop_edge(p, n, c->px, m3);
	if (!jc) c->p = (uint16_t)((c->px + 1) & 511);
'''),
(r'''	bit = (uint8_t)(1u << num);
	switch (op) {
''',
 r'''	bit = (uint8_t)(1u << num);
	if (op == 2 || op == 3) {
		if (p->sleepers >> num & 1) loop_resume(p, (int)num, h);
		loop_reset(&p->loop[num]);
	}
	switch (op) {
'''),
(r'''			if (op == 2) { a &= 0xFFFC; p->hub[a] = (uint8_t)v; p->hub[a + 1] = (uint8_t)(v >> 8); p->hub[a + 2] = (uint8_t)(v >> 16); p->hub[a + 3] = (uint8_t)(v >> 24); }
			else if (op == 1) { a &= 0xFFFE; p->hub[a] = (uint8_t)v; p->hub[a + 1] = (uint8_t)(v >> 8); }
			else p->hub[a] = (uint8_t)v;
		}
		q = op == 2 ? w : op == 1 ? (w >> ((a & 2) * 8)) & 0xFFFF : (w >> ((a & 3) * 8)) & 0xFF;
''',
 r'''			unsigned sz = op == 2 ? 4 : op == 1 ? 2 : 1, ha = a & ~(sz - 1), k;
			int changed = 0;
			for (k = 0; k < sz; k++) {
				changed |= p->hub[ha + k] != (uint8_t)(v >> (8 * k));
				p->hub[ha + k] = (uint8_t)(v >> (8 * k));
			}
			if (changed) {
				p->loop[n].dirty = 1;
				if (p->sleepers) loop_hub_write(p, n, ha, sz, h);
			}
		}
		q = op == 2 ? w : op == 1 ? (w >> ((a & 2) * 8)) & 0xFFFF : (w >> ((a & 3) * 8)) & 0xFF;
		if (p->loop[n].state == LOOP_RECORD) loop_hub_access(p, n, a, op);
'''),
(r'''	c->d = c->ram[DST(i)];
	c->px = (uint16_t)((c->cond && jump) ? (c->s & 511) : c->p);
''',
 r'''	c->d = c->ram[DST(i)];
	if (!c->run || (c->cond && (op == 3 || op >= 0x3C)) || (!FIM(i) && (SRC(i) == 0x1F1 || SRC(i) == 0x1FC || SRC(i) == 0x1FD)))
		p->loop[n].dirty = 1;
	else if (!FIM(i) && SRC(i) == 0x1F2 && p->loop[n].state == LOOP_RECORD)
		loop_in(p, n, op, c->s, c->d);
	c->px = (uint16_t)((c->cond && jump) ? (c->s & 511) : c->p);
'''),
(r'''	p8x32a_cog *c = &p->cog[n];
	c->p = 0;
''',
 r'''	p8x32a_cog *c = &p->cog[n];
	loop_reset(&p->loop[n]);
	c->p = 0;
'''),
(r'''	p8x32a_cog *c = &p->cog[n];
	uint64_t t2 = c->ev_t;
''',
 r'''	p8x32a_cog *c = &p->cog[n];
	p8x32a_loop *l = &p->loop[n];
	uint64_t t2 = c->ev_t;
'''),
(r'''		else s = sread(p, n, a, t2);
''',
 r'''		else {
			s = sread(p, n, a, t2);
			if (a == 0x1F1 || a == 0x1FC || a == 0x1FD) l->dirty = 1;
		}
'''),
(r'''			if (FWR(i) && wr) c->ram[DST(i)] = r;
''',
 r'''			if (FWR(i) && wr && c->ram[DST(i)] != r) { c->ram[DST(i)] = r; l->dirty = 1; }
'''),
(r'''			else if (op == 0x3B) jc = !(!(d >> 1) && !(d & 1));
		}
''',
 r'''			else if (op == 0x3B) jc = !(!(d >> 1) && !(d & 1));
		}
		l->nins++;
		if (cond && !jc && jump && px < pc) {
			loop_edge(p, n, px, t2 + 1);
			if (l->state == LOOP_RECORD) {
				c->i = i; c->s = s; c->d = d; c->px = (uint16_t)px; c->nix = nix; c->cond = (uint8_t)cond;
				if (!jc) pc = (px + 1) & 511;
				cancel = jc || px == 511;
				ix = nix;
				t2 += 4;
				break;
			}
		}
'''),
(r'''		}
		/* local instructions run on ahead of the other cogs: their order against them is unobservable */
		if (b->ev == EV_EXEC && b->ev_t <= t && local(b)) run_local(p, best, t);
''',
 r'''		case EV_SLEEP: loop_wake(p, best); break;
		}
		if (p->loop[best].state == LOOP_RECORD) loop_post(p, best);
		/* local instructions run on ahead of the other cogs: their order against them is unobservable */
		while (b->ev == EV_EXEC && b->ev_t <= t && local(b)) {
			if (p->loop[best].state == LOOP_RECORD) exec(p, best);
			else run_local(p, best, t);
			if (p->loop[best].state == LOOP_RECORD) loop_post(p, best);
		}
'''),
(r'''	p->ctr_ok = 0;
	p->flushed = t;
''',
 r'''	p->ctr_ok = 0;
	p->sleepers = 0;
	for (n = 0; n < 8; n++) loop_reset(&p->loop[n]);
	p->flushed = t;
'''),
])
edit('src/cpu/p8x32a/p8x32a.h', [
(r'''
#include <stdint.h>
''',
 r'''
#include <stddef.h>
#include <stdint.h>
'''),
(r'''	void (*ctr_state)(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq); /* ctr: 0 = A, 1 = B; t = cycle the change takes effect */
} p8x32a_bus;
''',
 r'''	void (*ctr_state)(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq); /* ctr: 0 = A, 1 = B; t = cycle the change takes effect */
	uint32_t pure_in; /* input pins that change only at pins_next edges, never inside pins_out; 0 = none */
} p8x32a_bus;
'''),
(r'''
typedef struct p8x32a {
''',
 r'''
#define P8X32A_PAT 32
#define P8X32A_SNAP (sizeof(p8x32a_cog) - offsetof(p8x32a_cog, ptr))

/* a cog polling in a loop that changes nothing: one recorded iteration replays it while its inputs stay the same */
typedef struct p8x32a_loop {
	uint8_t state, edge, dirty, hub;
	uint16_t head, edge_head, nins, nins0;
	uint64_t head_t, edge_t, period, t0;
	int nsnap, nin, nhub;
	uint32_t in_mask[4], in_val[4], hub_v[4], wake;
	uint16_t hub_a[4];
	uint8_t hub_n[4];
	uint64_t snap_t[P8X32A_PAT];
	unsigned char snap[P8X32A_PAT][P8X32A_SNAP];
} p8x32a_loop;

typedef struct p8x32a {
'''),
(r'''	uint64_t pend[40];
	int npend;
''',
 r'''	uint64_t pend[40];
	uint32_t pend_pins[40];
	int npend;
'''),
(r'''	uint64_t ctr_from, ctr_nt;
} p8x32a;
''',
 r'''	uint64_t ctr_from, ctr_nt;
	uint8_t sleepers;
	uint64_t sleeps; /* idle loops entered */
	p8x32a_loop loop[8];
} p8x32a;
'''),
])
edit('src/wpc/pinheck/prop.c', [
(r'''	bus.ctr_state = ctr_state;
	p8x32a_init(&p->chip, &bus);
''',
 r'''	bus.ctr_state = ctr_state;
	bus.pure_in = ~(PIN_DO | PIN_SDA); /* SD DO and EEPROM SDA answer inside pins_out */
	p8x32a_init(&p->chip, &bus);
'''),
])
edit('tests/pinheck/p8x32a/run.c', [
(r'''static int have_ee, have_sd, notrace;
''',
 r'''static int have_ee, have_sd, notrace, sleeps;
'''),
(r'''	p8x32a_bus bus = { NULL, pins_in, pins_next, pins_out, cog_start, clkset, logmsg, ctr_state };
''',
 r'''	p8x32a_bus bus = { NULL, pins_in, pins_next, pins_out, cog_start, clkset, logmsg, ctr_state, ~0x30000001u };
'''),
(r'''		else if (!strcmp(argv[i], "-notrace")) notrace = 1;
		else if (!strcmp(argv[i], "-ctrlog") && i + 1 < argc) { if (!(ctrlog = fopen(argv[++i], "w"))) { perror(argv[i]); return 2; } }
''',
 r'''		else if (!strcmp(argv[i], "-notrace")) notrace = 1;
		else if (!strcmp(argv[i], "-sleeps")) sleeps = 1;
		else if (!strcmp(argv[i], "-ctrlog") && i + 1 < argc) { if (!(ctrlog = fopen(argv[++i], "w"))) { perror(argv[i]); return 2; } }
'''),
(r'''		else { fprintf(stderr, "usage: p8run -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-halt] [-quantum n] [-sd romset.zip] [-notrace] [-ctrlog f] [-uart cycle hexbytes] [-extat cycle hex]... [-dump f]\n"); return 2; }
''',
 r'''		else { fprintf(stderr, "usage: p8run -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-halt] [-quantum n] [-sd romset.zip] [-notrace] [-sleeps] [-ctrlog f] [-uart cycle hexbytes] [-extat cycle hex]... [-dump f]\n"); return 2; }
'''),
(r'''	printf("E %llu\n", end);
	if (dump) {
''',
 r'''	printf("E %llu\n", end);
	if (sleeps) fprintf(stderr, "p8run: %llu idle-loop sleeps\n", (unsigned long long)chip.sleeps);
	if (dump) {
'''),
])
EOF
```

- [ ] **Step 4: The core suites pass**

Run: `tests/pinheck/p8x32a/check.sh | grep -v ' match' | tail -4; for s in link display audio; do tests/pinheck/$s/check.sh | tail -1; done` (about 7 minutes)
Expected:
```text
eeprom: ok
dasm: 31/31
alu: 4000000 cases, 0 mismatches
p8x32a: 218 passed, 0 failed
link: 0 failed
display: 0 failed
audio: 0 failed
```

- [ ] **Step 5: Rebuild and compare with the reference**

```sh
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m9.log 2>&1; tail -1 build/sdl3pinmame/m9.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame timeout 9000 tests/pinheck/perf/bench.sh attract video
```
Expected (the reference's runs from Task 2 are reused, as its binary is the same; speeds vary with the load):
```text
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.373x over 30.0 s emulated (80.5 s wall), whole run 29.4 G instructions and 2.5 s CPU per emulated s, load 6.02
bench attract ref: 0.095x over 30.0 s emulated (316.7 s wall), whole run 111.4 G instructions and 10.3 s CPU per emulated s, load 6.36
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 0.327x over 24.0 s emulated (73.4 s wall), whole run 31.2 G instructions and 2.8 s CPU per emulated s, load 6.04
bench video ref: 0.072x over 24.0 s emulated (335.0 s wall), whole run 115.1 G instructions and 13.7 s CPU per emulated s, load 8.24
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32a.h src/wpc/pinheck/prop.c tests/pinheck/p8x32a/run.c tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/chip/idle_ina.spin tests/pinheck/p8x32a/chip/idle_hub.spin tests/pinheck/p8x32a/chip/idle_ctr.spin
git commit -m "p8x32a: a cog polling an unchanging loop sleeps until an input may change"
```

### Task 4: MIPS32 fast paths (stage 2c) and the full regression

**Files:**
- Modify: `src/cpu/mips32/mips32.c`, `src/cpu/mips32/mips32.h`, `src/cpu/pic32mx/pic32mx.c`, `tests/pinheck/mips32/run.c`, `tests/pinheck/mips32/unit_test.c`

**Interfaces:**
- Consumes: Plan 1's `mips32_state`, `mips32_run`, `step`, `tick`, `load`, `store`, `set_cp0`, `mips32_get_cp0`; Plan 2's `pic32mx_init`.
- Produces: `void mips32_direct(mips32_state *s, int slot, uint32_t base, uint32_t size, const uint8_t *rd, uint8_t *wr)` (slot 0–3; `wr` NULL = read-only; `rd` NULL clears the slot) and `mips32_region`/`MIPS32_REGIONS` in `mips32.h`; `mips32_state` gains `region[4]`, `c0`, `count_at`, `ti_at`, `irq_chk`, `fva`, `fsize`, `fptr` (internal; no pointers into the state itself); `pic32mx_init` registers flash (slot 0), RAM (slot 1) and boot flash (slot 2); `mips32run` maps its 16 MB except the MMIO page unless `MIPS32RUN_BUS` is set.

- [ ] **Step 1: Measure the interpreter before**

Run: `tests/pinheck/mips32/bench.sh; perf stat -e instructions:u tests/pinheck/mips32/build/mips32run -n 1000000000 tests/pinheck/mips32/build/bench.elf 2>&1 >/dev/null | grep instructions`
Expected (the M instr/s figure varies with the load; the instruction count does not):
```text
bench: match, 34.7 M instr/s
    32,000,226,275      instructions:u
```

- [ ] **Step 2: Pin the Timer and interrupt timing**

The lazy Count and the interrupt checks on change must keep the cycle at which the Timer fires and the instruction boundary at which an interrupt raised by a store is taken. The tests pass on the current core (Task 6's mutations show they bite the new one):

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/mips32/unit_test.c', [
(r'''static int fails;

''',
 r'''static int fails;
static mips32_state *eic_cpu; /* a store to physical 0xFFF0 raises its EIC request */

'''),
(r'''	(void)ctx;
	if (!p) { *err = 1; return; }
''',
 r'''	(void)ctx;
	if (pa == 0xFFF0 && eic_cpu) { mips32_set_eic(eic_cpu, 1, 0, 0); return; }
	if (!p) { *err = 1; return; }
'''),
(r'''
static void user_mode(void)
''',
 r'''
/* Count advances every other cycle: the Timer stops the run after the instruction on whose cycle Count reaches Compare */
static void timer_fires_on_its_cycle(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0);
	s.compare = 5;
	CHECK(mips32_run(&s, 100) == 10);
	CHECK(mips32_timer_irq(&s) && s.count == 5);

	setup(&s, 0x80001000u, 0);
	s.count_half = 1;
	s.compare = 5;
	CHECK(mips32_run(&s, 100) == 9);

	setup(&s, 0x80001000u, 0);
	put(kmem, 0x1000, 0x24080014u);
	put(kmem, 0x1004, 0x40885800u);
	CHECK(mips32_run(&s, 100) == 40);
	CHECK(mips32_timer_irq(&s) && s.count == 20);

	/* Compare written below Count: the Timer waits for Count to wrap */
	setup(&s, 0x80001000u, 0);
	s.count = 10;
	put(kmem, 0x1028, 0x2408000Cu);
	put(kmem, 0x102C, 0x40885800u);
	CHECK(mips32_run(&s, 100) == 100);
	CHECK(!mips32_timer_irq(&s) && s.count == 60);
}

/* an interrupt raised by a store's bus callback is taken before the next instruction */
static void eic_raised_by_a_store(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0x00000001u);
	put(kmem, 0x1000, 0x3C098001u);
	put(kmem, 0x1004, 0xAD20FFF0u);
	put(kmem, 0x1008, 0x24080001u);
	eic_cpu = &s;
	mips32_run(&s, 3);
	eic_cpu = NULL;
	CHECK(s.epc == 0x80001008u);
	CHECK(mips32_regs(&s)[8] == 0);
	CHECK(s.pc == 0x80000184u);
}

static void user_mode(void)
'''),
(r'''	compare_crossed_by_div();
	user_mode();
''',
 r'''	compare_crossed_by_div();
	timer_fires_on_its_cycle();
	eic_raised_by_a_store();
	user_mode();
'''),
])
EOF
```

Run: `tests/pinheck/mips32/check.sh | tail -2`
Expected:
```text
unit: ok
mips32: 208 passed, 0 failed
```

- [ ] **Step 3: Direct memory, lazy Count, interrupt checks on change**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/mips32/mips32.c', [
(r'''#define REGS(s) ((s)->gpr[(s)->srsctl & 7])
#define USER(s) (((s)->status & (ST_UM | ST_EXL | ST_ERL)) == ST_UM)
''',
 r'''#define REGS(s) ((s)->gpr[(s)->srsctl & 7])
#define STATUS_CHANGED(s) ((s)->irq_chk = 1, (s)->fsize = 0)
#define NEVER (~(uint64_t)0)
#define USER(s) (((s)->status & (ST_UM | ST_EXL | ST_ERL)) == ST_UM)
'''),
(r'''
static void take_exception(mips32_state *s, int code, int ce)
''',
 r'''
/* Count advances every other cycle; count and count_half hold its value at cycle count_at */
static uint32_t count_now(const mips32_state *s, uint64_t c)
{
	return s->count + (uint32_t)((c - s->count_at + (uint64_t)s->count_half) >> 1);
}

static void count_sync(mips32_state *s, uint64_t c)
{
	uint64_t t = c - s->count_at + (uint64_t)s->count_half;
	s->count += (uint32_t)(t >> 1);
	s->count_half = (int)(t & 1);
	s->count_at = c;
}

/* the first cycle at which Count equals Compare, while the Timer interrupt is not pending */
static void count_arm(mips32_state *s)
{
	uint64_t d = (uint32_t)(s->compare - s->count);
	if (s->cause & CA_TI) { s->ti_at = NEVER; return; }
	if (!d) d = (uint64_t)1 << 32;
	s->ti_at = s->count_at + 2 * d - (uint64_t)s->count_half;
}

static void take_exception(mips32_state *s, int code, int ce)
'''),
(r'''	s->status |= ST_EXL;
	base = (s->status & ST_BEV) ? 0xBFC00200u : (s->ebase & 0xFFFFF000u);
''',
 r'''	s->status |= ST_EXL;
	STATUS_CHANGED(s);
	base = (s->status & ST_BEV) ? 0xBFC00200u : (s->ebase & 0xFFFFF000u);
'''),
(r'''static int load(mips32_state *s, uint32_t va, int size, uint32_t *out)
{
	uint32_t pa;
''',
 r'''static const uint8_t *direct_rd(const mips32_state *s, uint32_t pa, uint32_t size)
{
	int k;
	for (k = 0; k < MIPS32_REGIONS; k++) {
		const mips32_region *m = &s->region[k];
		if (pa - m->base < m->size && m->size - (pa - m->base) >= size) return m->rd + (pa - m->base);
	}
	return NULL;
}

static uint8_t *direct_wr(const mips32_state *s, uint32_t pa, uint32_t size)
{
	int k;
	for (k = 0; k < MIPS32_REGIONS; k++) {
		const mips32_region *m = &s->region[k];
		if (m->wr && pa - m->base < m->size && m->size - (pa - m->base) >= size) return m->wr + (pa - m->base);
	}
	return NULL;
}

static uint32_t le32(const uint8_t *m) { return m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 | (uint32_t)m[3] << 24; }

static int load(mips32_state *s, uint32_t va, int size, uint32_t *out)
{
	uint32_t pa;
	const uint8_t *m;
'''),
(r'''		take_exception(s, MIPS32_EXC_ADEL, 0);
		return 0;
	}
	*out = s->bus.read(s->bus.ctx, pa, size, 0, &err);
''',
 r'''		take_exception(s, MIPS32_EXC_ADEL, 0);
		return 0;
	}
	if ((m = direct_rd(s, pa, (uint32_t)size)) != NULL) {
		*out = size == 4 ? le32(m) : size == 2 ? (uint32_t)(m[0] | m[1] << 8) : m[0];
		return 1;
	}
	*out = s->bus.read(s->bus.ctx, pa, size, 0, &err);
'''),
(r'''	int err = 0;
	if ((va & (uint32_t)(size - 1)) || !mips32_translate(s, va, &pa)) {
		s->badvaddr = va;
		take_exception(s, MIPS32_EXC_ADES, 0);
		return 0;
''',
 r'''	uint8_t *m;
	int err = 0, i;
	if ((va & (uint32_t)(size - 1)) || !mips32_translate(s, va, &pa)) {
		s->badvaddr = va;
		take_exception(s, MIPS32_EXC_ADES, 0);
		return 0;
	}
	if ((m = direct_wr(s, pa, (uint32_t)size)) != NULL) {
		for (i = 0; i < size; i++) m[i] = (uint8_t)(v >> (8 * i));
		return 1;
'''),
(r'''	case 9 * 8:      return s->count;
''',
 r'''	case 9 * 8:      return count_now(s, s->c0);
'''),
(r'''	case 9 * 8:      s->count = v; s->count_half = 0; break;
	case 11 * 8:     s->compare = v; s->cause &= ~CA_TI; break;
	case 12 * 8:     s->status = (s->status & ~ST_WMASK) | (v & ST_WMASK); break;
''',
 r'''	case 9 * 8:      count_sync(s, s->c0); s->count = v; s->count_half = 0; count_arm(s); break;
	case 11 * 8:     count_sync(s, s->c0); s->compare = v; s->cause &= ~CA_TI; count_arm(s); break;
	case 12 * 8:     s->status = (s->status & ~ST_WMASK) | (v & ST_WMASK); STATUS_CHANGED(s); break;
'''),
(r'''{
	if (s->status & ST_ERL) {
''',
 r'''{
	STATUS_CHANGED(s);
	if (s->status & ST_ERL) {
'''),
(r'''		if (op & 0x20) s->status |= ST_IE; else s->status &= ~ST_IE;
		SET(RT(op), v);
''',
 r'''		if (op & 0x20) s->status |= ST_IE; else s->status &= ~ST_IE;
		STATUS_CHANGED(s);
		SET(RT(op), v);
'''),
(r'''		case 2: SET(RT(op), s->count); break;
''',
 r'''		case 2: SET(RT(op), count_now(s, s->c0)); break;
'''),
(r'''static void step(mips32_state *s)
{
	uint32_t pa, op;
''',
 r'''/* remember the direct region the fetch at pc (physical pa) came from */
static void fetch_cache(mips32_state *s, uint32_t pa)
{
	int k;
	for (k = 0; k < MIPS32_REGIONS; k++) {
		const mips32_region *m = &s->region[k];
		if (pa - m->base < m->size && m->size >= 4 && !(m->base & 3)) {
			s->fva = s->pc - (pa - m->base);
			s->fsize = m->size - 3;
			s->fptr = m->rd;
			return;
		}
	}
}

static void step(mips32_state *s)
{
	uint32_t pa, op, off = s->pc - s->fva;
	const uint8_t *m;
'''),
(r'''	if ((s->pc & 3) || !mips32_translate(s, s->pc, &pa)) {
		s->badvaddr = s->pc;
		take_exception(s, MIPS32_EXC_ADEL, 0);
		return;
	}
	op = s->bus.read(s->bus.ctx, pa, 4, 1, &err);
	if (err) { take_exception(s, MIPS32_EXC_IBE, 0); return; }
	s->pc = s->npc;
	s->npc += 4;
	s->delay = 0;
	execute(s, op);
}

static void tick(mips32_state *s, uint64_t n)
{
	uint64_t t = n + (uint64_t)s->count_half;
	uint32_t inc = (uint32_t)(t >> 1), old;

	s->count_half = (int)(t & 1);
	if (!inc) return;
	old = s->count;
	s->count += inc;
	if (s->compare - old - 1 < inc && !(s->cause & CA_TI)) {
		s->cause |= CA_TI;
		s->stop = 1;
	}
''',
 r'''	if (off < s->fsize && !(off & 3)) op = le32(s->fptr + off);
	else {
		if ((s->pc & 3) || !mips32_translate(s, s->pc, &pa)) {
			s->badvaddr = s->pc;
			take_exception(s, MIPS32_EXC_ADEL, 0);
			return;
		}
		if ((m = direct_rd(s, pa, 4)) != NULL) {
			op = le32(m);
			fetch_cache(s, pa);
		} else {
			op = s->bus.read(s->bus.ctx, pa, 4, 1, &err);
			if (err) { take_exception(s, MIPS32_EXC_IBE, 0); return; }
		}
	}
	s->pc = s->npc;
	s->npc += 4;
	s->delay = 0;
	execute(s, op);
'''),
(r'''	while (s->cycles < end && !s->stop) {
		uint64_t c0 = s->cycles;
		if (irq_pending(s)) {
			s->cur_pc = s->pc;
			s->cur_delay = s->delay;
			s->skip_pc = s->pc;
			take_exception(s, MIPS32_EXC_INT, 0);
			if (s->stop) break;
		}
		if (s->waiting) {
			uint64_t burn = end - s->cycles;
''',
 r'''	s->count_at = s->c0 = s->cycles;
	count_arm(s);
	STATUS_CHANGED(s);
	while (s->cycles < end && !s->stop) {
		s->c0 = s->cycles;
		if (s->irq_chk) {
			s->irq_chk = 0;
			if (irq_pending(s)) {
				s->cur_pc = s->pc;
				s->cur_delay = s->delay;
				s->skip_pc = s->pc;
				take_exception(s, MIPS32_EXC_INT, 0);
				if (s->stop) break;
			}
		}
		if (s->waiting) {
			uint64_t burn = end - s->cycles;
			count_sync(s, s->cycles);
'''),
(r'''		tick(s, s->cycles - c0);
	}
''',
 r'''		if (s->cycles >= s->ti_at) {
			s->cause |= CA_TI;
			s->stop = 1;
			s->ti_at = NEVER;
		}
	}
	count_sync(s, s->cycles);
	s->c0 = s->cycles;
'''),
(r'''void mips32_set_eic(mips32_state *s, int ripl, int vector, int srs)
{
''',
 r'''void mips32_direct(mips32_state *s, int slot, uint32_t base, uint32_t size, const uint8_t *rd, uint8_t *wr)
{
	mips32_region *m;
	if (slot < 0 || slot >= MIPS32_REGIONS) return;
	m = &s->region[slot];
	m->base = base;
	m->size = rd ? size : 0;
	m->rd = rd;
	m->wr = wr;
	s->fsize = 0;
}

void mips32_set_eic(mips32_state *s, int ripl, int vector, int srs)
{
	s->irq_chk = 1;
'''),
])
edit('src/cpu/mips32/mips32.h', [
(r'''typedef struct mips32_state mips32_state;

''',
 r'''typedef struct mips32_state mips32_state;

#define MIPS32_REGIONS 4

/* physical memory served directly, without the bus callbacks; wr NULL = read-only (writes go to the bus) */
typedef struct mips32_region {
	uint32_t base, size;
	const uint8_t *rd;
	uint8_t *wr;
} mips32_region;

'''),
(r'''};

void mips32_init(mips32_state *s, const mips32_bus *bus, int shadow_sets, uint32_t prid);
void mips32_reset(mips32_state *s);
''',
 r'''	mips32_region region[MIPS32_REGIONS];
	uint64_t c0, count_at, ti_at; /* instruction start; count/count_half hold Count at count_at; Timer fires at ti_at */
	int irq_chk;                  /* interrupt state may have changed */
	uint32_t fva, fsize;          /* instructions at [fva, fva + fsize) come from fptr */
	const uint8_t *fptr;
};

void mips32_init(mips32_state *s, const mips32_bus *bus, int shadow_sets, uint32_t prid);
void mips32_reset(mips32_state *s);
void mips32_direct(mips32_state *s, int slot, uint32_t base, uint32_t size, const uint8_t *rd, uint8_t *wr);
'''),
])
edit('src/cpu/pic32mx/pic32mx.c', [
(r'''	mips32_init(&p->cpu, &bus, 2, 0x00018700u);
	pic32mx_reset(p);
''',
 r'''	mips32_init(&p->cpu, &bus, 2, 0x00018700u);
	mips32_direct(&p->cpu, 0, 0x1D000000u, p->flash_size, p->flash, NULL);
	mips32_direct(&p->cpu, 1, 0, PIC32MX_RAM_SIZE, p->ram, p->ram);
	mips32_direct(&p->cpu, 2, 0x1FC00000u, PIC32MX_BOOT_SIZE, p->boot, NULL);
	pic32mx_reset(p);
'''),
])
edit('tests/pinheck/mips32/run.c', [
(r'''	mips32_init(&cpu, &bus, 2, 0x00018700u);
	if (!load_elf(path, &entry)) return 2;
''',
 r'''	mips32_init(&cpu, &bus, 2, 0x00018700u);
	if (!getenv("MIPS32RUN_BUS")) mips32_direct(&cpu, 0, 0, MMIO_EIC & ~0xFFFu, mem, mem); /* all but the MMIO page */
	if (!load_elf(path, &entry)) return 2;
'''),
])
EOF
```

- [ ] **Step 4: The core suites pass**

Run: `tests/pinheck/mips32/check.sh | tail -3; tests/pinheck/mips32/bench.sh; B=tests/pinheck/mips32/build; perf stat -e instructions:u $B/mips32run -n 1000000000 $B/bench.elf 2>&1 >/dev/null | grep instructions; $B/mips32run -n 1000000000 $B/bench.elf | od -An -tx4; MIPS32RUN_BUS=1 $B/mips32run -n 1000000000 $B/bench.elf | od -An -tx4`
Expected (the last two lines: the direct path and the bus callbacks compute the same result; the M instr/s figure varies with the load):
```text
dasm: 45/45
unit: ok
mips32: 208 passed, 0 failed
bench: match, 66.7 M instr/s
    17,400,364,132      instructions:u
 4c74ae80
 4c74ae80
```

- [ ] **Step 5: Rebuild; every suite and machine check**

```sh
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m9.log 2>&1; tail -1 build/sdl3pinmame/m9.log; grep -i warning build/sdl3pinmame/m9.log | grep -c 'mips32\|p8x32a\|pic32mx\|prop\.c'
export SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame
for s in mips32 pic32mx storage link p8x32a display audio board; do tests/pinheck/$s/check.sh > build/m9-$s.log 2>&1; echo "$s: exit $? $(tail -1 build/m9-$s.log)"; done
for c in pic32mx/pinmame_check.sh display/pinmame_display.sh audio/pinmame_audio.sh board/pinmame_board.sh; do timeout 7200 tests/pinheck/$c > build/m9-$(basename $c .sh).log 2>&1; echo "$c: exit $? $(tail -1 build/m9-$(basename $c .sh).log)"; done
```
Expected (about 20 minutes):
```text
[100%] Built target sdl3pinmame
0
mips32: exit 0 mips32: 208 passed, 0 failed
pic32mx: exit 0 pic32mx: 0 failed
storage: exit 0 storage: 0 failed
link: exit 0 link: 0 failed
p8x32a: exit 0 p8x32a: 218 passed, 0 failed
display: exit 0 display: 0 failed
audio: exit 0 audio: 0 failed
board: exit 0 board: 0 failed
pic32mx/pinmame_check.sh: exit 0 pinmame: ok
display/pinmame_display.sh: exit 0 pinmame display: ok
audio/pinmame_audio.sh: exit 0 pinmame audio: ok
board/pinmame_board.sh: exit 0 pinmame board: ok
```

- [ ] **Step 6: Compare with the reference; profile**

```sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame timeout 9000 tests/pinheck/perf/bench.sh attract video
timeout 2700 cmake --build build/sdl3pinmame-prof -j"$(nproc)" > build/sdl3pinmame-prof/m9.log 2>&1; tail -1 build/sdl3pinmame-prof/m9.log
SDL3PINMAME=build/sdl3pinmame-prof/sdl3pinmame timeout 5400 tests/pinheck/perf/bench.sh profile attract | tail -8
SDL3PINMAME=build/sdl3pinmame-prof/sdl3pinmame timeout 5400 tests/pinheck/perf/bench.sh profile video | tail -8
```
Expected (speeds vary with the load; percentages by about a point):
```text
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.448x over 30.0 s emulated (67.0 s wall), whole run 22.4 G instructions and 2.1 s CPU per emulated s, load 4.65
bench attract ref: 0.095x over 30.0 s emulated (316.7 s wall), whole run 111.4 G instructions and 10.3 s CPU per emulated s, load 6.36
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 0.399x over 24.0 s emulated (60.2 s wall), whole run 24.2 G instructions and 2.2 s CPU per emulated s, load 4.93
bench video ref: 0.072x over 24.0 s emulated (335.0 s wall), whole run 115.1 G instructions and 13.7 s CPU per emulated s, load 8.24
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
[100%] Built target sdl3pinmame
bench attract prof: 0.531x over 30.0 s emulated (56.5 s wall), load 4.13
  59.9%  Propeller core (p8x32a)
  34.1%  PIC32 (mips32, pic32mx)
   2.1%  other (PinMAME, kernel, libraries)
   1.6%  Propeller stepping and link (prop.c)
   1.1%  SD card and EEPROM (sd, vfat, zipsrc, eeprom, zlib)
   0.7%  display and audio (display.c, audio.c)
   0.5%  board and driver (board.c, pinheck.c, rtc.c)
bench video prof: 0.466x over 24.0 s emulated (51.4 s wall), load 3.35
  61.9%  Propeller core (p8x32a)
  31.5%  PIC32 (mips32, pic32mx)
   2.2%  other (PinMAME, kernel, libraries)
   1.9%  Propeller stepping and link (prop.c)
   1.3%  SD card and EEPROM (sd, vfat, zipsrc, eeprom, zlib)
   0.7%  display and audio (display.c, audio.c)
   0.5%  board and driver (board.c, pinheck.c, rtc.c)
```

- [ ] **Step 7: Commit**

```bash
git add src/cpu/mips32/mips32.c src/cpu/mips32/mips32.h src/cpu/pic32mx/pic32mx.c tests/pinheck/mips32/run.c tests/pinheck/mips32/unit_test.c
git commit -m "mips32: direct RAM and flash, lazy Count, interrupt checks on change"
```

### Task 5: The scripted game as a workload and regression check (after Plan 8b)

Plan 8b adds `tests/pinheck/game/pinmame_game.sh` (a 315 s game with `PINHECK_RTC` fixed; its four logs are byte-identical between runs of one build) and names it Milestone 9's workload and determinism reference. This task runs once Plan 8b is merged into `pinheck` and this branch is rebased onto it; until then skip it (`[ -x tests/pinheck/game/pinmame_game.sh ] || echo "Plan 8b not merged: Task 5 waits"`).

**Files:** none changed; the game stays in the suite as Plan 8b left it.

- [ ] **Step 1: A reference build that has the game**

The reference must contain Plan 8b but none of this plan's changes: build the commit this branch was rebased onto.

```sh
BASE=$(git merge-base HEAD pinheck)
git worktree add ../m9-game-ref $BASE
(cd ../m9-game-ref && cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt && cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null && timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m9.log 2>&1; tail -1 build/sdl3pinmame/m9.log)
```
Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 2: Play the game with both builds**

The commands are Plan 8b's (Task 2 Steps 9 and 10), timed with `perf stat`:

```sh
perf stat -e task-clock,instructions:u -o build/game-ref.perf env SDL3PINMAME=$PWD/../m9-game-ref/build/sdl3pinmame/sdl3pinmame timeout 6000 tests/pinheck/game/pinmame_game.sh | tail -1
rm -rf build/game1 && cp -r tests/pinheck/game/build/pinmame build/game1
perf stat -e task-clock,instructions:u -o build/game-opt.perf env SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 6000 tests/pinheck/game/pinmame_game.sh | tail -1
for f in link2.log out2.log uart2.log frames2.bin; do cmp build/game1/$f tests/pinheck/game/build/pinmame/$f && echo "$f same"; done
grep -h 'elapsed\|instructions' build/game-ref.perf build/game-opt.perf
```
Expected (the reference game takes about 50 minutes, as in Plan 8b):
```text
pinmame game: ok
pinmame game: ok
link2.log same
out2.log same
uart2.log same
frames2.bin same
```
followed by the two runs' instruction counts and elapsed times; record them in the roadmap's m9 row with the date and load.

- [ ] **Step 3: Remove the reference worktree**

Run: `git worktree remove --force ../m9-game-ref`

### Task 6: Record the results, prove the tests bite

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` (§4), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` (the m9 row, "Carried into Plan 9")

- [ ] **Step 1: Update the spec and the roadmap**

The anchors are lines Plan 8b does not edit, so the script applies before and after Plan 8b is merged.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md', [
    ('  4. Multi-threading only if the result stays deterministic.\n',
     '  4. Multi-threading only if the result stays deterministic.\n'
     '- **Established by Plan 9 (stages 1 and 2):**\n'
     '  - `tests/pinheck/perf/bench.sh` times attract mode (10–40 s) and a video with music and three effects (16–40 s) headless, with the DS1340 seeded from a fixed clock (without it UART1 and the frames differ between runs), and requires byte-identical UART1, frame, sound and NVRAM output against a reference build; `bench.sh profile` groups a `perf` profile of a `-g` build by component.\n'
     '  - The reference build (`3d9971c3`) executed 111 G host instructions per emulated second in attract mode and 115 G with video and four channels (0.07–0.13× real time on the shared machine); 85% of its time was in the Propeller core and 10% in the PIC32.\n'
     '  - Stage 2 is exact: the RTL, spinsim and QEMU suites and every machine check pass, and both workloads stay byte-identical to the reference. Host instructions per emulated second, attract / video: Propeller fast paths 43 / 45 G, idle loops 29 / 31 G, MIPS32 fast paths 22 / 24 G, about 5× fewer than the reference; 0.45–0.53× (attract) and 0.40–0.47× (video) real time at load averages 3–5.\n'
     '  - After stage 2 the Propeller core takes about 60% of the time and the PIC32 about 33%: the two cogs running the ROM Spin interpreter (about 16 M instructions/s each) and the MIPS32 interpreter. Real time needs the machine below one CPU second per emulated second, more than 2× less than now: stage 3 must bring the Propeller core from about 1.2 s to about 0.45 s and the PIC32 from about 0.7 s to about 0.35 s per emulated second (a JIT for the Spin-interpreter cogs and for the MIPS32 code in flash); stage 4 (the Propeller and the PIC32 on two threads) would bound the total by the larger of the two.\n'),
])
edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    ('| m9 | 9: performance, staged: profile, interpreter fast paths, `asmjit` JIT, deterministic threading only if needed | Plan 8 | none | ≥ 1.0× real time on the Ryzen 7 7730U for attract, scripted game and video+audio, byte-identical to the reference build | after Plan 8 (parallel with the display look) |\n',
     '| `2026-09-29-pinheck-m9-performance.md` | 9, stages 1–2: benchmark and profile; exact interpreter fast paths (Propeller ALU by unit, local run-ahead, idle-loop sleep; MIPS32 direct memory, lazy Count) | Plan 8a (Plan 8b for the scripted-game workload, Task 5) | none | byte-identical to the reference build; 0.45–0.53× attract, 0.40–0.47× video; ≥ 1.0× left to stage 3 | written |\n'
     '| m9b | 9, stages 3–4: `asmjit` JIT for the Spin-interpreter cogs and the MIPS32, deterministic threading only if needed | Plan 9 | none | ≥ 1.0× real time on the Ryzen 7 7730U for attract, scripted game and video+audio, byte-identical to the reference build | after Plan 9 |\n'),
    ('- Interpreter throughput: `mips32` 106–118 M instr/s (needs ~80); `p8x32a` ~52 M cycles/s with one busy cog (needs 104 M cycles/s with up to 8 cogs), so the Propeller core is the real-time risk.\n',
     '- Interpreter throughput: `mips32` 106–118 M instr/s (needs ~80); `p8x32a` ~52 M cycles/s with one busy cog (needs 104 M cycles/s with up to 8 cogs), so the Propeller core is the real-time risk.\n'
     '\n'
     '## Carried into stages 3–4 (m9b)\n'
     '\n'
     '- After Plan 9 the machine takes about 2 CPU seconds per emulated second (22–24 G host instructions): the Propeller core about 60% (cogs 0 and 5 run the ROM Spin interpreter, about 16 M instructions/s each; the polling cogs sleep), the PIC32 about 33%. `tests/pinheck/perf/bench.sh` with `REFERENCE` is the determinism check for every later stage; run Plan 9 Task 5 (the scripted game) once Plan 8b is merged.\n'),
])
EOF
```

- [ ] **Step 2: Mutations**

Apply each mutation, run the named check, confirm the named failure line (other failure lines may follow), then restore the file with `git checkout <file>`. The p8x32a suite takes about a minute, the mips32 suite about 40 seconds.

| File | Replace → with | Check | Must show |
|---|---|---|---|
| `src/cpu/p8x32a/p8x32a.c` | in `alu_run`, `case 0x39: r = d - 1; *co = !d;` → `case 0x39: r = d - 1; *co = 0;` | the ALU test (`cc -O2 -std=c99 -Isrc/cpu/p8x32a -o build/alu_test tests/pinheck/p8x32a/alu_test.c && build/alu_test`) | `ALU FAIL: alu_run op 39 s 0000003e d 00000000 pc 07f c 0 z 0: ref ffffffff w1 c1 z0, got ffffffff w1 c0 z0` |
| `src/cpu/p8x32a/p8x32a.c` | in `ina`, the line `flush(p, t);` → nothing | `tests/pinheck/p8x32a/check.sh` | `RTL MISMATCH chip/idle_ctr.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `loop_bound`, the line `if (p->bus.pins_next && (e = p->bus.pins_next(p->bus.ctx, t)) < w) w = e;` → nothing | `tests/pinheck/p8x32a/check.sh` | `RTL MISMATCH chip/idle_ina.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `loop_notify`, `(pins & p->loop[n].wake)` → `(pins & p->loop[n].wake & 0)` | `tests/pinheck/p8x32a/check.sh` | `RTL MISMATCH chip/idle_ina.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `do_hub`, `if (p->sleepers) loop_hub_write(` → `if (0) loop_hub_write(` | `tests/pinheck/p8x32a/check.sh` | `RTL MISMATCH chip/idle_hub.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `sys`, the line `if (p->sleepers >> num & 1) loop_resume(p, (int)num, h);` → nothing | `tests/pinheck/p8x32a/check.sh` | `RTL MISMATCH chip/nco.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `loop_edge`, after `p8x32a_loop *l = &p->loop[n];` add `if (l) return;` | `tests/pinheck/p8x32a/check.sh` | `SLEEPS chip/idle_ctr.spin: p8run: 0 idle-loop sleeps, expected 8` |
| `src/cpu/mips32/mips32.c` | in `count_arm`, `s->count_at + 2 * d -` → `s->count_at + 2 * d + 1 -` | `tests/pinheck/mips32/check.sh` | `UNIT FAIL unit_test.c:119: mips32_run(&s, 100) == 10` |
| `src/cpu/mips32/mips32.c` | in `set_cp0`, `case 11 * 8:     count_sync(s, s->c0); s->compare = v;` → `case 11 * 8:     s->compare = v;` | `tests/pinheck/mips32/check.sh` | `UNIT FAIL unit_test.c:138: mips32_run(&s, 100) == 100` |
| `src/cpu/mips32/mips32.c` | in `mips32_set_eic`, the line `s->irq_chk = 1;` → nothing | `tests/pinheck/mips32/check.sh` | `UNIT FAIL unit_test.c:153: s.epc == 0x80001008u` |
| `src/cpu/mips32/mips32.c` | in `set_cp0`, `(v & ST_WMASK); STATUS_CHANGED(s); break;` → `(v & ST_WMASK); break;` | `tests/pinheck/mips32/check.sh` | `GOLDEN FAIL golden/eic.S` |

Then `git status --short src tests` prints nothing.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "Spec: Milestone 9 stages 1 and 2 measured; what stage 3 must deliver"
```

## Result

Measured in the replay on the Ryzen 7 7730U while other agents' emulators and builds shared it; the load average is printed with every speed, and host instructions per emulated second (the benchmark's `perf stat`, whole run) do not depend on it.

| Build | attract: G instructions / emulated s | attract: speed (load) | video + four channels: G instructions / emulated s | video: speed (load) |
|---|---|---|---|---|
| reference (`3d9971c3`) | 111.4 | 0.095× (6.4) | 115.1 | 0.072× (8.2) |
| Task 2: Propeller fast paths | 43.2 | 0.174× (10.0) | 44.6 | 0.204× (8.8) |
| Task 3: idle loops sleep | 29.4 | 0.373× (6.0) | 31.2 | 0.327× (6.0) |
| Task 4: MIPS32 fast paths | 22.4 | 0.448× (4.7); 0.531× (4.1, the `-g` build) | 24.2 | 0.399× (4.9); 0.466× (3.4, the `-g` build) |

Every row is byte-identical to the reference in UART1, frames, sound and NVRAM. At a load of about 5 the reference ran attract mode at about 0.13× (spec §4 quotes 0.15–0.36× from earlier milestones' runs).

Where the time goes after stage 2 (Task 4 Step 6): the Propeller core about 60%, the PIC32 about 33%, everything else (SD card, EEPROM, display, audio, board, PinMAME) about 7%. On a lightly loaded machine that is about 1.9 s of CPU per emulated second in attract mode and 2.1 s with video: the Propeller core about 1.2 s (the ROM Spin interpreter in cogs 0 and 5, about 16 M instructions/s each, and the remaining non-idle PASM), the PIC32 about 0.7 s (80 M instructions/s at about 9 ns each).

What stage 3 must deliver: real time with headroom for the sound stream needs the whole machine below about 0.9 s per emulated second, 2.1–2.4× less than now. The other components already take about 0.15 s, which leaves about 0.45 s for the Propeller core (2.7× faster: a JIT for the Spin-interpreter cogs, whose code is the fixed ROM interpreter and whose cog RAM is rewritten only by known instructions) and about 0.3 s for the PIC32 (2.3× faster: a JIT for the firmware in flash, which changes only when the bootloader stand-in reprograms it). Stage 4, the Propeller and the PIC32 on two threads (the PIC32 already runs ahead and hands the Propeller timestamped edges; it would wait for the Propeller only where it reads it: RF13, the sound stream and the display), would bound the time by the slower of the two, about 1.2 s now, so it reaches real time only together with a Propeller JIT. `bench.sh` with `REFERENCE` is the determinism check for both.

## Self-Review

**Spec coverage** (addendum §4):

| Requirement | Where |
|---|---|
| headless benchmark over attract mode, the scripted game, video with four audio channels | Task 1 `bench.sh` (attract, video: `[V00AT9]` with music and three effects, Ruling 3); Task 5 (the scripted game, after Plan 8b) |
| stage 1: `perf` profile | Task 1 Step 5 and Task 4 Step 6 (`bench.sh profile`, `components.py`, Ruling 4) |
| stage 2: pre-decoded instructions | `alu_run` decodes a running cog's common ops directly and `run_local` keeps the decoded fields in registers (Ruling 7, 8); the MIPS32 fetch reads a cached host pointer (Ruling 10). A table of pre-decoded instructions was not built: the Propeller's cog RAM is self-modifying (`MOVS`/`MOVD`/`MOVI` in every loop) and the profile after Task 4 shows decode below the remaining costs (Result) |
| stage 2: direct RAM/flash fetch that bypasses bus callbacks | Task 4 (`mips32_direct`, Ruling 10) |
| stage 2: cheaper hub-slot and idle-cog handling | Task 2 (local instructions run ahead of the scheduler, Ruling 8), Task 3 (idle loops sleep, Ruling 9) |
| stages 3 and 4 only as far as needed | not in this plan (Ruling 1); the Result states stage 3's target |
| proof: all existing suites pass, including the RTL and QEMU differential suites | Tasks 2 and 3 Step 4, Task 4 Steps 4–5 |
| proof: determinism against the reference build (UART1, frames, audio byte for byte) | `bench.sh` with `REFERENCE` after Tasks 2, 3 and 4 (plus NVRAM), Task 1 Steps 3–4 (repeatable with the fixed clock, not without it), Task 5 (the game's logs) |
| target ≥ 1.0× with the sound never under-running | not reached: measured in the Result, handed to stage 3 |

**Placeholder scan:** no TBD/TODO; every code step carries its full code or an anchored edit script; every Run has its Expected output from the proving replay (speeds as measured, with the load).

**Type consistency:** `alu_run(unsigned, uint32_t, uint32_t, unsigned, int, int, int, int *, int *, int *)` is defined in `p8x32a.c` and called by `run_local` and `alu_test.c`; `p8x32a_bus.pure_in` is the last field, set by `prop.c` and `run.c`, zero in `dispboot.c`'s `memset`; `p8x32a.sleeps` is read by `run.c`'s `-sleeps`; `mips32_direct(mips32_state *, int, uint32_t, uint32_t, const uint8_t *, uint8_t *)` is declared in `mips32.h` and called by `pic32mx_init` and `run.c`.

**Review Focus:** five items, each pinned by the named tests in Tasks 2–5.

**Proof:** every file and edit this plan writes was extracted from this document into a fresh worktree of `3d9971c3` (`pinheck-m9-replay`) and is byte-identical to the proven prototype; every Expected output above was reproduced there, task by task, on a machine shared with other emulators (the load averages are printed with the speeds). The prototype and the replay each passed every suite and machine check, and both workloads were byte-identical to the reference build after Tasks 2, 3 and 4. Each mutation of Task 6 was caught with the named line. Task 5 could not run: Plan 8b is not merged yet; its commands are Plan 8b's own.
