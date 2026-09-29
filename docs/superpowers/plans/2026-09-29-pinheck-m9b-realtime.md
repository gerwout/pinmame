# Milestone 9b: performance, stages 3 and 4 (Propeller engine and translator, worker thread) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Domino's in `sdl3pinmame` runs 1.6–1.8 times as fast as after Plan 9 in attract mode and 1.3–1.6 times with video and four sound channels (0.75–0.85× and 0.58–0.72× real time on the shared 7730U, from 0.47× and 0.44×), with the UART1 log, display frames, sound and NVRAM still byte-identical to the reference build's: the Propeller core's pin, counter and event paths and its local instructions get exact fast paths, its local instruction runs are translated to x86-64 code through the vendored asmjit, the MIPS32 core gets a fast interpreter loop, and the Propeller runs on a worker thread beside the PIC32; what real time still needs is measured and stated.

**Architecture:** The Propeller core (`p8x32a`) keeps its event scheduler and pin model but spends less per event: the NCO counters' next changes are cached per counter and their pin levels flipped at each change, the register part of the pins is kept between change points, instructions are decoded once per word into a cache checked by the whole word (so self-modifying code needs no invalidation), a cog's local instructions run in a lean loop and its hub operations are issued ahead, and a cog that stays the earliest keeps running without a new search. `p8x32ajit.cpp` translates runs of local instructions to x86-64 with asmjit, block by block, each instruction doing exactly what the interpreter does; a write to a translated slot drops its blocks, slots whose S or D field keeps changing are read at run time, and blocks continue into each other while nothing needs the interpreter. The MIPS32 core runs instructions that raise no exception and touch only registers and direct memory in a loop that keeps its state in locals and hands every other instruction to the unchanged step. `prop.c` can run its calls on a worker thread: they queue in order and run there exactly as they would inline, and the PIC32 side waits for the queue only where it reads Propeller state (RF13, sound, video, NVRAM, reset and the bootloader phase).

**Tech Stack:** C (C89-syntax-clean cores and `prop.c`, `-std=c99 -Wall -Wextra -Werror -pedantic` test builds), C++17 and the vendored asmjit 1.21 (`ext/asmjit`, x86-64 backend) for the optional translator, POSIX threads or Win32 threads, `perf` 7.1, Python 3 (no third-party modules), POSIX sh, the existing Verilator P1 RTL, spinsim and QEMU oracles.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §4 (binding), parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` §5 and §7 (binding where the addendum is silent). Plan 9 (`docs/superpowers/plans/2026-09-29-pinheck-m9-performance.md`) provides the benchmark, the determinism method and stages 1–2 this plan builds on.

## Prerequisites

- Plan 9 executed, reviewed and merged (`pinheck-m9` `1f2ef40b` or later). Proven and replayed on exactly that commit. This plan edits Plan 3's and Plan 9's `p8x32a.[ch]` and test runner, Plan 1's and Plan 9's `mips32.[ch]`, Plan 5's `prop.[ch]` and the machine driver `pinheck.c`, `cmake/asmjit.cmake` and `src/unix/unix.mak`, and adds the translator `src/cpu/p8x32a/p8x32ajit.{h,cpp}` and the RTL test `tests/pinheck/p8x32a/chip/jit.spin`. The board, display, audio and storage blocks do not change.
- Plan 8b (the scripted game) is needed only for Task 7 Step 3, which runs the game with both builds when it is present. The plan's edits also apply unchanged to `pinheck` at `9151f96b`, which has Plan 8b and the display look merged.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export: `TOOLS=$PWD/tests/pinheck/p8x32a/build/tools` (or the main checkout's, where the oracles are built); `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = the romset zip; `DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN`; `DOMINOS_ZIP=$PINHECK_ZIP`. Every command below uses these.
- `perf` may count user-space events: `sudo sysctl kernel.perf_event_paranoid=1` after a reboot. A C++17 compiler (`c++`) builds the translator for the test runner.
- The p8x32a suite builds its RTL boot references once (about 10 minutes); copying `tests/pinheck/p8x32a/build/boot/*.rtl*` from a checkout that already has them skips that.
- The reference machine is shared: other emulators and builds run beside these steps. Wall-clock speeds below were measured at the load averages printed with them; the instruction counts do not depend on load. With the worker thread the benchmark's CPU seconds include the time each thread spends waiting for the other, so they no longer add up to the work done. Every PinMAME run has a `timeout -k 30`.

## Global Constraints

- Exactness: every change leaves the machine's behaviour unchanged, cycle for cycle. The RTL, spinsim and QEMU differential suites stay byte-identical, with and without the translator, and `bench.sh` with `REFERENCE` requires the UART1 log, the display frame log, the sound capture and the NVRAM of each workload to equal the reference build's byte for byte, with the worker thread and the translator on (the default) and off (`PINHECK_THREADS=0`, `PINHECK_JIT=0`).
- The reference build is `pinheck` at the commit this plan starts from, built Release exactly as below, and copied to `build/reference/sdl3pinmame` before any source changes. Plan 9 proved it byte-identical to the build before Plan 9.
- The cores and `prop.c` stay C, C89-syntax-clean (`-std=c89 -pedantic-errors -Wno-long-long`, which the suites gate) and warning-free in the test builds; they include no PinMAME headers. The translator is the one C++ file, `p8x32ajit.cpp` (C++17, asmjit); it is optional: without it every result is the same, only slower. The core structs hold no pointers into themselves; the translator's blocks are shared code, not machine state (Ruling 7).
- Test hooks stay inert unless set. `PINHECK_THREADS=0` and `PINHECK_JIT=0` are the only new switches; both default to on.
- Every sdl3pinmame launch uses private `-nvram_directory` and `-cfg_directory` and a `timeout -k 30`.
- Every existing suite stays green: `tests/pinheck/{mips32,pic32mx,storage,link,p8x32a,display,audio,board}/check.sh`, the p8x32a suite also with `P8X32A_JIT=1`, `pic32mx/pinmame_check.sh` (with `romset_check.sh`), `display/pinmame_display.sh`, `audio/pinmame_audio.sh`, `board/pinmame_board.sh`, and, once the display look is merged, `display/pinmame_look.sh`.
- Code comments stay short and factual.

## Rulings

Decisions the spec left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it). The profile figures are from this plan's prototype, measured in attract mode with `perf` on the Plan 9 build and each stage after it.

1. **Scope: stage 3 for the Propeller, stage 4, and what real time still needs.** Spec §4 takes the stages "only as far as needed" and asks for ≥ 1.0× with the sound never under-running. After Task 6 the emulation runs at 0.75–0.85× in attract mode and 0.58–0.72× with video and four channels (the Result; Plan 9 ran at 0.47× and 0.44× on the same machine), still below 1.0×: the Propeller worker thread is the bound (the PIC32 thread waits for it about half its time). The Result section itemises its remaining cost and what a next stage would have to remove. Cost if wrong: none for correctness; each stage is exact and measured on its own.
2. **The Propeller's time is in events and pin changes, so the interpreter paths come first.** After Plan 9 the Propeller core spends its time per emulated second on about 42 M local instructions of the two Spin-interpreter cogs and the drivers (about 100 host instructions each), about 8 M scheduler events (hub operations, `OUTA`/`INA` instructions, waits) and about 5.6 M pin change points, nearly all of them toggles of the NCO counters that clock the SD card (P1) and the display link (P22), each re-evaluating every counter and every cog's pins. A translator only speeds up the local instructions, so Tasks 2 and 3 first make the pin, counter and event paths cheaper and the local loop leaner, exactly; Task 6 then translates the local runs.
3. **The MIPS32 core gets a fast loop, not a translator.** With the worker thread (Task 5) the PIC32 runs on its own thread, which waits for the Propeller about half its time; a MIPS32 translator would not raise the speed. Task 4's loop keeps pc, delay state and cycles in locals, runs every instruction that raises no exception, touches only registers and direct memory and changes no CP0 state, and hands every other instruction, unchanged, to Plan 1's `step`. It runs 99.7% of the firmware's instructions and about halves the PIC32's cost. Cost if wrong: a later plan can translate the MIPS32 the same way as the Propeller; the flash code is stable.
4. **A decoded-instruction cache, checked by the whole word.** Plan 9's Ruling 13 rejected a pre-decoded table because cog code rewrites itself. `p8x32a_dec` keeps one decoded entry per cog address and is used only while its stored word equals the word the cog fetched, so a changed word is decoded again and nothing needs invalidating; the cog's prefetched word, not cog RAM, is what the check compares, which keeps the P1's fetch-before-write behaviour. The local loop keeps Z, C and cancel in one flag word, tests an instruction's condition with one shift, checks the cog's stop time against one precomputed limit and evaluates the carry of logic instructions only when `WC` asks for it.
5. **The translator.** `p8x32a_jit_build` translates a run of local instructions starting at a cog address: the ones `run_local` computes in its own loop plus `ROR`–`SAR`, `RCR`/`RCL`, `MUXC`–`MUXNZ`, `CMPS` and `TJZ`/`TJNZ`, with immediate or cog-RAM (not special-register) sources and non-special destinations. Each translated instruction does what `run_local` does for it, in the same order: condition, operands, result, the next word fetched before the write, the write (marking the loop dirty), flags. A run leaves after a jump that runs (with the jump's target word, `jc` and operands for `run_local`), before a slot whose word no longer fits, and ends before address `$1F0` and before any other instruction. Self-modifying code: a write that changes a slot some block translated as fixed is reported and drops those blocks (`jit_written`); a slot whose words have differed only in their S and D fields is translated to read its word at run time, fetched one instruction ahead as the cog fetches it; a fixed instruction that writes a later fixed slot of its own run other than the next ends the run there; a slot whose other bits change is left to the interpreter. After each block a shared tail does `loop_edge`'s search step for a backward jump and continues in the next block when no cancel, code change, stop time or record state needs `run_local`. The p8x32a suite runs with the translator (`P8X32A_JIT=1`), and `chip/jit.spin` checks the self-modifying cases against the RTL. Cost if wrong: the translated suite and the benchmark's byte comparison catch a mistranslated instruction; `PINHECK_JIT=0` turns the translator off.
6. **The worker thread keeps the sequential order.** `prop_catch_up` and `prop_pic_pins` queue their calls, with the PIC32 cycle at which each was made; the worker runs them in that order, so the Propeller sees exactly the calls it would inline, split at the same points. The PIC32 side waits for the queue to drain (`prop_sync`) before it reads Propeller state: `prop_p24` (RF13), `prop_time` (the sound stream), the video update, the NVRAM handler, `prop_reset`, and every hold while the bootloader stand-in is not released (it reads the Propeller's UART). Once released the stand-in neither holds the PIC32 nor answers the Propeller (`boot_advance` then only counts ignored frames), so `pinheck_hold` skips it. The display frame log stamps each frame with `prop_stamp`, the PIC32 cycle of the call that produced it, which is the value the sequential build read from the CPU. The worker does not run ahead of the calls (no progress markers): a run cut at a point the sequential build never cuts would end a session or a reset with the Propeller further on than the reference's, and the prototype measured it idle for only about 9% of its time. Cost if wrong: `PINHECK_THREADS=0` runs every call inline; `prop_test` and the benchmark compare both.
7. **The translator's blocks are shared code, not machine state.** `p8x32a` holds pointers to the translator and its blocks and the per-cog code bitmaps (`jcode`) that report writes to them; a copy of a `p8x32a` would share the blocks but not the bitmaps, so a copy must clear `jit_build` or get its own translator. Nothing copies a `p8x32a` today (the idle-loop snapshots copy cog state only). The decoded-instruction cache is plain state and copies with the core.
8. **Platforms and fallback.** The translator is built only where `cmake/asmjit.cmake` builds asmjit (x86, x64 and AArch64 targets of the CMake builds, `PINMAME_JIT_ASMJIT`); `p8x32ajit.cpp` translates only on x86-64 and elsewhere returns no translator, so 32-bit Windows, AArch64, the makefile builds (xpinmame, LISY on ARM) and the Visual Studio projects run the interpreter with the same results. The worker thread uses POSIX threads or Win32 threads and the compilers' atomics (`__atomic` builtins with GCC and Clang, `InterlockedExchange` and `MemoryBarrier` with MSVC), not C11's; `unix.mak` links `-lpthread` for the makefile builds; a compiler with neither can build with `PINHECK_NO_THREADS`, which runs every call inline. `PINHECK_THREADS=0` and `PINHECK_JIT=0` switch them off at run time.
9. **Counter changes and pin levels.** `ctr_next` scans only the counters in an NCO mode now or before their last write (`nco_list`), each with its own cached next change; `flush` keeps the register part of the pins from the last pending change point (`pins_regs`), where every `OUTA`, `DIRA` and counter write takes effect, and between such points flips the level of each counter whose cached next change is now (`pins_nco`): an NCO output is the phase's top bit, so it inverts at each change. `nco_toggle`'s quotients fit 32 bits and use a 32-bit division.
10. **Hub operations issue ahead, and the earliest cog keeps going.** `exec` for a hub operation or a `WAITCNT`/`WAITPxx` with an immediate or cog-RAM source reads only the cog's own state and the hub slot phase, so it runs ahead with the local instructions (`issue_local`); the access itself stays an event in time order. After an event the scheduler lets the same cog continue without a new search while its next event stays before the second earliest one found by the last search and no other cog's event has moved (`sched_gen` counts `sys`, `loop_notify` and `loop_hub_write` changing another cog's event). `complete` computes a hub operation's result directly instead of through `alu`.
11. **Devices see only their own pin changes.** `prop.c` calls the EEPROM, the SD card, the sound and the UART only when one of their pins changed since the last call; all four act only on changes of their inputs (edge-driven state machines; `audio_pins` already compared), so their state and answers are the same. The display callback still sees every change.
12. **Measuring with two threads.** `bench.sh` is Plan 9's: speed over the timed window, instructions and CPU seconds per emulated second over the whole run. With the worker thread the CPU seconds include both threads' waiting (spinning), so the wall-clock speed at the printed load and the instruction count are the figures to compare; `PINHECK_THREADS=0` gives the single-thread CPU cost.

## Review Focus

- **Cog code that rewrites itself under translation** (the next instruction, which the P1 has already fetched; an instruction two or more ahead; a dispatch `JMP` whose target another instruction patches every time; a loop that rewrites one of its own instructions; a cog reloaded by `COGINIT`): the cog must execute exactly the words the RTL executes. `chip/jit.spin` (Task 6) runs the first four against the RTL with and without the translator, and the whole p8x32a suite, whose boot tests load and restart cogs, runs translated; Task 7's mutations of the fixed-slot rule, the run-time slot check and the write report fail it.
- **The PIC32 side reading Propeller state while the worker runs** (RF13, the sound stream, the video update, NVRAM at exit, a reset, the bootloader window): it must see the state the sequential build would. `prop_test`'s threaded case (Task 5: the same calls with and without the worker give the same hub RAM, RF13 answers and frame stamps; removing `prop_p24`'s `prop_sync` fails it), the benchmark's byte comparison with the thread on and off, and the machine checks, which update from blank NVRAM through the bootloader window and reset.
- **Idle loops under translation** (a polling loop whose backward jump now runs in translated code): it must still be detected and put to sleep at the same cycle. The idle-loop RTL tests pin their sleep counts (`EXPECT-SLEEPS`) in the translated suite; removing the tail's search step fails them (Task 7).
- **A cog stopped, restarted or woken by another cog while one cog keeps going without a new search** (`COGSTOP`, `COGINIT`, a hub write or pin change waking a sleeper): the other cog's event must still come first. `chip/wake_order.spin` in Task 3 (pulses of 4–12 cycles from a busy cog wake a sleeping one; without `loop_notify`'s count in `sched_gen` it sleeps 12 times instead of 8 and misses them), the `cogs`, `restart` and `idle_*` RTL tests.
- **Builds without the translator or threads** (Visual Studio projects, the makefile builds, ARM): the same results, only slower. The C89 gates, the p8x32a suite without `P8X32A_JIT`, `PINHECK_NO_THREADS` compiling in the link suite, and the benchmark with `PINHECK_THREADS=0 PINHECK_JIT=0` byte-identical to the reference (Task 7).

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/p8x32a/p8x32a.[ch]` | per-counter change caches and NCO pin levels, the register part of the pins between change points (Task 2); decoded-instruction cache, lean local loop, `alu_run` for more ops with lazy carry, hub operations issued ahead, the earliest cog continuing (Task 3); translated blocks: lookup, write reports, the block call in the local loop (Task 6) (modified) |
| `src/cpu/p8x32a/p8x32ajit.h`, `p8x32ajit.cpp` | the translator: x86-64 code for runs of local instructions through asmjit, the shared block tail; no translator on other targets |
| `src/cpu/mips32/mips32.[ch]` | the fast loop (`fast_run`) for exception-free register and direct-memory instructions (modified) |
| `src/wpc/pinheck/prop.[ch]` | devices called on their own pin changes (Task 2); the worker thread, the call queue, `prop_sync`, `prop_stamp` (Task 5) (modified) |
| `src/wpc/pinheck.c` | starts and stops the worker and the translator, waits for the worker where it reads Propeller state, stamps frames with `prop_stamp` (modified) |
| `cmake/asmjit.cmake` | builds `p8x32ajit.cpp` with the ARM7 translator wherever asmjit is built (modified) |
| `src/unix/unix.mak` | links `-lpthread` in the makefile builds (modified) |
| `tests/pinheck/p8x32a/alu_test.c` | calls `alu_run` with its carry request (modified) |
| `tests/pinheck/p8x32a/chip/jit.spin` | self-modifying and translated cog code against the RTL |
| `tests/pinheck/p8x32a/chip/wake_order.spin` | a sleeping cog woken by a busy cog's short pulses, against the RTL |
| `tests/pinheck/p8x32a/check.sh`, `run.c` | `P8X32A_JIT=1`: every test runs with the translator (modified) |
| `tests/pinheck/link/prop_test.c`, `check.sh` | the same calls with and without the worker thread; builds with `-pthread` and without threads (modified) |
| `tests/pinheck/perf/bench.sh` | `profile` also prints the translated code's share (modified) |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | measured results and what remains (modified) |

---
### Task 1: Reference build and baseline

**Files:** none changed.

**Interfaces:**
- Consumes: Plan 9's `tests/pinheck/perf/bench.sh` (`bench.sh [attract] [video]`, `SDL3PINMAME`, `REFERENCE`) and its run directories.
- Produces: `build/reference/sdl3pinmame`, the reference every later task compares with, and its cached benchmark runs.

- [ ] **Step 1: Build the reference**

Build `pinheck` as it stands (Release, `PLATFORM=linux`, `ARCH=x64`, never the user's `build-dbg`) and keep the binary as the reference. Builds run niced and one at a time on the shared machine:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9b.log 2>&1; tail -1 build/sdl3pinmame/m9b.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```
Expected: `[100%] Built target sdl3pinmame`.

(`/code/spooky_domino/work/heavy.lock` is the shared machine's lock file; on another machine drop the `flock` prefix.)

- [ ] **Step 2: The baseline**

Run: `SDL3PINMAME=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video`
Expected (about 3 minutes; speeds vary with the load):
```text
bench attract opt: 0.471x over 30.0 s emulated (63.6 s wall), whole run 22.4 G instructions and 2.0 s CPU per emulated s, load 4.54
bench video opt: 0.444x over 24.0 s emulated (54.1 s wall), whole run 24.2 G instructions and 2.1 s CPU per emulated s, load 4.17
```

The later tasks run the reference beside each build with `REFERENCE=build/reference/sdl3pinmame`; its runs are kept while its binary is the same, so it is benchmarked once.

### Task 2: Propeller pins and counters (stage 3a)

**Files:**
- Modify: `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32a.h`, `src/wpc/pinheck/prop.c`, `src/wpc/pinheck/prop.h`

**Interfaces:**
- Consumes: Plan 9's `ctr_next`, `ctr_toggle`, `nco_toggle`, `flush`, `add_pending`, `ctr_save`, `loop_resume`; Plan 5's `pins_out` in `prop.c`.
- Produces: `p8x32a` gains `nco_mask`, `nco_ok`, `nco_n`, `nco_list[16]`, `nco_from[16]`, `nco_nt[16]` (the NCO counters and their cached next changes), `pins_ok`, `reg_out`, `reg_dir`, `cog_dir[8]`, `nco_lvl[16]` (the pins between change points); `static void ctr_mask(p8x32a *p, int n, int k)` (after any write to counter `k` of cog `n`); `pinheck_prop` gains `po_out`, `po_dir`, `po_ok` (the pins the devices last saw; `po_ok = 0` makes every device see the next change).

The counters' next changes are cached per counter and scanned only for counters in an NCO mode; between pending change points `flush` keeps the register part of the pins and inverts the output of each counter that changes; `prop.c` calls each device only on a change of its own pins (Rulings 9, 11).

- [ ] **Step 1: The pin and counter paths**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.c', [
(r'''	if (p->sleepers) loop_notify(p, t, pins);
	for (k = 0; k < p->npend; k++)
''',
 r'''	if (p->sleepers) loop_notify(p, t, pins);
	p->pins_ok = 0;
	for (k = 0; k < p->npend; k++)
'''),
(r'''	if (f < 0x80000000u) dt = ((uint64_t)0x80000000u - x + f - 1) / f;
	else dt = (uint64_t)x / (0x100000000ull - f) + 1;
''',
 r'''	/* both quotients fit 32 bits: 0x80000000 - x + f - 1 < 2^32 for f < 2^31, and x < 2^31 */
	if (f < 0x80000000u) dt = (uint32_t)(0x80000000u - x + f - 1) / f;
	else dt = x / (uint32_t)(0u - f) + 1;
'''),
(r'''/* the next counter pin change after t; cached until a counter changes or t reaches it */
static uint64_t ctr_next(p8x32a *p, uint64_t t)
{
	uint64_t nt = P8X32A_NEVER, e;
	int n, k;
	if (p->ctr_ok && t >= p->ctr_from && t < p->ctr_nt) return p->ctr_nt;
	for (n = 0; n < 8; n++)
		for (k = 0; k < 2; k++)
			if ((e = ctr_toggle(&p->cog[n], k, t)) < nt) nt = e;
''',
 r'''/* the next counter pin change after t; cached, per counter and in all, until a counter changes or t reaches it */
static uint64_t ctr_next(p8x32a *p, uint64_t t)
{
	uint64_t nt = P8X32A_NEVER;
	int k;
	if (p->ctr_ok && t >= p->ctr_from && t < p->ctr_nt) return p->ctr_nt;
	for (k = 0; k < p->nco_n; k++) {
		int j = p->nco_list[k];
		if (!(p->nco_ok >> j & 1) || t < p->nco_from[j] || t >= p->nco_nt[j]) {
			p->nco_nt[j] = ctr_toggle(&p->cog[j >> 1], j & 1, t);
			p->nco_from[j] = t;
			p->nco_ok |= (uint16_t)(1u << j);
		}
		if (p->nco_nt[j] < nt) nt = p->nco_nt[j];
	}
'''),
(r'''		uint32_t dd = regval(&c->dira, t);
		if (!dd) continue;
		d |= dd;
		o |= (regval(&c->outa, t) | ctr_pins(c, 0, t) | ctr_pins(c, 1, t)) & dd;
''',
 r'''		uint32_t dd = regval(&c->dira, t), v;
		if (!dd) continue;
		d |= dd;
		v = regval(&c->outa, t);
		if (p->nco_mask >> (2 * n) & 1) v |= ctr_pins(c, 0, t);
		if (p->nco_mask >> (2 * n) & 2) v |= ctr_pins(c, 1, t);
		o |= v & dd;
'''),
(r'''static void flush(p8x32a *p, uint64_t t)
{
	for (;;) {
		int k, best = -1;
''',
 r'''/* the pins at t from the register part and the NCO outputs: evaluated (full), or the outputs of the counters that
   change at t inverted (ctr_next(t) found t as their next change) */
static uint32_t pins_nco(p8x32a *p, uint64_t t, int full)
{
	uint32_t o = p->reg_out;
	int k;
	for (k = 0; k < p->nco_n; k++) {
		int j = p->nco_list[k], n = j >> 1, c = j & 1;
		const p8x32a_cog *g = &p->cog[n];
		if (full) p->nco_lvl[j] = ctr_pins(g, c, t);
		else if (p->nco_nt[j] == t) p->nco_lvl[j] ^= nco_pins(t < g->ctr_at[c] ? g->ctr_old[c] : g->ctr[c]);
		o |= p->nco_lvl[j] & p->cog_dir[n];
	}
	return o;
}

static void pins_regs(p8x32a *p, uint64_t t)
{
	int n;
	p->reg_out = p->reg_dir = 0;
	for (n = 0; n < 8; n++) {
		const p8x32a_cog *c = &p->cog[n];
		uint32_t dd = regval(&c->dira, t);
		p->cog_dir[n] = dd;
		if (!dd) continue;
		p->reg_dir |= dd;
		p->reg_out |= regval(&c->outa, t) & dd;
	}
	p->pins_ok = 1;
}

static void flush(p8x32a *p, uint64_t t)
{
	for (;;) {
		int k, best = -1, full;
'''),
(r'''		out = p8x32a_pins(p, when, &dir);
''',
 r'''		/* registers change only at pending points: between them only the counters move */
		full = !p->pins_ok || (best >= 0 && p->pend[best] == when);
		if (full) pins_regs(p, when);
		out = pins_nco(p, when, full);
		dir = p->reg_dir;
'''),
(r'''	c->ctr_at[k] = e;
}
''',
 r'''	c->ctr_at[k] = e;
	p->nco_ok &= (uint16_t)~(1u << (2 * (c - p->cog) + k));
}

/* after a write to a counter: is it, or was it before the write, an NCO */
static void ctr_mask(p8x32a *p, int n, int k)
{
	const p8x32a_cog *c = &p->cog[n];
	uint16_t b = (uint16_t)(1u << (2 * n + k));
	int j;
	if (nco(c->ctr[k]) || nco(c->ctr_old[k])) p->nco_mask |= b;
	else p->nco_mask &= (uint16_t)~b;
	p->nco_n = 0;
	for (j = 0; j < 16; j++)
		if (p->nco_mask >> j & 1) p->nco_list[p->nco_n++] = (uint8_t)j;
}
'''),
(r'''	case 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; add_pending(p, e, nco_pins(v)); ctr_check(p, v); ctr_notify(p, n, k, e); break;
''',
 r'''	case 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; add_pending(p, e, nco_pins(v)); ctr_mask(p, n, k); ctr_check(p, v); ctr_notify(p, n, k, e); break;
'''),
(r'''	memcpy(SNAP(c), l->snap[j], P8X32A_SNAP);
	c->ev_t += shift;
''',
 r'''	memcpy(SNAP(c), l->snap[j], P8X32A_SNAP);
	p->pins_ok = 0;
	c->ev_t += shift;
'''),
(r'''	c->ctr[0] = c->ctr[1] = 0;
	ctr_notify(p, n, 0, d);
''',
 r'''	c->ctr[0] = c->ctr[1] = 0;
	ctr_mask(p, n, 0);
	ctr_mask(p, n, 1);
	ctr_notify(p, n, 0, d);
'''),
(r'''	p->ctr_ok = 0;
	p->sleepers = 0;
''',
 r'''	p->ctr_ok = 0;
	p->nco_mask = p->nco_ok = 0;
	p->nco_n = 0;
	p->pins_ok = 0;
	p->sleepers = 0;
'''),
])

edit('src/cpu/p8x32a/p8x32a.h', [
(r'''	uint64_t ctr_from, ctr_nt;
	uint8_t sleepers;
''',
 r'''	uint64_t ctr_from, ctr_nt;
	uint16_t nco_mask, nco_ok; /* counters (2 * cog + ctr) with an NCO mode now or before their last write; cached next changes */
	uint8_t nco_n, nco_list[16];
	int pins_ok; /* reg_out, reg_dir, cog_dir, nco_lvl hold the pins since the last change point */
	uint32_t reg_out, reg_dir, cog_dir[8], nco_lvl[16];
	uint64_t nco_from[16], nco_nt[16];
	uint8_t sleepers;
'''),
])

edit('src/wpc/pinheck/prop.c', [
(r'''#define PIN_SDA  (1u << 29)

''',
 r'''#define PIN_SDA  (1u << 29)
#define PIN_SND  ((1u << 14) | (1u << 15))

'''),
(r'''static void pins_out(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	int scl = (dir & PIN_SCL) ? (out & PIN_SCL) != 0 : 1;
	int sda = (dir & PIN_SDA) ? (out & PIN_SDA) != 0 : 1;
	int tx = (dir & PIN_P25) ? (out & PIN_P25) != 0 : 1;
	if (p->tx && tx != p->tx_level) {
		p->tx_level = tx;
		p->tx(p->tx_ctx, to_pic(p, t), tx);
	}
	if (p->snd_pins) p->snd_pins(p->snd_ctx, t, out, dir);
	p->ee_bits = PIN_SCL | (cat24m01_update(&p->eeprom, scl, sda) ? PIN_SDA : 0);
	if (p->sd) {
''',
 r'''/* each device sees a change of its own pins; the EEPROM, SD card, UART and sound only act on those */
static void pins_out(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	uint32_t ch = p->po_ok ? (out ^ p->po_out) | (dir ^ p->po_dir) : 0xFFFFFFFFu;
	p->po_out = out;
	p->po_dir = dir;
	p->po_ok = 1;
	if (ch & PIN_P25) {
		int tx = (dir & PIN_P25) ? (out & PIN_P25) != 0 : 1;
		if (p->tx && tx != p->tx_level) {
			p->tx_level = tx;
			p->tx(p->tx_ctx, to_pic(p, t), tx);
		}
	}
	if (p->snd_pins && (ch & PIN_SND)) p->snd_pins(p->snd_ctx, t, out, dir);
	if (ch & (PIN_SCL | PIN_SDA)) {
		int scl = (dir & PIN_SCL) ? (out & PIN_SCL) != 0 : 1;
		int sda = (dir & PIN_SDA) ? (out & PIN_SDA) != 0 : 1;
		p->ee_bits = PIN_SCL | (cat24m01_update(&p->eeprom, scl, sda) ? PIN_SDA : 0);
	}
	if (p->sd && (ch & (PIN_CS | PIN_SCLK | PIN_DI))) {
'''),
(r'''	p->sd_do = 1;
	p->reset_pending = 0;
''',
 r'''	p->sd_do = 1;
	p->po_ok = 0;
	p->reset_pending = 0;
'''),
(r'''	p->sd_ctx = ctx;
	p->sd_do = 1;
}

''',
 r'''	p->sd_ctx = ctx;
	p->sd_do = 1;
	p->po_ok = 0;
}

'''),
(r'''	p->tx_ctx = ctx;
	p->tx_level = 1;
''',
 r'''	p->tx_ctx = ctx;
	p->po_ok = 0;
	p->tx_level = 1;
'''),
(r'''	p->snd_ctx = ctx;
}
''',
 r'''	p->snd_ctx = ctx;
	p->po_ok = 0;
}
'''),
])

edit('src/wpc/pinheck/prop.h', [
(r'''	uint32_t ee_bits, sd_do;
	int reset_pending;
''',
 r'''	uint32_t ee_bits, sd_do;
	uint32_t po_out, po_dir; /* the pins the devices last saw; po_ok 0: all see the next change */
	int po_ok;
	int reset_pending;
'''),
])
EOF
```

- [ ] **Step 2: The core suites pass**

Run: `for s in p8x32a link display audio; do nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh > build/m9b-$s.log 2>&1; echo "$s: exit $? $(tail -1 build/m9b-$s.log)"; done` (about 15 minutes)
Expected:
```text
p8x32a: exit 0 p8x32a: 226 passed, 0 failed
link: exit 0 link: 0 failed
display: exit 0 display: 0 failed
audio: exit 0 audio: 0 failed
```

- [ ] **Step 3: Rebuild and compare with the reference**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9b.log 2>&1; tail -1 build/sdl3pinmame/m9b.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video
```
Expected (speeds vary with the load):
```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.453x over 30.0 s emulated (66.2 s wall), whole run 20.8 G instructions and 2.2 s CPU per emulated s, load 6.82
bench attract ref: 0.417x over 30.0 s emulated (71.9 s wall), whole run 22.4 G instructions and 2.2 s CPU per emulated s, load 5.65
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 0.421x over 24.0 s emulated (57.0 s wall), whole run 22.3 G instructions and 2.2 s CPU per emulated s, load 4.87
bench video ref: 0.425x over 24.0 s emulated (56.4 s wall), whole run 24.2 G instructions and 2.2 s CPU per emulated s, load 4.38
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 4: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32a.h src/wpc/pinheck/prop.c src/wpc/pinheck/prop.h
git commit -m "p8x32a: counter changes cached per counter, NCO pin levels between change points; devices see their own pins"
```

### Task 3: Propeller local runs and events (stage 3b)

**Files:**
- Create: `tests/pinheck/p8x32a/chip/wake_order.spin`
- Modify: `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32a.h`, `tests/pinheck/p8x32a/alu_test.c`

**Interfaces:**
- Consumes: Task 2's core; Plan 9's `run_local`, `local`, `alu_run`, `exec`, `complete`, the scheduler in `p8x32a_run_until`, `loop_notify`, `loop_hub_write`, `sys`.
- Produces: `alu_run(unsigned i, uint32_t s, uint32_t d, unsigned pc, int ci, int zi, int bus_c, int wc, int *wr, int *co, int *zo)` (`wc = 0`: the carry need not be computed; now also `RCR`, `RCL`, `SAR`, `REV`, `MOVI`, `CMPS`, `TJNZ`, `TJZ`); `p8x32a_dec` and `p8x32a.dec[8][512]`, `static void dec_fill(p8x32a_dec *e, uint32_t i)`, the `K_*` kinds and `F_*` flags; `static int issue_local(const p8x32a_cog *c)`; `p8x32a.sched_gen`. `run_local` keeps Z, C and cancel in `fl` (bits 0, 1, 2) and a stop limit `tl`, which Task 6's block call uses.

- [ ] **Step 1: A wake-order test, and the ALU test calls `alu_run` with its carry request**

A busy cog sends short pulses (4, 8 and 12 cycles, at shifting phases) on a pin that a sleeping cog polls; the sleeping cog must wake at the pulse, before the busy cog's next event, exactly when the RTL's cog sees it. The test passes on the current core; Task 7's mutation of `sched_gen` shows it bites the new scheduler.

`tests/pinheck/p8x32a/chip/wake_order.spin`:

```text
' EXPECT-SLEEPS: 8
' short pulses from a busy cog must wake a polling cog before the busy cog's next event
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
        mov     n, #8
:d      test    p5, ina wc
  if_nc jmp     #:d
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:e      test    p5, ina wc
  if_c  jmp     #:e
        djnz    n, #:d
        cogid   t
        cogstop t
p5      long    %100000
t       long    0
x       long    0
y       long    0
n       long    0
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, pin
        mov     w, cnt
        add     w, d20000
        mov     k, #4
:p      waitcnt w, d3001
        or      outa, pin
        andn    outa, pin
        waitcnt w, d3002
        or      outa, pin
        nop
        andn    outa, pin
        waitcnt w, d3003
        or      outa, pin
        nop
        nop
        andn    outa, pin
        djnz    k, #:p
        waitcnt w, #0
        cogid   k
        cogstop k
pin     long    %100000
d3001   long    3001
d3002   long    3002
d3003   long    3003
d20000  long    20000
w       long    0
k       long    0
        long    $C0DEE0D0
```

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/p8x32a/alu_test.c', [
(r'''		r2 = alu_run(i, s, d, pc, ci, zi, bc, &w2, &c2, &z2);
''',
 r'''		r2 = alu_run(i, s, d, pc, ci, zi, bc, 1, &w2, &c2, &z2);
'''),
])
EOF
```

- [ ] **Step 2: Confirm it fails before the new `alu_run`**

Run: `mkdir -p build && cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -Isrc/cpu/p8x32a -o build/alu_test tests/pinheck/p8x32a/alu_test.c 2>&1 | grep -m1 error`
Expected:
```text
tests/pinheck/p8x32a/alu_test.c:136:55: error: passing argument 8 of ‘alu_run’ makes pointer from integer without a cast [-Wint-conversion]
```

- [ ] **Step 3: Local runs and events**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.c', [
(r'''/* the common ALU ops of a running cog (not hub ops); the others go to alu() */
static uint32_t alu_run(unsigned i, uint32_t s, uint32_t d, unsigned pc, int ci, int zi, int bus_c, int *wr, int *co, int *zo)
{
	uint32_t r;
	unsigned sh = s & 31;
	*wr = 1;
''',
 r'''/* the common ALU ops of a running cog (not hub ops); the others go to alu(). wc = 0: *co need not be set */
static uint32_t alu_run(unsigned i, uint32_t s, uint32_t d, unsigned pc, int ci, int zi, int bus_c, int wc, int *wr, int *co, int *zo)
{
	uint32_t r;
	unsigned sh = s & 31;
	*wr = 1;
	*co = 0;
'''),
(r'''	case 0x14: r = (d & 0xFFFFFE00u) | (s & 511); *co = d < s; break;
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
''',
 r'''	case 0x0C: r = sh ? d >> sh | (ci ? 0xFFFFFFFFu << (32 - sh) : 0) : d; *co = (int)(d & 1); break;
	case 0x0D: r = sh ? d << sh | (ci ? 0xFFFFFFFFu >> (32 - sh) : 0) : d; *co = (int)(d >> 31); break;
	case 0x0E: r = sh ? d >> sh | ((d >> 31) ? 0xFFFFFFFFu << (32 - sh) : 0) : d; *co = (int)(d & 1); break;
	case 0x0F: r = bitrev(d) >> sh; *co = (int)(d & 1); break;
	case 0x14: r = (d & 0xFFFFFE00u) | (s & 511); *co = d < s; break;
	case 0x15: r = (d & 0xFFFC01FFu) | ((s & 511) << 9); *co = d < s; break;
	case 0x16: r = ((s & 511) << 23) | (d & 0x007FFFFFu); *co = d < s; break;
	case 0x17: r = (d & 0xFFFFFE00u) | (pc & 511); *co = d < s; break;
	case 0x18: r = d & s; if (wc) *co = parity(r); break;
	case 0x19: r = d & ~s; if (wc) *co = parity(r); break;
	case 0x1A: r = d | s; if (wc) *co = parity(r); break;
	case 0x1B: r = d ^ s; if (wc) *co = parity(r); break;
	case 0x1C: r = ci ? d | s : d & ~s; if (wc) *co = parity(r); break;
	case 0x1D: r = ci ? d & ~s : d | s; if (wc) *co = parity(r); break;
	case 0x1E: r = zi ? d | s : d & ~s; if (wc) *co = parity(r); break;
	case 0x1F: r = zi ? d & ~s : d | s; if (wc) *co = parity(r); break;
	case 0x20: r = d + s; *co = r < d; break;
	case 0x21: r = d - s; *co = d < s; break;
	case 0x28: r = s; *co = (int)(s >> 31); break;
	case 0x30: r = d - s; *co = (int32_t)d < (int32_t)s; break;
	case 0x39: r = d - 1; *co = !d; break;
	case 0x3A: case 0x3B: r = d; break;
'''),
(r'''		if ((p->sleepers >> n & 1) && (pins & p->loop[n].wake) && t < p->cog[n].ev_t) p->cog[n].ev_t = t;
''',
 r'''		if ((p->sleepers >> n & 1) && (pins & p->loop[n].wake) && t < p->cog[n].ev_t) { p->cog[n].ev_t = t; p->sched_gen++; }
'''),
(r'''			if (a < (unsigned)l->hub_a[k] + l->hub_n[k] && l->hub_a[k] < a + sz && h < p->cog[n].ev_t) p->cog[n].ev_t = h;
''',
 r'''			if (a < (unsigned)l->hub_a[k] + l->hub_n[k] && l->hub_a[k] < a + sz && h < p->cog[n].ev_t) { p->cog[n].ev_t = h; p->sched_gen++; }
'''),
(r'''	r = alu(op, c->s, c->d, c->p, c->run, c->c, c->z, q, bus_c, &wr, &co, &zo);
''',
 r'''	if (op <= 3) {
		/* alu() for group 0: the hub result */
		r = (c->run || (c->p >> 4) != 31) ? q : 0;
		wr = 1;
		co = bus_c;
		zo = !r;
	} else
		r = alu(op, c->s, c->d, c->p, c->run, c->c, c->z, q, bus_c, &wr, &co, &zo);
'''),
(r'''
	while (newx < 7 && (enc >> newx & 1)) newx++;
''',
 r'''
	p->sched_gen++;
	while (newx < 7 && (enc >> newx & 1)) newx++;
'''),
(r'''/* exec + complete for local instructions of cog n, kept in locals, until one is not local or t is passed */
static void run_local(p8x32a *p, int n, uint64_t t)
{
	p8x32a_cog *c = &p->cog[n];
	p8x32a_loop *l = &p->loop[n];
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
		else {
			s = sread(p, n, a, t2);
			if (a == 0x1F1 || a == 0x1FC || a == 0x1FD) l->dirty = 1;
		}
		cond = ((COND(i) >> ((cf << 1) | zf)) & 1) && !cancel;
		d = c->ram[DST(i)];
		jump = op == 0x17 || (op >= 0x39 && op <= 0x3B);
		px = (cond && jump) ? (s & 511) : pc;
		nix = c->ram[px];
		if (t2 + 1 >= c->disable_at) { idled = 1; break; }
		r = alu_run(op, s, d, pc, cf, zf, p->sys_c, &wr, &co, &zo);
		if (cond) {
			if (FWR(i) && wr && c->ram[DST(i)] != r) { c->ram[DST(i)] = r; l->dirty = 1; }
			if (FWC(i)) cf = co;
			if (FWZ(i)) zf = zo;
			if (op == 0x39) jc = !(d >> 1) && (d & 1);
			else if (op == 0x3A) jc = !(d >> 1) && !(d & 1);
			else if (op == 0x3B) jc = !(!(d >> 1) && !(d & 1));
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
''',
 r'''/* a hub op or wait whose issue reads nothing shared: exec() for it may run ahead too */
static int issue_local(const p8x32a_cog *c)
{
	uint32_t i = c->ix;
	unsigned op = OP(i);
	if (!c->run || (op > 3 && (op < 0x3C || op > 0x3E))) return 0;
	return FIM(i) || SRC(i) < 0x1F0;
}

enum { K_NL, K_GEN, K_JMP, K_DJNZ, K_TJ, K_AND, K_ANDN, K_OR, K_XOR, K_ADD, K_SUB, K_MOV, K_SHL, K_SHR, K_MOVS, K_MOVD, K_MOVI };
enum { F_IMM = 1, F_SPEC = 2, F_WR = 4, F_WC = 8, F_WZ = 16 };

/* decode for run_local: K_NL for an instruction it does not run */
static void dec_fill(p8x32a_dec *e, uint32_t i)
{
	unsigned op = OP(i);
	static const uint8_t kinds[64] = {
		K_NL, K_NL, K_NL, K_NL, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_SHR, K_SHL, K_GEN, K_GEN, K_GEN, K_GEN,
		K_GEN, K_GEN, K_GEN, K_GEN, K_MOVS, K_MOVD, K_MOVI, K_JMP, K_AND, K_ANDN, K_OR, K_XOR, K_GEN, K_GEN, K_GEN, K_GEN,
		K_ADD, K_SUB, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_MOV, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN,
		K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_GEN, K_DJNZ, K_TJ, K_TJ, K_NL, K_NL, K_NL, K_NL
	};
	e->word = i;
	e->src = (uint16_t)SRC(i);
	e->dst = (uint16_t)DST(i);
	e->cond = (uint8_t)COND(i);
	e->kind = kinds[op];
	e->fl = (uint8_t)((FIM(i) ? F_IMM : (SRC(i) >= 0x1F0 ? F_SPEC : 0)) | (FWR(i) ? F_WR : 0) | (FWC(i) ? F_WC : 0) | (FWZ(i) ? F_WZ : 0));
	if ((FWR(i) && DST(i) >= 0x1F0) || (!FIM(i) && SRC(i) == 0x1F2)) e->kind = K_NL;
}

/* exec + complete for local instructions of cog n, kept in locals, until one is not local or t is passed.
   fl holds Z (bit 0), C (bit 1) and cancel (bit 2); an instruction runs if bit fl of its condition mask is set. */
static void run_local(p8x32a *p, int n, uint64_t t)
{
	p8x32a_cog *c = &p->cog[n];
	p8x32a_loop *l = &p->loop[n];
	p8x32a_dec *dec = p->dec[n], *e;
	uint32_t *ram = c->ram;
	uint64_t t2 = c->ev_t, dis = c->disable_at, tl = dis < 2 ? 0 : dis - 2 < t ? dis - 2 : t;
	uint32_t ix = c->ix;
	unsigned pc = c->p, fl = (unsigned)(c->c << 1 | c->z) | (unsigned)c->cancel << 2, nins = 0;
	int idled = 0;

	if (t2 > tl || dis < 2) goto limit;
	for (;;) {
		uint32_t s, d, r, nix;
		unsigned px = pc, jc = 0;
		e = &dec[(pc - 1) & 511];
		if (e->word != ix) dec_fill(e, ix);
		if (e->kind == K_NL) break;
		if (e->fl & F_IMM) s = e->src;
		else if (!(e->fl & F_SPEC)) s = ram[e->src];
		else {
			s = sread(p, n, e->src, t2);
			if (e->src == 0x1F1 || e->src == 0x1FC || e->src == 0x1FD) l->dirty = 1;
		}
		if ((e->cond >> fl) & 1) {
			unsigned co = 0, zo;
			d = ram[e->dst];
			switch (e->kind) {
			case K_JMP: px = s & 511; r = (d & 0xFFFFFE00u) | (pc & 511); co = d < s; break;
			case K_DJNZ: px = s & 511; r = d - 1; co = !d; jc = d == 1; break;
			case K_AND: r = d & s; if (e->fl & F_WC) co = (unsigned)parity(r); break;
			case K_ANDN: r = d & ~s; if (e->fl & F_WC) co = (unsigned)parity(r); break;
			case K_OR: r = d | s; if (e->fl & F_WC) co = (unsigned)parity(r); break;
			case K_XOR: r = d ^ s; if (e->fl & F_WC) co = (unsigned)parity(r); break;
			case K_ADD: r = d + s; co = r < d; break;
			case K_SUB: r = d - s; co = d < s; break;
			case K_MOV: r = s; co = s >> 31; break;
			case K_SHL: r = d << (s & 31); co = d >> 31; break;
			case K_SHR: r = d >> (s & 31); co = d & 1; break;
			case K_MOVS: r = (d & 0xFFFFFE00u) | (s & 511); co = d < s; break;
			case K_MOVD: r = (d & 0xFFFC01FFu) | ((s & 511) << 9); co = d < s; break;
			case K_MOVI: r = ((s & 511) << 23) | (d & 0x007FFFFFu); co = d < s; break;
			default: {
				int wr, ci, zi;
				unsigned op = OP(ix);
				if (e->kind == K_TJ) px = s & 511;
				nix = ram[px];
				r = alu_run(op, s, d, pc, (int)(fl >> 1 & 1), (int)(fl & 1), p->sys_c, (e->fl & F_WC) != 0, &wr, &ci, &zi);
				co = (unsigned)ci;
				zo = (unsigned)zi;
				if (op == 0x3A) jc = !d;
				else if (op == 0x3B) jc = d != 0;
				if ((e->fl & F_WR) && wr && ram[e->dst] != r) { ram[e->dst] = r; l->dirty = 1; }
				goto flags;
			}
			}
			nix = ram[px];
			zo = !r;
			if ((e->fl & F_WR) && ram[e->dst] != r) { ram[e->dst] = r; l->dirty = 1; }
		flags:
			if (e->fl & F_WC) fl = (fl & ~2u) | co << 1;
			if (e->fl & F_WZ) fl = (fl & ~1u) | zo;
			nins++;
			if (!jc && px < pc && e->kind >= K_JMP && e->kind <= K_TJ) {
				l->nins = (uint16_t)(l->nins + nins);
				nins = 0;
				loop_edge(p, n, px, t2 + 1);
				if (l->state == LOOP_RECORD) {
					c->i = ix; c->s = s; c->d = d; c->px = (uint16_t)px; c->nix = nix; c->cond = 1;
					pc = (px + 1) & 511;
					fl = (fl & 3) | (px == 511) << 2;
					ix = nix;
					t2 += 4;
					break;
				}
			}
		} else {
			nix = ram[px];
			nins++;
		}
		if (!jc) pc = (px + 1) & 511;
		fl = (fl & 3) | (jc || px == 511) << 2;
		ix = nix;
		t2 += 4;
		if (t2 > tl) {
			if (t2 - 2 >= dis) { idled = 1; break; }
			goto limit;
		}
	}
	goto out;
limit:
	/* t is passed, or the next instruction starts too late to run before the cog stops */
	if (t2 <= t) {
		e = &dec[(pc - 1) & 511];
		if (e->word != ix) dec_fill(e, ix);
		if (e->kind != K_NL) {
			if ((e->fl & F_SPEC)) {
				(void)sread(p, n, e->src, t2);
				if (e->src == 0x1F1 || e->src == 0x1FC || e->src == 0x1FD) l->dirty = 1;
			}
			idled = 1;
		}
	}
out:
	l->nins = (uint16_t)(l->nins + nins);
	c->p = (uint16_t)pc;
	c->ix = ix;
	c->c = (uint8_t)(fl >> 1 & 1);
	c->z = (uint8_t)(fl & 1);
	c->cancel = (uint8_t)(fl >> 2 & 1);
'''),
(r'''		int n, best = -1;
		p8x32a_cog *b;
		for (n = 0; n < 8; n++) {
			p8x32a_cog *c = &p->cog[n];
			if (c->ev == EV_NONE) continue;
			if (best < 0 || c->ev_t < p->cog[best].ev_t ||
			    (c->ev_t == p->cog[best].ev_t && c->ev == EV_HUB && p->cog[best].ev != EV_HUB)) best = n;
		}
		if (best < 0 || p->cog[best].ev_t > t || p->stop) break;
		b = &p->cog[best];
''',
 r'''		int n, best;
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
		}
		if (bk == P8X32A_NEVER) break;
		best = (int)(bk & 7);
		if (p->cog[best].ev_t > t || p->stop) break;
		b = &p->cog[best];
		gen = p->sched_gen;
	again:
'''),
(r'''		while (b->ev == EV_EXEC && b->ev_t <= t && local(b)) {
			if (p->loop[best].state == LOOP_RECORD) exec(p, best);
			else run_local(p, best, t);
			if (p->loop[best].state == LOOP_RECORD) loop_post(p, best);
''',
 r'''		while (b->ev == EV_EXEC && b->ev_t <= t && (local(b) || issue_local(b))) {
			if (p->loop[best].state == LOOP_RECORD || !local(b)) exec(p, best);
			else run_local(p, best, t);
			if (p->loop[best].state == LOOP_RECORD) loop_post(p, best);
		}
		/* while no other cog's next event has changed, this cog goes again if it is still the earliest */
		if (gen == p->sched_gen && b->ev != EV_NONE && b->ev_t <= t && !p->stop) {
			uint64_t e = b->ev_t < ((uint64_t)1 << 59) ? b->ev_t : (uint64_t)1 << 59;
			if ((e << 4 | (uint64_t)(b->ev != EV_HUB) << 3 | (uint64_t)best) < bk2) goto again;
'''),
(r'''	memset(p, 0, sizeof(*p));
	p->bus = *bus;
''',
 r'''	int n;
	memset(p, 0, sizeof(*p));
	p->bus = *bus;
	for (n = 0; n < 8 * 512; n++) dec_fill(&p->dec[0][0] + n, 0);
'''),
])

edit('src/cpu/p8x32a/p8x32a.h', [
(r'''
typedef struct p8x32a {
''',
 r'''
/* a decoded instruction word, valid while word matches the instruction being run */
typedef struct p8x32a_dec {
	uint32_t word;
	uint16_t src, dst;
	uint8_t kind, fl, cond, pad;
} p8x32a_dec;

typedef struct p8x32a {
'''),
(r'''	uint64_t sleeps; /* idle loops entered */
	p8x32a_loop loop[8];
''',
 r'''	unsigned sched_gen; /* counts changes one cog makes to another cog's next event */
	uint64_t sleeps; /* idle loops entered */
	p8x32a_loop loop[8];
	p8x32a_dec dec[8][512];
'''),
])
EOF
```

- [ ] **Step 4: The core suites pass**

Run: `for s in p8x32a link display audio; do nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh > build/m9b-$s.log 2>&1; echo "$s: exit $? $(tail -1 build/m9b-$s.log)"; done; grep -a 'alu:' build/m9b-p8x32a.log` (about 15 minutes)
Expected:
```text
p8x32a: exit 0 p8x32a: 227 passed, 0 failed
link: exit 0 link: 0 failed
display: exit 0 display: 0 failed
audio: exit 0 audio: 0 failed
alu: 4000000 cases, 0 mismatches
```

- [ ] **Step 5: Rebuild and compare with the reference**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9b.log 2>&1; tail -1 build/sdl3pinmame/m9b.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video
```
Expected (speeds vary with the load):
```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.508x over 30.0 s emulated (59.0 s wall), whole run 19.4 G instructions and 1.8 s CPU per emulated s, load 4.30
bench attract ref: 0.417x over 30.0 s emulated (71.9 s wall), whole run 22.4 G instructions and 2.2 s CPU per emulated s, load 5.65
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 0.452x over 24.0 s emulated (53.1 s wall), whole run 20.8 G instructions and 2.0 s CPU per emulated s, load 4.49
bench video ref: 0.425x over 24.0 s emulated (56.4 s wall), whole run 24.2 G instructions and 2.2 s CPU per emulated s, load 4.38
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32a.h tests/pinheck/p8x32a/alu_test.c tests/pinheck/p8x32a/chip/wake_order.spin
git commit -m "p8x32a: decoded instructions checked by word, lean local loop, hub ops issued ahead, earliest cog continues"
```

### Task 4: MIPS32 fast loop (stage 3c)

**Files:**
- Modify: `src/cpu/mips32/mips32.c`, `src/cpu/mips32/mips32.h`

**Interfaces:**
- Consumes: Plan 1's `mips32_run`, `step`, `execute`, the `SET`/`REGS` macros; Plan 9's direct regions (`mips32_region`, `direct_rd`/`direct_wr`), fetch cache (`fva`, `fsize`, `fptr`), lazy Count (`ti_at`) and `irq_chk`.
- Produces: `static int fast_run(mips32_state *s, uint64_t lim)` (runs instructions while `cycles < lim` and returns how many; stops before the first it does not run, with the state as before it); `static int direct_slot(const mips32_state *s, uint32_t pa, uint32_t size, int wr)`; `mips32_state.dslot` (the direct region of the last fast load; no pointer).

`mips32_run` calls `fast_run` with `lim` = the earlier of the run's end and the Timer's `ti_at` whenever the CPU is not waiting; when it ran nothing, the instruction is `step`'s, exactly as before (Ruling 3). `fast_run` runs: shifts, moves to and from HI/LO, multiplies and divides (35 cycles, as `step`), the ALU with registers and immediates (`ADD`, `ADDI`, `SUB` only when they do not overflow), `LUI`, `MUL`, `MADD`, `MSUB`, `CLZ`, `CLO`, `EXT`, `INS`, `WSBH`, `SEB`, `SEH`, jumps and branches with their delay slots (likely branches skip the slot when not taken), `SYNC`, `PREF`, `CACHE` in kernel mode, and aligned `LB`, `LH`, `LW`, `LBU`, `LHU`, `SB`, `SH`, `SW` inside a direct region, at a kuseg address or, in kernel mode, a kseg0/kseg1 one. Direct regions must not overlap (`pic32mx` and `mips32run` register disjoint ones), so the region of the last load is the one `direct_rd` would find.

- [ ] **Step 1: Measure the interpreter before**

Run: `tests/pinheck/mips32/bench.sh; B=tests/pinheck/mips32/build; perf stat -e instructions:u $B/mips32run -n 1000000000 $B/bench.elf 2>&1 >/dev/null | grep instructions`
Expected (the M instr/s figure varies with the load; the instruction count does not):
```text
bench: match, 110.2 M instr/s
    17,400,364,460      instructions:u
```

- [ ] **Step 2: The fast loop**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/mips32/mips32.c', [
(r'''
static int irq_pending(const mips32_state *s)
''',
 r'''
/* the direct region holding [pa, pa + size), writable if wr; -1 if none */
static int direct_slot(const mips32_state *s, uint32_t pa, uint32_t size, int wr)
{
	int k;
	for (k = 0; k < MIPS32_REGIONS; k++) {
		const mips32_region *m = &s->region[k];
		if ((!wr || m->wr) && pa - m->base < m->size && m->size - (pa - m->base) >= size) return k;
	}
	return -1;
}

/* Instructions that raise no exception, touch only registers and direct memory and change no CP0 state run here,
   held in locals; the first other instruction, or reaching lim cycles, returns with the state as before it.
   Returns the number of instructions run. */
static int fast_run(mips32_state *s, uint64_t lim)
{
	uint32_t *r = REGS(s);
	uint32_t pc = s->pc, npc = s->npc, fva = s->fva, fsize = s->fsize;
	const uint8_t *fptr = s->fptr;
	uint64_t cyc = s->cycles;
	int delay = s->delay, n = 0, kern = !USER(s), erl = (s->status & ST_ERL) != 0, ds = s->dslot;

	if (!fsize) return 0;
	while (cyc < lim) {
		uint32_t off = pc - fva, op, rs, rt, v, ea, pa, tpc = npc + 4, nd = 0;
		unsigned d = 0;
		uint64_t add = 1;
		const mips32_region *m;
		if (off >= fsize || (off & 3)) break;
		op = le32(fptr + off);
		rs = r[RS(op)];
		rt = r[RT(op)];
		switch (op >> 26) {
		case 0x00:
			switch (FUNCT(op)) {
			case 0x00: d = RD(op); v = rt << SA(op); break;
			case 0x02: d = RD(op); v = (op & (1u << 21)) ? ror32(rt, SA(op)) : rt >> SA(op); break;
			case 0x03: d = RD(op); v = sra32(rt, SA(op)); break;
			case 0x04: d = RD(op); v = rt << (rs & 31); break;
			case 0x06: d = RD(op); v = (op & (1u << 6)) ? ror32(rt, rs) : rt >> (rs & 31); break;
			case 0x07: d = RD(op); v = sra32(rt, rs); break;
			case 0x08: tpc = rs; nd = 1; v = 0; break;
			case 0x09: d = RD(op); v = pc + 8; tpc = rs; nd = 1; break;
			case 0x0A: d = rt ? 0 : RD(op); v = rs; break;
			case 0x0B: d = rt ? RD(op) : 0; v = rs; break;
			case 0x0F: v = 0; break;
			case 0x10: d = RD(op); v = s->hi; break;
			case 0x11: s->hi = rs; v = 0; break;
			case 0x12: d = RD(op); v = s->lo; break;
			case 0x13: s->lo = rs; v = 0; break;
			case 0x18: { int64_t p = (int64_t)(int32_t)rs * (int32_t)rt; s->lo = (uint32_t)p; s->hi = (uint32_t)((uint64_t)p >> 32); v = 0; break; }
			case 0x19: { uint64_t p = (uint64_t)rs * rt; s->lo = (uint32_t)p; s->hi = (uint32_t)(p >> 32); v = 0; break; }
			case 0x1A:
				if (rt) {
					if (rs == 0x80000000u && rt == 0xFFFFFFFFu) { s->lo = 0x80000000u; s->hi = 0; }
					else { s->lo = (uint32_t)((int32_t)rs / (int32_t)rt); s->hi = (uint32_t)((int32_t)rs % (int32_t)rt); }
				}
				add = 35; v = 0;
				break;
			case 0x1B: if (rt) { s->lo = rs / rt; s->hi = rs % rt; } add = 35; v = 0; break;
			case 0x20: v = rs + rt; if (~(rs ^ rt) & (rs ^ v) & 0x80000000u) goto out; d = RD(op); break;
			case 0x21: d = RD(op); v = rs + rt; break;
			case 0x22: v = rs - rt; if ((rs ^ rt) & (rs ^ v) & 0x80000000u) goto out; d = RD(op); break;
			case 0x23: d = RD(op); v = rs - rt; break;
			case 0x24: d = RD(op); v = rs & rt; break;
			case 0x25: d = RD(op); v = rs | rt; break;
			case 0x26: d = RD(op); v = rs ^ rt; break;
			case 0x27: d = RD(op); v = ~(rs | rt); break;
			case 0x2A: d = RD(op); v = (int32_t)rs < (int32_t)rt; break;
			case 0x2B: d = RD(op); v = rs < rt; break;
			default: goto out;
			}
			break;
		case 0x01:
			switch (RT(op)) {
			case 0x00: if ((int32_t)rs < 0) tpc = pc + 4 + (SIMM(op) << 2); nd = 1; v = 0; break;
			case 0x01: if ((int32_t)rs >= 0) tpc = pc + 4 + (SIMM(op) << 2); nd = 1; v = 0; break;
			case 0x10: if ((int32_t)rs < 0) tpc = pc + 4 + (SIMM(op) << 2); nd = 1; d = 31; v = pc + 8; break;
			case 0x11: if ((int32_t)rs >= 0) tpc = pc + 4 + (SIMM(op) << 2); nd = 1; d = 31; v = pc + 8; break;
			default: goto out;
			}
			break;
		case 0x02: tpc = ((pc + 4) & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2); nd = 1; v = 0; break;
		case 0x03: tpc = ((pc + 4) & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2); nd = 1; d = 31; v = pc + 8; break;
		case 0x04: if (rs == rt) tpc = pc + 4 + (SIMM(op) << 2); nd = 1; v = 0; break;
		case 0x05: if (rs != rt) tpc = pc + 4 + (SIMM(op) << 2); nd = 1; v = 0; break;
		case 0x06: if ((int32_t)rs <= 0) tpc = pc + 4 + (SIMM(op) << 2); nd = 1; v = 0; break;
		case 0x07: if ((int32_t)rs > 0) tpc = pc + 4 + (SIMM(op) << 2); nd = 1; v = 0; break;
		case 0x08: v = rs + SIMM(op); if (~(rs ^ SIMM(op)) & (rs ^ v) & 0x80000000u) goto out; d = RT(op); break;
		case 0x09: d = RT(op); v = rs + SIMM(op); break;
		case 0x0A: d = RT(op); v = (int32_t)rs < (int32_t)SIMM(op); break;
		case 0x0B: d = RT(op); v = rs < SIMM(op); break;
		case 0x0C: d = RT(op); v = rs & UIMM(op); break;
		case 0x0D: d = RT(op); v = rs | UIMM(op); break;
		case 0x0E: d = RT(op); v = rs ^ UIMM(op); break;
		case 0x0F: d = RT(op); v = UIMM(op) << 16; break;
		case 0x14: case 0x15: case 0x16: case 0x17: {
			int c = (op >> 26) == 0x14 ? rs == rt : (op >> 26) == 0x15 ? rs != rt : (op >> 26) == 0x16 ? (int32_t)rs <= 0 : (int32_t)rs > 0;
			v = 0;
			if (c) { tpc = pc + 4 + (SIMM(op) << 2); nd = 1; }
			else { npc += 4; tpc = npc + 4; }
			break;
		}
		case 0x1C: {
			uint64_t acc = ((uint64_t)s->hi << 32) | s->lo;
			switch (FUNCT(op)) {
			case 0x00: acc += (uint64_t)((int64_t)(int32_t)rs * (int32_t)rt); break;
			case 0x01: acc += (uint64_t)rs * rt; break;
			case 0x02: d = RD(op); v = (uint32_t)((int64_t)(int32_t)rs * (int32_t)rt); add = 2; goto set;
			case 0x04: acc -= (uint64_t)((int64_t)(int32_t)rs * (int32_t)rt); break;
			case 0x05: acc -= (uint64_t)rs * rt; break;
			case 0x20: d = RD(op); v = clz32(rs); goto set;
			case 0x21: d = RD(op); v = clz32(~rs); goto set;
			default: goto out;
			}
			s->lo = (uint32_t)acc;
			s->hi = (uint32_t)(acc >> 32);
			v = 0;
			break;
		}
		case 0x1F:
			switch (FUNCT(op)) {
			case 0x00: d = RT(op); v = (rs >> SA(op)) & mask32(RD(op) + 1); break;
			case 0x04: { uint32_t mk = mask32(RD(op) - SA(op) + 1) << SA(op); d = RT(op); v = (rt & ~mk) | ((rs << SA(op)) & mk); break; }
			case 0x20:
				switch (SA(op)) {
				case 0x02: d = RD(op); v = ((rt & 0x00FF00FFu) << 8) | ((rt >> 8) & 0x00FF00FFu); break;
				case 0x10: d = RD(op); v = (uint32_t)(int32_t)(int8_t)rt; break;
				case 0x18: d = RD(op); v = (uint32_t)(int32_t)(int16_t)rt; break;
				default: goto out;
				}
				break;
			default: goto out;
			}
			break;
		case 0x20: case 0x21: case 0x23: case 0x24: case 0x25: {
			uint32_t sz = (op >> 26) == 0x23 ? 4 : ((op >> 26) & 1) ? 2 : 1;
			const uint8_t *h;
			ea = rs + SIMM(op);
			if (ea & (sz - 1)) goto out;
			if (ea < 0x80000000u) pa = erl ? ea : ea + 0x40000000u;
			else if (kern && ea < 0xC0000000u) pa = ea & 0x1FFFFFFFu;
			else goto out;
			m = &s->region[ds];
			if (!(pa - m->base < m->size && m->size - (pa - m->base) >= sz)) {
				int k = direct_slot(s, pa, sz, 0);
				if (k < 0) goto out;
				ds = k;
				m = &s->region[k];
			}
			h = m->rd + (pa - m->base);
			switch (op >> 26) {
			case 0x20: v = (uint32_t)(int32_t)(int8_t)h[0]; break;
			case 0x21: v = (uint32_t)(int32_t)(int16_t)(h[0] | h[1] << 8); break;
			case 0x23: v = le32(h); break;
			case 0x24: v = h[0]; break;
			default: v = (uint32_t)(h[0] | h[1] << 8); break;
			}
			d = RT(op);
			break;
		}
		case 0x28: case 0x29: case 0x2B: {
			uint32_t sz = (op >> 26) == 0x2B ? 4 : (op >> 26) == 0x29 ? 2 : 1;
			uint8_t *h;
			int k;
			ea = rs + SIMM(op);
			if (ea & (sz - 1)) goto out;
			if (ea < 0x80000000u) pa = erl ? ea : ea + 0x40000000u;
			else if (kern && ea < 0xC0000000u) pa = ea & 0x1FFFFFFFu;
			else goto out;
			k = direct_slot(s, pa, sz, 1);
			if (k < 0) goto out;
			m = &s->region[k];
			h = m->wr + (pa - m->base);
			h[0] = (uint8_t)rt;
			if (sz > 1) h[1] = (uint8_t)(rt >> 8);
			if (sz > 2) { h[2] = (uint8_t)(rt >> 16); h[3] = (uint8_t)(rt >> 24); }
			v = 0;
			break;
		}
		case 0x2F: if (!kern) goto out; v = 0; break;
		case 0x33: v = 0; break;
		default: goto out;
		}
	set:
		if (d) r[d] = v;
		pc = npc;
		npc = tpc;
		delay = (int)nd;
		cyc += add;
		n++;
		continue;
	out:
		break;
	}
	s->pc = pc;
	s->npc = npc;
	s->delay = delay;
	s->cycles = cyc;
	s->dslot = ds;
	return n;
}

static int irq_pending(const mips32_state *s)
'''),
(r'''		if (s->waiting) {
''',
 r'''		if (!s->waiting && fast_run(s, end < s->ti_at ? end : s->ti_at)) {
		} else if (s->waiting) {
'''),
])

edit('src/cpu/mips32/mips32.h', [
(r'''	const uint8_t *fptr;
};
''',
 r'''	const uint8_t *fptr;
	int dslot;                    /* direct region of the last fast load */
};
'''),
])
EOF
```

- [ ] **Step 3: The core suite passes**

Run: `nice -n 15 ionice -c 3 tests/pinheck/mips32/check.sh | tail -3; tests/pinheck/mips32/bench.sh; B=tests/pinheck/mips32/build; perf stat -e instructions:u $B/mips32run -n 1000000000 $B/bench.elf 2>&1 >/dev/null | grep instructions; $B/mips32run -n 1000000000 $B/bench.elf | od -An -tx4; MIPS32RUN_BUS=1 $B/mips32run -n 1000000000 $B/bench.elf | od -An -tx4`
Expected (the last two lines: the direct path and the bus callbacks compute the same result):
```text
dasm: 45/45
unit: ok
mips32: 208 passed, 0 failed
bench: match, 210.6 M instr/s
    10,600,540,285      instructions:u
 4c74ae80
 4c74ae80
```

- [ ] **Step 4: Rebuild and compare with the reference**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9b.log 2>&1; tail -1 build/sdl3pinmame/m9b.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video
```
Expected (speeds vary with the load):
```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.614x over 30.0 s emulated (48.8 s wall), whole run 16.7 G instructions and 1.5 s CPU per emulated s, load 5.18
bench attract ref: 0.417x over 30.0 s emulated (71.9 s wall), whole run 22.4 G instructions and 2.2 s CPU per emulated s, load 5.65
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 0.565x over 24.0 s emulated (42.5 s wall), whole run 18.2 G instructions and 1.6 s CPU per emulated s, load 4.80
bench video ref: 0.425x over 24.0 s emulated (56.4 s wall), whole run 24.2 G instructions and 2.2 s CPU per emulated s, load 4.38
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 5: Commit**

```bash
git add src/cpu/mips32/mips32.c src/cpu/mips32/mips32.h
git commit -m "mips32: a fast loop for exception-free register and direct-memory instructions"
```

### Task 5: The Propeller on a worker thread (stage 4)

**Files:**
- Modify: `src/wpc/pinheck/prop.c`, `src/wpc/pinheck/prop.h`, `src/wpc/pinheck.c`, `src/unix/unix.mak`, `tests/pinheck/link/prop_test.c`, `tests/pinheck/link/check.sh`

**Interfaces:**
- Consumes: Plan 5's `prop_catch_up`, `prop_pic_pins`, `prop_p24`, `prop_reset`, `prop_time`; Plan 6's display frame callback; Plan 5's bootloader stand-in (`boot.state`, `BOOT_APP`).
- Produces: `typedef uint64_t (*prop_clock_fn)(void *ctx)`; `void prop_set_clock(pinheck_prop *p, prop_clock_fn fn, void *ctx)` (the PIC32 cycle now); `uint64_t prop_stamp(const pinheck_prop *p)` (the PIC32 cycle at which the running call was made); `int prop_start_thread(pinheck_prop *p)` (0, or -1 when no thread could start; the calls then run inline); `void prop_stop_thread(pinheck_prop *p)` (runs the queued calls first); `void prop_sync(pinheck_prop *p)` (returns once every queued call has run); `prop_time` loses its `const`; `pinheck_prop` gains `clock`, `clock_ctx`, `stamp`, `worker`. `PINHECK_NO_THREADS` builds `prop.c` without threads; `PINHECK_THREADS=0` keeps the driver from starting one.

The calls queue in order with the PIC32 cycle at which they were made and run on the worker exactly as they would inline; the driver waits for the queue where it reads Propeller state (Ruling 6).

- [ ] **Step 1: Write the thread test**

`prop_test` gains `threaded`: the same edges, catch-ups and RF13 reads with and without the worker give the same hub RAM, RF13 answers, final time and frame stamps (the pins callback records `prop_stamp` at every pin change). The link suite also builds `prop.c` with `-pthread` and checks that it compiles without threads.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/link/prop_test.c', [
(r'''
static int load(const char *path, uint8_t *dst)
''',
 r'''
static uint64_t clock_now;
static uint64_t stamps[4 * BITS];
static int nstamps;

static uint64_t test_clock(void *ctx) { (void)ctx; return clock_now; }

static void stamp_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx; (void)t; (void)out; (void)dir;
	if (nstamps < 4 * BITS) stamps[nstamps++] = prop_stamp(&p);
}

/* the same calls on the worker thread give the same Propeller, pin changes and stamps (the clock runs ahead of
   the calls' cycles, so a stamp taken from the wrong one differs) */
static void threaded(void)
{
	static uint8_t hub[65536];
	static uint64_t seq[4 * BITS];
	uint64_t pic, now = 0;
	int k, pass, n = 0, p24[BITS];
	for (pass = 0; pass < 2; pass++) {
		boot();
		prop_set_clock(&p, test_clock, NULL);
		prop_set_pins(&p, stamp_pins, NULL);
		nstamps = 0;
		if (pass) CHECK(prop_start_thread(&p) == 0);
		pic = 200000;
		clock_now = pic + 1000;
		prop_catch_up(&p, pic);
		for (k = 0; k < BITS; k++) {
			int bit = (k * 7 + 3) % 5 < 2, v;
			clock_now = pic + 1000;
			prop_pic_pins(&p, pic, CLK | (bit ? DATA : 0));
			clock_now = pic + 1100;
			v = prop_p24(&p, pic + 100);
			if (!pass) p24[k] = v;
			else CHECK(v == p24[k]);
			clock_now = pic + 1200;
			prop_pic_pins(&p, pic + 200, bit ? DATA : 0);
			clock_now = pic + 1300;
			prop_catch_up(&p, pic + 300);
			pic += 400;
		}
		clock_now = pic + 1000;
		prop_catch_up(&p, pic);
		prop_sync(&p);
		CHECK(count() == BITS);
		if (!pass) {
			memcpy(hub, p.chip.hub, sizeof(hub));
			memcpy(seq, stamps, sizeof(seq));
			n = nstamps;
			now = p.chip.now;
		} else {
			CHECK(!memcmp(hub, p.chip.hub, sizeof(hub)));
			CHECK(nstamps == n && !memcmp(seq, stamps, sizeof(seq)));
			CHECK(p.chip.now == now);
			prop_stop_thread(&p);
			CHECK(p.worker == NULL);
		}
	}
	CHECK(n > 0);
}

static int load(const char *path, uint8_t *dst)
'''),
(r'''	clkset_reset_bit();
	printf("prop: %s\n", fails ? "FAIL" : "ok");
''',
 r'''	clkset_reset_bit();
	threaded();
	printf("prop: %s\n", fails ? "FAIL" : "ok");
'''),
])

edit('tests/pinheck/link/check.sh', [
(r'''CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic -I$S/cpu/pic32mx"
''',
 r'''CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic -pthread -I$S/cpu/pic32mx"
'''),
(r'''for f in prop.c rtc.c bootldr.c; do [ -f $S/wpc/pinheck/$f ] || continue; cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/$f || { echo "C89 FAIL $f"; fail=$((fail + 1)); }; done
[ -x "$TOOLS/openspin/build/openspin" ] || { echo "TOOLS MISSING: run ../p8x32a/tools.sh or set TOOLS"; exit 2; }
''',
 r'''for f in prop.c rtc.c bootldr.c; do [ -f $S/wpc/pinheck/$f ] || continue; cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/$f || { echo "C89 FAIL $f"; fail=$((fail + 1)); }; done
cc -std=c89 -pedantic-errors -Wno-long-long -Wall -Werror -DPINHECK_NO_THREADS -fsyntax-only $S/wpc/pinheck/prop.c || { echo "NO_THREADS FAIL prop.c"; fail=$((fail + 1)); }
[ -x "$TOOLS/openspin/build/openspin" ] || { echo "TOOLS MISSING: run ../p8x32a/tools.sh or set TOOLS"; exit 2; }
'''),
])
EOF
```

- [ ] **Step 2: Confirm it fails before the worker**

Run: `nice -n 15 ionice -c 3 tests/pinheck/link/check.sh 2>&1 | grep -m2 'error\|link:'`
Expected:
```text
prop_test.c:158:53: error: implicit declaration of function ‘prop_stamp’; did you mean ‘prop_time’? [-Wimplicit-function-declaration]
prop_test.c:170:17: error: implicit declaration of function ‘prop_set_clock’; did you mean ‘prop_set_log’? [-Wimplicit-function-declaration]
```

- [ ] **Step 3: The worker, and the driver's waits**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck/prop.c', [
(r'''#include <string.h>

''',
 r'''#include <string.h>
#include <stdlib.h>

'''),
(r'''#define PIN_SND  ((1u << 14) | (1u << 15))

''',
 r'''#define PIN_SND  ((1u << 14) | (1u << 15))

static void do_catch_up(pinheck_prop *p, uint64_t pic_cycle);
static void do_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins);

'''),
(r'''	prop_catch_up(p, p->pic_last);
''',
 r'''	prop_sync(p);
	p->stamp = p->clock ? p->clock(p->clock_ctx) : 0;
	do_catch_up(p, p->pic_last);
'''),
(r'''uint64_t prop_time(const pinheck_prop *p, uint64_t pic_cycle)
{
	return to_prop(p, pic_cycle);
}

void prop_catch_up(pinheck_prop *p, uint64_t pic_cycle)
''',
 r'''uint64_t prop_time(pinheck_prop *p, uint64_t pic_cycle)
{
	prop_sync(p);
	return to_prop(p, pic_cycle);
}

void prop_set_clock(pinheck_prop *p, prop_clock_fn fn, void *ctx)
{
	p->clock = fn;
	p->clock_ctx = ctx;
}

uint64_t prop_stamp(const pinheck_prop *p) { return p->stamp; }

static void do_catch_up(pinheck_prop *p, uint64_t pic_cycle)
'''),
(r'''void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins)
{
	pins &= PROP_PIC_PINS;
	if (pic_cycle > p->pic_last) p->pic_last = pic_cycle;
	if (pins == p->last_pins) return;
	if (p->count == PROP_EDGES) prop_catch_up(p, pic_cycle);
''',
 r'''static void do_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins)
{
	if (pic_cycle > p->pic_last) p->pic_last = pic_cycle;
	if (pins == p->last_pins) return;
	if (p->count == PROP_EDGES) do_catch_up(p, pic_cycle);
'''),
(r'''	out = p8x32a_pins(&p->chip, p->chip.now, &dir);
	return (dir & PIN_P24) ? (out & PIN_P24) != 0 : 0;
}
''',
 r'''	prop_sync(p);
	out = p8x32a_pins(&p->chip, p->chip.now, &dir);
	return (dir & PIN_P24) ? (out & PIN_P24) != 0 : 0;
}

/* The calls from the PIC32 side can run on a worker thread: they queue in order and run there exactly as they
   would inline, each with the PIC32 cycle at which it was made. The caller waits for the queue to drain before
   it reads Propeller state (prop_sync). */
#define PROP_Q 4096
#define PROP_SPIN 20000

enum { CMD_PINS, CMD_CATCH_UP, CMD_QUIT };

typedef struct prop_cmd { uint64_t pic, stamp; uint32_t pins; int kind; } prop_cmd;

#ifdef PINHECK_NO_THREADS
int prop_start_thread(pinheck_prop *p) { (void)p; return -1; }
void prop_stop_thread(pinheck_prop *p) { (void)p; }
void prop_sync(pinheck_prop *p) { (void)p; }
static void post(pinheck_prop *p, int kind, uint64_t pic, uint32_t pins) { (void)p; (void)kind; (void)pic; (void)pins; }
#else
#ifdef _WIN32
#include <windows.h>
typedef struct prop_os { HANDLE th, ev; } prop_os;
static unsigned get_acq(volatile unsigned *v) { unsigned r = *v; MemoryBarrier(); return r; }
static void put_rel(volatile unsigned *v, unsigned x) { MemoryBarrier(); *v = x; }
static unsigned xchg(volatile unsigned *v, unsigned x) { return (unsigned)InterlockedExchange((volatile LONG *)v, (LONG)x); }
static void relax(int k) { if (k > PROP_SPIN) SwitchToThread(); else YieldProcessor(); }
#define CPU_RELAX_ANY() YieldProcessor()
#else
#include <pthread.h>
#include <sched.h>
typedef struct prop_os { pthread_t th; pthread_mutex_t m; pthread_cond_t c; int wake; } prop_os;
static unsigned get_acq(volatile unsigned *v) { return __atomic_load_n(v, __ATOMIC_ACQUIRE); }
static void put_rel(volatile unsigned *v, unsigned x) { __atomic_store_n(v, x, __ATOMIC_RELEASE); }
static unsigned xchg(volatile unsigned *v, unsigned x) { return __atomic_exchange_n(v, x, __ATOMIC_SEQ_CST); }
#if defined(__x86_64__) || defined(__i386__)
#define CPU_RELAX() __asm__ __volatile__("pause")
#elif defined(__aarch64__) || defined(__arm__)
#define CPU_RELAX() __asm__ __volatile__("yield")
#else
#define CPU_RELAX() ((void)0)
#endif
static void relax(int k) { if (k > PROP_SPIN) sched_yield(); else CPU_RELAX(); }
#define CPU_RELAX_ANY() CPU_RELAX()
#endif

typedef struct prop_worker {
	prop_os os;
	prop_cmd q[PROP_Q];
	volatile unsigned head, tail; /* head: next command the worker runs; tail: next free slot */
	volatile unsigned sleeping;
} prop_worker;

/* the worker, idle for a while, blocks until the next post */
static void idle_wait(prop_worker *w, unsigned h)
{
	if (xchg(&w->sleeping, 1) == 0 && get_acq(&w->tail) == h) {
#ifdef _WIN32
		WaitForSingleObject(w->os.ev, INFINITE);
#else
		pthread_mutex_lock(&w->os.m);
		while (!w->os.wake) pthread_cond_wait(&w->os.c, &w->os.m);
		w->os.wake = 0;
		pthread_mutex_unlock(&w->os.m);
#endif
	}
	xchg(&w->sleeping, 0);
}

static void wake(prop_worker *w)
{
	if (!xchg(&w->sleeping, 0)) return;
#ifdef _WIN32
	SetEvent(w->os.ev);
#else
	pthread_mutex_lock(&w->os.m);
	w->os.wake = 1;
	pthread_cond_signal(&w->os.c);
	pthread_mutex_unlock(&w->os.m);
#endif
}

static void run_cmd(pinheck_prop *p, const prop_cmd *c)
{
	p->stamp = c->stamp;
	if (c->kind == CMD_CATCH_UP) do_catch_up(p, c->pic);
	else do_pic_pins(p, c->pic, c->pins);
}

#ifdef _WIN32
static DWORD WINAPI worker(LPVOID arg)
#else
static void *worker(void *arg)
#endif
{
	pinheck_prop *p = (pinheck_prop *)arg;
	prop_worker *w = (prop_worker *)p->worker;
	unsigned h = w->head;
	int k = 0;
	for (;;) {
		unsigned t = get_acq(&w->tail);
		if (h == t) {
			if (++k < PROP_SPIN) CPU_RELAX_ANY();
			else { idle_wait(w, h); k = 0; }
			continue;
		}
		k = 0;
		if (w->q[h % PROP_Q].kind == CMD_QUIT) break;
		run_cmd(p, &w->q[h % PROP_Q]);
		put_rel(&w->head, ++h);
	}
	return 0;
}

static void post(pinheck_prop *p, int kind, uint64_t pic, uint32_t pins)
{
	prop_worker *w = (prop_worker *)p->worker;
	unsigned t = w->tail;
	prop_cmd *c;
	int k = 0;
	while (t - get_acq(&w->head) >= PROP_Q) relax(++k);
	c = &w->q[t % PROP_Q];
	c->kind = kind;
	c->pic = pic;
	c->pins = pins;
	c->stamp = p->clock ? p->clock(p->clock_ctx) : pic;
	put_rel(&w->tail, t + 1);
	wake(w);
}

void prop_sync(pinheck_prop *p)
{
	prop_worker *w = (prop_worker *)p->worker;
	int k = 0;
	if (!w) return;
	while (get_acq(&w->head) != w->tail) relax(++k);
}

int prop_start_thread(pinheck_prop *p)
{
	prop_worker *w;
	if (p->worker) return 0;
	w = (prop_worker *)calloc(1, sizeof(*w));
	if (!w) return -1;
	p->worker = w;
#ifdef _WIN32
	w->os.ev = CreateEvent(NULL, FALSE, FALSE, NULL);
	w->os.th = w->os.ev ? CreateThread(NULL, 0, worker, p, 0, NULL) : NULL;
	if (!w->os.th) { if (w->os.ev) CloseHandle(w->os.ev); free(w); p->worker = NULL; return -1; }
#else
	pthread_mutex_init(&w->os.m, NULL);
	pthread_cond_init(&w->os.c, NULL);
	if (pthread_create(&w->os.th, NULL, worker, p)) {
		pthread_cond_destroy(&w->os.c);
		pthread_mutex_destroy(&w->os.m);
		free(w);
		p->worker = NULL;
		return -1;
	}
#endif
	return 0;
}

void prop_stop_thread(pinheck_prop *p)
{
	prop_worker *w = (prop_worker *)p->worker;
	if (!w) return;
	post(p, CMD_QUIT, 0, 0);
#ifdef _WIN32
	WaitForSingleObject(w->os.th, INFINITE);
	CloseHandle(w->os.th);
	CloseHandle(w->os.ev);
#else
	pthread_join(w->os.th, NULL);
	pthread_cond_destroy(&w->os.c);
	pthread_mutex_destroy(&w->os.m);
#endif
	free(w);
	p->worker = NULL;
}
#endif

void prop_catch_up(pinheck_prop *p, uint64_t pic_cycle)
{
	if (p->worker) { post(p, CMD_CATCH_UP, pic_cycle, 0); return; }
	p->stamp = p->clock ? p->clock(p->clock_ctx) : pic_cycle;
	do_catch_up(p, pic_cycle);
}

void prop_pic_pins(pinheck_prop *p, uint64_t pic_cycle, uint32_t pins)
{
	pins &= PROP_PIC_PINS;
	if (p->worker) { post(p, CMD_PINS, pic_cycle, pins); return; }
	p->stamp = p->clock ? p->clock(p->clock_ctx) : pic_cycle;
	do_pic_pins(p, pic_cycle, pins);
}
'''),
])

edit('src/wpc/pinheck/prop.h', [
(r'''typedef void (*prop_pins_fn)(void *ctx, uint64_t prop_cycle, uint32_t out, uint32_t dir);

''',
 r'''typedef void (*prop_pins_fn)(void *ctx, uint64_t prop_cycle, uint32_t out, uint32_t dir);
typedef uint64_t (*prop_clock_fn)(void *ctx);

'''),
(r'''	void *pins_ctx;
} pinheck_prop;
''',
 r'''	void *pins_ctx;
	prop_clock_fn clock; /* the PIC32 cycle now */
	void *clock_ctx;
	uint64_t stamp; /* the PIC32 cycle at which the running call was made */
	void *worker; /* worker thread, NULL = calls run inline */
} pinheck_prop;
'''),
(r'''uint64_t prop_time(const pinheck_prop *p, uint64_t pic_cycle);
''',
 r'''void prop_set_clock(pinheck_prop *p, prop_clock_fn fn, void *ctx);
uint64_t prop_stamp(const pinheck_prop *p);
int prop_start_thread(pinheck_prop *p);
void prop_stop_thread(pinheck_prop *p);
void prop_sync(pinheck_prop *p);
uint64_t prop_time(pinheck_prop *p, uint64_t pic_cycle);
'''),
])

edit('src/wpc/pinheck.c', [
(r'''static uint64_t pinheck_hold(void *ctx, uint64_t cycle)
{
	(void)ctx;
	prop_catch_up(&prop, cycle);
	boot_advance(&boot, cycle);
	return boot_hold(&boot, cycle);
}
''',
 r'''/* once the bootloader stand-in has released the application it neither holds the PIC32 nor answers the
   Propeller, so the Propeller's catch-up can run on its own thread without the PIC32 waiting for it */
static uint64_t pinheck_hold(void *ctx, uint64_t cycle)
{
	(void)ctx;
	prop_catch_up(&prop, cycle);
	if (boot.state == BOOT_APP && prop.worker) return 0;
	prop_sync(&prop);
	boot_advance(&boot, cycle);
	return boot_hold(&boot, cycle);
}

static uint64_t pinheck_pic_now(void *ctx) { (void)ctx; return pic32cpu_soc()->cpu.cycles; }
'''),
(r'''	uint64_t pic = pic32cpu_soc()->cpu.cycles;
''',
 r'''	uint64_t pic = prop_stamp(&prop);
'''),
(r'''	int x, y;
	/* the core's visible area is larger than the panel: clear it so nothing stale shows */
''',
 r'''	int x, y;
	prop_sync(&prop);
	/* the core's visible area is larger than the panel: clear it so nothing stale shows */
'''),
(r'''	const char *log = getenv("PINHECK_UART1_LOG"), *plog = getenv("PINHECK_PROP_LOG");

	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
''',
 r'''	const char *log = getenv("PINHECK_UART1_LOG"), *plog = getenv("PINHECK_PROP_LOG");

	prop_stop_thread(&prop);
	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
'''),
(r'''	pic32cpu_set_board(&board);
}
''',
 r'''	pic32cpu_set_board(&board);
	prop_set_clock(&prop, pinheck_pic_now, NULL);
	if (!getenv("PINHECK_THREADS") || atoi(getenv("PINHECK_THREADS")) != 0) prop_start_thread(&prop);
}
'''),
(r'''	if (locals.idle) return;
	core_nvram(file, read_or_write, u13mem, sizeof(u13mem), 0xFF);
''',
 r'''	if (locals.idle) return;
	prop_sync(&prop);
	core_nvram(file, read_or_write, u13mem, sizeof(u13mem), 0xFF);
'''),
(r'''{
	if (locals.uart1) fclose(locals.uart1);
''',
 r'''{
	prop_stop_thread(&prop);
	if (locals.uart1) fclose(locals.uart1);
'''),
])

edit('src/unix/unix.mak', [
(r'''MY_LIBS = $(LIBS) $(LIBS.$(ARCH)) $(LIBS.$(DISPLAY_METHOD)) -lz
''',
 r'''MY_LIBS = $(LIBS) $(LIBS.$(ARCH)) $(LIBS.$(DISPLAY_METHOD)) -lz -lpthread
'''),
])
EOF
```

- [ ] **Step 4: The suites pass**

Run: `for s in link display audio board; do nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh > build/m9b-$s.log 2>&1; echo "$s: exit $? $(tail -1 build/m9b-$s.log)"; done; grep -a '^prop:' build/m9b-link.log` (about 10 minutes)
Expected:
```text
link: exit 0 link: 0 failed
display: exit 0 display: 0 failed
audio: exit 0 audio: 0 failed
board: exit 0 board: 0 failed
prop: ok
```

- [ ] **Step 5: Rebuild and compare with the reference, with and without the thread**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9b.log 2>&1; tail -1 build/sdl3pinmame/m9b.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video
PINHECK_THREADS=0 SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract | grep -v ' ref:'
```
Expected (speeds vary with the load; the last three lines are the single-thread run):
```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.741x over 30.0 s emulated (40.5 s wall), whole run 16.9 G instructions and 2.4 s CPU per emulated s, load 6.19
bench attract ref: 0.417x over 30.0 s emulated (71.9 s wall), whole run 22.4 G instructions and 2.2 s CPU per emulated s, load 5.65
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 0.648x over 24.0 s emulated (37.0 s wall), whole run 18.2 G instructions and 2.7 s CPU per emulated s, load 5.91
bench video ref: 0.425x over 24.0 s emulated (56.4 s wall), whole run 24.2 G instructions and 2.2 s CPU per emulated s, load 4.38
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.632x over 30.0 s emulated (47.4 s wall), whole run 16.6 G instructions and 1.5 s CPU per emulated s, load 4.97
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 6: Commit**

```bash
git add src/wpc/pinheck/prop.c src/wpc/pinheck/prop.h src/wpc/pinheck.c src/unix/unix.mak tests/pinheck/link/prop_test.c tests/pinheck/link/check.sh
git commit -m "pinheck: the Propeller's calls run in order on a worker thread; the PIC32 side waits where it reads Propeller state"
```

### Task 6: Translated Propeller code (stage 3d)

**Files:**
- Create: `src/cpu/p8x32a/p8x32ajit.h`, `src/cpu/p8x32a/p8x32ajit.cpp`, `tests/pinheck/p8x32a/chip/jit.spin`
- Modify: `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32a.h`, `cmake/asmjit.cmake`, `src/wpc/pinheck.c`, `tests/pinheck/p8x32a/check.sh`, `tests/pinheck/p8x32a/run.c`

**Interfaces:**
- Consumes: Task 3's `run_local` (`fl`, `tl`, `nins`), `loop_edge`, `p8x32a_loop` (`head`, `head_t`, `dirty`, `hub`, `nins`); `cmake/asmjit.cmake`'s `pinmame_enable_asmjit` and `PINMAME_JIT_ASMJIT`.
- Produces: in `p8x32a.h`, `P8X32A_JMAX` (32), `P8X32A_JDYN` (`0x3FFFF`, the S and D fields), `p8x32a_jst` (a block's in/out state), `p8x32a_jblk` (`fn`, `body`, `len`, `valid`, `words`, `dyn`), `p8x32a_jit_fn`; `p8x32a` gains `jit_build` (NULL: no translation), `jit`, `jblk[8][512]`, `jvar[8][512]` (the bits in which each slot has changed), `jcode[8][64]` (the fixed slots of valid blocks); in `p8x32ajit.h`, `void *p8x32a_jit_new(void)` (NULL where no translator exists), `void p8x32a_jit_free(void *jit)`, `p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var)`; `p8run` translates when built with `P8X32A_JIT`; `check.sh` with `P8X32A_JIT=1` builds asmjit once into `build/asmjit` and runs every test translated; `PINHECK_JIT=0` keeps the driver from translating.

A run of local instructions becomes x86-64 code that does, instruction for instruction, what `run_local` does; writes to translated slots drop their blocks, and slots whose S or D field keeps changing are read at run time (Ruling 5).

- [ ] **Step 1: The self-modifying code test and the translated test runner**

`chip/jit.spin` has a cog dispatch through a `JMP` its own `MOVS` patches every time, patch the instruction right after the patching one (the P1 runs the word it has already fetched: 9, then 7), fill and sum a table through `MOVD`/`MOVS`-patched instructions, exercise `MUXC`–`MUXNZ`, `CMPS`, `RCR`, `ROR`, `MOVI` and the flags, run an instruction whose S field another block keeps patching and then turn it from `ADD` into `SUB` with `MOVI`, rewrite an instruction of a loop it is running, and branch with `TJZ`/`TJNZ`/`DJNZ`; each result goes to hub RAM, which the RTL comparison checks.

`tests/pinheck/p8x32a/chip/jit.spin`:

```text
' self-modifying and translated local code: every result is written to hub and compared with the RTL
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     n, #20
l_disp  mov     t, n
        and     t, #3
        add     t, #tab
        movs    l_jmp, t
        add     acc, n
l_jmp   jmp     #0-0
c0      add     acc, #1
        jmp     #dnext
c1      xor     acc, n
        jmp     #dnext
c2      rcl     acc, #3 wc
        jmp     #dnext
c3      sar     acc, n wz
        muxz    acc, #$80
dnext   djnz    n, #l_disp
        wrlong  acc, ptr
        add     ptr, #4
        mov     n, #2
l_twice movs    l_nx, #7
l_nx    mov     x, #9
        wrlong  x, ptr
        add     ptr, #4
        djnz    n, #l_twice
        mov     n, #8
        mov     t, #table
l_md    movd    l_wr, t
        add     t, #1
        nop
l_wr    mov     0-0, n
        djnz    n, #l_md
        mov     n, #8
        mov     t, #table
        mov     x, #0
l_sum   movs    l_add, t
        add     t, #1
        nop
l_add   add     x, 0-0
        rol     x, #5
        djnz    n, #l_sum
        wrlong  x, ptr
        add     ptr, #4
        mov     x, #$55
        mov     y, #0
        test    x, #1 wc
        muxc    y, #$F0
        muxnc   y, #$0F
        test    x, #0 wz
        muxz    y, #$100
        muxnz   y, m200
        cmps    x, y wc, wz
        muxc    y, m400
        muxz    y, m800
        rcr     y, #4 wc
        muxc    y, m1000
        ror     y, x
        movi    y, #$1A5
        wrlong  y, ptr
        add     ptr, #4
        mov     n, #6
        mov     acc, #0
l_dyn   movs    l_tgt, n
        jmp     #l_run
l_run   mov     y, y
l_tgt   add     acc, #0-0
        cmp     n, #3 wz
  if_z  movi    l_tgt, #%100001_001
        djnz    n, #l_dyn
        wrlong  acc, ptr
        add     ptr, #4
        mov     n, #3
        mov     acc, #0
l_b     add     acc, #1
        movs    l_b, n
        shl     acc, #1
        djnz    n, #l_b
        wrlong  acc, ptr
        add     ptr, #4
        mov     x, #0
        tjz     x, #l_z1
        or      acc, #1
l_z1    tjnz    x, #l_z2
        or      acc, #2
l_z2    mov     x, #1
        tjnz    x, #l_z3
        or      acc, #4
l_z3    sub     x, #1 wz, wc
if_z    or      acc, #8
if_c    or      acc, #16
        mov     x, #0
        sub     x, #1 wc
if_c    or      acc, #32
        djnz    x, #l_z4
l_z4    wrlong  acc, ptr
        add     ptr, #4
        cogid   t
        cogstop t
tab     jmp     #c0
        jmp     #c1
        jmp     #c2
        jmp     #c3
n       long    0
t       long    0
x       long    0
y       long    0
acc     long    $12345
ptr     long    $6000
m200    long    $200
m400    long    $400
m800    long    $800
m1000   long    $1000
table   res     8
        long    $C0DEE0D0
```

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/p8x32a/check.sh', [
(r'''$CC -I$CORE -I$DEV -o $B/p8run run.c $CORE/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
if [ -f dasm_test.c ]; then
''',
 r'''$CC -I$CORE -I$DEV -o $B/p8run run.c $CORE/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
# P8X32A_JIT=1: every test runs with local instruction runs translated (x86-64 only)
if [ "$P8X32A_JIT" = 1 ]; then
	ASMJIT=../../../ext/asmjit
	JF="-O2 -std=c++17 -DASMJIT_STATIC -DASMJIT_NO_FOREIGN -DASMJIT_NO_UJIT -I$ASMJIT"
	mkdir -p $B/asmjit
	if [ ! -f $B/asmjit/libasmjit.a ]; then
		for f in $ASMJIT/asmjit/core/*.cpp $ASMJIT/asmjit/x86/*.cpp $ASMJIT/asmjit/support/*.cpp; do
			c++ $JF -c "$f" -o $B/asmjit/$(basename "$f" .cpp).o || exit 2
		done
		ar rcs $B/asmjit/libasmjit.a $B/asmjit/*.o || exit 2
	fi
	c++ $JF -Wall -Wextra -Werror -I$CORE -c $CORE/p8x32ajit.cpp -o $B/p8x32ajit.o || exit 2
	for f in run.c $CORE/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c; do
		$CC -DP8X32A_JIT -I$CORE -I$DEV -c "$f" -o $B/jit-$(basename "$f" .c).o || exit 2
	done
	c++ -o $B/p8run $B/jit-*.o $B/p8x32ajit.o $B/asmjit/libasmjit.a -lz -lpthread || exit 2
	echo "p8run: translated"
fi
if [ -f dasm_test.c ]; then
'''),
])

edit('tests/pinheck/p8x32a/run.c', [
(r'''#include "p8x32a.h"
#include "eeprom.h"
''',
 r'''#include "p8x32a.h"
#ifdef P8X32A_JIT
#include "p8x32ajit.h"
#endif
#include "eeprom.h"
'''),
(r'''	p8x32a_init(&chip, &bus);
	if (!load(rom, chip.hub + 0x8000, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "p8run: rom must be 32768 bytes\n"); return 2; }
''',
 r'''	p8x32a_init(&chip, &bus);
#ifdef P8X32A_JIT
	if ((chip.jit = p8x32a_jit_new()) != NULL) chip.jit_build = p8x32a_jit_build;
#endif
	if (!load(rom, chip.hub + 0x8000, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "p8run: rom must be 32768 bytes\n"); return 2; }
'''),
])
EOF
```

- [ ] **Step 2: The test passes on the interpreter; the translated runner does not build yet**

Run: `nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh | tail -1; P8X32A_JIT=1 nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh 2>&1 | grep -m1 'error'`
Expected (the first builds asmjit for the tests, about a minute):
```text
p8x32a: 228 passed, 0 failed
cc1plus: fatal error: ../../../src/cpu/p8x32a/p8x32ajit.cpp: No such file or directory
```

- [ ] **Step 3: The translator**

`src/cpu/p8x32a/p8x32ajit.h`:

```c
#ifndef P8X32AJIT_H
#define P8X32AJIT_H

#include "p8x32a.h"

#ifdef __cplusplus
extern "C" {
#endif

/* x86-64 translation of local instruction runs; p8x32a_jit_new returns NULL where it is not available */
void *p8x32a_jit_new(void);
void p8x32a_jit_free(void *jit);
p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var);

#ifdef __cplusplus
}
#endif

#endif
```

`src/cpu/p8x32a/p8x32ajit.cpp`:

```cpp
// Translates runs of a cog's local instructions to x86-64 code with asmjit (see p8x32a.h, p8x32a_jblk). Each
// instruction does exactly what run_local() does for it; a run leaves after a jump that runs, before a slot whose
// word no longer fits, and ends before any instruction run_local() would not run and before cog address 511.
#include "p8x32ajit.h"
#include <cstddef>

#if defined(__x86_64__) || defined(_M_X64)
#include <asmjit/x86.h>
#include <cstdlib>
#include <new>
#include <vector>

using namespace asmjit;

namespace {

struct P8Jit {
	JitRuntime rt;
	std::vector<p8x32a_jblk *> blocks;
	uint64_t tail, tail_ret; // the code a block leaves through, and its return to run_local
};

enum { OP_ROR = 0x08, OP_ROL, OP_SHR, OP_SHL, OP_RCR, OP_RCL, OP_SAR, OP_MOVS = 0x14, OP_MOVD, OP_MOVI, OP_JMP,
       OP_AND, OP_ANDN, OP_OR, OP_XOR, OP_MUXC, OP_MUXNC, OP_MUXZ, OP_MUXNZ, OP_ADD, OP_SUB, OP_MOV = 0x28,
       OP_CMPS = 0x30, OP_DJNZ = 0x39, OP_TJNZ, OP_TJZ };

inline unsigned op_of(uint32_t i) { return i >> 26; }
inline unsigned dst_of(uint32_t i) { return (i >> 9) & 511; }
inline unsigned src_of(uint32_t i) { return i & 511; }
inline bool fim(uint32_t i) { return (i >> 22) & 1; }
inline bool fwr(uint32_t i) { return (i >> 23) & 1; }
inline bool fwc(uint32_t i) { return (i >> 24) & 1; }
inline bool fwz(uint32_t i) { return (i >> 25) & 1; }
inline unsigned cond_of(uint32_t i) { return (i >> 18) & 15; }

bool op_ok(unsigned op)
{
	return (op >= OP_ROR && op <= OP_SAR) || (op >= OP_MOVS && op <= OP_SUB) || op == OP_MOV || op == OP_CMPS ||
	       (op >= OP_DJNZ && op <= OP_TJZ);
}

// a fixed word run_local() would run as a local instruction
bool supported(uint32_t i)
{
	if (!op_ok(op_of(i))) return false;
	if (!fim(i) && src_of(i) >= 0x1F0) return false;
	if (fwr(i) && dst_of(i) >= 0x1F0) return false;
	return true;
}

inline bool is_jump(unsigned op) { return op == OP_JMP || op >= OP_DJNZ; }

// registers: r10 st, r9 ram, r8d fl, ecx s, edx d, eax result, r11d carry out, esi/edi scratch,
// ebx the word fetched for the next slot, r12d the word of this slot, r13d its D address (run-time slots),
// r14 the time of the block's first instruction, r15d the budget left at it, ebp instructions run so far
const x86::Gp ST = x86::r10, RAM = x86::r9, FL = x86::r8d, NEXTW = x86::ebx, CURW = x86::r12d, DADR = x86::r13d,
              T2 = x86::r14, BUDGET = x86::r15d, TOTAL = x86::ebp;

// every block and the tail share this frame, so a block can continue in another
void make_frame(FuncDetail &func, FuncFrame &frame, const Environment &env)
{
	func.init(FuncSignature::build<uint32_t, p8x32a_jst *>(), env);
	frame.init(func);
	frame.add_dirty_regs(x86::rax, x86::rcx, x86::rdx, x86::rsi, x86::rdi, x86::r8, x86::r9, x86::r10, x86::r11,
	                     x86::rbx, x86::rbp, x86::r12, x86::r13, x86::r14, x86::r15);
}

x86::Mem cog(unsigned a) { return x86::dword_ptr(RAM, (int)(a * 4)); }
x86::Mem stf(size_t off) { return x86::dword_ptr(ST, (int)off); }
x86::Mem cogr(const x86::Gp &r) { return x86::dword_ptr(RAM, r.r64(), 2); }

void parity_to_r11(x86::Assembler &a)
{
	a.mov(x86::esi, x86::eax);
	a.shr(x86::esi, 16);
	a.xor_(x86::esi, x86::eax);
	a.mov(x86::edi, x86::esi);
	a.shr(x86::edi, 8);
	a.xor_(x86::esi, x86::edi);
	a.xor_(x86::r11d, x86::r11d);
	a.test(x86::sil, x86::sil);
	a.setnp(x86::r11b);
}

struct Emit {
	x86::Assembler &a;
	const FuncFrame &frame;
	unsigned base;
	uint64_t tail, tail_ret;

	// k instructions ran in this block; the last one's exit state is in st
	void leave(unsigned k)
	{
		a.mov(x86::eax, k);
		a.mov(x86::rsi, tail);
		a.jmp(x86::rsi);
	}
	// stop before slot k: the run so far ends with a sequential fetch of slot k
	void stop_before(unsigned k, const x86::Gp &word)
	{
		a.mov(stf(offsetof(p8x32a_jst, pc)), (base + k) & 511);
		a.mov(stf(offsetof(p8x32a_jst, px)), (base + k) & 511);
		a.mov(stf(offsetof(p8x32a_jst, nix)), word);
		a.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
		a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
		leave(k);
	}

	// slot k with word i (fixed, or the S/D fields read at run time from CURW when dyn); next_dyn: slot k+1 is read
	// at run time (or k is the last), so its word is fetched into NEXTW here, before this instruction writes
	void insn(unsigned k, uint32_t i, bool dyn, bool last, bool prefetch_next)
	{
		unsigned op = op_of(i), cond = cond_of(i), addr = base + k, pc = (addr + 1) & 511;
		bool imm = fim(i), jump = is_jump(op);
		Label skip = a.new_label(), before = a.new_label();

		if (dyn) {
			// the word this slot was fetched with: ix for slot 0, else fetched during the previous slot
			if (k == 0) a.mov(CURW, stf(offsetof(p8x32a_jst, ix)));
			else a.mov(CURW, NEXTW);
			a.mov(x86::eax, CURW);
			a.and_(x86::eax, ~P8X32A_JDYN);
			a.cmp(x86::eax, i & ~P8X32A_JDYN);
			a.jne(before);
			if (!imm) {
				a.mov(x86::esi, CURW);
				a.and_(x86::esi, 511);
				a.cmp(x86::esi, 0x1F0);
				a.jae(before);
			}
			a.mov(DADR, CURW);
			a.shr(DADR, 9);
			a.and_(DADR, 511);
			if (fwr(i)) {
				a.cmp(DADR, 0x1F0);
				a.jae(before);
			}
		}
		if (prefetch_next) a.mov(NEXTW, cog(pc));
		if (cond == 0) goto end;
		if (cond != 15) {
			a.mov(x86::eax, cond);
			a.bt(x86::eax, FL);
			a.jnc(skip);
		}
		if (dyn) {
			if (imm) { a.mov(x86::ecx, CURW); a.and_(x86::ecx, 511); }
			else a.mov(x86::ecx, cogr(x86::esi));
			a.mov(x86::edx, cogr(DADR));
		} else {
			if (imm) a.mov(x86::ecx, src_of(i));
			else a.mov(x86::ecx, cog(src_of(i)));
			a.mov(x86::edx, cog(dst_of(i)));
		}
		switch (op) {
		case OP_ROR: a.mov(x86::eax, x86::edx); a.ror(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.and_(x86::r11d, 1); break;
		case OP_ROL: a.mov(x86::eax, x86::edx); a.rol(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.shr(x86::r11d, 31); break;
		case OP_SHR: a.mov(x86::eax, x86::edx); a.shr(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.and_(x86::r11d, 1); break;
		case OP_SHL: a.mov(x86::eax, x86::edx); a.shl(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.shr(x86::r11d, 31); break;
		case OP_SAR: a.mov(x86::eax, x86::edx); a.sar(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.and_(x86::r11d, 1); break;
		case OP_RCR: case OP_RCL:
			a.mov(x86::esi, -1);
			if (op == OP_RCR) a.shr(x86::esi, x86::cl); else a.shl(x86::esi, x86::cl);
			a.not_(x86::esi);
			a.bt(FL, 1);
			a.sbb(x86::edi, x86::edi);
			a.and_(x86::esi, x86::edi);
			a.mov(x86::eax, x86::edx);
			if (op == OP_RCR) a.shr(x86::eax, x86::cl); else a.shl(x86::eax, x86::cl);
			a.or_(x86::eax, x86::esi);
			a.mov(x86::r11d, x86::edx);
			if (op == OP_RCR) a.and_(x86::r11d, 1); else a.shr(x86::r11d, 31);
			break;
		case OP_MOVS: case OP_MOVD: case OP_MOVI: case OP_JMP:
			a.mov(x86::eax, x86::ecx);
			if (op == OP_MOVS) { a.and_(x86::eax, 511); a.mov(x86::esi, x86::edx); a.and_(x86::esi, 0xFFFFFE00); }
			else if (op == OP_MOVD) { a.and_(x86::eax, 511); a.shl(x86::eax, 9); a.mov(x86::esi, x86::edx); a.and_(x86::esi, 0xFFFC01FF); }
			else if (op == OP_MOVI) { a.shl(x86::eax, 23); a.mov(x86::esi, x86::edx); a.and_(x86::esi, 0x007FFFFF); }
			else { a.mov(x86::eax, pc); a.mov(x86::esi, x86::edx); a.and_(x86::esi, 0xFFFFFE00); }
			a.or_(x86::eax, x86::esi);
			a.xor_(x86::r11d, x86::r11d);
			a.cmp(x86::edx, x86::ecx);
			a.setb(x86::r11b);
			break;
		case OP_AND: a.mov(x86::eax, x86::edx); a.and_(x86::eax, x86::ecx); break;
		case OP_ANDN: a.mov(x86::eax, x86::ecx); a.not_(x86::eax); a.and_(x86::eax, x86::edx); break;
		case OP_OR: a.mov(x86::eax, x86::edx); a.or_(x86::eax, x86::ecx); break;
		case OP_XOR: a.mov(x86::eax, x86::edx); a.xor_(x86::eax, x86::ecx); break;
		case OP_MUXC: case OP_MUXNC: case OP_MUXZ: case OP_MUXNZ:
			a.mov(x86::eax, x86::edx);
			a.or_(x86::eax, x86::ecx);
			a.mov(x86::esi, x86::ecx);
			a.not_(x86::esi);
			a.and_(x86::esi, x86::edx);
			a.bt(FL, (op == OP_MUXC || op == OP_MUXNC) ? 1 : 0);
			if (op == OP_MUXC || op == OP_MUXZ) a.cmovnc(x86::eax, x86::esi); else a.cmovc(x86::eax, x86::esi);
			break;
		case OP_ADD: a.xor_(x86::r11d, x86::r11d); a.mov(x86::eax, x86::edx); a.add(x86::eax, x86::ecx); a.setc(x86::r11b); break;
		case OP_SUB: a.xor_(x86::r11d, x86::r11d); a.mov(x86::eax, x86::edx); a.sub(x86::eax, x86::ecx); a.setc(x86::r11b); break;
		case OP_CMPS: a.xor_(x86::r11d, x86::r11d); a.mov(x86::eax, x86::edx); a.cmp(x86::edx, x86::ecx); a.setl(x86::r11b); a.sub(x86::eax, x86::ecx); break;
		case OP_MOV: a.mov(x86::eax, x86::ecx); a.mov(x86::r11d, x86::ecx); a.shr(x86::r11d, 31); break;
		case OP_DJNZ: a.xor_(x86::r11d, x86::r11d); a.test(x86::edx, x86::edx); a.sete(x86::r11b); a.lea(x86::eax, x86::ptr(x86::rdx, -1)); break;
		case OP_TJNZ: case OP_TJZ: a.xor_(x86::r11d, x86::r11d); a.mov(x86::eax, x86::edx); break;
		}
		if (op >= OP_AND && op <= OP_MUXNZ && fwc(i)) parity_to_r11(a);
		if (jump) {
			// the word at the target is fetched before the write
			a.mov(stf(offsetof(p8x32a_jst, s)), x86::ecx);
			a.mov(stf(offsetof(p8x32a_jst, d)), x86::edx);
			a.mov(x86::esi, x86::ecx);
			a.and_(x86::esi, 511);
			a.mov(stf(offsetof(p8x32a_jst, px)), x86::esi);
			a.mov(x86::esi, cogr(x86::esi));
			a.mov(stf(offsetof(p8x32a_jst, nix)), x86::esi);
			a.xor_(x86::esi, x86::esi);
			if (op == OP_DJNZ) { a.cmp(x86::edx, 1); a.sete(x86::sil); }
			else if (op == OP_TJNZ) { a.test(x86::edx, x86::edx); a.sete(x86::sil); }
			else if (op == OP_TJZ) { a.test(x86::edx, x86::edx); a.setne(x86::sil); }
			a.mov(stf(offsetof(p8x32a_jst, jc)), x86::esi);
			a.mov(stf(offsetof(p8x32a_jst, jmp)), 1);
			a.mov(stf(offsetof(p8x32a_jst, pc)), pc);
			if (dyn) a.mov(stf(offsetof(p8x32a_jst, w)), CURW);
			else a.mov(stf(offsetof(p8x32a_jst, w)), i);
		}
		if (fwr(i)) {
			Label same = a.new_label(), first = a.new_label();
			x86::Mem m = dyn ? cogr(DADR) : cog(dst_of(i));
			a.cmp(m, x86::eax);
			a.je(same);
			a.mov(x86::esi, m);
			a.mov(m, x86::eax);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
			a.mov(x86::byte_ptr(x86::rdi, (int)offsetof(p8x32a_loop, dirty)), 1);
			// a fixed code slot changed: report it
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, code)));
			if (dyn) a.bt(x86::dword_ptr(x86::rdi), DADR);
			else a.bt(x86::dword_ptr(x86::rdi, (int)((dst_of(i) >> 5) * 4)), dst_of(i) & 31);
			a.jnc(same);
			a.cmp(stf(offsetof(p8x32a_jst, inv)), 0);
			a.je(first);
			a.mov(stf(offsetof(p8x32a_jst, inv)), -1);
			a.jmp(same);
			a.bind(first);
			a.mov(stf(offsetof(p8x32a_jst, inv_old)), x86::esi);
			if (dyn) { a.lea(x86::esi, x86::ptr(DADR.r64(), 1)); a.mov(stf(offsetof(p8x32a_jst, inv)), x86::esi); }
			else a.mov(stf(offsetof(p8x32a_jst, inv)), dst_of(i) + 1);
			a.bind(same);
		}
		if (fwz(i)) {
			a.xor_(x86::esi, x86::esi);
			a.test(x86::eax, x86::eax);
			a.sete(x86::sil);
			a.and_(FL, ~1u);
			a.or_(FL, x86::esi);
		}
		if (fwc(i)) {
			a.and_(FL, ~2u);
			a.mov(x86::esi, x86::r11d);
			a.add(x86::esi, x86::esi);
			a.or_(FL, x86::esi);
		}
		if (jump) leave(k + 1);
		a.bind(skip);
	end:
		if (last) stop_before(k + 1, NEXTW);
		if (dyn) {
			Label over = a.new_label();
			a.jmp(over);
			a.bind(before);
			if (k == 0) {
				a.mov(x86::rsi, tail_ret);
				a.jmp(x86::rsi);
			} else
				stop_before(k, CURW);
			a.bind(over);
		}
	}
};

// After a block: account for the k (eax) instructions it ran, do run_local's loop_edge search step for a backward
// jump, and continue in the block at the next address when nothing needs run_local.
bool build_tail(P8Jit *j)
{
	CodeHolder code;
	code.init(j->rt.environment());
	x86::Assembler a(&code);
	FuncDetail func;
	FuncFrame frame;
	make_frame(func, frame, code.environment());
	frame.finalize();
	Label ret = a.new_label(), no_edge = a.new_label(), diff = a.new_label(), reset = a.new_label(), full = a.new_label();
	const x86::Gp L = x86::rdi;

	a.add(TOTAL, x86::eax);
	a.lea(T2, x86::ptr(T2, x86::rax, 2));
	a.sub(BUDGET, x86::eax);
	a.mov(L, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
	a.add(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), x86::ax);
	// a backward jump: the loop search state (loop_edge in SEARCH)
	a.cmp(stf(offsetof(p8x32a_jst, jmp)), 0);
	a.je(no_edge);
	a.cmp(stf(offsetof(p8x32a_jst, jc)), 0);
	a.jne(no_edge);
	a.mov(x86::ecx, stf(offsetof(p8x32a_jst, px)));
	a.cmp(x86::ecx, stf(offsetof(p8x32a_jst, pc)));
	a.jae(no_edge);
	a.movzx(x86::edx, x86::word_ptr(L, (int)offsetof(p8x32a_loop, head)));
	a.lea(x86::rax, x86::ptr(T2, -3));
	a.cmp(x86::ecx, x86::edx);
	a.jne(diff);
	a.cmp(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.jne(reset);
	a.cmp(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), P8X32A_PAT / 2);
	a.ja(reset);
	a.cmp(x86::rax, x86::qword_ptr(L, (int)offsetof(p8x32a_loop, head_t)));
	a.jbe(reset);
	a.mov(stf(offsetof(p8x32a_jst, edge)), 1);
	a.jmp(ret);
	a.bind(diff);
	a.cmp(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.jne(reset);
	a.cmp(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), P8X32A_PAT / 2);
	a.ja(reset);
	a.cmp(x86::edx, 0xFFFF);
	a.jne(no_edge);
	a.bind(reset);
	a.mov(x86::word_ptr(L, (int)offsetof(p8x32a_loop, head)), x86::cx);
	a.mov(x86::qword_ptr(L, (int)offsetof(p8x32a_loop, head_t)), x86::rax);
	a.mov(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.mov(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, hub)), 0);
	a.mov(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), 0);
	a.bind(no_edge);
	// continue unless a code slot changed, the next instruction is cancelled, or the next block does not fit
	a.cmp(stf(offsetof(p8x32a_jst, inv)), 0);
	a.jne(ret);
	a.cmp(stf(offsetof(p8x32a_jst, jc)), 0);
	a.jne(ret);
	a.mov(x86::ecx, stf(offsetof(p8x32a_jst, px)));
	a.cmp(x86::ecx, 511);
	a.je(ret);
	a.mov(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, tab)));
	a.mov(x86::rsi, x86::qword_ptr(x86::rsi, x86::rcx, 3));
	a.test(x86::rsi, x86::rsi);
	a.jz(ret);
	a.cmp(x86::dword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, valid)), 0);
	a.je(ret);
	a.mov(x86::eax, x86::dword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, len)));
	a.test(x86::eax, x86::eax);
	a.jz(ret);
	a.cmp(x86::eax, BUDGET);
	a.ja(ret);
	a.mov(x86::edx, stf(offsetof(p8x32a_jst, nix)));
	a.xor_(x86::edx, x86::dword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, words)));
	a.test(x86::byte_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, dyn)), 1);
	a.jz(full);
	a.and_(x86::edx, ~P8X32A_JDYN);
	a.bind(full);
	a.test(x86::edx, x86::edx);
	a.jnz(ret);
	a.mov(x86::eax, stf(offsetof(p8x32a_jst, nix)));
	a.mov(stf(offsetof(p8x32a_jst, ix)), x86::eax);
	a.jmp(x86::qword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, body)));
	a.bind(ret);
	a.mov(stf(offsetof(p8x32a_jst, fl)), FL);
	a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, t2)), T2);
	a.mov(stf(offsetof(p8x32a_jst, budget)), BUDGET);
	a.mov(x86::eax, TOTAL);
	a.emit_epilog(frame);
	void *fn = NULL;
	if (j->rt.add(&fn, &code) != kErrorOk) return false;
	j->tail = (uint64_t)(uintptr_t)fn;
	j->tail_ret = j->tail + code.label_offset(ret);
	return true;
}

} // namespace

extern "C" void *p8x32a_jit_new(void)
{
	P8Jit *j = new (std::nothrow) P8Jit();
	if (j && !build_tail(j)) { delete j; j = NULL; }
	return j;
}

extern "C" void p8x32a_jit_free(void *jit)
{
	P8Jit *j = (P8Jit *)jit;
	if (!j) return;
	for (size_t k = 0; k < j->blocks.size(); k++) {
		if (j->blocks[k]->fn) j->rt.release(j->blocks[k]->fn);
		free(j->blocks[k]);
	}
	delete j;
}

extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var)
{
	P8Jit *j = (P8Jit *)jit;
	p8x32a_jblk *b = old;
	unsigned len = 0, k;

	if (!b) {
		b = (p8x32a_jblk *)calloc(1, sizeof(*b));
		if (!b) return NULL;
		j->blocks.push_back(b);
	} else if (b->fn) {
		j->rt.release(b->fn);
	}
	b->fn = NULL;
	b->len = 0;
	b->valid = 1;
	b->dyn = 0;
	b->words[0] = ix;
	// the run: translatable words below the special registers, ending at an unconditional jump or a slot whose D field may point
	// anywhere; a slot whose op, flags or condition have changed is not translated
	while (len < P8X32A_JMAX && a + len < 0x1F0) {
		uint32_t w = len ? ram[a + len] : ix, v = var[a + len];
		bool dyn = v != 0;
		if ((v & ~P8X32A_JDYN) || !op_ok(op_of(w))) break;
		if (!dyn && !supported(w)) break;
		b->words[len] = w;
		if (dyn) b->dyn |= 1u << len;
		len++;
		if ((is_jump(op_of(w)) && cond_of(w) == 15) || (dyn && fwr(w))) break;
	}
	// a fixed instruction must not rewrite a later fixed slot other than the next, whose word it already fetched
	// (run-time slots are fetched as the run goes)
	for (k = 0; k < len; k++) {
		uint32_t w = b->words[k];
		unsigned d = dst_of(w);
		if (!(b->dyn >> k & 1) && fwr(w) && d >= a + k + 2 && d < a + len && !(b->dyn >> (d - a) & 1)) len = d - a;
	}
	if (!len) return b;

	CodeHolder code;
	code.init(j->rt.environment());
	x86::Assembler as(&code);
	FuncDetail func;
	FuncFrame frame;
	make_frame(func, frame, code.environment());
	FuncArgsAssignment args(&func);
	args.assign_all(ST);
	args.update_func_frame(frame);
	frame.finalize();
	Label body = as.new_label();
	as.emit_prolog(frame);
	as.emit_args_assignment(frame, args);
	as.mov(RAM, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, ram)));
	as.mov(FL, stf(offsetof(p8x32a_jst, fl)));
	as.mov(T2, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, t2)));
	as.mov(BUDGET, stf(offsetof(p8x32a_jst, budget)));
	as.xor_(TOTAL, TOTAL);
	as.bind(body);
	Emit e = { as, frame, a, j->tail, j->tail_ret };
	for (k = 0; k < len; k++) {
		bool last = k + 1 == len, next_dyn = !last && (b->dyn >> (k + 1) & 1);
		e.insn(k, b->words[k], (b->dyn >> k & 1) != 0, last, last || next_dyn);
	}
	uint32_t (*fn)(p8x32a_jst *) = NULL;
	if (j->rt.add(&fn, &code) != kErrorOk) return b;
	b->fn = fn;
	b->body = (const void *)((uintptr_t)fn + code.label_offset(body));
	b->len = len;
	return b;
}
#else
extern "C" void *p8x32a_jit_new(void) { return NULL; }
extern "C" void p8x32a_jit_free(void *jit) { (void)jit; }
extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var)
{
	(void)jit; (void)old; (void)a; (void)ix; (void)ram; (void)var;
	return NULL;
}
#endif
```

The core looks up, checks and calls the blocks, and reports every write to a translated slot; the build compiles the translator with asmjit; the driver creates it:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.c', [
(r'''static void loop_notify(p8x32a *p, uint64_t t, uint32_t pins);

''',
 r'''static void loop_notify(p8x32a *p, uint64_t t, uint32_t pins);
static void jit_write(p8x32a *p, int n, unsigned s, uint32_t old);

'''),
(r'''			if (wr && c->ram[DST(i)] != r) { c->ram[DST(i)] = r; p->loop[n].dirty = 1; }
''',
 r'''			if (wr && c->ram[DST(i)] != r) { uint32_t o = c->ram[DST(i)]; c->ram[DST(i)] = r; p->loop[n].dirty = 1; jit_write(p, n, DST(i), o); }
'''),
(r'''
/* exec + complete for local instructions of cog n, kept in locals, until one is not local or t is passed.
''',
 r'''
/* a write changed fixed code slot s of cog n from old: the blocks with it are dropped; its changed bits accumulate
   in jvar, which decides whether the slot is read at run time or left to the interpreter */
static void jit_written(p8x32a *p, int n, unsigned s, uint32_t old)
{
	int k;
	p->jvar[n][s] |= old ^ p->cog[n].ram[s];
	for (k = (int)s; k >= 0 && k > (int)s - P8X32A_JMAX; k--) {
		p8x32a_jblk *b = p->jblk[n][k];
		if (b && b->valid && (unsigned)k + b->len > s && !(b->dyn >> (s - (unsigned)k) & 1)) b->valid = 0;
	}
	p->jcode[n][s >> 3] &= (uint8_t)~(1u << (s & 7));
}

static void jit_write(p8x32a *p, int n, unsigned s, uint32_t old)
{
	if (p->jit_build && (p->jcode[n][s >> 3] >> (s & 7) & 1)) jit_written(p, n, s, old);
}

/* the translated block for ix at cog address a */
static p8x32a_jblk *jit_get(p8x32a *p, int n, unsigned a, uint32_t ix)
{
	p8x32a_jblk *b = p->jblk[n][a];
	uint32_t *var = p->jvar[n];
	unsigned k;
	if (var[a] & ~P8X32A_JDYN) return NULL;
	if (b && b->valid) {
		uint32_t x = (b->dyn & 1) ? (ix ^ b->words[0]) & ~P8X32A_JDYN : ix ^ b->words[0];
		if (!x) return b;
		var[a] |= x;
		if (var[a] & ~P8X32A_JDYN) return NULL;
	}
	b = p->jit_build(p->jit, b, a, ix, p->cog[n].ram, var);
	p->jblk[n][a] = b;
	if (b && b->valid)
		for (k = 0; k < b->len; k++)
			if (!(b->dyn >> k & 1)) p->jcode[n][(a + k) >> 3] |= (uint8_t)(1u << ((a + k) & 7));
	return b;
}

/* exec + complete for local instructions of cog n, kept in locals, until one is not local or t is passed.
'''),
(r'''		unsigned px = pc, jc = 0;
		e = &dec[(pc - 1) & 511];
''',
 r'''		unsigned px = pc, jc = 0;
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
				st.ix = ix;
				st.fl = fl & 3;
				st.inv = st.edge = 0;
				k = b->fn(&st);
			} else
				k = 0;
			if (k) {
				if (st.inv == ~0u) {
					unsigned j;
					for (j = 0; j < 512; j++)
						if (p->jblk[n][j]) p->jblk[n][j]->valid = 0;
					memset(p->jcode[n], 0, sizeof(p->jcode[n]));
				} else if (st.inv)
					jit_written(p, n, st.inv - 1, st.inv_old);
				fl = st.fl;
				px = st.px;
				jc = st.jc;
				nix = st.nix;
				t2 = st.t2;
				if (st.edge) {
					loop_edge(p, n, px, t2 - 3);
					if (l->state == LOOP_RECORD) {
						c->i = st.w; c->s = st.s; c->d = st.d; c->px = (uint16_t)px; c->nix = nix; c->cond = 1;
						pc = (px + 1) & 511;
						fl |= (px == 511) << 2;
						ix = nix;
						break;
					}
				}
				if (!jc) pc = (px + 1) & 511;
				else pc = st.pc;
				fl |= (jc || px == 511) << 2;
				ix = nix;
				if (t2 > tl) {
					if (t2 - 2 >= dis) { idled = 1; break; }
					goto limit;
				}
				continue;
			}
		}
		e = &dec[(pc - 1) & 511];
'''),
(r'''				if ((e->fl & F_WR) && wr && ram[e->dst] != r) { ram[e->dst] = r; l->dirty = 1; }
''',
 r'''				if ((e->fl & F_WR) && wr && ram[e->dst] != r) { uint32_t o = ram[e->dst]; ram[e->dst] = r; l->dirty = 1; jit_write(p, n, e->dst, o); }
'''),
(r'''			if ((e->fl & F_WR) && ram[e->dst] != r) { ram[e->dst] = r; l->dirty = 1; }
''',
 r'''			if ((e->fl & F_WR) && ram[e->dst] != r) { uint32_t o = ram[e->dst]; ram[e->dst] = r; l->dirty = 1; jit_write(p, n, e->dst, o); }
'''),
])

edit('src/cpu/p8x32a/p8x32a.h', [
(r'''
/* a decoded instruction word, valid while word matches the instruction being run */
''',
 r'''
/* Translated runs of a cog's local instructions (p8x32ajit.cpp). fn runs its block and the blocks it leads to on
   st and returns how many instructions ran, updating in st: fl (Z bit 0, C bit 1), t2 (time of the next
   instruction), budget (instructions that may still start), and loop's dirty, nins and search state as run_local
   does; for the last instruction run, pc, px (address the next word was fetched from), nix (that word), jmp (it was a
   jump that ran), jc (a jump that did not jump), edge (a backward jump run_local must pass to loop_edge), w, s, d
   (its word and operands). A block ends after a jump that runs and before a slot whose word no longer fits
   (P8X32A_JDYN); the next block runs if it is valid, fits the budget and no cancel or code change intervenes. */
#define P8X32A_JMAX 32
#define P8X32A_JDYN 0x3FFFFu /* the S and D fields: a slot whose words differ only here is read at run time */

typedef struct p8x32a_jblk p8x32a_jblk;

typedef struct p8x32a_jst {
	uint32_t *ram;
	const uint8_t *code; /* bit s: slot s is a fixed word of a block; a write that changes it is reported in inv */
	p8x32a_jblk **tab;   /* the cog's blocks by address */
	p8x32a_loop *loop;
	uint64_t t2;
	uint32_t budget, ix, fl, pc, px, nix, jmp, jc, edge, w, s, d; /* pc: of the last instruction run */
	uint32_t inv, inv_old; /* 0, or the slot + 1 and its old word; ~0: more than one */
} p8x32a_jst;

struct p8x32a_jblk {
	uint32_t (*fn)(p8x32a_jst *st);
	const void *body; /* entry for a block reached from another */
	unsigned len, valid;
	uint32_t words[P8X32A_JMAX], dyn; /* dyn: bit k set, slot k is read at run time */
};

/* translate the run at cog address a: ix, then the words after it in ram; var[k] holds the bits in which slot k
   has changed (0: fixed, only S/D: read at run time, else not translated); old is the block this one replaces */
typedef p8x32a_jblk *(*p8x32a_jit_fn)(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var);

/* a decoded instruction word, valid while word matches the instruction being run */
'''),
(r'''	p8x32a_dec dec[8][512];
} p8x32a;
''',
 r'''	p8x32a_dec dec[8][512];
	p8x32a_jit_fn jit_build; /* NULL: no translation */
	void *jit;
	p8x32a_jblk *jblk[8][512];
	uint32_t jvar[8][512];
	uint8_t jcode[8][64];
} p8x32a;
'''),
])

edit('cmake/asmjit.cmake', [
(r'''      target_sources(${target} PRIVATE ${_PINMAME_ASMJIT_LIST_DIR}/../src/windows/jit_asmjit.cpp)
''',
 r'''      target_sources(${target} PRIVATE ${_PINMAME_ASMJIT_LIST_DIR}/../src/windows/jit_asmjit.cpp
                                        ${_PINMAME_ASMJIT_LIST_DIR}/../src/cpu/p8x32a/p8x32ajit.cpp)
'''),
])

edit('src/wpc/pinheck.c', [
(r'''#include "pinheck/prop.h"
#include "pinheck/rtc.h"
''',
 r'''#include "pinheck/prop.h"
#ifdef PINMAME_JIT_ASMJIT
#include "cpu/p8x32a/p8x32ajit.h"
#endif
#include "pinheck/rtc.h"
'''),
(r'''	const char *log = getenv("PINHECK_UART1_LOG"), *plog = getenv("PINHECK_PROP_LOG");

	prop_stop_thread(&prop);
	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
	memset(&locals, 0, sizeof(locals));
''',
 r'''	const char *log = getenv("PINHECK_UART1_LOG"), *plog = getenv("PINHECK_PROP_LOG");

	prop_stop_thread(&prop);
#ifdef PINMAME_JIT_ASMJIT
	p8x32a_jit_free(prop.chip.jit);
	prop.chip.jit = NULL;
#endif
	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
	memset(&locals, 0, sizeof(locals));
'''),
(r'''	prop_init(&prop, memory_region(PINHECK_BIOSREGION), propmem);
	prop_set_log(&prop, pinheck_prop_log, NULL);
''',
 r'''	prop_init(&prop, memory_region(PINHECK_BIOSREGION), propmem);
#ifdef PINMAME_JIT_ASMJIT
	if ((!getenv("PINHECK_JIT") || atoi(getenv("PINHECK_JIT")) != 0) && (prop.chip.jit = p8x32a_jit_new()) != NULL)
		prop.chip.jit_build = p8x32a_jit_build;
#endif
	prop_set_log(&prop, pinheck_prop_log, NULL);
'''),
(r'''	prop_stop_thread(&prop);
	if (locals.uart1) fclose(locals.uart1);
''',
 r'''	prop_stop_thread(&prop);
#ifdef PINMAME_JIT_ASMJIT
	p8x32a_jit_free(prop.chip.jit);
	prop.chip.jit = NULL;
	prop.chip.jit_build = NULL;
#endif
	if (locals.uart1) fclose(locals.uart1);
'''),
])
EOF
```

- [ ] **Step 4: The suite passes, with and without the translator**

Run: `nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh > build/m9b-p8x32a.log 2>&1; tail -1 build/m9b-p8x32a.log; P8X32A_JIT=1 nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh > build/m9b-p8x32a-jit.log 2>&1; grep -a 'translated\|p8x32a:' build/m9b-p8x32a-jit.log` (about 20 minutes)
Expected:
```text
p8x32a: 228 passed, 0 failed
p8run: translated
p8x32a: 228 passed, 0 failed
```

- [ ] **Step 5: Rebuild and compare with the reference, with and without the translator**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9b.log 2>&1; tail -1 build/sdl3pinmame/m9b.log; grep -c 'p8x32ajit' build/sdl3pinmame/m9b.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video
PINHECK_JIT=0 SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract | grep -v ' ref:'
```
Expected (speeds vary with the load; the last three lines are the run without the translator):
```text
[100%] Built target sdl3pinmame
1
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.752x over 30.0 s emulated (39.9 s wall), whole run 15.4 G instructions and 2.4 s CPU per emulated s, load 5.81
bench attract ref: 0.417x over 30.0 s emulated (71.9 s wall), whole run 22.4 G instructions and 2.2 s CPU per emulated s, load 5.65
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 0.575x over 24.0 s emulated (41.7 s wall), whole run 16.7 G instructions and 2.8 s CPU per emulated s, load 6.46
bench video ref: 0.425x over 24.0 s emulated (56.4 s wall), whole run 24.2 G instructions and 2.2 s CPU per emulated s, load 4.38
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.632x over 30.0 s emulated (47.4 s wall), whole run 17.3 G instructions and 2.6 s CPU per emulated s, load 6.68
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32ajit.h src/cpu/p8x32a/p8x32ajit.cpp src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32a.h cmake/asmjit.cmake src/wpc/pinheck.c tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/run.c tests/pinheck/p8x32a/chip/jit.spin
git commit -m "p8x32a: translate runs of local cog instructions to x86-64 with asmjit"
```

### Task 7: Regression, results, and the tests bite

**Files:**
- Modify: `tests/pinheck/perf/bench.sh`, `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` (§4), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` (the m9b row, "Carried into stages 3–4")

- [ ] **Step 1: Every suite and machine check**

```sh
export SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame
grep -i warning build/sdl3pinmame/m9b.log | grep -c 'mips32\|p8x32a\|pic32mx\|prop\.c\|pinheck\.c'
for s in mips32 pic32mx storage link p8x32a display audio board; do nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh > build/m9b-$s.log 2>&1; echo "$s: exit $? $(tail -1 build/m9b-$s.log)"; done
P8X32A_JIT=1 nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh > build/m9b-p8x32a-jit.log 2>&1; echo "p8x32a translated: exit $? $(tail -1 build/m9b-p8x32a-jit.log)"
rm -rf tests/pinheck/storage/build
for c in pic32mx/pinmame_check.sh display/pinmame_display.sh audio/pinmame_audio.sh board/pinmame_board.sh; do nice -n 15 ionice -c 3 timeout 7200 tests/pinheck/$c > build/m9b-$(basename $c .sh).log 2>&1; echo "$c: exit $? $(tail -1 build/m9b-$(basename $c .sh).log)"; done
```
Expected (about an hour; the storage suite's images, about 7 GB, are deleted after it passes; once the display look is merged, run `display/pinmame_look.sh` too):
```text
0
mips32: exit 0 mips32: 208 passed, 0 failed
pic32mx: exit 0 pic32mx: 0 failed
storage: exit 0 storage: 0 failed
link: exit 0 link: 0 failed
p8x32a: exit 0 p8x32a: 228 passed, 0 failed
display: exit 0 display: 0 failed
audio: exit 0 audio: 0 failed
board: exit 0 board: 0 failed
p8x32a translated: exit 0 p8x32a: 228 passed, 0 failed
pic32mx/pinmame_check.sh: exit 0 pinmame: ok
display/pinmame_display.sh: exit 0 pinmame display: ok
audio/pinmame_audio.sh: exit 0 pinmame audio: ok
board/pinmame_board.sh: exit 0 pinmame board: ok
```

- [ ] **Step 2: Without the thread and the translator; profile**

The build with both switched off must still equal the reference; the profile shows where the time goes now. `bench.sh profile` also prints how much of the "other" share is translated Propeller code (its samples have no source file):

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/perf/bench.sh', [
(r'''	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort srcfile 2> /dev/null | python3 components.py
	exit 0
''',
 r'''	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort srcfile 2> /dev/null | python3 components.py
	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort dso 2> /dev/null | awk '/\[JIT\]/ { s += $1 } END { printf "%6.1f%%  of it translated Propeller code\n", s }'
	exit 0
'''),
])
EOF
```

```sh
PINHECK_THREADS=0 PINHECK_JIT=0 SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:'
cmake -S . -B build/sdl3pinmame-prof -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DCMAKE_C_FLAGS=-g -DCMAKE_CXX_FLAGS=-g > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame-prof -j6 > build/sdl3pinmame-prof/m9b.log 2>&1; tail -1 build/sdl3pinmame-prof/m9b.log
PINHECK_THREADS=0 SDL3PINMAME=build/sdl3pinmame-prof/sdl3pinmame nice -n 15 ionice -c 3 timeout 5400 tests/pinheck/perf/bench.sh profile attract | tail -9
```
Expected (speeds vary with the load; percentages by about a point; the profile runs one thread so that the shares add up to the work):
```text
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 0.659x over 30.0 s emulated (45.5 s wall), whole run 17.0 G instructions and 1.4 s CPU per emulated s, load 4.06
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 0.593x over 24.0 s emulated (40.4 s wall), whole run 18.4 G instructions and 1.5 s CPU per emulated s, load 3.72
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
[100%] Built target sdl3pinmame
bench attract prof: 0.686x over 30.0 s emulated (43.7 s wall), load 5.45
  54.4%  Propeller core (p8x32a)
  24.0%  PIC32 (mips32, pic32mx)
  17.0%  other (PinMAME, kernel, libraries)
   2.2%  Propeller stepping and link (prop.c)
   0.9%  display and audio (display.c, audio.c)
   0.7%  SD card and EEPROM (sd, vfat, zipsrc, eeprom, zlib)
   0.7%  board and driver (board.c, pinheck.c, rtc.c)
  14.5%  of it translated Propeller code
```

- [ ] **Step 3: The scripted game (Plan 8b)**

Plan 8b's `tests/pinheck/game/pinmame_game.sh` is the spec's third workload. When it is present (`[ -x tests/pinheck/game/pinmame_game.sh ] || echo "Plan 8b not merged: Step 3 waits"`), run Plan 9's Task 5 Step 2 commands with `build/reference/sdl3pinmame` as the reference (built in Task 1 from this plan's start, which then has the game) and require its `link2.log`, `out2.log`, `uart2.log` and `frames2.bin` to be identical; record the two runs' instruction counts and times in the roadmap's m9b row. The replay started from `1f2ef40b`, which predates Plan 8b's merge, so this step did not run there.

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
(r'''  - After stage 2 the Propeller core takes about 60% of the time and the PIC32 about 33%: the two cogs running the ROM Spin interpreter (about 16 M instructions/s each) and the MIPS32 interpreter. Real time needs the machine below one CPU second per emulated second, more than 2× less than now: stage 3 must bring the Propeller core from about 1.2 s to about 0.45 s and the PIC32 from about 0.7 s to about 0.35 s per emulated second (a JIT for the Spin-interpreter cogs and for the MIPS32 code in flash); stage 4 (the Propeller and the PIC32 on two threads) would bound the total by the larger of the two.
- **Proof for every stage:**
''',
 r'''  - After stage 2 the Propeller core takes about 60% of the time and the PIC32 about 33%: the two cogs running the ROM Spin interpreter (about 16 M instructions/s each) and the MIPS32 interpreter. Real time needs the machine below one CPU second per emulated second, more than 2× less than now: stage 3 must bring the Propeller core from about 1.2 s to about 0.45 s and the PIC32 from about 0.7 s to about 0.35 s per emulated second (a JIT for the Spin-interpreter cogs and for the MIPS32 code in flash); stage 4 (the Propeller and the PIC32 on two threads) would bound the total by the larger of the two.
- **Established by Plan 9b (stages 3 and 4):**
  - After Plan 9 the Propeller's cost was in its events and pin changes as much as in its instructions: per emulated second in attract mode about 42 M local cog instructions (about 100 host instructions each), 8 M scheduler events and 5.6 M pin change points, nearly all NCO toggles of the SD card and display link clocks. Stage 3 made the pin and counter paths, the local loop and the hub operations cheaper and exact (decoded instructions are checked by their whole word, so self-modifying code needs no invalidation), gave the MIPS32 a fast loop that hands every other instruction to the unchanged step, and translates runs of local cog instructions to x86-64 with the vendored asmjit, block by block, with writes to translated slots dropping their blocks and slots whose S or D field keeps changing read at run time. The translator is optional: CMake builds with asmjit on x86-64 use it, every other build interprets with the same results.
  - Stage 4 runs the Propeller's calls on a worker thread in their sequential order; the PIC32 side waits only where it reads Propeller state (RF13, the sound stream, the video update, NVRAM, reset, the bootloader window). Every workload stays byte-identical to the reference with the thread and the translator on and off; `PINHECK_THREADS=0` and `PINHECK_JIT=0` switch them off.
  - Host instructions per emulated second, attract / video: reference 22.4 / 24.2 G, stage 3 on one thread without the translator 16.7 / 18.2 G, with the thread and the translator 15.4 / 16.7 G. Speed on the shared machine (niced): 0.75–0.85× in attract mode and 0.58–0.72× with video and four channels, from 0.47× and 0.44× (loads 3.4–6.5).
  - Real time is not reached: the Propeller thread is the bound (71% of the work on one thread; the PIC32 thread waits for it about half its time). It must get 15–28% cheaper for 1.0× and 30–40% for 1.2×: its events (27% of the whole), pin changes (17%) and local code (26%, of which 14.5% translated). The next stage would translate hub operations and `OUTA`/`INA`/`WAITCNT` into the blocks, step the NCO clock toggles in a loop that calls only their device, link blocks with fixed targets, and let the worker run ahead of the PIC32's calls with a rule for the end of a session and a reset.
- **Proof for every stage:**
'''),
])

edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
(r'''| m9b | 9, stages 3–4: `asmjit` JIT for the Spin-interpreter cogs and the MIPS32, deterministic threading only if needed | Plan 9 | none | ≥ 1.0× real time on the Ryzen 7 7730U for attract, scripted game and video+audio, byte-identical to the reference build | after Plan 9 |
''',
 r'''| `2026-09-29-pinheck-m9b-realtime.md` | 9b, stages 3–4: exact Propeller pin, counter and event paths, decoded instructions, `asmjit` translation of local cog code (x86-64, optional), MIPS32 fast loop, the Propeller on a worker thread | Plan 9 | none | byte-identical to the reference build, thread and translator on and off; 0.75–0.85× attract, 0.58–0.72× video; ≥ 1.0× left to m9c | written |
| m9c | 9, stage 3 continued: hub operations and `OUTA`/`INA`/`WAITCNT` in translated blocks, NCO clock toggles stepped per device, blocks linked, the worker running ahead | Plan 9b | none | ≥ 1.0× real time on the Ryzen 7 7730U for attract, scripted game and video+audio, byte-identical to the reference build | after Plan 9b |
'''),
(r'''- After Plan 9 the machine takes about 2 CPU seconds per emulated second (22–24 G host instructions): the Propeller core about 60% (cogs 0 and 5 run the ROM Spin interpreter, about 16 M instructions/s each; the polling cogs sleep), the PIC32 about 33%. `tests/pinheck/perf/bench.sh` with `REFERENCE` is the determinism check for every later stage; run Plan 9 Task 5 (the scripted game) once Plan 8b is merged.
''',
 r'''- After Plan 9 the machine takes about 2 CPU seconds per emulated second (22–24 G host instructions): the Propeller core about 60% (cogs 0 and 5 run the ROM Spin interpreter, about 16 M instructions/s each; the polling cogs sleep), the PIC32 about 33%. `tests/pinheck/perf/bench.sh` with `REFERENCE` is the determinism check for every later stage; run Plan 9 Task 5 (the scripted game) once Plan 8b is merged.

## Carried into m9c

- After Plan 9b the Propeller thread is the bound: on one thread the Propeller takes about 71% of the time (events 27%, pin changes 17%, local code 26% of the whole, 14.5% of it translated) and the PIC32 24%. Real time needs the Propeller thread 15–28% cheaper, the aim of 1.2× 30–40%. `bench.sh` with `REFERENCE` (thread and translator on and off) stays the determinism check; the translated p8x32a suite (`P8X32A_JIT=1`) and `chip/jit.spin` cover the translator.
'''),
])
EOF
```

- [ ] **Step 5: Mutations**

Apply each mutation, run the named check, confirm the named failure line (other failure lines may follow), then restore the file with `git checkout <file>`. Each check is the suite's `check.sh`, run niced, with `P8X32A_JIT=1` where named; the p8x32a suite takes about 8 minutes, the mips32 and link suites a few minutes each (mutation 2 about 50: the firmware waits for its EEPROM until the machine check's timeout). The ALU check is `cc -O2 -std=c99 -Isrc/cpu/p8x32a -o build/alu_test tests/pinheck/p8x32a/alu_test.c && build/alu_test`.

| File | Replace → with | Check | Must show |
|---|---|---|---|
| `src/cpu/p8x32a/p8x32a.c` | in `pins_nco`, `else if (p->nco_nt[j] == t) p->nco_lvl[j] ^= nco_pins` → `else if (0) p->nco_lvl[j] ^= nco_pins` | p8x32a | `RTL MISMATCH chip/ctr_frq.spin` |
| `src/wpc/pinheck/prop.c` | in `pins_out`, `if (ch & (PIN_SCL \| PIN_SDA)) {` → `if (ch & PIN_SCL) {` | link | `MACHINE FAIL (fresh): stderr lacks 'boot: sign-on'` |
| `src/cpu/p8x32a/p8x32a.c` | in `alu_run`, `case 0x16: r = ((s & 511) << 23) \| (d & 0x007FFFFFu); *co = d < s; break;` → the same with `*co = 0;` | the ALU test | `ALU FAIL: alu_run op 16 s f77a418b d ea7a7302 pc 0d8 c 0 z 1: ref c5fa7302 w1 c1 z0, got c5fa7302 w1 c0 z0` |
| `src/cpu/p8x32a/p8x32a.c` | in `loop_notify`, `{ p->cog[n].ev_t = t; p->sched_gen++; }` → `p->cog[n].ev_t = t;` | p8x32a | `RTL MISMATCH chip/wake_order.spin` |
| `src/cpu/p8x32a/p8x32a.c` | in `dec_fill`, `(FWR(i) && DST(i) >= 0x1F0) \|\| (!FIM(i) && SRC(i) == 0x1F2)` → `(FWR(i) && DST(i) >= 0x1F0)` | p8x32a | `SDBOOT MISMATCH` |
| `src/cpu/mips32/mips32.c` | in `fast_run`, `else { npc += 4; tpc = npc + 4; }` → `else tpc = npc + 4;` | mips32 | `mips32: 76 passed, 132 failed` |
| `src/cpu/mips32/mips32.c` | in `fast_run`, `case 0x20: v = rs + rt; if (~(rs ^ rt) & (rs ^ v) & 0x80000000u) goto out; d = RD(op); break;` → `case 0x20: v = rs + rt; d = RD(op); break;` | mips32 | `GOLDEN FAIL golden/exceptions.S` |
| `src/wpc/pinheck/prop.c` | in `prop_p24`, the line `prop_sync(p);` → nothing | link | `PROP FAIL prop_test.c:185: v == p24[k]` |
| `src/wpc/pinheck/prop.c` | in `post`, `c->stamp = p->clock ? p->clock(p->clock_ctx) : pic;` → `c->stamp = pic;` | link | `PROP FAIL prop_test.c:203: nstamps == n && !memcmp(seq, stamps, sizeof(seq))` |
| `src/cpu/p8x32a/p8x32ajit.cpp` | in `p8x32a_jit_build`, `&& !(b->dyn >> (d - a) & 1)) len = d - a;` → `&& !(b->dyn >> (d - a) & 1)) len = len;` | p8x32a with `P8X32A_JIT=1` | `RTL MISMATCH chip/jit.spin` |
| `src/cpu/p8x32a/p8x32ajit.cpp` | in `Emit::insn`, the line `a.jne(before);` after `a.cmp(x86::eax, i & ~P8X32A_JDYN);` → nothing | p8x32a with `P8X32A_JIT=1` | `RTL MISMATCH chip/jit.spin` |
| `src/cpu/p8x32a/p8x32ajit.cpp` | in `build_tail`, the line `a.mov(x86::word_ptr(L, (int)offsetof(p8x32a_loop, head)), x86::cx);` → nothing | p8x32a with `P8X32A_JIT=1` | `SLEEPS chip/clk_idle.spin: p8run: 0 idle-loop sleeps, expected 1` |
| `src/cpu/p8x32a/p8x32a.c` | in `jit_written`, `!(b->dyn >> (s - (unsigned)k) & 1)) b->valid = 0;` → the same with `b->valid = 1;` | p8x32a with `P8X32A_JIT=1` | `RTL MISMATCH chip/jit.spin` |

Then `git status --short src tests` prints nothing.

- [ ] **Step 6: Commit**

```bash
git add tests/pinheck/perf/bench.sh docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "Spec: Milestone 9 stages 3 and 4 measured; what real time still needs"
```

## Result

Measured in the replay on the Ryzen 7 7730U while other agents' emulators and builds shared it; the load average is printed with every speed. Host instructions per emulated second (the benchmark's whole run, both threads) do not depend on the load; the wall-clock speed does.

| Build | attract: G instructions / emulated s | attract: speed (load) | video + four channels: G instructions / emulated s | video: speed (load) |
|---|---|---|---|---|
| reference (Plan 9, `1f2ef40b`) | 22.4 | 0.471× (4.5) | 24.2 | 0.444× (4.2) |
| Task 2: pins and counters | 20.8 | 0.453× (6.8) | 22.3 | 0.421× (4.9) |
| Task 3: local runs and events | 19.4 | 0.508× (4.3) | 20.8 | 0.452× (4.5) |
| Task 4: MIPS32 fast loop | 16.7 | 0.614× (5.2) | 18.2 | 0.565× (4.8) |
| Task 5: worker thread | 16.9 | 0.741× (6.2) | 18.2 | 0.648× (5.9) |
| Task 5 with `PINHECK_THREADS=0` | 16.6 | 0.632× (5.0) | | |
| Task 6: translator, with the thread | 15.4 | 0.752× (5.8); 0.845× (3.4) | 16.7 | 0.575× (6.5); 0.724× (3.7) |
| Task 6 with `PINHECK_JIT=0` | 17.3 | 0.632× (6.7) | | |
| Task 6 with neither (Task 7) | 17.0 | 0.659× (4.1) | 18.4 | 0.593× (3.7) |

All runs niced (`nice -n 15`) on the shared machine; the second Task 6 figures are a rerun of the same build at a lower load. With the thread the instruction counts include both threads' waiting loops (a few percent).

Every row is byte-identical to the reference in UART1, frames, sound and NVRAM, with the worker thread and the translator on and off.

Where the time goes after Task 6 (Task 7 Step 2, one thread so that the shares add up): the Propeller about 71% (54% in the core's C code, 14.5% in translated code, 2% in `prop.c`), the PIC32 24%, everything else 5%. Within the Propeller, by symbol: its events (the scheduler, `exec`, `complete`, `do_hub`, `alu`) about 27% of the whole, its pin changes (`flush`, `ctr_next`, the devices) about 17%, its local code about 26% (the translated blocks 14.5%, the local loop around them 11%). With the worker thread the Propeller thread is the bound: the PIC32 thread waits for it about half its time.

What real time still needs: at the quietest measured load (3.4–3.7) attract mode ran at 0.85× and video at 0.72×, so the Propeller thread must get about 15% (attract) to 28% (video) cheaper for 1.0×, and 30–40% for the aim of 1.2× with sound margin. By share, the candidates are: the events, by translating hub operations and `OUTA`/`INA`/`WAITCNT` into the blocks so that a cog's run need not return to the scheduler for each (their order against other cogs stays an event, but the issue, completion and local code around them need not); the pin changes, by stepping the NCO toggles of the SD card and display link clocks in a loop that calls only their device; the local loop around the blocks, by linking blocks with fixed targets directly; and letting the worker run ahead of the PIC32's calls (it idles about 9% of its time), which needs a rule for the Propeller's state at the end of a session and at a reset. The MIPS32 would need a translator only once the Propeller thread is faster than the PIC32 thread.

## Self-Review

**Spec coverage** (addendum §4):

| Requirement | Where |
|---|---|
| target ≥ 1.0× on the 7730U, sound never under-running | not reached: 0.75–0.85× attract, 0.58–0.72× video (Result); the bound (the Propeller thread) and what it would take are stated there and in the spec (Task 7 Step 4) |
| headless benchmark over attract mode, the scripted game, video with four channels | Plan 9's `bench.sh` after every task (attract, video); Task 7 Step 3 (the scripted game, after Plan 8b) |
| stage 3: a JIT through the vendored asmjit, only for the core the profile still blames | Task 6 (the Propeller, Rulings 2 and 5); the MIPS32 gets a fast loop instead (Task 4, Ruling 3); Tasks 2 and 3 first remove the Propeller costs a JIT cannot (Ruling 2) |
| stage 4: multi-threading only if deterministic | Task 5: the calls run in their sequential order on the worker (Ruling 6), proved by `prop_test` and the benchmark with the thread on and off |
| proof for every stage: all suites incl. RTL, spinsim, QEMU | Tasks 2, 3 Step 2/4, Task 4 Step 3, Task 5 Step 4, Task 6 Step 4 (also translated), Task 7 Step 1 |
| proof for every stage: byte-identical to the reference | `bench.sh` with `REFERENCE` after Tasks 2–6, and with the thread and the translator off (Tasks 5, 6, 7) |

**Placeholder scan:** no TBD/TODO; every code step carries its full code or an anchored edit script; every Run has its Expected output from the proving replay (speeds as measured, with the load).

**Type consistency:** `alu_run(unsigned, uint32_t, uint32_t, unsigned, int, int, int, int, int *, int *, int *)` is defined in `p8x32a.c` and called by `run_local` and `alu_test.c`; `p8x32a_jit_build(void *, p8x32a_jblk *, unsigned, uint32_t, const uint32_t *, const uint32_t *)` matches `p8x32a_jit_fn` in `p8x32a.h` and is declared in `p8x32ajit.h`, set by `pinheck.c` and `run.c`; `prop_set_clock`, `prop_stamp`, `prop_start_thread`, `prop_stop_thread`, `prop_sync` are declared in `prop.h`, defined in `prop.c` (the thread functions also without threads) and used by `pinheck.c` and `prop_test.c`; `fast_run(mips32_state *, uint64_t)` is static in `mips32.c`.

**Review Focus:** five items, each pinned by the named tests in Tasks 3, 5, 6 and 7.

**Proof:** every file and edit this plan writes was extracted from this document into a fresh worktree of `pinheck-m9` at `1f2ef40b`, task by task, and is byte-identical to the proven prototype; every Expected output above was reproduced there on a machine shared with other agents' emulators and builds (the load averages are printed with the speeds). Three tests were sharpened after the first pass because mutations 4, 9 and 11 survived it (`chip/wake_order.spin` in Task 3, the run-time slot turned from `ADD` into `SUB` in `chip/jit.spin`, the clock ahead of the calls in `prop_test.c`); a second fresh worktree replayed the plan again and reran the steps whose output they change (Task 3 Step 4, Task 5 Step 4, Task 6 Steps 2 and 4), and Task 7 Step 1 and the mutations ran on the final tree. Each mutation of Task 7 was caught with the named line. Task 7 Step 3 did not run: `1f2ef40b` predates Plan 8b's merge; its commands are Plan 9's.
