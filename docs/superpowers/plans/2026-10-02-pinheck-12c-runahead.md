# Plan 12c: America's Most Haunted and The Jetsons after Plan 12b (the lost bits explained; a time-order check; the translator after a restart; cached scheduling keys; PAR and CNT translated; the run-ahead built exact and measured) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Find why Plan 12b's prototype lost display-link bits, test whether an exact run-ahead (cogs' hub operations before other cogs' earlier events, judged by a lookahead over their code) is cheaper than the dispatches it saves, and take every exact change that makes America's Most Haunted (`amh`) and The Jetsons (`jetsons`) faster, every output byte-identical to Plan 12b's build.

**Architecture:** Four exact changes to the Propeller core and its tests; the run-ahead is a ruling, its prototype a kept branch. (1) *A time-order check in the test builds* (`P8X32A_CHECK`): a pin change registered after the pins were sent past its time, or a hub write before an already-run read or write of its bytes, stops the run; the Propeller suite's `p8run` is built with it, so the class of fault behind Plan 12b's lost bits fails the RTL cases. (2) *The translator after a restart:* a cog started again on new code drops its translated blocks and its record of changed words (`jvar`), which before this survived a reload (and, after The Jetsons' Propeller reboot, kept cog 0's Spin interpreter untranslated). (3) *Cached scheduling keys:* `p8x32a_run_until` keeps each cog's key and works out again only the cog that ran, or all of them when `sched_gen` (which already counts every change one cog makes to another cog's next event) moved. (4) *PAR and CNT sources translated:* a translated block reads PAR from the block state and CNT as the instruction's time less `cnt_base`, so polling loops (America's Most Haunted's cog 1) stay in translated code.

**Tech Stack:** C (C89-syntax-clean core), C++17 (asmjit translator), Spin/PASM test programs (openspin, the P1 Verilog RTL), Python 3 (no third-party modules), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §4 (Milestone 9: at least 1.0× on the reference machine, aim 1.2×; byte-identical determinism; every suite green including the RTL, spinsim and QEMU differentials), applied to the games of Plans 11b and 11c. Measured reasons: Plan 12b's Result and "what remains" (America's Most Haunted 0.86× with video and 0.80× in play, The Jetsons 0.90× and 0.88×, with the worker thread and without a profile).

## Prerequisites

- `pinheck` at `3f129b50` (Plan 12b merged with its review minors).
- Before starting, from the repository root, export what Plan 12b's checks use (`TOOLS`, `P8X32A_ROM`, `PINHECK_UPDATE_DIR`, `PINHECK_ZIP`, `DOMINOS_PRP`, `DOMINOS_ZIP`, `RZSPOOK_ZIP`, `RZSPOOK_UPDATE_DIR`, `JETSONS_ZIP`, `JETSONS_UPDATE_DIR`, `AMH_ZIP`, `AMH_UPDATE_DIR`, `AMH_HEX`, `SDL_AUDIO_DRIVER=dummy`) and define the game functions once:
  ```sh
  amh() { PINHECK_GAME=amh PINHECK_ZIP=$AMH_ZIP PINHECK_UPDATE_DIR=$AMH_UPDATE_DIR "$@"; }
  jet() { PINHECK_GAME=jetsons PINHECK_ZIP=$JETSONS_ZIP PINHECK_UPDATE_DIR=$JETSONS_UPDATE_DIR "$@"; }
  rz() { PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR "$@"; }
  ```
  A Domino's run is the command without a function (`env`). Run the steps with `bash`.
- The machine is shared: only one emulator runs at a time machine-wide, so every step that runs PinMAME, libpinmame or a firmware program (the Propeller suite's boots included) goes through `flock /code/spooky_domino/work/emu.lock` (on another machine drop the `flock`); builds and heavy suites run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6`. Every PinMAME launch has a `timeout -k 30`, a private `-nvram_directory` and `-cfg_directory`, and no sound device (`SDL_AUDIO_DRIVER=dummy`); `bench.sh` and the machine checks do this themselves.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit. The scripts were generated from the prototype and replayed in this order on a fresh worktree of `3f129b50`.
- Speeds are wall-clock on the Ryzen 7 7730U while other work shares it; the load average is printed with every speed. Instruction counts on one thread (`PINHECK_THREADS=0`) do not depend on the load; with the worker thread they include the threads' spinning.

## Global Constraints

- **Byte-identical:** every game's UART1 log, display frames (with a raw DMD its 16-subframe cycles and its subframes as the core gets them), sound and NVRAM equal those of the reference build (Task 1: `pinheck` at `3f129b50`) in attract mode, with video and in play, with and without the worker thread, with `PINHECK_JIT=0`, `PINHECK_LAZY=0`, `PINHECK_RF13=0`, `PINHECK_JOURNAL=0`, and for the profile-guided build; the scripted Domino's game's four logs are byte-identical.
- **All suites green:** `tests/pinheck/{mips32,pic32mx,storage,link,p8x32a,display,audio,board}/check.sh` (the `p8x32a` suite with and without the translator, `P8X32A_JIT=1`, with `mutate_lazy.sh` and the new `mutate_core.sh` inside it), and the machine checks of Plans 5–12b for all four games.
- No new switch: the translator's changes are off with `PINHECK_JIT=0`; the scheduler's keys are a rearrangement with the same decisions; the time-order check exists only in test builds (`-DP8X32A_CHECK`).
- Code comments short and factual; the core C89-syntax-clean; no `__inline__` or `__inline` (`P8_INLINE`/`P8_COLD`).
- Never commit the mask ROM, firmware, SD cards, romsets or profiles.

## Rulings

Decisions taken on measurements of the prototype (branch `pinheck-12c-proto`; the run-ahead on `pinheck-12c-ra-proto`). Unless they say otherwise, figures are per emulated second; "G" is 10⁹ host instructions; "one thread" is `PINHECK_THREADS=0`; "the proxy" is `p8run` (translated, no trace) running a game's Propeller firmware and SD card alone for 10 s of emulated time, the Propeller's share of a game without its PIC32 (The Jetsons 7.10 G, America's Most Haunted 5.44 G, Domino's 2.46 G at `3f129b50`).

1. **Why Plan 12b's prototype lost display-link bits.** Reproduced without PinMAME: the prototype (`proto-bind-la-journal.patch` on `f69cea15`) in `p8run` with The Jetsons' firmware and SD card leaves the in-order trace at cycle 49,233,988 (trace line 762,422): the display link's clock (counter A of cog 6 on P22, `FRQA` $40000000) runs eight edges too long. The bound SD cog (cog 3) ran pin events before other cogs' earlier events, and such an early event may read `INA` when its mask has no pin outside the group's inputs. The SD driver's `test r, ina` with `r` = 0 (cog 3, $015 and $018) passes that test, but `ina()` takes the bound path only when the mask meets the bound pins; a mask of 0 took the ordinary path, which `flush`es every pin up to the early event's time, ahead of the other cogs. Their later pin writes then became pending points behind `flushed`: cog 6's `FRQA` = 0 at 49,233,988 was registered with the pins already sent to 49,234,020. The bound cog alone does it too (4,507 late pending points in 50 million cycles, none visible in the outputs); with early hub reads the SD cog ran further ahead and the late points reached the display link. With `ina()` returning 0 for a mask of 0 (nothing to read, nothing to send), the prototype's trace equals the in-order trace over 400 million cycles (12.5 million early hub operations, 5.8 million early pin events), and PinMAME's Jetsons short run is byte-identical with it (frames included), with and without the worker thread. The merged code has no early events (the lazy cog runs behind and never reads `INA`): a build that aborts on a late pending point runs 20 s of each game's firmware without one. No merged code changes for it; Task 2 makes the class of fault a test failure.
2. **Where the time goes after Plan 12b** (a counting build, one thread). The Jetsons' attract mode: 4.4 million dispatches (the Spin interpreters, cogs 0 and 5, 1.05 million each at hub operations; the SD driver, cog 3, 1.0 million and the display link's sender, cog 6, 0.82 million at pin events), 4.3 million `flush` calls and 5.6 million `pins_out`; cog 0 runs 11.9 million instructions in the interpreter (560 million against 74 million translated in 47 s). Profile: PIC32 29%, pin path 22% (`flush` 11%), `run_local` 18%, scheduler 8%, translated code 15%. America's Most Haunted with video: 5.8 million dispatches, almost one per hub operation of its three interpreters (cog 0: 66.9 million dispatches and 7.5 million events run inline in 50 s), cog 1 interprets 2.6 million instructions and averages 2.5 instructions per translated block. Profile: PIC32 26%, `run_local` 24%, scheduler 9%, translated code 24%. Who stops whom: the interpreters mostly each other (America's Most Haunted: cog 1's hub operations waited 30.9 million times for cog 5 and 19.5 million for cog 0 in 50 s); in The Jetsons the SD driver and the display sender mostly each other (20.6 million and 19.9 million) and the interpreters.
3. **The translator after a restart is taken.** `jvar` (the slots whose words changed, which the translator then reads at run time or leaves to the interpreter) survived a cog's reload, and the reload's own writes to translated slots added to it. The Jetsons' Propeller reboots once in a normal start (`games.sh`: `REBOOTS=1`); after it the boot loader's words left cog 0's Spin interpreter untranslated. `restart()` now drops the cog's blocks (so the load's writes mark nothing) and clears `jvar`. One thread: The Jetsons' attract mode 16.2 → 15.0 G, video 16.7 → 15.4 G; America's Most Haunted 16.6 G before and after (it has no reboot). A chip test reloads a cog over a slot its first program rewrote and expects no block lookup left to the interpreter (2,000 without the change).
4. **Cached scheduling keys are taken.** `p8x32a_run_until` worked out all eight keys at every dispatch (70% of its own instructions). It now keeps them, works out again the cog that ran, and all of them when `sched_gen` moved; `sched_gen` already counts every change one cog makes to another cog's next event (`event_run` relies on it). The proxy: The Jetsons 7.10 → 6.86 G, America's Most Haunted 5.44 → 5.23 G, Domino's 2.46 → 2.39 G. Three-cog random programs (sleepers woken by other cogs, locks, rewritten code) stress it against the RTL; mutants that leave the cog that ran with its old key when its next event is a hub operation, or wake a sleeper without counting it, are caught (`mutate_core.sh` counts a case still running after 20 s as caught).
5. **PAR and CNT translated is taken.** America's Most Haunted's cog 1 polls a mailbox in a loop with `mov t, par` and `sub t, cnt`; the translator refused every special source, so the loop ran half in the interpreter in blocks of one to four instructions. The proxy: America's Most Haunted 5.23 → 4.71 G (−10%), the others unchanged; PinMAME with the worker: America's Most Haunted with video 16.5 → 15.6 G, in play 18.6 → 17.6 G. A translated CNT read also marks the loop search state dirty, as the interpreter does (a CNT-reading loop is never taken for an idle one; the recording that would decide it runs in the interpreter). The remaining interpreted work in America's Most Haunted (the interpreters' math slot whose opcode the code rewrites, and a few `SUMx`/`NEGx` opcodes) is under 1%.
6. **The run-ahead of hub operations is rejected: exact, and dearer than the dispatches it saves.** Built on `pinheck-12c-ra-proto` in eight versions: a cog's hub operation at h runs before other cogs' earlier events when none of them can write the hub (for a write: reach it at all) before h, nor wake a sleeper that could. Each other cog's earliest access comes from its next event and per-address lower bounds of the cycles to the next instruction of four classes (a hub write or system op; any hub operation; a `DIRA` or counter-mode write or system op, which may change any pin; an `OUTA`, `FRQ` or `PHS` write, which changes only the cog's own pins), found by a scan over every path through its code (a register jump, a written register on the paths, a word whose run-relevant bits changed, or code in the special registers may be anything), kept per address until a word it read changes (the translator's code-write tracking, extended), and a sleeper may run again at its wake time or once another cog may write hub RAM or change one of its pins. It is exact: the firmware traces of the three games equal the in-order ones over 400 million cycles in a time-order-checked build, 40 three-cog RTL programs match, and PinMAME's outputs are byte-identical for both games. The early operations cut the proxy's dispatches by 31% (America's Most Haunted: 7.76 → 5.36 million in 5 s) and would save about 1 G (18%) if the checks were free (checks always failing: 7.67 G; succeeding: 6.71 G). But the cheapest check found (bounds found at each dispatch's end, limits per dispatch, version 8) costs 2.05 G on America's Most Haunted's proxy (6.52 G against 5.36 G without), and in PinMAME on one thread The Jetsons with video takes 24.0 G and America's Most Haunted 29.5 G against 15.1 G and 16.2 G without it. Stale bounds (a bound found for an earlier event stays a bound), O(1) two-least structures, and failing fast on the earliest cog each lowered the cost but never below the dispatches saved: the cogs that stop an interpreter are within a few instructions of the hub, and every success needs a fresh bound for the cog that just moved.
7. **The run-ahead of pin events (Plan 12b's bound SD card and display link) is not rebuilt.** Early pin writes need several future values per register and counter (the pending points keep two), and early `INA` reads need the other cogs' pin horizons, which for a Spin interpreter end at its next bytecode dispatch (a register jump) a few instructions on; Plan 12b measured the bound path slower than the pending points before any running ahead. With Ruling 1's cause found, Plan 12b's prototype is exact again but takes 31.1 G against 13.6 G on The Jetsons' short run with the worker thread.
8. **Profile-guided builds** stay as Plan 12b documented them (`pgo.sh`, trained on Domino's).
9. **Real time.** With the worker thread (the replay, loads 1.1–3.5): The Jetsons 1.07× in attract mode, 0.96× with video, 0.96× in play without a profile and 1.21×, 1.09×, 1.08× with the profile-guided build; America's Most Haunted 1.18×, 0.91×, 0.86× and 1.22×, 0.96×, 0.87×. Real time is reached for The Jetsons in every workload with the profile-guided build (without it, in attract mode only, video and play at 0.96×), and for America's Most Haunted in attract mode only: with video it is 4% short with the profile, in play 13%. Cost if wrong: none to exactness; the statement is a measurement.

## Review Focus

- **The reset at a restart** (`restart()`): a cog started again by another cog or by itself, while translated blocks of its old code are valid; the load's writes must neither keep stale blocks running nor mark the new code's words as changing (`chip/jit_restart.spin` and its two mutants).
- **The cached keys:** every place that changes another cog's `ev` or `ev_t` must count in `sched_gen` (`loop_notify`, `loop_hub_write`, `sys`, `lazy_exit`, `lazy_try`); the lazy cog's `ev_t` stays `P8X32A_NEVER` across `lazy_run`, so its key does not change behind the scheduler.
- **Translated CNT:** the instruction's time inside a block (`T2 + 4k`, `T2` moved by a lazy cog's hub read), a CNT source in a slot whose condition never runs, the loop search's dirty flag.
- **The time-order check** sees hub accesses by every path that reads or writes hub RAM for a cog (`do_hub`, `hub_rw`, a sleeper's check of its polled bytes), and leaves out the lazy cog's reads, which run behind by design through the journal.

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/p8x32a/p8x32a.c` | the time-order check (`P8X32A_CHECK`); `restart()` drops a cog's blocks and `jvar`; `jit_refused`; cached keys (`ev_key`) in `p8x32a_run_until`; `st.par`, `st.cnt_base` (modified) |
| `src/cpu/p8x32a/p8x32a.h` | `jit_refused`; `p8x32a_jst.par`, `p8x32a_jst.cnt_base` (modified) |
| `src/cpu/p8x32a/p8x32ajit.cpp` | PAR and CNT sources translated (modified) |
| `tests/pinheck/p8x32a/{check.sh,run.c,gen.py}` | the suite's `p8run` time-order checked, `TIME ORDER` and `EXPECT-JITVAR` lines, `-jitvar`, three-cog random programs (`gen.py --cogs`) (modified) |
| `tests/pinheck/p8x32a/mutate_core.sh` | mutants of the time order, the restart, the keys and the translated sources, interpreted or translated (new) |
| `tests/pinheck/p8x32a/chip/{jit_restart,jit_cnt}.spin` | a reloaded cog translated again; PAR and CNT in translated runs and a CNT-polling loop (new) |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | what Plan 12c established; its roadmap row (modified) |

---
### Task 1: Reference build

**Files:** none (a build).

**Interfaces:**
- Consumes: `pinheck` at `3f129b50`, Plan 9's `bench.sh`.
- Produces: `build/reference/sdl3pinmame`, which every later task compares with.

- [ ] **Step 1: Build the reference**

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12c.log 2>&1; tail -1 build/sdl3pinmame/12c.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```

Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 2: The reference on one thread**

The Jetsons and America's Most Haunted with video, as the instruction counts later steps compare with (about 6 minutes with the first boots; on one thread the count does not depend on the load):

```sh
for g in jet amh; do $g env SDL3PINMAME=build/reference/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video | grep 'bench video' | sed "s/^/$g: /"; done
```

Expected (the speeds vary with the load):

```text
jet: bench video opt: 0.724x over 24.0 s emulated (33.1 s wall), whole run 16.7 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.544x, load 1.18
amh: bench video opt: 0.584x over 24.0 s emulated (41.1 s wall), whole run 16.6 G instructions and 1.5 s CPU per emulated s, worst 100 ms 0.472x, load 1.76
```

### Task 2: A time-order check in the Propeller suite

**Files:**
- Create: `tests/pinheck/p8x32a/mutate_core.sh`
- Modify: `tests/pinheck/p8x32a/check.sh`, `src/cpu/p8x32a/p8x32a.c`

**Interfaces:**
- Consumes: the core's pending points (`add_pending`, `p->flushed`), hub accesses (`do_hub`, `hub_rw`, `loop_same`).
- Produces: `-DP8X32A_CHECK` (test builds): `p8x32a: time order: <what> at <t> after <t>` on stderr and `abort()`; check.sh's `TIME ORDER` failure; `mutate_core.sh` with `mutant NAME FILE FROM TO JIT` (JIT 1: a translated `p8run`), its cases `chip/*.spin` and `gen.py` programs, a case still running after 20 s counted as caught, `exit` its last line.

- [ ] **Step 1: The tests**

The suite's `p8run` is built with the check and fails a case that hits it; `mutate_core.sh` runs mutants against the RTL cases; its first mutant sends the pins 64 cycles ahead at an `INA` read (Ruling 1's fault):

```sh
python3 - <<'EOF'
import os


def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/p8x32a/check.sh', [
    (r'''$CC -I$CORE -I$DEV -o $B/p8run run.c $CORE/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
''',
     r'''# P8X32A_CHECK: a pin change or hub access out of time order stops the run
$CC -DP8X32A_CHECK -I$CORE -I$DEV -o $B/p8run run.c $CORE/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
'''),
    (r'''		$CC -DP8X32A_JIT -I$CORE -I$DEV -c "$f" -o $B/jit-$(basename "$f" .c).o || exit 2''',
     r'''		$CC -DP8X32A_JIT -DP8X32A_CHECK -I$CORE -I$DEV -c "$f" -o $B/jit-$(basename "$f" .c).o || exit 2'''),
    (r'''	if grep -q "p8x32a: lazy\|p8run: lazy" "$o.log"; then echo "LAZY $1: $(grep "p8x32a: lazy\|p8run: lazy" "$o.log" | head -3)"; fail=$((fail + 1)); fi
''',
     r'''	if grep -q "p8x32a: lazy\|p8run: lazy" "$o.log"; then echo "LAZY $1: $(grep "p8x32a: lazy\|p8run: lazy" "$o.log" | head -3)"; fail=$((fail + 1)); fi
	if grep -q "p8x32a: time order" "$o.log"; then echo "TIME ORDER $1: $(grep "p8x32a: time order" "$o.log")"; fail=$((fail + 1)); fi
'''),
    (r'''TOOLS=$TOOLS ./mutate_lazy.sh || fail=$((fail + 1))
''',
     r'''TOOLS=$TOOLS ./mutate_lazy.sh || fail=$((fail + 1))
# mutations of the time order, the scheduler's keys, the translator's restart and special sources
TOOLS=$TOOLS ./mutate_core.sh || fail=$((fail + 1))
'''),
])

open('tests/pinheck/p8x32a/mutate_core.sh', 'w').write(r'''#!/bin/sh
# Mutations of the core that the RTL cases must catch: each mutant p8run (time-order checked; translated where the
# mutation is in the translator's path) must differ from the RTL, stop on a time-order fault, or miss a chip test's
# EXPECT-JITVAR, on one of chip/*.spin and gen.py's programs (SEEDS of each kind, default 100).
set -u
cd "$(dirname "$0")" || exit 2
TOOLS=${TOOLS:-$PWD/build/tools}
SEEDS=${SEEDS:-100}
OPENSPIN=$TOOLS/openspin/build/openspin
P1RTL=$TOOLS/p1rtl/p1rtl
CORE=../../../src/cpu/p8x32a
DEV=../../../src/wpc/pinheck
ASMJIT=../../../ext/asmjit
JF="-O2 -std=c++17 -DASMJIT_STATIC -DASMJIT_NO_FOREIGN -DASMJIT_NO_UJIT -I$ASMJIT"
B=build/mutcore
mkdir -p $B/case $B/obj build/asmjit
rm -rf $B/gen
python3 gen.py --out $B/gen --count "$SEEDS" --first 700001 --hubflags --workers 0.3
cases="$(ls chip/*.spin) $(ls $B/gen/*.spin)"
for f in $cases; do
	o=$B/case/$(basename "$f" .spin)
	[ -s "$o.rtl" ] && [ "$o.rtl" -nt "$f" ] && continue
	$OPENSPIN -q "$f" -o "$o.binary" > /dev/null 2>&1 || { echo "COMPILE FAIL $f"; exit 2; }
	python3 mkrom.py "$o.binary" "$o.rom" "$o.ram"
	$P1RTL -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 -dump "$o.rtlhub" > "$o.rtl"
done
if [ ! -f build/asmjit/libasmjit.a ]; then
	for f in $ASMJIT/asmjit/core/*.cpp $ASMJIT/asmjit/x86/*.cpp $ASMJIT/asmjit/support/*.cpp; do
		c++ $JF -c "$f" -o build/asmjit/$(basename "$f" .cpp).o || exit 2
	done
	ar rcs build/asmjit/libasmjit.a build/asmjit/*.o || exit 2
fi
fail=0
# mutant NAME FILE FROM TO JIT: the core's FILE with FROM replaced by TO; JIT 1 builds p8run translated
mutant() {
	name=$1 file=$2 jit=$5
	cp $CORE/p8x32a.c $CORE/p8x32ajit.cpp $B/
	python3 - "$3" "$4" $CORE/$file $B/$file <<'PY' || { echo "MUTANT $name: no match"; fail=$((fail + 1)); return; }
import sys
frm, to, src, dst = sys.argv[1:]
s = open(src).read()
if frm and s.count(frm) != 1: sys.exit(1)
open(dst, 'w').write(s.replace(frm, to) if frm else s)
PY
	if [ "$jit" = 1 ]; then
		c++ $JF -I$CORE -c $B/p8x32ajit.cpp -o $B/obj/jit.o || exit 2
		for f in run.c $B/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c; do
			cc -O2 -std=c99 -DP8X32A_JIT -DP8X32A_CHECK -I$CORE -I$DEV -c "$f" -o $B/obj/$(basename "$f" .c).o || exit 2
		done
		c++ -o $B/p8run $B/obj/*.o build/asmjit/libasmjit.a -lz -lpthread || exit 2
	else
		cc -O2 -std=c99 -DP8X32A_CHECK -I$CORE -I$DEV -o $B/p8run run.c $B/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
	fi
	bad=0 first=
	for f in $cases; do
		o=$B/case/$(basename "$f" .spin)
		jv=$(sed -n "s/^' EXPECT-JITVAR: //p" "$f")
		# a mutant that loops for ever is caught too
		timeout -k 5 20 ./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 ${jv:+-jitvar} -dump "$o.mhub" > "$o.m" 2> "$o.mlog"
		if ! cmp -s "$o.rtl" "$o.m" || ! cmp -s "$o.rtlhub" "$o.mhub" || grep -q "time order" "$o.mlog" ||
		   { [ -n "$jv" ] && [ "$jit" = 1 ] && ! grep -qxF "p8run: $jv block lookups left to the interpreter" "$o.mlog"; }; then
			bad=$((bad + 1)); [ -n "$first" ] || first=$(basename "$f")
		fi
	done
	if [ "${name%-*}" = none ]; then
		if [ $bad = 0 ]; then echo "unmutated core ($name): all cases match"; else echo "UNMUTATED CORE ($name) differs on $bad cases (first $first)"; fail=$((fail + 1)); fi
	elif [ $bad = 0 ]; then echo "MUTANT SURVIVED $name"; fail=$((fail + 1))
	else echo "mutant $name caught by $bad cases (first $first)"; fi
}
mutant none-interpreted p8x32a.c '' '' 0
mutant none-translated p8x32a.c '' '' 1
# a pin read that sends the pins ahead of the other cogs (the cause of Plan 12b's lost bits)
mutant ina-ahead p8x32a.c '	if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);
	flush(p, t);' '	if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);
	flush(p, t + 64);' 0
exit $((fail != 0))
''')
os.chmod('tests/pinheck/p8x32a/mutate_core.sh', 0o755)
EOF
```

- [ ] **Step 2: Run them to see what they catch now**

```sh
nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
```

Expected (about a minute with the RTL references; without the check the mutant shows only where a trace differs):

```text
unmutated core (none-interpreted): all cases match
unmutated core (none-translated): all cases match
mutant ina-ahead caught by 7 cases (first event_order.spin)
```

- [ ] **Step 3: The check**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''static uint32_t regval(const p8x32a_reg *r, uint64_t t) { return t >= r->at ? r->cur : r->prev; }
''',
     r'''static uint32_t regval(const p8x32a_reg *r, uint64_t t) { return t >= r->at ? r->cur : r->prev; }

#ifdef P8X32A_CHECK
/* test builds: the emulation's time order. A pin change must not come after the pins were sent past its time, a hub
   write not before a read or write of its bytes that already ran (the lazy cog's reads, behind by design, excepted) */
static uint64_t chk_rd[65536], chk_wr[65536];
static void chk_fail(const char *what, uint64_t t, uint64_t past)
{
	fprintf(stderr, "p8x32a: time order: %s at %llu after %llu\n", what, (unsigned long long)t, (unsigned long long)past);
	abort();
}
static void chk_read(uint32_t a, unsigned sz, uint64_t t)
{
	unsigned k;
	for (k = 0; k < sz; k++)
		if (t > chk_rd[(a + k) & 0xFFFF]) chk_rd[(a + k) & 0xFFFF] = t;
}
static void chk_write(uint32_t a, unsigned sz, uint64_t t)
{
	unsigned k;
	for (k = 0; k < sz; k++) {
		uint32_t b = (a + k) & 0xFFFF;
		if (t < chk_rd[b]) chk_fail("hub write after a later read", t, chk_rd[b]);
		if (t < chk_wr[b]) chk_fail("hub write after a later write", t, chk_wr[b]);
		chk_wr[b] = t;
	}
}
#define CHK_READ(a, sz, t) chk_read(a, sz, t)
#define CHK_WRITE(a, sz, t) chk_write(a, sz, t)
#else
#define CHK_READ(a, sz, t) ((void)0)
#define CHK_WRITE(a, sz, t) ((void)0)
#endif
'''),
    (r'''	int k;
	if (p->sleepers) loop_notify(p, t, pins);
	for (k = 0; k < p->npend; k++)''',
     r'''	int k;
#ifdef P8X32A_CHECK
	if (t < p->flushed) chk_fail("pin change sent", t, p->flushed);
#endif
	if (p->sleepers) loop_notify(p, t, pins);
	for (k = 0; k < p->npend; k++)'''),
    (r'''	for (k = 0; k < l->nhub; k++)
		if (loop_hub_now(p, l, k) != l->hub_v[k]) return 0;
	return 1;''',
     r'''	for (k = 0; k < l->nhub; k++) {
		CHK_READ(l->hub_a[k], l->hub_n[k], t);
		if (loop_hub_now(p, l, k) != l->hub_v[k]) return 0;
	}
	return 1;'''),
    (r'''		w = rd32(p, a);
		if (!c->run && (a & 0x8000)) w = unscramble(w);
		if (!FWR(c->i) && a < 0x8000) {
			uint32_t v = c->d;
			unsigned sz = op == 2 ? 4 : op == 1 ? 2 : 1, ha = a & ~(sz - 1), k;
			int changed = 0;''',
     r'''		w = rd32(p, a);
		if (!c->run && (a & 0x8000)) w = unscramble(w);
		if (FWR(c->i) || !c->run) CHK_READ(c->run ? a & ~((op == 2 ? 4u : op == 1 ? 2u : 1u) - 1) : a & 0xFFFC, c->run ? (op == 2 ? 4 : op == 1 ? 2 : 1) : 4, h);
		if (!FWR(c->i) && a < 0x8000) {
			uint32_t v = c->d;
			unsigned sz = op == 2 ? 4 : op == 1 ? 2 : 1, ha = a & ~(sz - 1), k;
			int changed = 0;
			CHK_WRITE(ha, sz, h);'''),
    (r'''	unsigned op = OP(i), dst = DST(i);
	if (!FWR(i) && a < 0x8000) {
		unsigned sz = op == 2 ? 4 : op == 1 ? 2 : 1, ha = a & ~(sz - 1), k;
		int changed = 0;''',
     r'''	unsigned op = OP(i), dst = DST(i);
	if (FWR(i) && !(p->lz_on && n == p->lz)) CHK_READ(a & ~((op == 2 ? 4u : op == 1 ? 2u : 1u) - 1), op == 2 ? 4 : op == 1 ? 2 : 1, h);
	if (!FWR(i) && a < 0x8000) {
		unsigned sz = op == 2 ? 4 : op == 1 ? 2 : 1, ha = a & ~(sz - 1), k;
		int changed = 0;
		CHK_WRITE(ha, sz, h);'''),
])
EOF
```

- [ ] **Step 4: The mutant is caught by many more cases**

```sh
nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
```

Expected (under a minute):

```text
unmutated core (none-interpreted): all cases match
unmutated core (none-translated): all cases match
mutant ina-ahead caught by 26 cases (first clk_idle.spin)
```

- [ ] **Step 5: Commit**

```sh
git add src/cpu/p8x32a/p8x32a.c tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/mutate_core.sh
git commit -m "p8x32a: a time-order check in the test builds (pins sent ahead, hub accesses out of order) and its mutant"
```

### Task 3: The translator after a restart

**Files:**
- Create: `tests/pinheck/p8x32a/chip/jit_restart.spin`
- Modify: `tests/pinheck/p8x32a/run.c`, `tests/pinheck/p8x32a/check.sh`, `tests/pinheck/p8x32a/mutate_core.sh`, `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`

**Interfaces:**
- Consumes: `jit_get`, `jit_drop`, `jvar`, `restart()`; Task 2's `mutate_core.sh`.
- Produces: `uint64_t p8x32a.jit_refused` (block lookups left to the interpreter because a slot's word changed beyond its S and D fields); `p8run -jitvar` prints `p8run: N block lookups left to the interpreter`; check.sh: `' EXPECT-JITVAR: N`.

- [ ] **Step 1: The tests**

`chip/jit_restart.spin`: cog 1 runs program A (a loop over slot 1, then a `MOVI` that changes slot 1's opcode), and is started again on program B at the same cog addresses (slot 1 in a loop of 2,000). With the translator, B's loop must be translated: no block lookup left to the interpreter. Two mutants keep `jvar`, or keep the old blocks (the load's writes then mark the words as changing):

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/p8x32a/run.c', [
    (r'''static int have_ee, have_sd, notrace, sleeps, nolazy, lazies;''',
     r'''static int have_ee, have_sd, notrace, sleeps, nolazy, lazies, jitvar;'''),
    (r'''		else if (!strcmp(argv[i], "-lazies")) lazies = 1;''',
     r'''		else if (!strcmp(argv[i], "-lazies")) lazies = 1;
		else if (!strcmp(argv[i], "-jitvar")) jitvar = 1;'''),
    (r'''	if (lazies) fprintf(stderr, "p8run: %llu lazy cogs\n", (unsigned long long)chip.lazies);''',
     r'''	if (lazies) fprintf(stderr, "p8run: %llu lazy cogs\n", (unsigned long long)chip.lazies);
	if (jitvar) fprintf(stderr, "p8run: %llu block lookups left to the interpreter\n", (unsigned long long)chip.jit_refused);'''),
])
edit('tests/pinheck/p8x32a/check.sh', [
    (r'''	journal=$(sed -n "s/^' EXPECT-JOURNAL: //p" "$1")
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 ${sleeps:+-sleeps} ${lazies:+-lazies} -dump "$o.ourhub" > "$o.our" 2> "$o.log"''',
     r'''	journal=$(sed -n "s/^' EXPECT-JOURNAL: //p" "$1")
	jitvar=$(sed -n "s/^' EXPECT-JITVAR: //p" "$1")
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 ${sleeps:+-sleeps} ${lazies:+-lazies} ${jitvar:+-jitvar} -dump "$o.ourhub" > "$o.our" 2> "$o.log"'''),
    (r'''	if grep -q "p8x32a: lazy\|p8run: lazy" "$o.log"; then echo "LAZY $1: $(grep "p8x32a: lazy\|p8run: lazy" "$o.log" | head -3)"; fail=$((fail + 1)); fi''',
     r'''	if grep -q "p8x32a: lazy\|p8run: lazy" "$o.log"; then echo "LAZY $1: $(grep "p8x32a: lazy\|p8run: lazy" "$o.log" | head -3)"; fail=$((fail + 1)); fi
	# EXPECT-JITVAR: n = the translator left n block lookups to the interpreter for changed words
	if [ -n "$jitvar" ] && ! grep -qxF "p8run: $jitvar block lookups left to the interpreter" "$o.log"; then
		echo "JITVAR $1: $(grep -F 'left to the interpreter' "$o.log"), expected $jitvar"; fail=$((fail + 1)); fi'''),
])
open('tests/pinheck/p8x32a/chip/jit_restart.spin', 'w').write("' EXPECT-JITVAR: 0\n" + r'''PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 0 runs program A in cog 1 (it rewrites the opcode of slot 1 after running it), then program B in cog 1 at the
' same cog addresses (slot 1 in a loop); B's sum at $6000
entry   mov     x, wk
        shl     x, #2
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d30000
        waitcnt t, #0
        mov     x, wk
        add     x, #32
        shl     x, #2
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d60000
        waitcnt t, #0
        cogid   x
        cogstop x
wk      long    $C0DEADD1
d30000  long    30000
d60000  long    60000
x       long    0
t       long    0
        long    $C0DEE0D0
        long    $C0DE0B0B
' program A, 8 longs
        org     0
pa      mov     k, #50
pax     add     acc, #1
        djnz    k, #pax
        movi    pax, #%011000_001
        cogid   k
        cogstop k
k       long    0
acc     long    0
' program B
        org     0
pb      mov     k2, d2000
pbx     add     acc2, #3
        djnz    k2, #pbx
        wrlong  acc2, rb
        cogid   k2
        cogstop k2
d2000   long    2000
rb      long    $6000
k2      long    0
acc2    long    0
''')
edit('tests/pinheck/p8x32a/mutate_core.sh', [
    (r"""exit $((fail != 0))
""",
     r"""# a reloaded cog's changed words kept, its blocks kept (the load's writes mark its words as changing)
mutant restart-jvar p8x32a.c '		memset(p->jvar[n], 0, sizeof(p->jvar[n]));' '' 1
mutant restart-drop p8x32a.c '		jit_drop(p, n);
		memset(p->jvar[n]' '		memset(p->jvar[n]' 1
exit $((fail != 0))
"""),
])
EOF
```

- [ ] **Step 2: Run them to see them fail**

```sh
nice -n 15 tests/pinheck/p8x32a/mutate_core.sh 2>&1 | grep -m3 -E "error|MUTANT|unmutated"
```

Expected: `p8run` does not build (no counter yet), so the script stops:

```text
run.c:344:116: error: ‘p8x32a’ has no member named ‘jit_refused’
```

- [ ] **Step 3: The reset**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.h', [
    (r'''	uint32_t jvar[8][512];
''',
     r'''	uint32_t jvar[8][512];
	uint64_t jit_refused; /* block lookups left to the interpreter: the slot's word changed beyond its S and D fields */
'''),
])
edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''	unsigned k;
	if (var[a] & ~P8X32A_JDYN) return NULL;''',
     r'''	unsigned k;
	if (var[a] & ~P8X32A_JDYN) { p->jit_refused++; return NULL; }'''),
    (r'''	p8x32a_cog *c = &p->cog[n];
	loop_reset(&p->loop[n]);
	c->p = 0;''',
     r'''	p8x32a_cog *c = &p->cog[n];
	loop_reset(&p->loop[n]);
	/* new code: the translator forgets the old code's blocks (the load's writes would mark their words as changing)
	   and which of its words changed */
	if (p->jit_build) {
		jit_drop(p, n);
		memset(p->jvar[n], 0, sizeof(p->jvar[n]));
	}
	c->p = 0;'''),
])
EOF
```

- [ ] **Step 4: The mutants are caught**

```sh
nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
```

Expected (under a minute):

```text
unmutated core (none-interpreted): all cases match
unmutated core (none-translated): all cases match
mutant ina-ahead caught by 26 cases (first clk_idle.spin)
mutant restart-jvar caught by 1 cases (first jit_restart.spin)
mutant restart-drop caught by 1 cases (first jit_restart.spin)
```

- [ ] **Step 5: The Jetsons byte-identical, and faster**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12c.log 2>&1; tail -1 build/sdl3pinmame/12c.log
jet env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video | grep -E 'bench video opt|determinism|FAIL'
```

Expected (about 3 minutes; compare Task 1's count):

```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/jetsons.nv
bench video opt: 0.763x over 24.0 s emulated (31.4 s wall), whole run 15.4 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.562x, load 2.07
determinism video: identical uart.log frames.bin snd.wav nvram/jetsons.nv
```

- [ ] **Step 6: Commit**

```sh
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c tests/pinheck/p8x32a/run.c tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/mutate_core.sh tests/pinheck/p8x32a/chip/jit_restart.spin
git commit -m "p8x32a: a cog started again on new code is translated again (the old code's changed words forgotten)"
```

### Task 4: Cached scheduling keys

**Files:**
- Modify: `tests/pinheck/p8x32a/gen.py`, `tests/pinheck/p8x32a/check.sh`, `tests/pinheck/p8x32a/mutate_core.sh`, `src/cpu/p8x32a/p8x32a.c`

**Interfaces:**
- Consumes: `sched_gen` (every change one cog makes to another cog's next event counts in it); Task 2's `mutate_core.sh`.
- Produces: `static uint64_t ev_key(const p8x32a_cog *c, int n)`; `gen.py --cogs N` (N cogs on shared hub bytes: hub reads and writes, `INA`, `OUTA` on P8, `WAITCNT`, CNT and PAR sources, locks, a register jump, a rewritten jump target and hub-read destination, sleepers on a flag byte or P8); check.sh's `$B/rtl/multi` set.

- [ ] **Step 1: The tests**

Three-cog random programs join the suite and `mutate_core.sh`; two mutants leave the cog that ran with its old key when its next event is a hub operation, or wake a sleeper without counting it in `sched_gen`:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/p8x32a/gen.py', [
    (r'''import argparse
import os
import random
''',
     r'''import argparse
import os
import random
import re
'''),
    (r'''def main():
    ap = argparse.ArgumentParser()''',
     r'''def multi(r, hubflags, n, cogs):
    """Several cogs on shared hub bytes (RTL only): cog 0 starts cogs 1..cogs-1 on a second program,
    each with PAR $6040 * k and its registers mixed with PAR. Their bodies mix hub reads and writes on $6200-$62FF
    (each cog its own share), INA reads, OUTA writes on P8 (cog 0 drives it), short WAITCNTs, CNT and PAR sources,
    lock operations, a jump
    through a register, a jump whose target and a hub read whose destination the code rewrites; the started cogs also
    wait in idle loops (sleep) for their flag byte at $6100 + k, which cog 0 writes now and then and at its end, or for
    P8, which cog 0 toggles and leaves high."""
    names = re.compile(r'(?<![$\w])(r\d+|h\d|L\d+|fl|t|ptr|done|one|zero|hmask|hbase|entry|lk|pin|tgt|myf|f6100|J\d+|D\d+|W\d+)\b')

    def body(g, cog0):
        hub = g.r.choice([4, 8, 12])
        for _ in range(n):
            k = g.r.randrange(24)
            if k < 2:
                e = g.r.randrange(3)
                if e == 0:
                    g.emit('%-7s %s, ina%s' % (g.r.choice(['mov', 'and', 'xor', 'add']), g.r.choice(REGS), g.r.choice(['', ' wz', ' wc'])))
                elif e == 1:
                    g.emit('%-7s outa, %s' % (g.r.choice(['xor', 'or', 'andn']), 'pin'))
                else:
                    g.emit('mov     t, cnt')
                    g.emit('add     t, #%d' % g.r.randrange(9, 40))
                    g.emit('waitcnt t, #0')
            elif k < 3:
                # a lock: new, set or clear (their C comes from the hub)
                g.emit('%-7s lk%s' % (g.r.choice(['locknew', 'lockset', 'lockclr']), g.r.choice([' wc', ''])))
                g.emit('and     lk, #7')
            elif k < 4:
                lab, lab2 = 'L%d' % g.label, 'L%d' % (g.label + 1)
                g.label += 2
                m = g.r.randrange(3)
                if m == 0:
                    # a jump through a register
                    g.emit('mov     tgt, #%s' % lab)
                    g.emit('jmp     tgt')
                    g.emit(g.alu())
                    g.out.append(lab)
                elif m == 1:
                    # a jump whose target the code rewrites
                    g.emit('movs    J%d, #%s' % (g.label, lab))
                    g.emit(g.alu())
                    g.out.append('J%d     jmp     #%s' % (g.label, lab2))
                    g.emit(g.alu())
                    g.out.append(lab2)
                    g.emit(g.alu())
                    g.out.append(lab)
                else:
                    # a hub read whose destination the code rewrites
                    g.emit('movd    D%d, #%s' % (g.label, g.r.choice(REGS)))
                    g.emit(g.alu())
                    g.out.append('D%d     rdlong  0-0, %s' % (g.label, g.r.choice(HUB)))
                    g.out.append(lab)
                    g.out.append(lab2)
            elif k < 5 and cog0:
                # a started cog's flag byte
                g.emit('mov     t, #%d' % g.r.randrange(1, cogs))
                g.emit('add     t, f6100')
                g.emit('wrbyte  one, t')
            elif k < 5 and g.r.random() < 0.3:
                lab = 'W%d' % g.label
                g.label += 1
                if g.r.random() < 0.5:
                    # sleep until the flag byte is not zero
                    g.emit('wrbyte  zero, myf')
                    g.out.append(lab)
                    g.emit('rdbyte  t, myf')
                    g.emit('tjz     t, #%s' % lab)
                else:
                    # sleep until P8 is high
                    g.out.append(lab)
                    g.emit('test    pin, ina wz')
                    g.emit('if_z    jmp     #%s' % lab)
            elif k < 6:
                # CNT and PAR as sources
                g.emit('%-12s %-7s %s, %s%s' % (g.cond(), g.r.choice(['add', 'xor', 'sub', 'mov']), g.r.choice(REGS),
                                                g.r.choice(['cnt', 'par']), g.flags(True)))
            elif k < 16 - hub:
                g.emit(g.alu())
            elif k < 16:
                g.hub()
            else:
                g.branch()

    def block(head, tail, cog0):
        g = Gen(r, hubflags)
        g.out.append('entry')
        for x in head:
            g.emit(x)
        g.emit('test    %s, #%d wz, wc' % (r.choice(REGS), r.randrange(512)))
        body(g, cog0)
        if cog0:
            # the sleepers' flags and pin: set at the end
            g.emit('mov     t, f6100')
            for _ in range(cogs):
                g.emit('wrbyte  one, t')
                g.emit('add     t, #1')
            g.emit('or      outa, pin')
        g.emit('muxc    fl, #1')
        g.emit('muxz    fl, #2')
        for reg in REGS + ['fl']:
            g.emit('wrlong  %s, ptr' % reg)
            g.emit('add     ptr, #4')
        g.emit('wrlong  one, done')
        g.emit('cogid   ptr')
        g.emit('cogstop ptr')
        for reg in REGS:
            g.out.append('%-7s long    $%08X' % (reg, r.getrandbits(32)))
        for k in range(4):
            g.out.append('%-7s long    $%04X' % ('h%d' % k, 0x6200 + r.randrange(0, 0x100, 4) + k))
        g.out += ['fl      long    0', 't       long    0', 'one     long    1', 'zero    long    0', 'hmask   long    $FF',
                  'hbase   long    $6200', 'lk      long    0', 'pin     long    $100', 'tgt     long    0', 'myf     long    0',
                  'f6100   long    $6100'] + tail
        return g.out

    start = ['mov     x, wk', 'shl     x, #2', 'or      x, #%1000', 'mov     cn, #%d' % (cogs - 1), 'mov     cpar, #$40',
             ':start', 'mov     y, cpar', 'add     y, par0', 'shl     y, #16', 'or      y, x', 'coginit y', 'add     cpar, #$40',
             'djnz    cn, #:start', 'or      dira, pin']
    a = block(start, ['ptr     long    $6000', 'done    long    $6FFC', 'x       long    0', 'y       long    0',
                      'cn      long    0', 'cpar    long    0', 'par0    long    $6000', 'wk      long    $C0DEADD1'], True)
    a = [('        ' + x[1:].strip() if x == ':start' else x) for x in a]
    a = [(':start' if x.strip() == ':start' else x) for x in a]
    mix = ['xor     %s, par' % reg for reg in REGS] + ['mov     ptr, par', 'mov     done, par', 'add     done, #$3C',
                                                    'mov     myf, par', 'shr     myf, #6', 'and     myf, #7', 'add     myf, f6100']
    w = block(mix, ['ptr     long    0', 'done    long    0'], False)
    w = [names.sub(lambda m: 'w' + m.group(1), x) for x in w]
    out = ['PUB main', 'DAT', '        long    $C0DE5EED', '        org     0'] + a
    out += ['        long    $C0DEE0D0', '        long    $C0DE0B0B', '        org     0'] + w
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()'''),
    (r'''    ap.add_argument('--workers', type=float, default=0, help='share of programs with a scan-cog-shaped cog 1 (RTL only)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for seed in range(a.first, a.first + a.count):''',
     r'''    ap.add_argument('--workers', type=float, default=0, help='share of programs with a scan-cog-shaped cog 1 (RTL only)')
    ap.add_argument('--cogs', type=int, default=0, help='programs of this many cogs on shared hub bytes (RTL only)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if a.cogs:
        for seed in range(a.first, a.first + a.count):
            with open(os.path.join(a.out, 'm%05d.spin' % seed), 'w') as f:
                f.write(multi(random.Random(seed), a.hubflags, a.length, a.cogs))
        return
    for seed in range(a.first, a.first + a.count):'''),
])

edit('tests/pinheck/p8x32a/check.sh', [
    (r"""	python3 gen.py --out $B/spin/rand --count "$SEEDS"
	for f in $B/spin/rand/*.spin; do spin_case "$f" $B/spin/rand; done
fi""",
     r"""	python3 gen.py --out $B/spin/rand --count "$SEEDS"
	for f in $B/spin/rand/*.spin; do spin_case "$f" $B/spin/rand; done
	# three cogs on shared hub bytes, sleepers, locks, rewritten code, CNT and PAR sources
	rm -rf $B/rtl/multi
	python3 gen.py --out $B/rtl/multi --count "$SEEDS" --cogs 3 --hubflags
	for f in $B/rtl/multi/*.spin; do rtl_case "$f" $B/rtl/multi; done
fi"""),
])
edit('tests/pinheck/p8x32a/mutate_core.sh', [
    (r"""python3 gen.py --out $B/gen --count "$SEEDS" --first 700001 --hubflags --workers 0.3
cases="$(ls chip/*.spin) $(ls $B/gen/*.spin)"
""",
     r"""python3 gen.py --out $B/gen --count "$SEEDS" --first 700001 --hubflags --workers 0.3
python3 gen.py --out $B/gen --count "$SEEDS" --first 800001 --cogs 3 --hubflags
cases="$(ls chip/*.spin) $(ls $B/gen/*.spin)"
"""),
    (r"""exit $((fail != 0))
""",
     r"""# the scheduler's key of the cog that ran left as it was when its next event is a hub operation; a sleeper woken
# without sched_gen counting it
mutant keys-ran p8x32a.c '		key[best] = ev_key(b, best);' '		if (b->ev != EV_HUB) key[best] = ev_key(b, best);' 0
mutant keys-gen p8x32a.c '(pins & p->loop[n].wake) && t < p->cog[n].ev_t) { p->cog[n].ev_t = t; p->sched_gen++; }' \
	'(pins & p->loop[n].wake) && t < p->cog[n].ev_t) { p->cog[n].ev_t = t; }' 0
exit $((fail != 0))
"""),
])
EOF
```

- [ ] **Step 2: Run them to see them fail**

```sh
nice -n 15 tests/pinheck/p8x32a/mutate_core.sh | grep -E "MUTANT|unmutated"
```

Expected (about 4 minutes with the new RTL references): the first mutant has no code to change yet:

```text
unmutated core (none-interpreted): all cases match
unmutated core (none-translated): all cases match
MUTANT keys-ran: no match
```

- [ ] **Step 3: The cached keys**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''void p8x32a_run_until(p8x32a *p, uint64_t t)
{
	p->horizon = t;
	for (;;) {
		int n, best;
		uint64_t bk = P8X32A_NEVER, bk2 = P8X32A_NEVER;
		unsigned gen;
		p8x32a_cog *b;
		/* the next event: earliest, then hub events, then the lowest cog; bk2 is the one after it */
		for (n = 0; n < 8; n++) {
			const p8x32a_cog *c = &p->cog[n];
			uint64_t e = c->ev_t < ((uint64_t)1 << 59) ? c->ev_t : (uint64_t)1 << 59;
			uint64_t k = e << 4 | (uint64_t)(c->ev != EV_HUB) << 3 | (uint64_t)n;
			if (c->ev == EV_NONE) continue;
			if (k < bk) { bk2 = bk; bk = k; }
			else if (k < bk2) bk2 = k;
		}''',
     r'''/* a cog's scheduling key: earliest event first, then hub events, then the lowest cog (P8X32A_NEVER: none) */
static uint64_t ev_key(const p8x32a_cog *c, int n)
{
	uint64_t e = c->ev_t < ((uint64_t)1 << 59) ? c->ev_t : (uint64_t)1 << 59;
	return c->ev == EV_NONE ? P8X32A_NEVER : e << 4 | (uint64_t)(c->ev != EV_HUB) << 3 | (uint64_t)n;
}

void p8x32a_run_until(p8x32a *p, uint64_t t)
{
	uint64_t key[8];
	unsigned kgen = p->sched_gen - 1;
	p->horizon = t;
	for (;;) {
		int n, best;
		uint64_t bk = P8X32A_NEVER, bk2 = P8X32A_NEVER;
		unsigned gen;
		p8x32a_cog *b;
		/* the keys change only for the cog that ran, unless sched_gen counts a change to another cog's event */
		if (kgen != p->sched_gen) {
			for (n = 0; n < 8; n++) key[n] = ev_key(&p->cog[n], n);
			kgen = p->sched_gen;
		}
		/* the next event, and bk2 the one after it */
		for (n = 0; n < 8; n++) {
			uint64_t k = key[n];
			if (k < bk) { bk2 = bk; bk = k; }
			else if (k < bk2) bk2 = k;
		}'''),
    (r'''			if ((e << 4 | (uint64_t)(b->ev != EV_HUB) << 3 | (uint64_t)best) < bk2) goto again;
		}
	}''',
     r'''			if ((e << 4 | (uint64_t)(b->ev != EV_HUB) << 3 | (uint64_t)best) < bk2) goto again;
		}
		key[best] = ev_key(b, best);
	}'''),
])
EOF
```

- [ ] **Step 4: The mutants are caught**

```sh
nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
```

Expected (about 4 minutes):

```text
unmutated core (none-interpreted): all cases match
unmutated core (none-translated): all cases match
mutant ina-ahead caught by 96 cases (first clk_idle.spin)
mutant restart-jvar caught by 1 cases (first jit_restart.spin)
mutant restart-drop caught by 1 cases (first jit_restart.spin)
mutant keys-ran caught by 73 cases (first cogs.spin)
mutant keys-gen caught by 1 cases (first wake_order.spin)
```

- [ ] **Step 5: Commit**

```sh
git add src/cpu/p8x32a/p8x32a.c tests/pinheck/p8x32a/gen.py tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/mutate_core.sh
git commit -m "p8x32a: the scheduler keeps each cog's key until it or sched_gen changes; three-cog random programs"
```

### Task 5: PAR and CNT translated

**Files:**
- Create: `tests/pinheck/p8x32a/chip/jit_cnt.spin`
- Modify: `tests/pinheck/p8x32a/mutate_core.sh`, `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32ajit.cpp`

**Interfaces:**
- Consumes: `supported()`, `Emit::insn` (`T2` the block's first instruction's time, `4 * k` the slot's), `run_local`'s block state.
- Produces: `p8x32a_jst.par` (`(ptr >> 14) << 2`), `p8x32a_jst.cnt_base`; a fixed word with a PAR or CNT source (not a jump) translated.

- [ ] **Step 1: The tests**

`chip/jit_cnt.spin` adds PAR and CNT to a translated loop (some reads conditional) and ends in a loop that reads CNT and changes nothing until CNT passes a time (`EXPECT-SLEEPS: 0`); two mutants read CNT four cycles late or PAR unshifted:

```sh
python3 - <<'EOF'
open('tests/pinheck/p8x32a/chip/jit_cnt.spin', 'w').write("' EXPECT-SLEEPS: 0\n" + r'''PUB main
DAT
        long    $C0DE5EED
        org     0
' CNT and PAR as sources in translated runs (some conditional), then a loop that reads CNT and changes nothing until
' CNT passes a time (it never sleeps); results at $6000
entry   mov     k, #40
:l      add     acc, cnt
        sub     acc, par
        test    k, #3 wc
  if_c  xor     acc, cnt
  if_nc add     acc2, cnt
        rol     acc2, #3
        djnz    k, #:l
        mov     x, cnt
        add     x, d3000
:w      cmp     x, cnt wc
  if_nc jmp     #:w
        mov     y, cnt
        sub     y, x
        wrlong  acc, r0
        wrlong  acc2, r1
        wrlong  y, r2
        cogid   k
        cogstop k
d3000   long    3000
r0      long    $6000
r1      long    $6004
r2      long    $6008
k       long    0
acc     long    $1234
acc2    long    0
x       long    0
y       long    0
        long    $C0DEE0D0
''')



def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/p8x32a/mutate_core.sh', [
    (r"""exit $((fail != 0))
""",
     r"""# a translated CNT four cycles late; PAR not shifted
mutant jit-cnt p8x32ajit.cpp '(int)(4 * k)));
				a.sub(x86::rax' '(int)(4 * k + 4)));
				a.sub(x86::rax' 1
mutant jit-par p8x32a.c 'st.par = (c->ptr >> 14) << 2;' 'st.par = c->ptr << 2;' 1
exit $((fail != 0))
"""),
])
EOF
```

- [ ] **Step 2: Run them to see them fail**

```sh
nice -n 15 tests/pinheck/p8x32a/mutate_core.sh | grep -E "MUTANT|unmutated"
```

Expected (about 4 minutes): the mutants have no code to change yet:

```text
unmutated core (none-interpreted): all cases match
unmutated core (none-translated): all cases match
MUTANT jit-cnt: no match
MUTANT jit-par: no match
```

- [ ] **Step 3: PAR and CNT in translated blocks**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.h', [
    (r'''	const uint8_t *jmap;   /* hub longs with a journal entry (p8x32a.jmap): a block stops before reading one */
} p8x32a_jst;''',
     r'''	const uint8_t *jmap;   /* hub longs with a journal entry (p8x32a.jmap): a block stops before reading one */
	uint32_t par;          /* the cog's PAR */
	uint64_t cnt_base;     /* CNT is the time less this */
} p8x32a_jst;'''),
])
edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''	st.ram = ram;
	st.code = p->jcode[n];
	st.tab = p->jblk[n];
	st.loop = l;''',
     r'''	st.ram = ram;
	st.code = p->jcode[n];
	st.tab = p->jblk[n];
	st.loop = l;
	st.par = (c->ptr >> 14) << 2;
	st.cnt_base = p->cnt_base;'''),
])
edit('src/cpu/p8x32a/p8x32ajit.cpp', [
    (r'''// a fixed word run_local() would run as a local instruction; with outa, also a write to OUTA or a hub read without
// WC (a lazy cog's)
bool supported(uint32_t i, int outa)
{
	unsigned op = op_of(i);
	if (outa && op <= 2) return fwr(i) && !fwc(i) && dst_of(i) < 0x1F0 && (fim(i) || src_of(i) < 0x1F0);
	if (!op_ok(op)) return false;
	if (!fim(i) && src_of(i) >= 0x1F0) return false;''',
     r'''inline bool is_jump(unsigned op) { return op == OP_JMP || op >= OP_DJNZ; }

// a fixed word run_local() would run as a local instruction (PAR and CNT the only special sources); with outa, also
// a write to OUTA or a hub read without WC (a lazy cog's)
bool supported(uint32_t i, int outa)
{
	unsigned op = op_of(i);
	if (outa && op <= 2) return fwr(i) && !fwc(i) && dst_of(i) < 0x1F0 && (fim(i) || src_of(i) < 0x1F0);
	if (!op_ok(op)) return false;
	if (!fim(i) && src_of(i) >= 0x1F0 && (src_of(i) > 0x1F1 || is_jump(op))) return false;'''),
    (r'''	return true;
}

inline bool is_jump(unsigned op) { return op == OP_JMP || op >= OP_DJNZ; }
''',
     r'''	return true;
}
'''),
    (r'''		if (prefetch_next) a.mov(NEXTW, cog(pc));
		if (cond == 0) goto end;''',
     r'''		if (prefetch_next) a.mov(NEXTW, cog(pc));
		if (!dyn && !imm && src_of(i) == 0x1F1) {
			// a CNT source: the cog's loop is not idle, whether the instruction runs or not
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
			a.mov(x86::byte_ptr(x86::rdi, (int)offsetof(p8x32a_loop, dirty)), 1);
		}
		if (cond == 0) goto end;'''),
    (r'''		} else {
			if (imm) a.mov(x86::ecx, src_of(i));
			else a.mov(x86::ecx, cog(src_of(i)));
			a.mov(x86::edx, cog(dst_of(i)));
		}''',
     r'''		} else {
			if (imm) a.mov(x86::ecx, src_of(i));
			else if (src_of(i) == 0x1F0) a.mov(x86::ecx, stf(offsetof(p8x32a_jst, par)));
			else if (src_of(i) == 0x1F1) {
				// CNT at this instruction's time
				a.lea(x86::rax, x86::ptr(T2, (int)(4 * k)));
				a.sub(x86::rax, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, cnt_base)));
				a.mov(x86::ecx, x86::eax);
			} else a.mov(x86::ecx, cog(src_of(i)));
			a.mov(x86::edx, cog(dst_of(i)));
		}'''),
])
EOF
```

- [ ] **Step 4: The suites pass**

```sh
flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh | tail -1
P8X32A_JIT=1 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh | grep -E 'translated|passed'
nice -n 15 tests/pinheck/display/check.sh | tail -1
```

Expected (about 30 minutes, the RTL references of a fresh worktree and both mutation scripts included; the translated run also prints `mutate_core.sh`'s translated line):

```text
p8x32a: 343 passed, 0 failed
p8run: translated
unmutated core (none-translated): all cases match
p8x32a: 343 passed, 0 failed
display: 0 failed
```

- [ ] **Step 5: America's Most Haunted byte-identical, and faster**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12c.log 2>&1; tail -1 build/sdl3pinmame/12c.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video | grep -E 'bench video opt|determinism|FAIL'
for s in PINHECK_JIT=0 PINHECK_LAZY=0; do amh env $s REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -E 'determinism short|FAIL' | sed "s/^/$s: /"; done
```

Expected (about 8 minutes; compare Task 1's count):

```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/amh.nv
bench video opt: 0.694x over 24.0 s emulated (34.5 s wall), whole run 15.2 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.582x, load 1.88
determinism video: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_JIT=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_LAZY=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
```

- [ ] **Step 6: Commit**

```sh
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32ajit.cpp tests/pinheck/p8x32a/mutate_core.sh tests/pinheck/p8x32a/chip/jit_cnt.spin
git commit -m "p8x32a: PAR and CNT sources translated"
```

### Task 6: Speed, regression, the tests bite, the documents

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`

**Interfaces:**
- Consumes: everything above; `build/reference/sdl3pinmame`; Plan 9d's `pgo.sh`.
- Produces: `build/pgo-use/sdl3pinmame`; the Result's measurements; spec §4's paragraph for Plan 12c; the roadmap's row.

- [ ] **Step 1: The profile-guided build**

```sh
flock /code/spooky_domino/work/heavy.lock flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/pgo.sh -DPLATFORM=linux -DARCH=x64 | grep -E '^pgo: |pgo training: bench'
```

Expected (about 25 minutes; it builds twice and trains on Domino's):

```text
pgo training: bench attract opt: 0.661x over 30.0 s emulated (45.4 s wall), whole run 15.3 G instructions and 1.7 s CPU per emulated s, worst 100 ms 0.329x, load 2.73
pgo training: bench video opt: 0.573x over 24.0 s emulated (41.9 s wall), whole run 16.7 G instructions and 1.9 s CPU per emulated s, worst 100 ms 0.389x, load 1.98
pgo: build/pgo-use/sdl3pinmame
```

- [ ] **Step 2: Every game against the reference, without and with the profile, with the worker thread**

```sh
for b in build/sdl3pinmame/sdl3pinmame build/pgo-use/sdl3pinmame; do
	for g in amh jet rz; do $g env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=$b flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video play | sed "s|^|$b $g: |"; done
	REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=$b flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video | sed "s|^|$b dominos: |"
done
```

Expected: every `determinism` line `identical` (America's Most Haunted's with `dmd.bin`), no `FAIL`; the speeds are the Result's table (about 30 minutes; the reference's runs are cached after the first pass). For example:

```text
build/sdl3pinmame/sdl3pinmame amh: determinism first boot: identical nvram.base/amh.nv
build/sdl3pinmame/sdl3pinmame amh: bench attract opt: 1.176x over 30.0 s emulated (25.5 s wall), whole run 13.8 G instructions and 1.2 s CPU per emulated s, worst 100 ms 1.079x, load 3.47
build/sdl3pinmame/sdl3pinmame amh: bench attract ref: 1.067x over 30.0 s emulated (28.1 s wall), whole run 15.1 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.664x, load 2.49
build/sdl3pinmame/sdl3pinmame amh: determinism attract: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
…
build/pgo-use/sdl3pinmame jet: bench play opt: 1.075x over 55.0 s emulated (51.1 s wall), whole run 15.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.563x, load 1.39
build/pgo-use/sdl3pinmame jet: bench play ref: 0.879x over 55.0 s emulated (62.6 s wall), whole run 18.5 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.489x, load 1.50
build/pgo-use/sdl3pinmame jet: determinism play: identical uart.log frames.bin snd.wav nvram/jetsons.nv
```

- [ ] **Step 3: On one thread; the switches give the same output**

```sh
for g in amh jet; do $g env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh video play | grep -E 'opt:|determinism|FAIL' | sed "s/^/$g one thread: /"; done
for s in PINHECK_JOURNAL=0 PINHECK_RF13=0; do amh env $s REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -E 'determinism short|FAIL' | sed "s/^/$s: /"; done
jet env PINHECK_JIT=0 REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -E 'determinism short|FAIL' | sed "s/^/PINHECK_JIT=0: /"
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh contention | tail -1
```

Expected (about 10 minutes):

```text
amh one thread: determinism first boot: identical nvram.base/amh.nv
amh one thread: bench video opt: 0.688x over 24.0 s emulated (34.8 s wall), whole run 15.2 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.389x, load 1.24
amh one thread: determinism video: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
amh one thread: bench play opt: 0.639x over 55.0 s emulated (86.0 s wall), whole run 17.2 G instructions and 1.4 s CPU per emulated s, worst 100 ms 0.285x, load 1.32
amh one thread: determinism play: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
jet one thread: determinism first boot: identical nvram.base/jetsons.nv
jet one thread: bench video opt: 0.782x over 24.0 s emulated (30.7 s wall), whole run 15.1 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.585x, load 1.28
jet one thread: determinism video: identical uart.log frames.bin snd.wav nvram/jetsons.nv
jet one thread: bench play opt: 0.759x over 55.0 s emulated (72.5 s wall), whole run 16.8 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.408x, load 1.54
jet one thread: determinism play: identical uart.log frames.bin snd.wav nvram/jetsons.nv
PINHECK_JOURNAL=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_RF13=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_JIT=0: determinism short: identical uart.log frames.bin snd.wav nvram/jetsons.nv
contention: ok
```

- [ ] **Step 4: Regression**

The machine checks of all four games, Domino's scripted game, libpinmame, the look, and the unit suites (the storage suite writes about 7 GB of images; delete them after it passes):

```sh
for g in amh env jet rz; do
	$g env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh | tail -1
	$g env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh | tail -1
	$g env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh | tail -1
	$g env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh | tail -1
done
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/romset_check.sh | tail -1
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/game/pinmame_game.sh | tail -2
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/audio/pinmame_audio.sh | tail -1
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_look.sh | tail -1
mkdir -p build/libpinmame-src && ln -sfn ../../cmake build/libpinmame-src/cmake && ln -sfn ../../ext build/libpinmame-src/ext && ln -sfn ../../src build/libpinmame-src/src
cp cmake/libpinmame/CMakeLists.txt build/libpinmame-src/CMakeLists.txt
cmake -S build/libpinmame-src -B build/libpinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DBUILD_STATIC=OFF > build/libpinmame-cfg.log 2>&1
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
for g in env amh jet; do $g env LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh | tail -1; done
for s in mips32 pic32mx link audio board display storage; do flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh 2>&1 | tail -1; done
D=tests/pinheck/storage/build; rm -rf "${D:?}"
```

Expected (about 40 minutes):

```text
pinmame: ok
pinmame display: ok
pinmame board: ok
pinmame sim: ok
pinmame: ok
pinmame display: ok
pinmame board: ok
pinmame sim: ok
pinmame: ok
pinmame display: ok
pinmame board: ok
pinmame sim: ok
pinmame: ok
pinmame display: ok
pinmame board: ok
pinmame sim: ok
romset: ok
game: 44 checks, 0 failures
pinmame game: ok
pinmame audio: ok
pinmame look: ok
[100%] Built target pinmame_test
pinmame vpx: ok
pinmame vpx: ok (amh: the raw DMD)
pinmame vpx: ok (jetsons: display and sound)
mips32: 208 passed, 0 failed
pic32mx: 0 failed
link: 0 failed
audio: 0 failed
board: 0 failed
display: 0 failed
storage: 0 failed
```

- [ ] **Step 5: The machine-level test bites**

A translated PAR not shifted (a `mutate_core.sh` mutant) must also fail America's Most Haunted's determinism (its cog 1 polls a mailbox at PAR; a CNT read four cycles late, the other translated mutant, leaves its timeout comparisons and the short run unchanged):

```sh
f=src/cpu/p8x32a/p8x32a.c
python3 -c "import sys; p=sys.argv[1]; s=open(p).read(); a,b=sys.argv[2],sys.argv[3]; assert s.count(a)==1; open(p,'w').write(s.replace(a,b))" $f 'st.par = (c->ptr >> 14) << 2;' 'st.par = c->ptr << 2;'
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12c.log 2>&1; tail -1 build/sdl3pinmame/12c.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short 2>&1 | grep -m3 FAIL
git checkout $f
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12c.log 2>&1; tail -1 build/sdl3pinmame/12c.log
git status --short | grep -v '^??'
```

Expected (about 5 minutes; `git status` prints nothing):

```text
[100%] Built target sdl3pinmame
DETERMINISM FAIL first boot: nvram.base/amh.nv differs from the reference build's
DETERMINISM FAIL short: uart.log differs from the reference build's
DETERMINISM FAIL short: frames.bin differs from the reference build's
Updated 1 path from the index
[100%] Built target sdl3pinmame
```

- [ ] **Step 6: The documents**

Spec §4 gains what this plan established; the roadmap gains its row and what it leaves:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md', [
    (r'''- **Proof for every stage:**''',
     r'''- **Established by Plan 12c (after Plan 12b):**
  - Plan 12b's lost display-link bits: its bound SD cog's early `INA` read with a mask of 0 took the ordinary path, which sent every pin up to the early event's time ahead of the other cogs, so their later pin writes landed behind the sent pins. The merged code has no early events; a time-order check in the test builds (`P8X32A_CHECK`: a pending point behind the sent pins, or a hub write before an already-run access of its bytes, stops the run) now makes that class of fault fail the RTL cases.
  - Taken, each exact: a cog started again on new code is translated again (its translator state was kept across reloads; after The Jetsons' Propeller reboot cog 0's Spin interpreter ran untranslated: −8% host instructions for The Jetsons); the scheduler keeps each cog's key until that cog or `sched_gen` changes; PAR and CNT sources translated (America's Most Haunted's polling cog: −5% to −8%). `mutate_core.sh` with eight mutants, three-cog random programs (`gen.py --cogs`), two chip tests.
  - Rejected after an exact prototype: hub operations before other cogs' earlier events, judged by per-address lower bounds of the cycles to each cog's next hub write, hub access or pin change (kept per address until a word they read changes) and by sleepers' earliest wake. Byte-identical, and the early operations cut the dispatches by 31%, but the cheapest checks cost more than the dispatches saved (one thread with video: The Jetsons 24.0 G and America's Most Haunted 29.5 G against 15.1 G and 16.2 G without).
  - Every output byte-identical to Plan 12b's build for the four games, on one thread, with `PINHECK_JIT=0`, `PINHECK_LAZY=0`, `PINHECK_RF13=0`, `PINHECK_JOURNAL=0` and for the profile-guided build. Speed with the worker (loads 1.1–3.5), without / with the profile: America's Most Haunted 1.18×/1.22× attract, 0.91×/0.96× video, 0.86×/0.87× play; The Jetsons 1.07×/1.21×, 0.96×/1.09×, 0.96×/1.08×; Rob Zombie 1.60×/1.77×, 1.35×/1.49×, 1.22×/1.35×; Domino's 1.38×/1.52×, 1.20×/1.31×. Real time is reached for The Jetsons with the profile-guided build in every workload, not for America's Most Haunted with video or in play.
- **Proof for every stage:**'''),
])

edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    (r'''| release | release:''',
     r'''| `2026-10-02-pinheck-12c-runahead.md` | 12c: Plan 12b's lost bits explained (an early `INA` of mask 0 sent the pins ahead); a time-order check in the test builds; the translator after a restart; cached scheduling keys; PAR and CNT translated; the run-ahead of hub operations built exact and rejected (dearer than the dispatches it saves) | Plan 12b | none | every output byte-identical to Plan 12b's build for the four games, every switch and the profile-guided build; the RTL, spinsim and QEMU suites green with `mutate_core.sh`, three-cog random programs and two chip tests; speeds in the plan's Result | written |
| release | release:'''),
    (r'''- After Plan 12b: with the worker thread,''',
     r'''- After Plan 12c: with the worker thread, The Jetsons runs 1.07× / 0.96× / 0.96× (attract, video, play) without a profile and 1.21× / 1.09× / 1.08× with the profile-guided build; America's Most Haunted 1.18× / 0.91× / 0.86× and 1.22× / 0.96× / 0.87×. What remains for America's Most Haunted with video and in play: its three Spin interpreters stop at almost every hub operation (5.8 million dispatches per emulated second); an exact run-ahead removes a third of them but its checks cost more than they save, so the next lever is a cheaper dispatch itself (`run_local`'s entry and exit and the translated blocks' entry, about 900 host instructions a dispatch).
- After Plan 12b: with the worker thread,'''),
])
EOF
```

- [ ] **Step 7: Commit**

```sh
git add docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "docs: what Plan 12c established (the lost bits, the time-order check, the translator after a restart, cached keys, PAR and CNT, the run-ahead rejected) and its roadmap row"
```

## Windows checklist

For the Windows session (VS2022 BuildTools / MSVC 14.44, `C:\code\dominos`), after Plan 12c is pushed, with the combined four-game recheck. Results come back by message; a Windows-only fix comes back as a diff with a test, applied on Linux. Never push. Muted or the silent device for every item.

1. **Build.** MSVC x64 and Win32 standalone PinMAME, VPinMAME64 and libpinmame, exactly as CI does. Expected: all build; no warning in `p8x32a.c`, `p8x32ajit.cpp`.
2. **Determinism across compilers.** The scripted Domino's game (`tests\pinheck\game`, as for Plan 9d): the score 32176700 and the four logs as on Linux.
3. **Speed.** `PinMAME.exe jetsons` and `PinMAME.exe amh`, unthrottled (`-nothrottle`), 60 s of attract mode with `PINHECK_TIME_LOG=time.log`: the last line's emulated over host seconds, against Plan 12b's build on the same machine.

## Result

Measured in the replay of this plan's text: a fresh worktree of `3f129b50` (branch `pinheck-12c`), every step run as written, every Expected line checked, on the Ryzen 7 7730U while other work shared it (one emulator at a time, `emu.lock`); the load average is printed with every speed.

- **Task 1:** the reference on one thread with video: The Jetsons 16.7 G, America's Most Haunted 16.6 G.
- **Task 2:** before the check, the `ina-ahead` mutant (pins sent 64 cycles ahead at an `INA` read) is caught by 7 cases (where a trace happens to differ); with it by 26 (96 once the three-cog programs join in Task 4).
- **Task 3:** `p8run` did not build first (no counter); then both restart mutants are caught by `chip/jit_restart.spin` (it expects 0 block lookups left to the interpreter; the mutant without the reset leaves 2,000). The Jetsons with video on one thread: 15.4 G (16.7 G), byte-identical.
- **Task 4:** the stale-key mutant is caught by 73 cases, the uncounted wake by 1 (`chip/wake_order.spin`). Three forms of the first were tried in the prototype: leaving the key as it was makes the scheduler take the same cog for ever, a key one cycle later changes almost no order (it survived), a key left as it was only when the cog's next event is a hub operation is caught.
- **Task 5:** the Propeller suite: 343 passed, 0 failed, interpreted and translated (`mutate_lazy.sh` and `mutate_core.sh` inside it, every mutant caught), the display suite passes. America's Most Haunted with video on one thread: 15.2 G (16.6 G), byte-identical; with `PINHECK_JIT=0` and `PINHECK_LAZY=0` byte-identical too.
- **Task 6, speed with the worker thread** (each workload against the reference in the same session, the profile-guided build against the same reference runs; all 30 determinism lines identical, no `FAIL`):

| Game | Attract | Video | Play | With the profile: attract, video, play | Reference: attract, video, play |
|---|---|---|---|---|---|
| America's Most Haunted | 1.176× (13.8 G) | 0.907× (15.6 G) | 0.857× (17.6 G) | 1.219×, 0.958×, 0.867× (13.1, 14.8, 16.7 G) | 1.067×, 0.654×*, 0.784× (15.1, 16.6, 19.1 G) |
| The Jetsons | 1.071× (14.7 G) | 0.955× (15.1 G) | 0.957× (16.9 G) | 1.210×, 1.093×, 1.075× (13.6, 14.0, 15.6 G) | 0.928×, 0.724×*, 0.879× (16.2, 16.7, 18.5 G) |
| Rob Zombie | 1.598× | 1.352× | 1.216× | 1.769×, 1.493×, 1.352× | 1.581×, 1.348×, 1.201× |
| Domino's | 1.382× | 1.197× | — | 1.523×, 1.309× | 1.311×, 1.171× |

  Loads 1.1–3.5 (*the reference's video runs were cached from Task 1, on one thread; their instruction counts are the comparable figures). Instruction counts with the worker include its spinning. On one thread: America's Most Haunted with video 15.2 G and in play 17.2 G, The Jetsons 15.1 G and 16.8 G, byte-identical; `PINHECK_JOURNAL=0`, `PINHECK_RF13=0` (America's Most Haunted) and `PINHECK_JIT=0` (The Jetsons) give the short run byte-identical; `bench.sh contention` passes.
- **Task 6, regression:** the boot, display, service-test and simulator checks of all four games, the romset check, Domino's scripted game (44 checks, 0 failures), the audio and look checks, libpinmame for Domino's, America's Most Haunted (the raw DMD) and The Jetsons, and the MIPS32 (208 passed), PIC32MX, link, audio, board, display and storage suites pass; a translated PAR not shifted fails America's Most Haunted's determinism from the first boot, and the tree is clean afterwards (a translated CNT read four cycles late, the other translated mutant, does not change its short run: its cog 1 only compares CNT against a timeout).
- **Gain per change** (host instructions, one thread unless stated): the translator after a restart −8% for The Jetsons (16.7 → 15.4 G with video), none for the others; cached keys −3% to −4% on the proxies; PAR and CNT −10% on America's Most Haunted's proxy (−8% in PinMAME with video, 16.6 → 15.2 G with both of its changes), none for the others; the profile-guided build a further 5–8%.

**What remains.** America's Most Haunted with video (0.91×, 0.96× with the profile) and in play (0.86×, 0.87×) is below real time; everything else runs at real time with the profile-guided build. Its Propeller thread is bound by dispatches: its three Spin interpreters stop at almost every hub operation (5.8 million dispatches per emulated second), each costing about 900 host instructions in `run_local`, the scheduler and the translated blocks' entry and exit. The exact run-ahead (Ruling 6, branch `pinheck-12c-ra-proto`, `/code/spooky_domino/work/m12c/proto-runahead-v8-full.patch`) removes a third of those dispatches but costs more than it saves; the next lever is a cheaper dispatch itself (a translated block that issues its hub operation and returns the pending state without `run_local`'s decode and `event_run`, blocks that start at a pending hub operation's completion). The Windows recheck of the four games follows this plan.
