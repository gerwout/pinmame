# Milestone 9c: performance, stage 3 continued (event instructions in the local loop, pins between change points) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Domino's in `sdl3pinmame` runs faster than real time in attract mode, with video and four sound channels, and in the scripted game on the shared 7730U at the loads of the replay (1.24×, 1.08× and 1.0–1.1× at loads 2.5–2.8), with the UART1 log, display frames, sound, NVRAM and the game's logs still byte-identical to Plan 9b's build: a cog's event instructions run inside the Propeller's local loop while that cog's event is the one the scheduler would take next, and the pins between change points cost less.

**Architecture:** After Plan 9b the Propeller worker thread is the bound, and its time goes into events (the scheduler, `exec`, `do_hub`, `complete`), pin changes (`flush`, the counters, the devices) and the translated code with its entry and exit. `event_run` runs a hub read or write, an instruction with an `INA` source or one with a special destination inside `run_local` whenever the scheduler would take that event next (its key is below the other cogs' earliest event and no other cog's event has moved since), exactly as `exec`, `do_hub` and `complete` would. A hub read or write that must wait is issued in `run_local`; the scheduler hands a running cog's due instruction or hub access straight back to `run_local`; an event instruction whose condition fails is a four-cycle no-op there. Between pin change points the NCO toggles are stepped in one loop, a pending point re-evaluates only the cogs and counters that change there, a counter write keeps the pin and toggle caches of the state that lasts until it takes effect, and the display is called only for its own pins. The translator, the worker thread and the MIPS32 core are unchanged.

**Tech Stack:** C (C89-syntax-clean cores and `prop.c`, `-std=c99 -Wall -Wextra -Werror -pedantic` test builds), the vendored asmjit 1.21 (unchanged translator), `perf` 7.1, Python 3 (no third-party modules), POSIX sh, the existing Verilator P1 RTL, spinsim and QEMU oracles.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §4 (binding), parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` §5 and §7 (binding where the addendum is silent). Plans 9 and 9b (`docs/superpowers/plans/2026-09-29-pinheck-m9-performance.md`, `2026-09-29-pinheck-m9b-realtime.md`) provide the benchmark, the determinism method and the stages this plan builds on.

## Prerequisites

- Plan 9b executed, reviewed and merged. Proven and replayed on `pinheck` at `d8ddb9e9` with Plan 9b's result applied as one commit (`37fa7861`, "Plan 9b result": `work/m9b/m9b-replay.patch`, byte-identical to branch `pinheck-m9b`'s result). The patch was cut before Plan 8b and the display look were merged; it applies to `d8ddb9e9` with `git apply -3` except for the roadmap's milestone table, where the merged rows are kept and the m9b row is replaced by Plan 9b's two rows (Ruling 14).
- This plan edits Plan 3's, Plan 9's and Plan 9b's `p8x32a.[ch]`, Plan 5's and Plan 9b's `prop.[ch]` and one line of the machine driver `pinheck.c`, and adds the RTL test `tests/pinheck/p8x32a/chip/event_order.spin` and a case in `tests/pinheck/link/prop_test.c`. The translator (`p8x32ajit.cpp`), the MIPS32 and PIC32 cores, the board, display, audio and storage blocks do not change.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export: `TOOLS=$PWD/tests/pinheck/p8x32a/build/tools` (or the main checkout's, where the oracles are built); `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = the romset zip; `DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN`; `DOMINOS_ZIP=$PINHECK_ZIP`. Every command below uses these.
- `perf` may count user-space events (`sudo sysctl kernel.perf_event_paranoid=1` after a reboot).
- The p8x32a suite builds its RTL boot references once (about 10 minutes); copying `tests/pinheck/p8x32a/build/boot/*.rtl*` from a checkout that already has them skips that.
- The reference machine is shared: other agents' emulators and builds run beside these steps. Wall-clock speeds below were measured at the load averages printed with them; the single-thread instruction counts (`PINHECK_THREADS=0`) do not depend on the load. Builds run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6` (on another machine drop the `flock` prefix). Every PinMAME run has a `timeout -k 30`.

## Global Constraints

- Exactness: every change leaves the machine's behaviour unchanged, cycle for cycle. The RTL, spinsim and QEMU differential suites stay byte-identical, with and without the translator, and `bench.sh` with `REFERENCE` requires the UART1 log, the display frame log, the sound capture and the NVRAM of each workload to equal the reference build's byte for byte, with the worker thread and the translator on (the default) and off (`PINHECK_THREADS=0 PINHECK_JIT=0`). The scripted game's link, board, UART1 and frame logs equal the reference's.
- The reference build is `pinheck` at the commit this plan starts from, built Release exactly as below and copied to `build/reference/sdl3pinmame` before any source changes.
- The cores and `prop.c` stay C, C89-syntax-clean (`-std=c89 -pedantic-errors -Wno-long-long`, which the suites gate) and warning-free in the test builds; they include no PinMAME headers. No new files join the builds, so the CMake, makefile and Visual Studio builds need no change; the interpreter path this plan changes is the one every build without the translator runs.
- Test hooks stay inert unless set. No new switches.
- Every sdl3pinmame launch uses private `-nvram_directory` and `-cfg_directory` and a `timeout -k 30`.
- Every existing suite stays green: `tests/pinheck/{mips32,pic32mx,storage,link,p8x32a,display,audio,board}/check.sh`, the p8x32a suite also with `P8X32A_JIT=1`, `pic32mx/pinmame_check.sh` (with `romset_check.sh`), `display/pinmame_display.sh`, `audio/pinmame_audio.sh`, `board/pinmame_board.sh`, `display/pinmame_look.sh`, and `game/pinmame_game.sh`.
- Code comments stay short and factual.

## Rulings

Decisions the spec left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it). The counts are per emulated second in attract mode, measured on this plan's prototype with a counting build of the Plan 9b result; the profile shares are of the whole work on one thread.

1. **Scope: the Propeller's event path and pin changes, measured; real time reached.** Spec §4 asks for ≥ 1.0× with the sound never under-running and names the scripted game, attract mode and video with four channels. After Task 3 all three run faster than real time on the shared machine at loads up to about 2.8 (the Result): attract mode 1.24×, above the aim of 1.2×; video 1.08×; the scripted game 1.0× including its launches and checks, 1.1× for its play alone. At a load of about 3 the video workload falls to 0.97×. Cost if wrong: none for correctness; each stage is exact and measured on its own, and the Result states what the video workload needs for 1.2× and at higher loads.
2. **Where the Propeller's time went after Plan 9b.** The Propeller ran about 8.8 M events: 4.3 M hub operations (1.8 M each for cogs 0 and 5, the two ROM Spin interpreters), 1.6 M `INA` reads (cog 3, the SD card driver, eight per byte read) and 2.0 M special-register writes (cog 6, the display link: `SHL PHSB,#1` shifts out each bit while `FRQA` clocks P22; cog 3 starts and stops its SD clock the same way). The scheduler searched for the next cog 4.5 M times and let the same cog go on without a search 3.6 M times; each event went through the scheduler, `exec` or `do_hub`, `complete` and a new entry into `run_local` and a translated block (43 M translated instructions in 7.25 M block calls). The pins changed at 5.7 M points (2.0 M where a register changes, the rest NCO toggles of the SD clock P1 and the display clock P22). Of the Propeller's time the scheduler and the event functions took about 42%, the local loop around the blocks 14%, pin changes 23% and the translated code 19%.
3. **Event instructions run in the local loop while the scheduler would take them next.** The scheduler takes cog n's next event when its key (time, hub before other events, cog number) is below `bk2`, the second-lowest key of its last search, and no other cog's event has moved since (`sched_gen`); that is also the condition under which it lets the same cog go on (`goto again`). `event_run` runs such an event inside `run_local` with exactly what the scheduler would do: an `INA` source flushes the pins and reads them at the instruction's time, a special destination is written through `special_write` one cycle after completion, a hub read or write goes to its hub slot (`next_slot`), writes hub RAM, wakes sleepers watching those bytes and completes as `complete` does. It sets `p->now` to the event's time as the scheduler does. It does not run jumps, waits, `COGINIT`/`COGSTOP`/lock operations (hub op 3), a hub operation with an `INA` source, or anything while the cog records an idle loop; those go to the scheduler as before. `chip/event_order.spin` (Task 2) makes six cogs' hub reads and writes, `OUTA` toggles, counter writes, `INA` samples and a sleeping hub poller interleave and compares them with the RTL; dropping either key test or the `sched_gen` test fails it (Task 4).
4. **A hub operation that must wait is issued in `run_local`, and the scheduler hands a running cog's due event back to it.** When `event_run` declines a hub read or write, `run_local` issues it as `exec` does (`i`, `s`, `d`, `px`, `nix`, `cond`, `latch`, `EV_HUB` at `latch + 2`). The scheduler gives an `EV_EXEC` or `EV_HUB` event of a running cog that is not recording a loop to `run_local`, which completes a due hub access with `hub_rw` (the one copy of `do_hub` and `complete` for a hub read or write) or runs the due instruction, and falls back to `exec` or `do_hub` when it cannot take it (it returns 0 without touching the cog). Its local instructions and events then go on under the same key.
5. **An event instruction whose condition fails is a no-op in `run_local`.** `exec` performs no hub access, special write or wait for such an instruction and `complete` writes nothing; only its source is read. So `run_local` treats it as a four-cycle instruction that reads nothing, except that a `CNT` or `PHSx` source marks the idle-loop search dirty as `exec` does, and an `INA` source (whose read flushes the pins) goes to the scheduler.
6. **No event slots in translated blocks.** The prototype let the translator emit event instructions as calls to `event_run` from the blocks: single-thread host instructions rose by about 6% and attract mode slowed from 1.27× to 1.03× at similar loads, because about half the events are declined (another cog's event comes first) and a declined call costs more than leaving the block. `run_local` skips the translator for event words, which it then runs itself; the translator is unchanged.
7. **The worker does not run ahead of the PIC32's calls.** The worker idles about 9% of the time: after each `prop_sync` it waits for the PIC32 to post its next call. Most syncs are `RF13` reads: about 27,000 a second, all from one routine (`$9D02DEF4`) that reads the Propeller's ready line once per link byte, 255 PIC32 cycles apart, and always finds it high; the Propeller and the PIC32 run in lock step there. Running the worker past the calls the PIC32 made changes observable bytes: the display frame log records the PIC32 cycle of the call that produced a frame and the frame's hub address at the moment the pins delivered it, and sleeping cogs wake at each call's horizon and flush the pins there; a run cut at a point the reference never cuts delivers pins at other points, and ends a session or a reset with the Propeller further on. Cost if wrong: the idle share stays; a later plan could give the frame log a stamp that does not depend on the calls.
8. **The Propeller stays on one thread.** Its cogs interact at the hub (each cog's slot comes every 16 cycles) and through the pins, so a conservative split across threads could let one group run at most 16 cycles ahead of the other before it must wait; a thread handoff costs far more than 16 emulated cycles.
9. **Pins between change points.** Between two pending points only the NCO outputs move, so `flush` steps them in one loop (`nco_step`: the counters that change at `when` flip and get their next change). A pending point records which registers change there (`pend_what`: a cog's `OUTA`/`DIRA`, a counter); while the pin cache is valid only those cogs' registers and those counters' levels are evaluated there (`pins_what`), the other counters flip if they change at that point. A counter write no longer drops the pin cache (`add_pending` cleared `pins_ok`): until the write takes effect at `e` the counter keeps the state in effect, so its level and its cached next change stay valid, capped at `e`. A counter written again before its previous write took effect (possible only when another cog stops it within two cycles of the write) keeps the reference's full re-evaluation; no test reaches that case. A counter that joins the NCO list was no NCO before, so its cached level is 0: a counter leaves the list only at a write after its level was evaluated as a non-NCO, and `p8x32a_reset` clears the levels. The RTL tests `ctr_frq`, `nco`, `idle_ctr` and `event_order` fail when the flip, the cap or the per-point evaluation is removed (Task 4).
10. **The display sees only its own pins.** `prop.c` called the display on every pin change; its decoder acts on changes of P17 and P20–P22 only (`pinheck_display_pins` compares its pins with their last level), so `prop_set_pins_mask` limits the call to those pins; `prop_test` checks the mask.
11. **Measuring.** Speeds are wall-clock over `bench.sh`'s window and depend on the machine's load, printed with each. The load-independent measure of the work is the host instructions per emulated second of a single-thread run (`PINHECK_THREADS=0`, the whole run); the worker thread's own count (`perf record -s`) was tried and lost counts at thread exit. The reference build's runs are cached by `bench.sh`, so its speed is from one moment's load; Task 1 measures it with the thread and on one thread.
12. **The sound stream.** The spec measures real time with the headless benchmark. The sound stream under-runs when the emulation falls behind real time for longer than the audio queue (100 ms at 48 kHz); the benchmark reports the average over its window, so the margin above 1.0× is the margin for the stream: 8% for the video workload at load 2.6, none at about 3. Cost if wrong: a slower stretch shorter than the window could still under-run; the video workload has the least margin (the Result).
13. **Platforms and fallback.** No file is added to any build and no new dependency is used; the changed code is portable C (64-bit event keys are `uint64_t`, as the scheduler's already are). Every build runs `run_local`, `event_run` and the pin changes, with or without the translator: the p8x32a suite without `P8X32A_JIT`, the benchmark with `PINHECK_THREADS=0 PINHECK_JIT=0` against the reference, the C89 gates and the makefile-built test runners cover the interpreter path the 32-bit, AArch64, makefile and Visual Studio builds run.
14. **The base.** Plan 9b's replay patch was cut on `1f2ef40b`, before Plan 8b and the display look were merged into `pinheck`; applied to `d8ddb9e9` with `git apply -3` every hunk merges except the roadmap's milestone table, where `pinheck` has the rows of Plans 8b, 9 (merged) and the display look. The resolution keeps those rows and replaces the old m9b row with Plan 9b's two rows (its own and the m9c row it adds); the source files are byte-identical to the patch's.

## Review Focus

- **Another cog's earlier event** (a hub write that a later-scheduled cog's read must not see yet, an `OUTA` toggle before a later `INA` sample, a counter write while another cog's NCO toggles): each must happen in time order. `chip/event_order.spin` (Task 2) interleaves all of them against the RTL; dropping the hub key test or the other key test in `event_run` fails it (Task 4).
- **A cog woken by an event run in the local loop** (a hub write reaching the bytes a sleeping cog polls, then a second write right after it): the sleeper's event moves earlier and `sched_gen` changes, so the writer's next event goes back to the scheduler and the sleeper reads the first value when the hardware does. `chip/event_order.spin`'s polling cog and double writer; removing the `sched_gen` test in `event_run` fails it (Task 4).
- **Counter writes while an NCO toggles** (the display link shifting `PHSB` while `FRQA` clocks P22, the SD driver starting and stopping its clock): the pins between change points and each counter's next change must be exactly the reference's. `ctr_frq`, `nco`, `idle_ctr`, `event_order` against the RTL; mutations of the flip, the cap and the per-point evaluation fail them (Task 4).
- **Builds without the translator or the thread** (Visual Studio, makefile, 32-bit, AArch64): the same results, only slower. The p8x32a suite without `P8X32A_JIT`, the benchmark with `PINHECK_THREADS=0 PINHECK_JIT=0` byte-identical to the reference (Task 3).
- **Long runs** (the 315 s scripted game with the playfield simulator, multiball, high-score entry): its link, board, UART1 and frame logs equal the reference's (Task 4).

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/p8x32a/p8x32a.c` | `event_run` and `hub_rw`; `run_local` runs event instructions, issues waiting hub operations, completes a due hub access; the scheduler hands `EV_EXEC` and `EV_HUB` to `run_local` (Task 2); `nco_step`, `pins_what`, `pend_what`, the counter caches kept across counter writes (Task 3) (modified) |
| `src/cpu/p8x32a/p8x32a.h` | `pend_what`, `cog_out`, `P8X32A_PEND_COG`, `P8X32A_PEND_CTR` (modified) |
| `src/wpc/pinheck/prop.[ch]` | `prop_set_pins_mask`: the pins callback runs on a change of its pins only (modified) |
| `src/wpc/pinheck.c` | the display's pins mask (modified) |
| `tests/pinheck/p8x32a/chip/event_order.spin` | six cogs' hub, pin and counter events and a sleeping hub poller interleaved, against the RTL |
| `tests/pinheck/link/prop_test.c` | the pins mask (modified) |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | measured results (modified) |

---
### Task 1: Reference build and baseline

**Files:** none changed.

**Interfaces:**
- Consumes: Plan 9's `tests/pinheck/perf/bench.sh` (`bench.sh [attract] [video]`, `bench.sh profile WORKLOAD`, `SDL3PINMAME`, `REFERENCE`) and Plan 9b's `PINHECK_THREADS`.
- Produces: `build/reference/sdl3pinmame`, the reference every later task compares with, its cached benchmark runs, and the baseline figures of the Result.

- [ ] **Step 1: Build the reference**

Build `pinheck` as it stands (Release, `PLATFORM=linux`, `ARCH=x64`, never the user's `build-dbg`) and keep the binary as the reference:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9c.log 2>&1; tail -1 build/sdl3pinmame/m9c.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```
Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 2: The baseline, with the thread and on one thread**

```sh
SDL3PINMAME=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video
PINHECK_THREADS=0 SDL3PINMAME=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | sed 's/^/one thread: /'
```
Expected (about 5 minutes; speeds vary with the load, the one-thread instruction counts do not):
```text
bench attract opt: 0.852x over 30.0 s emulated (35.2 s wall), whole run 15.4 G instructions and 2.1 s CPU per emulated s, load 4.75
bench video opt: 0.867x over 24.0 s emulated (27.7 s wall), whole run 16.8 G instructions and 2.1 s CPU per emulated s, load 3.81
one thread: bench attract opt: 0.755x over 30.0 s emulated (39.7 s wall), whole run 15.2 G instructions and 1.2 s CPU per emulated s, load 3.08
one thread: bench video opt: 0.624x over 24.0 s emulated (38.4 s wall), whole run 16.5 G instructions and 1.4 s CPU per emulated s, load 3.11
```

The later tasks run the reference beside each build with `REFERENCE=build/reference/sdl3pinmame`; its runs are kept while its binary is the same, so it is benchmarked once.

- [ ] **Step 3: Where the time goes**

A Release build with `-g` in its own directory, profiled on one thread so that the shares add up to the work:

```sh
cmake -S . -B build/sdl3pinmame-prof -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DCMAKE_C_FLAGS=-g -DCMAKE_CXX_FLAGS=-g > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame-prof -j6 > build/sdl3pinmame-prof/m9c.log 2>&1; tail -1 build/sdl3pinmame-prof/m9c.log
PINHECK_THREADS=0 SDL3PINMAME=build/sdl3pinmame-prof/sdl3pinmame nice -n 15 ionice -c 3 timeout 5400 tests/pinheck/perf/bench.sh profile video | tail -9
perf report -i tests/pinheck/perf/build/prof/video/perf.data --stdio --sort sym 2> /dev/null | grep -v '^#' | grep -v '^$' | head -12
```
Expected (percentages vary by about a point; the translated code's samples appear as addresses, summed in the "translated" line):
```text
[100%] Built target sdl3pinmame
bench video prof: 0.628x over 24.0 s emulated (38.2 s wall), load 4.39
  57.5%  Propeller core (p8x32a)
  20.6%  PIC32 (mips32, pic32mx)
  15.9%  other (PinMAME, kernel, libraries)
   2.6%  Propeller stepping and link (prop.c)
   1.4%  SD card and EEPROM (sd, vfat, zipsrc, eeprom, zlib)
   1.3%  display and audio (display.c, audio.c)
   0.6%  board and driver (board.c, pinheck.c, rtc.c)
  13.4%  of it translated Propeller code
    20.17%  [.] pic32cpu_execute
    10.70%  [.] do_catch_up.constprop.0
    10.34%  [.] run_local
     8.14%  [.] complete
     7.33%  [.] exec.constprop.0
     7.03%  [.] flush
     5.57%  [.] ctr_next
     3.42%  [.] do_hub
     2.47%  [.] alu
     2.30%  [.] pins_out
     1.33%  [.] pinheck_spi
     0.78%  [.] ctr_save
```

### Task 2: Event instructions in the local loop (stage 3e)

**Files:**
- Create: `tests/pinheck/p8x32a/chip/event_order.spin`
- Modify: `src/cpu/p8x32a/p8x32a.c`

**Interfaces:**
- Consumes: Plan 9b's `run_local` (`fl`, `tl`, `nins`, the block call), `p8x32a_dec`/`dec_fill` and the `K_*`/`F_*` decodings, `alu_run`, `special_write`, `sread`, `ina`, `next_slot`, `loop_hub_write`, `jit_write`, `sched_gen`, and the scheduler's `bk2` in `p8x32a_run_until`.
- Produces: `F_INA` (an `INA` source; such words keep their kind); `static unsigned hub_rw(p8x32a *p, int n, uint32_t i, uint32_t s, uint32_t d, uint64_t h, uint64_t m3, unsigned fl)` (a hub read or write's access and completion, returns the flags); `static int event_run(p8x32a *p, int n, const p8x32a_dec *e, uint32_t ix, unsigned pc, unsigned *fl, uint64_t *t2, uint32_t *nix, uint64_t t, uint64_t lim, unsigned gen)` (1: run, with the next instruction's time and word); `static int run_local(p8x32a *p, int n, uint64_t t, uint64_t lim, unsigned gen)` (0: the cog did not move).

Event instructions run in `run_local` while the scheduler would take them next; a waiting hub operation is issued there; the scheduler hands a running cog's due instruction or hub access to `run_local`; an event instruction whose condition fails is a no-op (Rulings 3–6).

- [ ] **Step 1: An RTL test of interleaved events**

Six copies of one worker start with different `PAR` values and take six roles: a writer that stores a counting value into one hub long with varying gaps, a reader that samples that long into its own table, a toggler that flips P5 with `OUTA` and rewrites an NCO counter's `FRQA` on P6, a sampler that stores `INA` into its table, a poller that waits for a second hub long to change in an idle loop (it sleeps) and records each value it sees, and a writer that stores two values into that long 16 cycles apart, at shifting gaps. The events interleave at shifting phases; the tables in hub RAM and the pin trace go to the RTL comparison. The test passes on the current core; Task 4's mutations of `event_run` show it bites.

`tests/pinheck/p8x32a/chip/event_order.spin`:

```text
' six cogs whose events interleave: hub writes and reads of one long, OUTA toggles, NCO writes and INA samples,
' and a cog polling a hub long in an idle loop that another cog writes twice in a row
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        mov     p, #$60
        shl     p, #8
        mov     n, #6
:start  mov     y, p
        shl     y, #16
        or      y, x
        coginit y
        add     p, #$100
        djnz    n, #:start
        cogid   y
        cogstop y
x       long    0
y       long    0
n       long    0
p       long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     ptr, par
        mov     role, par
        shr     role, #8
        and     role, #7 wz
  if_z  jmp     #writer
        cmp     role, #1 wz
  if_z  jmp     #reader
        cmp     role, #2 wz
  if_z  jmp     #toggler
        cmp     role, #4 wz
  if_z  jmp     #poller
        cmp     role, #5 wz
  if_z  jmp     #twice
sampler mov     m, #64
:s      mov     v, ina
        and     v, pins
        wrlong  v, ptr
        add     ptr, #4
        mov     d, m
        and     d, #6
        add     d, #1
:d4     djnz    d, #:d4
        djnz    m, #:s
        jmp     #done
writer  mov     m, #64
:w      wrlong  k, shared
        add     k, #1
        mov     d, k
        and     d, #7
        add     d, #1
:d1     djnz    d, #:d1
        djnz    m, #:w
        jmp     #done
reader  mov     m, #64
:r      rdlong  v, shared
        wrlong  v, ptr
        add     ptr, #4
        mov     d, m
        and     d, #3
        add     d, #1
:d2     djnz    d, #:d2
        djnz    m, #:r
        jmp     #done
toggler mov     dira, pins
        movs    ctra, #6
        movi    ctra, #$20
        mov     m, #64
:t      xor     outa, pin5
        mov     frqa, m
        shl     frqa, #24
        mov     d, m
        and     d, #5
        add     d, #1
:d3     djnz    d, #:d3
        djnz    m, #:t
        mov     frqa, #0
poller  mov     m, #16
:p      rdlong  v, flag
        cmp     v, last wz
  if_z  jmp     #:p
        mov     last, v
        wrlong  v, ptr
        add     ptr, #4
        djnz    m, #:p
        jmp     #done
twice   mov     d, cnt
        add     d, d3000
        mov     m, #16
:tw     waitcnt d, d_gap
        wrlong  m, flag
        add     m, #100
        wrlong  m, flag
        sub     m, #100
        add     d_gap, #7
        djnz    m, #:tw
done    cogid   v
        cogstop v
pins    long    %1100000
pin5    long    %100000
shared  long    $5F00
flag    long    $5F04
last    long    0
d3000   long    3000
d_gap   long    200
k       long    1
ptr     long    0
role    long    0
m       long    0
d       long    0
v       long    0
        long    $C0DEE0D0
```

- [ ] **Step 2: The suite passes with the new test on the current core**

Run: `nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh 2>&1 | tail -1` (about a minute once the RTL references are cached)
Expected:
```text
p8x32a: 229 passed, 0 failed
```

- [ ] **Step 3: Events in the local loop**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.c', [
(r'''
enum { K_NL, K_GEN, K_JMP, K_DJNZ, K_TJ, K_AND, K_ANDN, K_OR, K_XOR, K_ADD, K_SUB, K_MOV, K_SHL, K_SHR, K_MOVS, K_MOVD, K_MOVI };
enum { F_IMM = 1, F_SPEC = 2, F_WR = 4, F_WC = 8, F_WZ = 16 };

/* decode for run_local: K_NL for an instruction it does not run */
static void dec_fill(p8x32a_dec *e, uint32_t i)
{
''',
 r'''
enum { K_NL, K_GEN, K_JMP, K_DJNZ, K_TJ, K_AND, K_ANDN, K_OR, K_XOR, K_ADD, K_SUB, K_MOV, K_SHL, K_SHR, K_MOVS, K_MOVD, K_MOVI };
enum { F_IMM = 1, F_SPEC = 2, F_WR = 4, F_WC = 8, F_WZ = 16, F_INA = 32 };

/* decode for run_local: K_NL for an instruction it does not run as a local one; F_INA for an INA source */
static void dec_fill(p8x32a_dec *e, uint32_t i)
{
'''),
(r'''	e->kind = kinds[op];
	e->fl = (uint8_t)((FIM(i) ? F_IMM : (SRC(i) >= 0x1F0 ? F_SPEC : 0)) | (FWR(i) ? F_WR : 0) | (FWC(i) ? F_WC : 0) | (FWZ(i) ? F_WZ : 0));
	if ((FWR(i) && DST(i) >= 0x1F0) || (!FIM(i) && SRC(i) == 0x1F2)) e->kind = K_NL;
}

''',
 r'''	e->kind = kinds[op];
	e->fl = (uint8_t)((FIM(i) ? F_IMM : (SRC(i) >= 0x1F0 ? F_SPEC : 0)) | (FWR(i) ? F_WR : 0) | (FWC(i) ? F_WC : 0) | (FWZ(i) ? F_WZ : 0));
	if (!FIM(i) && SRC(i) == 0x1F2) e->fl |= F_INA;
	if (FWR(i) && DST(i) >= 0x1F0) e->kind = K_NL;
}

/* the hub access and completion of cog n's hub read or write i, with source s and destination d, due at h with
   m3 = latch + 4, as do_hub and complete do them; returns the flags fl (Z bit 0, C bit 1) after it */
static unsigned hub_rw(p8x32a *p, int n, uint32_t i, uint32_t s, uint32_t d, uint64_t h, uint64_t m3, unsigned fl)
{
	p8x32a_loop *l = &p->loop[n];
	uint32_t *ram = p->cog[n].ram, a = s & 0xFFFF, w = rd32(p, a), r;
	unsigned op = OP(i), dst = DST(i);
	if (!FWR(i) && a < 0x8000) {
		unsigned sz = op == 2 ? 4 : op == 1 ? 2 : 1, ha = a & ~(sz - 1), k;
		int changed = 0;
		for (k = 0; k < sz; k++) {
			changed |= p->hub[ha + k] != (uint8_t)(d >> (8 * k));
			p->hub[ha + k] = (uint8_t)(d >> (8 * k));
		}
		if (changed) {
			l->dirty = 1;
			if (p->sleepers) loop_hub_write(p, n, ha, sz, h);
		}
	}
	r = op == 2 ? w : op == 1 ? (w >> ((a & 2) * 8)) & 0xFFFF : (w >> ((a & 3) * 8)) & 0xFF;
	if (FWR(i)) {
		if (ram[dst] != r) { uint32_t o = ram[dst]; ram[dst] = r; l->dirty = 1; jit_write(p, n, dst, o); }
		if (dst >= 0x1F0) { special_write(p, n, dst, r, m3); l->dirty = 1; }
	}
	if (FWC(i)) fl = (fl & ~2u) | (unsigned)p->sys_c << 1;
	if (FWZ(i)) fl = (fl & ~1u) | (unsigned)!r;
	return fl;
}

/* An event instruction of running cog n that the scheduler would take next: its event is before lim, the key of
   the other cogs' earliest event, no other cog's event has moved since lim was taken (gen), and it is not past t.
   Runs it as exec, do_hub and complete would: a hub read or write, or an instruction with an INA source or a
   special destination (not a jump, wait or system op). e decodes ix, at cog address pc - 1, due at *t2 with flags
   *fl. Returns 0 if it must wait for the scheduler; else 1, with *fl, *t2 (the next instruction's time) and *nix
   (the word fetched for the next instruction). */
static int event_run(p8x32a *p, int n, const p8x32a_dec *e, uint32_t ix, unsigned pc, unsigned *fl, uint64_t *t2, uint32_t *nix, uint64_t t, uint64_t lim, unsigned gen)
{
	p8x32a_cog *c = &p->cog[n];
	p8x32a_loop *l = &p->loop[n];
	uint32_t *ram = c->ram, s, d, r;
	unsigned op = OP(ix), f = *fl;
	uint64_t now = *t2;

	if (p->sched_gen != gen || p->stop || l->state != LOOP_SEARCH || !((e->cond >> f) & 1)) return 0;
	if (op <= 2) {
		uint64_t latch = next_slot(p, n, now + 1), h = latch + 2, m3 = latch + 4;
		if ((e->fl & F_INA) || h > t || (h << 4 | (uint64_t)n) >= lim || m3 + 1 >= c->disable_at) return 0;
		if (e->fl & F_IMM) s = e->src;
		else if (!(e->fl & F_SPEC)) s = ram[e->src];
		else {
			s = sread(p, n, e->src, now);
			if (e->src == 0x1F1 || e->src == 0x1FC || e->src == 0x1FD) l->dirty = 1;
		}
		*nix = ram[pc];
		p->now = h;
		c->latch = latch;
		f = hub_rw(p, n, ix, s, ram[e->dst], h, m3, f);
		*t2 = m3 + 3;
	} else {
		int wr, ci, zi;
		if (op == 3 || op > 0x3B || op == 0x17 || op >= 0x39) return 0;
		if (((now << 4) | 8 | (uint64_t)n) >= lim || now + 2 >= c->disable_at) return 0;
		p->now = now;
		if (e->fl & F_IMM) s = e->src;
		else if (e->fl & F_INA) s = ina(p, now);
		else if (!(e->fl & F_SPEC)) s = ram[e->src];
		else {
			s = sread(p, n, e->src, now);
			if (e->src == 0x1F1 || e->src == 0x1FC || e->src == 0x1FD) l->dirty = 1;
		}
		d = ram[e->dst];
		*nix = ram[pc];
		r = alu_run(op, s, d, pc, (int)(f >> 1 & 1), (int)(f & 1), p->sys_c, (e->fl & F_WC) != 0, &wr, &ci, &zi);
		if ((e->fl & F_WR) && wr && ram[e->dst] != r) { uint32_t o = ram[e->dst]; ram[e->dst] = r; l->dirty = 1; jit_write(p, n, e->dst, o); }
		if ((e->fl & F_WR) && e->dst >= 0x1F0) { special_write(p, n, e->dst, r, now + 1); l->dirty = 1; }
		if (e->fl & F_WC) f = (f & ~2u) | (unsigned)ci << 1;
		if (e->fl & F_WZ) f = (f & ~1u) | (unsigned)zi;
		*t2 = now + 4;
	}
	*fl = f;
	return 1;
}

'''),
(r'''
/* exec + complete for local instructions of cog n, kept in locals, until one is not local or t is passed.
   fl holds Z (bit 0), C (bit 1) and cancel (bit 2); an instruction runs if bit fl of its condition mask is set. */
static void run_local(p8x32a *p, int n, uint64_t t)
{
	p8x32a_cog *c = &p->cog[n];
''',
 r'''
/* exec + complete for local instructions of cog n, kept in locals, until one is not local or t is passed.
   fl holds Z (bit 0), C (bit 1) and cancel (bit 2); an instruction runs if bit fl of its condition mask is set.
   Event instructions run here while the scheduler would take them next (event_run, with the key lim of the other
   cogs' earliest event and gen); one that does not run is a no-op unless it reads INA; a hub read or write that
   must wait is issued as exec issues it. A cog whose hub read or write is due (EV_HUB, the scheduler's next event)
   first completes it as do_hub and complete would. Returns 0 if the cog did not move. */
static int run_local(p8x32a *p, int n, uint64_t t, uint64_t lim, unsigned gen)
{
	p8x32a_cog *c = &p->cog[n];
'''),
(r'''	uint32_t ix = c->ix;
	unsigned pc = c->p, fl = (unsigned)(c->c << 1 | c->z) | (unsigned)c->cancel << 2, nins = 0;
	int idled = 0;

	if (t2 > tl || dis < 2) goto limit;
	for (;;) {
		uint32_t s, d, r, nix;
		unsigned px = pc, jc = 0;
		if (p->jit_build && !(fl & 4)) {
			unsigned a = (pc - 1) & 511;
			p8x32a_jblk *b = jit_get(p, n, a, ix);
			unsigned k;
			p8x32a_jst st;
			if (b && b->len && t2 + 4 * (uint64_t)(b->len - 1) <= tl) {
				l->nins = (uint16_t)(l->nins + nins);
				nins = 0;
				st.ram = ram;
				st.code = p->jcode[n];
				st.tab = p->jblk[n];
				st.loop = l;
				st.t2 = t2;
				st.budget = (uint32_t)((tl - t2) / 4 + 1);
''',
 r'''	uint32_t ix = c->ix;
	unsigned pc = c->p, fl = (unsigned)(c->c << 1 | c->z) | (unsigned)c->cancel << 2, nins = 0;
	int idled = 0, done = 0;
	uint64_t t2in = t2, hub = 0;
	p8x32a_jst st;

	st.ram = ram;
	st.code = p->jcode[n];
	st.tab = p->jblk[n];
	st.loop = l;

	if (c->ev == EV_HUB) {
		uint64_t m3 = c->latch + 4;
		if (OP(c->i) > 2 || m3 + 1 >= dis) return 0;
		fl = hub_rw(p, n, c->i, c->s, c->d, t2, m3, fl);
		nins++;
		pc = (c->px + 1) & 511;
		fl = (fl & 3) | (c->px == 511) << 2;
		ix = c->nix;
		t2 = m3 + 3;
		done = 1;
	}
	if (t2 > tl || dis < 2) goto limit;
	for (;;) {
		uint32_t s, d, r, nix;
		unsigned px = pc, jc = 0;
		e = &dec[(pc - 1) & 511];
		if (e->word != ix) dec_fill(e, ix);
		/* event instructions are never translated */
		if (p->jit_build && !(fl & 4) && e->kind != K_NL && !(e->fl & F_INA)) {
			unsigned a = (pc - 1) & 511;
			p8x32a_jblk *b = jit_get(p, n, a, ix);
			unsigned k;
			if (b && b->len && t2 + 4 * (uint64_t)(b->len - 1) <= tl) {
				l->nins = (uint16_t)(l->nins + nins);
				nins = 0;
				st.t2 = t2;
				st.budget = (uint32_t)((tl - t2) / 4 + 1);
'''),
(r'''			}
		}
		e = &dec[(pc - 1) & 511];
		if (e->word != ix) dec_fill(e, ix);
		if (e->kind == K_NL) break;
		if (e->fl & F_IMM) s = e->src;
		else if (!(e->fl & F_SPEC)) s = ram[e->src];
''',
 r'''			}
		}
		if (e->kind == K_NL || (e->fl & F_INA)) {
			if (!((e->cond >> fl) & 1)) {
				if (e->fl & F_INA) break;
				if ((e->fl & F_SPEC) && (e->src == 0x1F1 || e->src == 0x1FC || e->src == 0x1FD)) l->dirty = 1;
				nix = ram[px];
				nins++;
				goto next;
			}
			if (!event_run(p, n, e, ix, pc, &fl, &t2, &nix, t, lim, gen)) {
				if (OP(ix) <= 2 && !(e->fl & F_INA) && l->state == LOOP_SEARCH) {
					/* a hub read or write waiting for its slot, as exec issues it */
					c->i = ix;
					c->cond = 1;
					c->s = (e->fl & F_IMM) ? e->src : (e->fl & F_SPEC) ? sread(p, n, e->src, t2) : ram[e->src];
					if ((e->fl & F_SPEC) && (e->src == 0x1F1 || e->src == 0x1FC || e->src == 0x1FD)) l->dirty = 1;
					c->d = ram[e->dst];
					c->px = (uint16_t)pc;
					c->nix = ram[pc];
					c->latch = next_slot(p, n, t2 + 1);
					hub = c->latch + 2;
				}
				break;
			}
			nins++;
			t2 -= 4;
			goto next;
		}
		if (e->fl & F_IMM) s = e->src;
		else if (!(e->fl & F_SPEC)) s = ram[e->src];
'''),
(r'''			nins++;
		}
		if (!jc) pc = (px + 1) & 511;
		fl = (fl & 3) | (jc || px == 511) << 2;
''',
 r'''			nins++;
		}
	next:
		if (!jc) pc = (px + 1) & 511;
		fl = (fl & 3) | (jc || px == 511) << 2;
'''),
(r'''		e = &dec[(pc - 1) & 511];
		if (e->word != ix) dec_fill(e, ix);
		if (e->kind != K_NL) {
			if ((e->fl & F_SPEC)) {
				(void)sread(p, n, e->src, t2);
''',
 r'''		e = &dec[(pc - 1) & 511];
		if (e->word != ix) dec_fill(e, ix);
		if (e->kind != K_NL && !(e->fl & F_INA)) {
			if ((e->fl & F_SPEC)) {
				(void)sread(p, n, e->src, t2);
'''),
(r'''	c->ev_t = t2;
	c->ev = EV_EXEC;
	if (idled) idle(p, n);
}

''',
 r'''	c->ev_t = t2;
	c->ev = EV_EXEC;
	if (hub) {
		c->ev = EV_HUB;
		c->ev_t = hub;
	}
	if (idled) idle(p, n);
	return done || t2 != t2in || hub || idled;
}

'''),
(r'''		p->now = b->ev_t;
		switch (b->ev) {
		case EV_HUB: do_hub(p, best); break;
		case EV_EXEC: exec(p, best); break;
		case EV_WAITPIN: wait_pins(p, best); break;
		case EV_RESTART: restart(p, best); break;
''',
 r'''		p->now = b->ev_t;
		switch (b->ev) {
		case EV_HUB:
			if (!b->run || p->loop[best].state != LOOP_SEARCH || !run_local(p, best, t, bk2, gen)) do_hub(p, best);
			break;
		case EV_EXEC:
			if (!b->run || p->loop[best].state != LOOP_SEARCH || !run_local(p, best, t, bk2, gen)) exec(p, best);
			break;
		case EV_WAITPIN: wait_pins(p, best); break;
		case EV_RESTART: restart(p, best); break;
'''),
(r'''		while (b->ev == EV_EXEC && b->ev_t <= t && (local(b) || issue_local(b))) {
			if (p->loop[best].state == LOOP_RECORD || !local(b)) exec(p, best);
			else run_local(p, best, t);
			if (p->loop[best].state == LOOP_RECORD) loop_post(p, best);
		}
''',
 r'''		while (b->ev == EV_EXEC && b->ev_t <= t && (local(b) || issue_local(b))) {
			if (p->loop[best].state == LOOP_RECORD || !local(b)) exec(p, best);
			else run_local(p, best, t, bk2, gen);
			if (p->loop[best].state == LOOP_RECORD) loop_post(p, best);
		}
'''),
])
EOF
```

- [ ] **Step 4: The core suites pass, with and without the translator**

Run: `for s in p8x32a link display audio; do nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh > build/m9c-$s.log 2>&1; echo "$s: exit $? $(tail -1 build/m9c-$s.log)"; done; P8X32A_JIT=1 nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh > build/m9c-p8x32a-jit.log 2>&1; echo "p8x32a translated: exit $? $(tail -1 build/m9c-p8x32a-jit.log)"` (about 5 minutes)
Expected:
```text
p8x32a: exit 0 p8x32a: 229 passed, 0 failed
link: exit 0 link: 0 failed
display: exit 0 display: 0 failed
audio: exit 0 audio: 0 failed
p8x32a translated: exit 0 p8x32a: 229 passed, 0 failed
```

- [ ] **Step 5: Rebuild and compare with the reference**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9c.log 2>&1; tail -1 build/sdl3pinmame/m9c.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:'
PINHECK_THREADS=0 SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:\|determinism first' | sed 's/^/one thread: /'
```
Expected (speeds vary with the load):
```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 1.214x over 30.0 s emulated (24.7 s wall), whole run 13.4 G instructions and 1.5 s CPU per emulated s, load 2.34
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 1.065x over 24.0 s emulated (22.5 s wall), whole run 14.5 G instructions and 1.6 s CPU per emulated s, load 2.16
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
one thread: bench attract opt: 1.004x over 30.0 s emulated (29.9 s wall), whole run 13.2 G instructions and 0.9 s CPU per emulated s, load 1.79
one thread: determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
one thread: bench video opt: 0.898x over 24.0 s emulated (26.7 s wall), whole run 14.3 G instructions and 1.0 s CPU per emulated s, load 1.48
one thread: determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.c tests/pinheck/p8x32a/chip/event_order.spin
git commit -m "p8x32a: event instructions run in the local loop while their event is the scheduler's next"
```

### Task 3: Pins between change points (stage 3f)

**Files:**
- Modify: `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32a.h`, `src/wpc/pinheck/prop.c`, `src/wpc/pinheck/prop.h`, `src/wpc/pinheck.c`, `tests/pinheck/link/prop_test.c`

**Interfaces:**
- Consumes: Plan 9b's `flush`, `ctr_next`, `ctr_toggle`, `pins_nco`, `pins_regs`, `ctr_save`, `ctr_mask`, `regset`, `add_pending`, the NCO caches (`nco_nt`, `nco_from`, `nco_ok`, `nco_lvl`, `ctr_nt`, `ctr_ok`, `pins_ok`); `prop.c`'s `pins_out` and `prop_set_pins`.
- Produces: `p8x32a.pend_what[40]`, `p8x32a.cog_out[8]`, `P8X32A_PEND_COG(n)`, `P8X32A_PEND_CTR(j)`; `static void add_pending(p8x32a *p, uint64_t t, uint32_t pins, uint32_t what)`; `static void regset(p8x32a *p, int n, p8x32a_reg *r, uint32_t v, uint64_t at)`; `static uint64_t nco_step(p8x32a *p, uint64_t when, uint32_t *out)`; `static uint32_t pins_what(p8x32a *p, uint64_t t, uint32_t what)`; `void prop_set_pins_mask(pinheck_prop *p, uint32_t mask)` and `pinheck_prop.pins_mask` (`prop_set_pins` sets it to all pins).

NCO toggles between pending points are stepped in one loop, a pending point re-evaluates only the registers that change there, a counter write keeps the caches of the state in effect until it takes effect, and the display is called only for its own pins (Rulings 9, 10).

- [ ] **Step 1: The pins mask in the link test**

`prop_test` gains `pins_mask`: echo (the link test's Propeller program) drives only P24, so a callback limited to P24 sees every change and one limited to the other pins sees only the first change, which every device sees.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/link/prop_test.c', [
(r'''}

static int load(const char *path, uint8_t *dst)
{
''',
 r'''}

static int npins;

static void count_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx; (void)t; (void)out; (void)dir;
	npins++;
}

/* the pins callback runs on a change of the pins in its mask (and once for the first change, which every device
   sees); echo drives P24 only */
static void pins_mask(void)
{
	const uint32_t masks[3] = { 0xFFFFFFFFu, 1u << 24, ~(1u << 24) };
	int k, calls[3];
	for (k = 0; k < 3; k++) {
		uint64_t pic = 200000;
		int b;
		boot();
		prop_set_pins(&p, count_pins, NULL);
		if (k) prop_set_pins_mask(&p, masks[k]);
		npins = 0;
		prop_catch_up(&p, pic);
		for (b = 0; b < BITS; b++) pulse(&pic, (b * 7 + 3) % 5 < 2);
		prop_catch_up(&p, pic);
		calls[k] = npins;
	}
	CHECK(calls[0] > 2 && calls[1] == calls[0] && calls[2] == 1);
}

static int load(const char *path, uint8_t *dst)
{
'''),
(r'''	clkset_reset_bit();
	threaded();
	printf("prop: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
''',
 r'''	clkset_reset_bit();
	threaded();
	pins_mask();
	printf("prop: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
'''),
])
EOF
```

- [ ] **Step 2: Confirm it fails before the mask exists**

Run: `nice -n 15 ionice -c 3 tests/pinheck/link/check.sh 2>&1 | grep -m1 error`
Expected:
```text
prop_test.c:231:24: error: implicit declaration of function ‘prop_set_pins_mask’; did you mean ‘prop_set_pins’? [-Wimplicit-function-declaration]
```

- [ ] **Step 3: Pins between change points**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.c', [
(r'''static void jit_write(p8x32a *p, int n, unsigned s, uint32_t old);

/* a pin change at t; pins = the pins it may change */
static void add_pending(p8x32a *p, uint64_t t, uint32_t pins)
{
	int k;
	if (p->sleepers) loop_notify(p, t, pins);
	p->pins_ok = 0;
	for (k = 0; k < p->npend; k++)
		if (p->pend[k] == t) { p->pend_pins[k] |= pins; return; }
	if (p->npend < (int)(sizeof(p->pend) / sizeof(p->pend[0]))) { p->pend_pins[p->npend] = pins; p->pend[p->npend++] = t; }
}

static void flush(p8x32a *p, uint64_t t);

static void regset(p8x32a *p, p8x32a_reg *r, uint32_t v, uint64_t at)
{
	uint32_t pins = (r->cur ^ v) | (r->prev ^ v);
''',
 r'''static void jit_write(p8x32a *p, int n, unsigned s, uint32_t old);

/* a pin change at t; pins = the pins it may change, what = the registers that change (P8X32A_PEND_COG(n): cog n's
   OUTA or DIRA, P8X32A_PEND_CTR(j): counter j = 2 * cog + A/B) */
static void add_pending(p8x32a *p, uint64_t t, uint32_t pins, uint32_t what)
{
	int k;
	if (p->sleepers) loop_notify(p, t, pins);
	for (k = 0; k < p->npend; k++)
		if (p->pend[k] == t) { p->pend_pins[k] |= pins; p->pend_what[k] |= what; return; }
	if (p->npend < (int)(sizeof(p->pend) / sizeof(p->pend[0]))) {
		p->pend_pins[p->npend] = pins;
		p->pend_what[p->npend] = what;
		p->pend[p->npend++] = t;
	}
}

static void flush(p8x32a *p, uint64_t t);

static void regset(p8x32a *p, int n, p8x32a_reg *r, uint32_t v, uint64_t at)
{
	uint32_t pins = (r->cur ^ v) | (r->prev ^ v);
'''),
(r'''	r->cur = v;
	r->at = at;
	add_pending(p, at, pins);
}

''',
 r'''	r->cur = v;
	r->at = at;
	add_pending(p, at, pins, P8X32A_PEND_COG(n));
}

'''),
(r'''}

static void pins_regs(p8x32a *p, uint64_t t)
{
''',
 r'''}

/* pins_nco(p, when, 0) and the next ctr_next(p, when) in one pass: the counters that change at when flip and get
   their next change; when is ctr_next(p->flushed), so every listed counter's next change is cached */
static uint64_t nco_step(p8x32a *p, uint64_t when, uint32_t *out)
{
	uint32_t o = p->reg_out;
	uint64_t nt = P8X32A_NEVER;
	int k;
	for (k = 0; k < p->nco_n; k++) {
		int j = p->nco_list[k];
		const p8x32a_cog *g = &p->cog[j >> 1];
		if (p->nco_nt[j] == when) {
			int c = j & 1;
			p->nco_lvl[j] ^= nco_pins(when < g->ctr_at[c] ? g->ctr_old[c] : g->ctr[c]);
			p->nco_nt[j] = ctr_toggle(g, c, when);
			p->nco_from[j] = when;
		}
		o |= p->nco_lvl[j] & p->cog_dir[j >> 1];
		if (p->nco_nt[j] < nt) nt = p->nco_nt[j];
	}
	p->ctr_from = when;
	p->ctr_nt = nt;
	*out = o;
	return nt;
}

static void pins_regs(p8x32a *p, uint64_t t)
{
'''),
(r'''		uint32_t dd = regval(&c->dira, t);
		p->cog_dir[n] = dd;
		if (!dd) continue;
		p->reg_dir |= dd;
		p->reg_out |= regval(&c->outa, t) & dd;
	}
	p->pins_ok = 1;
}

''',
 r'''		uint32_t dd = regval(&c->dira, t);
		p->cog_dir[n] = dd;
		p->cog_out[n] = dd ? regval(&c->outa, t) & dd : 0;
		p->reg_dir |= dd;
		p->reg_out |= p->cog_out[n];
	}
	p->pins_ok = 1;
}

/* a pending point at t where only the registers in what change (the others keep their state since the last one):
   those cogs' OUTA and DIRA and those counters' levels are evaluated, the other counters flip if they change at t */
static uint32_t pins_what(p8x32a *p, uint64_t t, uint32_t what)
{
	uint32_t o, d = 0;
	int k, n;
	o = 0;
	for (n = 0; n < 8; n++) {
		if (what >> n & 1) {
			const p8x32a_cog *c = &p->cog[n];
			uint32_t dd = regval(&c->dira, t);
			p->cog_dir[n] = dd;
			p->cog_out[n] = dd ? regval(&c->outa, t) & dd : 0;
		}
		d |= p->cog_dir[n];
		o |= p->cog_out[n];
	}
	p->reg_out = o;
	p->reg_dir = d;
	for (k = 0; k < p->nco_n; k++) {
		int j = p->nco_list[k], c = j & 1;
		const p8x32a_cog *g = &p->cog[j >> 1];
		if (what >> (8 + j) & 1) p->nco_lvl[j] = ctr_pins(g, c, t);
		else if (p->nco_nt[j] == t) p->nco_lvl[j] ^= nco_pins(t < g->ctr_at[c] ? g->ctr_old[c] : g->ctr[c]);
		o |= p->nco_lvl[j] & p->cog_dir[j >> 1];
	}
	return o;
}

'''),
(r'''		/* registers change only at pending points: between them only the counters move */
		full = !p->pins_ok || (best >= 0 && p->pend[best] == when);
		if (full) pins_regs(p, when);
		out = pins_nco(p, when, full);
		dir = p->reg_dir;
		if ((out != p->last_out || dir != p->last_dir) && p->bus.pins_out) p->bus.pins_out(p->bus.ctx, when, out, dir);
''',
 r'''		/* registers change only at pending points: between them only the counters move */
		full = !p->pins_ok || (best >= 0 && p->pend[best] == when);
		if (!full) {
			/* NCO changes up to the next pending point or t */
			uint64_t end = best >= 0 ? p->pend[best] : t + 1;
			do {
				uint64_t nt = nco_step(p, when, &out);
				if (out != p->last_out && p->bus.pins_out) p->bus.pins_out(p->bus.ctx, when, out, p->last_dir);
				p->last_out = out;
				p->flushed = when;
				when = nt;
			} while (when < end);
			continue;
		}
		if (p->pins_ok) {
			uint32_t what = 0;
			for (k = 0; k < p->npend; k++)
				if (p->pend[k] == when) what |= p->pend_what[k];
			out = pins_what(p, when, what);
		} else {
			pins_regs(p, when);
			out = pins_nco(p, when, 1);
		}
		dir = p->reg_dir;
		if ((out != p->last_out || dir != p->last_dir) && p->bus.pins_out) p->bus.pins_out(p->bus.ctx, when, out, dir);
'''),
(r'''		if (when > p->flushed) p->flushed = when;
		for (k = 0; k < p->npend; k++)
			if (p->pend[k] == when) { --p->npend; p->pend[k] = p->pend[p->npend]; p->pend_pins[k] = p->pend_pins[p->npend]; k--; }
	}
	if (t > p->flushed) p->flushed = t;
''',
 r'''		if (when > p->flushed) p->flushed = when;
		for (k = 0; k < p->npend; k++)
			if (p->pend[k] == when) { --p->npend; p->pend[k] = p->pend[p->npend]; p->pend_pins[k] = p->pend_pins[p->npend]; p->pend_what[k] = p->pend_what[p->npend]; k--; }
	}
	if (t > p->flushed) p->flushed = t;
'''),
(r'''static void ctr_save(p8x32a *p, p8x32a_cog *c, int k, uint64_t e)
{
	flush(p, p->now);
	p->ctr_ok = 0;
	add_pending(p, e, nco_pins(c->ctr[k]));
	c->ctr_old[k] = c->ctr[k];
	c->frq_old[k] = c->frq[k];
''',
 r'''static void ctr_save(p8x32a *p, p8x32a_cog *c, int k, uint64_t e)
{
	int j = 2 * (int)(c - p->cog) + k;
	flush(p, p->now);
	if (c->ctr_at[k] <= p->flushed && (p->nco_ok >> j & 1)) {
		/* the state in effect lasts until e: its cached next change stays, capped at e */
		if (e < p->nco_nt[j]) p->nco_nt[j] = e;
		if (e < p->ctr_nt) p->ctr_nt = e;
	} else {
		p->ctr_ok = 0;
		p->nco_ok &= (uint16_t)~(1u << j);
		/* a state that has not taken effect yet is replaced (a cog stopped right after a counter write): the pins
		   are evaluated again */
		if (c->ctr_at[k] > p->flushed) p->pins_ok = 0;
	}
	add_pending(p, e, nco_pins(c->ctr[k]), P8X32A_PEND_CTR(j));
	c->ctr_old[k] = c->ctr[k];
	c->frq_old[k] = c->frq[k];
'''),
(r'''	c->phs_t_old[k] = c->phs_t[k];
	c->ctr_at[k] = e;
	p->nco_ok &= (uint16_t)~(1u << (2 * (c - p->cog) + k));
}

''',
 r'''	c->phs_t_old[k] = c->phs_t[k];
	c->ctr_at[k] = e;
}

'''),
(r'''{
	const p8x32a_cog *c = &p->cog[n];
	uint16_t b = (uint16_t)(1u << (2 * n + k));
	int j;
	if (nco(c->ctr[k]) || nco(c->ctr_old[k])) p->nco_mask |= b;
	else p->nco_mask &= (uint16_t)~b;
	p->nco_n = 0;
	for (j = 0; j < 16; j++)
''',
 r'''{
	const p8x32a_cog *c = &p->cog[n];
	uint16_t b = (uint16_t)(1u << (2 * n + k)), m = p->nco_mask;
	int j;
	if (nco(c->ctr[k]) || nco(c->ctr_old[k])) p->nco_mask |= b;
	else p->nco_mask &= (uint16_t)~b;
	if (p->nco_mask == m) return;
	p->nco_n = 0;
	for (j = 0; j < 16; j++)
'''),
(r'''	int k = a & 1;
	switch (a) {
	case 0x1F4: regset(p, &c->outa, v, e); break;
	case 0x1F6: if (e < c->disable_at) regset(p, &c->dira, v, e); break;
	case 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; add_pending(p, e, nco_pins(v)); ctr_mask(p, n, k); ctr_check(p, v); ctr_notify(p, n, k, e); break;
	case 0x1FA: case 0x1FB: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->frq[k] = v; ctr_notify(p, n, k, e); break;
	case 0x1FC: case 0x1FD: ctr_save(p, c, k, e); c->phs[k] = v; c->phs_t[k] = e; break;
''',
 r'''	int k = a & 1;
	switch (a) {
	case 0x1F4: regset(p, n, &c->outa, v, e); break;
	case 0x1F6: if (e < c->disable_at) regset(p, n, &c->dira, v, e); break;
	case 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; add_pending(p, e, nco_pins(v), P8X32A_PEND_CTR(2 * n + k)); ctr_mask(p, n, k); ctr_check(p, v); ctr_notify(p, n, k, e); break;
	case 0x1FA: case 0x1FB: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->frq[k] = v; ctr_notify(p, n, k, e); break;
	case 0x1FC: case 0x1FD: ctr_save(p, c, k, e); c->phs[k] = v; c->phs_t[k] = e; break;
'''),
(r'''	p8x32a_cog *c = &p->cog[n];
	if (d < c->disable_at) c->disable_at = d;
	regset(p, &c->dira, 0, d);
	ctr_save(p, c, 0, d);
	ctr_save(p, c, 1, d);
''',
 r'''	p8x32a_cog *c = &p->cog[n];
	if (d < c->disable_at) c->disable_at = d;
	regset(p, n, &c->dira, 0, d);
	ctr_save(p, c, 0, d);
	ctr_save(p, c, 1, d);
'''),
(r'''	p->nco_mask = p->nco_ok = 0;
	p->nco_n = 0;
	p->pins_ok = 0;
	p->sleepers = 0;
''',
 r'''	p->nco_mask = p->nco_ok = 0;
	p->nco_n = 0;
	memset(p->nco_lvl, 0, sizeof(p->nco_lvl));
	p->pins_ok = 0;
	p->sleepers = 0;
'''),
])

edit('src/cpu/p8x32a/p8x32a.h', [
(r'''
#define P8X32A_NEVER 0xFFFFFFFFFFFFFFFFull

typedef struct p8x32a_bus {
''',
 r'''
#define P8X32A_NEVER 0xFFFFFFFFFFFFFFFFull
#define P8X32A_PEND_COG(n) (1u << (n))       /* pend_what: cog n's OUTA or DIRA */
#define P8X32A_PEND_CTR(j) (1u << (8 + (j))) /* pend_what: counter j (2 * cog + 0 for A, 1 for B) */

typedef struct p8x32a_bus {
'''),
(r'''	uint64_t now, horizon, flushed, slot_base, cnt_base;
	uint64_t pend[40];
	uint32_t pend_pins[40];
	int npend;
	uint32_t last_out, last_dir;
''',
 r'''	uint64_t now, horizon, flushed, slot_base, cnt_base;
	uint64_t pend[40];
	uint32_t pend_pins[40], pend_what[40]; /* the pins a pending point may change, and the registers that change there */
	int npend;
	uint32_t last_out, last_dir;
'''),
(r'''	uint8_t nco_n, nco_list[16];
	int pins_ok; /* reg_out, reg_dir, cog_dir, nco_lvl hold the pins since the last change point */
	uint32_t reg_out, reg_dir, cog_dir[8], nco_lvl[16];
	uint64_t nco_from[16], nco_nt[16];
	uint8_t sleepers;
''',
 r'''	uint8_t nco_n, nco_list[16];
	int pins_ok; /* reg_out, reg_dir, cog_dir, nco_lvl hold the pins since the last change point */
	uint32_t reg_out, reg_dir, cog_dir[8], cog_out[8], nco_lvl[16];
	uint64_t nco_from[16], nco_nt[16];
	uint8_t sleepers;
'''),
])

edit('src/wpc/pinheck/prop.c', [
(r'''		p->sd_do = p->sd(p->sd_ctx, cs, sclk, mosi) != 0;
	}
	if (p->pins) p->pins(p->pins_ctx, t, out, dir);
}

''',
 r'''		p->sd_do = p->sd(p->sd_ctx, cs, sclk, mosi) != 0;
	}
	if (p->pins && (ch & p->pins_mask)) p->pins(p->pins_ctx, t, out, dir);
}

'''),
(r'''	p->pins = fn;
	p->pins_ctx = ctx;
}

''',
 r'''	p->pins = fn;
	p->pins_ctx = ctx;
	p->pins_mask = 0xFFFFFFFFu;
}

void prop_set_pins_mask(pinheck_prop *p, uint32_t mask)
{
	p->pins_mask = mask;
	p->po_ok = 0;
}

'''),
])

edit('src/wpc/pinheck/prop.h', [
(r'''	prop_pins_fn pins;
	void *pins_ctx;
	prop_clock_fn clock; /* the PIC32 cycle now */
	void *clock_ctx;
''',
 r'''	prop_pins_fn pins;
	void *pins_ctx;
	uint32_t pins_mask; /* pins calls fn only on a change of these (default all) */
	prop_clock_fn clock; /* the PIC32 cycle now */
	void *clock_ctx;
'''),
(r'''void prop_set_sound(pinheck_prop *p, prop_ctr_fn ctr, prop_pins_fn pins, void *ctx);
void prop_set_pins(pinheck_prop *p, prop_pins_fn fn, void *ctx);
void prop_reset(pinheck_prop *p, uint64_t pic_cycle);
void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins);
''',
 r'''void prop_set_sound(pinheck_prop *p, prop_ctr_fn ctr, prop_pins_fn pins, void *ctx);
void prop_set_pins(pinheck_prop *p, prop_pins_fn fn, void *ctx);
void prop_set_pins_mask(pinheck_prop *p, uint32_t mask);
void prop_reset(pinheck_prop *p, uint64_t pic_cycle);
void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins);
'''),
])

edit('src/wpc/pinheck.c', [
(r'''	disp_opened = 1;
	prop_set_pins(&prop, pinheck_disp_pins, NULL);
}

''',
 r'''	disp_opened = 1;
	prop_set_pins(&prop, pinheck_disp_pins, NULL);
	prop_set_pins_mask(&prop, DISPLAY_P17 | DISPLAY_P20 | DISPLAY_P21 | DISPLAY_P22);
}

'''),
])
EOF
```

- [ ] **Step 4: The core suites pass, with and without the translator**

Run: `for s in p8x32a link display audio; do nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh > build/m9c-$s.log 2>&1; echo "$s: exit $? $(tail -1 build/m9c-$s.log)"; done; grep -a '^prop:' build/m9c-link.log; P8X32A_JIT=1 nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh > build/m9c-p8x32a-jit.log 2>&1; echo "p8x32a translated: exit $? $(tail -1 build/m9c-p8x32a-jit.log)"` (about 5 minutes)
Expected:
```text
p8x32a: exit 0 p8x32a: 229 passed, 0 failed
link: exit 0 link: 0 failed
display: exit 0 display: 0 failed
audio: exit 0 audio: 0 failed
prop: ok
p8x32a translated: exit 0 p8x32a: 229 passed, 0 failed
```

- [ ] **Step 5: Rebuild and compare with the reference, with the thread and translator on and off**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9c.log 2>&1; tail -1 build/sdl3pinmame/m9c.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:'
PINHECK_THREADS=0 SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:\|determinism first' | sed 's/^/one thread: /'
PINHECK_THREADS=0 PINHECK_JIT=0 SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:\|determinism first' | sed 's/^/interpreter, one thread: /'
```
Expected (speeds vary with the load):
```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 1.237x over 30.0 s emulated (24.2 s wall), whole run 12.7 G instructions and 1.5 s CPU per emulated s, load 2.80
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 1.083x over 24.0 s emulated (22.1 s wall), whole run 13.7 G instructions and 1.6 s CPU per emulated s, load 2.56
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
one thread: bench attract opt: 1.015x over 30.0 s emulated (29.5 s wall), whole run 12.5 G instructions and 0.9 s CPU per emulated s, load 1.95
one thread: determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
one thread: bench video opt: 0.917x over 24.0 s emulated (26.2 s wall), whole run 13.5 G instructions and 1.0 s CPU per emulated s, load 1.59
one thread: determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
interpreter, one thread: bench attract opt: 0.920x over 30.0 s emulated (32.6 s wall), whole run 14.9 G instructions and 1.0 s CPU per emulated s, load 1.49
interpreter, one thread: determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
interpreter, one thread: bench video opt: 0.822x over 24.0 s emulated (29.2 s wall), whole run 16.1 G instructions and 1.1 s CPU per emulated s, load 1.56
interpreter, one thread: determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32a.h src/wpc/pinheck/prop.c src/wpc/pinheck/prop.h src/wpc/pinheck.c tests/pinheck/link/prop_test.c
git commit -m "p8x32a: NCO steps in one loop, pending points re-evaluate what changes there, counter writes keep the pin caches; the display sees its own pins"
```

### Task 4: Regression, the scripted game, results, and the tests bite

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` (§4), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` (the m9c row, "Carried into m9c")

- [ ] **Step 1: Every suite and machine check**

```sh
export SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame
grep -i warning build/sdl3pinmame/m9c.log | grep -c 'mips32\|p8x32a\|pic32mx\|prop\.c\|pinheck\.c'
for s in mips32 pic32mx link p8x32a display audio board; do nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh > build/m9c-$s.log 2>&1; echo "$s: exit $? $(tail -1 build/m9c-$s.log)"; done
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 tests/pinheck/storage/check.sh > build/m9c-storage.log 2>&1; echo "storage: exit $? $(tail -1 build/m9c-storage.log)"; rm -rf tests/pinheck/storage/build
P8X32A_JIT=1 nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh > build/m9c-p8x32a-jit.log 2>&1; echo "p8x32a translated: exit $? $(tail -1 build/m9c-p8x32a-jit.log)"
for c in pic32mx/pinmame_check.sh display/pinmame_display.sh audio/pinmame_audio.sh board/pinmame_board.sh display/pinmame_look.sh; do nice -n 15 ionice -c 3 timeout 7200 tests/pinheck/$c > build/m9c-$(basename $c .sh).log 2>&1; echo "$c: exit $? $(tail -1 build/m9c-$(basename $c .sh).log)"; done
```
Expected (about 15 minutes once the RTL boot references are cached; the storage suite's images, about 7 GB, are deleted after it passes):
```text
0
mips32: exit 0 mips32: 208 passed, 0 failed
pic32mx: exit 0 pic32mx: 0 failed
link: exit 0 link: 0 failed
p8x32a: exit 0 p8x32a: 229 passed, 0 failed
display: exit 0 display: 0 failed
audio: exit 0 audio: 0 failed
board: exit 0 board: 0 failed
storage: exit 0 storage: 0 failed
p8x32a translated: exit 0 p8x32a: 229 passed, 0 failed
pic32mx/pinmame_check.sh: exit 0 pinmame: ok
display/pinmame_display.sh: exit 0 pinmame display: ok
audio/pinmame_audio.sh: exit 0 pinmame audio: ok
board/pinmame_board.sh: exit 0 pinmame board: ok
display/pinmame_look.sh: exit 0 pinmame look: ok
```

- [ ] **Step 2: The scripted game with both builds**

The game (Plan 8b's `tests/pinheck/game/pinmame_game.sh`, 315 s of play with `PINHECK_RTC` fixed) runs with the reference and with this build; its four logs must be identical:

```sh
for b in reference sdl3pinmame; do
	perf stat -e task-clock,instructions:u -o build/game-$b.perf env SDL3PINMAME=$PWD/build/$b/sdl3pinmame nice -n 15 ionice -c 3 timeout 6000 tests/pinheck/game/pinmame_game.sh | tail -1
	echo "load $(cut -d' ' -f1 /proc/loadavg)"
	rm -rf build/game-$b && cp -r tests/pinheck/game/build/pinmame build/game-$b
done
for f in link2.log out2.log uart2.log frames2.bin; do cmp build/game-reference/$f build/game-sdl3pinmame/$f && echo "$f same"; done
grep -h 'elapsed\|instructions' build/game-reference.perf build/game-sdl3pinmame.perf
rm -rf build/game-reference build/game-sdl3pinmame
```
Expected (about 15 minutes; the second run is this build):
```text
pinmame game: ok
load 5.16
pinmame game: ok
load 2.80
link2.log same
out2.log same
uart2.log same
frames2.bin same
 6,057,156,284,997      instructions:u                                                        
     416.688303390 seconds time elapsed
 4,939,964,219,114      instructions:u                                                        
     332.730892268 seconds time elapsed
```

- [ ] **Step 3: Where the time goes now**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame-prof -j6 > build/sdl3pinmame-prof/m9c.log 2>&1; tail -1 build/sdl3pinmame-prof/m9c.log
PINHECK_THREADS=0 SDL3PINMAME=build/sdl3pinmame-prof/sdl3pinmame nice -n 15 ionice -c 3 timeout 5400 tests/pinheck/perf/bench.sh profile video | tail -9
perf report -i tests/pinheck/perf/build/prof/video/perf.data --stdio --sort sym 2> /dev/null | grep -v '^#' | grep -v '^$' | head -12
```
Expected (percentages vary by about a point):
```text
[100%] Built target sdl3pinmame
bench video prof: 0.819x over 24.0 s emulated (29.3 s wall), load 2.67
  48.5%  Propeller core (p8x32a)
  26.3%  PIC32 (mips32, pic32mx)
  19.3%  other (PinMAME, kernel, libraries)
   2.9%  Propeller stepping and link (prop.c)
   1.2%  SD card and EEPROM (sd, vfat, zipsrc, eeprom, zlib)
   1.1%  display and audio (display.c, audio.c)
   0.7%  board and driver (board.c, pinheck.c, rtc.c)
  16.8%  of it translated Propeller code
    25.68%  [.] pic32cpu_execute
    19.68%  [.] run_local.lto_priv.0
    10.03%  [.] flush.lto_priv.0
     6.88%  [.] do_catch_up.constprop.0
     3.53%  [.] hub_rw
     2.56%  [.] pins_out
     2.35%  [.] ctr_next.lto_priv.0
     1.40%  [.] ctr_save
     1.12%  [.] alu_run
     0.99%  [.] special_write
     0.96%  [.] pinheck_spi
     0.79%  [.] add_pending
```

- [ ] **Step 4: Update the spec and the roadmap**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md', [
(r'''- **Proof for every stage:**
''',
 r'''- **Established by Plan 9c (stage 3 continued):**
  - After Plan 9b each Propeller event went through the scheduler, `exec` or `do_hub`, `complete` and a new entry into the local loop and a translated block: per emulated second in attract mode about 8.8 M events (4.3 M hub operations, mostly the two Spin interpreters; 1.6 M `INA` reads by the SD card driver; 2.0 M special-register writes by the display link and the SD driver) and 5.7 M pin change points, most of them NCO toggles of the SD clock and the display clock.
  - Stage 3 continued: a cog's hub read or write, `INA` read or special-register write runs inside the local loop whenever the scheduler would take that event next (its key is below the other cogs' earliest event and no other cog's event has moved), a waiting hub operation is issued there, and the scheduler hands a running cog's due instruction or hub access back to it; NCO toggles are stepped in one loop, a pending point re-evaluates only the registers that change there, counter writes keep the pin and toggle caches of the state that lasts until they take effect, and the display is called only for its own pins. Translating event instructions into the blocks and running the worker ahead of the PIC32's calls were measured and rejected: the first costs more than it saves, the second changes the frame log's stamps and the pins' delivery points.
  - Host instructions per emulated second on one thread, attract / video: reference (Plan 9b) 15.2 / 16.5 G, events in the local loop 13.2 / 14.3 G, pins between change points 12.5 / 13.5 G. The scripted game: 6.06 T instructions for the reference, 4.94 T for this stage, its four logs byte-identical. Speed on the shared machine with the worker thread (niced): reference 0.85× attract and 0.87× video (loads 4.8 and 3.8); after this stage 1.24× attract and 1.08× video (loads 2.8 and 2.6), and the scripted game's 335 s in 333 s wall including its checks (load 2.8; the reference took 417 s at load 5.2).
  - Real time is reached for all three workloads on the shared machine. The Propeller thread is still the bound (about 70% of the work on one thread); the worker idles about 9% of the time after the PIC32's `RF13` reads (about 27,000 a second, one per link byte), where the two threads run in lock step. The aim of 1.2× for the video workload needs the Propeller thread about 10% (15–20% at load 3.5) cheaper at the loads measured.
- **Proof for every stage:**
'''),
])

edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
(r'''| m9c | 9, stage 3 continued: hub operations and `OUTA`/`INA`/`WAITCNT` in translated blocks, NCO clock toggles stepped per device, blocks linked, the worker running ahead | Plan 9b | none | ≥ 1.0× real time on the Ryzen 7 7730U for attract, scripted game and video+audio, byte-identical to the reference build | after Plan 9b |
''',
 r'''| `2026-09-30-pinheck-m9c-realtime.md` | 9c, stage 3 continued: event instructions in the Propeller's local loop while the scheduler would take them next, waiting hub operations issued there, NCO toggles stepped in one loop, pending points re-evaluating what changes there, counter writes keeping the pin caches | Plan 9b | none | byte-identical to the reference build (thread and translator on and off, scripted game); 1.24× attract, 1.08× video, the scripted game 1.0× with its logs (loads 2.6–2.8); the aim of 1.2× for video left open | written |
'''),
(r'''- After Plan 9b the Propeller thread is the bound: on one thread the Propeller takes about 71% of the time (events 27%, pin changes 17%, local code 26% of the whole, 14.5% of it translated) and the PIC32 24%. Real time needs the Propeller thread 15–28% cheaper, the aim of 1.2× 30–40%. `bench.sh` with `REFERENCE` (thread and translator on and off) stays the determinism check; the translated p8x32a suite (`P8X32A_JIT=1`) and `chip/jit.spin` cover the translator.
''',
 r'''- After Plan 9b the Propeller thread is the bound: on one thread the Propeller takes about 71% of the time (events 27%, pin changes 17%, local code 26% of the whole, 14.5% of it translated) and the PIC32 24%. Real time needs the Propeller thread 15–28% cheaper, the aim of 1.2× 30–40%. `bench.sh` with `REFERENCE` (thread and translator on and off) stays the determinism check; the translated p8x32a suite (`P8X32A_JIT=1`) and `chip/jit.spin` cover the translator.

## Carried into release

- After Plan 9c the three workloads run faster than real time on the shared machine (1.24× attract, 1.08× video, 1.0–1.1× the scripted game at loads 2.5–2.8); the Propeller thread is the bound. What remains for the aim of 1.2× with video: the worker's idle time after `RF13` reads (a frame log stamp that does not depend on the PIC32's calls would let the worker run ahead), the translated blocks' entry and exit, and the pin change points of the SD and display links. `bench.sh` with `REFERENCE` and the scripted game stay the determinism checks.
'''),
])
EOF
```

- [ ] **Step 5: Mutations**

Apply each mutation, run the named check, confirm the named failure line (other failure lines may follow), then restore the file with `git checkout <file>`. Each check is the suite's `check.sh`, run niced; the p8x32a suite takes about a minute, the link suite about 2.

| File | Replace → with | Check | Must show |
|---|---|---|---|
| `src/cpu/p8x32a/p8x32a.c` | in `event_run`, `h > t \|\| (h << 4 \| (uint64_t)n) >= lim \|\| m3 + 1` → `h > t \|\| m3 + 1` | p8x32a | `RTL MISMATCH chip/event_order.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `event_run`, `if (((now << 4) \| 8 \| (uint64_t)n) >= lim \|\| now + 2 >= c->disable_at) return 0;` → `if (now + 2 >= c->disable_at) return 0;` | p8x32a | `RTL MISMATCH chip/event_order.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `event_run`, `if (p->sched_gen != gen \|\| p->stop` → `if ((gen & 0) \|\| p->stop` | p8x32a | `RTL MISMATCH chip/event_order.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `run_local`'s issue of a waiting hub operation, `c->d = ram[e->dst];` → `c->d = 0;` | p8x32a | `RTL MISMATCH chip/cogs.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `hub_rw`, `if (FWZ(i)) fl = (fl & ~1u) \| (unsigned)!r;` → the same with `!!r` | p8x32a | `RTL MISMATCH chip/idle_hub.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `p8x32a_run_until`, `if (!b->run \|\| p->loop[best].state != LOOP_SEARCH \|\| !run_local(p, best, t, bk2, gen)) exec(p, best);` → `if (!run_local(p, best, t, bk2, gen)) exec(p, best);` | p8x32a | `RTL MISMATCH chip/clk_idle.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `nco_step`, the line `p->nco_lvl[j] ^= nco_pins(when < g->ctr_at[c] ? g->ctr_old[c] : g->ctr[c]);` → nothing | p8x32a | `RTL MISMATCH chip/ctr_frq.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `pins_what`, the line `else if (p->nco_nt[j] == t) p->nco_lvl[j] ^= nco_pins(t < g->ctr_at[c] ? g->ctr_old[c] : g->ctr[c]);` → nothing | p8x32a | `RTL MISMATCH chip/event_order.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `pins_what`, `if (what >> n & 1) {` → `if (0) {` | p8x32a | `RTL MISMATCH chip/cogs.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `ctr_save`, the line `if (e < p->nco_nt[j]) p->nco_nt[j] = e;` → nothing | p8x32a | `RTL MISMATCH chip/ctr_frq.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `ctr_save`, the line `p->ctr_ok = 0;` after `} else {` → nothing | p8x32a | `RTL MISMATCH chip/ctr_frq.spin` |
| `src/wpc/pinheck/prop.c` | in `pins_out`, `if (p->pins && (ch & p->pins_mask)) p->pins(` → `if (p->pins) p->pins(` | link | `PROP FAIL prop_test.c:238: calls[0] > 2 && calls[1] == calls[0] && calls[2] == 1` |

Then `git status --short src tests` prints nothing.

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "Spec: Milestone 9 stage 3 continued measured; real time reached"
```

## Result

Measured in the replay on the Ryzen 7 7730U while other agents' emulators and builds shared it; the load average is printed with every speed. The single-thread host instructions per emulated second (`PINHECK_THREADS=0`, the whole run) do not depend on the load; the wall-clock speeds do.

| Build | attract: G instructions / emulated s, one thread | attract: speed with the thread (load) | video + four channels: G instructions, one thread | video: speed with the thread (load) |
|---|---|---|---|---|
| reference (Plan 9b result) | 15.2 | 0.852× (4.8); 0.927× (2.6) | 16.5 | 0.867× (3.8); 0.794× (2.9) |
| Task 2: events in the local loop | 13.2 | 1.214× (2.3) | 14.3 | 1.065× (2.2) |
| Task 3: pins between change points | 12.5 | 1.237× (2.8); 1.116× (3.0) | 13.5 | 1.083× (2.6); 0.970× (3.1) |
| Task 3, interpreter on one thread (`PINHECK_THREADS=0 PINHECK_JIT=0`) | 14.9 | 0.920× on one thread (1.5) | 16.1 | 0.822× on one thread (1.6) |

The second figures of the reference and of Task 3 are a paired rerun, the reference and then Task 3 back to back, after Task 4's steps, while the mutations of Task 4 Step 5 ran beside them. Every row is byte-identical to the reference in UART1, frames, sound and NVRAM, with the worker thread and the translator on and off.

The scripted game (Task 4 Step 2), 335 s of emulated play in two launches with its four logs: the reference executed 6.06 T host instructions in 417 s (load 5.2), this plan's build 4.94 T in 333 s (load 2.8), including both launches' start and the game's planning and checks; its logs are byte-identical. Timed on the prototype (the same code) at load 2.5, the game's second launch alone ran at 1.115× with its logs and 1.112× without them.

Where the time goes after Task 3 (Task 4 Step 3, video on one thread, so that the shares add up): the Propeller about 70% of the work (its core 48.5%, translated code 16.8%, `prop.c` 2.9%, the SD card, display and audio devices 2.3%), the PIC32 26%. By symbol: the local loop with `event_run` and the block entry 19.7%, `flush` 10.0%, the scheduler 6.9%, `hub_rw` 3.5%, `pins_out` 2.6%, `ctr_next` 2.4%. With the worker thread the Propeller thread is the bound; it idles about 9% of the time, mostly after the PIC32's `RF13` reads, where the threads run in lock step (Ruling 7).

What real time with margin still needs: at loads up to about 2.8 all three workloads run faster than real time and attract mode above the aim of 1.2×; at a load of about 3 the video workload falls to about 0.97×. The aim of 1.2× for video needs the Propeller thread about 10% cheaper at load 2.6 and 15–20% at load 3.5. The candidates, by share: the worker's idle time after `RF13` reads (running it ahead needs a frame log whose stamps and hub addresses do not depend on where the PIC32's calls cut the Propeller's runs, Ruling 7), the translated blocks' entry, exit and shared tail (about a quarter of the translated code's time), the scheduler's search after each declined event (keeping the three earliest keys instead of searching), and the pin change points of the SD and display links (`flush`, `pins_out` and the devices, about a fifth of the Propeller's time).

## Self-Review

**Spec coverage** (addendum §4):

| Requirement | Where |
|---|---|
| target ≥ 1.0× on the 7730U, sound never under-running | the Result: attract mode, video with four channels and the scripted game faster than real time at the printed loads; Ruling 12 for the sound stream |
| headless benchmark over attract mode, the scripted game, video with four channels | Plan 9's `bench.sh` after every task (attract, video, with the thread and on one thread); Task 4 Step 2 (the scripted game with both builds) |
| stage 1: `perf` profile | Task 1 Step 3 and Task 4 Step 3; Ruling 2 (the counts behind the profile) |
| stage 3: a JIT only for the core the profile still blames | the profile blamed the Propeller's event path and pin changes, which are not translated code: Tasks 2 and 3 change those; event slots in translated blocks were measured and rejected (Ruling 6); the translator is unchanged |
| stage 4: multi-threading only if deterministic | Plan 9b's worker thread is unchanged; running it ahead and splitting the Propeller were measured and rejected (Rulings 7, 8) |
| proof for every stage: all suites incl. RTL, spinsim, QEMU | Task 2 Step 4, Task 3 Step 4 (both with and without the translator), Task 4 Step 1 |
| proof for every stage: byte-identical to the reference | `bench.sh` with `REFERENCE` after Tasks 2 and 3, with the thread, on one thread, and without the translator (Task 3 Step 5); the scripted game's four logs (Task 4 Step 2) |

**Placeholder scan:** no TBD/TODO; every code step carries its full code or an anchored edit script; every Run has its Expected output from the proving replay (speeds as measured, with the load).

**Type consistency:** `event_run(p8x32a *, int, const p8x32a_dec *, uint32_t, unsigned, unsigned *, uint64_t *, uint32_t *, uint64_t, uint64_t, unsigned)` and `hub_rw(p8x32a *, int, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, unsigned)` are static in `p8x32a.c` and called by `run_local` (Task 2); `run_local(p8x32a *, int, uint64_t, uint64_t, unsigned)` returns `int` and is called by the scheduler for `EV_EXEC`, `EV_HUB` and local runs with `bk2` and `gen`; `add_pending(p8x32a *, uint64_t, uint32_t, uint32_t)` and `regset(p8x32a *, int, p8x32a_reg *, uint32_t, uint64_t)` have every caller updated in Task 3 (`regset` in `special_write` and `stop_cog`, `add_pending` in `regset`, `ctr_save` and `special_write`); `P8X32A_PEND_COG`, `P8X32A_PEND_CTR`, `pend_what` and `cog_out` are declared in `p8x32a.h`; `prop_set_pins_mask(pinheck_prop *, uint32_t)` is declared in `prop.h`, defined in `prop.c` and called by `pinheck.c` and `prop_test.c`.

**Review Focus:** five items, each pinned by the named tests in Tasks 2–4 and the mutations of Task 4.

**Proof:** every file and edit this plan writes was extracted from this document (its edit scripts and `chip/event_order.spin`) into a fresh worktree of `pinheck` at `d8ddb9e9` with Plan 9b's result as one commit (`37fa7861`), task by task, and the source and test files it produced are byte-identical to the proven prototype's; every Expected output above was reproduced there on a machine shared with other agents' emulators and builds (the load averages are printed with the speeds). Each mutation of Task 4 was caught with the named line, and the tree was clean afterwards. The prototype passed every suite and machine check and the scripted game before the replay; the replay passed them again (Task 4 Steps 1 and 2).
